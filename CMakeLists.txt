cmake_minimum_required(VERSION 3.16)
project(inspector VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(LLVM_NO_DEAD_STRIP 1)

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Options
set(CLANG_LIBDIR "" CACHE PATH "Path to Clang library directory for static linking (required)")
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Find required packages
find_package(PkgConfig REQUIRED)

# Find jsoncpp
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# Find zlib
find_package(ZLIB REQUIRED)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Add LLVM definitions
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

# Get LLVM libraries using llvm-config
execute_process(
  COMMAND llvm-config --libs --link-static all
  OUTPUT_VARIABLE LLVM_STATIC_LIBS_STRING
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND llvm-config --ldflags
  OUTPUT_VARIABLE LLVM_LDFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Handle Clang configuration
if(NOT CLANG_LIBDIR OR NOT EXISTS ${CLANG_LIBDIR})
  message(FATAL_ERROR "CLANG_LIBDIR (${CLANG_LIBDIR}) must be set to a valid directory containing Clang static libraries. Use -DCLANG_LIBDIR=/path/to/clang/lib")
endif()

message(STATUS "Using Clang library directory: ${CLANG_LIBDIR}")

# Find all Clang static libraries in the directory
file(GLOB CLANG_STATIC_LIBRARIES "${CLANG_LIBDIR}/libclang*.a")
if(NOT CLANG_STATIC_LIBRARIES)
  message(FATAL_ERROR "No Clang static libraries found in ${CLANG_LIBDIR}")
endif()
message(STATUS "Found ${CMAKE_WORDS_COUNT} Clang static libraries")

# Try to infer Clang include directory from library path
get_filename_component(CLANG_INSTALL_PREFIX "${CLANG_LIBDIR}" DIRECTORY)
set(CLANG_INCLUDE_DIRS "${CLANG_INSTALL_PREFIX}/include")
if(EXISTS ${CLANG_INCLUDE_DIRS})
  include_directories(${CLANG_INCLUDE_DIRS})
  message(STATUS "Using Clang include directory: ${CLANG_INCLUDE_DIRS}")
else()
  message(WARNING "Could not find Clang include directory at ${CLANG_INCLUDE_DIRS}, you may need to set include paths manually")
endif()

# Include project headers
include_directories(include)

# Add library subdirectory
add_subdirectory(lib)

# Installation for headers
set(INSPECTOR_INCLUDE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(INSPECTOR_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
file(GLOB_RECURSE INSPECTOR_HEADERS "${INSPECTOR_INCLUDE_SOURCE_DIR}/*.h")

install(FILES ${INSPECTOR_HEADERS}
  DESTINATION "${INSPECTOR_INCLUDE_DIR}")

# Add test and python subdirectories if they exist
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt")
  add_subdirectory(test)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/python/CMakeLists.txt")
  add_subdirectory(python)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  LLVM version: ${LLVM_PACKAGE_VERSION}")
message(STATUS "  Clang libraries from: ${CLANG_LIBDIR}")
message(STATUS "")
