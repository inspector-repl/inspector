#include "source.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

auto inspector::stateDir() -> std::filesystem::path {
  // NOLINTBEGIN(concurrency-mt-unsafe): nothing modifies the environment
  const char *state = std::getenv("XDG_STATE_HOME");
  const char *home = std::getenv("HOME");
  // NOLINTEND(concurrency-mt-unsafe)
  const std::filesystem::path base =
      state != nullptr && *state != '\0'
          ? std::filesystem::path(state)
          : std::filesystem::path(home != nullptr ? home : ".") / ".local" /
                "state";
  return base / "inspector";
}

auto inspector::readLines(const std::string &path) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::ifstream file(path);
  for (std::string line; std::getline(file, line);) {
    lines.push_back(std::move(line));
  }
  return lines;
}

auto inspector::parseLocals(const std::string &prelude) -> std::vector<Local> {
  static const std::regex decl(R"(^inspector_repl::type<(.*)>& (\w+) = \*\()");
  std::vector<Local> out;
  std::istringstream lines(prelude);
  for (std::string line; std::getline(lines, line);) {
    std::smatch match;
    if (std::regex_search(line, match, decl)) {
      out.push_back({match.str(2), match.str(1)});
    }
  }
  return out;
}
