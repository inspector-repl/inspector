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

TEST(EscapeCString, EscapesBytesNotCodePoints) {
  EXPECT_EQ(escapeCString("a\"b\\\n\xc3\xa4"), "a\\042b\\134\\012\\303\\244");
}

} // namespace
