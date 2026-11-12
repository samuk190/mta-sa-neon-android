#include <cmath>
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

// MTA NetBitStream::ReadNormVector encoding: x sign flag, y and z zero flags, then 16-bit
// components for y and z (when not zero); x is rebuilt from the unit-length constraint.
// Same encoding as ReadMtaNorm in PacketHandler.cpp, which the phone already uses for players.
bool ReadNormVector(Reader& r, Vec3& out) {
    bool xNeg = false, yZero = false, zZero = false;
    VS_TRY(ReadFlag(r, xNeg) && ReadFlag(r, yZero));
    float y = 0.0f, z = 0.0f;
    if (!yZero) {
        uint16_t raw = 0;
        VS_TRY(r.Read(raw));
        y = (static_cast<float>(raw) / 32767.5f) - 1.0f;
    }
    VS_TRY(ReadFlag(r, zZero));
    if (!zZero) {
        uint16_t raw = 0;
        VS_TRY(r.Read(raw));
        z = (static_cast<float>(raw) / 32767.5f) - 1.0f;
    }
    const float rest = 1.0f - y * y - z * z;
    float x = rest > 0.0f ? std::sqrt(rest) : 0.0f;
    if (xNeg) x = -x;
    out.x = x;
    out.y = y;
    out.z = z;
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

// ---- Writers: exact inverses of the readers above (same field order, widths and bit layout). ----
using Writer = WorldRPG::Net::BitStreamWriter;

// Inverse of the reader's byte-array bit stream: the `bits` low bits of value, LSB-first bytes.
void WriteBitsValue(Writer& w, uint32_t value, unsigned bits) {
    uint8_t bytes[4] = {};
    for (int i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
    w.WriteBits(bytes, bits);
}

// Inverse of ReadFlag: one bit.
void WriteFlag(Writer& w, bool on) {
    uint8_t bit = on ? 1u : 0u;
    w.WriteBits(&bit, 1);
}

// Inverse of ReadElementId: 17 bits, 0xFFFFFFFF is INVALID_ELEMENT_ID (all ones on the wire).
void WriteElementId(Writer& w, uint32_t id) {
    constexpr uint32_t kMask = (1u << 17) - 1u;
    WriteBitsValue(w, id == 0xFFFFFFFFu ? kMask : (id & kMask), 17);
}

// Inverse of ReadFloatAsBits: raw = round((value - min) / (max - min) * (2^bits - 1)), clamped.
// preserveAboveMin mirrors SFloatAsBitsSync's m_bPreserveGreaterThanMin: a value above min never
// rounds down to raw 0 (SyncStructures.h 120-177).
void WriteFloatAsBits(Writer& w, unsigned bits, float minValue, float maxValue, float value, bool preserveAboveMin = false) {
    const double maxRaw = static_cast<double>((1ull << bits) - 1ull);
    double alpha = 0.0;
    if (maxValue != minValue) alpha = (static_cast<double>(value) - minValue) / (static_cast<double>(maxValue) - minValue);
    if (!std::isfinite(alpha)) alpha = 0.0;
    alpha = std::clamp(alpha, 0.0, 1.0);
    uint32_t raw = static_cast<uint32_t>(std::lround(alpha * maxRaw));
    if (preserveAboveMin && raw == 0 && alpha > 0.0) raw = 1;
    WriteBitsValue(w, raw, bits);
}

// Inverse of ReadVehicleHealth (SVehicleHealthSync): 12 bits, 0 - 2047.5, preserve > 0 (SyncStructures 201).
void WriteVehicleHealth(Writer& w, float health) {
    WriteFloatAsBits(w, 12, 0.f, 2047.5f, health, true);
}

// Inverse of ReadFixed24: signed 24 bits, value * 1024.
void WriteFixed24(Writer& w, float value) {
    const double scaled = std::isfinite(value) ? static_cast<double>(value) * 1024.0 : 0.0;
    const long fixed = std::lround(std::clamp(scaled, -8388608.0, 8388607.0));
    WriteBitsValue(w, static_cast<uint32_t>(fixed) & 0xFFFFFFu, 24);
}

// Inverse of ReadPosition: two fixed24 components, then a float Z.
void WriteVec3Position(Writer& w, const Vec3& in) {
    WriteFixed24(w, in.x);
    WriteFixed24(w, in.y);
    w.Write(in.z);
}

// Inverse of ReadRotationDegrees: one uint16 per axis, 360 / 65536 degrees per step, wrapped to [0, 360).
void WriteRotationAxis(Writer& w, float degrees) {
    double d = std::isfinite(degrees) ? std::fmod(static_cast<double>(degrees), 360.0) : 0.0;
    if (d < 0.0) d += 360.0;
    const long raw = std::lround(d * 65536.0 / 360.0);
    w.Write(static_cast<uint16_t>(static_cast<uint32_t>(raw) & 0xFFFFu));
}

void WriteVec3Rotation(Writer& w, const Vec3& in) {
    WriteRotationAxis(w, in.x);
    WriteRotationAxis(w, in.y);
    WriteRotationAxis(w, in.z);
}

// Inverse of the 16-bit norm component used by ReadNormVector: (c + 1) * 32767.5 over [-1, 1].
uint16_t QuantizeNormComponent(float c) {
    const double scaled = std::isfinite(c) ? std::clamp(static_cast<double>(c), -1.0, 1.0) : 0.0;
    const long raw = std::lround((scaled + 1.0) * 32767.5);
    return static_cast<uint16_t>(std::clamp(raw, 0L, 65535L));
}

// Inverse of ReadNormVector: x sign flag, y zero flag and value, z zero flag and value.
// x is not sent; the reader rebuilds its magnitude from the unit length.
void WriteNormVector(Writer& w, const Vec3& in) {
    WriteFlag(w, in.x < 0.0f);
    const bool yZero = in.y == 0.0f;
    WriteFlag(w, yZero);
    if (!yZero) w.Write(QuantizeNormComponent(in.y));
    const bool zZero = in.z == 0.0f;
    WriteFlag(w, zZero);
    if (!zZero) w.Write(QuantizeNormComponent(in.z));
}

// Inverse of ReadVelocity (SVelocitySync): moving flag, then module float and norm vector.
// A zero vector is written as a single cleared flag, as in the reference.
void WriteVelocity(Writer& w, const Vec3& in) {
    const double length = std::sqrt(static_cast<double>(in.x) * in.x + static_cast<double>(in.y) * in.y +
                                    static_cast<double>(in.z) * in.z);
    if (!(length > 0.0) || !std::isfinite(length)) {
        WriteFlag(w, false);
        return;
    }
    WriteFlag(w, true);
    w.Write(static_cast<float>(length));
    const Vec3 dir{static_cast<float>(in.x / length), static_cast<float>(in.y / length), static_cast<float>(in.z / length)};
    WriteNormVector(w, dir);
}

// Inverse of ReadFullKeys (SFullKeysyncSync, SyncStructures.h 939 Write): 8 button bits, then each
// analog byte only when it is 1..254 (flag + byte), then the two stick bytes.
void WriteFullKeys(Writer& w, const FullKeys& k) {
    uint8_t bits = 0;
    if (k.leftShoulder1) bits |= 0x01;
    if (k.rightShoulder1) bits |= 0x02;
    if (k.buttonSquare) bits |= 0x04;
    if (k.buttonCross) bits |= 0x08;
    if (k.buttonCircle) bits |= 0x10;
    if (k.buttonTriangle) bits |= 0x20;
    if (k.shockButtonL) bits |= 0x40;
    if (k.pedWalk) bits |= 0x80;
    w.Write(bits);

    const bool hasSquare = k.analogSquare >= 1 && k.analogSquare <= 254;
    WriteFlag(w, hasSquare);
    if (hasSquare) w.Write(k.analogSquare);

    const bool hasCross = k.analogCross >= 1 && k.analogCross <= 254;
    WriteFlag(w, hasCross);
    if (hasCross) w.Write(k.analogCross);

    // Stick: desktop stores -128..128 and writes (char)(value * 127 / 128), truncated toward zero.
    auto stickByte = [](int16_t s) {
        const int c = static_cast<int>(static_cast<float>(s) * 127.0f / 128.0f);
        return static_cast<uint8_t>(static_cast<int8_t>(std::clamp(c, -128, 127)));
    };
    w.Write(stickByte(k.stickX));
    w.Write(stickByte(k.stickY));
}

// Camera orientation, desktop CNetAPI::WriteCameraOrientation (CNetAPI.cpp 1023-1114). Vehicle packets
// carry no SCameraRotationSync (the 12-bit field of the on-foot packet), only these fields: two 8-bit
// angles, a relative/absolute flag, a 2-bit table index and three offsets. The camera position is not
// sampled, so the offset is zero, quantised as the reader sees it (3-bit raw 4 = +0.57, the same value
// NeonJoinProtocol writes for the on-foot camera).
void WriteCameraOrientation(Writer& w, float rotZ, float rotX) {
    WriteFloatAsBits(w, 8, -kPi, kPi, rotZ);
    WriteFloatAsBits(w, 8, -kPi, kPi, rotX);
    WriteFlag(w, false);                              // relative to the vehicle position
    WriteBitsValue(w, 0, 2);                          // table entry 0: 3 bits, range +-4
    for (int i = 0; i < 3; ++i) WriteFloatAsBits(w, 3, -4.0f, 4.0f, 0.0f);
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

bool WriteUnoccupiedEntry(Writer& w, const UnoccupiedUpdate& in) {
    WriteElementId(w, in.vehicleId);
    w.Write(in.timeContext);

    // Same bit layout as ReadUnoccupiedEntry: 9 flag bits, LSB first.
    unsigned f = 0;
    if (in.syncPosition) f |= 0x001u;
    if (in.syncRotation) f |= 0x002u;
    if (in.syncVelocity) f |= 0x004u;
    if (in.syncTurnVelocity) f |= 0x008u;
    if (in.syncHealth) f |= 0x010u;
    if (in.syncTrailer) f |= 0x020u;
    if (in.engineOn) f |= 0x040u;
    if (in.derailed) f |= 0x080u;
    if (in.inWater) f |= 0x100u;
    const uint8_t flagRaw[2] = {static_cast<uint8_t>(f & 0xFFu), static_cast<uint8_t>((f >> 8) & 0x01u)};
    w.WriteBits(flagRaw, 9);

    if (in.syncPosition) WriteVec3Position(w, in.position);
    if (in.syncRotation) WriteVec3Rotation(w, in.rotation);
    if (in.syncVelocity) WriteVelocity(w, in.velocity);
    if (in.syncTurnVelocity) WriteVelocity(w, in.turnVelocity);
    if (in.syncHealth) WriteVehicleHealth(w, in.health);
    if (in.syncTrailer) WriteElementId(w, in.trailer);
    return true;
}

// Client-to-server driver body of packet 33. Field order follows the desktop writer
// (CNetAPI::WriteVehiclePuresync, CNetAPI.cpp 1615-1826) and the server reader
// (Server/.../packets/CVehiclePuresyncPacket.cpp Read, 48-600). The server reader wins where they differ.
bool WriteDriverPuresync(Writer& w, const DriverPuresyncIn& in) {
    const uint32_t model = static_cast<uint32_t>(in.model);
    // Refuse before writing anything: the reader would expect a train block or a weapon block here.
    if (VM::IsTrainModel(model) || in.flags.hasWeapon) return false;

    // Sync context and SFullKeysyncSync (CNetAPI 1618-1623; reader 68-80).
    w.Write(in.timeContext);
    WriteFullKeys(w, in.keys);

    // Model id as a 32-bit int (CNetAPI 1632; reader 85).
    w.Write(static_cast<int32_t>(in.model));

    // SPositionSync(false): fixed24 x, fixed24 y, float z (CNetAPI 1639; reader 91-93).
    WriteVec3Position(w, in.position);

    // Camera orientation (CNetAPI 1657 WriteCameraOrientation; reader 118 ReadCameraOrientation).
    WriteCameraOrientation(w, in.cameraRotZ, in.cameraRotX);

    // SOccupiedSeatSync, 4 bits. The phone only drives seat 0 (CNetAPI 1660; reader 122-130).
    WriteBitsValue(w, 0, 4);

    // Driver block, desktop uses the seat==0 branch (CNetAPI 1664-1719; reader 134-154).
    WriteVec3Rotation(w, in.rotation);       // SRotationDegreesSync(false)
    WriteVelocity(w, in.velocity);           // SVelocitySync, move speed
    WriteVelocity(w, in.turnVelocity);       // SVelocitySync, turn speed
    WriteVehicleHealth(w, in.vehicleHealth); // SVehicleHealthSync, 12 bits (reader 154)

    // Trailer chain (CNetAPI 1720-1723; reader 272). No trailer source: continuation bit clear.
    WriteFlag(w, false);

    // Damage block (CNetAPI 1724-1753; reader 387-400). One-shot: bit, ElementID, 6-bit weapon type,
    // 3-bit body part. Body part raw 0 is torso, as the on-foot writer does.
    WriteFlag(w, in.hasDamage);
    if (in.hasDamage) {
        WriteElementId(w, in.damagerId);
        WriteBitsValue(w, in.damageWeapon & 0x3Fu, 6);
        WriteBitsValue(w, 0, 3);
    }

    // Player health and armor (CNetAPI 1754-1766; reader 405-456). Health 0-255, armor 0-127.5.
    WriteFloatAsBits(w, 8, 0.0f, 255.0f, in.playerHealth, true);
    WriteFloatAsBits(w, 8, 0.0f, 127.5f, in.playerArmor, true);

    // SVehiclePuresyncFlags, 11 bits, bit order as the reader (reader 507; SyncStructures.h 732).
    const VehicleFlags& fl = in.flags;
    unsigned flagBits = 0;
    if (fl.wearingGoggles) flagBits |= 0x001u;
    if (fl.gangDriveby) flagBits |= 0x002u;
    if (fl.sirenOrAlarm) flagBits |= 0x004u;
    if (fl.smokeTrail) flagBits |= 0x008u;
    if (fl.landingGearDown) flagBits |= 0x010u;
    if (fl.onGround) flagBits |= 0x020u;
    if (fl.inWater) flagBits |= 0x040u;
    if (fl.derailed) flagBits |= 0x080u;
    if (fl.aircraft) flagBits |= 0x100u;
    if (fl.hasWeapon) flagBits |= 0x200u;  // always false here, see the refusal above
    if (fl.heliSearchLight) flagBits |= 0x400u;
    WriteBitsValue(w, flagBits, 11);

    // No weapon block: flags.hasWeapon is false (CNetAPI 1783; reader 515 is skipped).

    // WriteFullVehicleSpecific (CNetAPI 2005, called at 1815; reader 555 and CVehiclePuresyncPacket::ReadVehicleSpecific 832).
    // Each block is gated by the model, so the reader expects it whatever the phone knows.
    if (VM::HasTurret(model)) {
        // SVehicleTurretSync: two int16 angles. Turret angle is not sampled, so 0 (centred).
        w.Write(static_cast<uint16_t>(0));
        w.Write(static_cast<uint16_t>(0));
    }
    if (VM::HasAdjustableProperty(model)) {
        // uint16 adjustable property. Not sampled, so 0.
        w.Write(static_cast<uint16_t>(0));
    }
    if (VM::HasDoors(model)) {
        // SDoorOpenRatioSync for doors 2..5. Not sampled, so 0 (closed): flag 0, flag 0 (SyncStructures 1424-1470).
        for (int door = 0; door < 4; ++door) {
            WriteFlag(w, false);
            WriteFlag(w, false);
        }
    }

    // Aircraft look-left / look-right bits (CNetAPI 1820-1823; reader 573-575). Only when flags.aircraft.
    if (fl.aircraft) {
        WriteFlag(w, in.lookLeft);
        WriteFlag(w, in.lookRight);
    }

    // OnFire bit (CNetAPI 1826; reader 585). Not sampled on Android, so false.
    WriteFlag(w, false);
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
