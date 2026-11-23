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

}  // namespace WorldRPG::Script
