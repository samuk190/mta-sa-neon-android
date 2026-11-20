#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "entity/PedSpawn.h"
#include "SpatialWorld.h"
#include "entity/WeaponSpawn.h"

namespace WorldRPG::Net {
struct ResourceStartInfo;
class BitStreamReader;
}

namespace WorldRPG::ServerWorld {

// Reconnect: forgets every element, its custom data and the resource root of the old session. Radar blips of the
// old session are cleared on the next Tick (game thread). Call before the new session's first packet.
void ResetSession();
// Drops a resource-root placeholder (kind 0) stored by OnResourceStart. A real
// player, ped, vehicle, marker or blip is left alone.
void ForgetPlaceholder(uint32_t id);

void SetLocalPlayer(uint32_t id);
uint32_t LocalPlayer();
void NotePlayerSkin(uint32_t id, int model);

// -1 until PLAYER_LIST or ENTITY_ADD names a model.
int ModelOf(uint32_t id);

// isBoolean (optional) is set when the server sent a Lua boolean; number is then 1 or 0.
bool GetData(uint32_t id, const char* key, double& number, std::string& text, bool& isNumber,
             bool* isBoolean = nullptr);

// One custom-data value as GetData reports it (number, or text when number is false).
struct ElementDataValue {
    bool number = false;
    bool boolean = false;
    double value = 0;
    std::string text;
};

// Own custom data for one element. Empty when the element has none.
void CopyElementData(uint32_t id, std::vector<std::pair<std::string, ElementDataValue>>& out);

// Nick from the join or from PLAYER_LIST. False when this id has no nick yet.
bool CopyPlayerNick(uint32_t id, std::string& out);
void NotePlayerNick(uint32_t id, const std::string& nick);
// Case-insensitive nick match among players (CClientPlayerManager::Get, default insensitive).
bool FindPlayerByNick(const std::string& nick, uint32_t& id);

void SetCurrentWaterFlags(bool enabled);
void OnEntityAdd(const uint8_t* body, size_t length, size_t bits);
void OnEntityRemove(const uint8_t* body, size_t length, size_t bits);
void OnCustomData(const uint8_t* body, size_t length, size_t bits);
// CElementRPCs::SetElementData (CElementRPCs.cpp:84-102): compressed u16 name length, the name, one CLuaArgument.
// Reads from the RPC body after the element id. False on a malformed body.
bool ReadSetElementDataRpc(uint32_t id, Net::BitStreamReader& reader);
// CElementRPCs::RemoveElementData (104-120): compressed u16 name length, the name, one bit (recursive, unused).
bool ReadRemoveElementDataRpc(uint32_t id, Net::BitStreamReader& reader);
void OnPlayerList(const uint8_t* body, size_t length, size_t bits);
void OnResourceStart(const WorldRPG::Net::ResourceStartInfo& info);
uint32_t LastResourceRoot();

struct ActorSpawn {
    uint32_t id = 0;
    int kind = 0;
    int model = -1;
    float x = 0, y = 0, z = 0, heading = 0;
    // Vehicles only (see Entity::Visual): roll and pitch in degrees, and health (< 0 when unknown).
    float rotX = 0, rotY = 0;
    float health = -1.f;
    bool hosted = false;
    // ENTITY_ADD interior (CPacketHandler common header). Outdoor traffic is 0.
    uint8_t interior = 0;
};
// Copies peds (kind 14) and vehicles (kind 2).
// Both limits negative: the two kinds share `capacity` (a run of vehicles can fill it).
// Otherwise each kind stops at its own limit and peds are copied first, so a full vehicle
// quota cannot take a ped slot. `capacity` is the size of `out`.
int CopyActors(ActorSpawn* out, int capacity, int pedLimit = -1, int vehicleLimit = -1);
void SetActorHost(uint32_t id, bool host);
bool ActorHosted(uint32_t id);
bool ElementPose(uint32_t id, float& x, float& y, float& z);
bool CopyVisual(uint32_t id, ActorSpawn& out);
void CopySpatialElements(std::vector<Spatial::Element>& out);
uint64_t ElementGeneration(uint32_t id);
bool CopyColshape(uint32_t id,Spatial::Element& out);
bool UpdateColshape(uint32_t id, const std::function<bool(Spatial::Geometry&)>& update);
const char* ElementKindName(uint32_t id);
int CountKind(int kind);
// ENTITY_ADD marker body fields that Packet_EntityAdd stores on the client marker.
struct MarkerFields {
    int type = 2;
    float x = 0, y = 0, z = 0, size = 1;
    int r = 255, g = 255, b = 255, a = 255;
    bool ignoreAlpha = false;
    bool hasTarget = false;
    float targetX = 0, targetY = 0, targetZ = 0;
    int arrowR = 255, arrowG = 64, arrowB = 64, arrowA = 255;
    float arrowSize = 0.625f;
};
bool CopyMarkerFields(uint32_t id, MarkerFields& out);
struct RadarFields {
    float x = 0, y = 0, w = 0, h = 0;
    int r = 255, g = 0, b = 0, a = 255;
    bool flashing = false;
};
bool CopyRadarFields(uint32_t id, RadarFields& out);
struct TeamFields {
    uint32_t id = 0;
    std::string name;
    int r = 0, g = 0, b = 0;
    bool friendlyFire = false;
    std::vector<uint32_t> players;
};
void CopyTeams(std::vector<TeamFields>& out);
bool CopyTeam(uint32_t id, TeamFields& out);
// Sync time context from ENTITY_ADD (0 when this id has none). Desktop CClientEntity::GetSyncTimeContext.
uint8_t SyncTimeContext(uint32_t id);
// The ENTITY_ADD ped body beyond pose and model (PedSpawn.h). False when the id is not a ped from ENTITY_ADD.
bool CopyPedSpawn(uint32_t id, Entity::PedSpawn& out);
// Last ENTITY_ADD weapon snapshot. False for removed elements and other types.
bool CopyWeaponSpawn(uint32_t id, Entity::WeaponSpawn& out);
// Attachment metadata received in ENTITY_ADD. This does not move a GTA body.
struct Attachment {
    uint32_t parent = 0xFFFFFFFFu;
    float position[3] = {}, rotation[3] = {}; // Lua-facing rotation in degrees.
};
bool CopyAttachment(uint32_t id, Attachment& out);
// setElementAttachedOffsets. Rotation stays in degrees, the same units CopyAttachment returns.
// A missing record is created unattached. An existing parent is left in place.
void NoteAttachedOffsets(uint32_t id, float x, float y, float z, float rx, float ry, float rz);
std::vector<uint32_t> CopyAttachedElements(uint32_t parent);
// PLAYER_QUIT (5): CPacketHandler::Packet_PlayerQuit -> CClientGame::QuitPlayer deletes the player element.
void OnPlayerQuit(uint32_t id);
// A remote player this session knows (PLAYER_LIST nick, PLAYER_SPAWN skin, or a body record of kind 1).
bool IsKnownPlayer(uint32_t id);
// A PLAYER_PURESYNC came from this id: it is a player (kind 1) even without a PLAYER_LIST or PLAYER_SPAWN record.
void NotePlayerSeen(uint32_t id);
// CClientEntity::SetSyncTimeContext from an RPC (SET_ELEMENT_POSITION, SET_ELEMENT_HEALTH, SET_PED_ARMOR, ...).
void SetSyncTimeContext(uint32_t id, uint8_t context);
// Element record updates from RPCs, so the per-frame actor copy does not undo them.
void NoteElementPosition(uint32_t id, float x, float y, float z);
void NoteElementInterior(uint32_t id, uint8_t interior);
void NoteElementModel(uint32_t id, int model);
// A vehicle element for script state. Does not replace a player, ped, or other live kind.
void NoteVehicle(uint32_t id, int model);
// CClientEntity::SetDimension and CClientGame::SetAllDimensions. A ped whose dimension is not the local one is streamed
// out (no body). DimensionOf returns the local dimension for an element this session has no record of.
void NoteElementDimension(uint32_t id, uint16_t dimension);
uint16_t DimensionOf(uint32_t id);
uint8_t InteriorOf(uint32_t id);
void SetLocalDimension(uint16_t dimension);
uint16_t LocalDimension();

// Radar blips and the F11 snapshot. Call on the game thread.
void Tick(float playerX, float playerY, float playerZ);
std::string MapSnapshot(float playerX, float playerY, float playerHeading);

} // namespace WorldRPG::ServerWorld
