#pragma once

#include <cstdint>
#include <string>
struct lua_State;
namespace WorldRPG::Script {
void InstallNativeUiLua(lua_State* L);
void NativeUiDropResource(const std::string& name, uint16_t netId, uint32_t generation);
}
