#pragma once

// Client-side MTA API bindings that do not need the GTA engine: chat output, element
// data, element helpers and no-op natives for engine features the phone does not
// expose yet. InstallExtraBindings runs at the end of installDxBindings, so a
// binding here replaces a core binding or an "unimplemented" stub of the same name.

#include <cstdint>
#include <string>

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

// CElementRPCs: element parent and name updates. VM thread only.
void RpcSetElementParent(uint32_t id, uint32_t parent);
void RpcSetElementName(uint32_t id, const std::string& name);

// CPacketHandler::Packet_PlayerStats & CPlayerRPCs: stats, nametag, wanted level. VM thread only.
void RpcSetPedStat(uint32_t id, uint16_t stat, float value);
void RpcSetPlayerNametagText(uint32_t id, const std::string& text, bool clear);
void RpcSetPlayerNametagColor(uint32_t id, uint8_t r, uint8_t g, uint8_t b);
void RpcRemovePlayerNametagColor(uint32_t id);
void RpcSetPlayerNametagShowing(uint32_t id, bool showing);
void RpcSetWantedLevel(uint8_t level);

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

// Vehicle RPC state setters (CVehicleRPCs.cpp & CHandlingRPCs.cpp). Thread-safe via g_vehicleFlagsLock.
void VehicleRpcSetDoorsUndamageable(uint32_t id, bool undamageable);
void VehicleRpcSetSirensOn(uint32_t id, bool on);
void VehicleRpcSetLandingGearDown(uint32_t id, bool down);
void VehicleRpcSetHeliRotorSpeed(uint32_t id, float speed);
void VehicleRpcSetDamageState(uint32_t id, uint8_t objectType, uint8_t part, uint8_t state);
void VehicleRpcSetOverrideLights(uint32_t id, uint8_t lights);
void VehicleRpcSetDirtLevel(uint32_t id, float dirt);
void VehicleRpcSetDamageProof(uint32_t id, bool proof);
void VehicleRpcSetPaintjob(uint32_t id, uint8_t paintjob);
void VehicleRpcSetFuelTankExplodable(uint32_t id, bool explodable);
void VehicleRpcSetWheelStates(uint32_t id, const uint8_t states[4]);
void VehicleRpcSetTrainDerailed(uint32_t id, bool derailed);
void VehicleRpcSetTrainDerailable(uint32_t id, bool derailable);
void VehicleRpcSetTrainDirection(uint32_t id, bool clockwise);
void VehicleRpcSetTrainSpeed(uint32_t id, float speed);
void VehicleRpcSetTrainTrack(uint32_t id, uint8_t track);
void VehicleRpcSetTrainPosition(uint32_t id, float position);
void VehicleRpcSetTaxiLightOn(uint32_t id, bool on);
void VehicleRpcSetHeadlightColor(uint32_t id, uint8_t r, uint8_t g, uint8_t b);
void VehicleRpcSetDoorOpenRatio(uint32_t id, uint8_t door, float ratio);
void VehicleRpcSetTurretPosition(uint32_t id, float h, float v);
void VehicleRpcSetVariant(uint32_t id, uint8_t v1, uint8_t v2);
void VehicleRpcGiveSirens(uint32_t id, uint8_t count, uint8_t type, bool f360, bool los, bool rnd, bool silent);
void VehicleRpcRemoveSirens(uint32_t id);
void VehicleRpcSetSirenPoint(uint32_t id, uint8_t point, float x, float y, float z, uint8_t r, uint8_t g, uint8_t b,
                             uint8_t a, uint32_t minAlpha, bool f360, bool los, bool rnd, bool silent);
void VehicleRpcSetPlateText(uint32_t id, const std::string& text);
void VehicleRpcSetNitroActivated(uint32_t id, bool activated);

// Ped & player RPC state setters (CPedRPCs.cpp, CPlayerRPCs.cpp). VM thread.
void RpcForcePlayerMap(bool forced);
void RpcSetPlayerScriptDebugLevel(uint8_t level);
void RpcSetPedJetpack(uint32_t id, bool hasJetpack);
void RpcRemovePedClothes(uint32_t id, uint8_t type);
void RpcSetPedGravity(uint32_t id, float gravity);
void RpcSetPedChoking(uint32_t id, bool choking);
void RpcSetPedFightingStyle(uint32_t id, uint8_t style);
void RpcSetPedMoveAnim(uint32_t id, uint32_t anim);
void RpcSetPedOnFire(uint32_t id, bool onFire);
void RpcSetPedHeadless(uint32_t id, bool headless);
void RpcSetPedFrozen(uint32_t id, bool frozen);
void RpcReloadPedWeapon(uint32_t id);
// CInputRPCs::SetControlState (CInputRPCs.cpp:220-239): server control state update for local player.
void PedSetServerControlState(const std::string& control, bool enabled);

// Element / transfer-box RPC setters (CElementRPCs.cpp, COutputRPCs.cpp). VM thread.
void RpcSetCallPropagationEnabled(uint32_t id, bool enabled);
void RpcSetTransferBoxVisibility(bool visible);

} // namespace WorldRPG::Script
