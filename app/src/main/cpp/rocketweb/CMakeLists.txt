cmake_minimum_required(VERSION 3.22.1)
project(rocketweb LANGUAGES C)

# RocketWeb kernel, copied unchanged from the desktop Neon tree
# (vendor/rocketweb): the car-soccer physics as wasm2c output, its arena data,
# Neon's Kernel.c host and the wasm2c runtime. Rights: see NOTICE.txt and
# README.md. Built with the desktop premake settings (vendor/rocketweb/premake5.lua).
file(GLOB ROCKETWEB_RUNTIME runtime/*.c)
add_library(rocketweb STATIC Kernel.c generated/reference.c ${ROCKETWEB_RUNTIME})
target_include_directories(rocketweb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE runtime)
target_compile_definitions(rocketweb PRIVATE
    NEON_WASM_UNSHARED_ONLY WASM_RT_USE_MMAP=0 WASM_RT_MEMCHECK_BOUNDS_CHECK=1 _POSIX_C_SOURCE=200809L)
set_target_properties(rocketweb PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
# The client steps the same kernel as the server, so float results must match
# an x86 build: no fused multiply-add (arm64 compilers fuse by default) and no
# fast-math. Optimized even in debug builds: the generated physics is 9 MB of C.
target_compile_options(rocketweb PRIVATE -w -ffp-contract=off -fno-fast-math -O2)
