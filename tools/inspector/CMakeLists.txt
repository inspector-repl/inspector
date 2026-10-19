# Header generation and compiler wrapping, shared by both binaries.
add_library(inspector-driver STATIC driver.cpp prebuild.cpp)
target_include_directories(
  inspector-driver
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
separate_arguments(_llvm_defs NATIVE_COMMAND "${LLVM_DEFINITIONS}")
# Paths are relative to the binaries so the install can be relocated and
# the build tree never ends up in the package.
file(RELATIVE_PATH _rel_libdir ${CMAKE_INSTALL_FULL_BINDIR}
     ${CMAKE_INSTALL_FULL_LIBDIR})
file(RELATIVE_PATH _rel_includedir ${CMAKE_INSTALL_FULL_BINDIR}
     ${CMAKE_INSTALL_FULL_INCLUDEDIR})
target_compile_definitions(
  inspector-driver
  PUBLIC ${_llvm_defs}
  PRIVATE
    INSPECTOR_CLANG_RESOURCE_DIR="${CLANG_RESOURCE_DIR}"
    INSPECTOR_REL_LIBDIR="${_rel_libdir}"
    INSPECTOR_REL_INCLUDEDIR="${_rel_includedir}"
    INSPECTOR_LIBRARY_FILE="$<TARGET_FILE_NAME:inspector>")
target_compile_options(inspector-driver PRIVATE -Wall -Wextra)
target_link_libraries(inspector-driver PUBLIC ${INSPECTOR_CLANG_LIBS}
                                              Threads::Threads)

# In the build tree the binaries find headers through this link.
file(CREATE_LINK ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include
     SYMBOLIC)

add_executable(inspector-tool main.cpp)
add_executable(inspector-cxx cxx_main.cpp)
foreach(_tool inspector-tool inspector-cxx)
  target_compile_options(${_tool} PRIVATE -Wall -Wextra)
  target_link_libraries(${_tool} PRIVATE inspector-driver)
  # Next to lib/, where the binaries look for libinspector in the build tree.
  set_target_properties(${_tool} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                            ${PROJECT_BINARY_DIR})
endforeach()
set_target_properties(inspector-tool PROPERTIES OUTPUT_NAME inspector)
set_target_properties(inspector-cxx PROPERTIES OUTPUT_NAME inspector-c++)
install(TARGETS inspector-tool inspector-cxx
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
