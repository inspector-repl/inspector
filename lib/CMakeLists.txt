# Source files
set(SOURCES
  socket.cpp
  socket_path.cpp
  repl.cpp
  prompt.cpp
)

# Create library
if(BUILD_SHARED_LIBS)
  add_library(inspector SHARED ${SOURCES})
else()
  add_library(inspector STATIC ${SOURCES})
endif()

# Set properties
set_target_properties(inspector PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  ENABLE_EXPORTS 1
)

# Include directories
target_include_directories(inspector PUBLIC 
  ${JSONCPP_INCLUDE_DIRS}
  ${LLVM_INCLUDE_DIRS}
  ${CLANG_INCLUDE_DIRS}
)

# Compile definitions
target_compile_definitions(inspector PUBLIC 
  LLVMDIR="${LLVM_INSTALL_PREFIX}"
)

# Compile options
target_compile_options(inspector PUBLIC 
  ${JSONCPP_CFLAGS_OTHER}
  -Wall -Wextra -O2
)

# Find system libraries
find_package(Threads REQUIRED)

# macOS-specific dependencies
if(APPLE)
  # Find libffi
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFI libffi)
endif()

# Link libraries
if(CLANG_LIBDIR AND EXISTS ${CLANG_LIBDIR})
  # When using custom Clang libraries, use the group linking approach
  # to resolve circular dependencies
  if(APPLE)
    # macOS doesn't support --start-group/--end-group
    # Use -all_load to force loading all symbols from static libraries
    target_link_libraries(inspector
      -Wl,-all_load
      ${CLANG_STATIC_LIBRARIES}
      ${LLVM_STATIC_LIBS_STRING}
      ${JSONCPP_LIBRARIES}
      ${ZLIB_LIBRARIES}
      ${CMAKE_DL_LIBS}
      Threads::Threads
      # macOS system frameworks
      "-framework CoreFoundation"
      "-framework CoreServices"
    )
    
    # Add libffi if found
    if(FFI_FOUND)
      target_link_libraries(inspector ${FFI_LIBRARIES})
      target_include_directories(inspector PUBLIC ${FFI_INCLUDE_DIRS})
    endif()
  else()
    # Linux/Unix supports --start-group/--end-group for circular dependencies
    target_link_libraries(inspector 
      -Wl,--start-group
      ${CLANG_STATIC_LIBRARIES}
      ${LLVM_STATIC_LIBS_STRING}
      -Wl,--end-group
      ${JSONCPP_LIBRARIES}
      ${ZLIB_LIBRARIES}
      ${CMAKE_DL_LIBS}
      Threads::Threads
    )
  endif()
  
  # Add math library if needed
  if(UNIX)
    target_link_libraries(inspector m)
  endif()
  
  # Add LLVM linker flags
  target_link_options(inspector PRIVATE ${LLVM_LDFLAGS})
else()
  message(FATAL_ERROR "CLANG_LIBDIR must be specified. Clang libraries cannot be linked without specifying the directory.")
endif()

# Installation
install(TARGETS inspector 
  EXPORT inspectorTargets 
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
