// OBJECT (type 3) and WEAPON (type 18) bodies of ENTITY_ADD. Port of CPacketHandler.cpp Packet_EntityAdd,
// case CClientGame::OBJECT / WEAPON (mta_neon_source Client/mods/deathmatch/logic/CPacketHandler.cpp,
// lines 3139-3376), 0x0030 layout. Reference line in brackets.
//   SPositionSync(false): 2 x 24-bit fixed, Z float                          [3148]
//   SRotationRadiansSync(false): 3 x uint16                                  [3149]
//   usObjectID: ReadCompressed(ushort)                                        [3150]
//   SEntityAlphaSync: ReadCompressed(uchar)                                   [3150]
//   low LOD flag, then low LOD ElementID (17 bits)                            [3166-3167]
//   double sided, breakable, visible-in-all-dimensions flags                  [3197-3201]
//   moving flag, then CPositionRotationAnimation::FromBitStream               [3205]
//   uniform flag; uniform: unit flag, then float if not unit;
//     non-uniform: 3 floats                                                   [3221-3237]
//   frozen flag                                                               [3242]
//   SObjectHealthSync: 11 bits                                                [3246]
//   break flag, respawn flag                                                  [3249-3252]
// WEAPON tail (after the common object tail) [3258-3340]:
//   target type: 3 bits (0 vector, 1 entity, 2 fixed)                         [3259]
//   vector target: 3 raw floats                                               [3291]
//   entity target: ElementID (17 bits), then a sub-target that depends on the element type [3272-3282]
//   changed flag; if set: 12-bit damage, 3 raw floats                        [3297-3305]
//   13 config flags                                                           [3313-3325]
//   weapon state: 4 bits, ammo and clip ammo: uint16 each                     [3329-3331]
//   owner ElementID (17 bits)                                                 [3337]
// CPositionRotationAnimation::FromBitStream (Shared/animation/CPositionRotationAnimation.cpp, 111-176).

#include "EntityAddTypes.h"
#include "EntitySyncReaders.h"

