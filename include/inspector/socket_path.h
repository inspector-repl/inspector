#pragma once

#include <filesystem>

namespace inspector {

// Platform-specific function to get socket directory
std::filesystem::path getSocketDirectory();

} // namespace inspector