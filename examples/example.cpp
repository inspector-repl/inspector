// #include INSPECTOR drops into the REPL at that line
#include <string>

int main() {
  int a = 1;
  std::string b = "Hello world!";
#include INSPECTOR
  return a == 1 ? 0 : 1;
}
