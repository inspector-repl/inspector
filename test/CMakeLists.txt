find_package(GTest REQUIRED)

add_executable(prelude prelude.cpp)
target_include_directories(prelude PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(prelude PRIVATE GTest::gtest_main)
add_test(NAME prelude COMMAND prelude)

add_executable(driver-test driver.cpp)
target_link_libraries(driver-test PRIVATE inspector-driver GTest::gmock_main)
target_compile_definitions(
  driver-test
  PRIVATE INSPECTOR_SOURCE_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/include")
add_test(NAME driver COMMAND driver-test)

# Same path users take with make/autotools/meson: CXX=inspector-c++.
set(_e2e_bin ${CMAKE_CURRENT_BINARY_DIR}/test-proc)
add_custom_command(
  OUTPUT ${_e2e_bin}
  COMMAND ${CMAKE_COMMAND} -E env INSPECTOR_CXX=${CMAKE_CXX_COMPILER}
          $<TARGET_FILE:inspector-cxx> -std=c++17
          ${CMAKE_CURRENT_SOURCE_DIR}/test.cpp -o ${_e2e_bin}
  DEPENDS test.cpp inspector inspector-cxx
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_custom_target(e2e_program ALL DEPENDS ${_e2e_bin})

# Takes the program as argument so the build-system examples reuse it.
add_executable(inspector-e2e e2e.cpp)
target_compile_definitions(inspector-e2e
                           PRIVATE INSPECTOR_LLVM_MAJOR=${LLVM_VERSION_MAJOR})
target_link_libraries(inspector-e2e PRIVATE inspector-client GTest::gmock)
add_test(NAME e2e COMMAND inspector-e2e ${_e2e_bin})

add_executable(inspector-repl-tty repl_tty.cpp)
target_link_libraries(inspector-repl-tty PRIVATE GTest::gmock
                                                 $<$<PLATFORM_ID:Linux>:util>)
add_test(NAME repl_tty COMMAND inspector-repl-tty $<TARGET_FILE:inspector-repl>
                               ${_e2e_bin})

set_tests_properties(e2e repl_tty PROPERTIES TIMEOUT 300)
