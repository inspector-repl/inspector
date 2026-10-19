// Command line handling of inspector-c++ / inspector prebuild.
#include "driver.h"
#include "prebuild.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

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

TEST(EscapeCString, EscapesBytesNotCodePoints) {
  EXPECT_EQ(escapeCString("a\"b\\\n\xc3\xa4"), "a\\042b\\134\\012\\303\\244");
}

} // namespace
