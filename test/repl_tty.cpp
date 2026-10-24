// Drives inspector-repl through a pseudo terminal against a real program.
//
// usage: inspector-repl-tty <inspector-repl> <program> [gtest flags]
#include "support.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <regex>
#include <signal.h> // NOLINT(modernize-deprecated-headers): kill is POSIX
#include <sstream>
#include <stdlib.h> // NOLINT(modernize-deprecated-headers): setenv is POSIX
#include <string>
#include <sys/poll.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

using inspector::test::Child;
using inspector::test::TempDir;
using testing::ContainsRegex;
using testing::HasSubstr;

namespace {

// argv entries; std::string copies could throw before gtest takes over.
auto paths() -> std::array<const char *, 2> & {
  static std::array<const char *, 2> repl{};
  return repl;
}

auto forkTty(std::string program, int &master) -> pid_t {
  const pid_t pid = forkpty(&master, nullptr, nullptr, nullptr);
  if (pid < 0) {
    throw std::system_error(errno, std::generic_category(), "forkpty");
  }
  if (pid == 0) {
    std::array<char *, 2> argv{program.data(), nullptr};
    execv(program.c_str(), argv.data());
    constexpr int notFound = 127; // like a shell
    _exit(notFound);
  }
  return pid;
}

class Tty {
  int fd = -1;
  Child child;
  std::string seen;

  // Reads until `done(output, quiet)` returns where the wanted output
  // ends; `quiet` means nothing arrived for a moment.
  template <typename Done>
  auto readUntil(const std::string &what, Done done) -> std::string {
    static const std::regex ansi("\x1b\\[[0-9;?]*[A-Za-z]");
    constexpr std::chrono::milliseconds settle{200};
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(60);
    bool quiet = false;
    while (true) {
      std::string plain = std::regex_replace(seen, ansi, "");
      if (const auto end = done(plain, quiet)) {
        seen = plain.substr(*end);
        plain.resize(*end);
        return plain;
      }
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now());
      if (left.count() <= 0) {
        ADD_FAILURE() << "timeout waiting for " << what << " in " << plain;
        return "";
      }
      pollfd waiting{fd, POLLIN, 0};
      const int ready =
          poll(&waiting, 1, static_cast<int>(std::min(left, settle).count()));
      quiet = ready == 0;
      if (quiet) {
        continue;
      }
      constexpr size_t chunkSize = 4096;
      std::array<char, chunkSize> buf{};
      const ssize_t got = ready < 0 ? -1 : read(fd, buf.data(), buf.size());
      if (got <= 0) {
        ADD_FAILURE() << "tty closed waiting for " << what << " in " << plain;
        return "";
      }
      seen.append(buf.data(), static_cast<size_t>(got));
    }
  }

public:
  explicit Tty(const std::string &program) : child(forkTty(program, fd)) {}
  ~Tty() {
    kill(child.id(), SIGINT);
    // On macOS, exit blocks closing the terminal until its output is read.
    close(fd);
    (void)child.wait();
  }
  Tty(const Tty &) = delete;
  auto operator=(const Tty &) -> Tty & = delete;
  Tty(Tty &&) = delete;
  auto operator=(Tty &&) -> Tty & = delete;

  // Output up to and including `want`, without escape sequences; "" on
  // timeout or hang-up. What follows `want` is kept for the next call, it
  // may have arrived in the same read.
  auto expect(const std::string &want) -> std::string {
    return readUntil(
        want, [&](const std::string &plain, bool) -> std::optional<size_t> {
          const size_t found = plain.find(want);
          return found == std::string::npos
                     ? std::nullopt
                     : std::optional(found + want.size());
        });
  }

  // Output until the REPL waits at its prompt. Keys typed earlier would
  // reach the terminal in line mode, which turns Enter into a newline, and
  // replxx redraws the prompt on every key, so seeing it isn't enough.
  auto idle() -> std::string {
    return readUntil(
        "the prompt",
        [](const std::string &plain, bool quiet) -> std::optional<size_t> {
          return quiet && plain.ends_with("> ") ? std::optional(plain.size())
                                                : std::nullopt;
        });
  }

  void send(const std::string &keys) const {
    ASSERT_EQ(write(fd, keys.data(), keys.size()),
              static_cast<ssize_t>(keys.size()));
  }
};

// An $EDITOR that records its arguments and, given a file alone, replaces
// its contents with `code`.
auto fakeEditor(const TempDir &tmp, const std::string &code)
    -> std::filesystem::path {
  const auto script = tmp.path() / "editor";
  std::ofstream(script) << "#!/bin/sh\n"
                        << "echo \"$@\" > '" << (tmp.path() / "args").string()
                        << "'\n"
                        << "[ $# -eq 1 ] && printf '%s\\n' '" << code
                        << "' > \"$1\"\n"
                        << "exit 0\n";
  std::filesystem::permissions(script, std::filesystem::perms::owner_all);
  // Before the REPL starts, which inherits it; tests run no threads.
  unsetenv("VISUAL");                  // NOLINT(concurrency-mt-unsafe)
  setenv("EDITOR", script.c_str(), 1); // NOLINT(concurrency-mt-unsafe)
  return tmp.path() / "args";
}

