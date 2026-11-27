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

// CElementRPCs custom weapon RPCs (181-190). VM thread.
void RpcFireCustomWeapon(uint32_t id);
void RpcSetCustomWeaponState(uint32_t id, int8_t state);
void RpcSetCustomWeaponAmmo(uint32_t id, int32_t ammo);
void RpcSetCustomWeaponClipAmmo(uint32_t id, int32_t ammo);
void RpcSetCustomWeaponTargetPos(uint32_t id, float x, float y, float z);
void RpcSetCustomWeaponTargetElement(uint32_t id, uint32_t targetId, int8_t bone);
void RpcResetCustomWeaponTarget(uint32_t id);
void RpcSetCustomWeaponFlags(uint32_t id, bool disableModel, bool instantReload, bool shootBlocked,
                             bool shootOutOfRange, bool checkBuildings, bool checkCarTires, bool checkDummies,
                             bool checkObjects, bool checkPeds, bool checkVehicles, bool ignoreSomeObjects,
                             bool seeThrough, bool shootThrough);
void RpcSetCustomWeaponFiringRate(uint32_t id, int32_t rateMs);
void RpcResetCustomWeaponFiringRate(uint32_t id);
void RpcSetWeaponOwner(uint32_t id, uint32_t ownerId);
void RpcSetWeaponPropertyScalar(uint8_t weaponId, uint8_t skill, const char* property, double value);
void RpcSetWeaponPropertyFlag(uint8_t weaponId, uint8_t skill, int flagBit, bool enable);
}
