#pragma once

#include <llvm/Support/JSON.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace inspector {

// Listening Unix socket that instrumented programs connect to.
class Listener {
  int fd;
  std::filesystem::path socket;

public:
  // Creates the private socket directory and replaces a stale socket.
  explicit Listener(std::filesystem::path path);
  ~Listener();
  Listener(const Listener &) = delete;
  auto operator=(const Listener &) -> Listener & = delete;
  Listener(Listener &&) = delete;
  auto operator=(Listener &&) -> Listener & = delete;

  [[nodiscard]] auto path() const -> const std::filesystem::path & {
    return socket;
  }
  // Blocks until a program connects; returns its socket.
  [[nodiscard]] auto accept() const -> int;
  // False if nobody connected within `timeout`.
  [[nodiscard]] auto wait(std::chrono::milliseconds timeout) const -> bool;
};

// Socket connected to `path`, or -1 if nobody listens there.
auto connectTo(const std::filesystem::path &path) -> int;

// One program stopped at a breakpoint: NUL-delimited JSON both ways.
class Connection {
  int fd;
  std::string pending;

public:
  explicit Connection(int fd) : fd(fd) {}
  ~Connection();
  Connection(const Connection &) = delete;
  auto operator=(const Connection &) -> Connection & = delete;
  Connection(Connection &&) = delete;
  auto operator=(Connection &&) -> Connection & = delete;

  // std::nullopt once the program hung up.
  auto receive() -> std::optional<llvm::json::Object>;
  [[nodiscard]] auto send(llvm::json::Object message) const -> bool;
  // send() followed by receive().
  auto request(llvm::json::Object message) -> std::optional<llvm::json::Object>;
  // False if nothing arrived within `timeout`.
  [[nodiscard]] auto wait(std::chrono::milliseconds timeout) const -> bool;
};

} // namespace inspector
