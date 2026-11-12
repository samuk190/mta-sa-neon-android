#pragma once

// Decoders for PLAYER_VEHICLE_PURESYNC (game packet 33) and UNOCCUPIED_VEHICLE_SYNC (game packet 56).
// Read order mirrors mta_neon_source:
//   Client/mods/deathmatch/logic/CNetAPI.cpp  ReadVehiclePuresync (1306-1610), ReadFullKeysync (1882),
//                                             ReadFullVehicleSpecific (1965), ReadVehiclePartsState (2288)
//   Client/mods/deathmatch/logic/CUnoccupiedVehicleSync.cpp  Packet_UnoccupiedVehicleSync (178-208)
//   Shared/sdk/net/SyncStructures.h  SUnoccupiedVehicleSync (787), SVehiclePuresyncFlags (732), etc.
// Peer bitstream is 0x0030 (entity::kServerEntityBitStream), so the 0x31+ gates are not taken.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "entity/EntityAddTypes.h"

namespace WorldRPG::VehicleSync {

// Number of parts per damage group (Client/sdk/game/CDamageManager.h enums).
constexpr unsigned kMaxDoors = 6;
constexpr unsigned kMaxWheels = 4;
constexpr unsigned kMaxPanels = 7;
constexpr unsigned kMaxLights = 4;
constexpr unsigned kMaxTrailers = 8; // reference is unbounded; we cap to protect the parse

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// Decoded SUnoccupiedVehicleSync (one entry of packet 56). Rotation is in degrees, as on the wire.
struct UnoccupiedUpdate {
    uint32_t vehicleId = 0xFFFFFFFFu;
    uint8_t timeContext = 0;
    bool syncPosition = false;
    bool syncRotation = false;
    bool syncVelocity = false;
    bool syncTurnVelocity = false;
    bool syncHealth = false;
    bool syncTrailer = false;
    bool engineOn = false;
    bool derailed = false;
    bool inWater = false;
    Vec3 position;
    Vec3 rotation;
    Vec3 velocity;
    Vec3 turnVelocity;
    float health = 0;
    uint32_t trailer = 0xFFFFFFFFu;
};

// SFullKeysyncSync (SyncStructures.h 939): the on-foot/driver keys sent with every puresync.
struct FullKeys {
    bool leftShoulder1 = false;
    bool rightShoulder1 = false;
    bool buttonSquare = false;
    bool buttonCross = false;
    bool buttonCircle = false;
    bool buttonTriangle = false;
    bool shockButtonL = false;
    bool pedWalk = false;
    uint8_t analogSquare = 0;
    uint8_t analogCross = 0;
    int16_t stickX = 0;
    int16_t stickY = 0;
};

// SVehiclePuresyncFlags (11 bits, SyncStructures.h 732).
struct VehicleFlags {
    bool wearingGoggles = false;
    bool gangDriveby = false;
    bool sirenOrAlarm = false;
    bool smokeTrail = false;
    bool landingGearDown = false;
    bool onGround = false;
    bool inWater = false;
    bool derailed = false;
    bool aircraft = false;
    bool hasWeapon = false;
    bool heliSearchLight = false;
};

// One trailer block: SPositionSync(false) + SRotationDegreesSync(false) per entry.
struct TrailerPose {
    uint32_t id = 0xFFFFFFFFu;
    Vec3 position;
    Vec3 rotation;
};

// Weapon part of packet 33 (only when the weapon slot is read, see ReadOccupiedSync).
struct WeaponState {
    uint8_t slot = 0;
    bool ammoSynced = false;   // slot has ammo and the player is doing a gang driveby
    uint16_t totalAmmo = 0;
    uint16_t ammoInClip = 0;
    int16_t armRaw = 0;        // SWeaponAimSync arm, raw degrees
    Vec3 aimOrigin;
    Vec3 aimDirection;         // unit vector from the norm read; zero when the aim was cut short
    bool aimFull = false;      // false when the origin limit check stopped the read (reference quirk)
    uint8_t drivebyDirection = 0;
};

// Decoded body of PLAYER_VEHICLE_PURESYNC after the player ElementID.
struct OccupiedSync {
    uint8_t timeContext = 0;
    uint16_t latency = 0;
    FullKeys keys;
    int32_t remoteModel = 0;
    VehicleFlags flags;
    float playerHealth = 0;
    float playerArmor = 0;

