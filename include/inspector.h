#pragma once

#include "string"

extern "C" {
  void inspectorRunRepl(const char* path, unsigned lineNumber, const char* clingContext, ...);
}
