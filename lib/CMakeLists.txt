add_library(inspector SHARED repl.cpp prompt.cpp completer.cpp
                              compat.cpp)
add_library(inspector::inspector ALIAS inspector)

target_include_directories(
  inspector PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                   $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# LLVM/Clang stay private: programs being inspected only see repl.h.
target_include_directories(inspector SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS}
                                                    ${CLANG_INCLUDE_DIRS})
separate_arguments(_llvm_defs NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(
  inspector PRIVATE ${_llvm_defs}
                    INSPECTOR_CLANG_RESOURCE_DIR="${CLANG_RESOURCE_DIR}")
target_compile_options(inspector PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(inspector PRIVATE ${INSPECTOR_CLANG_LIBS}
                                        Threads::Threads)

install(
  TARGETS inspector
  EXPORT inspectorTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
