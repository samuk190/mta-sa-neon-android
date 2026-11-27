#pragma once

// Client-side MTA API bindings that do not need the GTA engine: chat output, element
// data, element helpers and no-op natives for engine features the phone does not
// expose yet. InstallExtraBindings runs at the end of installDxBindings, so a
// binding here replaces a core binding or an "unimplemented" stub of the same name.

#include <cstdint>

struct lua_State;

namespace WorldRPG::ServerWorld { struct ElementDataChange; }

namespace WorldRPG::Script {

void InstallExtraBindings(lua_State* L);
// Applies a server custom data change to the script store and fires
// onClientElementDataChange (CClientEntity::SetCustomData / DeleteCustomData).
void ApplyServerElementData(lua_State* L, const ServerWorld::ElementDataChange& change);
// Forgets the values setElementData stored for an element that is gone, so a reused id starts clean.
// L is the primary state.
void DropClientElementData(lua_State* L, uint32_t id);

// Element tables are {id = <id>, element = true}, as every element binding expects.
void PushElement(lua_State* L, uint32_t id);
uint32_t ElementIdOf(lua_State* L, int index);

// CPlayerRPCs::SetPlayerMoney (CPlayerRPCs.cpp:29-42): the value getPlayerMoney reports. VM thread only.
void RpcSetPlayerMoney(int32_t amount, bool instant);
long long ScriptPlayerMoney();

// CInputRPCs::ShowChat (CInputRPCs.cpp:293-304): the state isChatVisible reports and the chat view. VM thread only.
void RpcSetChatVisible(bool visible, bool inputBlocked);

// Thread-safe vehicle flag updates from server sync packets (VEHICLE_RESYNC, VEHICLE_SPAWN,
// VEHICLE_DAMAGE_SYNC, VEHICLE_TRAILER, EXPLOSION). Backed by LuaVehicleApi.cpp's locked g_vehicleFlags.
void VehicleApplyDamageSync(uint32_t id, const bool doorChanged[6], const uint8_t doors[6],
                            const bool wheelChanged[4], const uint8_t wheels[4],
                            const bool panelChanged[7], const uint8_t panels[7],
                            const bool lightChanged[4], const uint8_t lights[4]);
void VehicleApplyFixState(uint32_t id, bool resetSpawnDefaults);
void VehicleApplyBlownState(uint32_t id, bool blown);
void VehicleApplyTrailerLink(uint32_t vehicleId, uint32_t trailerId, bool attached);
void VehicleDetachTowedBy(uint32_t vehicleId);
void VehicleFlagsResetSession();

} // namespace WorldRPG::Script
