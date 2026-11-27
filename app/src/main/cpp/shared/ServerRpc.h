#pragma once

// Server to client RPCs of PACKET_ID_LUA (76) and PACKET_ID_LUA_ELEMENT_RPC (77) that change script-visible
// state: world, camera, HUD, money, sound, input binds, attach, vehicle and object RPCs.
//
// Desktop dispatch is CRPCFunctions::ProcessPacket (CRPCFunctions.cpp:125-172). The net thread decodes a packet
// here, with bounds checks on every read, and queues one operation per RPC. Pump() runs the queue on the Lua VM
// thread (LuaVMStub::pumpDx), because the state these RPCs write (the camera, the world settings, key binds,
// objects) is owned by the Lua bindings and is not locked. Calls into the thread-safe game layer (ServerWorld, Game)
// are made from the same queue so the order of two RPCs is kept.
//
// A packet that cannot be decoded is Malformed and changes nothing, as the desktop handlers do when a Read fails.

#include <cstddef>
#include <cstdint>

namespace WorldRPG::Net {
class BitStreamReader;
}

namespace WorldRPG::ServerRpc {

enum class Result {
    NotHandled,  // not an RPC this module owns
    Handled,
    Malformed,   // truncated or out of range; nothing was queued
};

// PACKET_ID_LUA after the function id has been read.
Result HandleGlobal(uint8_t func, Net::BitStreamReader& reader);
// PACKET_ID_LUA_ELEMENT_RPC after the function id and ElementID have been read.
Result HandleElement(uint8_t func, uint32_t id, Net::BitStreamReader& reader);

// VM thread. Runs every queued operation in arrival order.
void Pump();
// Operations waiting for Pump (tests, and the log line that reports a stalled VM).
size_t Pending();
// Drops the queue. A new session starts clean (ServerWorld::ResetSession).
void ResetSession();

// CClientCamera::SetSyncTimeContext of the last camera RPC (CCameraRPCs.cpp:32-36, 96-101).
uint8_t CameraSyncTimeContext();

}  // namespace WorldRPG::ServerRpc
