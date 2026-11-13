#pragma once

// ENTITY_ADD (game packet 50) element parsing: shared types and bit helpers.
// Layout follows mta_neon_source Client/mods/deathmatch/logic/CPacketHandler.cpp
// Packet_EntityAdd, for the 0x0030 peer bitstream that NeonJoinProtocol pins.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "../../net/raknet/RakNetWrapper.h"

#ifdef __ANDROID__
#include <android/log.h>
#define ENTITY_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon", __VA_ARGS__)
#else
#define ENTITY_LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif

#define DIAG(...) ENTITY_LOGI("DIAG " __VA_ARGS__)

// Use inside a function that has `reader` and `common` in scope.
#define ENTITY_BODY_FAIL() do { DIAG("ENTITY_ADD fail line=%d id=%u type=%u bit=%zu", __LINE__, common.id, static_cast<unsigned>(common.type), reader.GetReadOffsetBits()); return false; } while (0)

namespace WorldRPG::Entity {

// Effective peer bitstream for entity/ped data. NeonJoinProtocol pins the peer to 0x0030,
// so the server writes the 0x0030 layout even though MOD_NAME reports 0x3D.
constexpr uint16_t kServerEntityBitStream = 0x0030;

// Element type ids (CClientGame.h enum). Only the values S1 touches are named.
enum ElementType : uint8_t {
    kTypeDummy = 0,
    kTypePlayer = 1,
    kTypeVehicle = 2,
    kTypeObject = 3,
    kTypeMarker = 4,
    kTypeBlip = 5,
    kTypePickup = 6,
    kTypeRadarArea = 7,
    kTypeSpawnpointDeprecated = 8,
    kTypeRemoteClient = 9,
    kTypeConsole = 10,
    kTypePathNode = 11,
    kTypeWorldMesh = 12,
    kTypeTeam = 13,
    kTypePed = 14,
    kTypeColshape = 15,
};

struct Visual {
    int kind = 0; // 4 marker, 5 blip, 7 area, 14 ped, 1 player
    int model = -1;
    float x = 0, y = 0, z = 0, w = 0, h = 0, size = 1;
    int icon = 0;
    int r = 255, g = 255, b = 255, a = 255;
    int markerType = 2;
    float heading = 0;
    // Vehicles (kind 2) only, from the ENTITY_ADD body: roll and pitch in degrees and SVehicleHealthSync.
    // health < 0 means the body did not carry one.
    float rotX = 0, rotY = 0;
    float health = -1.f;
    bool hosted = false;
    uint32_t attached = 0xFFFFFFFFu;
    int blipHandle = -1;
    bool blipLive = false;
};

struct Datum {
    bool number = false;
    double value = 0;
    std::string text;
};

// Output of the common ENTITY_ADD header (reference order), including custom data and name.
struct EntityCommon {
    uint32_t id = 0;
    uint8_t type = 0;
    uint32_t parent = 0;
    uint8_t interior = 0;
    uint32_t dimension = 0;
    bool attached = false;
    uint32_t attachedTo = 0;
    float attachedX = 0, attachedY = 0, attachedZ = 0;
    float attachedRotX = 0, attachedRotY = 0, attachedRotZ = 0; // degrees
    bool collisions = false;
    bool callProp = true;     // default true, as in the reference
    std::string name;
    std::vector<std::pair<std::string, Datum>> customData;
};

// ---- Bit helpers (widths verified against SyncStructures.h / bitstream.h) ----

inline bool ReadFlag(Net::BitStreamReader& reader, bool& out) {
    uint8_t bit = 0;
    if (!reader.ReadBits(&bit, 1)) return false;
    out = (bit & 1u) != 0;
    return true;
}

// ElementID: 17 bits, all-ones means INVALID_ELEMENT_ID.
inline bool ReadElementId(Net::BitStreamReader& reader, uint32_t& out) {
    uint8_t bytes[4] = {};
    if (!reader.ReadBits(bytes, 17)) return false;
    uint32_t value = 0;
    std::memcpy(&value, bytes, 4);
    out = value == ((1u << 17) - 1) ? 0xFFFFFFFFu : value;
    return true;
}

// NetBitStream ReadCompressed for an integer of `bytes` width (1-4).
inline bool ReadCompressed(Net::BitStreamReader& reader, int bytes, uint32_t& out) {
    out = 0;
    if (bytes < 1 || bytes > 4) return false;
    for (int i = bytes - 1; i > 0; --i) {
        bool empty = false;
        if (!ReadFlag(reader, empty)) return false;
        if (!empty) {
            uint8_t buf[4] = {};
            if (!reader.ReadBits(buf, static_cast<size_t>((i + 1) * 8))) return false;
            for (int b = 0; b <= i; ++b) out |= static_cast<uint32_t>(buf[b]) << (8 * b);
            return true;
        }
    }
    bool highNibbleEmpty = false;
    if (!ReadFlag(reader, highNibbleEmpty)) return false;
    if (!highNibbleEmpty) {
        uint8_t low = 0;
        if (!reader.Read(low)) return false;
        out = low;
        return true;
    }
    uint8_t nibble = 0;
    if (!reader.ReadBits(&nibble, 4)) return false;
    out = nibble & 0x0Fu;
    return true;
}

// SPositionComponentSync at 0x0030: SFloatSync<14, 10> = 24 signed bits, value / 1024.
inline bool ReadFixed24(Net::BitStreamReader& reader, float& out) {
    uint8_t bytes[4] = {};
    if (!reader.ReadBits(bytes, 24)) return false;
    int32_t raw = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16);
    if (raw & 0x800000) raw |= ~0xFFFFFF;
    out = static_cast<float>(raw) / 1024.f;
    return true;
}

