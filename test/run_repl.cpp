#include "inspector.h"

#include <string>

int main() {
  inspector::runRepl(std::string(__FILE__), __LINE__, "");
}
