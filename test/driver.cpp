// Command line handling of inspector-c++ / inspector prebuild.
#include "driver.h"
#include "prebuild.h"
#include "support.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using inspector::driver::replFlags;
using inspector::driver::splitCommand;
using inspector::prebuild::escapeCString;
using testing::ElementsAre;

TEST(SplitCommand, SeparatesSourcesFromParseFlags) {
  const auto split = splitCommand({"-c", "-o", "out.o", "-MF", "dep.d", "-I",
                                   "inc", "-DX=1", "main.cpp", "lib.a"});
  EXPECT_THAT(split.sources, ElementsAre("main.cpp"));
  EXPECT_THAT(split.flags, ElementsAre("-I", "inc", "-DX=1"));
  EXPECT_TRUE(split.compileOnly);
}

TEST(SplitCommand, LinkStepIsNotCompileOnly) {
  EXPECT_FALSE(splitCommand({"main.o", "-o", "app"}).compileOnly);
}

TEST(ReplFlags, MakesPathsAbsoluteAndKeepsMacros) {
  EXPECT_THAT(
      replFlags({"-Iinc", "-isystem", "sys", "-isystemrel", "-include", "pre.h",
                 "-imacros", "m.h", "-DX", "-std=c++20", "-O2", "-Wall"},
                "/src"),
      ElementsAre("-I/src/inc", "-isystem", "/src/sys", "-isystem/src/rel",
                  "-include", "/src/pre.h", "-imacros", "/src/m.h", "-DX",
                  "-std=c++20"));
}

// Clang brings its own builtin headers. GCC's use GCC-only builtins and
// must not shadow them.
TEST(IncludeSearch, DropsCompilerBuiltinDirectories) {
  const std::string verbose =
      "ignoring nonexistent directory \"/x\"\n"
      "#include <...> search starts here:\n"
      " /gcc/include/c++/15\n"
      " /gcc/lib/gcc/x86_64-linux-gnu/15/include\n"
      " /gcc/lib/gcc/x86_64-linux-gnu/15/include-fixed\n"
      " /clang/lib/clang/21/include\n"
      " /glibc/include\n"
      " /System/Library/Frameworks (framework directory)\n"
      "End of search list.\n";
  EXPECT_THAT(inspector::driver::parseIncludeSearch(verbose),
              ElementsAre("/gcc/include/c++/15", "/glibc/include",
                          "/System/Library/Frameworks"));
}

// The source sits five directories above the compiler's working directory,
// so __FILE__ in INSPECTOR starts with ../../../../../.
TEST(Generate, KeepsHeadersOfParentDirectorySourcesInOutdir) {
  const inspector::test::TempDir tmp;
  const fs::path cwd = tmp.path() / "a" / "b" / "c" / "d" / "e";
  fs::create_directories(cwd);
  std::ofstream(tmp.path() / "t.cpp")
      << "int main() {\n  int x = 1;\n#include INSPECTOR\n  return x;\n}\n";
  const fs::path outdir = cwd / ".inspector-includes";

  const auto written = inspector::prebuild::generate(
      {.directory = cwd,
       .source = "../../../../../t.cpp",
       .flags = {"-I" INSPECTOR_SOURCE_INCLUDE_DIR},
       .outdir = outdir});

  ASSERT_THAT(written, testing::SizeIs(1));
  const fs::path header = fs::weakly_canonical(written.front());
  EXPECT_THAT(header.string(),
              testing::StartsWith(fs::weakly_canonical(outdir).string()));
}

// Meson and CMake unity builds compile a generated file that only
// #includes the real sources.
TEST(Generate, FindsBreakpointsInUnityBuildSources) {
  const inspector::test::TempDir tmp;
  std::ofstream(tmp.path() / "t.cpp")
      << "int main() {\n  int x = 1;\n#include INSPECTOR\n  return x;\n}\n";
  std::ofstream(tmp.path() / "unity.cpp")
      << "#include<" << (tmp.path() / "t.cpp").string() << ">\n";

  const auto written = inspector::prebuild::generate(
      {.directory = tmp.path(),
       .source = "unity.cpp",
       .flags = {"-I" INSPECTOR_SOURCE_INCLUDE_DIR},
       .outdir = tmp.path() / ".inspector-includes"});

  EXPECT_THAT(written, testing::SizeIs(1));
}

// Meson's unity files include sources as </abs/build/../src/x.cc>, and
// __FILE__ keeps that spelling, so the header must be found under it.
TEST(Generate, NamesHeadersAfterTheIncludeSpelling) {
  const inspector::test::TempDir tmp;
  fs::create_directories(tmp.path() / "src");
  fs::create_directories(tmp.path() / "build");
  std::ofstream(tmp.path() / "src" / "t.cpp")
      << "int main() {\n  int x = 1;\n#include INSPECTOR\n  return x;\n}\n";
  const std::string spelled =
      (tmp.path() / "build" / ".." / "src" / "t.cpp").string();
  std::ofstream(tmp.path() / "build" / "unity.cpp")
      << "#include<" << spelled << ">\n";
  const fs::path outdir = tmp.path() / "build" / ".inspector-includes";

  const auto written = inspector::prebuild::generate(
      {.directory = tmp.path() / "build",
       .source = "unity.cpp",
       .flags = {"-I" INSPECTOR_SOURCE_INCLUDE_DIR},
       .outdir = outdir});

  ASSERT_THAT(written, testing::SizeIs(1));
  EXPECT_TRUE(fs::exists(inspector::driver::includeRoot(outdir) / "inspector" /
                         ("\"" + spelled + "\"-3")));
}

TEST(EscapeCString, EscapesBytesNotCodePoints) {
  EXPECT_EQ(escapeCString("a\"b\\\n\xc3\xa4"), "a\\042b\\134\\012\\303\\244");
}

} // namespace
