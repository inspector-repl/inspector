#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Compiler command line handling shared by `inspector wrap`,
// `inspector-c++` and `inspector prebuild`.
namespace inspector::driver {

constexpr const char *defaultOutdir = ".inspector-includes";

// Where libinspector and its headers live, relative to the running binary.
struct Paths {
  std::filesystem::path library;
  std::filesystem::path include;
};
auto paths() -> const Paths &;

// Value of environment variable `name`, or `fallback`. Only called before
// any worker thread starts.
auto env(const char *name, std::string fallback = "") -> std::string;

struct Split {
  std::vector<std::string> sources;
  // Flags that affect parsing; output and dependency options are dropped.
  std::vector<std::string> flags;
  bool compileOnly = false;
};
auto splitCommand(const std::vector<std::string> &args) -> Split;

// Flags the REPL needs to see the same code: include paths and forced
// includes (made absolute against `directory`), macros and the standard.
auto replFlags(const std::vector<std::string> &flags,
               const std::filesystem::path &directory)
    -> std::vector<std::string>;

// System include directories `compiler` uses (libstdc++, glibc, ...).
auto compilerIncludePaths(const std::string &compiler)
    -> std::vector<std::string>;

// Generates headers for the sources in `args`, then returns the command
// `inspector-c++`/`inspector wrap` runs.
auto wrappedArgv(const std::string &compiler,
                 const std::vector<std::string> &args)
    -> std::vector<std::string>;

auto cflags() -> std::string;

// Replaces the process with `args`; returns an exit code only on failure.
auto exec(const std::vector<std::string> &args) -> int;

} // namespace inspector::driver
