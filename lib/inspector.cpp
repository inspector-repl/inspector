#include "inspector.h"

#include <iostream>
#include <sstream>

#include <cling/Interpreter/Interpreter.h>
#include <cling/Interpreter/Value.h>
#include <llvm/Support/raw_ostream.h>

namespace inspector {
  void runRepl(std::string path, unsigned lineNumber, std::string clingContext, ...) {
    cling::Interpreter interp(0, NULL, LLVMDIR);
    interp.declare(clingContext);
    while (true) {
      std::string in;
      std::cin >> in;
      cling::Value value;
      auto result = interp.process(in, &value, nullptr, true);
      std::string out;
      llvm::raw_string_ostream outStream(out);
      value.print(outStream);
      std::cout << out;
    }
  }
}
