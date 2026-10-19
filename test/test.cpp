#include <map>
#include <memory>
#include <string>

int main(int argc, char **argv) {
  // Variables of closed scopes, lambdas and local classes must not be
  // captured; arrays, lambdas and libstdc++ aliases must still load.
  {
    int gone = 0;
    (void)gone;
  }
  struct Local {
    int get(int param) { return param; }
  };
  auto lambda = [](int param) { return param; };
  int arr[2] = {3, 4};
  auto ptr = std::make_unique<int>(5);
  std::map<std::string, int> map{{"k", 6}};
  int a = 1;
  std::string b = "Hello world!";
#include INSPECTOR
  return lambda(Local{}.get(0));
}
