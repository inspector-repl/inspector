#pragma once

#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <vector>

// Not declared by <unistd.h> on macOS.
extern "C" char **environ; // NOLINT(readability-redundant-declaration)

namespace inspector::test {

// Private XDG_RUNTIME_DIR (and HOME) for the processes a test starts, so
// tests never meet a REPL socket or history of the user.
class TempDir {
  std::filesystem::path dir;

public:
  TempDir() {
    std::string pattern =
        (std::filesystem::temp_directory_path() / "inspector-test.XXXXXX")
            .string();
    if (mkdtemp(pattern.data()) == nullptr) {
      throw std::system_error(errno, std::generic_category(), "mkdtemp");
    }
    dir = pattern;
    for (const char *name : {"XDG_RUNTIME_DIR", "XDG_STATE_HOME", "HOME"}) {
      setenv(name, pattern.c_str(), 1);
    }
    // Build sandboxes have none; replxx then disables completion.
    setenv("TERM", "xterm", 1);
  }
  ~TempDir() {
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
  }
  TempDir(const TempDir &) = delete;
  auto operator=(const TempDir &) -> TempDir & = delete;
  TempDir(TempDir &&) = delete;
  auto operator=(TempDir &&) -> TempDir & = delete;

  [[nodiscard]] auto path() const -> const std::filesystem::path & {
    return dir;
  }
  [[nodiscard]] auto socket() const -> std::filesystem::path {
    return dir / "inspector" / "sock";
  }
};

// A started program, waited for by wait().
class Child {
  pid_t pid = 0;

public:
  // Starts `program` with the current environment plus `extraEnv`,
  // optionally writing its stderr to a file.
  explicit Child(std::string program,
                 const std::filesystem::path &stderrPath = {},
                 std::vector<std::string> extraEnv = {}) {
    std::vector<char *> env;
    for (char **var = environ; *var != nullptr; ++var) {
      env.push_back(*var);
    }
    for (std::string &var : extraEnv) {
      env.push_back(var.data());
    }
    env.push_back(nullptr);
    posix_spawn_file_actions_t actions{};
    posix_spawn_file_actions_init(&actions);
    if (!stderrPath.empty()) {
      posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                       stderrPath.c_str(),
                                       O_WRONLY | O_CREAT | O_TRUNC, 0600);
    }
    std::array<char *, 2> argv{program.data(), nullptr};
    const int err = posix_spawn(&pid, program.c_str(), &actions, nullptr,
                                argv.data(), env.data());
    posix_spawn_file_actions_destroy(&actions);
    if (err != 0) {
      throw std::system_error(err, std::generic_category(), "spawn " + program);
    }
  }
  // A pid from forkpty.
  explicit Child(pid_t pid) : pid(pid) {}

  [[nodiscard]] auto id() const -> pid_t { return pid; }

  // Exit code, or 128 + signal like a shell; -1 if waiting failed.
  [[nodiscard]] auto wait() const noexcept -> int {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
      if (errno != EINTR) {
        return -1;
      }
    }
    constexpr int signalBase = 128;
    return WIFEXITED(status) ? WEXITSTATUS(status)
                             : signalBase + WTERMSIG(status);
  }
};

} // namespace inspector::test
