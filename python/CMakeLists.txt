find_package(PythonInterp 3 REQUIRED)

# Configure the config.py file with library paths
set(LIBPATH_IN "${CMAKE_CURRENT_SOURCE_DIR}/inspector/config.py.in")
set(LIBPATH "${CMAKE_CURRENT_SOURCE_DIR}/inspector/config.py")
configure_file(${LIBPATH_IN} ${LIBPATH} @ONLY)

# Configure the binary stub
set(BINSTUB_IN "${CMAKE_CURRENT_SOURCE_DIR}/inspector.in")
set(BINSTUB "${CMAKE_CURRENT_BINARY_DIR}/../inspector")
configure_file(${BINSTUB_IN} ${BINSTUB})

# Copy pyproject.toml to build directory
configure_file("${CMAKE_SOURCE_DIR}/pyproject.toml" "${CMAKE_CURRENT_BINARY_DIR}/pyproject.toml" COPYONLY)

# Build the Python package using hatchling
file(GLOB_RECURSE PYTHON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.py")
LIST(APPEND PYTHON_FILES "${CMAKE_SOURCE_DIR}/pyproject.toml;inspector.in;inspector/config.py.in")
set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dist/timestamp")

add_custom_command(OUTPUT ${OUTPUT}
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/python
                   COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/pyproject.toml ${CMAKE_CURRENT_BINARY_DIR}/
                   COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR} ${PYTHON_EXECUTABLE} -m hatchling build -t wheel
                   COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
                   DEPENDS ${PYTHON_FILES}
                   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_custom_target(python_target ALL DEPENDS ${OUTPUT})

# Install the wheel
install(CODE "
  file(GLOB WHEEL_FILES \"${CMAKE_CURRENT_BINARY_DIR}/dist/*.whl\")
  if(WHEEL_FILES)
    foreach(wheel \${WHEEL_FILES})
      execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -m pip install --prefix=${CMAKE_INSTALL_PREFIX} --no-deps --no-build-isolation --force-reinstall \${wheel}
        RESULT_VARIABLE install_result
      )
      if(NOT install_result EQUAL 0)
        message(FATAL_ERROR \"Failed to install Python package: \${wheel}\")
      endif()
    endforeach()
  else()
    message(FATAL_ERROR \"No wheel files found in ${CMAKE_CURRENT_BINARY_DIR}/dist/\")
  endif()
")