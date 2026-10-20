#include "command.h"

#include "connection.h"
#include "inspector/prompt.h"
#include "source.h"

#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <signal.h> // NOLINT(modernize-deprecated-headers): kill is POSIX
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace inspector {
namespace {

constexpr std::string_view usage =
    R"(usage: inspector-repl                      interactive REPL
       inspector-repl eval [-t S] [--json] [CODE | -]
       inspector-repl continue [--json]
       inspector-repl stop

eval waits up to S seconds (default 30) for a program to stop at
INSPECTOR, then runs CODE in it, or stdin with '-'. Without CODE it
describes the stop. continue resumes the program. A background server,
started by the first command, keeps programs stopped in between. stop
ends it and resumes them.

Exit codes: 0 ok, 1 error in CODE, 2 no program stopped, 3 program
disconnected, 124 timed out.
)";

constexpr std::chrono::minutes idleLimit{30};
constexpr std::chrono::seconds startLimit{5};
constexpr std::chrono::seconds replyGrace{2};
constexpr std::chrono::milliseconds startPoll{20};
constexpr double defaultTimeout = 30;

constexpr int exitOk = 0;
constexpr int exitFailed = 1;
constexpr int exitNoProgram = 2;
constexpr int exitDisconnected = 3;
constexpr int exitTimedOut = 124; // like timeout(1)

auto controlPath() -> std::filesystem::path {
  return socketPath().parent_path() / "control";
}

auto logPath() -> std::filesystem::path { return stateDir() / "server.log"; }

// `stop` signals the server, because a hung input keeps it from reading
// requests.
auto pidPath() -> std::filesystem::path {
  return socketPath().parent_path() / "server.pid";
}

auto status(llvm::StringRef text) -> llvm::json::Object {
  return llvm::json::Object{{"status", text}};
}

struct Stop {
  std::unique_ptr<Connection> conn;
  llvm::json::Object info;
  bool announced = false;
};

// Holds at most one stopped program. Others queue in the listen backlog
// until it continues.
class Server {
  Listener programs{socketPath()};
  Listener control{controlPath()};
  std::optional<Stop> stop;

  // The held program, or the next one stopping within `timeout`.
  auto acquire(std::chrono::milliseconds timeout) -> Stop * {
    if (stop || !programs.wait(timeout)) {
      return stop ? &*stop : nullptr;
    }
    auto conn = std::make_unique<Connection>(programs.accept());
    const auto location = conn->receive();
    const auto ready = location ? conn->receive() : std::nullopt;
    if (!ready) {
      return nullptr;
    }
    llvm::json::Array locals;
    for (const Local &local :
         parseLocals(location->getString("locals").value_or("").str())) {
      locals.push_back(
          llvm::json::Object{{"name", local.name}, {"type", local.type}});
    }
    return &stop.emplace(
        Stop{.conn = std::move(conn),
             .info = llvm::json::Object{
                 {"file", location->getString("file").value_or("").str()},
                 {"line", location->getInteger("line").value_or(0)},
                 {"locals", std::move(locals)},
                 {"startup", ready->getString("value").value_or("").str()}}});
  }

  auto evaluate(const llvm::json::Object &request) -> llvm::json::Object {
    const std::string code = request.getString("eval").value_or("").str();
    const llvm::StringRef trimmed = llvm::StringRef(code).trim();
    if (trimmed == ".quit" || trimmed == ".abort") {
      return llvm::json::Object{
          {"status", "error"},
          {"value", "use `inspector-repl continue` to resume"}};
    }
    Stop *held = acquire(
        std::chrono::milliseconds(request.getInteger("timeout").value_or(0)));
    if (held == nullptr) {
      return status("none");
    }
    llvm::json::Object reply;
    if (!held->announced || code.empty()) {
      reply.try_emplace("stop", llvm::json::Object(held->info));
      held->announced = true;
    }
    if (code.empty()) {
      reply.try_emplace("status", "ok");
      return reply;
    }
    const auto result =
        held->conn->request(llvm::json::Object{{"input", code}});
    if (!result) {
      stop.reset();
      reply.try_emplace("status", "disconnected");
      return reply;
    }
    reply.try_emplace("value", result->getString("value").value_or("").str());
    reply.try_emplace(
        "status", result->getBoolean("error").value_or(false) ? "error" : "ok");
    return reply;
  }

  auto resume() -> llvm::json::Object {
    Stop *held = acquire(std::chrono::milliseconds::zero());
    if (held == nullptr) {
      return status("none");
    }
    held->conn->request(llvm::json::Object{{"input", ".quit"}});
    stop.reset();
    return llvm::json::Object{{"status", "ok"}, {"continued", true}};
  }

public:
  // Exiting closes every socket, which resumes the held program.
  void run() {
    while (control.wait(idleLimit)) {
      Connection client(control.accept());
      const auto request = client.receive();
      if (!request) {
        continue;
      }
      (void)client.send(
          request->get("continue") != nullptr ? resume() : evaluate(*request));
    }
  }
};

auto listening(const std::filesystem::path &path) -> bool {
  const int sock = connectTo(path);
  if (sock >= 0) {
    close(sock);
  }
  return sock >= 0;
}

// Double fork, so the server outlives the command and is no child of its
// caller, with output in the log.
void startServer() {
  std::filesystem::create_directories(stateDir());
  const pid_t child = fork();
  if (child < 0) {
    throw std::system_error(errno, std::generic_category(), "fork");
  }
  if (child == 0) {
    setsid();
    if (fork() != 0) {
      _exit(0);
    }
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg): open(2)
    const int null = open("/dev/null", O_RDONLY);
    const int log =
        open(logPath().c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    // NOLINTEND(cppcoreguidelines-pro-type-vararg)
    if (null < 0 || log < 0) {
      _exit(1);
    }
    dup2(null, STDIN_FILENO);
    dup2(log, STDOUT_FILENO);
    dup2(log, STDERR_FILENO);
    close(null);
    close(log);
    int status = 1;
    try {
      if (listening(socketPath())) {
        std::cerr << "another REPL listens on " << socketPath().string()
                  << "\n";
      } else {
        Server server;
        std::ofstream(pidPath()) << getpid() << "\n";
        server.run();
        status = 0;
      }
    } catch (const std::exception &e) {
      std::cerr << "inspector-repl server: " << e.what() << "\n";
    }
    _exit(status);
  }
  waitpid(child, nullptr, 0);
}

// --- commands ---

auto server(bool start) -> std::unique_ptr<Connection> {
  if (const int sock = connectTo(controlPath()); sock >= 0) {
    return std::make_unique<Connection>(sock);
  }
  if (!start) {
    return nullptr;
  }
  startServer();
  const auto deadline = std::chrono::steady_clock::now() + startLimit;
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(startPoll);
    if (const int sock = connectTo(controlPath()); sock >= 0) {
      return std::make_unique<Connection>(sock);
    }
  }
  throw std::runtime_error(
      "the background server did not start. Is an interactive inspector-repl "
      "running? See " +
      logPath().string());
}

