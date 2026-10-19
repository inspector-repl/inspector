// Drives an instrumented program through the REPL socket protocol, the way
// inspector-repl does, without a terminal.
//
// usage: inspector-e2e <program> [gtest flags]
#include "connection.h"
#include "support.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <llvm/Support/JSON.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using inspector::Connection;
using inspector::Listener;
using inspector::test::Child;
using inspector::test::TempDir;
using testing::Contains;
using testing::Each;
using testing::HasSubstr;
using testing::Not;
using testing::StartsWith;

namespace {

auto program() -> std::string & {
  static std::string path;
  return path;
}

// The program stopped at its first breakpoint. All Session tests share it,
// in definition order, like statements typed at one prompt.
struct Stopped {
  TempDir tmp;
  Listener listener{tmp.socket()};
  Child program{::program()};
  Connection conn{listener.accept()};
  llvm::json::Object location = conn.receive().value_or(llvm::json::Object{});
  llvm::json::Object ready = conn.receive().value_or(llvm::json::Object{});

  [[nodiscard]] auto locals() const -> std::string {
    return location.getString("locals").value_or("").str();
  }
};

auto stopped() -> std::unique_ptr<Stopped> & {
  static std::unique_ptr<Stopped> instance;
  return instance;
}

auto ask(const std::string &statement) -> std::string {
  const auto reply =
      stopped()->conn.request(llvm::json::Object{{"input", statement}});
  if (!reply) {
    ADD_FAILURE() << "program hung up on " << statement;
    return "";
  }
  std::string value = reply->getString("value").value_or("").str();
  value.erase(value.find_last_not_of(" \n") + 1);
  return value;
}

auto complete(const std::string &text) -> std::vector<std::string> {
  const auto reply =
      stopped()->conn.request(llvm::json::Object{{"complete", text}});
  std::vector<std::string> names;
  if (const auto *list = reply ? reply->getArray("completions") : nullptr) {
    for (const auto &name : *list) {
      names.push_back(name.getAsString().value_or("").str());
    }
  }
  return names;
}

// Only test/test.cpp declares these; the build-system examples do not.
auto hasTestLocals() -> bool {
  return stopped()->locals().find("& arr = *(") != std::string::npos;
}

class Session : public testing::Test {
protected:
  static void SetUpTestSuite() { stopped() = std::make_unique<Stopped>(); }

  static void TearDownTestSuite() {
    (void)stopped()->conn.request(llvm::json::Object{{"input", ".quit"}});
    EXPECT_EQ(stopped()->program.wait(), 0);
    stopped().reset();
  }
};

TEST_F(Session, CapturesLocals) {
  EXPECT_THAT(stopped()->locals(), HasSubstr("& a = *("));
  EXPECT_EQ(stopped()->ready.getBoolean("ready"), true);
  EXPECT_EQ(stopped()->ready.getBoolean("error"), false);
}

TEST_F(Session, EvaluatesExpressions) {
  EXPECT_THAT(ask("a + 41"), HasSubstr("42"));
  EXPECT_THAT(ask("b.size()"), HasSubstr("12"));
  EXPECT_THAT(ask("b"), HasSubstr(R"("Hello world!")"));
}

TEST_F(Session, KeepsDeclarations) {
  EXPECT_EQ(ask("int c = a * 3;"), "");
  EXPECT_THAT(ask("c"), HasSubstr("3"));
  EXPECT_THAT(complete("c"), Contains("c"));
}

TEST_F(Session, ReportsErrors) {
  EXPECT_THAT(ask("undefined_name + 1"), HasSubstr("undeclared identifier"));
}

TEST_F(Session, KeepsTemporariesAlive) {
  EXPECT_EQ(ask("#include <optional>"), "");
  EXPECT_THAT(ask("std::optional<int>{5}.value()"), HasSubstr("(int) 5"));
  EXPECT_THAT(ask("std::optional<std::string>{b}.value()"),
              HasSubstr(R"("Hello world!")"));
}

// Input runs at run time, like code in the program.
TEST_F(Session, EvaluatesAtRunTime) {
  EXPECT_THAT(ask("__builtin_is_constant_evaluated()"), HasSubstr("false"));
}

TEST_F(Session, CompletesMembersOfLibraryTypes) {
  EXPECT_THAT(complete("b.si"), Contains("size"));
  EXPECT_THAT(complete("c + b.si"), Contains("size"));
  EXPECT_THAT(complete("b."), Each(Not(StartsWith("_"))));
}

TEST_F(Session, SpellsScopedTypes) {
  if (!hasTestLocals()) {
    GTEST_SKIP() << "program is not test/test.cpp";
  }
  EXPECT_THAT(ask("arr[1] + *ptr"), HasSubstr("9"));
  EXPECT_THAT(ask("map"),
              HasSubstr(R"((std::map<std::string, int> &) {{"k", 6}})"));
}

TEST_F(Session, ReportsExceptions) {
  if (!hasTestLocals()) {
    GTEST_SKIP() << "program is not test/test.cpp";
  }
  EXPECT_THAT(ask("throw 1;"), HasSubstr("uncaught exception"));
#if defined(__APPLE__) && INSPECTOR_LLVM_MAJOR < 23
  GTEST_SKIP() << "LLVM 22's JIT on Darwin cannot unwind exceptions thrown "
                  "inside libc++ through JIT frames";
#endif
  EXPECT_THAT(ask("b.at(99)"), HasSubstr("uncaught exception"));
}

// Without a listening REPL the program must keep running.
TEST(NoListener, ProgramContinues) {
  const TempDir tmp;
  const auto log = tmp.path() / "stderr";
  EXPECT_EQ(Child(program(), log).wait(), 0);
  std::ifstream file(log);
  EXPECT_THAT(std::string(std::istreambuf_iterator<char>(file), {}),
              HasSubstr("continuing"));
}

} // namespace

auto main(int argc, char **argv) -> int {
  testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "usage: inspector-e2e <program> [gtest flags]\n";
    return 2;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): argv
  program() = argv[1];
  return RUN_ALL_TESTS();
}
