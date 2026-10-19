#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace inspector {

struct Reply {
  std::string value;
  bool error;
};

// A message from the REPL: a statement to run, or text to complete.
struct Request {
  enum class Kind : std::uint8_t { Input, Complete };
  Kind kind;
  std::string text;
};

struct Disconnected : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// $XDG_RUNTIME_DIR/inspector/sock, falling back to $TMPDIR (macOS) or /tmp.
auto socketPath() -> std::filesystem::path;

// NUL-delimited JSON messages over the REPL's Unix socket.
class Prompt {
  int fd;
  std::string pending; // bytes received after the last NUL delimiter
  void send(const std::string &json) const;

public:
  // Throws Disconnected if no REPL is listening.
  explicit Prompt(const std::filesystem::path &path);
  ~Prompt();
  Prompt(const Prompt &) = delete;
  auto operator=(const Prompt &) -> Prompt & = delete;
  Prompt(Prompt &&) = delete;
  auto operator=(Prompt &&) -> Prompt & = delete;

  void sendInspectorLocation(const std::string &path, unsigned lineNumber,
                             const std::string &locals) const;
  void sendReady(const Reply &startup) const;
  void sendResult(const Reply &reply) const;
  void sendCompletions(const std::vector<std::string> &names,
                       const std::string &prefix) const;
  // Throws Disconnected when the REPL hangs up.
  auto getRequest() -> Request;
};

} // namespace inspector