// SPositionSync(false): two fixed24 components, then a float Z.
inline bool ReadPosition(Net::BitStreamReader& reader, float& x, float& y, float& z) {
    return ReadFixed24(reader, x) && ReadFixed24(reader, y) && reader.Read(z);
}

// SPosition2DSync(false): two fixed24 components.
inline bool ReadPosition2(Net::BitStreamReader& reader, float& x, float& y) {
    return ReadFixed24(reader, x) && ReadFixed24(reader, y);
}

inline bool SkipBytes(Net::BitStreamReader& reader, uint32_t count) {
    if (count > 1024 * 1024) return false;
    std::vector<uint8_t> dump(count);
    return count == 0 || reader.ReadBits(dump.data(), count * 8);
}

// Reads a ReadCompressed(ushort) length followed by that many raw bytes.
inline bool ReadSizedName(Net::BitStreamReader& reader, std::string& out, uint32_t maxLength) {
    uint32_t length = 0;
    out.clear();
    if (!ReadCompressed(reader, 2, length) || length > maxLength) return false;
    std::vector<uint8_t> bytes(length);
    if (length && !reader.ReadBits(bytes.data(), length * 8)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

// CLuaArgument::ReadFromBitStream equivalent. Defined in EntityAddCore.cpp.
bool ReadLuaValue(Net::BitStreamReader& reader, Datum& out);

// ---- Element parsing ----

// Common header, custom data and name. Defined in EntityAddCore.cpp.
bool ReadEntityCommon(Net::BitStreamReader& reader, EntityCommon& out);

// Dispatch on common.type. Defined in EntityAddCore.cpp.
// Returns false on a parse failure (DIAG already logged); the caller stops the packet.
bool ReadEntityBody(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);

// Per-type bodies.
bool ReadEntityBodyPed(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);      // EntityAddPed.cpp (stub)
bool ReadEntityBodyVehicle(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);  // EntityAddVehicle.cpp (stub)
bool ReadEntityBodyObject(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);   // EntityAddObject.cpp (stub)
bool ReadEntityBodyDummy(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);   // EntityAddDummy.cpp
bool ReadEntityBodyMarker(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);  // EntityAddMarker.cpp
bool ReadEntityBodyOther(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);   // EntityAddOther.cpp: BLIP, PICKUP, RADAR_AREA, COLSHAPE, PATH_NODE, TEAM, ...

} // namespace WorldRPG::Entity
