// VEHICLE (type 2) body of ENTITY_ADD. Port of CPacketHandler.cpp Packet_EntityAdd, case CClientGame::VEHICLE
// (mta_neon_source Client/mods/deathmatch/logic/CPacketHandler.cpp, lines 3428-3773), 0x0030 layout.
// Read order and widths (reference line in brackets):
//   SPositionSync(false): 2 x 24-bit fixed, Z float                      [3431]
//   SRotationDegreesSync(false): 3 x uint16                              [3435]
//   usModel: uint16 plain                                                 [3440]
//   SVehicleHealthSync: 12 bits                                           [3456]
//   blow state: 2 bits                                                    [3466]
//   colour count: 2 bits, then (count + 1) x 3 bytes                      [3485-3492]
//   SPaintjobSync: 2 bits                                                 [3500]
//   SVehicleDamageSync(deltaSync=false): doors 6x3, wheels 4x2,
//     panels 7x2, lights 4x2 bits                                         [3505]
//   ucVariant, ucVariant2: uint8 each                                     [3512, 3519]
//   turret (HasTurret): 2 x int16                                         [3560]
//   adjustable property (HasAdjustableProperty): ReadCompressed(ushort)   [3568]
//   door open ratios (HasDoors): 6 x SDoorOpenRatioSync                   [3578]
//   upgrades: uint8 count, then uint8 each                                [3586, 3594]
//   reg plate: 8 raw bytes                                                [3612]
//   SOverrideLightsSync: 2 bits                                           [3617]
//   12 flag bits: landing gear, sirens, petrol, engine, locked,
//     undamageable, damage proof, frozen, derailed, derailable,
//     train direction, taxi light                                         [3621-3632]
//   SEntityAlphaSync: ReadCompressed(uchar)                               [3661]
//   headlight colour: flag, then 3 bytes                                  [3666-3670]
//   handling: flag, then SVehicleHandlingSync                             [3675-3684]
//   sirens: flag, uint8 count, uint8 type, count x SVehicleSirenSync      [3739-3749]
// SVehicleHandlingSync reads its suspension fields unconditionally; the reference only gates
// their application, so no car-class check is needed for the read.

#include "EntityAddTypes.h"
#include "EntitySyncReaders.h"
#include "EntityVehicleModels.h"

