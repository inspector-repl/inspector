// Drives a real program through `inspector-repl eval|continue|stop`.
//
// usage: inspector-command <inspector-repl> <program> [gtest flags]
#include "support.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using inspector::test::Child;
using inspector::test::TempDir;
using testing::HasSubstr;
using testing::Not;
using testing::StartsWith;

namespace {

auto paths() -> std::array<const char *, 2> & {
  static std::array<const char *, 2> both{};
  return both;
}

struct Result {
  int exit;
  std::string output;
};

// Runs inspector-repl with `args`, stdout and stderr captured.
auto repl(std::vector<std::string> args, const TempDir &tmp) -> Result {
  const auto log = tmp.path() / "output";
  args.insert(args.begin(), paths().front());
  const int exit = Child(std::move(args), log, {}, true).wait();
  std::ifstream file(log);
  return {exit, std::string(std::istreambuf_iterator<char>(file), {})};
}

class Command : public testing::Test {
  TempDir tmp;

protected:
  auto run(std::vector<std::string> args) -> Result {
    return repl(std::move(args), tmp);
  }

  void TearDown() override { run({"stop"}); }
};

TEST_F(Command, EvaluatesUntilContinued) {
  const Child program(paths().back(), {}, {"INSPECTOR_WAIT=1"});
  const auto first = run({"eval", "-t", "120", "a + 1"});
  EXPECT_EQ(first.exit, 0) << first.output;
  EXPECT_THAT(first.output, HasSubstr("stopped at"));
  EXPECT_THAT(first.output, HasSubstr("  a: int"));
  EXPECT_THAT(first.output, HasSubstr("(int) 2"));

  // The stop is described once.
  const auto again = run({"eval", "a"});
  EXPECT_EQ(again.exit, 0) << again.output;
  EXPECT_THAT(again.output, Not(HasSubstr("stopped at")));
  EXPECT_THAT(again.output, HasSubstr("(int) 1"));

  const auto broken = run({"eval", "undefined_name"});
  EXPECT_EQ(broken.exit, 1);
  EXPECT_THAT(broken.output, HasSubstr("undeclared identifier"));

  const auto json = run({"eval", "--json", "b"});
  EXPECT_EQ(json.exit, 0);
  EXPECT_THAT(json.output, StartsWith("{"));
  EXPECT_THAT(json.output, HasSubstr("Hello world!"));

  const auto resumed = run({"continue"});
  EXPECT_EQ(resumed.exit, 0) << resumed.output;
  EXPECT_EQ(program.wait(), 0);
}

TEST_F(Command, ReportsNoProgram) {
  const auto none = run({"eval", "-t", "0.2", "1"});
  EXPECT_EQ(none.exit, 2);
  EXPECT_THAT(none.output, HasSubstr("no program stopped"));
  EXPECT_EQ(run({"continue"}).exit, 2);
}

// A hung input keeps the server busy. stop must still end it.
TEST_F(Command, StopsDuringHungInput) {
  const Child program(paths().back(), {}, {"INSPECTOR_WAIT=1"});
  EXPECT_EQ(run({"eval", "-t", "120"}).exit, 0);
  EXPECT_EQ(run({"eval", "-t", "1", "while (true) {}"}).exit, 124);
  EXPECT_EQ(run({"stop"}).exit, 0);
  EXPECT_EQ(run({"eval", "-t", "0.2", "1"}).exit, 2);
  program.kill();
}

// Exiting the server resumes the programs it held.
TEST_F(Command, StopResumesPrograms) {
  const Child program(paths().back(), {}, {"INSPECTOR_WAIT=1"});
  EXPECT_EQ(run({"eval", "-t", "120"}).exit, 0);
  EXPECT_EQ(run({"stop"}).exit, 0);
  EXPECT_EQ(program.wait(), 0);
}

} // namespace

auto main(int argc, char **argv) -> int {
  testing::InitGoogleTest(&argc, argv);
  if (argc != 3) {
    std::cerr << "usage: inspector-command <inspector-repl> <program> "
                 "[gtest flags]\n";
    return 2;
  }
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic): argv
  paths() = {argv[1], argv[2]};
  // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  return RUN_ALL_TESTS();
}
