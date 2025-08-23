#pragma once

extern "C" {
void inspectorRunRepl(const char *path, unsigned lineNumber,
                      const char *clingDeclare, const char *clingContext,
                      const char *const *clingIncludes, ...);
}
