// ENTITY_ADD common header, custom data, name, Lua value reader and type dispatch.
// Port of CPacketHandler::Packet_EntityAdd (mta_neon_source) for the 0x0030 peer.

#include "EntityAddTypes.h"

namespace WorldRPG::Entity {
namespace {

constexpr uint32_t kMaxCustomDataItems = 4096;
constexpr uint32_t kMaxCustomDataNameLength = 128;  // MAX_CUSTOMDATA_NAME_LENGTH
constexpr uint32_t kMaxEntityNameLength = 4096;     // reference allows any ushort; capped for allocation safety

#define LUA_FAIL() do { DIAG("lua fail line=%d kind=%u bit=%zu", __LINE__, static_cast<unsigned>(kind), reader.GetReadOffsetBits()); return false; } while (0)
#define CB_FAIL() do { DIAG("customblock fail line=%d count=%u bit=%zu", __LINE__, static_cast<unsigned>(count), reader.GetReadOffsetBits()); return false; } while (0)

bool SkipLuaBody(Net::BitStreamReader& reader, uint8_t kind, int depth);

bool SkipLua(Net::BitStreamReader& reader, int depth) {
    uint8_t kind = 0;
    if (!reader.ReadBits(&kind, 4)) return false;
    return SkipLuaBody(reader, kind & 0x0Fu, depth);
}

bool SkipLuaBody(Net::BitStreamReader& reader, uint8_t kind, int depth) {
    if (depth > 12) return false;
    if (kind == 0) return true;
    if (kind == 1) {
        bool value = false;
        return ReadFlag(reader, value);
    }
    if (kind == 3) {
        bool wide = false;
        if (!ReadFlag(reader, wide)) return false;
        if (wide) {
            bool asDouble = false;
            if (!ReadFlag(reader, asDouble)) return false;
            if (asDouble) {
                uint8_t bytes[8] = {};
                return reader.ReadBits(bytes, 64);
            }
            float number = 0;
            return reader.Read(number);
        }
        uint32_t integer = 0;
        return ReadCompressed(reader, 4, integer);
    }
    if (kind == 4 || kind == 9) {
        uint32_t length = 0;
        const int width = kind == 4 ? 2 : 4;
        if (!ReadCompressed(reader, width, length) || length > 1024 * 1024) return false;
        return SkipBytes(reader, length);
    }
    if (kind == 5) {
        uint32_t count = 0;
        if (!ReadCompressed(reader, 4, count) || count > 4096) return false;
        for (uint32_t i = 0; i < count; ++i) if (!SkipLua(reader, depth + 1)) return false;
        return true;
    }
    if (kind == 2 || kind == 7) {
        uint32_t id = 0;
        return ReadElementId(reader, id);
    }
    if (kind == 10) {
        uint32_t ref = 0;
        return ReadCompressed(reader, 4, ref);
    }
    return false;
}

bool ReadCustomBlock(Net::BitStreamReader& reader, EntityCommon& common) {
    uint32_t count = 0;
    if (!ReadCompressed(reader, 2, count) || count > kMaxCustomDataItems) CB_FAIL();
    DIAG("customblock id=%u count=%u bit=%zu", common.id, static_cast<unsigned>(count), reader.GetReadOffsetBits());
    common.customData.clear();
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t nameLength = 0;
        if (!reader.Read(nameLength) || nameLength > kMaxCustomDataNameLength) CB_FAIL();
        std::vector<uint8_t> name(nameLength);
        if (nameLength && !reader.ReadBits(name.data(), nameLength * 8)) CB_FAIL();
        DIAG("customblock item %u/%u len=%u name=%.*s bit=%zu", i, static_cast<unsigned>(count), nameLength, static_cast<int>(name.size()), reinterpret_cast<const char*>(name.data()), reader.GetReadOffsetBits());
        Datum value;
        if (!ReadLuaValue(reader, value)) CB_FAIL();
        common.customData.emplace_back(std::string(name.begin(), name.end()), std::move(value));
    }
    return true;
}

} // namespace

