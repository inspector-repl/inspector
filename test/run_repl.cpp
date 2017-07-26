#include "inspector/repl.h"

#include <stdio.h>

int globalVar = 5;

int main() {
  std::string s = "local string";
  inspectorRunRepl(__FILE__, __LINE__, "", "");
}
