#pragma once

#include <cstdint>
#include <string>

struct lua_State;

namespace WorldRPG::Script {

// CLuaCameraDefs.cpp LoadFunctions table (68-119) and the handlers through 932.
void InstallCameraLua(lua_State* L);
// Move, track and fade timers. Called from the VM pulse before onClientPreRender.
void PulseCamera();
// CClientCamera::ReleaseScriptCamera(owner) when that resource stops.
void ReleaseCameraOwner(const std::string& owner);
// The element getCamera returns. Not a server id.
bool CameraElementAlive(uint32_t id);

}  // namespace WorldRPG::Script
