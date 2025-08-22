// save as main.cpp
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  int a = 1;
  std::string b = "hello world";
#include INSPECTOR
  std::cout << "second break." << std::endl;
#include INSPECTOR
}
