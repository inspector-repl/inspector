#include "inspector/repl.h"

#include <stdio.h>
#include <string>

int globalVar = 5;

int main() {
  std::string s = "local string";
  static const char *includes[] = {nullptr};
  inspectorRunRepl(__FILE__, __LINE__, "", "", includes);
}
