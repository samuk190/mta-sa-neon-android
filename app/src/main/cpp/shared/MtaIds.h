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
constexpr uint8_t PlayerChangeNick = 28;
constexpr uint8_t PlayerStats = 29;
constexpr uint8_t Lightsync = 34;
constexpr uint8_t VehicleResync = 35;
constexpr uint8_t Explosion = 37;
constexpr uint8_t Fire = 38;
constexpr uint8_t Projectile = 39;
constexpr uint8_t DetonateSatchels = 40;
constexpr uint8_t DestroySatchels = 41;
constexpr uint8_t ConsoleEcho = 44;
constexpr uint8_t DebugEcho = 45;
constexpr uint8_t EntityAdd = 50;
constexpr uint8_t EntityRemove = 51;
constexpr uint8_t PickupHideShow = 52;
constexpr uint8_t PickupHitConfirm = 53;
constexpr uint8_t VehicleSpawn = 57;
constexpr uint8_t VehicleDamageSync = 59;
constexpr uint8_t VehicleTrailer = 60;
constexpr uint8_t PedStartSync = 61;
constexpr uint8_t PedStopSync = 62;
constexpr uint8_t PedSync = 63;
constexpr uint8_t PedWasted = 64;
constexpr uint8_t VoiceData = 71;
constexpr uint8_t VoiceEnd = 72;
constexpr uint8_t Lua = 76;
constexpr uint8_t LuaElementRpc = 77;
constexpr uint8_t TextItem = 78;
constexpr uint8_t ObjectStartSync = 86;
constexpr uint8_t ObjectStopSync = 87;
constexpr uint8_t ObjectSync = 88;
constexpr uint8_t LatentTransfer = 96;
constexpr uint8_t PedTask = 101;
constexpr uint8_t PlayerNetworkStatus = 103;
constexpr uint8_t ChatClear = 105;
constexpr uint8_t ServerInfoSync = 106;
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
constexpr uint8_t SetPedDoingGangDriveby = 36;
constexpr uint8_t SetPedAnimation = 37;
constexpr uint8_t SetPedAnimationProgress = 38;
constexpr uint8_t SetPedOnFire = 39;
constexpr uint8_t SetPedHeadless = 40;
constexpr uint8_t SetPedFrozen = 41;
constexpr uint8_t SetVehicleRotation = 46;
constexpr uint8_t SetVehicleTurnSpeed = 47;
constexpr uint8_t SetVehicleColor = 48;
constexpr uint8_t SetVehicleLocked = 49;
constexpr uint8_t SetVehicleEngineState = 59;
constexpr uint8_t SetVehicleFrozen = 65;
constexpr uint8_t AddVehicleUpgrade = 54;
constexpr uint8_t AddAllVehicleUpgrades = 55;
constexpr uint8_t RemoveVehicleUpgrade = 56;
constexpr uint8_t GiveWeapon = 73;
constexpr uint8_t TakeWeapon = 74;
constexpr uint8_t TakeAllWeapons = 75;
constexpr uint8_t SetWeaponAmmo = 76;
constexpr uint8_t SetWeaponSlot = 77;
constexpr uint8_t ToggleControlAbility = 105;
constexpr uint8_t ToggleAllControlAbility = 106;
constexpr uint8_t ShowChat = 119;
constexpr uint8_t SetElementCollisionsEnabled = 148;
constexpr uint8_t SetElementFrozen = 152;
constexpr uint8_t SetPedAnimationSpeed = 200;
constexpr uint8_t SetTime = 1;
constexpr uint8_t SetWeather = 2;
constexpr uint8_t SetWeatherBlended = 3;
constexpr uint8_t SetMinuteDuration = 4;
constexpr uint8_t AttachElements = 11;
constexpr uint8_t DetachElements = 12;
constexpr uint8_t SetElementAttachedOffsets = 17;
constexpr uint8_t SetPlayerMoney = 18;
constexpr uint8_t ShowPlayerHudComponent = 19;
constexpr uint8_t FixVehicle = 44;
constexpr uint8_t BlowVehicle = 45;
constexpr uint8_t SetObjectRotation = 84;
constexpr uint8_t MoveObject = 85;
constexpr uint8_t StopObject = 86;
constexpr uint8_t PlaySound = 100;
constexpr uint8_t BindKey = 101;
constexpr uint8_t UnbindKey = 102;
constexpr uint8_t BindCommand = 103;
constexpr uint8_t UnbindCommand = 104;
constexpr uint8_t SetControlState = 107;
constexpr uint8_t SetCameraMatrix = 114;
constexpr uint8_t SetCameraTarget = 115;
constexpr uint8_t SetCameraInterior = 116;
constexpr uint8_t FadeCamera = 117;
constexpr uint8_t ShowCursor = 118;
constexpr uint8_t SetGravity = 120;
constexpr uint8_t SetGameSpeed = 121;
constexpr uint8_t SetWaveHeight = 122;
constexpr uint8_t SetBlurLevel = 127;
constexpr uint8_t SetCloudsEnabled = 132;
constexpr uint8_t SetVehicleHandling = 134;
constexpr uint8_t SetVehicleHandlingProperty = 135;
constexpr uint8_t ResetVehicleHandlingProperty = 136;
constexpr uint8_t ResetVehicleHandling = 137;
constexpr uint8_t SetObjectScale = 147;
constexpr uint8_t SetJetpackMaxHeight = 149;
constexpr uint8_t SetInteriorSoundsEnabled = 155;
constexpr uint8_t SetRainLevel = 156;
constexpr uint8_t SetSunSize = 157;
constexpr uint8_t SetSunColor = 158;
constexpr uint8_t SetWindVelocity = 159;
constexpr uint8_t SetFarClipDistance = 160;
constexpr uint8_t SetFogDistance = 161;
constexpr uint8_t ResetRainLevel = 162;
constexpr uint8_t ResetSunSize = 163;
constexpr uint8_t ResetSunColor = 164;
constexpr uint8_t ResetWindVelocity = 165;
constexpr uint8_t ResetFarClipDistance = 166;
constexpr uint8_t ResetFogDistance = 167;
constexpr uint8_t SetAircraftMaxHeight = 168;
constexpr uint8_t SetOcclusionsEnabled = 175;
constexpr uint8_t SetAircraftMaxVelocity = 191;
constexpr uint8_t ResetMoonSize = 192;
constexpr uint8_t SetMoonSize = 193;
constexpr uint8_t SetElementParent = 5;
constexpr uint8_t SetElementName = 14;
constexpr uint8_t SetPlayerNametagText = 21;
constexpr uint8_t RemovePlayerNametagColor = 22;
constexpr uint8_t SetPlayerNametagColor = 23;
constexpr uint8_t SetPlayerNametagShowing = 24;
constexpr uint8_t DestroyAllVehicles = 43;
constexpr uint8_t DestroyAllBlips = 78;
constexpr uint8_t SetBlipIcon = 79;
constexpr uint8_t SetBlipSize = 80;
constexpr uint8_t SetBlipColor = 81;
constexpr uint8_t SetBlipOrdering = 82;
constexpr uint8_t DestroyAllObjects = 83;
constexpr uint8_t DestroyAllRadarAreas = 87;
constexpr uint8_t SetRadarAreaSize = 88;
constexpr uint8_t SetRadarAreaColor = 89;
constexpr uint8_t SetRadarAreaFlashing = 90;
constexpr uint8_t DestroyAllMarkers = 91;
constexpr uint8_t SetMarkerType = 92;
constexpr uint8_t SetMarkerColor = 93;
constexpr uint8_t SetMarkerSize = 94;
constexpr uint8_t SetMarkerTarget = 95;
constexpr uint8_t SetMarkerIcon = 96;
constexpr uint8_t DestroyAllPickups = 97;
constexpr uint8_t SetPickupType = 98;
constexpr uint8_t SetPickupVisible = 99;
constexpr uint8_t SetTeamName = 109;
constexpr uint8_t SetTeamColor = 110;
constexpr uint8_t SetPlayerTeam = 111;
constexpr uint8_t SetTeamFriendlyFire = 112;
constexpr uint8_t SetWantedLevel = 113;
constexpr uint8_t SetSkyGradient = 123;
constexpr uint8_t ResetSkyGradient = 124;
constexpr uint8_t SetHeatHaze = 125;
constexpr uint8_t ResetHeatHaze = 126;
constexpr uint8_t SetFpsLimit = 128;
constexpr uint8_t SetGarageOpen = 129;
constexpr uint8_t SetGlitchEnabled = 131;
constexpr uint8_t SetWorldWaterLevel = 141;
constexpr uint8_t ResetWorldWaterLevel = 142;
constexpr uint8_t SetElementDoubleSided = 144;
constexpr uint8_t SetTrafficLightState = 145;
constexpr uint8_t SetWaterColor = 150;
constexpr uint8_t ResetWaterColor = 151;
constexpr uint8_t SetBlipVisibleDistance = 154;
constexpr uint8_t RemoveWorldModel = 171;
constexpr uint8_t RestoreWorldModel = 172;
constexpr uint8_t RestoreAllWorldModels = 173;
constexpr uint8_t NumRpcFuncs = 225;
}  // namespace Rpc

}  // namespace WorldRPG::MtaIds
