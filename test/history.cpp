// The vendored replxx history, as inspector-repl uses it.
#include "support.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>
#include <replxx.hxx>
#include <string>
#include <unistd.h>
#include <vector>

using inspector::test::TempDir;
using testing::ElementsAre;

namespace {

auto editor() -> replxx::Replxx {
  return {std::cin, std::cout, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
}

auto entries(const replxx::Replxx &editor) -> std::vector<std::string> {
  std::vector<std::string> lines;
  for (auto scan = editor.history_scan(); scan.next();) {
    lines.emplace_back(scan.get().text());
  }
  return lines;
}

// Pasted lines share a millisecond timestamp. The copy re-read from the
// file must not overtake lines entered after it.
TEST(History, SyncKeepsOrderWithinOneMillisecond) {
  const TempDir tmp;
  const std::string file = tmp.path() / "history";
  replxx::Replxx session = editor();
  for (const char *line : {"first", "second", "third"}) {
    session.history_add(line);
    session.history_sync(file);
  }
  EXPECT_THAT(entries(session), ElementsAre("first", "second", "third"));

  replxx::Replxx reloaded = editor();
  reloaded.history_load(file);
  EXPECT_THAT(entries(reloaded), ElementsAre("first", "second", "third"));
}

} // namespace
