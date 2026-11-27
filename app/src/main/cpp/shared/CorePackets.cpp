#include "CorePackets.h"
#include "WorldNativeBridge.h"

#include "LuaApiExtra.h"
#include "LuaAudioApi.h"
#include "LuaFieldApi.h"
#include "LuaVMStub.h"
#include "LuaWaterApi.h"
#include "LuaWorldObjectApi.h"
#include "MtaIds.h"
#include "ServerRpc.h"
#include "ServerWorld.h"
#include "VehicleColors.h"
#include "VoiceRuntime.h"
#include "entity/EntityAddTypes.h"
#include "entity/EntitySyncReaders.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/ProjectileEngine.h"
#include "../gtasa/RemoteVehicle.h"
#include "../net/NeonJoinProtocol.h"
#include "../net/raknet/RakNetWrapper.h"

#include "lua.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace WorldRPG::JniBridge { void PostGameLine(int kind, const char* text); }

namespace WorldRPG::CorePackets {
namespace {

using Net::BitStreamReader;
using Net::BitStreamWriter;
using Entity::ReadCompressed;
using Entity::ReadElementId;
using Entity::ReadFlag;

std::mutex g_extraStateMutex;
PartialPacketProgress g_lastPartialPacket{};
DisconnectRecord g_lastDisconnect{};
ReturnSyncRecord g_lastReturnSync{};
UpdateInfoRecord g_lastUpdateInfo{};
DebugEventRecord g_lastDebugEvent{};
OutboundPacketSink g_outboundSink;

void WriteFlag(BitStreamWriter& w, bool on) {
    uint8_t bit = on ? 1u : 0u;
    w.WriteBits(&bit, 1);
}

void WriteBitsValue(BitStreamWriter& w, uint32_t value, unsigned bits) {
    uint8_t bytes[4] = {};
    for (int i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
    w.WriteBits(bytes, bits);
}

void WriteElementId(BitStreamWriter& w, uint32_t id) {
    constexpr uint32_t kMask = (1u << 17) - 1u;
    WriteBitsValue(w, id == 0xFFFFFFFFu ? kMask : (id & kMask), 17);
}

void WriteFixed24(BitStreamWriter& w, float value) {
    const double scaled = std::isfinite(value) ? static_cast<double>(value) * 1024.0 : 0.0;
    const long fixed = std::lround(std::clamp(scaled, -8388608.0, 8388607.0));
    WriteBitsValue(w, static_cast<uint32_t>(fixed) & 0xFFFFFFu, 24);
}

void WritePosition(BitStreamWriter& w, float x, float y, float z) {
    WriteFixed24(w, x);
    WriteFixed24(w, y);
    w.Write(z);
}

void WriteFloatAsBits(BitStreamWriter& w, unsigned bits, float minValue, float maxValue, float value,
                      bool preserveAboveMin = false) {
    const double maxRaw = static_cast<double>((1ull << bits) - 1ull);
    double alpha = 0.0;
    if (maxValue != minValue) alpha = (static_cast<double>(value) - minValue) / (static_cast<double>(maxValue) - minValue);
    if (!std::isfinite(alpha)) alpha = 0.0;
    alpha = std::clamp(alpha, 0.0, 1.0);
    uint32_t raw = static_cast<uint32_t>(std::lround(alpha * maxRaw));
    if (preserveAboveMin && raw == 0 && alpha > 0.0) raw = 1;
    WriteBitsValue(w, raw, bits);
}

bool DispatchOutboundPacket(uint8_t gamePacketId, BitStreamWriter& packet, bool reliable, bool highPriority) {
    OutboundPacketSink sink;
    {
        std::lock_guard<std::mutex> lock(g_extraStateMutex);
        sink = g_outboundSink;
    }
    if (sink) {
        return sink(gamePacketId, packet.GetData(), packet.GetByteLength(), packet.GetBitLength(),
                    reliable, highPriority);
    }
    if (highPriority) return Net::SendGamePacketHighPriority(packet);
    return Net::SendGamePacket(packet, reliable);
}

bool ReadUsString(BitStreamReader& reader, std::string& out, size_t maxBytes = 4096) {
    uint16_t len = 0;
    if (!reader.Read(len) || len > maxBytes) return false;
    out.resize(len);
    if (len > 0 && !reader.ReadBytes(reinterpret_cast<uint8_t*>(out.data()), len)) {
        out.clear();
        return false;
    }
    return true;
}

template <unsigned Bits, typename T>
bool ReadRangeBits(BitStreamReader& reader, T& out, T low, T high) {
    uint8_t raw[4] = {};
    if (!reader.ReadBits(raw, Bits)) return false;
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= static_cast<uint32_t>(raw[i]) << (8u * i);
    const uint32_t mask = (Bits >= 32) ? 0xFFFFFFFFu : ((1u << Bits) - 1u);
    const int64_t val = static_cast<int64_t>(v & mask) + static_cast<int64_t>(low);
    if (val < static_cast<int64_t>(low)) out = low;
    else if (val > static_cast<int64_t>(high)) out = high;
    else out = static_cast<T>(val);
    return true;
}
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
            ServerWorld::NoteVehiclePose(resolvedOrigin, false, nullptr, false, nullptr, true, 0.f);
            if (L) {
                lua_pushboolean(L, !cancel ? 1 : 0);
                Script::ActiveVmFireElementEvent(L, "onClientVehicleExplode", resolvedOrigin, 1);
            }
        }