auto slurp(const std::filesystem::path &path) -> std::string {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), {}};
}

TEST(ReplTty, ShowsMoreContext) {
  const TempDir tmp;
  Tty tty(paths().at(0));
  tty.expect("Waiting");
  const Child program(paths().at(1));
  tty.idle();

  tty.send(".where 1\r");
  const std::string shown = tty.idle();
  const std::regex numbered("\n(-->| {3}) +[0-9]+: ");
  EXPECT_EQ(
      std::distance(std::sregex_iterator(shown.begin(), shown.end(), numbered),
                    std::sregex_iterator()),
      3)
      << shown;
  EXPECT_THAT(shown, HasSubstr("--> "));

  tty.send(".continue\r");
  tty.expect("continuing program");
  EXPECT_EQ(program.wait(), 0);
}

TEST(ReplTty, EditsInputAndOpensSources) {
  const TempDir tmp;
  const auto args = fakeEditor(tmp, "6 * 7");
  Tty tty(paths().at(0));
  tty.expect("Waiting");
  const Child program(paths().at(1));
  tty.idle();

  // Without an argument the edited buffer is run.
  tty.send(".edit\r");
  EXPECT_THAT(tty.idle(), HasSubstr("(int) 42"));

  tty.send(".edit Hidden\r");
  tty.idle();
  EXPECT_THAT(slurp(args), testing::MatchesRegex("\\+[0-9]+ .*test\\.cpp\n"));
  tty.send(".edit nope\r");
  EXPECT_THAT(tty.idle(), HasSubstr("nope"));

  tty.send(".continue\r");
  tty.expect("continuing program");
  EXPECT_EQ(program.wait(), 0);
}

TEST(ReplTty, CompletesEditsAndEvaluates) {
  const TempDir tmp;
  Tty tty(paths().at(0));
  tty.expect("Waiting");
  const Child program(paths().at(1));
  // The prompt names the breakpoint.
  EXPECT_THAT(tty.idle(), ContainsRegex(R"(test\.cpp:[0-9]+> $)"));

  // Tab completion of a library member, answered by the program.
  tty.send("b.si\t");
  tty.expect("b.size");
  tty.send("()\r");
  EXPECT_THAT(tty.idle(), HasSubstr("(unsigned long) 12"));

  // Enter inside open brackets continues the input.
  tty.send("int z = (1 +\r2);\r");
  tty.idle();
  tty.send("z\r");
  EXPECT_THAT(tty.idle(), HasSubstr("(int) 3"));

  // Ctrl-P recalls the previous line like readline.
  tty.send("z + 1\r");
  EXPECT_THAT(tty.idle(), HasSubstr("(int) 4"));
  tty.send("\x10\x10\r");
  EXPECT_THAT(tty.idle(), HasSubstr("(int) 3"));

  tty.send(".continue\r");
  tty.expect("continuing program");
  EXPECT_EQ(program.wait(), 0);
}

TEST(ReplTty, ListsLocalsWithValues) {
  const TempDir tmp;
  Tty tty(paths().at(0));
  tty.expect("Waiting");
  const Child program(paths().at(1));
  tty.idle();

  // Columns: name, type, value.
  tty.send(".locals\r");
  const std::string locals = tty.idle();
  EXPECT_THAT(locals, ContainsRegex("\n  a +int += 1\r"));
  EXPECT_THAT(locals, ContainsRegex("\n  b +[^ ]+ += \"Hello world!\""));

  tty.send(".continue\r");
  tty.expect("continuing program");
  EXPECT_EQ(program.wait(), 0);
}

TEST(ReplTty, ListsMembers) {
  const TempDir tmp;
  Tty tty(paths().at(0));
  tty.expect("Waiting");
  const Child program(paths().at(1));
  tty.idle();

  // Each class with its fields and methods, the class first, then bases.
  tty.send(".ls hidden\r");
  const std::string listed = tty.idle();
  EXPECT_THAT(listed, ContainsRegex("Hidden\r\n  secret +int += 11\r"));
  EXPECT_THAT(listed, ContainsRegex("methods: peek\r\n[^\n]*Base\r"));

  // Long method lists wrap at the terminal width (80 without a size).
  tty.send(".ls b\r");
  const std::string library = tty.idle();
  tty.send(".ls a\r");
  const std::string wrapped = library + tty.idle();
  std::istringstream lines(wrapped);
  for (std::string line; std::getline(lines, line);) {
    EXPECT_LE(line.size(), 81U) << line; // with the '\r'
  }
  EXPECT_THAT(wrapped, HasSubstr("int is not a class"));

  tty.send(".continue\r");
  tty.expect("continuing program");
  EXPECT_EQ(program.wait(), 0);
}

} // namespace

auto main(int argc, char **argv) -> int {
  testing::InitGoogleTest(&argc, argv);
  if (argc != 3) {
    std::cerr << "usage: inspector-repl-tty <inspector-repl> <program>\n";
    return 2;
  }
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic): argv
  paths() = {argv[1], argv[2]};
  // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  return RUN_ALL_TESTS();
}
