#include <map>
#include <memory>
#include <string>
#include <utility>

namespace {
struct Hidden {
  int value = 7;
};
struct Bits {
  int flag : 1;
  int whole;
};
} // namespace

// Defined in another file of the executable, so the REPL must find it
// there.
int answer();

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
  // References, anonymous-namespace types and structured bindings (except
  // bit-fields, which have no address) are captured; inner names shadow
  // outer ones.
  int shadowed = 1;
  int &ref = a;
  Hidden hidden;
  auto [first, second] = std::pair<int, int>{8, 9};
  auto [flag, whole] = Bits{0, 10};
  {
    int shadowed = 2;
#include INSPECTOR
    (void)shadowed;
  }
  (void)shadowed;
  (void)flag;
  return lambda(Local{}.get(0)) + ref + hidden.value + first + second + whole -
         35;
}
