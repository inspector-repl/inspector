#pragma once

#include "string"

namespace inspector {
    void runRepl(std::string path, unsigned lineNumber, std::string clingContext, ...);
}