# Socket side of the REPL, shared with the end-to-end tests.
add_library(inspector-client STATIC connection.cpp)
target_include_directories(
  inspector-client
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
separate_arguments(_llvm_defs NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(inspector-client PUBLIC ${_llvm_defs})
target_compile_options(inspector-client PRIVATE -Wall -Wextra)
target_link_libraries(inspector-client PUBLIC ${INSPECTOR_CLANG_LIBS})

add_executable(inspector-repl main.cpp highlight.cpp)
# Next to the `inspector` stub, where `inspector repl` looks for it.
set_target_properties(inspector-repl PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                ${PROJECT_BINARY_DIR})
target_include_directories(inspector-repl SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS})
target_compile_options(inspector-repl PRIVATE -Wall -Wextra)
target_link_libraries(inspector-repl PRIVATE inspector inspector-client replxx)
install(TARGETS inspector-repl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
