#pragma once

// Client-side MTA API bindings that do not need the GTA engine: chat output, element
// data, element helpers and no-op natives for engine features the phone does not
// expose yet. InstallExtraBindings runs at the end of installDxBindings, so a
// binding here replaces a core binding or an "unimplemented" stub of the same name.

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {

void InstallExtraBindings(lua_State* L);

// Element tables are {id = <id>, element = true}, as every element binding expects.
void PushElement(lua_State* L, uint32_t id);
uint32_t ElementIdOf(lua_State* L, int index);

} // namespace WorldRPG::Script