void printStop(const llvm::json::Object &stop) {
  std::cout << "stopped at " << stop.getString("file").value_or("").str() << ":"
            << stop.getInteger("line").value_or(0) << "\n";
  if (const auto *locals = stop.getArray("locals")) {
    std::cout << "locals:\n";
    for (const auto &local : *locals) {
      if (const auto *entry = local.getAsObject()) {
        std::cout << "  " << entry->getString("name").value_or("").str() << ": "
                  << entry->getString("type").value_or("").str() << "\n";
      }
    }
  }
  const llvm::StringRef startup =
      stop.getString("startup").value_or("").rtrim();
  if (!startup.empty()) {
    std::cout << startup.str() << "\n";
  }
}

auto exitCode(llvm::StringRef status) -> int {
  if (status == "ok") {
    return exitOk;
  }
  if (status == "error") {
    return exitFailed;
  }
  if (status == "none") {
    return exitNoProgram;
  }
  if (status == "disconnected") {
    return exitDisconnected;
  }
  return exitTimedOut;
}

auto report(const llvm::json::Object &reply, bool json) -> int {
  const llvm::StringRef status = reply.getString("status").value_or("");
  if (json) {
    std::string text;
    llvm::raw_string_ostream stream(text);
    stream << llvm::json::Value(llvm::json::Object(reply));
    std::cout << text << "\n";
    return exitCode(status);
  }
  if (const auto *stop = reply.getObject("stop")) {
    printStop(*stop);
  }
  const llvm::StringRef value = reply.getString("value").value_or("").rtrim();
  if (!value.empty()) {
    std::cout << value.str() << "\n";
  }
  if (status == "ok" && reply.get("continued") != nullptr) {
    std::cout << "continued\n";
  } else if (status == "none") {
    std::cerr << "no program stopped at INSPECTOR\n";
  } else if (status == "disconnected") {
    std::cerr << "program disconnected\n";
  } else if (status == "timeout") {
    std::cerr << "timed out. The input may still be running.\n";
  }
  return exitCode(status);
}

