add_library(inspector SHARED inspector.cpp)
target_link_libraries(inspector libcling)
target_compile_options(inspector PUBLIC -DLLVMDIR="${LLVM_INSTALL_PREFIX}"
  -I${LLVM_INSTALL_PREFIX}/include)

set_target_properties(inspector PROPERTIES ENABLE_EXPORTS 1)

install(TARGETS inspector RUNTIME LIBRARY DESTINATION lib)
