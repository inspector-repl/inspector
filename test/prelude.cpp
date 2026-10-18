#include "inspector/repl_prelude.h"

#include <map>
#include <string>
#include <vector>

// Instantiates the REPL prelude at build time so compiler warnings and
// clang-tidy see it; at runtime it is only parsed by the interpreter.
auto main() -> int {
  try {
    std::string out;
    inspector_repl::print(out,
                          std::vector<std::map<std::string, int>>{{{"a", 1}}});
    return out == R"({{{"a", 1}}})" ? 0 : 1;
  } catch (...) {
    return 1;
  }
}