namespace WorldRPG::Entity {

namespace {

constexpr unsigned kMaxDoors = 6;    // CDamageManager.h MAX_DOORS
constexpr unsigned kMaxWheels = 4;   // MAX_WHEELS
constexpr unsigned kMaxPanels = 7;   // MAX_PANELS
constexpr unsigned kMaxLights = 4;   // MAX_LIGHTS
constexpr unsigned kDoorOpenRatioBits = 10;  // SDoorOpenRatioSync uncompressed SFloatAsBitsSync<10>

// SDoorOpenRatioSync: flag uncompressed; if clear, flag nonzero (value 0 or 1); else 10-bit float.
bool SkipDoorOpenRatio(Net::BitStreamReader& reader) {
    bool uncompressed = false;
    if (!ReadFlag(reader, uncompressed)) return false;
    if (!uncompressed) {
        bool nonZero = false;
        return ReadFlag(reader, nonZero);
    }
    float ratio = 0.f;
    return ReadFloatAsBits(reader, kDoorOpenRatioBits, 0.f, 1.f, ratio);
}

// SVehicleHandlingSync::Read, every field in order.
bool SkipHandling(Net::BitStreamReader& reader) {
    // 3 floats mass/turnMass/drag, centre of mass (3 floats), percent submerged (uchar)
    for (int i = 0; i < 3; ++i) { float f = 0; if (!reader.Read(f)) return false; }
    for (int i = 0; i < 3; ++i) { float f = 0; if (!reader.Read(f)) return false; }
    uint8_t u8 = 0;
    if (!reader.Read(u8)) return false;                 // ucPercentSubmerged
    float f = 0;
    if (!reader.Read(f)) return false;                  // fTractionMultiplier
    if (!reader.Read(u8) || !reader.Read(u8) || !reader.Read(u8)) return false;  // drive type, engine type, gears
    // fEngineAcceleration, fEngineInertia, fMaxVelocity, fBrakeDeceleration, fBrakeBias
    for (int i = 0; i < 5; ++i) { if (!reader.Read(f)) return false; }
    bool abs = false;
    if (!ReadFlag(reader, abs)) return false;           // bABS (1 bit)
    // fSteeringLock, fTractionLoss, fTractionBias
    for (int i = 0; i < 3; ++i) { if (!reader.Read(f)) return false; }
    // suspension: force, damping, high speed damping, upper, lower, front/rear bias, anti-dive
    for (int i = 0; i < 7; ++i) { if (!reader.Read(f)) return false; }
    if (!reader.Read(f)) return false;                  // fCollisionDamageMultiplier
    uint32_t u32 = 0;
    if (!reader.Read(u32) || !reader.Read(u32)) return false;  // uiModelFlags, uiHandlingFlags
    if (!reader.Read(f)) return false;                  // fSeatOffsetDistance
    return reader.Read(u8);                             // ucAnimGroup
}

} // namespace

bool ReadEntityBodyVehicle(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    float x = 0, y = 0, z = 0;
    if (!ReadPosition(reader, x, y, z)) ENTITY_BODY_FAIL();

    float rotX = 0, rotY = 0, rotZ = 0;  // degrees
    if (!ReadRotationDegrees(reader, rotX, rotY, rotZ)) ENTITY_BODY_FAIL();

    uint16_t model = 0;
    if (!reader.Read(model)) ENTITY_BODY_FAIL();
    if (!VehicleModels::IsStandardModel(model)) {
        DIAG("ENTITY_ADD vehicle model outside 400-611 id=%u model=%u (no parent lookup on this side)", common.id, static_cast<unsigned>(model));
    }

    float health = 0;
    if (!ReadVehicleHealth(reader, health)) ENTITY_BODY_FAIL();

    uint8_t blowState = 0;  // 0 intact, 1 awaiting explosion sync, 2 blown
    if (!reader.ReadBits(&blowState, 2)) ENTITY_BODY_FAIL();

    uint8_t extraColours = 0;  // ucNumColors: (n + 1) colours
    if (!reader.ReadBits(&extraColours, 2)) ENTITY_BODY_FAIL();
    for (unsigned i = 0; i <= extraColours; ++i) {
        uint8_t r = 0, g = 0, b = 0;
        if (!reader.Read(r) || !reader.Read(g) || !reader.Read(b)) ENTITY_BODY_FAIL();
    }

    uint8_t paintjob = 0;  // SPaintjobSync
    if (!reader.ReadBits(&paintjob, 2)) ENTITY_BODY_FAIL();

    // SVehicleDamageSync(true, true, true, true, deltaSync=false)
    if (!SkipPartStates(reader, kMaxDoors, 3)) ENTITY_BODY_FAIL();
    if (!SkipPartStates(reader, kMaxWheels, 2)) ENTITY_BODY_FAIL();
    if (!SkipPartStates(reader, kMaxPanels, 2)) ENTITY_BODY_FAIL();
    if (!SkipPartStates(reader, kMaxLights, 2)) ENTITY_BODY_FAIL();

    uint8_t variant = 0, variant2 = 0;
    if (!reader.Read(variant) || !reader.Read(variant2)) ENTITY_BODY_FAIL();

    if (VehicleModels::HasTurret(model)) {
        uint16_t turretX = 0, turretY = 0;  // SVehicleTurretSync: two shorts
        if (!reader.Read(turretX) || !reader.Read(turretY)) ENTITY_BODY_FAIL();
    }

    if (VehicleModels::HasAdjustableProperty(model)) {
        uint32_t adjustable = 0;
        if (!ReadCompressed(reader, 2, adjustable)) ENTITY_BODY_FAIL();
    }

    if (VehicleModels::HasDoors(model)) {
        for (unsigned door = 0; door < 6; ++door) {
            if (!SkipDoorOpenRatio(reader)) ENTITY_BODY_FAIL();
        }
    }

    uint8_t upgradeCount = 0;
    if (!reader.Read(upgradeCount)) ENTITY_BODY_FAIL();
    for (unsigned i = 0; i < upgradeCount; ++i) {
        uint8_t upgrade = 0;
        if (!reader.Read(upgrade)) ENTITY_BODY_FAIL();
    }

    uint8_t regPlate[8] = {};  // char szRegPlate[9], 8 raw bytes
    if (!reader.ReadBits(regPlate, 64)) ENTITY_BODY_FAIL();

    uint8_t overrideLights = 0;  // SOverrideLightsSync
    if (!reader.ReadBits(&overrideLights, 2)) ENTITY_BODY_FAIL();

    // Flag bits, reference order. Landing gear, sirens, petrol, engine, locked, undamageable,
    // damage proof, frozen, derailed, derailable, train direction, taxi light.
    bool flags[12] = {};
    for (bool& flag : flags) {
        if (!ReadFlag(reader, flag)) ENTITY_BODY_FAIL();
    }

    uint32_t alpha = 255;  // SEntityAlphaSync
    if (!ReadEntityAlpha(reader, alpha)) ENTITY_BODY_FAIL();

    bool headlightSet = false;  // SColorRGBA headlight, default white
    if (!ReadFlag(reader, headlightSet)) ENTITY_BODY_FAIL();
    if (headlightSet) {
        uint8_t r = 0, g = 0, b = 0;
        if (!reader.Read(r) || !reader.Read(g) || !reader.Read(b)) ENTITY_BODY_FAIL();
    }

    bool hasHandling = false;
    if (!ReadFlag(reader, hasHandling)) ENTITY_BODY_FAIL();
    if (hasHandling && !SkipHandling(reader)) ENTITY_BODY_FAIL();

    bool hasSirenSync = false;
    if (!ReadFlag(reader, hasSirenSync)) ENTITY_BODY_FAIL();
    if (hasSirenSync) {
        uint8_t sirenCount = 0, sirenType = 0;
        if (!reader.Read(sirenCount) || !reader.Read(sirenType)) ENTITY_BODY_FAIL();
        for (unsigned i = 0; i < sirenCount; ++i) {
            // SVehicleSirenSync: the override flag gates the rest. The reference ignores a false
            // override (it only consumes the flag), so this does the same.
            bool overrideSirens = false;
            if (!ReadFlag(reader, overrideSirens)) ENTITY_BODY_FAIL();
            if (!overrideSirens) continue;
            uint8_t sirenId = 0;
            float px = 0, py = 0, pz = 0;
            uint8_t colA = 0, colR = 0, colG = 0, colB = 0;
            uint32_t minAlpha = 0;
            if (!reader.Read(sirenId) || !reader.Read(px) || !reader.Read(py) || !reader.Read(pz)) ENTITY_BODY_FAIL();
            if (!reader.Read(colA) || !reader.Read(colR) || !reader.Read(colG) || !reader.Read(colB)) ENTITY_BODY_FAIL();
            if (!reader.Read(minAlpha)) ENTITY_BODY_FAIL();
            bool b360 = false, losCheck = false, randomiser = false, silent = false;
            if (!ReadFlag(reader, b360) || !ReadFlag(reader, losCheck) || !ReadFlag(reader, randomiser) || !ReadFlag(reader, silent)) {
                ENTITY_BODY_FAIL();
            }
        }
    }

    // Collision flag is written by the common header (bCollisonsEnabled), not here.
    constexpr float kPi = 3.14159265358979f;
    visual.kind = 2;
    visual.model = model;
    visual.x = x;
    visual.y = y;
    visual.z = z;
    visual.heading = rotZ * (kPi / 180.f);
    // Desktop CPacketHandler::Packet_EntityAdd, vehicle case: SetRotationDegrees(rotation) and SetHealth(health).
    // Kept so the body can be seeded with them (GameEngine.cpp ApplyCarSeed).
    visual.rotX = rotX;
    visual.rotY = rotY;
    visual.health = health;

    DIAG("vehicle stored id=%u model=%u pos=%.2f,%.2f,%.2f health=%.1f blow=%u paint=%u rot=%.1f",
         common.id, static_cast<unsigned>(model), x, y, z, health, static_cast<unsigned>(blowState),
         static_cast<unsigned>(paintjob), rotZ);
    (void)variant;
    (void)variant2;
    (void)overrideLights;
    (void)alpha;
    return true;
}

} // namespace WorldRPG::Entity
