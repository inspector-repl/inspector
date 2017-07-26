find_package(PythonInterp 3 REQUIRED)

set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
set(SETUP_PY "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
configure_file(${SETUP_PY_IN} ${SETUP_PY})

set(BINSTUB_IN "${CMAKE_CURRENT_SOURCE_DIR}/inspector.in")
set(BINSTUB "${CMAKE_CURRENT_BINARY_DIR}/../inspector")
configure_file(${BINSTUB_IN} ${BINSTUB})

set(LIBPATH_IN "${CMAKE_CURRENT_SOURCE_DIR}/inspector/config.py.in")
set(LIBPATH "${CMAKE_CURRENT_BINARY_DIR}/build/lib/inspector/config.py")
configure_file(${LIBPATH_IN} ${LIBPATH} @ONLY)

set(SETUP_PY "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
file(GLOB_RECURSE PYTHON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.py")
LIST(APPEND PYTHON_FILES "setup.py.in;inspector.in;inspector/config.py.in")
set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/build/timestamp")

add_custom_command(OUTPUT ${OUTPUT}
                   COMMAND ${PYTHON_EXECUTABLE} ${SETUP_PY} build
                   COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
                   DEPENDS ${PYTHON_FILES})

add_custom_target(target ALL DEPENDS ${OUTPUT})

install(CODE "execute_process(COMMAND ${PYTHON_EXECUTABLE} ${SETUP_PY} install -f --prefix=${CMAKE_INSTALL_PREFIX})")
