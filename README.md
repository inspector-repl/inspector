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