    // Driver only (seat 0): vehicle position, rotation, velocity and health.
    bool hasDriverBlock = false;
    bool trainBlock = false;
    Vec3 position;
    Vec3 rotation;
    Vec3 velocity;
    Vec3 turnVelocity;
    float vehicleHealth = 0;
    float trainPosition = 0;
    float trainSpeed = 0;
    bool trainDirection = false;
    uint8_t trainTrack = 0;
    std::vector<TrailerPose> trailers;

    bool hasWeapon = false;
    WeaponState weapon;

    // Driver only: ReadFullVehicleSpecific.
    bool hasTurret = false;
    float turretX = 0, turretY = 0;
    bool hasAdjustable = false;
    uint16_t adjustable = 0;
    bool hasDoorRatios = false;
    float doorRatio[4] = {0, 0, 0, 0}; // doors 2..5

    // Aircraft look keys (only when flags.aircraft).
    bool lookLeft = false;
    bool lookRight = false;

    // ReadVehiclePartsState (SVehicleDamageSyncMethodeB): all groups, zero when not synced.
    bool syncDoors = false, syncWheels = false, syncPanels = false, syncLights = false;
    uint8_t doors[kMaxDoors] = {};
    uint8_t wheels[kMaxWheels] = {};
    uint8_t panels[kMaxPanels] = {};
    uint8_t lights[kMaxLights] = {};

    bool onFire = false;
};

// Reads one SUnoccupiedVehicleSync entry. False on a short read.
bool ReadUnoccupiedEntry(WorldRPG::Net::BitStreamReader& reader, UnoccupiedUpdate& out);

// Writes one SUnoccupiedVehicleSync entry (packet 56). Exact inverse of ReadUnoccupiedEntry.
// Only the fields whose sync flag is set are written. Always returns true.
bool WriteUnoccupiedEntry(WorldRPG::Net::BitStreamWriter& writer, const UnoccupiedUpdate& in);

// Input for the driver form of PLAYER_VEHICLE_PURESYNC (packet 33, seat 0), client to server.
// Fields without a phone source are written in their absent form (see WriteDriverPuresync).
struct DriverPuresyncIn {
    uint8_t timeContext = 0;
    FullKeys keys;               // controller state; zero when no car-control source exists
    int32_t model = 0;           // GTA model id (400-611)
    Vec3 position;               // vehicle position
    Vec3 rotation;               // vehicle rotation in degrees
    Vec3 velocity;               // move speed, as sampled from the body
    Vec3 turnVelocity;           // turn speed, as sampled from the body
    float vehicleHealth = 0;     // 0 - 2047.5
    float playerHealth = 0;      // 0 - 255 (0 - 200 game scale)
    float playerArmor = 0;       // 0 - 127.5
    float cameraRotZ = 0;        // camera heading, radians, -pi..pi
    float cameraRotX = 0;        // camera pitch, radians, -pi..pi
    VehicleFlags flags;          // flags.hasWeapon must be false (weapon block not ported)
    bool lookLeft = false;       // aircraft only, used when flags.aircraft
    bool lookRight = false;
    bool hasDamage = false;      // one-shot damage block (desktop DamageSent)
    uint32_t damagerId = 0xFFFFFFFFu;
    uint8_t damageWeapon = 0;    // 6-bit weapon type
};

// Writes the driver body of packet 33 (desktop CNetAPI::WriteVehiclePuresync, seat 0), field by field in
// the order the server reader CVehiclePuresyncPacket::Read consumes it. The caller writes the packet id
// (33) first. Returns false without writing when the body cannot match the reader: train models (the
// train block is not ported) and flags.hasWeapon (the weapon block is not ported).
bool WriteDriverPuresync(WorldRPG::Net::BitStreamWriter& writer, const DriverPuresyncIn& in);

// Reads the packet 33 body that follows the player ElementID. `seat` must be the occupant seat
// (the reference reads it from the local player, not from the wire). False on a short read.
bool ReadOccupiedSync(WorldRPG::Net::BitStreamReader& reader, uint8_t seat, OccupiedSync& out);

} // namespace WorldRPG::VehicleSync
