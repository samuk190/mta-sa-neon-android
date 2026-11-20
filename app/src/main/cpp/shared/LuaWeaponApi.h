#pragma once
#include <cstdint>
#include <string>
struct lua_State;
namespace WorldRPG::Script {
void InstallWeaponLua(lua_State* L);
// createWeapon elements (CClientWeapon). VM thread.
bool LocalWeaponAlive(uint32_t id);
bool ReleaseLocalWeapon(uint32_t id);
void WeaponsDropOwner(const std::string& owner, uint16_t netId, uint32_t generation);
// CClientWeaponManager::DoPulse. Call once per VM pulse.
void WeaponsPulse();
// A new session starts without the old session's weapons (LuaVMStub::shutdown).
void WeaponsReset();
}
