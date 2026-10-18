#include "inspector/prompt.h"

#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

using namespace inspector;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 // SO_NOSIGPIPE is used instead
#endif

namespace {

auto sysError(const std::string &what) -> Disconnected {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): message only, races are harmless
  return Disconnected{what + ": " + strerror(errno)};
}

auto toJson(llvm::json::Object message) -> std::string {
  std::string out;
  llvm::raw_string_ostream stream(out);
  stream << llvm::json::Value(std::move(message));
  return out;
}

} // namespace

auto inspector::socketPath() -> std::filesystem::path {
  // NOLINTBEGIN(concurrency-mt-unsafe): nothing modifies the environment
  const char *dir = getenv("XDG_RUNTIME_DIR");
  if (dir == nullptr) {
    dir = getenv("TMPDIR");
  }
  // NOLINTEND(concurrency-mt-unsafe)
  return std::filesystem::path(dir != nullptr ? dir : "/tmp") / "inspector" /
         "sock";
}

Prompt::Prompt(const std::filesystem::path &path)
    : fd(socket(AF_UNIX, SOCK_STREAM, 0)) {
  if (fd < 0) {
    throw sysError("socket");
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg)
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const std::string &native = path.native();
  if (native.size() >= sizeof(addr.sun_path)) {
    close(fd);
    throw Disconnected{"socket path too long: " + native};
  }
  native.copy(std::data(addr.sun_path), native.size());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockaddr API
  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    const int savedErrno = errno;
    close(fd);
    errno = savedErrno;
    throw sysError("connect " + native);
  }
}

Prompt::~Prompt() { close(fd); }

void Prompt::send(const std::string &json) const {
  // Include the trailing NUL as message delimiter.
  std::string_view left(json.c_str(), json.size() + 1);
  while (!left.empty()) {
    const ssize_t sent = ::send(fd, left.data(), left.size(), MSG_NOSIGNAL);
    if (sent <= 0) {
      throw sysError("send");
    }
    left.remove_prefix(static_cast<size_t>(sent));
  }
}

void Prompt::sendInspectorLocation(const std::string &path, unsigned lineNumber,
                                   const std::string &locals) const {
  send(toJson(llvm::json::Object{
      {"file", path}, {"line", lineNumber}, {"locals", locals}}));
}

void Prompt::sendReady(const Reply &startup) const {
  send(toJson(llvm::json::Object{
      {"ready", true}, {"value", startup.value}, {"error", startup.error}}));
}

void Prompt::sendResult(const Reply &reply) const {
  send(toJson(
      llvm::json::Object{{"value", reply.value}, {"error", reply.error}}));
}

auto Prompt::getCppStatement() -> std::string {
  constexpr size_t chunkSize = 4096;
  size_t end = 0;
  while ((end = pending.find('\0')) == std::string::npos) {
    std::array<char, chunkSize> buf{};
    const ssize_t received = recv(fd, buf.data(), buf.size(), 0);
    if (received <= 0) {
      throw Disconnected{"connection closed"};
    }
    pending.append(buf.data(), static_cast<size_t>(received));
  }
  auto message = llvm::json::parse(pending.substr(0, end));
  pending.erase(0, end + 1);
  if (!message) {
    throw Disconnected{"invalid message: " +
                       llvm::toString(message.takeError())};
  }
  const auto *object = message->getAsObject();
  const auto input =
      object != nullptr ? object->getString("input") : std::nullopt;
  return input.value_or("").str();
}
