#include "inspector/repl.h"
#include "inspector/socket.h"
#include "inspector/prompt.h"

#include <cling/Interpreter/Interpreter.h>
#include <cling/Interpreter/Value.h>
#include <cling/MetaProcessor/MetaProcessor.h>
#include <cling/Utils/Output.h>

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <sstream>

using namespace inspector;

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

    const char* argv = "cling";
    cling::Interpreter interp(1, &argv, LLVMDIR);
    interp.declare(clingDeclare);

    cling::MetaProcessor metaProcessor(interp, cling::errs());

    cling::Value value;
    cling::Interpreter::CompilationResult result;
    metaProcessor.process(clingContextBuffer, result, &value, /*disableValuePrinting*/ true);
    delete []clingContextBuffer;

    try {
      TCPSocket socket("localhost", 5000);
      Prompt prompt(socket);
      prompt.sendInspectorLocation(path, lineNumber);
      while (true) {
        std::string statement = prompt.getCppStatement();
        if (statement == ".quit") {
          prompt.sendResult("");
          return;
        }

        if (metaProcessor.process(statement, result, &value, /*disableValuePrinting*/ true)) {
          continue;
        }

        if (result != cling::Interpreter::kSuccess) {
          prompt.sendResult("Statement is not correct.");
          continue;
        }
        if (value.isValid()) {
          std::string out;
          {
            llvm::raw_string_ostream os(out);
            value.print(os);
          }
          prompt.sendResult(out);
        } else {
          prompt.sendResult("No result to display.");
        }

      }

    } catch(SocketException &e) {
      cerr << e.what() << endl;
      exit(1);
    }

  }

  // C++ mangled version of inspectorRunRepl
  void _Z16inspectorRunReplPKcjS0_S0_z(const char* path, unsigned lineNumber, const char* clingContext, ...) __attribute__((weak, alias ("inspectorRunRepl")));
}
