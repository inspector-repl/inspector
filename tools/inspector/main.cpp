// `inspector`: prebuild, print-cflags, wrap and repl subcommands.
#include "driver.h"
#include "prebuild.h"

#include <clang/Tooling/JSONCompilationDatabase.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Parallel.h>
#include <llvm/Support/StringSaver.h>
#include <llvm/Support/Threading.h>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
using inspector::prebuild::Job;

namespace {

constexpr const char *usage =
    R"(usage: inspector <command> [args...]

  repl                         wait for programs to reach INSPECTOR
  repl eval|continue|stop      the same without a terminal, see `repl -h`
  wrap <compiler> [args...]    compile with INSPECTOR support
  prebuild [options] [sources...]
      -p, --compile-commands <file>   every file in compile_commands.json
      -o, --outdir <dir>              where headers go (.inspector-includes)
      -j, --jobs <n>                  parallel parses (all cores)
      --cflags <flags>                flags used to compile the sources
  print-cflags                 flags to compile and link by hand
)";

struct PrebuildArgs {
  std::vector<std::string> sources;
  std::vector<std::string> cflags;
  std::string compileCommands;
  fs::path outdir = inspector::driver::defaultOutdir;
  unsigned jobs = 0; // all cores
};

auto shellSplit(llvm::StringRef text) -> std::vector<std::string> {
  llvm::BumpPtrAllocator allocator;
  llvm::StringSaver saver(allocator);
  llvm::SmallVector<const char *> tokens;
  llvm::cl::TokenizeGNUCommandLine(text, saver, tokens);
  return {tokens.begin(), tokens.end()};
}

auto parsePrebuild(const std::vector<std::string> &args) -> PrebuildArgs {
  PrebuildArgs out;
  for (size_t i = 0; i < args.size(); ++i) {
    // `--name=value`, `-jN` and `--name value` are all accepted.
    llvm::StringRef name = args.at(i);
    llvm::StringRef value;
    if (name.starts_with("--")) {
      std::tie(name, value) = name.split('=');
    } else if (name.starts_with("-j") && name.size() > 2) {
      value = name.drop_front(2);
      name = "-j";
    }
    if (!name.starts_with("-")) {
      out.sources.push_back(args.at(i));
      continue;
    }
    if (value.empty()) {
      if (++i == args.size()) {
        throw std::runtime_error(name.str() + " needs a value");
      }
      value = args.at(i);
    }
    if (name == "-p" || name == "--compile-commands") {
      out.compileCommands = value.str();
    } else if (name == "-o" || name == "--outdir") {
      out.outdir = value.str();
    } else if (name == "-j" || name == "--jobs") {
      out.jobs = std::stoul(value.str());
    } else if (name == "--cflags") {
      out.cflags = shellSplit(value);
    } else {
      throw std::runtime_error("unknown option " + name.str());
    }
  }
  return out;
}

auto prebuildJobs(const PrebuildArgs &args) -> std::vector<Job> {
  const bool scanAll = !inspector::driver::env("INSPECTOR_SCAN_ALL").empty();
  std::map<std::string, std::vector<std::string>> includes; // per compiler
  const auto systemIncludes =
      [&](const std::string &compiler) -> const std::vector<std::string> & {
    auto [entry, added] = includes.try_emplace(compiler);
    if (added) {
      entry->second = inspector::driver::compilerIncludePaths(compiler);
    }
    return entry->second;
  };
  std::vector<Job> jobs;
  jobs.reserve(args.sources.size());
  const std::string cxx = inspector::driver::env("CXX", "c++");
  for (const auto &source : args.sources) {
    jobs.push_back({.directory = fs::current_path(),
                    .source = source,
                    .flags = args.cflags,
                    .outdir = fs::absolute(args.outdir),
                    .systemIncludes = systemIncludes(cxx),
                    .scanAll = scanAll});
  }
  if (args.compileCommands.empty()) {
    return jobs;
  }
  std::string error;
  const auto database = clang::tooling::JSONCompilationDatabase::loadFromFile(
      args.compileCommands, error,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);
  if (!database) {
    throw std::runtime_error(error);
  }
  for (const auto &command : database->getAllCompileCommands()) {
    const auto &argv = command.CommandLine;
    if (argv.empty()) {
      continue;
    }
    const auto split =
        inspector::driver::splitCommand({std::next(argv.begin()), argv.end()});
    for (const auto &source : split.sources) {
      jobs.push_back({.directory = command.Directory,
                      .source = source,
                      .flags = split.flags,
                      .outdir = fs::path(command.Directory) / args.outdir,
                      .systemIncludes = systemIncludes(argv.front()),
                      .scanAll = scanAll});
    }
  }
  return jobs;
}

auto prebuild(const std::vector<std::string> &rawArgs) -> int {
  const PrebuildArgs args = parsePrebuild(rawArgs);
  const std::vector<Job> jobs = prebuildJobs(args);
  std::vector<std::vector<fs::path>> results(jobs.size());
  std::vector<std::string> errors(jobs.size());
  llvm::parallel::strategy = llvm::hardware_concurrency(args.jobs);
  llvm::parallelFor(0, jobs.size(), [&](size_t index) -> void {
    try {
      results.at(index) = inspector::prebuild::generate(jobs.at(index));
    } catch (const std::exception &e) {
      errors.at(index) = e.what();
    }
  });
  int status = 0;
  for (size_t i = 0; i < jobs.size(); ++i) {
    for (const auto &header : results.at(i)) {
      std::cout << header.string() << "\n";
    }
    if (!errors.at(i).empty()) {
      std::cerr << "inspector: " << errors.at(i) << "\n";
      status = 1;
    }
  }
  return status;
}

auto repl(const std::vector<std::string> &args) -> int {
  const fs::path sibling =
      inspector::driver::paths().binaries / "inspector-repl";
  std::vector<std::string> argv{fs::exists(sibling) ? sibling.string()
                                                    : "inspector-repl"};
  argv.insert(argv.end(), args.begin(), args.end());
  return inspector::driver::exec(argv);
}

auto run(const std::vector<std::string> &args) -> int {
  const std::string command = args.empty() ? "" : args.front();
  const std::vector<std::string> rest(
      args.empty() ? args.end() : std::next(args.begin()), args.end());
  if (command == "wrap" && !rest.empty()) {
    return inspector::driver::exec(inspector::driver::wrappedArgv(
        rest.front(), {std::next(rest.begin()), rest.end()}));
  }
  if (command == "prebuild") {
    return prebuild(rest);
  }
  if (command == "print-cflags") {
    // Relative, for pasting into a Makefile next to `inspector prebuild`.
    auto flags =
        inspector::driver::compileFlags(inspector::driver::defaultOutdir);
    const auto link = inspector::driver::linkFlags();
    flags.insert(flags.end(), link.begin(), link.end());
    std::cout << llvm::join(flags, " ") << "\n";
    return 0;
  }
  if (command == "repl") {
    return repl(rest);
  }
  std::cerr << usage;
  return command == "-h" || command == "--help" ? 0 : 1;
}

} // namespace

auto main(int argc, char **argv) -> int {
  try {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): argv
    return run({std::next(argv), std::next(argv, argc)});
  } catch (const std::exception &e) {
    std::cerr << "inspector: " << e.what() << "\n";
    return 1;
  }
}
