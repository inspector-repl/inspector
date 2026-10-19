#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace inspector::prebuild {

// Everything a parse needs, resolved up front so jobs can run in parallel.
struct Job {
  std::filesystem::path directory; // relative paths in the job resolve here
  std::string source;
  std::vector<std::string> flags;
  std::filesystem::path outdir;
  std::vector<std::string> systemIncludes; // the real compiler's
  bool scanAll = false; // parse even if the file doesn't say INSPECTOR
};

// Escapes `text` for a C string literal; other than printable ASCII,
// bytes become octal escapes.
auto escapeCString(const std::string &text) -> std::string;

// Parses the job's source and writes one header per `#include INSPECTOR`,
// returning their paths. Throws std::runtime_error if parsing fails.
auto generate(const Job &job) -> std::vector<std::filesystem::path>;

} // namespace inspector::prebuild
