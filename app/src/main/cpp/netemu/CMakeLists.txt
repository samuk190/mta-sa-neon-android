# libsaonline_netemu.so: the PC netc.dll emulator (see README.md).
#
# A separate library so it costs nothing unless "Use PC Network — Compatibility"
# is on: libsaonlineneon.so dlopens it on demand and calls netemu_get_api().
# Only that symbol is exported.
#
# Unicorn 2.1.4 (GPLv2, x86 only) is built from source. For offline builds,
# point FETCHCONTENT_SOURCE_DIR_UNICORN at an unpacked 2.1.4 tree (Gradle
# passes it from UNICORN_SOURCE_DIR in .env).

include(FetchContent)

# Unicorn's own options, set before it is configured (policy CMP0077).
set(UNICORN_ARCH "x86" CACHE STRING "Unicorn architectures for netemu" FORCE)
set(UNICORN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UNICORN_INSTALL OFF CACHE BOOL "" FORCE)
set(UNICORN_LEGACY_STATIC_ARCHIVE OFF CACHE BOOL "" FORCE)
set(UNICORN_FUZZ OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)

# A debug APK builds at -O0, where the emulator is too slow to finish netc's
# join step before the server times out. Unicorn and netemu are built
# optimised in every variant (these flags only reach this directory).
string(APPEND CMAKE_C_FLAGS_DEBUG " -O2")
string(APPEND CMAKE_CXX_FLAGS_DEBUG " -O2")

FetchContent_Declare(unicorn
    URL https://github.com/unicorn-engine/unicorn/archive/refs/tags/2.1.4.tar.gz
    URL_HASH SHA256=ea8863f095a0136388694e5a6063afd9bb7650e30243dd6251af59c5ce5601f4)
FetchContent_MakeAvailable(unicorn)

file(GLOB NETEMU_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(saonline_netemu SHARED ${NETEMU_SOURCES})
set_target_properties(saonline_netemu PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(saonline_netemu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_options(saonline_netemu PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(saonline_netemu PRIVATE unicorn)
if(ANDROID)
    target_link_libraries(saonline_netemu PRIVATE log)
    target_link_options(saonline_netemu PRIVATE "-Wl,--no-undefined" "-Wl,--exclude-libs,ALL"
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/netemu.map")
    set_property(TARGET saonline_netemu APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/netemu.map")
endif()
# Same 16 KB LOAD alignment as libsaonlineneon.so.
target_link_options(saonline_netemu PRIVATE
    "-Wl,-z,max-page-size=16384"
    "-Wl,-z,common-page-size=16384")
