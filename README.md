# Inspector
A drop-anywhere C++ REPL


## Build

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
cmake -DCMAKE_INSTALL_PREFIX=inst ../src
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
```

buildsystem integration coming soon...
