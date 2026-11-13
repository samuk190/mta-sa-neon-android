cmake_minimum_required(VERSION 3.22.1)
project(lua53 LANGUAGES C)

# Lua 5.3.6 core sources (excluding lua.c/luac.c which are standalone interpreters)
set(LUA_CORE_SOURCES
    src/lapi.c
    src/lcode.c
    src/lctype.c
    src/ldebug.c
    src/ldo.c
    src/ldump.c
    src/lfunc.c
    src/lgc.c
    src/llex.c
    src/lmem.c
    src/lobject.c
    src/lopcodes.c
    src/lparser.c
    src/lstate.c
    src/lstring.c
    src/ltable.c
    src/ltm.c
    src/lundump.c
    src/lvm.c
    src/lzio.c
    # Standard libraries
    src/lauxlib.c
    src/lbaselib.c
    src/lbitlib.c
    src/lcorolib.c
    src/ldblib.c
    src/liolib.c
    src/lmathlib.c
    src/loadlib.c
    src/loslib.c
    src/lstrlib.c
    src/ltablib.c
    src/lutf8lib.c
    src/linit.c
)

add_library(lua53 STATIC ${LUA_CORE_SOURCES})

target_include_directories(lua53 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Lua requires POSIX; Android Bionic provides it natively
if(ANDROID)
    target_compile_definitions(lua53 PRIVATE LUA_USE_POSIX)
else()
    target_compile_definitions(lua53 PRIVATE LUA_USE_LINUX)
endif()

# Suppress warnings in vendored C code
target_compile_options(lua53 PRIVATE -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-string-plus-int)

# Align to 16KB pages for Android 15+ compatibility
if(ANDROID)
    set_target_properties(lua53 PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

set_target_properties(lua53 PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 8)