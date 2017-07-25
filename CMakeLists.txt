cmake_minimum_required(VERSION 3.5)

set(LLVM_NO_DEAD_STRIP 1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(inspector)
set(INSPECTOR_VERSION 0.1)

find_package(Cling REQUIRED)

include_directories(INSPECTOR_INCLUDE_DIRS include)

add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(python)
