#include "CorePackets.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "LuaWorldObjectApi.h"
#include "ServerRpc.h"
#include "ServerWorld.h"
#include "VehicleColors.h"
#include "entity/EntityAddTypes.h"
#include "entity/EntitySyncReaders.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/ProjectileEngine.h"
#include "../gtasa/RemoteVehicle.h"
#include "../net/raknet/RakNetWrapper.h"

#include "lua.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

namespace WorldRPG::CorePackets {
namespace {

using Net::BitStreamReader;
using Entity::ReadCompressed;
using Entity::ReadElementId;
using Entity::ReadFlag;
using Entity::ReadFloatAsBits;
using Entity::ReadObjectHealth;
using Entity::ReadPosition;
using Entity::ReadRotationDegrees;
using Entity::ReadRotationRadians;
using Entity::ReadVehicleHealth;

constexpr uint32_t kInvalidElementId = 0xFFFFFFFFu;
constexpr float kLegacyLowPrecisionBound = 8192.0f;  // SyncStructures.h:36 (bitstream 0x0030)

// Client/sdk/game/CExplosion.h:23-26.
constexpr uint8_t kExpTypeCar = 4;
constexpr uint8_t kExpTypeCarQuick = 5;
constexpr uint8_t kExpTypeBoat = 6;
constexpr uint8_t kExpTypeHeli = 7;

// Client/sdk/game/CWeaponInfo.h:91-139.
constexpr uint8_t kWeaponGrenade = 16;
constexpr uint8_t kWeaponTeargas = 17;
constexpr uint8_t kWeaponMolotov = 18;
constexpr uint8_t kWeaponRocket = 19;
constexpr uint8_t kWeaponRocketHs = 20;
constexpr uint8_t kWeaponFreefallBomb = 21;
constexpr uint8_t kWeaponSatchel = 39;
constexpr uint8_t kWeaponFlare = 58;

// Client/sdk/game/TaskTypes.h:398.
constexpr uint16_t kTaskSimpleBeHit = 1008;

// Shared/mods/deathmatch/logic/CLatentTransferManager.h:22-33.
constexpr uint8_t kLatentFlagHead = 0;
constexpr uint8_t kLatentFlagTail = 1;
constexpr uint8_t kLatentFlagCancel = 2;
constexpr uint16_t kLatentCategoryPacket = 1;
constexpr uint32_t kLatentMaxSize = 100u * 1024u * 1024u;  // CLatentReceiver.cpp:128

bool Finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

bool ReadFinitePosition(BitStreamReader& reader, float out[3]) {
    return ReadPosition(reader, out[0], out[1], out[2]) && Finite3(out);
}

bool ReadFiniteFloat3(BitStreamReader& reader, float out[3]) {
    return reader.Read(out[0]) && reader.Read(out[1]) && reader.Read(out[2]) && Finite3(out);
}

// SVelocitySync (SyncStructures.h:557-594): 1 bit moving, then finite float module + ReadNormVector.
bool ReadVelocity(BitStreamReader& reader, float out[3]) {
    bool moving = false;
    if (!ReadFlag(reader, moving)) return false;
    if (!moving) {
        out[0] = out[1] = out[2] = 0.f;
        return true;
    }
    float module = 0.f;
    float dx = 0.f, dy = 0.f, dz = 0.f;
    if (!reader.Read(module) || !std::isfinite(module)) return false;
    if (!reader.ReadNormVector(dx, dy, dz)) return false;
    out[0] = dx * module;
    out[1] = dy * module;
    out[2] = dz * module;
    return Finite3(out);
}

// SFloatSync<7, 17> (SyncStructures.h:45-89): 24 signed bits, value / (1 << 17).
bool ReadFloat7_17(BitStreamReader& reader, float& out) {
    uint8_t bytes[4] = {};
    if (!reader.ReadBits(bytes, 24)) return false;
    int32_t raw = static_cast<int32_t>(bytes[0]) | (static_cast<int32_t>(bytes[1]) << 8) |
                  (static_cast<int32_t>(bytes[2]) << 16);
    if (raw & 0x800000) raw |= ~0xFFFFFF;
    out = static_cast<float>(static_cast<double>(raw) / static_cast<double>(1 << 17));
    return std::isfinite(out);
}

// SLowPrecisionPositionSync at bitstream 0x0030 (SyncStructures.h:383-402): u16 x, u16 y, 11-bit z.
bool ReadLowPrecisionPosition(BitStreamReader& reader, float out[3]) {
    uint16_t usX = 0, usY = 0;
    uint8_t zBytes[2] = {};
    if (!reader.Read(usX) || !reader.Read(usY) || !reader.ReadBits(zBytes, 11)) return false;
    const uint16_t usZ = static_cast<uint16_t>(zBytes[0]) | (static_cast<uint16_t>(zBytes[1] & 0x07u) << 8);
    out[0] = (kLegacyLowPrecisionBound * 2.0f) * (static_cast<float>(usX) / 65535.0f) - kLegacyLowPrecisionBound;
    out[1] = (kLegacyLowPrecisionBound * 2.0f) * (static_cast<float>(usY) / 65535.0f) - kLegacyLowPrecisionBound;
    out[2] = static_cast<float>(usZ) - 110.0f;
    return Finite3(out);
}

// SVehiclePartStateSyncMethodeB<COUNT, BITS> (SyncStructures.h:1857-1886).
template <unsigned Bits, size_t Count>
bool ReadPartStateMethodeB(BitStreamReader& reader, uint8_t (&out)[Count]) {
    bool nonZero = false;
    if (!ReadFlag(reader, nonZero)) return false;
    if (!nonZero) {
        std::memset(out, 0, Count);
        return true;
    }
    for (size_t i = 0; i < Count; ++i) {
        uint8_t raw = 0;
        if (!reader.ReadBits(&raw, Bits)) return false;
        out[i] = static_cast<uint8_t>(raw & ((1u << Bits) - 1u));
    }
    return true;
}

// SVehiclePartStateSync<COUNT, BITS>(delta=true) (SyncStructures.h:1686-1706).
template <size_t Count, unsigned Bits>
bool ReadPartStateDelta(BitStreamReader& reader, bool (&changed)[Count], uint8_t (&states)[Count]) {
    for (size_t i = 0; i < Count; ++i) {
        changed[i] = false;
        states[i] = 0;
        bool has = false;
        if (!ReadFlag(reader, has)) return false;
        changed[i] = has;
        if (has) {
            uint8_t raw = 0;
            if (!reader.ReadBits(&raw, Bits)) return false;
            states[i] = static_cast<uint8_t>(raw & ((1u << Bits) - 1u));
        }
    }
    return true;
}

bool IsVehicle(uint32_t id) {
    return id != 0 && id != kInvalidElementId && std::strcmp(ServerWorld::ElementKindName(id), "vehicle") == 0;
}

bool IsObject(uint32_t id) {
    return id != 0 && id != kInvalidElementId && std::strcmp(ServerWorld::ElementKindName(id), "object") == 0;
}

bool IsPedOrPlayer(uint32_t id) {
    if (id == 0 || id == kInvalidElementId) return false;
    const char* kind = ServerWorld::ElementKindName(id);
    return std::strcmp(kind, "player") == 0 || std::strcmp(kind, "ped") == 0;
}

struct LatentReceiveState {
    bool started = false;
    uint16_t id = 0;
    uint16_t category = 0;
    uint16_t resourceNetId = 0xFFFFu;
    uint32_t finalSize = 0;
    uint32_t rate = 0;
    std::vector<uint8_t> buffer;
};

std::mutex g_latentMutex;
LatentReceiveState g_latentRx;
thread_local int g_latentDepth = 0;

}  // namespace

// CNetAPI::ProcessPacket / ReadPlayerLightsync (CNetAPI.cpp:43-54, 2107-2246).
bool HandleLightSync(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    if (reader.GetRemainingBits() < 17) return false;
    while (reader.GetRemainingBits() >= 17) {
        uint32_t playerId = 0;
        uint8_t syncContext = 0;
        uint32_t latency = 0;
        bool readHealth = false;
        float health = 0.f, armor = 0.f;
        bool readPosition = false;
        float pos[3] = {};
        bool readVehicleHealth = false;
        float vehicleHealth = 0.f;

        if (!ReadElementId(reader, playerId)) return false;
        if (!reader.Read(syncContext)) return false;
        if (!ReadCompressed(reader, 2, latency)) return false;
        if (!ReadFlag(reader, readHealth)) return false;
        if (readHealth) {
            if (!ReadFloatAsBits(reader, 8, 0.f, 255.f, health)) return false;
            if (!ReadFloatAsBits(reader, 8, 0.f, 127.5f, armor)) return false;
        }
        if (!ReadFlag(reader, readPosition)) return false;
        if (readPosition) {
            if (!ReadLowPrecisionPosition(reader, pos)) return false;
            if (!ReadFlag(reader, readVehicleHealth)) return false;
            if (readVehicleHealth) {
                if (!ReadFloatAsBits(reader, 8, 0.f, 2040.f, vehicleHealth)) return false;
            }
        }
        (void)latency;

        if (!ServerWorld::IsKnownPlayer(playerId) || playerId == ServerWorld::LocalPlayer()) continue;
        if (!ServerWorld::CanUpdateSync(playerId, syncContext)) continue;

        if (readHealth) {
            Game::RpcSetHealth(playerId, health);
            Game::LockPedHealth(playerId, health);
            Game::RpcSetArmor(playerId, armor);
            Game::LockPedArmor(playerId, armor);
        }
        if (readPosition) {
            ServerWorld::NoteElementPosition(playerId, pos[0], pos[1], pos[2]);
            uint32_t vehicleId = 0;
            uint8_t seat = 0;
            if (RemoteVehicle::FindOccupant(playerId, vehicleId, seat)) {
                if (seat == 0) {
                    ServerWorld::NoteVehiclePose(vehicleId, true, pos, false, nullptr, readVehicleHealth, vehicleHealth);
                    Game::RpcSetVehiclePosition(vehicleId, pos[0], pos[1], pos[2]);
                    if (readVehicleHealth) Game::RpcSetVehicleHealth(vehicleId, vehicleHealth);
                }
            } else {
                Game::RpcSetPosition(playerId, pos[0], pos[1], pos[2]);
            }
        }
        static bool logged = false;
        if (!logged) {
            logged = true;
            LUA_LOGI("DIAG LIGHTSYNC: ping/last-puresync timestamp and remote interpolation reset are not hooked");
        }
    }
    return true;
}

// CNetAPI::ProcessPacket / ReadVehicleResync (CNetAPI.cpp:56-72, 2248-2316).
bool HandleVehicleResync(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t vehicleId = 0;
    float pos[3] = {}, rotDeg[3] = {}, move[3] = {}, turn[3] = {};
    float health = 0.f;
    uint8_t damageFlags = 0;
    uint8_t doors[6] = {}, wheels[4] = {}, panels[7] = {}, lights[4] = {};

    if (!ReadElementId(reader, vehicleId)) return false;
    if (!ReadFinitePosition(reader, pos)) return false;
    if (!ReadRotationDegrees(reader, rotDeg[0], rotDeg[1], rotDeg[2])) return false;
    if (!ReadVelocity(reader, move) || !ReadVelocity(reader, turn)) return false;
    if (!ReadVehicleHealth(reader, health)) return false;
    if (!reader.ReadBits(&damageFlags, 4)) return false;

    const bool syncDoors = (damageFlags & 0x01u) != 0;
    const bool syncWheels = (damageFlags & 0x02u) != 0;
    const bool syncPanels = (damageFlags & 0x04u) != 0;
    const bool syncLights = (damageFlags & 0x08u) != 0;

    if (syncDoors && !ReadPartStateMethodeB<3>(reader, doors)) return false;
    if (syncWheels && !ReadPartStateMethodeB<2>(reader, wheels)) return false;
    if (syncPanels && !ReadPartStateMethodeB<2>(reader, panels)) return false;
    if (syncLights && !ReadPartStateMethodeB<2>(reader, lights)) return false;

    if (!IsVehicle(vehicleId)) return true;

    ServerWorld::NoteVehiclePose(vehicleId, true, pos, true, rotDeg, true, health);
    Game::RpcSetVehiclePosition(vehicleId, pos[0], pos[1], pos[2]);
    Game::RpcSetVehicleRotation(vehicleId, rotDeg[0], rotDeg[1], rotDeg[2]);
    Game::RpcSetVehicleVelocity(vehicleId, move[0], move[1], move[2], false);
    Game::RpcSetVehicleVelocity(vehicleId, turn[0], turn[1], turn[2], true);
    Game::RpcSetVehicleHealth(vehicleId, health);

    bool doorChanged[6], wheelChanged[4], panelChanged[7], lightChanged[4];
    for (bool& b : doorChanged) b = syncDoors;
    for (bool& b : wheelChanged) b = syncWheels;
    for (bool& b : panelChanged) b = syncPanels;
    for (bool& b : lightChanged) b = syncLights;
    Script::VehicleApplyDamageSync(vehicleId, doorChanged, doors, wheelChanged, wheels, panelChanged, panels,
                                   lightChanged, lights);

    static bool logged = false;
    if (!logged) {
        logged = true;
        LUA_LOGI("DIAG VEHICLE_RESYNC: native CDamageManager visual damage write is not hooked");
    }
    return true;
}

// CPacketHandler::Packet_ExplosionSync (CPacketHandler.cpp:4769-4958).
bool HandleExplosion(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    bool hasCreator = false;
    uint32_t creatorId = kInvalidElementId;
    uint32_t latency = 0;
    if (!ReadFlag(reader, hasCreator)) return false;
    if (hasCreator) {
        if (!ReadElementId(reader, creatorId) || !ReadCompressed(reader, 2, latency)) return false;
    }

    bool hasOrigin = false;
    uint32_t originId = kInvalidElementId;
    bool isVehicleResponsible = false;
    bool blowWithoutExplosion = false;
    if (!ReadFlag(reader, hasOrigin)) return false;
    if (hasOrigin) {
        if (!ReadElementId(reader, originId) || !ReadFlag(reader, isVehicleResponsible)) return false;
        if (isVehicleResponsible && !ReadFlag(reader, blowWithoutExplosion)) return false;
    }

    float pos[3] = {};
    if (!ReadFinitePosition(reader, pos)) return false;
    uint8_t expType = 0;
    if (!reader.ReadBits(&expType, 4)) return false;
    expType &= 0x0Fu;
    (void)latency;

    ServerRpc::Enqueue([creatorId, originId, blowWithoutExplosion, pos, expType]() mutable {
        uint32_t resolvedOrigin = originId;
        if (resolvedOrigin != kInvalidElementId) {
            if (std::strcmp(ServerWorld::ElementKindName(resolvedOrigin), "player") == 0) {
                uint32_t vehId = 0;
                uint8_t seat = 0;
                if (RemoteVehicle::FindOccupant(resolvedOrigin, vehId, seat)) resolvedOrigin = vehId;
            }
            float ox = 0.f, oy = 0.f, oz = 0.f;
            if (ServerWorld::ElementPose(resolvedOrigin, ox, oy, oz)) {
                pos[0] += ox;
                pos[1] += oy;
                pos[2] += oz;
            }
        }

        const bool creatorKnown = creatorId != kInvalidElementId && ServerWorld::IsKnownPlayer(creatorId);
        bool cancel = false;
        if (creatorKnown && creatorId != ServerWorld::LocalPlayer()) {
            cancel = ServerWorld::DimensionOf(creatorId) != ServerWorld::LocalDimension();
        }

        lua_State* L = Script::ActiveVmState();
        if (!cancel && L) {
            lua_pushnumber(L, pos[0]);
            lua_pushnumber(L, pos[1]);
            lua_pushnumber(L, pos[2]);
            lua_pushnumber(L, expType);
            const uint32_t source = creatorKnown ? creatorId : Script::ClientRootElement();
            if (source != 0 && !Script::ActiveVmFireElementEvent(L, "onClientExplosion", source, 4)) cancel = true;
            if (source == 0) lua_pop(L, 4);
        }

        if (IsVehicle(resolvedOrigin) &&
            (expType == kExpTypeCar || expType == kExpTypeCarQuick || expType == kExpTypeBoat ||
             expType == kExpTypeHeli)) {
            if (blowWithoutExplosion) cancel = true;
            Script::VehicleApplyBlownState(resolvedOrigin, true);
            Game::RpcSetVehicleHealth(resolvedOrigin, 0.f);
            if (L) {
                lua_pushboolean(L, !cancel ? 1 : 0);
                Script::ActiveVmFireElementEvent(L, "onClientVehicleExplode", resolvedOrigin, 1);
            }
        }

        if (!cancel) {
            static int diagCount = 0;
            if (diagCount < 4) {
                ++diagCount;
                LUA_LOGI("DIAG EXPLOSION type=%u pos=(%.1f,%.1f,%.1f): CExplosionManager::Create is not hooked",
                         static_cast<unsigned>(expType), pos[0], pos[1], pos[2]);
            }
        }
    });
    return true;
}

// CPacketHandler::Packet_FireSync (CPacketHandler.cpp:4960-4981).
bool HandleFire(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t creatorId = kInvalidElementId;
    uint32_t latency = 0;
    float pos[3] = {};
    float size = 0.f;
    if (!ReadElementId(reader, creatorId) || !ReadCompressed(reader, 2, latency)) return false;
    if (!ReadFiniteFloat3(reader, pos) || !reader.Read(size) || !std::isfinite(size)) return false;
    (void)creatorId;
    (void)latency;

    static int diagCount = 0;
    if (diagCount < 4) {
        ++diagCount;
        LUA_LOGI("DIAG FIRE pos=(%.1f,%.1f,%.1f) size=%.2f: CFireManager::StartFire is not hooked",
                 pos[0], pos[1], pos[2], size);
    }
    return true;
}

// CPacketHandler::Packet_ProjectileSync (CPacketHandler.cpp:4983-5129).
bool HandleProjectile(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    bool hasCreator = false;
    uint32_t creatorId = kInvalidElementId;
    uint32_t latency = 0;
    if (!ReadFlag(reader, hasCreator)) return false;
    if (hasCreator) {
        if (!ReadElementId(reader, creatorId) || !ReadCompressed(reader, 2, latency)) return false;
    }

    bool hasOrigin = false;
    uint32_t originId = kInvalidElementId;
    if (!ReadFlag(reader, hasOrigin)) return false;
    if (hasOrigin) {
        if (!ReadElementId(reader, originId)) return false;
    }

    float originPos[3] = {};
    if (!ReadFinitePosition(reader, originPos)) return false;

    uint8_t weaponType = 0;
    if (!reader.ReadBits(&weaponType, 6)) return false;
    weaponType &= 0x3Fu;

    uint16_t model = 0;
    if (!reader.Read(model)) return false;

    float force = 0.f;
    uint32_t targetId = kInvalidElementId;
    float velocity[3] = {};
    float rotation[3] = {};
    bool create = false;

    switch (weaponType) {
        case kWeaponGrenade:
        case kWeaponTeargas:
        case kWeaponMolotov:
        case kWeaponSatchel:
            if (!ReadFloat7_17(reader, force)) return false;
            if (!ReadVelocity(reader, velocity)) return false;
            create = true;
            break;
        case kWeaponRocket:
        case kWeaponRocketHs: {
            bool hasTarget = false;
            if (!ReadFlag(reader, hasTarget)) return false;
            if (hasTarget && !ReadElementId(reader, targetId)) return false;
            if (!ReadVelocity(reader, velocity)) return false;
            if (!ReadFiniteFloat3(reader, rotation)) return false;
            create = true;
            break;
        }
        case kWeaponFlare:
        case kWeaponFreefallBomb:
            create = true;
            break;
        default:
            create = false;
            break;
    }
    (void)latency;

    if (!create || creatorId == kInvalidElementId) return true;

    ServerRpc::Enqueue([creatorId, originId, targetId, weaponType, model, force, originPos, velocity,
                        rotation]() mutable {
        if (originId != kInvalidElementId) {
            float ox = 0.f, oy = 0.f, oz = 0.f;
            if (ServerWorld::ElementPose(originId, ox, oy, oz)) {
                originPos[0] += ox;
                originPos[1] += oy;
                originPos[2] += oz;
            }
        }
        uint32_t resolvedCreator = creatorId;
        if (IsPedOrPlayer(resolvedCreator)) {
            uint32_t vehId = 0;
            uint8_t seat = 0;
            if (RemoteVehicle::FindOccupant(resolvedCreator, vehId, seat)) resolvedCreator = vehId;
        }
        if (model != 0 || rotation[0] != 0.f || rotation[1] != 0.f || rotation[2] != 0.f) {
            static int diagOverride = 0;
            if (diagOverride < 4) {
                ++diagOverride;
                LUA_LOGI("DIAG PROJECTILE weapon=%u model=%u: initial rotation/model override is not hooked",
                         static_cast<unsigned>(weaponType), static_cast<unsigned>(model));
            }
        }

        Projectiles::Request req;
        req.creator = resolvedCreator;
        req.target = targetId == kInvalidElementId ? 0 : targetId;
        req.weapon = static_cast<int>(weaponType);
        req.origin = {originPos[0], originPos[1], originPos[2]};
        req.originMask = 7;
        req.velocity = {velocity[0], velocity[1], velocity[2]};
        req.force = force;
        const uint32_t projId = Projectiles::Create(req);
        if (projId != 0) {
            lua_State* L = Script::ActiveVmState();
            if (L) {
                Script::PushElement(L, resolvedCreator);
                Script::ActiveVmFireElementEvent(L, "onClientProjectileCreation", projId, 1);
            }
        } else {
            static int diagFail = 0;
            if (diagFail < 4) {
                ++diagFail;
                LUA_LOGI("DIAG PROJECTILE weapon=%u pos=(%.1f,%.1f,%.1f): native CProjectileInfo::AddProjectile unavailable",
                         static_cast<unsigned>(weaponType), originPos[0], originPos[1], originPos[2]);
            }
        }
    });
    return true;
}

// CPacketHandler::Packet_PickupHideShow (CPacketHandler.cpp:4569-4615).
bool HandlePickupHideShow(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    bool show = false;
    if (!ReadFlag(reader, show)) return false;

    while (reader.GetRemainingBits() > 10) {
        uint32_t pickupId = 0;
        uint32_t model = 0;
        if (!ReadElementId(reader, pickupId)) return false;
        if (!ReadCompressed(reader, 2, model)) return false;
        ServerWorld::NotePickupHideShow(pickupId, show, static_cast<uint16_t>(model));
    }

    static bool logged = false;
    if (!logged) {
        logged = true;
        LUA_LOGI("DIAG PICKUP_HIDESHOW: native CPickup visibility/model update is not hooked");
    }
    return true;
}

// CPacketHandler::Packet_PickupHitConfirm (CPacketHandler.cpp:4617-4655).
bool HandlePickupHitConfirm(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t pickupId = 0;
    bool hide = false;
    bool playSound = false;
    if (!ReadElementId(reader, pickupId) || !ReadFlag(reader, hide) || !ReadFlag(reader, playSound)) return false;

    // Desktop CPacketHandler.cpp:4633 literally passes bHide to pPickup->SetVisible(bHide).
    ServerWorld::NotePickupVisible(pickupId, hide);
    if (playSound) {
        static int diagCount = 0;
        if (diagCount < 4) {
            ++diagCount;
            LUA_LOGI("DIAG PICKUP_HIT_CONFIRM: CAudioEngine::PlayFrontEndSound(40) is not hooked");
        }
    }
    return true;
}

// CPacketHandler::Packet_VehicleSpawn (CPacketHandler.cpp:1677-1758).
bool HandleVehicleSpawn(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    if (reader.GetRemainingBits() <= 16) return false;

    while (reader.GetRemainingBits() > 16) {
        uint32_t vehicleId = 0;
        uint8_t timeContext = 0;
        float pos[3] = {}, rotDeg[3] = {};
        VehicleColors colors;

        if (!ReadElementId(reader, vehicleId)) return false;
        if (!reader.Read(timeContext)) return false;
        if (!ReadFiniteFloat3(reader, pos) || !ReadFiniteFloat3(reader, rotDeg)) return false;
        if (!ReadVehicleColors(reader, colors)) return false;

        if (!IsVehicle(vehicleId)) continue;

        ServerWorld::SetSyncTimeContext(vehicleId, timeContext);
        ServerWorld::NoteVehiclePose(vehicleId, true, pos, true, rotDeg, true, 1000.f);
        ServerWorld::SetVehicleColors(vehicleId, colors);
        ServerWorld::NoteAttachTo(vehicleId, kInvalidElementId);

        Script::VehicleApplyFixState(vehicleId, true);
        Script::VehicleDetachTowedBy(vehicleId);

        Game::RpcSetVehiclePosition(vehicleId, pos[0], pos[1], pos[2]);
        Game::RpcSetVehicleRotation(vehicleId, rotDeg[0], rotDeg[1], rotDeg[2]);
        Game::RpcSetVehicleVelocity(vehicleId, 0.f, 0.f, -0.01f, false);
        Game::RpcSetVehicleVelocity(vehicleId, 0.f, 0.f, 0.f, true);
        Game::RpcSetVehicleHealth(vehicleId, 1000.f);

        ServerRpc::Enqueue([vehicleId] {
            lua_State* L = Script::ActiveVmState();
            if (L) Script::ActiveVmFireElementEvent(L, "onClientVehicleRespawn", vehicleId, 0);
        });
    }
    return true;
}

// CPacketHandler::Packet_VehicleDamageSync (CPacketHandler.cpp:1760-1803).
bool HandleVehicleDamageSync(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t vehicleId = 0;
    if (!ReadElementId(reader, vehicleId)) return false;

    bool doorChanged[6], wheelChanged[4], panelChanged[7], lightChanged[4];
    uint8_t doors[6], wheels[4], panels[7], lights[4];
    if (!ReadPartStateDelta<6, 3>(reader, doorChanged, doors)) return false;
    if (!ReadPartStateDelta<4, 2>(reader, wheelChanged, wheels)) return false;
    if (!ReadPartStateDelta<7, 2>(reader, panelChanged, panels)) return false;
    if (!ReadPartStateDelta<4, 2>(reader, lightChanged, lights)) return false;

    if (!IsVehicle(vehicleId)) return true;

    Script::VehicleApplyDamageSync(vehicleId, doorChanged, doors, wheelChanged, wheels, panelChanged, panels,
                                   lightChanged, lights);
    static bool logged = false;
    if (!logged) {
        logged = true;
        LUA_LOGI("DIAG VEHICLE_DAMAGE_SYNC: native CDamageManager visual damage write is not hooked");
    }
    return true;
}

// CPacketHandler::Packet_VehicleTrailer (CPacketHandler.cpp:2313-2375).
bool HandleVehicleTrailer(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t vehicleId = 0, trailerId = 0;
    bool attached = false;
    float pos[3] = {}, rotDeg[3] = {}, turn[3] = {};

    if (!ReadElementId(reader, vehicleId) || !ReadElementId(reader, trailerId) || !ReadFlag(reader, attached))
        return false;
    if (attached) {
        if (!ReadFinitePosition(reader, pos)) return false;
        if (!ReadRotationDegrees(reader, rotDeg[0], rotDeg[1], rotDeg[2])) return false;
        if (!ReadVelocity(reader, turn)) return false;
    }

    if (!IsVehicle(vehicleId) || !IsVehicle(trailerId)) return true;

    if (attached) {
        ServerWorld::NoteVehiclePose(trailerId, true, pos, true, rotDeg, false, 0.f);
        Game::RpcSetVehiclePosition(trailerId, pos[0], pos[1], pos[2]);
        Game::RpcSetVehicleRotation(trailerId, rotDeg[0], rotDeg[1], rotDeg[2]);
        Game::RpcSetVehicleVelocity(trailerId, turn[0], turn[1], turn[2], true);
        Script::VehicleApplyTrailerLink(vehicleId, trailerId, true);
        ServerRpc::Enqueue([vehicleId, trailerId] {
            lua_State* L = Script::ActiveVmState();
            if (L) {
                Script::PushElement(L, vehicleId);
                Script::ActiveVmFireElementEvent(L, "onClientTrailerAttach", trailerId, 1);
            }
        });
    } else {
        Script::VehicleApplyTrailerLink(vehicleId, trailerId, false);
        ServerRpc::Enqueue([vehicleId, trailerId] {
            lua_State* L = Script::ActiveVmState();
            if (L) {
                Script::PushElement(L, vehicleId);
                Script::ActiveVmFireElementEvent(L, "onClientTrailerDetach", trailerId, 1);
            }
        });
    }
    return true;
}

// CObjectSync::Packet_ObjectStartSync (CObjectSync.cpp:99-137).
bool HandleObjectStartSync(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t objectId = 0;
    bool dynamicPhysics = false;
    float pos[3] = {}, rotRad[3] = {}, vel[3] = {}, turn[3] = {};
    float health = 0.f;

    if (!ReadElementId(reader, objectId) || !ReadFlag(reader, dynamicPhysics)) return false;
    if (!ReadFinitePosition(reader, pos)) return false;
    if (!ReadRotationRadians(reader, rotRad[0], rotRad[1], rotRad[2])) return false;
    if (!ReadVelocity(reader, vel) || !ReadVelocity(reader, turn)) return false;
    if (!ReadObjectHealth(reader, health)) return false;
    (void)dynamicPhysics;

    if (!IsObject(objectId)) return true;

    // Desktop intentionally does not overwrite position/rotation here (CObjectSync.cpp:121-135).
    ServerWorld::NoteObjectSync(objectId, false, nullptr, false, nullptr, true, health);
    ServerWorld::SetObjectSyncHosted(objectId, true);

    static bool logged = false;
    if (!logged) {
        logged = true;
        LUA_LOGI("DIAG OBJECT_STARTSYNC: CClientObjectPhysicsManager dynamic physics and velocity are not hooked");
    }
    return true;
}

// CObjectSync::Packet_ObjectStopSync (CObjectSync.cpp:139-148).
bool HandleObjectStopSync(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t objectId = 0;
    if (!ReadElementId(reader, objectId)) return false;
    if (IsObject(objectId)) ServerWorld::SetObjectSyncHosted(objectId, false);
    return true;
}

// CObjectSync::Packet_ObjectSync (CObjectSync.cpp:150-201).
bool HandleObjectSync(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    if (reader.GetRemainingBits() <= 8) return false;

    while (reader.GetRemainingBits() > 8) {
        uint32_t objectId = 0;
        uint8_t syncContext = 0;
        uint8_t flags = 0;
        if (!ReadElementId(reader, objectId) || !reader.Read(syncContext) || !reader.ReadBits(&flags, 5)) return false;
        flags &= 0x1Fu;

        float pos[3] = {}, rotRad[3] = {}, vel[3] = {}, turn[3] = {};
        float health = 0.f;
        const bool hasPos = (flags & 0x01u) != 0;
        const bool hasRot = (flags & 0x02u) != 0;
        const bool hasHealth = (flags & 0x04u) != 0;
        const bool hasVel = (flags & 0x08u) != 0;
        const bool hasTurn = (flags & 0x10u) != 0;

        if (hasPos && !ReadFinitePosition(reader, pos)) return false;
        if (hasRot && !ReadRotationRadians(reader, rotRad[0], rotRad[1], rotRad[2])) return false;
        if (hasHealth && !ReadObjectHealth(reader, health)) return false;
        if (hasVel && !ReadVelocity(reader, vel)) return false;
        if (hasTurn && !ReadVelocity(reader, turn)) return false;

        if (!IsObject(objectId) || !ServerWorld::CanUpdateSync(objectId, syncContext)) continue;

        ServerWorld::NoteObjectSync(objectId, hasPos, pos, hasRot, rotRad, hasHealth, health);
        if (hasPos || hasRot) {
            std::array<float, 3> posCopy = {pos[0], pos[1], pos[2]};
            std::array<float, 3> rotCopy = {rotRad[0], rotRad[1], rotRad[2]};
            ServerRpc::Enqueue([objectId, hasPos, posCopy, hasRot, rotCopy] {
                Script::ServerSyncObject(objectId, hasPos, posCopy.data(), hasRot, rotCopy.data());
            });
        }
        if (hasVel || hasTurn) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                LUA_LOGI("DIAG OBJECT_SYNC: CClientObjectPhysicsManager synced velocity is not hooked");
            }
        }
    }
    return true;
}

