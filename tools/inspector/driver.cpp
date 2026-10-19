#include "driver.h"

#include "prebuild.h"

#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Program.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace inspector::driver {

namespace {

// Options whose value is a separate argv entry.
constexpr std::array<std::string_view, 20> argFlags = {
    "-o",       "-I",       "-D",      "-U",         "-include",
    "-imacros", "-isystem", "-iquote", "-idirafter", "-isysroot",
    "-x",       "-MF",      "-MT",     "-MQ",        "-Xclang",
    "-target",  "--target", "-arch",   "-L",         "-l"};
// Output and dependency options, irrelevant or harmful for parsing.
constexpr std::array<std::string_view, 11> dropped = {
    "-o", "-MF", "-MT", "-MQ", "-c", "-S", "-E", "-MD", "-MMD", "-M", "-MM"};
constexpr std::array<std::string_view, 6> compileOnlyFlags = {
    "-c", "-S", "-E", "-M", "-MM", "-fsyntax-only"};
constexpr std::array<std::string_view, 7> sourceSuffixes = {
    ".cpp", ".cc", ".cxx", ".c++", ".C", ".cp", ".CPP"};
// Path-valued flags the REPL needs; longest first so -I doesn't match
// -isystem.
constexpr std::array<std::string_view, 6> replPathFlags = {
    "-isystem", "-idirafter", "-imacros", "-include", "-iquote", "-I"};

auto contains(const auto &list, std::string_view arg) -> bool {
  return std::ranges::find(list, arg) != list.end();
}

auto absoluteIn(const fs::path &directory, llvm::StringRef path)
    -> std::string {
  return (directory / path.str()).lexically_normal().string();
}

auto findPaths() -> Paths {
  static int anchor = 0;
  const fs::path dir =
      fs::path(llvm::sys::fs::getMainExecutable(nullptr, &anchor))
          .parent_path();
  // In the build tree the library sits in lib/ next to the binaries and
  // include/ links to the sources.
  if (fs::exists(dir / "lib" / INSPECTOR_LIBRARY_FILE)) {
    return {dir, dir / "lib", dir / "include"};
  }
  return {dir, (dir / INSPECTOR_REL_LIBDIR).lexically_normal(),
          (dir / INSPECTOR_REL_INCLUDEDIR).lexically_normal()};
}

} // namespace

auto paths() -> const Paths & {
  static const Paths found = findPaths();
  return found;
}

auto env(const char *name, std::string fallback) -> std::string {
  const char *value = getenv(name); // NOLINT(concurrency-mt-unsafe)
  return value != nullptr ? std::string(value) : std::move(fallback);
}

auto isSource(std::string_view path) -> bool {
  return std::ranges::any_of(
      sourceSuffixes,
      [&](std::string_view suffix) -> bool { return path.ends_with(suffix); });
}

auto splitCommand(const std::vector<std::string> &args) -> Split {
  Split split;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string &arg = args.at(i);
    split.compileOnly |= contains(compileOnlyFlags, arg);
    const bool isFlag = arg.front() == '-' || arg.front() == '@';
    const bool keep = isFlag && !contains(dropped, arg);
    if (keep) {
      split.flags.push_back(arg);
    }
    if (contains(argFlags, arg) && i + 1 < args.size()) {
      const std::string &value = args.at(++i);
      if (keep) {
        split.flags.push_back(value);
      }
    } else if (!isFlag && isSource(arg)) {
      split.sources.push_back(arg);
    }
  }
  return split;
}

auto replFlags(const std::vector<std::string> &flags, const fs::path &directory)
    -> std::vector<std::string> {
  std::vector<std::string> out;
  for (size_t i = 0; i < flags.size(); ++i) {
    const llvm::StringRef flag = flags.at(i);
    const auto *pathFlag = std::ranges::find_if(
        replPathFlags, [&](std::string_view prefix) -> bool {
          return flag.starts_with(prefix);
        });
    const bool separate = contains(argFlags, flag) && i + 1 < flags.size();
    // The REPL runs in the program's working directory, not the compiler's.
    if (pathFlag != replPathFlags.end()) {
      if (separate) {
        out.emplace_back(*pathFlag);
        out.push_back(absoluteIn(directory, flags.at(++i)));
      } else {
        out.push_back(std::string(*pathFlag) +
                      absoluteIn(directory, flag.drop_front(pathFlag->size())));
      }
    } else if (flag.starts_with("-D") || flag.starts_with("-U") ||
               flag.starts_with("-std=")) {
      out.push_back(flag.str());
      if (separate) {
        out.push_back(flags.at(++i));
      }
    }
  }
  return out;
}

