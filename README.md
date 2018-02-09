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
$ ./inspector prebuild main.cpp 
$ clang++ $(./inspector print-cflags) main.cpp -o main 
$ ./inspector repl
$ ./main
```

![Inspector example session](https://i.imgur.com/D7MQrDU.png "Inspector")

[Presentation slides on Inspector](https://hackmd.shackspace.de/p/rypPYkqUM#/)

[10 min presentation on Inspector](https://www.youtube.com/watch?v=Cl5RSlW6xAc)

## Build

### Requirements

- a c++ compiler (gcc or clang++)
- libcling (see installation instructions below)
- jsoncpp
- python3
- python-prompt-toolkit (for python3)
- python-setuptools (for python3)
- pkg-config

### Build Cling

```
git clone http://root.cern.ch/git/llvm.git src
cd src
git checkout cling-patches
cd tools
git clone http://root.cern.ch/git/cling.git
git clone http://root.cern.ch/git/clang.git
cd clang
git checkout cling-patches
cd ../..
mkdir build inst
cd build
cmake -DCMAKE_INSTALL_PREFIX=../inst ../src
cmake --build .
cmake --build . --target install
```

### Build Inspector

```
git clone git@github.com:inspector-repl/inspector.git
cd inspector
mkdir build
cd build
cmake -DCMAKE_PREFIX_PATH="../inst" ..
cmake --build .
```

## Usage

```
# bring python bindings
# (requires clang 5/trunk for python3 support, the clang branch from cling is new enough)
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
- [ ] use verbose mode of compiler to get additional include paths for cling /
      libclang parsing
- [ ] multiprocessing scanning
- [ ] GDB/LLDB plugin: link/preload libinspector.so and invoke inspector with debug information from gdb/lldb. 
- Support for more languages:
   - [ ] C
