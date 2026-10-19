// Drives inspector-repl through a pseudo terminal against a real program.
//
// usage: inspector-repl-tty <inspector-repl> <program> [gtest flags]
#include "support.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <regex>
#include <signal.h> // NOLINT(modernize-deprecated-headers): kill is POSIX
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
using testing::HasSubstr;

namespace {

auto paths() -> std::array<std::string, 2> & {
  static std::array<std::string, 2> repl;
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

public:
  explicit Tty(const std::string &program) : child(forkTty(program, fd)) {}
  ~Tty() {
    kill(child.id(), SIGINT);
    (void)child.wait();
    close(fd);
  }
  Tty(const Tty &) = delete;
  auto operator=(const Tty &) -> Tty & = delete;
  Tty(Tty &&) = delete;
  auto operator=(Tty &&) -> Tty & = delete;

  // Output up to and including `want`, without escape sequences; "" on
  // timeout or hang-up.
  auto expect(const std::string &want) -> std::string {
    static const std::regex ansi("\x1b\\[[0-9;?]*[A-Za-z]");
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (true) {
      std::string plain = std::regex_replace(seen, ansi, "");
      if (plain.find(want) != std::string::npos) {
        seen.clear();
        return plain;
      }
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now());
      pollfd waiting{fd, POLLIN, 0};
      constexpr size_t chunkSize = 4096;
      std::array<char, chunkSize> buf{};
      if (left.count() <= 0 ||
          poll(&waiting, 1, static_cast<int>(left.count())) <= 0) {
        ADD_FAILURE() << "timeout waiting for " << want << " in " << plain;
        return "";
      }
      const ssize_t got = read(fd, buf.data(), buf.size());
      if (got <= 0) {
        ADD_FAILURE() << "tty closed waiting for " << want << " in " << plain;
        return "";
      }
      seen.append(buf.data(), static_cast<size_t>(got));
    }
  }

  void send(const std::string &keys) const {
    ASSERT_EQ(write(fd, keys.data(), keys.size()),
              static_cast<ssize_t>(keys.size()));
  }
};

TEST(ReplTty, CompletesEditsAndEvaluates) {
  const TempDir tmp;
  Tty tty(paths().at(0));
  tty.expect("Waiting");
  const Child program(paths().at(1));
  tty.expect("[0]");

  // Tab completion of a library member, answered by the program.
  tty.send("b.si\t");
  tty.expect("b.size");
  tty.send("()\r");
  EXPECT_THAT(tty.expect("[1]"), HasSubstr("(unsigned long) 12"));

  // Enter inside open brackets continues the input.
  tty.send("int z = (1 +\r2);\r");
  tty.expect("[2]");
  tty.send("z\r");
  EXPECT_THAT(tty.expect("[3]"), HasSubstr("(int) 3"));

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
