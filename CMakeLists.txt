cmake_minimum_required(VERSION 3.16)
project(
  inspector
  VERSION 0.1.0
  LANGUAGES C CXX)

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
set(CLANG_LIBDIR
    ""
    CACHE PATH "Path to Clang library directory for static linking (required)")
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

# Get LLVM libraries using llvm-config Just use all LLVM libraries to avoid
# complexity
execute_process(
  COMMAND llvm-config --libs --link-static all
  OUTPUT_VARIABLE LLVM_ALL_LIBS_STRING
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# Libraries to exclude (these are tool-specific and not needed)
set(LLVM_EXCLUDE_LIBS
    # Fuzzing tools
    "LLVMFuzzMutate"
    "LLVMFuzzerCLI"
    # Standalone tools
    "LLVMDWP"
    "LLVMObjCopy"
    "LLVMDlltoolDriver"
    "LLVMLibDriver"
    # Windows-specific tools
    "LLVMWindowsManifest"
    # Note: LLVMCFGuard is needed by X86/ARM/AArch64 codegen DWARF tools (not
    # needed for JIT)
    "LLVMDWARFLinker"
    "LLVMDWARFLinkerClassic"
    "LLVMDWARFLinkerParallel"
    "LLVMDebugInfoLogicalView"
    "LLVMDWARFCFIChecker"
    # Development and analysis tools
    "LLVMFileCheck"
    "LLVMTableGen"
    "LLVMTableGenGlobalISel"
    "LLVMTableGenCommon"
    # Other tools
    "LLVMInterfaceStub"
    "LLVMXRay"
    "LLVMTelemetry"
    # Machine Code Analyzer (tool for analyzing CPU performance)
    "LLVMMCA"
    "LLVMAMDGPUTargetMCA"
    "LLVMRISCVTargetMCA"
    "LLVMX86TargetMCA"
    # Interactive tools
    "LLVMLineEditor"
    # Note: LLVMMIRParser is needed by AMDGPUCodeGen Note: LLVMSymbolize is
    # needed by ProfileData CAS (Content Addressable Storage - experimental)
    "LLVMCAS"
    "LLVMCASStatic"
    # Extensions (experimental/optional features)
    "LLVMExtensions"
    # Note: LLVMDebugInfoGSYM is needed by LLVMSymbolize Note: LLVMSandboxIR is
    # needed by LLVMVectorize Note: LLVMCGData is needed by LLVMCodeGen and
    # LLVMLTO Note: LLVMHipStdPar is needed by AMDGPUCodeGen
)

# Filter out excluded libraries
set(LLVM_STATIC_LIBS_LIST ${LLVM_ALL_LIBS_STRING})
separate_arguments(LLVM_STATIC_LIBS_LIST)
set(LLVM_FILTERED_LIBS "")
foreach(lib ${LLVM_STATIC_LIBS_LIST})
  set(should_exclude FALSE)
  foreach(exclude_lib ${LLVM_EXCLUDE_LIBS})
    if(lib MATCHES "-l${exclude_lib}$")
      set(should_exclude TRUE)
      message(STATUS "Excluding LLVM library: ${lib}")
      break()
    endif()
  endforeach()

  if(NOT should_exclude)
    list(APPEND LLVM_FILTERED_LIBS ${lib})
  endif()
endforeach()

# Convert back to string
string(REPLACE ";" " " LLVM_STATIC_LIBS_STRING "${LLVM_FILTERED_LIBS}")

execute_process(
  COMMAND llvm-config --ldflags
  OUTPUT_VARIABLE LLVM_LDFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# Handle Clang configuration
if(NOT CLANG_LIBDIR OR NOT EXISTS ${CLANG_LIBDIR})
  message(
    FATAL_ERROR
      "CLANG_LIBDIR (${CLANG_LIBDIR}) must be set to a valid directory containing Clang static libraries. Use -DCLANG_LIBDIR=/path/to/clang/lib"
  )
endif()

message(STATUS "Using Clang library directory: ${CLANG_LIBDIR}")

# Find all Clang static libraries in the directory
file(GLOB CLANG_STATIC_LIBRARIES_ALL "${CLANG_LIBDIR}/libclang*.a")

# Exclude libraries with unmet dependencies or that are not needed
set(EXCLUDE_PATTERNS
    "libclangIncludeFixer" # Has unmet dependencies on FindAllSymbols
    "libclangIncludeFixerPlugin" # Has unmet dependencies on FindAllSymbols
    "libclangTidy" # clang-tidy tool libraries (not needed)
    "libclangDoc" # Documentation tool (not needed)
    "libclangQuery" # Query tool (not needed)
    "libclangMove" # Refactoring tool (not needed)
    "libclangReorderFields" # Refactoring tool (not needed)
    "libclangChangeNamespace" # Refactoring tool (not needed)
    "libclangApplyReplacements" # Refactoring tool (not needed)
    "libclangDaemon" # clangd language server (not needed)
    "libclangdMain" # clangd main (not needed)
    "libclangdRemoteIndex" # clangd remote index (not needed)
    "libclangdSupport" # clangd support (not needed)
    "libclangIncludeCleaner" # Include cleaner tool (not needed)
)

set(CLANG_STATIC_LIBRARIES)
foreach(lib ${CLANG_STATIC_LIBRARIES_ALL})
  get_filename_component(lib_name ${lib} NAME_WE)
  set(should_exclude FALSE)
  foreach(pattern ${EXCLUDE_PATTERNS})
    if(lib_name MATCHES "${pattern}")
      set(should_exclude TRUE)
      break()
    endif()
  endforeach()

  if(NOT should_exclude)
    list(APPEND CLANG_STATIC_LIBRARIES "${lib}")
  else()
    message(STATUS "Excluding ${lib_name}")
  endif()
endforeach()

if(NOT CLANG_STATIC_LIBRARIES)
  message(FATAL_ERROR "No Clang static libraries found in ${CLANG_LIBDIR}")
endif()
list(LENGTH CLANG_STATIC_LIBRARIES CLANG_LIB_COUNT)
message(
  STATUS "Found ${CLANG_LIB_COUNT} Clang static libraries (after exclusions)")

# Try to infer Clang include directory from library path
get_filename_component(CLANG_INSTALL_PREFIX "${CLANG_LIBDIR}" DIRECTORY)
set(CLANG_INCLUDE_DIRS "${CLANG_INSTALL_PREFIX}/include")
if(EXISTS ${CLANG_INCLUDE_DIRS})
  include_directories(${CLANG_INCLUDE_DIRS})
  message(STATUS "Using Clang include directory: ${CLANG_INCLUDE_DIRS}")
else()
  message(
    WARNING
      "Could not find Clang include directory at ${CLANG_INCLUDE_DIRS}, you may need to set include paths manually"
  )
endif()

# Include project headers
include_directories(include)

# Configure clang-tidy (enabled by default)
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE)
  message(STATUS "Found clang-tidy: ${CLANG_TIDY_EXE}")
  set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
                           "-header-filter=^${CMAKE_CURRENT_SOURCE_DIR}/.*")
else()
  message(WARNING "clang-tidy not found - static analysis disabled")
endif()

# Add library subdirectory
add_subdirectory(lib)

# Installation for headers
set(INSPECTOR_INCLUDE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(INSPECTOR_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
file(GLOB_RECURSE INSPECTOR_HEADERS "${INSPECTOR_INCLUDE_SOURCE_DIR}/*.h")

install(FILES ${INSPECTOR_HEADERS} DESTINATION "${INSPECTOR_INCLUDE_DIR}")

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