auto send(llvm::json::Object request, std::chrono::milliseconds timeout,
          bool json) -> int {
  const auto conn = server(true);
  if (!conn->send(std::move(request)) || !conn->wait(timeout)) {
    return report(status("timeout"), json);
  }
  const auto reply = conn->receive();
  return report(reply ? *reply : status("timeout"), json);
}

auto evalCommand(const std::vector<std::string> &args) -> int {
  double seconds = defaultTimeout;
  bool json = false;
  std::vector<std::string> code;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string &arg = args.at(i);
    if ((arg == "-t" || arg == "--timeout") && i + 1 < args.size()) {
      seconds = std::stod(args.at(++i));
    } else if (arg == "--json") {
      json = true;
    } else if (arg == "-") {
      code.emplace_back(std::istreambuf_iterator<char>(std::cin),
                        std::istreambuf_iterator<char>());
    } else {
      code.push_back(arg);
    }
  }
  const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::duration<double>(seconds));
  return send(
      llvm::json::Object{{"eval", llvm::join(code, " ")},
                         {"timeout", static_cast<int64_t>(timeout.count())}},
      timeout + replyGrace, json);
}

auto continueCommand(const std::vector<std::string> &args) -> int {
  return send(llvm::json::Object{{"continue", true}},
              std::chrono::seconds(static_cast<int>(defaultTimeout)),
              std::ranges::find(args, "--json") != args.end());
}

auto stopCommand() -> int {
  // A live server keeps the control socket, so the pid can't be stale.
  if (!listening(controlPath())) {
    return exitOk;
  }
  pid_t pid = 0;
  std::ifstream(pidPath()) >> pid;
  if (pid <= 0 || kill(pid, SIGTERM) < 0) {
    std::cerr << "cannot stop the server, see " << pidPath().string() << "\n";
    return exitFailed;
  }
  const auto deadline = std::chrono::steady_clock::now() + startLimit;
  while (listening(controlPath())) {
    if (std::chrono::steady_clock::now() > deadline) {
      std::cerr << "the server (pid " << pid << ") did not exit\n";
      return exitFailed;
    }
    std::this_thread::sleep_for(startPoll);
  }
  // SIGTERM skipped the server's cleanup.
  std::error_code ignored;
  for (const auto &path : {socketPath(), controlPath(), pidPath()}) {
    std::filesystem::remove(path, ignored);
  }
  return exitOk;
}

} // namespace

auto serverRunning() -> bool { return listening(controlPath()); }

auto runCommand(const std::vector<std::string> &args) -> int {
  const std::string command = args.empty() ? "" : args.front();
  const std::vector<std::string> rest(
      args.empty() ? args.end() : std::next(args.begin()), args.end());
  if (command == "eval") {
    return evalCommand(rest);
  }
  if (command == "continue") {
    return continueCommand(rest);
  }
  if (command == "stop") {
    return stopCommand();
  }
  std::cerr << usage;
  return command == "-h" || command == "--help" ? exitOk : exitFailed;
}

} // namespace inspector