bool ReadLuaValue(Net::BitStreamReader& reader, Datum& out) {
    uint8_t kind = 0;
    if (!reader.ReadBits(&kind, 4)) LUA_FAIL();
    kind &= 0x0Fu;
    DIAG("lua value kind=%u bit=%zu", static_cast<unsigned>(kind), reader.GetReadOffsetBits());
    if (kind == 0) {
        out.number = false;
        out.text.clear();
        return true;
    }
    if (kind == 1) {
        bool value = false;
        if (!ReadFlag(reader, value)) LUA_FAIL();
        out.number = true;
        out.value = value ? 1 : 0;
        return true;
    }
    if (kind == 3) {
        bool wide = false;
        if (!ReadFlag(reader, wide)) LUA_FAIL();
        if (wide) {
            bool asDouble = false;
            if (!ReadFlag(reader, asDouble)) LUA_FAIL();
            if (asDouble) {
                uint8_t bytes[8] = {};
                if (!reader.ReadBits(bytes, 64)) LUA_FAIL();
                double number = 0;
                std::memcpy(&number, bytes, 8);
                out.number = true;
                out.value = number;
                return true;
            }
            float number = 0;
            if (!reader.Read(number)) LUA_FAIL();
            out.number = true;
            out.value = number;
            return true;
        }
        uint32_t integer = 0;
        if (!ReadCompressed(reader, 4, integer)) LUA_FAIL();
        out.number = true;
        out.value = static_cast<int32_t>(integer);
        return true;
    }
    if (kind == 4) {
        uint32_t length = 0;
        if (!ReadCompressed(reader, 2, length) || length > 65535) LUA_FAIL();
        if (length > 512) {
            out.number = false;
            out.text.clear();
            return SkipBytes(reader, length);
        }
        std::vector<uint8_t> bytes(length);
        if (length && !reader.ReadBits(bytes.data(), length * 8)) LUA_FAIL();
        out.number = false;
        out.text.assign(bytes.begin(), bytes.end());
        return true;
    }
    // The type nibble is already consumed. Skipping must not read it again.
    out.number = false;
    out.text.clear();
    DIAG("lua skip-body kind=%u bit=%zu", static_cast<unsigned>(kind), reader.GetReadOffsetBits());
    return SkipLuaBody(reader, kind, 0);
}

