// Non-ped, non-vehicle, non-object, non-marker, non-dummy ENTITY_ADD bodies.
// Each case is a port of CClientGame cases in CPacketHandler::Packet_EntityAdd:
//   BLIP ~3849, RADAR_AREA ~3890, PATH_NODE ~3922, WORLD_MESH ~3949 (reads nothing),
//   TEAM ~3952, PICKUP ~3358, COLSHAPE ~4222.

#include "EntityAddTypes.h"

namespace WorldRPG::Entity {

namespace {

constexpr uint32_t kMaxTeamNameLength = 255;   // MAX_TEAM_NAME_LENGTH
constexpr uint32_t kMaxBodyNameLength = 4096;  // only a sanity cap for allocation

// CClientPickup::ePickupType order (CClientPickup.h).
constexpr uint8_t kPickupHealth = 0;
constexpr uint8_t kPickupArmor = 1;
constexpr uint8_t kPickupWeapon = 2;

// eColShapeType order (CClientColShape.h).
constexpr uint8_t kColshapeCircle = 0;
constexpr uint8_t kColshapeCuboid = 1;
constexpr uint8_t kColshapeSphere = 2;
constexpr uint8_t kColshapeRectangle = 3;
constexpr uint8_t kColshapePolygon = 4;
constexpr uint8_t kColshapeTube = 5;

// Blip: SPositionSync, ReadCompressed short ordering, SIntegerSync<ushort,14> visible distance,
// SIntegerSync<uchar,6> icon, SIntegerSync<uchar,5> size, SColorSync (32 bits).
bool ReadBlip(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    float x = 0, y = 0, z = 0;
    if (!ReadPosition(reader, x, y, z)) ENTITY_BODY_FAIL();
    visual.x = x;
    visual.y = y;
    visual.z = z;

    uint32_t ordering = 0;  // ReadCompressed(short): same 2-byte scheme as the other ushort/short fields
    if (!ReadCompressed(reader, 2, ordering)) ENTITY_BODY_FAIL();

    uint8_t rawDistance[2] = {};
    if (!reader.ReadBits(rawDistance, 14)) ENTITY_BODY_FAIL();
    uint32_t visibleDistance = rawDistance[0] | ((rawDistance[1] & 0x3Fu) << 8);

    uint8_t icon = 0, size = 0;
    if (!reader.ReadBits(&icon, 6) || !reader.ReadBits(&size, 5)) ENTITY_BODY_FAIL();
    visual.icon = icon & 0x3Fu;
    visual.size = static_cast<float>(size & 0x1Fu);

    uint8_t color[4] = {};
    if (!reader.ReadBits(color, 32)) ENTITY_BODY_FAIL();
    visual.r = color[0];
    visual.g = color[1];
    visual.b = color[2];
    visual.a = color[3];
    if (common.attached) visual.attached = common.parent;  // as before this stage
    (void)ordering;
    (void)visibleDistance;
    return true;
}

// Pickup: SPositionSync, ReadCompressed ushort model, visible bit, SPickupTypeSync (3 bits),
// then a per-type payload.
bool ReadPickup(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    float x = 0, y = 0, z = 0;
    if (!ReadPosition(reader, x, y, z)) ENTITY_BODY_FAIL();
    visual.x = x;
    visual.y = y;
    visual.z = z;

    uint32_t model = 0;
    if (!ReadCompressed(reader, 2, model)) ENTITY_BODY_FAIL();
    visual.model = static_cast<int>(model);
    bool visible = false;
    if (!ReadFlag(reader, visible)) ENTITY_BODY_FAIL();

    uint8_t pickupType = 0;
    if (!reader.ReadBits(&pickupType, 3)) ENTITY_BODY_FAIL();
    pickupType &= 7u;

    switch (pickupType) {
        case kPickupArmor:
        case kPickupHealth: {
            // SPlayerArmorSync / SPlayerHealthSync: SFloatAsBitsSync<8>, 8 bits each.
            uint8_t amount = 0;
            if (!reader.Read(amount)) ENTITY_BODY_FAIL();
            break;
        }
        case kPickupWeapon: {
            // SWeaponTypeSync (6 bits), then SWeaponAmmoSync(type, true, false): total ammo only.
            uint8_t weaponType = 0;
            if (!reader.ReadBits(&weaponType, 6)) ENTITY_BODY_FAIL();
            uint32_t ammo = 0;
            if (!ReadCompressed(reader, 2, ammo)) ENTITY_BODY_FAIL();
            break;
        }
        default:  // CUSTOM and unknown: reference reads nothing.
            break;
    }
    (void)visible;
    return true;
}

// RadarArea: SPosition2DSync(false) position and size, SColor (4 bytes), flashing bit.
bool ReadRadarArea(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    float px = 0, py = 0, sx = 0, sy = 0;
    if (!ReadPosition2(reader, px, py) || !ReadPosition2(reader, sx, sy)) ENTITY_BODY_FAIL();
    visual.x = px;
    visual.y = py;
    visual.w = sx;
    visual.h = sy;
    uint8_t color[4] = {};
    if (!reader.ReadBits(color, 32)) ENTITY_BODY_FAIL();
    visual.r = color[0];
    visual.g = color[1];
    visual.b = color[2];
    visual.a = color[3];
    bool flashing = false;
    if (!ReadFlag(reader, flashing)) ENTITY_BODY_FAIL();
    (void)common;
    return true;
}

// PathNode: 6 floats (position, rotation), int time, uchar style, ElementID next node.
bool ReadPathNode(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    float px = 0, py = 0, pz = 0, rx = 0, ry = 0, rz = 0;
    if (!reader.Read(px) || !reader.Read(py) || !reader.Read(pz) || !reader.Read(rx) || !reader.Read(ry) || !reader.Read(rz))
        ENTITY_BODY_FAIL();
    visual.x = px;
    visual.y = py;
    visual.z = pz;
    int32_t time = 0;
    uint8_t style = 0;
    uint32_t nextNode = 0;
    if (!reader.Read(time) || !reader.Read(style) || !ReadElementId(reader, nextNode)) ENTITY_BODY_FAIL();
    (void)common;
    return true;
}

// Team: a second name (ushort length + chars; the common header already read one), RGB bytes,
// friendly-fire bit, uint player count, then one ElementID per player.
bool ReadTeam(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    std::string teamName;
    if (!ReadSizedName(reader, teamName, kMaxTeamNameLength)) ENTITY_BODY_FAIL();
    uint8_t red = 0, green = 0, blue = 0;
    if (!reader.Read(red) || !reader.Read(green) || !reader.Read(blue)) ENTITY_BODY_FAIL();
    bool friendlyFire = false;
    if (!ReadFlag(reader, friendlyFire)) ENTITY_BODY_FAIL();
    uint32_t playerCount = 0;
    if (!reader.Read(playerCount)) ENTITY_BODY_FAIL();
    for (uint32_t i = 0; i < playerCount; ++i) {
        uint32_t playerId = 0;
        if (!ReadElementId(reader, playerId)) ENTITY_BODY_FAIL();
    }
    visual.r = red;
    visual.g = green;
    visual.b = blue;
    (void)common;
    return true;
}

// Colshape: SColshapeTypeSync (3 bits), SPositionSync(false), enabled bit, auto-call bit, then a
// shape-specific payload.
bool ReadColshape(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    uint8_t shape = 0;
    if (!reader.ReadBits(&shape, 3)) ENTITY_BODY_FAIL();
    shape &= 7u;
    float x = 0, y = 0, z = 0;
    if (!ReadPosition(reader, x, y, z)) ENTITY_BODY_FAIL();
    visual.x = x;
    visual.y = y;
    visual.z = z;
    bool enabled = false, autoCall = false;
    if (!ReadFlag(reader, enabled) || !ReadFlag(reader, autoCall)) ENTITY_BODY_FAIL();

    switch (shape) {
        case kColshapeCircle:
        case kColshapeSphere: {
            float radius = 0;
            if (!reader.Read(radius)) ENTITY_BODY_FAIL();
            break;
        }
        case kColshapeCuboid: {
            float sx = 0, sy = 0, sz = 0;
            if (!ReadPosition(reader, sx, sy, sz)) ENTITY_BODY_FAIL();  // SPositionSync(false) size
            break;
        }
        case kColshapeRectangle: {
            float sx = 0, sy = 0;
            if (!ReadPosition2(reader, sx, sy)) ENTITY_BODY_FAIL();  // SPosition2DSync(false) size
            break;
        }
        case kColshapeTube: {
            float radius = 0, height = 0;
            if (!reader.Read(radius) || !reader.Read(height)) ENTITY_BODY_FAIL();
            break;
        }
        case kColshapePolygon: {
            uint32_t points = 0;
            if (!ReadCompressed(reader, 4, points)) ENTITY_BODY_FAIL();
            for (uint32_t i = 0; i < points; ++i) {
                float vx = 0, vy = 0;
                if (!ReadPosition2(reader, vx, vy)) ENTITY_BODY_FAIL();
            }
            float floorHeight = 0, ceilHeight = 0;
            if (!reader.Read(floorHeight) || !reader.Read(ceilHeight)) ENTITY_BODY_FAIL();
            break;
        }
        default:
            DIAG("ENTITY_ADD colshape unknown shape=%u id=%u bit=%zu", static_cast<unsigned>(shape), common.id, reader.GetReadOffsetBits());
            ENTITY_BODY_FAIL();
    }
    return true;
}

} // namespace

bool ReadEntityBodyOther(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    switch (common.type) {
        case kTypeBlip: return ReadBlip(reader, common, visual);
        case kTypePickup: return ReadPickup(reader, common, visual);
        case kTypeRadarArea: return ReadRadarArea(reader, common, visual);
        case kTypePathNode: return ReadPathNode(reader, common, visual);
        case kTypeWorldMesh: return true;  // reference case has no reads
        case kTypeTeam: return ReadTeam(reader, common, visual);
        case kTypeColshape: return ReadColshape(reader, common, visual);
        default:
            DIAG("ENTITY_ADD other-type dispatch error type=%u id=%u", static_cast<unsigned>(common.type), common.id);
            ENTITY_BODY_FAIL();
    }
}

} // namespace WorldRPG::Entity
