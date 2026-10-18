# Inspector - A drop-anywhere C++ REPL

Allows to inject a fully-functional C++17 REPL into running, compiled programs
that can access your program state and offers features like code-completion and
syntax highlighting.

The API is similar to tools like [Pry](http://pryrepl.org/) in Ruby or
[Pdb](https://docs.python.org/3/library/pdb.html) in Python:

Example program:

```c++
// save as main.cpp
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    int a = 1;
    std::string b = "hello world";
#include INSPECTOR
    std::cout << "second break." << std::endl;
#include INSPECTOR
}
```

```console
$ inspector repl &
$ inspector-c++ main.cpp -o main
$ ./main
```

![Inspector example session](https://i.imgur.com/D7MQrDU.png "Inspector")

[Presentation slides on Inspector](https://hackmd.shackspace.de/p/rypPYkqUM#/)

[10 min video presentation on Inspector](https://www.youtube.com/watch?v=Cl5RSlW6xAc)

[Cppcast about Inspector](http://cppcast.com/2018/02/mathieu-ropert/)

## Build

Requirements: a released LLVM/Clang **>= 22** (with its CMake package),
jsoncpp, pkg-config, python3 with prompt-toolkit, pygments, libclang and
hatchling. Distro packages (apt.llvm.org, Homebrew, Arch, nixpkgs) are enough;
no LLVM source tree is needed.

```console
$ nix develop        # or install the deps above
$ cmake -B build -GNinja && ninja -C build && ctest --test-dir build
```

Or with Nix: `nix build` / `nix run .#inspector -- repl`.

## Usage

Start the REPL server in one terminal, then run your instrumented program:

```console
$ inspector repl
```

### Any build system: compiler wrapper

`inspector-c++` wraps the real compiler (`$INSPECTOR_CXX`, default `c++`).
It generates the headers for `#include INSPECTOR` on the fly and links
`libinspector`:

```console
$ make CXX=inspector-c++                         # make / autotools
$ CXX=inspector-c++ meson setup build            # meson
$ inspector wrap g++ -std=c++17 main.cpp -o main # explicit
```

### CMake

```cmake
find_package(inspector REQUIRED)
add_executable(app main.cpp)
inspector_enable(app)
```

### Manual / compile_commands.json

```console
$ inspector prebuild main.cpp --cflags "-std=c++17 -Iinclude"
$ inspector prebuild -p build/compile_commands.json -j8   # parallel
$ c++ $(inspector print-cflags) main.cpp -o main
```

See `examples/` for working setups; they run as `nix flake check` tests.

## TODO

- [ ] include type declarations into the repl
- [ ] GDB/LLDB plugin: preload libinspector.so and invoke inspector with debug
      information from gdb/lldb
- [ ] C support
