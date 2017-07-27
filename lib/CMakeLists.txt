add_library(inspector SHARED inspector.cpp PracticalSocket.cpp)
target_link_libraries(inspector libcling ${JSONCPP_LIBRARIES})
target_include_directories(inspector PUBLIC ${JSONCPP_INCLUDE_DIRS})
target_compile_options(inspector PUBLIC -DLLVMDIR="${LLVM_INSTALL_PREFIX}"
  -I${LLVM_INSTALL_PREFIX}/include ${JSONCPP_CFLAGS_OTHER})

set_target_properties(inspector PROPERTIES ENABLE_EXPORTS 1)

install(TARGETS inspector EXPORT inspectorTargets RUNTIME LIBRARY DESTINATION lib)
