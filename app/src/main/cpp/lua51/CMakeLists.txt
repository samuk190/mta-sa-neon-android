cmake_minimum_required(VERSION 3.22.1)
project(lua51 LANGUAGES C)

# Lua 5.1.5 (MIT, lua.org) with luautf8 0.1.1 (MIT), as one translation unit
# plus utf8. Every global symbol is renamed to lua51_* (lua51_prefix.h), so it
# links beside lua53. Changes from upstream are marked NEON; see README.md.
add_library(lua51 STATIC lua51_all.c lua51_utf8.c)
target_include_directories(lua51 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(ANDROID)
    target_compile_definitions(lua51 PRIVATE LUA_USE_POSIX)
    set_target_properties(lua51 PROPERTIES POSITION_INDEPENDENT_CODE ON)
else()
    target_compile_definitions(lua51 PRIVATE LUA_USE_LINUX)
endif()
target_compile_options(lua51 PRIVATE -w)  # vendored C, unchanged warnings
# Lua 5.1 keeps a string's bytes after its header and reads them as
# (TString*)ts + 1 (getstr, lobject.h:210). Bionic's FORTIFY sees a pointer
# past the end of the header and aborts the first strchr on one (the weak
# table check in lgc.c:167, at luaopen_math on a device). Upstream code is
# correct; build it without FORTIFY instead of changing it.
target_compile_options(lua51 PRIVATE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
