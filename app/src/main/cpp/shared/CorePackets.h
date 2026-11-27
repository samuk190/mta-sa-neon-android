#pragma once

// Core inbound gameplay packet handlers (P0-3).
// Each handler follows its desktop MTA Neon reference in Client/mods/deathmatch/logic
// and Shared/mods/deathmatch/logic, pinned to bitstream 0x0030.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

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

// PACKET_ID_PLAYER_CHANGE_NICK (28): CPacketHandler::Packet_PlayerChangeNick (CPacketHandler.cpp:1100-1142).
bool HandlePlayerChangeNick(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_PLAYER_STATS (29): CPacketHandler::Packet_PlayerStats (CPacketHandler.cpp:2772-2807).
bool HandlePlayerStats(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_DETONATE_SATCHELS (40): CPacketHandler::Packet_DetonateSatchels (CPacketHandler.cpp:5131-5158).
bool HandleDetonateSatchels(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_DESTROY_SATCHELS (41): CPacketHandler::Packet_DestroySatchels (CPacketHandler.cpp:5160-5176).
bool HandleDestroySatchels(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_CONSOLE_ECHO (44): CPacketHandler::Packet_ConsoleEcho (CPacketHandler.cpp:1221-1244).
bool HandleConsoleEcho(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_DEBUG_ECHO (45): CPacketHandler::Packet_DebugEcho (CPacketHandler.cpp:1246-1275).
bool HandleDebugEcho(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_VOICE_END (72): CPacketHandler::Packet_VoiceEnd (CPacketHandler.cpp:5824-5843).
bool HandleVoiceEnd(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_TEXT_ITEM (78): CPacketHandler::Packet_TextItem (CPacketHandler.cpp:5266-5325).
bool HandleTextItem(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_CHAT_CLEAR (105): CPacketHandler::Packet_ChatClear (CPacketHandler.cpp:1215-1219).
bool HandleChatClear(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_SERVER_INFO_SYNC (106): CPacketHandler::Packet_ServerInfoSync (CPacketHandler.cpp:5937-5956).
bool HandleServerInfoSync(const uint8_t* body, size_t length, size_t bits);

// Appends active server text display items (PACKET_ID_TEXT_ITEM) as DX draw lines for the current frame.
void AppendServerTextItems(std::string& frame, int screenW, int screenH);
size_t ServerTextItemCount();

// Clears per-session latent transfer reassembly state and server text items (called from ServerWorld::ResetSession).
void ResetSession();

}  // namespace WorldRPG::CorePackets
