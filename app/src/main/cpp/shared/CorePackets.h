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

// PACKET_ID_PACKET_PROGRESS (8): CPacketHandler::Packet_PartialPacketInfo (CPacketHandler.cpp:2819-2839).
bool HandlePartialPacketInfo(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_SERVER_DISCONNECTED (23): CPacketHandler::Packet_ServerDisconnected (CPacketHandler.cpp:545-685).
bool HandleServerDisconnected(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_RETURN_SYNC (36): CNetAPI::ProcessPacket case PACKET_ID_RETURN_SYNC (CNetAPI.cpp:161-221).
bool HandleReturnSync(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_MAP_INFO (46): CPacketHandler::Packet_MapInfo (CPacketHandler.cpp:2377-2715, bitstream 0x0030).
bool HandleMapInfo(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_UPDATE_INFO (89): CPacketHandler::Packet_UpdateInfo (CPacketHandler.cpp:5824-5832).
bool HandleUpdateInfo(const uint8_t* body, size_t length, size_t bits);

// PACKET_ID_DEBUG_EVENT (110): CPacketHandler::Packet_DebugEvent (CPacketHandler.cpp:1643-1675, SDebugEvent.h:1-155).
bool HandleDebugEvent(const uint8_t* body, size_t length, size_t bits);

struct PartialPacketProgress {
    uint32_t partCount = 0;
    uint32_t partTotal = 0;
    uint32_t partLength = 0;
};
struct DisconnectRecord {
    bool received = false;
    uint8_t type = 0;
    uint32_t durationSeconds = 0;
    std::string reason;
};
struct ReturnSyncRecord {
    bool valid = false;
    bool inVehicle = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
};
struct UpdateInfoRecord {
    bool received = false;
    std::string type;
    std::string data;
};
struct DebugEventRecord {
    bool received = false;
    uint32_t eventId = 0;
    uint32_t elementId = 0;
    uint32_t timeMs = 0;
    std::string message;
};

PartialPacketProgress LastPartialPacketProgress();
DisconnectRecord LastDisconnectRecord();
ReturnSyncRecord LastReturnSyncRecord();
UpdateInfoRecord LastUpdateInfoRecord();
DebugEventRecord LastDebugEventRecord();

// Outbound packet sink override for host testing (when unset, packets send via Net::SendGamePacket*).
using OutboundPacketSink = std::function<bool(uint8_t gamePacketId, const uint8_t* bytes, size_t len, size_t bitLen,
                                              bool reliable, bool highPriority)>;
void SetOutboundPacketSink(OutboundPacketSink sink);

// Outbound gameplay packet encoders matching desktop client wire format (bitstream 0x0030):
// PACKET_ID_EXPLOSION (37): CClientGame::SendExplosionSync (CClientGame.cpp:6288-6317).
bool SendExplosionSync(float x, float y, float z, uint8_t explosionType, uint32_t originId = 0x1FFFFu);
// PACKET_ID_PROJECTILE (39): CClientGame::ProjectileCreationHandler (CClientGame.cpp:6335-6418).
bool SendProjectileSync(uint8_t weaponType, uint16_t model, float ox, float oy, float oz,
                        uint32_t originSourceId = 0x1FFFFu, uint32_t targetId = 0x1FFFFu,
                        float force = 0.0f, float vx = 0.0f, float vy = 0.0f, float vz = 0.0f,
                        float rx = 0.0f, float ry = 0.0f, float rz = 0.0f);
// PACKET_ID_DETONATE_SATCHELS (40): CClientGame::SendDetonateSatchels (CClientGame.cpp:2040-2046).
bool SendDetonateSatchels();
// PACKET_ID_DESTROY_SATCHELS (41): CClientGame::SendDestroySatchels (CClientGame.cpp:6512-6517).
bool SendDestroySatchels();
// PACKET_ID_VEHICLE_DAMAGE_SYNC (59): CDeathmatchVehicle::SyncDamageModel (CDeathmatchVehicle.cpp:118-129).
bool SendVehicleDamageSync(uint32_t vehicleId, const uint8_t doorStates[6], const uint8_t wheelStates[4],
                           const uint8_t panelStates[7], const uint8_t lightStates[4]);
// PACKET_ID_CUSTOM_DATA (84): CStaticFunctionDefinitions::SetElementData (CStaticFunctionDefinitions.cpp:1035-1047).
bool SendCustomData(uint32_t elementId, const std::string& key, const uint8_t* encodedValue, size_t encodedLen,
                    size_t encodedBits);
// PACKET_ID_CAMERA_SYNC (85): CNetAPI::WriteCameraSync (CNetAPI.cpp:2036-2073).
bool SendCameraSync(bool fixedMode, float x, float y, float z, float lookX, float lookY, float lookZ,
                    uint32_t targetId = 0x1FFFFu);
// PACKET_ID_OBJECT_SYNC (88): CObjectSync::WriteObjectInformation (CObjectSync.cpp:203-270).
bool SendObjectSync(uint32_t objectId, uint8_t timeContext, bool hasPos, float x, float y, float z,
                    bool hasRot, float rx, float ry, float rz, bool hasHealth, float health);
// PACKET_ID_PLAYER_TRANSGRESSION (91): CClientGame::DoPulse (CClientGame.cpp:1248-1256).
bool SendPlayerTransgression(uint32_t level, const std::string& message);
// PACKET_ID_PLAYER_DIAGNOSTIC (92): CClientGame::DoPulse (CClientGame.cpp:1278-1283).
bool SendPlayerDiagnostic(const std::string& message);
// PACKET_ID_PLAYER_SCREENSHOT (94): CClientGame::TakePlayerScreenShot (CClientGame.cpp:7260-7272).
bool SendPlayerScreenshotStatus(uint8_t status, uint32_t serverSentTime, uint16_t resourceNetId,
                                const std::string& tag);
// PACKET_ID_VEHICLE_PUSH_SYNC (97): CClientGame::SendVehiclePushSync (CClientGame.cpp:4955-4962).
bool SendVehiclePushSync(uint32_t vehicleId);
// PACKET_ID_WEAPON_BULLETSYNC (100): CNetAPI::SendBulletSyncCustomWeaponFire (CNetAPI.cpp:2416-2430).
bool SendWeaponBulletSync(uint32_t weaponId, float sx, float sy, float sz, float ex, float ey, float ez,
                          uint8_t orderCounter);
// PACKET_ID_PED_TASK (101): CClientGame::TaskSimpleBeHitHandler (CClientGame.cpp:5350-5362).
bool SendPedTask(uint16_t taskType, uint32_t attackerId, uint8_t hitBodyPart, uint8_t hitBodySide,
                 uint8_t weaponId);
// PACKET_ID_PLAYER_WORLD_SPECIAL_PROPERTY (109): CLuaWorldDefs::SetWorldSpecialPropertyEnabled (CLuaWorldDefs.cpp:2002-2015).
bool SendWorldSpecialProperty(const std::string& propertyName, bool enabled);

// Appends active server text display items (PACKET_ID_TEXT_ITEM) as DX draw lines for the current frame.
void AppendServerTextItems(std::string& frame, int screenW, int screenH);
size_t ServerTextItemCount();

// Clears per-session latent transfer reassembly state and server text items (called from ServerWorld::ResetSession).
void ResetSession();

}  // namespace WorldRPG::CorePackets
