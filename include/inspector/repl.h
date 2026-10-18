#pragma once

// Entry point called by the generated `#include INSPECTOR` headers.
// `context` holds one `%p` per captured local, filled from `addresses`.
// extern "C" keeps it callable from C and stable across compilers.
#ifdef __cplusplus
extern "C" {
#endif
void inspectorRunRepl(const char *path, unsigned lineNumber,
                      const char *declare, const char *context,
                      const char *const *includes,
                      const void *const *addresses);
#ifdef __cplusplus
}
#endif