namespace WorldRPG::Entity {

namespace {

constexpr uint8_t kElementWeapon = 18;  // CClientGame::WEAPON (enum index after WATER)
constexpr uint8_t kTargetVector = 0;    // eTargetType (CClientWeapon.h)
constexpr uint8_t kTargetEntity = 1;
constexpr uint8_t kTargetFixed = 2;

// CPositionRotationAnimation::FromBitStream, consumed without storing: no animation runs on this side yet.
bool SkipPositionRotationAnimation(Net::BitStreamReader& reader) {
    bool resumeMode = false;
    if (!ReadFlag(reader, resumeMode)) return false;
    uint32_t duration = 0;
    if (!ReadCompressed(reader, 4, duration)) return false;          // ulElaspedTime or ulDuration
    if (resumeMode && !ReadCompressed(reader, 4, duration)) return false;  // ulTimeLeft

    float fx = 0, fy = 0, fz = 0;
    float rx = 0, ry = 0, rz = 0;
    if (!ReadPosition(reader, fx, fy, fz) || !reader.Read(rx) || !reader.Read(ry) || !reader.Read(rz)) return false;

    bool deltaRotation = false;
    if (!ReadPosition(reader, fx, fy, fz) || !ReadFlag(reader, deltaRotation)) return false;
    if (!reader.Read(rx) || !reader.Read(ry) || !reader.Read(rz)) return false;

    // SizeType = unsigned short length, then the easing name bytes.
    uint16_t easingLength = 0;
    if (!reader.Read(easingLength)) return false;
    if (!SkipBytes(reader, easingLength)) return false;

    // Easing period, amplitude and overshoot: three raw doubles (8 bytes each).
    for (int i = 0; i < 3; ++i) {
        uint8_t raw[8] = {};
        if (!reader.ReadBytes(raw, sizeof(raw))) return false;
    }
    return true;
}

// Weapon-only tail. Returns false on a short read.
bool SkipWeaponTail(Net::BitStreamReader& reader, const EntityCommon& common) {
    uint8_t targetType = kTargetFixed;
    if (!reader.ReadBits(&targetType, 3)) ENTITY_BODY_FAIL();

    switch (targetType) {
        case kTargetVector: {
            float vx = 0, vy = 0, vz = 0;
            if (!reader.Read(vx) || !reader.Read(vy) || !reader.Read(vz)) ENTITY_BODY_FAIL();
            break;
        }
        case kTargetEntity: {
            uint32_t targetId = 0;
            if (!ReadElementId(reader, targetId)) ENTITY_BODY_FAIL();
            // The reference reads 8 bits for a ped target and 4 bits for a vehicle target, decided by the
            // element type. This layer has no element table, so the sub-target is not read here.
            DIAG("ENTITY_ADD weapon entity target not resolved id=%u target=%u bit=%zu (sub-target width unknown)",
                 common.id, targetId, reader.GetReadOffsetBits());
            break;
        }
        default:
            break;  // TARGET_TYPE_FIXED (and unused values): no data
    }

    bool changed = false;
    if (!ReadFlag(reader, changed)) ENTITY_BODY_FAIL();
    if (changed) {
        uint8_t damage[2] = {};  // 12 bits per SWeapon damage
        if (!reader.ReadBits(damage, 12)) ENTITY_BODY_FAIL();
        float accuracy = 0, targetRange = 0, weaponRange = 0;
        if (!reader.Read(accuracy) || !reader.Read(targetRange) || !reader.Read(weaponRange)) ENTITY_BODY_FAIL();
    }

    // SWeaponConfiguration: 4 flags, then 9 check flags.
    for (int i = 0; i < 13; ++i) {
        bool flag = false;
        if (!ReadFlag(reader, flag)) ENTITY_BODY_FAIL();
    }

    uint8_t weaponState = 0;
    if (!reader.ReadBits(&weaponState, 4)) ENTITY_BODY_FAIL();
    uint16_t ammo = 0, clipAmmo = 0;
    if (!reader.Read(ammo) || !reader.Read(clipAmmo)) ENTITY_BODY_FAIL();

    uint32_t ownerId = 0;
    if (!ReadElementId(reader, ownerId)) ENTITY_BODY_FAIL();
    return true;
}

} // namespace

bool ReadEntityBodyObject(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    const bool isWeapon = common.type == kElementWeapon;

    float x = 0, y = 0, z = 0;
    if (!ReadPosition(reader, x, y, z)) ENTITY_BODY_FAIL();

    float rotX = 0, rotY = 0, rotZ = 0;  // radians
    if (!ReadRotationRadians(reader, rotX, rotY, rotZ)) ENTITY_BODY_FAIL();

    uint32_t objectId = 0;
    if (!ReadCompressed(reader, 2, objectId)) ENTITY_BODY_FAIL();

    uint32_t alpha = 255;
    if (!ReadEntityAlpha(reader, alpha)) ENTITY_BODY_FAIL();

    bool lowLodFlag = false;
    if (!ReadFlag(reader, lowLodFlag)) ENTITY_BODY_FAIL();
    uint32_t lowLodId = 0;
    if (!ReadElementId(reader, lowLodId)) ENTITY_BODY_FAIL();

    bool doubleSided = false, breakable = false, visibleInAllDimensions = false;
    if (!ReadFlag(reader, doubleSided) || !ReadFlag(reader, breakable) || !ReadFlag(reader, visibleInAllDimensions)) ENTITY_BODY_FAIL();

    bool moving = false;
    if (!ReadFlag(reader, moving)) ENTITY_BODY_FAIL();
    if (moving && !SkipPositionRotationAnimation(reader)) ENTITY_BODY_FAIL();

    bool uniformScale = false;
    if (!ReadFlag(reader, uniformScale)) ENTITY_BODY_FAIL();
    if (uniformScale) {
        bool unitSize = false;
        if (!ReadFlag(reader, unitSize)) ENTITY_BODY_FAIL();
        if (!unitSize) {
            float scale = 0;
            if (!reader.Read(scale)) ENTITY_BODY_FAIL();
        }
    } else {
        float sx = 0, sy = 0, sz = 0;
        if (!reader.Read(sx) || !reader.Read(sy) || !reader.Read(sz)) ENTITY_BODY_FAIL();
    }

    bool frozen = false;
    if (!ReadFlag(reader, frozen)) ENTITY_BODY_FAIL();

    float health = 0;
    if (!ReadObjectHealth(reader, health)) ENTITY_BODY_FAIL();

    bool broken = false, respawnEnabled = false;
    if (!ReadFlag(reader, broken) || !ReadFlag(reader, respawnEnabled)) ENTITY_BODY_FAIL();

    if (isWeapon && !SkipWeaponTail(reader, common)) ENTITY_BODY_FAIL();

    visual.kind = common.type;
    visual.model = static_cast<int>(objectId);
    visual.x = x;
    visual.y = y;
    visual.z = z;
    visual.heading = rotZ;  // already radians

    if (isWeapon) {
        DIAG("weapon stored id=%u model=%u pos=%.2f,%.2f,%.2f", common.id, static_cast<unsigned>(objectId), x, y, z);
    } else {
        DIAG("object stored id=%u model=%u pos=%.2f,%.2f,%.2f health=%.1f", common.id, static_cast<unsigned>(objectId), x, y, z, health);
    }
    (void)rotX;
    (void)rotY;
    (void)alpha;
    (void)lowLodFlag;
    (void)lowLodId;
    (void)doubleSided;
    (void)breakable;
    (void)visibleInAllDimensions;
    (void)frozen;
    (void)broken;
    (void)respawnEnabled;
    return true;
}

} // namespace WorldRPG::Entity
