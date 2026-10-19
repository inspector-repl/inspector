add_executable(prelude prelude.cpp)
target_include_directories(prelude PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME prelude COMMAND prelude)

# Same path users take with make/autotools/meson: CXX=inspector-c++.
set(_e2e_bin ${CMAKE_CURRENT_BINARY_DIR}/test-proc)
add_custom_command(
  OUTPUT ${_e2e_bin}
  COMMAND ${CMAKE_COMMAND} -E env INSPECTOR_CXX=${CMAKE_CXX_COMPILER}
          ${Python3_EXECUTABLE} ${PROJECT_BINARY_DIR}/inspector-c++ -std=c++17
          ${CMAKE_CURRENT_SOURCE_DIR}/test.cpp -o ${_e2e_bin}
  DEPENDS test.cpp inspector python_target
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_custom_target(e2e_program ALL DEPENDS ${_e2e_bin})

add_test(NAME e2e COMMAND ${Python3_EXECUTABLE}
                          ${CMAKE_CURRENT_SOURCE_DIR}/e2e.py ${_e2e_bin})
add_test(NAME repl_tty
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/repl_tty.py
                 $<TARGET_FILE:inspector-repl> ${_e2e_bin})

set_tests_properties(e2e PROPERTIES ENVIRONMENT
                                    INSPECTOR_LLVM_MAJOR=${LLVM_VERSION_MAJOR})
