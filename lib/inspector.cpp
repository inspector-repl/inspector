#include "inspector.h"

#include <iostream>
#include <sstream>

#include <cling/Interpreter/Interpreter.h>
#include <cling/Interpreter/Value.h>
#include <cling/MetaProcessor/MetaProcessor.h>
#include <cling/Utils/Output.h>
#include <llvm/Support/raw_ostream.h>

extern "C" {
  void inspectorRunRepl(const char* path, unsigned lineNumber, const char* clingContext, ...) {
    std::cout << "stopped at " << path <<  ":" << lineNumber << std::endl;
    const char* argv = "cling";
    cling::Interpreter interp(1, &argv, LLVMDIR);
    interp.declare(clingContext);
    cling::MetaProcessor metaProcessor(interp, cling::errs());

    while (true) {
      std::string in;
      std::cout << "> ";
      std::getline (std::cin, in);
      std::cout << "process... " << std::endl;
      cling::Value value;
      cling::Interpreter::CompilationResult result;
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
}