auto compilerIncludePaths(const std::string &compiler)
    -> std::vector<std::string> {
  auto program = llvm::sys::findProgramByName(compiler);
  llvm::SmallString<0> log;
  if (!program || llvm::sys::fs::createTemporaryFile("inspector", "txt", log)) {
    return {};
  }
  const std::array<llvm::StringRef, 6> args = {*program, "-E", "-x",
                                               "c++",    "-",  "-v"};
  const std::array<std::optional<llvm::StringRef>, 3> redirects = {"", "",
                                                                   log.str()};
  const int status =
      llvm::sys::ExecuteAndWait(*program, args, std::nullopt, redirects);
  auto output = llvm::MemoryBuffer::getFile(log);
  // A leftover temp file is harmless.
  [[maybe_unused]] const std::error_code removed = llvm::sys::fs::remove(log);
  if (status != 0 || !output) {
    return {};
  }
  std::vector<std::string> found;
  for (auto &dir : parseIncludeSearch((*output)->getBuffer())) {
    if (fs::is_directory(dir)) {
      found.push_back(std::move(dir));
    }
  }
  return found;
}

auto parseIncludeSearch(llvm::StringRef verbose) -> std::vector<std::string> {
  verbose = verbose.split("#include <...> search starts here:\n").second;
  verbose = verbose.split("End of search list.").first;
  llvm::SmallVector<llvm::StringRef> lines;
  verbose.split(lines, '\n', -1, /*KeepEmpty=*/false);
  std::vector<std::string> dirs;
  for (llvm::StringRef line : lines) {
    line = line.trim();
    line.consume_back(" (framework directory)");
    if (!line.contains("/lib/gcc/") && !line.contains("/lib/clang/")) {
      dirs.push_back(line.str());
    }
  }
  return dirs;
}

auto wrappedArgv(const std::string &compiler,
                 const std::vector<std::string> &args)
    -> std::vector<std::string> {
  const Split split = splitCommand(args);
  const fs::path outdir =
      fs::absolute(env("INSPECTOR_INCLUDE_DIR", defaultOutdir));
  if (!split.sources.empty()) {
    const auto systemIncludes = compilerIncludePaths(compiler);
    const bool scanAll = !env("INSPECTOR_SCAN_ALL").empty();
    for (const auto &source : split.sources) {
      prebuild::generate({.directory = fs::current_path(),
                          .source = source,
                          .flags = split.flags,
                          .outdir = outdir,
                          .systemIncludes = systemIncludes,
                          .scanAll = scanAll});
    }
  }
  std::vector<std::string> argv = {compiler};
  if (!split.sources.empty()) {
    const auto flags = compileFlags(outdir);
    argv.insert(argv.end(), flags.begin(), flags.end());
  }
  argv.insert(argv.end(), args.begin(), args.end());
  // CMake links via its own rule (inspector_enable adds the library).
  if (!split.compileOnly && env("INSPECTOR_NO_LINK").empty()) {
    const auto flags = linkFlags();
    argv.insert(argv.end(), flags.begin(), flags.end());
  }
  return argv;
}

auto includeRoot(const fs::path &outdir) -> fs::path {
  fs::path root = outdir;
  for (int level = 0; level < climbLimit; ++level) {
    root /= "_";
  }
  return root;
}

auto compileFlags(const fs::path &outdir) -> std::vector<std::string> {
  return {"-I" + includeRoot(outdir).string(), "-I" + paths().include.string(),
          "-DINSPECTOR=<inspector/__FILE__-__LINE__>", "-include",
          "inspector/repl.h"};
}

auto linkFlags() -> std::vector<std::string> {
  const std::string lib = paths().library.string();
  // The REPL calls the program's own functions, which the JIT can only find
  // if the executable exports them.
  return {"-rdynamic", "-L" + lib, "-Wl,-rpath," + lib, "-linspector"};
}

auto exec(const std::vector<std::string> &args) -> int {
  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (const auto &arg : args) {
    argv.push_back(const_cast<char *>(arg.c_str())); // NOLINT: execvp API
  }
  argv.push_back(nullptr);
  execvp(argv.front(), argv.data());
  std::cerr << "inspector: cannot run " << args.front() << ": "
            << std::strerror(errno) << "\n"; // NOLINT(concurrency-mt-unsafe)
  constexpr int notFound = 127;              // like a shell
  return notFound;
}

} // namespace inspector::driver
