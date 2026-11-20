#pragma once

// Packet and element-RPC ids used by the ped port (docs/ROADMAP_PEDS_SYNC.md step 0.2).
//
// The values were produced by compiling mta_neon_source/Shared/sdk/net/Packets.h and rpc_enums.h and printing
// the enumerators (2026-11-14). Do not hand-count the enums: blank lines and the jump of SET_PED_ANIMATION_SPEED
// to 200 make that error-prone.
//
// Known disagreement, not yet resolved on a device: PacketHandler routes Lua RPCs on game id 68 and matches
// SHOW_CHAT as 120 (commit fd8ef3f). In the reference, 68 is PACKET_ID_PLAYER_RCON_BAN and SHOW_CHAT is 119;
// RPCs arrive on PACKET_ID_LUA (76) and PACKET_ID_LUA_ELEMENT_RPC (77), dispatched by
// CRPCFunctions::ProcessPacket (CRPCFunctions.cpp 125-172). The DIAG "ped-roadmap packet" line logs both.

#include <cstdint>

namespace WorldRPG::MtaIds {

namespace Packet {
constexpr uint8_t PlayerWasted = 27;
constexpr uint8_t EntityAdd = 50;
constexpr uint8_t EntityRemove = 51;
constexpr uint8_t PedStartSync = 61;
constexpr uint8_t PedStopSync = 62;
constexpr uint8_t PedSync = 63;
constexpr uint8_t PedWasted = 64;
constexpr uint8_t Lua = 76;
constexpr uint8_t LuaElementRpc = 77;
constexpr uint8_t PedTask = 101;
}  // namespace Packet

// eElementRPCFunctions (rpc_enums.h). NUM_RPC_FUNCS is 225.
namespace Rpc {
constexpr uint8_t SetColshapeRadius = 201;
constexpr uint8_t SetColshapeSize = 202;
constexpr uint8_t AddColPolygonPoint = 203;
constexpr uint8_t RemoveColPolygonPoint = 204;
constexpr uint8_t UpdateColPolygonPoint = 205;
constexpr uint8_t SetColPolygonHeight = 208;
constexpr uint8_t SetElementData = 6;
constexpr uint8_t SetElementPosition = 7;
constexpr uint8_t RemoveElementData = 133;
constexpr uint8_t SetElementVelocity = 8;
constexpr uint8_t SetElementInterior = 9;
constexpr uint8_t SetElementDimension = 10;
constexpr uint8_t SetElementAlpha = 13;
constexpr uint8_t SetElementHealth = 15;
constexpr uint8_t SetElementModel = 16;
constexpr uint8_t SetPedArmor = 25;
constexpr uint8_t SetPedRotation = 26;
constexpr uint8_t SetPedMoveAnim = 33;
constexpr uint8_t WarpPedIntoVehicle = 34;
constexpr uint8_t RemovePedFromVehicle = 35;
constexpr uint8_t SetPedAnimation = 37;
constexpr uint8_t SetPedAnimationProgress = 38;
constexpr uint8_t SetPedOnFire = 39;
constexpr uint8_t SetPedHeadless = 40;
constexpr uint8_t SetPedFrozen = 41;
constexpr uint8_t GiveWeapon = 73;
constexpr uint8_t TakeWeapon = 74;
constexpr uint8_t TakeAllWeapons = 75;
constexpr uint8_t SetWeaponAmmo = 76;
constexpr uint8_t SetWeaponSlot = 77;
constexpr uint8_t ShowChat = 119;
constexpr uint8_t SetElementCollisionsEnabled = 148;
constexpr uint8_t SetElementFrozen = 152;
constexpr uint8_t SetPedAnimationSpeed = 200;
constexpr uint8_t NumRpcFuncs = 225;
}  // namespace Rpc

}  // namespace WorldRPG::MtaIds
