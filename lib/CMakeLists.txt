add_library(inspector SHARED inspector.cpp PracticalSocket.cpp)
target_link_libraries(inspector libcling jsoncpp)
target_compile_options(inspector PUBLIC -DLLVMDIR="${LLVM_INSTALL_PREFIX}"
  -I${LLVM_INSTALL_PREFIX}/include)

set_target_properties(inspector PROPERTIES ENABLE_EXPORTS 1)

install(TARGETS inspector EXPORT inspectorTargets RUNTIME LIBRARY DESTINATION lib)
