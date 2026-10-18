cmake_minimum_required(VERSION 3.20)
project(
  inspector
  VERSION 0.1.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ClangConfig pulls in LLVMConfig, so any packaged LLVM >= 22 works (distro
# packages, nixpkgs, Homebrew, apt.llvm.org); no source tree is needed.
find_package(Clang REQUIRED CONFIG)
if(LLVM_PACKAGE_VERSION VERSION_LESS 22)
  message(
    FATAL_ERROR
      "LLVM ${LLVM_PACKAGE_VERSION} found, but clang-repl APIs need >= 22")
endif()
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
message(STATUS "Using Clang from ${Clang_DIR}")

# The interpreter derives the resource dir from the *host* executable, which is
# the user's program, not clang. Bake in the real one so builtin headers like
# <stddef.h> resolve.
find_program(
  INSPECTOR_CLANG_EXE
  NAMES clang-${LLVM_VERSION_MAJOR} clang
  HINTS ${LLVM_TOOLS_BINARY_DIR})
set(CLANG_RESOURCE_DIR
    ""
    CACHE PATH "clang resource directory (auto-detected from clang)")
if(NOT CLANG_RESOURCE_DIR AND INSPECTOR_CLANG_EXE)
  execute_process(
    COMMAND ${INSPECTOR_CLANG_EXE} -print-resource-dir
    OUTPUT_VARIABLE _resource_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(CLANG_RESOURCE_DIR
      "${_resource_dir}"
      CACHE PATH "clang resource directory (auto-detected from clang)" FORCE)
endif()
if(NOT CLANG_RESOURCE_DIR)
  message(
    FATAL_ERROR "Could not detect clang resource dir, set -DCLANG_RESOURCE_DIR")
endif()
message(STATUS "Using clang resource dir ${CLANG_RESOURCE_DIR}")

add_subdirectory(lib)
add_subdirectory(python)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()

install(DIRECTORY include/inspector DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Consumers: find_package(inspector) + inspector_enable(<target>)
set(INSPECTOR_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/inspector)
install(
  EXPORT inspectorTargets
  NAMESPACE inspector::
  DESTINATION ${INSPECTOR_CMAKE_DIR})
configure_package_config_file(
  cmake/inspectorConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/inspectorConfig.cmake
  INSTALL_DESTINATION ${INSPECTOR_CMAKE_DIR}
  PATH_VARS CMAKE_INSTALL_BINDIR)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/inspectorConfigVersion.cmake
  COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/inspectorConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/inspectorConfigVersion.cmake
        DESTINATION ${INSPECTOR_CMAKE_DIR})

configure_file(cmake/inspector.pc.in ${CMAKE_CURRENT_BINARY_DIR}/inspector.pc
               @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/inspector.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