// CLatentTransferManager::OnReceive / CLatentReceiver::OnReceive
// (CLatentTransferManager.cpp:297-308, CLatentReceiver.cpp:66-195).
bool HandleLatentTransfer(const uint8_t* body, size_t length, size_t bits, const InnerPacketDispatch& dispatch) {
    BitStreamReader reader(body, length, bits);
    uint8_t idRaw[2] = {};
    if (!reader.ReadBits(idRaw, 15)) return false;
    const uint16_t usId = static_cast<uint16_t>(idRaw[0]) | (static_cast<uint16_t>(idRaw[1] & 0x7Fu) << 8);

    bool isSpecial = false;
    if (!ReadFlag(reader, isSpecial)) return false;

    bool isHead = false;
    bool isTail = false;
    bool isCancel = false;
    uint16_t category = 0;
    uint16_t resourceNetId = 0xFFFFu;
    uint32_t finalSize = 0;
    uint32_t rate = 0;

    if (isSpecial) {
        uint8_t specialFlag = 0;
        if (!reader.Read(specialFlag)) return false;
        if (specialFlag == kLatentFlagHead) {
            isHead = true;
            if (!reader.Read(category) || !reader.Read(finalSize) || !reader.Read(rate) || !reader.Read(resourceNetId))
                return false;
        } else if (specialFlag == kLatentFlagTail) {
            isTail = true;
        } else if (specialFlag == kLatentFlagCancel) {
            isCancel = true;
        } else {
            LUA_LOGE("DIAG LATENT_TRANSFER: invalid special flag %u", static_cast<unsigned>(specialFlag));
            return false;
        }
    }

    const size_t rem = reader.GetReadOffsetBits() & 7u;
    if (rem != 0) {
        uint8_t pad = 0;
        if (!reader.ReadBits(&pad, 8u - rem)) return false;
    }

    uint16_t sizeSent = 0;
    if (!reader.Read(sizeSent)) return false;
    if (static_cast<size_t>(sizeSent) * 8u > reader.GetRemainingBits()) return false;

    std::vector<uint8_t> chunk(sizeSent);
    if (sizeSent != 0 && !reader.ReadBits(chunk.data(), static_cast<size_t>(sizeSent) * 8u)) return false;

    std::vector<uint8_t> completed;
    uint16_t completedCategory = 0;
    {
        std::lock_guard<std::mutex> lock(g_latentMutex);
        if (isHead) {
            if (g_latentRx.started) {
                LUA_LOGE("DIAG LATENT_TRANSFER: HEAD while transfer %u already active", g_latentRx.id);
                g_latentRx = LatentReceiveState{};
                return false;
            }
            if (finalSize > kLatentMaxSize) {
                LUA_LOGE("DIAG LATENT_TRANSFER: finalSize %u exceeds limit", finalSize);
                return false;
            }
            g_latentRx.started = true;
            g_latentRx.id = usId;
            g_latentRx.category = category;
            g_latentRx.rate = rate;
            g_latentRx.resourceNetId = resourceNetId;
            g_latentRx.finalSize = finalSize;
            g_latentRx.buffer.clear();
        }

        if (!g_latentRx.started || g_latentRx.id != usId) {
            LUA_LOGE("DIAG LATENT_TRANSFER: unexpected id %u (active=%d id=%u)", usId, g_latentRx.started ? 1 : 0,
                     g_latentRx.id);
            return false;
        }

        if (isCancel) {
            g_latentRx = LatentReceiveState{};
            return true;
        }

        if (g_latentRx.buffer.size() + chunk.size() > g_latentRx.finalSize) {
            LUA_LOGE("DIAG LATENT_TRANSFER: chunk overflow (%zu + %zu > %u)", g_latentRx.buffer.size(), chunk.size(),
                     g_latentRx.finalSize);
            g_latentRx = LatentReceiveState{};
            return false;
        }
        if (isTail && g_latentRx.buffer.size() + chunk.size() != g_latentRx.finalSize) {
            LUA_LOGE("DIAG LATENT_TRANSFER: tail size mismatch (%zu + %zu != %u)", g_latentRx.buffer.size(),
                     chunk.size(), g_latentRx.finalSize);
            g_latentRx = LatentReceiveState{};
            return false;
        }

        g_latentRx.buffer.insert(g_latentRx.buffer.end(), chunk.begin(), chunk.end());
        if (isTail) {
            completed = std::move(g_latentRx.buffer);
            completedCategory = g_latentRx.category;
            g_latentRx = LatentReceiveState{};
        }
    }

    if (!isTail) return true;

    if (completedCategory != kLatentCategoryPacket) {
        LUA_LOGI("DIAG LATENT_TRANSFER: unknown category %u", static_cast<unsigned>(completedCategory));
        return true;
    }

    if (completed.size() < 5) {
        LUA_LOGE("DIAG LATENT_TRANSFER: packet payload too small (%zu)", completed.size());
        return false;
    }
    const uint8_t innerPacketId = completed[0];
    uint32_t bitsUsed = 0;
    std::memcpy(&bitsUsed, completed.data() + 1, sizeof(bitsUsed));
    const uint64_t bytesUsed = (static_cast<uint64_t>(bitsUsed) + 7ull) >> 3;
    if (bytesUsed != static_cast<uint64_t>(completed.size() - 5)) {
        LUA_LOGE("DIAG LATENT_TRANSFER: packet bit size mismatch (bits=%u bytes=%llu payload=%zu)", bitsUsed,
                 static_cast<unsigned long long>(bytesUsed), completed.size() - 5);
        return false;
    }

    if (!dispatch) return true;
    if (g_latentDepth >= 2) {
        LUA_LOGE("DIAG LATENT_TRANSFER: nested recursion limit reached");
        return false;
    }
    struct DepthGuard {
        DepthGuard() { ++g_latentDepth; }
        ~DepthGuard() { --g_latentDepth; }
    } guard;
    return dispatch(innerPacketId, completed.data() + 5, static_cast<size_t>(bytesUsed),
                    static_cast<size_t>(bitsUsed));
}

