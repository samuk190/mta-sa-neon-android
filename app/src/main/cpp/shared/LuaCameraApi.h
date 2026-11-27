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

// Server camera RPCs (CCameraRPCs.cpp 21-196). VM thread only. They act even while a resource holds the camera
// lease, as the desktop RPC does. roll is in degrees, already wrapped to [0, 360); fov is in degrees.
void RpcSetCameraMatrix(float x, float y, float z, float lookX, float lookY, float lookZ, float roll, float fov);
// CCameraRPCs::SetCameraTarget (89-141). False when the element is unknown or not a player, ped or vehicle.
bool RpcSetCameraTarget(uint32_t id);
void RpcSetCameraInterior(uint8_t interior);
void RpcFadeCamera(bool fadeIn, float seconds, uint8_t red, uint8_t green, uint8_t blue);

}  // namespace WorldRPG::Script
