#include "VehicleSync.h"

#include "entity/EntitySyncReaders.h"
#include "entity/EntityVehicleModels.h"

#include <algorithm>

namespace WorldRPG::VehicleSync {
namespace {

namespace VM = WorldRPG::Entity::VehicleModels;
using Reader = WorldRPG::Net::BitStreamReader;
using WorldRPG::Entity::ReadCompressed;
using WorldRPG::Entity::ReadElementId;
using WorldRPG::Entity::ReadFlag;
using WorldRPG::Entity::ReadFloatAsBits;
using WorldRPG::Entity::ReadPosition;
using WorldRPG::Entity::ReadRotationDegrees;
using WorldRPG::Entity::ReadVehicleHealth;

// SYNC_POSITION_LIMIT (SyncStructures.h 28).
constexpr float kSyncPositionLimit = 100000.0f;
constexpr float kPi = 3.14159265f;

// Early return on a failed read. DIAG names the source line and bit offset.
#define VS_TRY(expr)                                                                    \
    do {                                                                                \
        if (!(expr)) {                                                                  \
            DIAG("VehicleSync fail line=%d bit=%zu", __LINE__, r.GetReadOffsetBits()); \
            return false;                                                               \
        }                                                                               \
    } while (0)

// RakNet BitStream::ReadFloat16(-1, 1): one uint16 mapped linearly onto [-1, 1].
// BitStream.h 1907 ReadNormVector reads three of these; no sign or length prefix.
float UnitFromUint16(uint16_t raw) {
    const float v = -1.0f + (static_cast<float>(raw) / 65535.0f) * 2.0f;
    return std::clamp(v, -1.0f, 1.0f);
}

bool ReadNormVector(Reader& r, Vec3& out) {
    uint16_t a = 0, b = 0, c = 0;
    VS_TRY(r.Read(a) && r.Read(b) && r.Read(c));
    out.x = UnitFromUint16(a);
    out.y = UnitFromUint16(b);
    out.z = UnitFromUint16(c);
    return true;
}

// SVelocitySync (SyncStructures.h 557): one bit, then float module and norm vector.
bool ReadVelocity(Reader& r, Vec3& out) {
    bool moving = false;
    VS_TRY(ReadFlag(r, moving));
    if (!moving) {
        out = Vec3{};
        return true;
    }
    float module = 0.0f;
    VS_TRY(r.Read(module));
    Vec3 dir;
    VS_TRY(ReadNormVector(r, dir));
    out = Vec3{dir.x * module, dir.y * module, dir.z * module};
    return true;
}

bool ReadVec3Position(Reader& r, Vec3& out) {
    VS_TRY(ReadPosition(r, out.x, out.y, out.z));
    return true;
}

bool ReadVec3Rotation(Reader& r, Vec3& out) {
    VS_TRY(ReadRotationDegrees(r, out.x, out.y, out.z));
    return true;
}

// SFullKeysyncSync (SyncStructures.h 939). Bit order: LeftShoulder1 is bit 0 ... PedWalk bit 7.
bool ReadFullKeys(Reader& r, FullKeys& k) {
    uint8_t bits[1] = {};
    VS_TRY(r.ReadBits(bits, 8));
    k.leftShoulder1 = (bits[0] & 0x01) != 0;
    k.rightShoulder1 = (bits[0] & 0x02) != 0;
    k.buttonSquare = (bits[0] & 0x04) != 0;
    k.buttonCross = (bits[0] & 0x08) != 0;
    k.buttonCircle = (bits[0] & 0x10) != 0;
    k.buttonTriangle = (bits[0] & 0x20) != 0;
    k.shockButtonL = (bits[0] & 0x40) != 0;
    k.pedWalk = (bits[0] & 0x80) != 0;

    bool hasSquare = false;
    VS_TRY(ReadFlag(r, hasSquare));
    k.analogSquare = 0;
    if (hasSquare) VS_TRY(r.Read(k.analogSquare));

    bool hasCross = false;
    VS_TRY(ReadFlag(r, hasCross));
    k.analogCross = 0;
    if (hasCross) VS_TRY(r.Read(k.analogCross));

    uint8_t stickX = 0, stickY = 0;
    VS_TRY(r.Read(stickX));
    VS_TRY(r.Read(stickY));
    // Stick bytes are signed chars stretched to -128..128.
    k.stickX = static_cast<int16_t>(static_cast<float>(static_cast<int8_t>(stickX)) * 128.0f / 127.0f);
    k.stickY = static_cast<int16_t>(static_cast<float>(static_cast<int8_t>(stickY)) * 128.0f / 127.0f);
    return true;
}

// SWeaponAimSync (SyncStructures.h 1569), full variant. The reference ignores the bool returned by
// Read(&aim) and stops reading after a failed origin limit check, so the same stop point is kept here.
bool ReadWeaponAim(Reader& r, WeaponState& w) {
    uint16_t arm = 0;
    VS_TRY(r.Read(arm));
    w.armRaw = static_cast<int16_t>(arm);

    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
    VS_TRY(r.Read(ox));
    w.aimOrigin.x = ox;
    if (!(ox > -kSyncPositionLimit && ox < kSyncPositionLimit)) {
        w.aimFull = false;
        return true;
    }
    VS_TRY(r.Read(oy));
    w.aimOrigin.y = oy;
    if (!(oy > -kSyncPositionLimit && oy < kSyncPositionLimit)) {
        w.aimFull = false;
        return true;
    }
    VS_TRY(r.Read(oz));
    w.aimOrigin.z = oz;
    if (!(oz > -kSyncPositionLimit && oz < kSyncPositionLimit)) {
        w.aimFull = false;
        return true;
    }
    // The reference maps the norm vector as (X, Z, Y); only the bit count matters for parsing.
    Vec3 dir;
    VS_TRY(ReadNormVector(r, dir));
    w.aimDirection = dir;
    w.aimFull = true;
    return true;
}

// SVehicleTurretSync (SyncStructures.h 1394): two int16 values.
bool ReadTurret(Reader& r, float& tx, float& ty) {
    uint16_t h = 0, v = 0;
    VS_TRY(r.Read(h) && r.Read(v));
    tx = static_cast<float>(static_cast<int16_t>(h)) / (32767.0f / kPi);
    ty = static_cast<float>(static_cast<int16_t>(v)) / (32767.0f / kPi);
    return true;
}

// SDoorOpenRatioSync (SyncStructures.h 1424). Note the inverted name in the reference:
// a 0 first bit means "compressed to 0 or 1", a 1 first bit means a 10-bit float.
bool ReadDoorRatio(Reader& r, float& ratio) {
    bool uncompressed = false;
    VS_TRY(ReadFlag(r, uncompressed));
    if (!uncompressed) {
        bool notZero = false;
        VS_TRY(ReadFlag(r, notZero));
        ratio = notZero ? 1.0f : 0.0f;
        return true;
    }
    VS_TRY(ReadFloatAsBits(r, 10, 0.0f, 1.0f, ratio));
    return true;
}

// SVehiclePartStateSyncMethodeB (SyncStructures.h 1858): nonzero flag, then count * bits when set.
bool ReadPartStates(Reader& r, uint8_t* states, unsigned count, unsigned bits) {
    bool nonZero = false;
    VS_TRY(ReadFlag(r, nonZero));
    const unsigned mask = (1u << bits) - 1u;
    for (unsigned i = 0; i < count; ++i) states[i] = 0;
    if (!nonZero) return true;
    for (unsigned i = 0; i < count; ++i) {
        uint8_t raw[1] = {};
        VS_TRY(r.ReadBits(raw, bits));
        states[i] = static_cast<uint8_t>(raw[0] & mask);
    }
    return true;
}

// ReadVehiclePartsState: SVehicleDamageSyncMethodeB (SyncStructures.h 1920). Four flag bits
// (doors, wheels, panels, lights), then each enabled group.
bool ReadPartsState(Reader& r, OccupiedSync& o) {
    uint8_t flagBits[1] = {};
    VS_TRY(r.ReadBits(flagBits, 4));
    o.syncDoors = (flagBits[0] & 0x01) != 0;
    o.syncWheels = (flagBits[0] & 0x02) != 0;
    o.syncPanels = (flagBits[0] & 0x04) != 0;
    o.syncLights = (flagBits[0] & 0x08) != 0;
    if (o.syncDoors) VS_TRY(ReadPartStates(r, o.doors, kMaxDoors, 3));
    if (o.syncWheels) VS_TRY(ReadPartStates(r, o.wheels, kMaxWheels, 2));
    if (o.syncPanels) VS_TRY(ReadPartStates(r, o.panels, kMaxPanels, 2));
    if (o.syncLights) VS_TRY(ReadPartStates(r, o.lights, kMaxLights, 2));
    return true;
}

// Driver-only block of packet 33 (CNetAPI.cpp 1342-1435): position, optional train data,
// rotation, velocity, turn velocity, vehicle health and trailers.
bool ReadDriverBlock(Reader& r, OccupiedSync& o) {
    o.hasDriverBlock = true;
    VS_TRY(ReadVec3Position(r, o.position));

    o.trainBlock = VM::IsTrainModel(static_cast<uint32_t>(o.remoteModel));
    if (o.trainBlock) {
        VS_TRY(r.Read(o.trainPosition));
        VS_TRY(ReadFlag(r, o.trainDirection));
        VS_TRY(r.Read(o.trainSpeed));
        VS_TRY(r.Read(o.trainTrack));
    }

    VS_TRY(ReadVec3Rotation(r, o.rotation));
    VS_TRY(ReadVelocity(r, o.velocity));
    VS_TRY(ReadVelocity(r, o.turnVelocity));
    VS_TRY(ReadVehicleHealth(r, o.vehicleHealth));

    // Trailers: while a continuation bit is set, read an ElementID. The reference reads the pose
    // only when that trailer exists locally; this port has no vehicle lookup, so it assumes it
    // exists. A wrong guess misparses the rest of this packet only.
    for (;;) {
        bool more = false;
        if (!ReadFlag(r, more) || !more) break;
        uint32_t trailerId = 0;
        if (!ReadElementId(r, trailerId)) {
            DIAG("VehicleSync trailer id read failed bit=%zu", r.GetReadOffsetBits());
            continue;
        }
        if (o.trailers.size() >= kMaxTrailers) {
            DIAG("VehicleSync trailer cap reached (%u) bit=%zu", static_cast<unsigned>(kMaxTrailers), r.GetReadOffsetBits());
            return false;
        }
        TrailerPose pose;
        pose.id = trailerId;
        VS_TRY(ReadVec3Position(r, pose.position));
        VS_TRY(ReadVec3Rotation(r, pose.rotation));
        o.trailers.push_back(pose);
    }
    return true;
}

// ReadFullVehicleSpecific (CNetAPI.cpp 1965): turret, adjustable property and door ratios.
// Each read is gated by the remote model id, not by the local vehicle.
bool ReadDriverSpecific(Reader& r, OccupiedSync& o) {
    const uint32_t model = static_cast<uint32_t>(o.remoteModel);
    if (VM::HasTurret(model)) {
        o.hasTurret = true;
        VS_TRY(ReadTurret(r, o.turretX, o.turretY));
    }
    if (VM::HasAdjustableProperty(model)) {
        o.hasAdjustable = true;
        VS_TRY(r.Read(o.adjustable));
    }
    if (VM::HasDoors(model)) {
        o.hasDoorRatios = true;
        for (unsigned i = 0; i < 4; ++i) VS_TRY(ReadDoorRatio(r, o.doorRatio[i]));
    }
    return true;
}

// Weapon block (CNetAPI.cpp 1395-1460 region): slot, then ammo, aim and driveby only for a
// gang driveby on a slot that has ammo. Slots 0, 1, 10, 11, 12 never have ammo (CWeaponNames.h).
bool ReadWeaponBlock(Reader& r, const VehicleFlags& flags, WeaponState& w) {
    uint8_t slotBits[1] = {};
    VS_TRY(r.ReadBits(slotBits, 4));
    w.slot = static_cast<uint8_t>(slotBits[0] & 0x0Fu);
    const bool slotHasAmmo = !(w.slot == 0 || w.slot == 1 || w.slot == 10 || w.slot == 11 || w.slot == 12);
    if (!flags.gangDriveby || !slotHasAmmo) return true;

    w.ammoSynced = true;
    uint32_t total = 0, clip = 0;
    VS_TRY(ReadCompressed(r, 2, total));
    VS_TRY(ReadCompressed(r, 2, clip));
    w.totalAmmo = static_cast<uint16_t>(total);
    w.ammoInClip = static_cast<uint16_t>(clip);
    VS_TRY(ReadWeaponAim(r, w));
    uint8_t driveby[1] = {};
    VS_TRY(r.ReadBits(driveby, 2));
    w.drivebyDirection = static_cast<uint8_t>(driveby[0] & 0x03u);
    return true;
}

} // namespace

bool ReadUnoccupiedEntry(Reader& r, UnoccupiedUpdate& out) {
    uint32_t id = 0;
    uint8_t context = 0;
    uint8_t flagRaw[2] = {};
    VS_TRY(ReadElementId(r, id));
    VS_TRY(r.Read(context));
    VS_TRY(r.ReadBits(flagRaw, 9));

    out.vehicleId = id;
    out.timeContext = context;
    const unsigned f = flagRaw[0] | (static_cast<unsigned>(flagRaw[1]) << 8);
    out.syncPosition = (f & 0x001u) != 0;
    out.syncRotation = (f & 0x002u) != 0;
    out.syncVelocity = (f & 0x004u) != 0;
    out.syncTurnVelocity = (f & 0x008u) != 0;
    out.syncHealth = (f & 0x010u) != 0;
    out.syncTrailer = (f & 0x020u) != 0;
    out.engineOn = (f & 0x040u) != 0;
    out.derailed = (f & 0x080u) != 0;
    out.inWater = (f & 0x100u) != 0;

    if (out.syncPosition) VS_TRY(ReadVec3Position(r, out.position));
    if (out.syncRotation) VS_TRY(ReadVec3Rotation(r, out.rotation));
    if (out.syncVelocity) VS_TRY(ReadVelocity(r, out.velocity));
    if (out.syncTurnVelocity) VS_TRY(ReadVelocity(r, out.turnVelocity));
    if (out.syncHealth) VS_TRY(ReadVehicleHealth(r, out.health));
    if (out.syncTrailer) VS_TRY(ReadElementId(r, out.trailer));
    return true;
}

bool ReadOccupiedSync(Reader& r, uint8_t seat, OccupiedSync& o) {
    uint8_t context = 0;
    VS_TRY(r.Read(context));
    o.timeContext = context;

    uint32_t latency = 0;
    VS_TRY(ReadCompressed(r, 2, latency));
    o.latency = static_cast<uint16_t>(latency);

    VS_TRY(ReadFullKeys(r, o.keys));

    int32_t remoteModel = 0;
    VS_TRY(r.Read(remoteModel));
    o.remoteModel = remoteModel;

    if (seat == 0) VS_TRY(ReadDriverBlock(r, o));

    float health = 0, armor = 0;
    VS_TRY(ReadFloatAsBits(r, 8, 0.0f, 255.0f, health));
    VS_TRY(ReadFloatAsBits(r, 8, 0.0f, 127.5f, armor));
    o.playerHealth = health;
    o.playerArmor = armor;

    uint8_t flagRaw[2] = {};
    VS_TRY(r.ReadBits(flagRaw, 11));
    const unsigned f = flagRaw[0] | (static_cast<unsigned>(flagRaw[1]) << 8);
    o.flags.wearingGoggles = (f & 0x001u) != 0;
    o.flags.gangDriveby = (f & 0x002u) != 0;
    o.flags.sirenOrAlarm = (f & 0x004u) != 0;
    o.flags.smokeTrail = (f & 0x008u) != 0;
    o.flags.landingGearDown = (f & 0x010u) != 0;
    o.flags.onGround = (f & 0x020u) != 0;
    o.flags.inWater = (f & 0x040u) != 0;
    o.flags.derailed = (f & 0x080u) != 0;
    o.flags.aircraft = (f & 0x100u) != 0;
    o.flags.hasWeapon = (f & 0x200u) != 0;
    o.flags.heliSearchLight = (f & 0x400u) != 0;

    o.hasWeapon = o.flags.hasWeapon;
    if (o.hasWeapon) VS_TRY(ReadWeaponBlock(r, o.flags, o.weapon));

    if (seat == 0) VS_TRY(ReadDriverSpecific(r, o));

    if (o.flags.aircraft) {
        VS_TRY(ReadFlag(r, o.lookLeft));
        VS_TRY(ReadFlag(r, o.lookRight));
    }

    VS_TRY(ReadPartsState(r, o));
    VS_TRY(ReadFlag(r, o.onFire));
    return true;
}

} // namespace WorldRPG::VehicleSync
