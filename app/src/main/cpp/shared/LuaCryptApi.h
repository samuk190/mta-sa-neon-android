#pragma once

#include <cstdint>
#include <string>

struct lua_State;

namespace WorldRPG::Script {

// encodeString, decodeString, passwordHash, passwordVerify, generateKeyPair
// (CLuaCryptDefs.cpp:20-1264). Called by InstallUtilLua; binds the state that
// owns the callback references.
void InstallCryptLua(lua_State* L);

// The async form (a callback argument) runs on a worker thread, as desktop
// CLuaShared::GetAsyncTaskScheduler does. Results reach Lua here, on the VM
// thread, from ResourceLoader::Pump.
void CryptDeliverCompletions();
// Drops pending callbacks of a stopped resource (its CLuaMain is gone on desktop).
void CryptDropOwner(const std::string& owner, uint16_t netId, uint32_t generation);
// Drops every pending callback while the state is alive (ResourceLoader::Reset).
void CryptAbandonAll();

// Host tests: waits until the worker has finished every queued task.
void CryptWaitIdleForTests();

}  // namespace WorldRPG::Script