// CPacketHandler::Packet_PedTask (CPacketHandler.cpp:5893-5935).
bool HandlePedTask(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t sourceId = kInvalidElementId;
    uint16_t taskType = 0;
    if (!ReadElementId(reader, sourceId) || !reader.Read(taskType)) return false;

    if (taskType == kTaskSimpleBeHit) {
        uint32_t attackerId = kInvalidElementId;
        uint8_t hitBodyPart = 0, hitBodySide = 0, weaponId = 0;
        if (!ReadElementId(reader, attackerId) || !reader.Read(hitBodyPart) || !reader.Read(hitBodySide) ||
            !reader.Read(weaponId))
            return false;
        static int diagCount = 0;
        if (diagCount < 4) {
            ++diagCount;
            LUA_LOGI("DIAG PED_TASK BE_HIT ped=%u attacker=%u part=%u side=%u weapon=%u: CClientPed::BeHit is not hooked",
                     sourceId, attackerId, static_cast<unsigned>(hitBodyPart), static_cast<unsigned>(hitBodySide),
                     static_cast<unsigned>(weaponId));
        }
    }
    return true;
}

void ResetSession() {
    std::lock_guard<std::mutex> lock(g_latentMutex);
    g_latentRx = LatentReceiveState{};
}

}  // namespace WorldRPG::CorePackets
