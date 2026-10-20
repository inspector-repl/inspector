# Inspector - A drop-anywhere C++ REPL

Inspector injects a fully-functional C++ REPL into running, compiled programs
that can access your program state and offers features like code-completion and
syntax highlighting. The REPL uses the language standard and include paths the
program was compiled with.

The API is similar to tools like [Pry](http://pryrepl.org/) in Ruby or
[Pdb](https://docs.python.org/3/library/pdb.html) in Python:

Drop `#include INSPECTOR` anywhere inside a function:

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
$ inspector-c++ -std=c++17 main.cpp -o main
$ inspector repl      # terminal 1: waits for programs to connect
$ ./main              # terminal 2: stops at each INSPECTOR and hands over
```

![Inspector example session](https://i.imgur.com/D7MQrDU.png "Inspector")

[Presentation slides on Inspector](https://hackmd.shackspace.de/p/rypPYkqUM#/)

[10 min video presentation on Inspector](https://www.youtube.com/watch?v=Cl5RSlW6xAc)

[Cppcast about Inspector](http://cppcast.com/2018/02/mathieu-ropert/)

## How it works

1. At build time, `inspector` parses each source file with Clang. For every
   `#include INSPECTOR` it writes a small header that captures the addresses
   of all local variables in scope.
2. At runtime the program calls into `libinspector`, which connects to
   `inspector repl` (the `inspector-repl` binary) over a Unix socket. It then
   starts clang-repl (`clang::Interpreter`) with references to those
   variables. Input and completion requests are evaluated inside the program.
3. Typing `.continue` (or Ctrl-D) resumes the program.

## Build

Requirements:

- LLVM/Clang **>= 22** including its CMake package (`ClangConfig.cmake`).
  Released distro packages work (apt.llvm.org, Homebrew, Arch, nixpkgs); no
  LLVM source tree or fork is needed.
- CMake >= 3.20, ninja
- GoogleTest, only with `BUILD_TESTING` (the default)

The line editor, [replxx](https://github.com/ClickHouse/replxx), is vendored
in `third_party/` on purpose: distributions ship the 2021 release, which
lacks the fork's paste, redraw and crash fixes.

```console
$ cmake -B build -GNinja
$ ninja -C build
$ ctest --test-dir build      # gtest suites, including a pty-driven REPL test
```

The binaries end up in `build/inspector`, `build/inspector-c++` and
`build/inspector-repl`. To run the end-to-end tests against another
instrumented program: `build/test/inspector-e2e ./program`.

If CMake can't find the clang executable, it can't detect the clang resource
dir (builtin headers). Pass it explicitly:
`-DCLANG_RESOURCE_DIR=$(clang -print-resource-dir)`.

### Nix

```console
$ nix build                   # package, uses llvmPackages_latest
$ nix run . -- repl
$ nix develop                 # dev shell with all dependencies
$ nix build .#checks.x86_64-linux.example-cmake   # make / meson / package-llvm22
$ nix build .#checks.x86_64-linux.clang-tidy      # or: run-clang-tidy -p build
```

## Usage

Run `inspector repl` in one terminal, then start your instrumented program
in another. Pick whichever integration fits your build:

### In the REPL

```
[0] .locals
  v     std::vector<int>
  name  std::string
[1] v.push_back(4)
[2] v
(std::vector<int> &) {1, 2, 3, 4}
[3] name + "!"
(std::string &) "inspector!"
[4] undefined + 1
error: use of undeclared identifier 'undefined'
```

- Expressions print their value. Strings, containers, pairs and any type with
  `operator<<` are shown by content. A trailing `;` suppresses the output.
- Multi-line input continues while brackets are open.
- `.locals`, `.where`, `.help`, `.abort`; `.continue` or Ctrl-D resumes the
  program. Ctrl-C clears the current line.
- History is saved in `$XDG_STATE_HOME/inspector/history`.
- The REPL keeps running between breakpoints and programs; Ctrl-C while
  waiting exits.

### Any build system: compiler wrapper

`inspector-c++` wraps the real compiler (`$INSPECTOR_CXX`, default `c++`).
It generates the headers for `#include INSPECTOR` on the fly and links
`libinspector`:

```console
$ make CXX=inspector-c++                         # make / autotools
$ CXX=inspector-c++ meson setup build            # meson
$ inspector wrap g++ -std=c++17 main.cpp -o main # explicit
```

Environment variables:

| Variable | Meaning |
|---|---|
| `INSPECTOR_CXX` | real compiler used by `inspector-c++` (default `c++`) |
| `INSPECTOR_INCLUDE_DIR` | where generated headers go (default `./.inspector-includes`) |
| `INSPECTOR_SCAN_ALL` | parse every file, not only those containing `INSPECTOR` |
| `INSPECTOR_NO_LINK` | don't add `-linspector` when linking |
| `INSPECTOR_REQUIRED` | exit instead of continuing when no REPL is listening |
| `INSPECTOR_WAIT` | wait until a REPL listens instead of continuing |

### CMake

```cmake
find_package(inspector REQUIRED)
add_executable(app main.cpp)
inspector_enable(app)   # wraps compiles of `app`, links inspector::inspector
```

### pkg-config

`pkg-config --cflags --libs inspector` provides the include and link flags.
Generating the headers still needs `inspector-c++` or `inspector prebuild`.

### Manual / compile_commands.json

```console
$ inspector prebuild main.cpp --cflags "-std=c++17 -Iinclude"
$ inspector prebuild -p build/compile_commands.json -j8   # parallel
$ c++ $(inspector print-cflags) main.cpp -o main
```

See `examples/` for working setups; `nix flake check` builds each one and runs
`inspector-e2e` against it.

## Limitations

- C++ only.
- The quick pre-scan only looks at the source file itself. If
  `#include INSPECTOR` sits in a header, set `INSPECTOR_SCAN_ALL=1`.
- If no REPL is listening when the program reaches `INSPECTOR`, it prints a
  warning and continues.

## TODO

- [ ] include type declarations into the repl
- [ ] GDB/LLDB plugin: preload libinspector.so and invoke inspector with debug
      information from gdb/lldb
- [ ] C support
