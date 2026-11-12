cmake_minimum_required(VERSION 3.22.1)
project(WorldRPGNeon LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ARM NEON optimization for mobile
# ATENCAO: -mfpu=neon e -mfloat-abi=softfp sao flags de ARM 32-bit. Em
# arm64-v8a (aarch64) o NEON e' obrigatorio e o compilador rejeita essas flags.
# Por isso separamos por ABI.
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    add_compile_options(-mfpu=neon -mfloat-abi=softfp)
elseif(ANDROID_ABI STREQUAL "arm64-v8a")
    # aarch64: NEON (ASIMD) e' parte da baseline; nada a adicionar.
    add_compile_options(-DANDROID_ARM_NEON=TRUE)
endif()

# Source files
file(GLOB_RECURSE NATIVE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/jni/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/hook/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/gtasa/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/*.cpp"
)

# Exclude CNet.cpp from glob since it's included directly by JNI_OnLoad.cpp
list(FILTER NATIVE_SOURCES EXCLUDE REGEX ".*/net/CNet\\.cpp$")

# The production path is net/mta/MtaClientTransport (OPEN51 + cipher).
# Stock RakNet Connect() is ignored by the official MTA net module.
list(FILTER NATIVE_SOURCES EXCLUDE REGEX ".*/net/raknet/RakNetAdapter\\.cpp$")
list(FILTER NATIVE_SOURCES EXCLUDE REGEX ".*/net/mta/RakNetDirectTransport\\.cpp$")

# ---------------------------------------------------------------------------
# Lua 5.3.6 Static Library (vendored for Android NDK cross-compilation)
# Source: https://www.lua.org/ftp/lua-5.3.6.tar.gz
# License: MIT
# Phase 5: Replaces LuaVMStub with real lua_State interpreter.
# ---------------------------------------------------------------------------
add_subdirectory(lua53)

# ---------------------------------------------------------------------------
# Opus 1.4 Static Library (vendored for Android NDK cross-compilation)
# Source: https://downloads.xiph.org/releases/opus/opus-1.4.tar.gz
# License: BSD-3-Clause
# Phase 6: Replaces VoiceCodecStub with real Opus encoder/decoder.
# ---------------------------------------------------------------------------
add_subdirectory(opus)

add_library(WorldRPGNeon SHARED ${NATIVE_SOURCES})

target_include_directories(WorldRPGNeon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/net
    ${CMAKE_CURRENT_SOURCE_DIR}/net/raknet
    ${CMAKE_CURRENT_SOURCE_DIR}/hook
    ${CMAKE_CURRENT_SOURCE_DIR}/gtasa
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
)

target_link_libraries(WorldRPGNeon
    android
    log
    EGL
    GLESv2
    z
    lua53
    opus
)

# Resolve every native dependency during the Android link
if(ANDROID)
    target_link_options(WorldRPGNeon PRIVATE "-Wl,--no-undefined")
endif()

# Align LOAD segments to 16KB pages for modern Android compatibility
target_link_options(WorldRPGNeon PRIVATE
    "-Wl,-z,max-page-size=16384"
    "-Wl,-z,common-page-size=16384"
)