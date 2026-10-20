#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace inspector {

// $XDG_STATE_HOME/inspector, for history and logs.
auto stateDir() -> std::filesystem::path;

auto readLines(const std::string &path) -> std::vector<std::string>;

struct Local {
  std::string name;
  std::string type;
};

// Recovers (name, type) from the generated reference prelude.
auto parseLocals(const std::string &prelude) -> std::vector<Local>;

} // namespace inspector
