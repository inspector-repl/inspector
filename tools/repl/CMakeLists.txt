add_executable(inspector-repl main.cpp connection.cpp highlight.cpp)
# Next to the `inspector` stub, where `inspector repl` looks for it.
set_target_properties(inspector-repl PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                ${PROJECT_BINARY_DIR})
target_include_directories(inspector-repl SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS}
                                                         ${CLANG_INCLUDE_DIRS})
separate_arguments(_llvm_defs NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(inspector-repl PRIVATE ${_llvm_defs})
target_compile_options(inspector-repl PRIVATE -Wall -Wextra)
target_link_libraries(inspector-repl PRIVATE inspector replxx
                                             ${INSPECTOR_CLANG_LIBS})
install(TARGETS inspector-repl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
