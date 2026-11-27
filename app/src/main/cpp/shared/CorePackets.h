#pragma once

// Core inbound gameplay packet handlers (P0-3).
// Each handler follows its desktop MTA Neon reference in Client/mods/deathmatch/logic
// and Shared/mods/deathmatch/logic, pinned to bitstream 0x0030.

#include <cstddef>
#include <cstdint>
#include <functional>

namespace WorldRPG::CorePackets {

using InnerPacketDispatch = std::function<bool(uint8_t packetId, const uint8_t* body, size_t length, size_t bits)>;

// PACKET_ID_LIGHTSYNC (34): CNetAPI::ProcessPacket / ReadPlayerLightsync (CNetAPI.cpp:43-54, 2107-2246).
bool HandleLightSync(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_VEHICLE_RESYNC (35): CNetAPI::ProcessPacket / ReadVehicleResync (CNetAPI.cpp:56-72, 2248-2316).
bool HandleVehicleResync(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_EXPLOSION (37): CPacketHandler::Packet_ExplosionSync (CPacketHandler.cpp:4769-4958).
bool HandleExplosion(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_FIRE (38): CPacketHandler::Packet_FireSync (CPacketHandler.cpp:4960-4981).
bool HandleFire(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_PROJECTILE (39): CPacketHandler::Packet_ProjectileSync (CPacketHandler.cpp:4983-5129).
bool HandleProjectile(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_PICKUP_HIDESHOW (52): CPacketHandler::Packet_PickupHideShow (CPacketHandler.cpp:4569-4615).
bool HandlePickupHideShow(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_PICKUP_HIT_CONFIRM (53): CPacketHandler::Packet_PickupHitConfirm (CPacketHandler.cpp:4617-4655).
bool HandlePickupHitConfirm(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_VEHICLE_SPAWN (57): CPacketHandler::Packet_VehicleSpawn (CPacketHandler.cpp:1677-1758).
bool HandleVehicleSpawn(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_VEHICLE_DAMAGE_SYNC (59): CPacketHandler::Packet_VehicleDamageSync (CPacketHandler.cpp:1760-1803).
bool HandleVehicleDamageSync(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_VEHICLE_TRAILER (60): CPacketHandler::Packet_VehicleTrailer (CPacketHandler.cpp:2313-2375).
bool HandleVehicleTrailer(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_OBJECT_STARTSYNC (86): CObjectSync::Packet_ObjectStartSync (CObjectSync.cpp:99-137).
bool HandleObjectStartSync(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_OBJECT_STOPSYNC (87): CObjectSync::Packet_ObjectStopSync (CObjectSync.cpp:139-148).
bool HandleObjectStopSync(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_OBJECT_SYNC (88): CObjectSync::Packet_ObjectSync (CObjectSync.cpp:150-201).
bool HandleObjectSync(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_LATENT_TRANSFER (96): CLatentTransferManager::OnReceive / CLatentReceiver::OnReceive
// (CLatentTransferManager.cpp:297-308, CLatentReceiver.cpp:66-195).
bool HandleLatentTransfer(const uint8_t* body, size_t length, size_t bits, const InnerPacketDispatch& dispatch);

// PACKET_ID_PED_TASK (101): CPacketHandler::Packet_PedTask (CPacketHandler.cpp:5893-5935).
bool HandlePedTask(const uint8_t* body, size_t length, size_t bits);

// Clears per-session latent transfer reassembly state (called from ServerWorld::ResetSession).
void ResetSession();

}  // namespace WorldRPG::CorePackets
