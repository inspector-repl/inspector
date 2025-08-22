#include "inspector/socket_path.h"

#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif

namespace inspector {

std::filesystem::path getSocketDirectory() {
#ifdef __linux__
  // On Linux, prefer XDG_RUNTIME_DIR (usually /run/user/UID)
  const char *xdgRuntimeDir = std::getenv("XDG_RUNTIME_DIR");
  if (xdgRuntimeDir) {
    return std::filesystem::path(xdgRuntimeDir) / "inspector";
  }
  // Fallback to ~/.cache/inspector
  const char *home = std::getenv("HOME");
  if (home) {
    return std::filesystem::path(home) / ".cache" / "inspector";
  }
  // Should never happen, but use current directory as last resort
  return std::filesystem::path(".") / ".inspector";

#elif defined(__APPLE__)
  // On macOS, use ~/Library/Application Support for persistent sockets
  const char *home = std::getenv("HOME");
  if (home) {
    return std::filesystem::path(home) / "Library" / "Application Support" /
           "inspector";
  }
  // Fallback to ~/.cache
  return std::filesystem::path(".") / ".inspector";

#elif defined(_WIN32)
  // On Windows, use %LOCALAPPDATA% for persistent app data
  const char *localAppData = std::getenv("LOCALAPPDATA");
  if (localAppData) {
    return std::filesystem::path(localAppData) / "inspector";
  }
  // Fallback to %APPDATA%
  const char *appData = std::getenv("APPDATA");
  if (appData) {
    return std::filesystem::path(appData) / "inspector";
  }
  // Fallback to user profile
  const char *userProfile = std::getenv("USERPROFILE");
  if (userProfile) {
    return std::filesystem::path(userProfile) / ".inspector";
  }
  return std::filesystem::path(".") / ".inspector";

#else
  // Generic Unix fallback: use ~/.cache
  const char *home = std::getenv("HOME");
  if (home) {
    return std::filesystem::path(home) / ".cache" / "inspector";
  }
  return std::filesystem::path(".") / ".inspector";
#endif
}

} // namespace inspector