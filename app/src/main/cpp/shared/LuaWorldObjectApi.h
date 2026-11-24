#pragma once

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {

void InstallWorldObjectLua(lua_State* L);

// Position stored for a client or server object, after sampling an active move.
// False when this id is not an object record.
bool CopyObjectPose(uint32_t id, float& x, float& y, float& z);
// CClientObject::IsLowLod / GetLowLodObject. False when this id is not an object record.
// createdLow is the createObject lowLOD flag. linkedLow is 0 when this object is low LOD or unlinked.
bool CopyObjectLod(uint32_t id, bool& createdLow, uint32_t& linkedLow);
// CClientObject::SetLowLodObject. clear drops the link. False when this object is low LOD,
// the target is not a low LOD object, or a clear finds no link.
bool SetObjectLod(uint32_t id, uint32_t lowId, bool clear);
// Publishes the client objects to the game-thread streamer (WorldObjectFeed). VM thread, once per pump.
void PublishClientObjects();
// setElementRotation / setElementFrozen on an object (radians, createObject's order).
// False when id is not an object.
// A client object, or a server object (adopted on first use).
bool IsWorldObject(uint32_t id);
bool SetObjectRotation(uint32_t id, float rx, float ry, float rz);
bool SetObjectFrozen(uint32_t id, bool frozen);

}  // namespace WorldRPG::Script
