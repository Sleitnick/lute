add_library(Lute.Socket STATIC)

target_sources(Lute.Socket PRIVATE
    include/lute/socket.h

    src/socket.cpp
)

target_compile_features(Lute.Socket PUBLIC cxx_std_17)
target_include_directories(Lute.Socket PUBLIC "include" ${LIBUV_INCLUDE_DIR})
target_link_libraries(Lute.Socket PRIVATE Lute.Runtime Luau.VM uv_a)
target_compile_options(Lute.Socket PRIVATE ${LUTE_OPTIONS})
