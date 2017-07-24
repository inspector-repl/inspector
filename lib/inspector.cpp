#include "inspector.h"

#include <iostream>
#include <sstream>

#include <cling/Interpreter/Interpreter.h>
#include <cling/Interpreter/Value.h>
#include <llvm/Support/raw_ostream.h>

namespace inspector {
  void runRepl(std::string path, unsigned lineNumber, std::string clingContext, ...) {
    std::cout << "stopped at " << path <<  ":" << lineNumber << std::endl;
    const char* argv = "cling";
    cling::Interpreter interp(1, &argv, LLVMDIR);
    interp.declare(clingContext);
    while (true) {
      std::string in;
      std::cout << "> ";
      std::getline (std::cin, in);
      std::cout << "process... " << std::endl;
      cling::Value value;
      auto result = interp.process(in, &value, nullptr, true);
      if (result == cling::Interpreter::kSuccess) {
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
