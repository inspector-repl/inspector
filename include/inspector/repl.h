#pragma once

#include "inspector/common.h"

#include "string"

extern "C" {
  void inspectorRunRepl(const char* path, unsigned lineNumber, const char* clingDeclare, const char* clingContext, ...);
}
