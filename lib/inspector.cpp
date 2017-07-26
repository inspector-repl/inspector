#include "inspector/repl.h"

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <sstream>

#include <cling/Interpreter/Interpreter.h>
#include <cling/Interpreter/Value.h>
#include <cling/MetaProcessor/MetaProcessor.h>
#include <cling/Utils/Output.h>
#include <llvm/Support/raw_ostream.h>

extern "C" {
  void inspectorRunRepl(const char* path, unsigned lineNumber, const char* clingDeclare, const char* clingContext, ...) {
    va_list arglist;

    // pass all pointer to cling
    va_start(arglist, clingContext);
    int bufferSize = vsnprintf(0, 0, clingContext, arglist);
    va_end(arglist);

    char* clingContextBuffer = new char[bufferSize + 1];
    va_start(arglist, clingContext);
    vsnprintf(clingContextBuffer, bufferSize + 1, clingContext, arglist);
    va_end(arglist);

    std::cout << "stopped at " << path <<  ":" << lineNumber << std::endl;

    const char* argv = "cling";
    cling::Interpreter interp(1, &argv, LLVMDIR);
    interp.declare(clingDeclare);

    cling::MetaProcessor metaProcessor(interp, cling::errs());

    cling::Value value;
    cling::Interpreter::CompilationResult result;
    metaProcessor.process(clingContextBuffer, result, &value, /*disableValuePrinting*/ true);
    delete []clingContextBuffer;

    while (true) {
      std::string in;
      std::cout << "> ";
      if (!std::getline (std::cin, in)) {
        return;
      };
      std::cout << "process... " << std::endl;
      if (metaProcessor.process(in, result, &value, /*disableValuePrinting*/ true)) {
        continue;
      }
      if (result != cling::Interpreter::kSuccess) {
        continue;
      }
      if (value.isValid()) {
        std::string out;
        {
          llvm::raw_string_ostream os(out);
          value.print(os);
        }
        std::cout << out << std::endl;
      }
    }
  }

  // C++ mangled version of inspectorRunRepl
  void _Z16inspectorRunReplPKcjS0_S0_z(const char* path, unsigned lineNumber, const char* clingContext, ...) __attribute__((weak, alias ("inspectorRunRepl")));
}
