#pragma once

#include <string>
#include <vector>

namespace inspector {

// `inspector-repl eval|continue|stop`, talking to a background server that
// the first of them starts. Returns the exit code.
auto runCommand(const std::vector<std::string> &args) -> int;

// True while the background server owns the program socket.
auto serverRunning() -> bool;

} // namespace inspector
