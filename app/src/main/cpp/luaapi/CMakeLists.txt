cmake_minimum_required(VERSION 3.22.1)
project(luaapi LANGUAGES C)

# The public Lua C API for both VMs (README.md): LuaVmDispatch.c sends each
# call to Lua 5.3 (lua53_*) or, through LuaVm51.c, to Lua 5.1 (lua51_*), and
# LuaAuxLib.c is 5.3's lauxlib on top of it. Link this instead of lua53.
add_library(luaapi STATIC LuaVmDispatch.c LuaVm51.c LuaAuxLib.c)
target_include_directories(luaapi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../lua53/src)
set_source_files_properties(LuaVmDispatch.c LuaVm51.c PROPERTIES COMPILE_OPTIONS "-std=c11")
set_source_files_properties(LuaAuxLib.c PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(luaapi PUBLIC lua53 lua51 m)
if(ANDROID)
    set_target_properties(luaapi PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
