// `inspector-c++`: a drop-in CXX for make, autotools and meson.
#include "driver.h"

#include <exception>
#include <iostream>
#include <iterator>

auto main(int argc, char **argv) -> int {
  try {
    return inspector::driver::exec(inspector::driver::wrappedArgv(
        inspector::driver::env("INSPECTOR_CXX", "c++"),
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        {std::next(argv), std::next(argv, argc)}));
  } catch (const std::exception &e) {
    std::cerr << "inspector-c++: " << e.what() << "\n";
    return 1;
  }
}