        if (!cancel) {
            if (!WorldNative::CreateExplosion(pos[0], pos[1], pos[2], static_cast<int>(expType), true, -1.f, true)) {
                static int diagCount = 0;
                if (diagCount < 4) {
                    ++diagCount;
                    LUA_LOGI("DIAG EXPLOSION type=%u pos=(%.1f,%.1f,%.1f): CExplosion::AddExplosion is not hooked",
                             static_cast<unsigned>(expType), pos[0], pos[1], pos[2]);
                }
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

    if (!WorldNative::CreateFire(pos[0], pos[1], pos[2], size)) {
        static int diagCount = 0;
        if (diagCount < 4) {
            ++diagCount;
            LUA_LOGI("DIAG FIRE pos=(%.1f,%.1f,%.1f) size=%.2f: CFireManager::StartFire is not hooked",
                     pos[0], pos[1], pos[2], size);
        }
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
        Script::AudioPlayFrontEndSound(40);
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

    if (!IsVehicle(vehicleId) || !IsVehicle(trailerId) || vehicleId == trailerId) return true;

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
    if (innerPacketId == MtaIds::Packet::LatentTransfer || g_latentDepth >= 1) {
        LUA_LOGE("DIAG LATENT_TRANSFER: nested latent transfer rejected (depth=%d)", g_latentDepth);
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

struct ServerTextItem {
    uint32_t id = 0;
    float x = 0.f;
    float y = 0.f;
    float scale = 1.f;
    uint8_t r = 255, g = 255, b = 255, a = 255;
    uint8_t format = 0;
    uint8_t shadowAlpha = 0;
    std::string text;
};

std::mutex g_textMutex;
std::map<uint32_t, ServerTextItem> g_textItems;

std::string ReadRemainingString(BitStreamReader& reader, size_t maxBytes) {
    const size_t avail = reader.GetRemainingBits() / 8;
    const size_t n = std::min(avail, maxBytes);
    if (n == 0) return {};
    std::string out(n, '\0');
    if (!reader.ReadBytes(reinterpret_cast<uint8_t*>(out.data()), n)) return {};
    const size_t nul = out.find('\0');
    if (nul != std::string::npos) out.resize(nul);
    return out;
}

std::string SanitizeSingleLine(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (c == '\r') continue;
        if (c == '\n') out.push_back(' ');
        else if (c >= 0x20 || c == '\t') out.push_back(static_cast<char>(c));
    }
    return out;
}

// CPacketHandler::Packet_PlayerChangeNick (CPacketHandler.cpp:1100-1142).
bool HandlePlayerChangeNick(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t playerId = kInvalidElementId;
    if (!ReadElementId(reader, playerId)) return false;
    std::string newNick = ReadRemainingString(reader, 22);
    if (newNick.empty()) return false;

    std::string oldNick;
    ServerWorld::CopyPlayerNick(playerId, oldNick);
    ServerWorld::NotePlayerNick(playerId, newNick);

    ServerRpc::Enqueue([playerId, oldNick, newNick] {
        lua_State* L = Script::ActiveVmState();
        if (!L) return;
        lua_pushlstring(L, oldNick.data(), oldNick.size());
        lua_pushlstring(L, newNick.data(), newNick.size());
        Script::ActiveVmFireElementEvent(L, "onClientPlayerChangeNick", playerId, 2);
    });
    return true;
}

// CPacketHandler::Packet_PlayerStats (CPacketHandler.cpp:2772-2807).
bool HandlePlayerStats(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t playerId = kInvalidElementId;
    uint32_t count = 0;
    if (!ReadElementId(reader, playerId) || !ReadCompressed(reader, 2, count)) return false;
    if (count > 256) return false;
    std::vector<std::pair<uint16_t, float>> updates;
    updates.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t stat = 0;
        float value = 0.f;
        if (!reader.Read(stat) || !reader.Read(value)) return false;
        if (!std::isfinite(value)) continue;
        updates.emplace_back(stat, value);
    }
    ServerRpc::Enqueue([playerId, updates = std::move(updates)] {
        for (const auto& [stat, value] : updates) {
            Script::RpcSetPedStat(playerId, stat, value);
        }
    });
    return true;
}

// CPacketHandler::Packet_DetonateSatchels (CPacketHandler.cpp:5131-5158).
bool HandleDetonateSatchels(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t playerId = kInvalidElementId;
    uint32_t latency = 0;
    if (!ReadElementId(reader, playerId)) return false;
    if (reader.GetRemainingBits() >= 8) ReadCompressed(reader, 2, latency);
    LUA_LOGI("DETONATE_SATCHELS player=%u latency=%u", playerId, latency);
    return true;
}

// CPacketHandler::Packet_DestroySatchels (CPacketHandler.cpp:5160-5176).
bool HandleDestroySatchels(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t playerId = kInvalidElementId;
    if (!ReadElementId(reader, playerId)) return false;
    LUA_LOGI("DESTROY_SATCHELS player=%u", playerId);
    return true;
}

// CPacketHandler::Packet_ConsoleEcho (CPacketHandler.cpp:1221-1244).
bool HandleConsoleEcho(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    const std::string text = SanitizeSingleLine(ReadRemainingString(reader, 1024));
    LUA_LOGI("CONSOLE: %s", text.c_str());
    ServerRpc::Enqueue([text] {
        lua_State* L = Script::ActiveVmState();
        if (!L) return;
        lua_pushlstring(L, text.data(), text.size());
        Script::ActiveVmFireElementEvent(L, "onClientConsole", Script::ClientRootElement(), 1);
    });
    return true;
}

// CPacketHandler::Packet_DebugEcho (CPacketHandler.cpp:1246-1275).
bool HandleDebugEcho(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint8_t level = 0;
    if (!reader.Read(level)) return false;
    uint8_t r = 255, g = 255, b = 255;
    if (level == 0 || level == 4) {
        if (!reader.Read(r) || !reader.Read(g) || !reader.Read(b)) return false;
    } else if (level == 1) {
        r = 255; g = 0; b = 0;
    } else if (level == 2) {
        r = 255; g = 128; b = 0;
    } else if (level == 3) {
        r = 0; g = 255; b = 0;
    }
    const std::string text = SanitizeSingleLine(ReadRemainingString(reader, 1024));
    LUA_LOGI("DEBUG[%u]: %s", static_cast<unsigned>(level), text.c_str());
    ServerRpc::Enqueue([text, level, r, g, b] {
        lua_State* L = Script::ActiveVmState();
        if (!L) return;
        lua_pushlstring(L, text.data(), text.size());
        lua_pushinteger(L, level);
        lua_pushstring(L, "");
        lua_pushinteger(L, 0);
        lua_pushinteger(L, r);
        lua_pushinteger(L, g);
        lua_pushinteger(L, b);
        Script::ActiveVmFireElementEvent(L, "onClientDebugMessage", Script::ClientRootElement(), 7);
    });
    return true;
}

// CPacketHandler::Packet_VoiceEnd (CPacketHandler.cpp:5824-5843).
bool HandleVoiceEnd(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t playerId = kInvalidElementId;
    if (!ReadElementId(reader, playerId)) return false;
    WorldRPG::Audio::VoiceRuntime::OnVoiceEnd(playerId);
    return true;
}

// CPacketHandler::Packet_TextItem (CPacketHandler.cpp:5266-5325).
bool HandleTextItem(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    bool del = false;
    if (!ReadCompressed(reader, 4, id) || !ReadFlag(reader, del)) return false;
    if (del) {
        std::lock_guard<std::mutex> lock(g_textMutex);
        g_textItems.erase(id);
        return true;
    }
    ServerTextItem item;
    item.id = id;
    uint32_t len = 0;
    if (!reader.Read(item.x) || !reader.Read(item.y) || !reader.Read(item.scale)) return false;
    if (!std::isfinite(item.x) || !std::isfinite(item.y) || !std::isfinite(item.scale)) return false;
    if (!reader.Read(item.r) || !reader.Read(item.g) || !reader.Read(item.b) || !reader.Read(item.a)) return false;
    if (!reader.Read(item.format) || !reader.Read(item.shadowAlpha)) return false;
    if (!ReadCompressed(reader, 2, len) || len > 1024) return false;
    if (len > 0) {
        std::string raw(len, '\0');
        if (!reader.ReadBytes(reinterpret_cast<uint8_t*>(raw.data()), len)) return false;
        item.text = SanitizeSingleLine(raw);
    }
    std::lock_guard<std::mutex> lock(g_textMutex);
    g_textItems[id] = std::move(item);
    return true;
}

// CPacketHandler::Packet_ChatClear (CPacketHandler.cpp:1215-1219).
bool HandleChatClear(const uint8_t* body, size_t length, size_t bits) {
    (void)body;
    (void)length;
    (void)bits;
    WorldRPG::JniBridge::PostGameLine(8, "");
    return true;
}

// CPacketHandler::Packet_ServerInfoSync (CPacketHandler.cpp:5937-5956).
bool HandleServerInfoSync(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint8_t flags = 0;
    if (!reader.Read(flags)) return false;
    if (flags & 1u) {
        uint32_t maxPlayers = 0;
        if (!reader.Read(maxPlayers)) return false;
        ServerWorld::SetMaxPlayers(maxPlayers);
    }
    return true;
}

void AppendServerTextItems(std::string& frame, int screenW, int screenH) {
    if (screenW <= 0 || screenH <= 0) return;
    std::lock_guard<std::mutex> lock(g_textMutex);
    if (g_textItems.empty()) return;
    char hdr[160];
    for (const auto& [id, item] : g_textItems) {
        (void)id;
        if (item.text.empty() || item.a == 0) continue;
        const float px = item.x * static_cast<float>(screenW);
        const float py = item.y * static_cast<float>(screenH);
        const float scale = std::clamp(item.scale, 0.25f, 10.f);
        const float boxH = std::max(24.f, 24.f * scale);
        float left = px;
        float right = static_cast<float>(screenW);
        if (item.format & 1u) {  // DT_CENTER
            const float halfW = std::min(px, static_cast<float>(screenW) - px);
            left = std::max(0.f, px - std::max(halfW, 120.f));
            right = std::min(static_cast<float>(screenW), px + std::max(halfW, 120.f));
        } else if (item.format & 2u) {  // DT_RIGHT
            left = 0.f;
            right = std::max(1.f, px);
        }
        float top = py;
        float bottom = std::min(static_cast<float>(screenH), py + boxH * 3.f);
        if (item.format & 4u) {  // DT_VCENTER
            top = std::max(0.f, py - boxH * 0.5f);
            bottom = std::min(static_cast<float>(screenH), py + boxH * 0.5f);
        } else if (item.format & 8u) {  // DT_BOTTOM
            top = std::max(0.f, py - boxH);
            bottom = std::max(top + 1.f, py);
        }
        if (right <= left || bottom <= top) continue;
        if (item.shadowAlpha > 0) {
            const uint32_t shadowArgb = static_cast<uint32_t>(item.shadowAlpha) << 24;
            std::snprintf(hdr, sizeof(hdr), "T %.1f %.1f %.1f %.1f %.3f %u 0 ",
                          left + 1.f, top + 1.f, right + 1.f, bottom + 1.f, scale, shadowArgb);
            frame += hdr;
            frame += item.text;
            frame.push_back('\n');
        }
        const uint32_t argb = (static_cast<uint32_t>(item.a) << 24) |
                              (static_cast<uint32_t>(item.r) << 16) |
                              (static_cast<uint32_t>(item.g) << 8) |
                              static_cast<uint32_t>(item.b);
        std::snprintf(hdr, sizeof(hdr), "T %.1f %.1f %.1f %.1f %.3f %u 0 ",
                      left, top, right, bottom, scale, argb);
        frame += hdr;
        frame += item.text;
        frame.push_back('\n');
    }
}

size_t ServerTextItemCount() {
    std::lock_guard<std::mutex> lock(g_textMutex);
    return g_textItems.size();
}

// =========================================================================
// PACKET_ID_PACKET_PROGRESS (8)
// Desktop: CPacketHandler::Packet_PartialPacketInfo (CPacketHandler.cpp:2819-2839)
// =========================================================================
bool HandlePartialPacketInfo(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint32_t partCount = 0, partTotal = 0, partLength = 0;
    uint8_t mtaPacketId = 0;
    if (!reader.Read(partCount) || !reader.Read(partTotal) || !reader.Read(partLength) || !reader.Read(mtaPacketId)) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_extraStateMutex);
        g_lastPartialPacket = PartialPacketProgress{partCount, partTotal, partLength};
    }
    return true;
}

// =========================================================================
// PACKET_ID_SERVER_DISCONNECTED (23)
// Desktop: CPacketHandler::Packet_ServerDisconnected (CPacketHandler.cpp:545-685)
// =========================================================================
bool HandleServerDisconnected(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint8_t rawType = 0;
    if (!reader.ReadBits(&rawType, 5)) return false;
    const uint8_t type = static_cast<uint8_t>(rawType & 0x1Fu);

    std::string durationStr;
    // BANNED_SERIAL (2), BANNED_IP (3), BAN (18) read a duration string before message + trouble link.
    if (type == 2 || type == 3 || type == 18) {
        (void)ReadUsString(reader, durationStr, 512);
    }

    std::string message;
    std::string troubleLink;
    if (type != 16) {  // INVALID_PASSWORD (16) skips message box strings on desktop.
        (void)ReadUsString(reader, message, 2048);
        (void)ReadUsString(reader, troubleLink, 512);
    }

    uint32_t durationSec = 0;
    if (!durationStr.empty()) {
        durationSec = static_cast<uint32_t>(std::strtoul(durationStr.c_str(), nullptr, 10));
    }

    std::string displayReason = message;
    if (displayReason.empty()) {
        switch (type) {
            case 0: displayReason = "Disconnected: Invalid nickname"; break;
            case 1: displayReason = "Disconnected from server"; break;
            case 5: displayReason = "Disconnected: Version mismatch"; break;
            case 6: displayReason = "Disconnected: Join flood"; break;
            case 11: displayReason = "Disconnected: Nick already in use"; break;
            case 12: displayReason = "Disconnected: Player element could not be created"; break;
            case 14: displayReason = "Disconnected: Serial verification failed"; break;
            case 15: displayReason = "Disconnected: Connection desync"; break;
            case 16: displayReason = "Disconnected: Invalid password"; break;
            case 20: displayReason = "Disconnected: Server shutdown or restarting"; break;
            default: displayReason = "Disconnected by server"; break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_extraStateMutex);
        g_lastDisconnect = DisconnectRecord{true, type, durationSec, displayReason};
    }
    JniBridge::PostGameLine(0, displayReason.c_str());
    return true;
}

// =========================================================================
// PACKET_ID_RETURN_SYNC (36)
// Desktop: CNetAPI::ProcessPacket case PACKET_ID_RETURN_SYNC (CNetAPI.cpp:161-221)
// =========================================================================
bool HandleReturnSync(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    bool inVehicle = false;
    if (!ReadFlag(reader, inVehicle)) return false;

    float x = 0.0f, y = 0.0f, z = 0.0f;
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    if (!Entity::ReadPosition(reader, x, y, z)) return false;
    if (inVehicle) {
        if (!Entity::ReadRotationDegrees(reader, rx, ry, rz)) return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_extraStateMutex);
        g_lastReturnSync = ReturnSyncRecord{true, inVehicle, x, y, z, rx, ry, rz};
    }
    return true;
}

// =========================================================================
// PACKET_ID_MAP_INFO (46)
// Desktop: CPacketHandler::Packet_MapInfo (CPacketHandler.cpp:2377-2715)
// Pinned to bitstream 0x0030 (SFunBugsStateSync reads 10 bits; Glitch_FastWeaponStrafe is >= 0x0032).
// =========================================================================
bool HandleMapInfo(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint8_t weather = 0, weatherBlendingTo = 0, blendedHour = 0xFF;
    if (!reader.Read(weather) || !reader.Read(weatherBlendingTo) || !reader.Read(blendedHour)) return false;

    bool hasSky = false;
    uint8_t skyTR = 0, skyTG = 0, skyTB = 0, skyBR = 0, skyBG = 0, skyBB = 0;
    if (!ReadFlag(reader, hasSky)) return false;
    if (hasSky) {
        if (!reader.Read(skyTR) || !reader.Read(skyTG) || !reader.Read(skyTB) ||
            !reader.Read(skyBR) || !reader.Read(skyBG) || !reader.Read(skyBB)) return false;
    }

    bool hasHeat = false;
    uint8_t heatInt = 0, heatShift = 0;
    uint16_t heatSpeedMin = 0, heatSpeedMax = 0, heatRenderX = 0, heatRenderY = 0;
    int16_t heatScanX = 0, heatScanY = 0;
    bool heatInside = false;
    if (!ReadFlag(reader, hasHeat)) return false;
    if (hasHeat) {
        if (!reader.Read(heatInt) || !reader.Read(heatShift) ||
            !ReadRangeBits<10, uint16_t>(reader, heatSpeedMin, 0, 1000) ||
            !ReadRangeBits<10, uint16_t>(reader, heatSpeedMax, 0, 1000) ||
            !ReadRangeBits<11, int16_t>(reader, heatScanX, -1000, 1000) ||
            !ReadRangeBits<11, int16_t>(reader, heatScanY, -1000, 1000) ||
            !ReadRangeBits<10, uint16_t>(reader, heatRenderX, 0, 1000) ||
            !ReadRangeBits<10, uint16_t>(reader, heatRenderY, 0, 1000) ||
            !ReadFlag(reader, heatInside)) return false;
    }

    uint8_t clockHour = 0, clockMin = 0;
    if (!reader.Read(clockHour) || !reader.Read(clockMin)) return false;

    uint32_t minuteDuration = 1000;
    if (!ReadCompressed(reader, 4, minuteDuration)) return false;

    uint8_t mapFlagsRaw = 0;
    if (!reader.ReadBits(&mapFlagsRaw, 3)) return false;
    const bool cloudsEnabled = (mapFlagsRaw & 0x04u) != 0;

    float gravity = 0.008f;
    if (!reader.Read(gravity)) return false;

    bool defaultGameSpeed = true;
    float gameSpeed = 1.0f;
    if (!ReadFlag(reader, defaultGameSpeed)) return false;
    if (!defaultGameSpeed && !reader.Read(gameSpeed)) return false;

    float waveHeight = 0.0f;
    if (!reader.Read(waveHeight)) return false;

    float seaLevel = 0.0f, nonSeaLevel = 0.0f, outsideLevel = 0.0f;
    bool hasNonSea = false, hasOutside = false;
    if (!reader.Read(seaLevel) || !ReadFlag(reader, hasNonSea)) return false;
    if (hasNonSea && !reader.Read(nonSeaLevel)) return false;
    if (!ReadFlag(reader, hasOutside)) return false;
    if (hasOutside && !reader.Read(outsideLevel)) return false;

    uint32_t fpsLimit32 = 36;
    if (!ReadCompressed(reader, 2, fpsLimit32)) return false;
    const uint16_t fpsLimit = static_cast<uint16_t>(fpsLimit32);

    std::array<bool, 50> garages{};
    for (size_t i = 0; i < garages.size(); ++i) {
        if (!ReadFlag(reader, garages[i])) return false;
    }

    // SFunBugsStateSync: 10 bits at bitstream 0x0030 (SyncStructures.h:2264-2285).
    uint8_t funBugsRaw[2] = {};
    if (!reader.ReadBits(funBugsRaw, 10)) return false;
    const uint16_t funBugs = static_cast<uint16_t>(funBugsRaw[0]) | (static_cast<uint16_t>(funBugsRaw[1] & 0x03u) << 8);

    // SWorldSpecialPropertiesStateSync: 20 bits (SyncStructures.h:2333-2351).
    uint8_t wsRaw[3] = {};
    if (!reader.ReadBits(wsRaw, 20)) return false;
    const uint32_t wsBits = static_cast<uint32_t>(wsRaw[0]) | (static_cast<uint32_t>(wsRaw[1]) << 8) |
                            (static_cast<uint32_t>(wsRaw[2] & 0x0Fu) << 16);

    float jetpackMaxHeight = 100.0f;
    if (!reader.Read(jetpackMaxHeight)) return false;

    bool overrideWaterColor = false;
    uint8_t wcR = 0, wcG = 0, wcB = 0, wcA = 0;
    if (!ReadFlag(reader, overrideWaterColor)) return false;
    if (overrideWaterColor) {
        if (!reader.Read(wcR) || !reader.Read(wcG) || !reader.Read(wcB) || !reader.Read(wcA)) return false;
    }

    bool interiorSounds = true;
    if (!ReadFlag(reader, interiorSounds)) return false;

    bool overrideRain = false;
    float rainLevel = 0.0f;
    if (!ReadFlag(reader, overrideRain)) return false;
    if (overrideRain && !reader.Read(rainLevel)) return false;

    bool overrideMoon = false;
    int32_t moonSize = 3;
    if (!ReadFlag(reader, overrideMoon)) return false;
    if (overrideMoon && !reader.Read(moonSize)) return false;

    bool overrideSunSize = false;
    float sunSize = 1.0f;
    if (!ReadFlag(reader, overrideSunSize)) return false;
    if (overrideSunSize && !reader.Read(sunSize)) return false;

    bool overrideSunColor = false;
    uint8_t scR = 0, scG = 0, scB = 0, soR = 0, soG = 0, soB = 0;
    if (!ReadFlag(reader, overrideSunColor)) return false;
    if (overrideSunColor) {
        if (!reader.Read(scR) || !reader.Read(scG) || !reader.Read(scB) ||
            !reader.Read(soR) || !reader.Read(soG) || !reader.Read(soB)) return false;
    }

    bool overrideWind = false;
    float windX = 0.0f, windY = 0.0f, windZ = 0.0f;
    if (!ReadFlag(reader, overrideWind)) return false;
    if (overrideWind && (!reader.Read(windX) || !reader.Read(windY) || !reader.Read(windZ))) return false;

    bool overrideFarClip = false;
    float farClip = 0.0f;
    if (!ReadFlag(reader, overrideFarClip)) return false;
    if (overrideFarClip && !reader.Read(farClip)) return false;

    bool overrideFog = false;
    float fogDist = 0.0f;
    if (!ReadFlag(reader, overrideFog)) return false;
    if (overrideFog && !reader.Read(fogDist)) return false;

    float aircraftMaxHeight = 800.0f;
    float aircraftMaxVelocity = 1.5f;
    if (!reader.Read(aircraftMaxHeight) || !reader.Read(aircraftMaxVelocity)) return false;

    ServerRpc::Enqueue([=]() {
        if (blendedHour != 0xFF) {
            const uint8_t startHour = (blendedHour == 0) ? 23 : static_cast<uint8_t>(blendedHour - 1);
            Script::RpcSetWeather(weather);
            Script::RpcSetWeatherBlended(weatherBlendingTo, startHour);
        } else {
            Script::RpcSetWeather(weather);
        }
        if (hasSky) Script::RpcSetSkyGradient(skyTR, skyTG, skyTB, skyBR, skyBG, skyBB);
        else Script::RpcResetSkyGradient();

        if (hasHeat) Script::RpcSetHeatHaze(heatInt, heatShift, heatSpeedMin, heatSpeedMax, heatScanX, heatScanY,
                                            heatRenderX, heatRenderY, heatInside);
        else Script::RpcResetHeatHaze();

        if (clockHour < 24 && clockMin < 60) Game::NoteScriptTime(clockHour, clockMin);
        Script::RpcSetMinuteDuration(minuteDuration);
        Script::RpcSetCloudsEnabled(cloudsEnabled);
        Script::RpcSetGravity(gravity);
        Script::RpcSetGameSpeed(gameSpeed);
        Script::RpcSetWaveHeight(waveHeight);

        Script::RpcResetWorldWaterLevel();
        if (hasNonSea) Script::RpcSetWorldWaterLevel(nonSeaLevel, true, false, false, false);
        if (hasOutside) Script::RpcSetWorldWaterLevel(outsideLevel, false, false, false, true);
        Script::RpcSetWorldWaterLevel(seaLevel, false, false, true, false);

        Script::RpcSetFpsLimit(fpsLimit);
        for (uint8_t i = 0; i < 50; ++i) Script::RpcSetGarageOpen(i, garages[i]);
        for (uint8_t g = 0; g < 10; ++g) Script::RpcSetGlitchEnabled(g, ((funBugs >> g) & 1u) != 0);
        for (int p = 0; p < 20; ++p) Script::RpcSetWorldSpecialProperty(p, ((wsBits >> p) & 1u) != 0);

        Script::RpcSetJetpackMaxHeight(jetpackMaxHeight);
        if (overrideWaterColor) Script::RpcSetWaterColor(wcR, wcG, wcB, wcA);
        Script::RpcSetInteriorSoundsEnabled(interiorSounds);
        if (overrideRain) Script::RpcSetRainLevel(rainLevel);
        if (overrideMoon) Script::RpcSetMoonSize(moonSize);
        if (overrideSunSize) Script::RpcSetSunSize(sunSize);
        if (overrideSunColor) Script::RpcSetSunColor(scR, scG, scB, soR, soG, soB);
        if (overrideWind) Script::RpcSetWindVelocity(windX, windY, windZ);
        if (overrideFarClip) Script::RpcSetFarClipDistance(farClip);
        if (overrideFog) Script::RpcSetFogDistance(fogDist);
        Script::RpcSetAircraftMaxHeight(aircraftMaxHeight);
        Script::RpcSetAircraftMaxVelocity(aircraftMaxVelocity);
    });
    return true;
}

// =========================================================================
// PACKET_ID_UPDATE_INFO (89)
// Desktop: CPacketHandler::Packet_UpdateInfo (CPacketHandler.cpp:5824-5832)
// =========================================================================
bool HandleUpdateInfo(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    std::string type, data;
    if (!ReadUsString(reader, type, 512) || !ReadUsString(reader, data, 4096)) return false;
    {
        std::lock_guard<std::mutex> lock(g_extraStateMutex);
        g_lastUpdateInfo = UpdateInfoRecord{true, type, data};
    }
    return true;
}

// =========================================================================
// PACKET_ID_DEBUG_EVENT (110)
// Desktop: CPacketHandler::Packet_DebugEvent (CPacketHandler.cpp:1643-1675, SDebugEvent.h:124-154)
// =========================================================================
bool HandleDebugEvent(const uint8_t* body, size_t length, size_t bits) {
    BitStreamReader reader(body, length, bits);
    uint8_t wireVersion = 0, side = 0, severity = 0, r = 255, g = 255, b = 255;
    uint32_t sequence = 0, timestamp = 0, line = 0, repeatCount = 0;
    if (!reader.Read(wireVersion) || wireVersion != 2 ||
        !reader.Read(sequence) || !reader.Read(timestamp) ||
        !reader.Read(side) || !reader.Read(severity) ||
        !reader.Read(line) || !reader.Read(repeatCount) ||
        !reader.Read(r) || !reader.Read(g) || !reader.Read(b)) {
        return false;
    }
    if (side > 1 || severity > 4 || repeatCount == 0) return false;

    std::string category, context, resource, file, message, correlationId;
    if (!ReadUsString(reader, category, 64) ||
        !ReadUsString(reader, context, 192) ||
        !ReadUsString(reader, resource, 128) ||
        !ReadUsString(reader, file, 512) ||
        !ReadUsString(reader, message, 2048) ||
        !ReadUsString(reader, correlationId, 96)) {
        return false;
    }

    auto sanitize = [](std::string& s) {
        for (char& ch : s) {
            const unsigned char u = static_cast<unsigned char>(ch);
            if (u < 0x20 && ch != '\n' && ch != '\t') ch = ' ';
        }
    };
    sanitize(message);
    sanitize(file);

    std::string legacyText = (!file.empty() && line != 0)
                                 ? (file + ":" + std::to_string(line) + ": " + message)
                                 : message;
    {
        std::lock_guard<std::mutex> lock(g_extraStateMutex);
        g_lastDebugEvent = DebugEventRecord{true, sequence, line, timestamp, legacyText};
    }
    JniBridge::PostGameLine(2, legacyText.c_str());
    return true;
}

PartialPacketProgress LastPartialPacketProgress() {
    std::lock_guard<std::mutex> lock(g_extraStateMutex);
    return g_lastPartialPacket;
}

DisconnectRecord LastDisconnectRecord() {
    std::lock_guard<std::mutex> lock(g_extraStateMutex);
    return g_lastDisconnect;
}

ReturnSyncRecord LastReturnSyncRecord() {
    std::lock_guard<std::mutex> lock(g_extraStateMutex);
    return g_lastReturnSync;
}

UpdateInfoRecord LastUpdateInfoRecord() {
    std::lock_guard<std::mutex> lock(g_extraStateMutex);
    return g_lastUpdateInfo;
}

DebugEventRecord LastDebugEventRecord() {
    std::lock_guard<std::mutex> lock(g_extraStateMutex);
    return g_lastDebugEvent;
}

void SetOutboundPacketSink(OutboundPacketSink sink) {
    std::lock_guard<std::mutex> lock(g_extraStateMutex);
    g_outboundSink = std::move(sink);
}

// =========================================================================
// Outbound gameplay packets (bitstream 0x0030)
// =========================================================================
bool SendExplosionSync(float x, float y, float z, uint8_t explosionType, uint32_t originId) {
    // CClientGame::SendExplosionSync (CClientGame.cpp:6280-6317):
    // [bit hasOrigin] (if true: [ElementID origin][bit isVehicle][opt bit isBlown])
    // [SPositionSync(false)] [SExplosionTypeSync (4 bits)]
    BitStreamWriter w;
    w.Write(MtaIds::Packet::Explosion);
    const bool hasOrigin = (originId != 0x1FFFFu && originId != 0xFFFFFFFFu);
    WriteFlag(w, hasOrigin);
    if (hasOrigin) {
        WriteElementId(w, originId);
        WriteFlag(w, false);  // non-vehicle origin
    }
    WritePosition(w, x, y, z);
    WriteBitsValue(w, explosionType & 0x0Fu, 4);
    return DispatchOutboundPacket(MtaIds::Packet::Explosion, w, true, true);
}

bool SendDetonateSatchels() {
    // CClientGame::SendDetonateSatchels (CClientGame.cpp:2040-2046): empty body.
    BitStreamWriter w;
    w.Write(MtaIds::Packet::DetonateSatchels);
    return DispatchOutboundPacket(MtaIds::Packet::DetonateSatchels, w, true, true);
}

bool SendDestroySatchels() {
    // CClientGame::SendDestroySatchels (CClientGame.cpp:6512-6517): empty body.
    BitStreamWriter w;
    w.Write(MtaIds::Packet::DestroySatchels);
    return DispatchOutboundPacket(MtaIds::Packet::DestroySatchels, w, true, true);
}

bool SendVehicleDamageSync(uint32_t vehicleId, const uint8_t doorStates[6], const uint8_t wheelStates[4],
                           const uint8_t panelStates[7], const uint8_t lightStates[4]) {
    // CDeathmatchVehicle::SyncDamageModel (CDeathmatchVehicle.cpp:118-129):
    // [ElementID vehicleId] [SVehicleDamageSync(true, true, true, true, true)]
    BitStreamWriter w;
    w.Write(MtaIds::Packet::VehicleDamageSync);
    WriteElementId(w, vehicleId);
    auto writePartStates = [&](const uint8_t* states, size_t count, unsigned bitsPerPart) {
        // SVehiclePartStateSync::Write (SyncStructures.h:1709-1727): interleaved per part.
        for (size_t i = 0; i < count; ++i) {
            if (states) {
                WriteFlag(w, true);
                WriteBitsValue(w, states[i], bitsPerPart);
            } else {
                WriteFlag(w, false);
            }
        }
    };
    writePartStates(doorStates, 6, 3);
    writePartStates(wheelStates, 4, 2);
    writePartStates(panelStates, 7, 2);
    writePartStates(lightStates, 4, 2);
    return DispatchOutboundPacket(MtaIds::Packet::VehicleDamageSync, w, true, true);
}

bool SendCustomData(uint32_t elementId, const std::string& key, const uint8_t* encodedValue, size_t encodedLen,
                    size_t encodedBits) {
    // CStaticFunctionDefinitions::SetElementData (CStaticFunctionDefinitions.cpp:1035-1047):
    // [ElementID] [WriteCompressed(ushort nameLen)] [name bytes] [CLuaArgument bitstream]
    if (key.size() > 65535) return false;
    BitStreamWriter w;
    w.Write(MtaIds::Packet::CustomData);
    WriteElementId(w, elementId);
    w.WriteCompressed(static_cast<uint32_t>(key.size()), 2);
    if (!key.empty()) w.WriteBytes(reinterpret_cast<const uint8_t*>(key.data()), key.size());
    if (encodedValue && encodedBits > 0) {
        w.WriteBits(encodedValue, encodedBits);
    } else if (encodedValue && encodedLen > 0) {
        w.WriteBytes(encodedValue, encodedLen);
    }
    return DispatchOutboundPacket(MtaIds::Packet::CustomData, w, true, true);
}

bool SendCameraSync(bool fixedMode, float x, float y, float z, float lookX, float lookY, float lookZ,
                    uint32_t targetId) {
    // CNetAPI::WriteCameraSync (CNetAPI.cpp:2036-2073):
    // [uint8 timeContext] [bit bFixed]
    //   if fixed: [SPositionSync(false) pos] [SPositionSync(false) lookAt]
    //   else:     [ElementID targetId]
    BitStreamWriter w;
    w.Write(MtaIds::Packet::CameraSync);
    w.Write(ServerRpc::CameraSyncTimeContext());
    WriteFlag(w, fixedMode);
    if (fixedMode) {
        WritePosition(w, x, y, z);
        WritePosition(w, lookX, lookY, lookZ);
    } else {
        WriteElementId(w, targetId);
    }
    return DispatchOutboundPacket(MtaIds::Packet::CameraSync, w, false, false);
}

bool SendObjectSync(uint32_t objectId, uint8_t timeContext, bool hasPos, float x, float y, float z,
                    bool hasRot, float rx, float ry, float rz, bool hasHealth, float health) {
    // CObjectSync::WriteObjectInformation (CObjectSync.cpp:215-275):
    // [ElementID] [uint8 timeContext] [5-bit flags] [opt SPositionSync(false)]
    // [opt SRotationRadiansSync(false)] [opt SObjectHealthSync (11 bits 0..1023.5)]
    uint8_t flags = 0;
    if (hasPos) flags |= 0x01u;
    if (hasRot) flags |= 0x02u;
    if (hasHealth) flags |= 0x04u;
    if (flags == 0) return false;

    BitStreamWriter w;
    w.Write(MtaIds::Packet::ObjectSync);
    WriteElementId(w, objectId);
    w.Write(timeContext);
    WriteBitsValue(w, flags, 5);
    if (hasPos) WritePosition(w, x, y, z);
    if (hasRot) {
        constexpr float kTwoPi = 6.283185307f;
        auto packRad = [](float rad) -> uint16_t {
            float m = std::fmod(rad, kTwoPi);
            if (m < 0.0f) m += kTwoPi;
            return static_cast<uint16_t>(std::lround(m * (65536.0f / kTwoPi)) & 0xFFFF);
        };
        w.Write(packRad(rx));
        w.Write(packRad(ry));
        w.Write(packRad(rz));
    }
    if (hasHealth) WriteFloatAsBits(w, 11, 0.0f, 1023.5f, health, true);
    return DispatchOutboundPacket(MtaIds::Packet::ObjectSync, w, false, false);
}

bool SendVehiclePushSync(uint32_t vehicleId) {
    // CClientGame::SendVehiclePushSync (CClientGame.cpp:4955-4962): [ElementID vehicleId].
    BitStreamWriter w;
    w.Write(MtaIds::Packet::VehiclePushSync);
    WriteElementId(w, vehicleId);
    return DispatchOutboundPacket(MtaIds::Packet::VehiclePushSync, w, false, false);
}

bool SendWorldSpecialProperty(const std::string& propertyName, bool enabled) {
    // CLuaWorldDefs::SetWorldSpecialPropertyEnabled (CLuaWorldDefs.cpp:2002-2015):
    // [WriteString(name)] [WriteBit(enabled)].
    if (propertyName.size() > 65535) return false;
    BitStreamWriter w;
    w.Write(MtaIds::Packet::PlayerWorldSpecialProperty);
    w.Write(static_cast<uint16_t>(propertyName.size()));
    if (!propertyName.empty()) {
        w.WriteBytes(reinterpret_cast<const uint8_t*>(propertyName.data()), propertyName.size());
    }
    WriteFlag(w, enabled);
    return DispatchOutboundPacket(MtaIds::Packet::PlayerWorldSpecialProperty, w, true, true);
}

void ResetSession() {
    {
        std::lock_guard<std::mutex> lock(g_latentMutex);
        g_latentRx = LatentReceiveState{};
    }
    {
        std::lock_guard<std::mutex> lock(g_textMutex);
        g_textItems.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_extraStateMutex);
        g_lastPartialPacket = PartialPacketProgress{};
        g_lastDisconnect = DisconnectRecord{};
        g_lastReturnSync = ReturnSyncRecord{};
        g_lastUpdateInfo = UpdateInfoRecord{};
        g_lastDebugEvent = DebugEventRecord{};
    }
}

}  // namespace WorldRPG::CorePackets
