#!/bin/bash

set -eux

dnf install -y jsoncpp-devel python3-prompt_toolkit cmake bzip2 gcc-c++

curl https://root.cern.ch/download/cling/cling_2018-02-19_fedora27.tar.bz2 | tar xj --strip 1 -C /usr

mkdir build
cd build
cmake ..
make -j$(nproc)

curl https://root.cern.ch/download/cling/cling_2018-02-19_sources.tar.bz2 | tar xj
export PYTHONPATH=$(readlink -f src/tools/clang/bindings/python)
./inspector print-cflags
