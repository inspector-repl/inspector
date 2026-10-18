#include "inspector/prompt.h"

#include <json/json.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace inspector;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 // SO_NOSIGPIPE is used instead
#endif

std::filesystem::path inspector::socketPath() {
  const char *dir = getenv("XDG_RUNTIME_DIR");
  if (!dir)
    dir = getenv("TMPDIR");
  return std::filesystem::path(dir ? dir : "/tmp") / "inspector" / "sock";
}

static Disconnected sysError(const std::string &what) {
  return Disconnected(what + ": " + strerror(errno));
}

Prompt::Prompt(const std::filesystem::path &path) {
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path.native().size() >= sizeof(addr.sun_path))
    throw Disconnected("socket path too long: " + path.native());
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    throw sysError("socket");
  fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    auto err = sysError("connect " + path.native());
    close(fd);
    throw err;
  }
}

Prompt::~Prompt() { close(fd); }

void Prompt::send(const std::string &json) {
  // Include the trailing NUL as message delimiter.
  const char *p = json.c_str();
  size_t left = json.size() + 1;
  while (left > 0) {
    ssize_t n = ::send(fd, p, left, MSG_NOSIGNAL);
    if (n <= 0)
      throw sysError("send");
    p += n;
    left -= n;
  }
}

static std::string toJson(const Json::Value &message) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, message);
}

void Prompt::sendInspectorLocation(const std::string &path, unsigned lineNumber,
                                   const std::string &locals) {
  Json::Value message;
  message["file"] = path;
  message["line"] = lineNumber;
  message["locals"] = locals;
  send(toJson(message));
}

void Prompt::sendReady(const Reply &startup) {
  Json::Value message;
  message["ready"] = true;
  message["value"] = startup.value;
  message["error"] = startup.error;
  send(toJson(message));
}

void Prompt::sendResult(const Reply &reply) {
  Json::Value message;
  message["value"] = reply.value;
  message["error"] = reply.error;
  send(toJson(message));
}

std::string Prompt::getCppStatement() {
  size_t end;
  while ((end = pending.find('\0')) == std::string::npos) {
    char buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0)
      throw Disconnected("connection closed");
    pending.append(buf, n);
  }
  std::stringstream ss(pending.substr(0, end));
  pending.erase(0, end + 1);
  Json::Value message;
  ss >> message;
  return message["input"].asString();
}