bool ReadEntityCommon(Net::BitStreamReader& reader, EntityCommon& out) {
    // Reference order: ElementID, type (1 byte), parent ElementID, interior (1 byte),
    // dimension (ReadCompressed ushort), attached flag.
    uint32_t id = 0, parent = 0, dimension = 0;
    uint8_t type = 0, interior = 0;
    bool attached = false;
    if (!ReadElementId(reader, id) || !reader.Read(type) || !ReadElementId(reader, parent) ||
        !reader.Read(interior) || !ReadCompressed(reader, 2, dimension) || !ReadFlag(reader, attached)) {
        DIAG("ENTITY_ADD fail line=%d stage=header bit=%zu", __LINE__, reader.GetReadOffsetBits());
        return false;
    }
    out.id = id;
    out.type = type;
    out.parent = parent;
    out.interior = interior;
    out.dimension = dimension;
    out.attached = attached;
    DIAG("ENTITY_ADD header id=%u type=%u parent=%u interior=%u dim=%u attached=%d bit=%zu", id, static_cast<unsigned>(type), parent, static_cast<unsigned>(interior), dimension, attached ? 1 : 0, reader.GetReadOffsetBits());

    if (attached) {
        // ElementID, SPositionSync(false) (80 bits), SRotationDegreesSync(false) (3 x ushort).
        if (!ReadElementId(reader, out.attachedTo) || !ReadPosition(reader, out.attachedX, out.attachedY, out.attachedZ)) {
            DIAG("ENTITY_ADD fail line=%d stage=attached-target id=%u type=%u bit=%zu", __LINE__, id, static_cast<unsigned>(type), reader.GetReadOffsetBits());
            return false;
        }
        uint8_t raw[6] = {};
        if (!reader.ReadBits(raw, 48)) {
            DIAG("ENTITY_ADD fail line=%d stage=attached-rotation id=%u type=%u bit=%zu", __LINE__, id, static_cast<unsigned>(type), reader.GetReadOffsetBits());
            return false;
        }
        const float scale = 360.f / 65536.f;
        out.attachedRotX = static_cast<uint16_t>(raw[0] | (raw[1] << 8)) * scale;
        out.attachedRotY = static_cast<uint16_t>(raw[2] | (raw[3] << 8)) * scale;
        out.attachedRotZ = static_cast<uint16_t>(raw[4] | (raw[5] << 8)) * scale;
    }

    if (!ReadFlag(reader, out.collisions)) {
        DIAG("ENTITY_ADD fail line=%d stage=collisions id=%u type=%u bit=%zu", __LINE__, id, static_cast<unsigned>(type), reader.GetReadOffsetBits());
        return false;
    }
    // Call-propagation bit exists only for peers at 0x31+ (reference: Can(ExtendedWorldPositions)).
    out.callProp = true;
    if (kServerEntityBitStream >= 0x0031 && !ReadFlag(reader, out.callProp)) {
        DIAG("ENTITY_ADD fail line=%d stage=callprop id=%u type=%u bit=%zu", __LINE__, id, static_cast<unsigned>(type), reader.GetReadOffsetBits());
        return false;
    }

    if (!ReadCustomBlock(reader, out)) {
        DIAG("ENTITY_ADD fail line=%d stage=customdata id=%u type=%u bit=%zu", __LINE__, id, static_cast<unsigned>(type), reader.GetReadOffsetBits());
        return false;
    }

    if (!ReadSizedName(reader, out.name, kMaxEntityNameLength)) {
        DIAG("ENTITY_ADD fail line=%d stage=name id=%u type=%u bit=%zu", __LINE__, id, static_cast<unsigned>(type), reader.GetReadOffsetBits());
        return false;
    }

    // Sync time context: one byte after the name. Read unconditionally. Device evidence (2026-11-12):
    // the server writes it at the 0x0030 peer version too. Without it the DUMMY type name starts
    // 8 bits early and the MARKER fields misalign. The reference gates it on ExtendedWorldPositions,
    // so this is a deliberate deviation based on the bytes on the wire.
    {
        uint8_t syncTimeContext = 0;
        if (!reader.Read(syncTimeContext)) {
            DIAG("ENTITY_ADD fail line=%d stage=synctime id=%u type=%u bit=%zu", __LINE__, id, static_cast<unsigned>(type), reader.GetReadOffsetBits());
            return false;
        }
    }
    DIAG("ENTITY_ADD common ok id=%u type=%u name=%s custom=%zu bit=%zu", id, static_cast<unsigned>(type), out.name.c_str(), out.customData.size(), reader.GetReadOffsetBits());
    return true;
}

bool ReadEntityBody(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    switch (common.type) {
        case kTypeDummy:
            return ReadEntityBodyDummy(reader, common, visual);
        case kTypePlayer:
        case kTypeSpawnpointDeprecated:  // reference has no case: reads nothing
        case kTypeRemoteClient:          // reference has no case: reads nothing
        case kTypeConsole:               // reference has no case: reads nothing
            return true;
        case kTypeVehicle:
            return ReadEntityBodyVehicle(reader, common, visual);
        case kTypeObject:
        case 18:  // WEAPON: the reference routes it through the object case
            return ReadEntityBodyObject(reader, common, visual);
        case kTypeMarker:
            return ReadEntityBodyMarker(reader, common, visual);
        case kTypeBlip:
        case kTypePickup:
        case kTypeRadarArea:
        case kTypePathNode:
        case kTypeWorldMesh:  // reference case reads nothing
        case kTypeTeam:
        case kTypeColshape:
            return ReadEntityBodyOther(reader, common, visual);
        case kTypePed:
            return ReadEntityBodyPed(reader, common, visual);
        default:
            // Types 16+ (SCRIPTFILE, WATER, WEAPON, BUILDING, ...) are not sent for this layout.
            DIAG("ENTITY_ADD unhandled type id=%u type=%u bit=%zu", common.id, static_cast<unsigned>(common.type), reader.GetReadOffsetBits());
            ENTITY_BODY_FAIL();
    }
}

} // namespace WorldRPG::Entity
