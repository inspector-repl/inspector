#include "connection.h"

#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>
#include <utility>

using namespace inspector;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 // SO_NOSIGPIPE is used instead
#endif

namespace {

auto sysError(const std::string &what) -> std::system_error {
  return {errno, std::generic_category(), what};
}

} // namespace

Listener::Listener(std::filesystem::path path)
    : fd(::socket(AF_UNIX, SOCK_STREAM, 0)), socket(std::move(path)) {
  if (fd < 0) {
    throw sysError("socket");
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg)
  // Private dir: the fallback base (/tmp) is shared between users.
  std::filesystem::create_directories(socket.parent_path());
  std::filesystem::permissions(socket.parent_path(),
                               std::filesystem::perms::owner_all);
  std::filesystem::remove(socket);

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const std::string &native = socket.native();
  if (native.size() >= sizeof(addr.sun_path)) {
    close(fd);
    throw std::runtime_error("socket path too long: " + native);
  }
  native.copy(std::data(addr.sun_path), native.size());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockaddr API
  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
      listen(fd, 1) < 0) {
    const int savedErrno = errno;
    close(fd);
    errno = savedErrno;
    throw sysError("bind " + native);
  }
}

Listener::~Listener() {
  close(fd);
  std::error_code ignored;
  std::filesystem::remove(socket, ignored);
}

auto Listener::accept() const -> int {
  while (true) {
    const int conn = ::accept(fd, nullptr, nullptr);
    if (conn >= 0) {
#ifdef SO_NOSIGPIPE
      int one = 1;
      setsockopt(conn, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
      return conn;
    }
    if (errno != EINTR) {
      throw sysError("accept");
    }
  }
}

Connection::~Connection() { close(fd); }

auto Connection::receive() -> std::optional<llvm::json::Object> {
  constexpr size_t chunkSize = 8192;
  size_t end = 0;
  while ((end = pending.find('\0')) == std::string::npos) {
    std::array<char, chunkSize> buf{};
    const ssize_t received = recv(fd, buf.data(), buf.size(), 0);
    if (received < 0 && errno == EINTR) {
      continue;
    }
    if (received <= 0) {
      return std::nullopt;
    }
    pending.append(buf.data(), static_cast<size_t>(received));
  }
  auto message = llvm::json::parse(pending.substr(0, end));
  pending.erase(0, end + 1);
  if (!message) {
    llvm::consumeError(message.takeError());
    return llvm::json::Object{};
  }
  if (auto *object = message->getAsObject()) {
    return std::move(*object);
  }
  return llvm::json::Object{};
}

auto Connection::send(llvm::json::Object message) const -> bool {
  std::string json;
  llvm::raw_string_ostream stream(json);
  stream << llvm::json::Value(std::move(message));
  // Include the trailing NUL as message delimiter.
  std::string_view left(json.c_str(), json.size() + 1);
  while (!left.empty()) {
    const ssize_t sent = ::send(fd, left.data(), left.size(), MSG_NOSIGNAL);
    if (sent < 0 && errno == EINTR) {
      continue;
    }
    if (sent <= 0) {
      return false;
    }
    left.remove_prefix(static_cast<size_t>(sent));
  }
  return true;
}

auto Connection::request(llvm::json::Object message)
    -> std::optional<llvm::json::Object> {
  if (!send(std::move(message))) {
    return std::nullopt;
  }
  return receive();
}
