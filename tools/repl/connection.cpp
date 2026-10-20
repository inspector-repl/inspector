#include "connection.h"

#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/poll.h>
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

auto readable(int sock, std::chrono::milliseconds timeout) -> bool {
  pollfd entry{.fd = sock, .events = POLLIN, .revents = 0};
  int ready = 0;
  while ((ready = poll(&entry, 1, static_cast<int>(timeout.count()))) < 0 &&
         errno == EINTR) {
  }
  return ready > 0;
}

void noSigpipe([[maybe_unused]] int sock) {
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

auto address(const std::filesystem::path &path) -> sockaddr_un {
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const std::string &native = path.native();
  if (native.size() >= sizeof(addr.sun_path)) {
    throw std::runtime_error("socket path too long: " + native);
  }
  native.copy(std::data(addr.sun_path), native.size());
  return addr;
}

void privateDirectory(const std::filesystem::path &dir) {
  // The fallback base (/tmp) is shared between users.
  std::filesystem::create_directories(dir);
  std::filesystem::permissions(dir, std::filesystem::perms::owner_all);
}

} // namespace

Listener::Listener(std::filesystem::path path)
    : fd(::socket(AF_UNIX, SOCK_STREAM, 0)), socket(std::move(path)) {
  if (fd < 0) {
    throw sysError("socket");
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg)
  privateDirectory(socket.parent_path());
  std::filesystem::remove(socket);

  sockaddr_un addr{};
  try {
    addr = address(socket);
  } catch (...) {
    close(fd);
    throw;
  }
  const std::string &native = socket.native();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockaddr API
  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
      listen(fd, SOMAXCONN) < 0) {
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
      noSigpipe(conn);
      return conn;
    }
    if (errno != EINTR) {
      throw sysError("accept");
    }
  }
}

auto Listener::wait(std::chrono::milliseconds timeout) const -> bool {
  return readable(fd, timeout);
}

auto inspector::connectTo(const std::filesystem::path &path) -> int {
  const sockaddr_un addr = address(path);
  const int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    throw sysError("socket");
  }
  fcntl(sock, F_SETFD, FD_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg)
  noSigpipe(sock);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockaddr API
  if (connect(sock, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) <
      0) {
    close(sock);
    return -1;
  }
  return sock;
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

auto Connection::wait(std::chrono::milliseconds timeout) const -> bool {
  return !pending.empty() || readable(fd, timeout);
}

auto Connection::request(llvm::json::Object message)
    -> std::optional<llvm::json::Object> {
  if (!send(std::move(message))) {
    return std::nullopt;
  }
  return receive();
}
