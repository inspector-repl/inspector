# Inspector - A drop-anywhere C++ REPL

[![Build Status](https://travis-ci.org/inspector-repl/inspector.svg?branch=master)](https://travis-ci.org/inspector-repl/inspector)

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
$ ./inspector prebuild main.cpp 
$ clang++ $(./inspector print-cflags) main.cpp -o main 
$ ./inspector repl
$ ./main
```

![Inspector example session](https://i.imgur.com/D7MQrDU.png "Inspector")

[Presentation slides on Inspector](https://hackmd.shackspace.de/p/rypPYkqUM#/)

[10 min video presentation on Inspector](https://www.youtube.com/watch?v=Cl5RSlW6xAc)

[Cppcast about Inspector](http://cppcast.com/2018/02/mathieu-ropert/)

## Build

### Requirements

- a c++ compiler (gcc or clang++)
- LLVM/Clang 22+ with clang-repl (unreleased as of August 23, 2025)
- jsoncpp
- python3
- python-prompt-toolkit (for python3)
- python-setuptools (for python3)
- pkg-config
- ninja (build system)

### Using Nix

The easiest way to get all dependencies is using the Nix flake:

```bash
nix develop
```

### Manual Installation

You need LLVM/Clang 22 or later with clang-repl support. As LLVM 22 is unreleased (as of August 23, 2025), you'll need to build from source or use the development version.

Building LLVM from source:
```bash
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
cmake -S llvm -B build -G Ninja \
  -DLLVM_ENABLE_PROJECTS="clang" \
  -DCLANG_ENABLE_CLANG_REPL=ON \
  -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

Note: Package managers will provide LLVM 22 packages once it's officially released.

### Build Inspector

```bash
git clone git@github.com:inspector-repl/inspector.git
cd inspector
mkdir build
cd build
cmake -GNinja .. -DCLANG_LIBDIR=<path-to-llvm>/lib
ninja
```

Note: The `-DCLANG_LIBDIR` flag should point to the lib directory of your LLVM/Clang installation. For example:
- If built from source: `-DCLANG_LIBDIR=/path/to/llvm-project/build/lib`
- If installed system-wide: `-DCLANG_LIBDIR=/usr/lib/llvm-22/lib`

## Usage

```
# bring python bindings
# (requires clang with clang-repl support, LLVM 22+ required)
# and libclang into path
export PYTHONPATH=$(readlink -f <llvm-root>/src/tools/clang/bindings/python/)
export LD_LIBRARY_PATH=$(readlink -f <llvm-repo>/inst/lib)
cd build
./inspector prebuild ../test/test.cpp
gcc -o test-proc $(./inspector print-cflags) ../test/test.cpp
# start repl cli
./inspector repl
# let program connect to repl
./test-proc
```

## TODO

- [ ] buildsystem integration:
  - [ ] cmake
  - [ ] autotools
  - [ ] make
  - [ ] meson
- [ ] include type declaration into the repl
- [ ] search for '#include INSPECTOR' before parsing (speed)
- [ ] use verbose mode of compiler to get additional include paths for clang-repl /
      libclang parsing
- [ ] multiprocessing scanning
- [ ] GDB/LLDB plugin: link/preload libinspector.so and invoke inspector with debug information from gdb/lldb. 
- Support for more languages:
   - [ ] C
