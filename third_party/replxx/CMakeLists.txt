# Vendored on purpose: distros ship upstream replxx 0.0.4 (2021), which lacks
# the fork's paste, redraw and crash fixes. Update by replacing include/ and
# src/ and bumping VERSION.
file(GLOB _replxx_sources CONFIGURE_DEPENDS src/*.cxx src/*.cpp)
add_library(replxx STATIC ${_replxx_sources})
target_include_directories(replxx SYSTEM PUBLIC include)
target_include_directories(replxx PRIVATE src)
target_compile_definitions(replxx PUBLIC REPLXX_STATIC)
set_target_properties(replxx PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(replxx PUBLIC Threads::Threads)
