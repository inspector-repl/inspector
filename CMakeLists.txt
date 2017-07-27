cmake_minimum_required(VERSION 3.5)

set(LLVM_NO_DEAD_STRIP 1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(inspector)
set(INSPECTOR_VERSION 0.1)

find_package(Cling REQUIRED)
find_package(PkgConfig)

pkg_search_module(JSONCPP REQUIRED jsoncpp)

include_directories(include)

add_subdirectory(lib)

set(INSPECTOR_INCLUDE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(INSPECTOR_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
file(GLOB_RECURSE INSPECTOR_HEADERS "${INSPECTOR_INCLUDE_SOURCE_DIR}/*.h")

install(FILES ${INSPECTOR_HEADERS}
  DESTINATION "${INSPECTOR_INCLUDE_DIR}")

add_subdirectory(test)
add_subdirectory(python)
