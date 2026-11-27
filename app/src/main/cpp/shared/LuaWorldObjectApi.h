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

// Server RPCs of a CDeathmatchObject (CObjectRPCs.cpp:30-109). Each acts on the object only; the Lua moveObject,
// stopObject and setObjectScale also walk the children (call propagation), the RPCs do not. A server object is adopted
// on first use. False when id is not an object. VM thread.
//
// MOVE_OBJECT: CPositionRotationAnimation::FromBitStream (CPositionRotationAnimation.cpp:111-178) then
// CDeathmatchObject::StartMovement (CDeathmatchObject.cpp:53-74). Rotations are radians. With deltaRotation the
// target rotation is the source rotation plus `targetRot`. elapsedMs is the resume-mode time already spent, totalMs
// the whole duration; an animation with nothing left snaps to the final value.
struct ServerObjectMove {
    float sourcePos[3] = {}, sourceRot[3] = {};
    float targetPos[3] = {}, targetRot[3] = {};
    bool deltaRotation = false;
    bool linear = true;
    uint32_t elapsedMs = 0;
    uint32_t totalMs = 0;
};
// CEasingCurve::GetEasingTypeFromString: true when name is one of the easing types.
bool IsKnownEasingName(const char* name);
bool ServerMoveObject(uint32_t id, const ServerObjectMove& move);
// STOP_OBJECT: StopMovement then SetOrientation(position, rotation radians).
bool ServerStopObject(uint32_t id, const float position[3], const float rotation[3]);
// SET_OBJECT_ROTATION: SetRotationRadians. A running move keeps writing its own rotation, as on desktop.
bool ServerSetObjectRotation(uint32_t id, float rx, float ry, float rz);
// SET_OBJECT_SCALE: CClientObject::SetScale.
bool ServerSetObjectScale(uint32_t id, float x, float y, float z);
// OBJECT_SYNC (88): updates an adopted server object's position/rotation (radians) so Lua getters see it.
bool ServerSyncObject(uint32_t id, bool hasPos, const float pos[3], bool hasRot, const float rot[3]);
// What a test reads back: position, rotation (radians), scale, move running.
bool CopyObjectState(uint32_t id, float pos[3], float rot[3], float scale[3], bool& moving);
// World-object damage/break events (CClientWorldObjectManager.cpp:252, 276).
bool WorldObjectNoteDamage(uint32_t proxyId, float loss, uint32_t attackerId);
bool WorldObjectNoteBreak(uint32_t proxyId, uint32_t attackerId);

}  // namespace WorldRPG::Script
