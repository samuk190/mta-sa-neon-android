#pragma once

#include "../net/raknet/RakNetWrapper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace WorldRPG::PlayerWeaponSync {

constexpr uint8_t kAim = 0x02;
constexpr uint8_t kFire = 0x10;
constexpr uint16_t kHasWeapon = 0x0200;
constexpr uint16_t kReloading = 0x1000;

struct Vector {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct Aim {
    int16_t arm = 0;
    Vector origin;
    Vector direction;
    bool full = false;
    float yaw = 0.f;
    uint8_t drivebyDirection = 0;
    bool inVehicle = false;
    bool akimboUp = false;
    bool akimbo = false;
    float range = 0.f;
};

constexpr float kArmUnits = 90.f * 180.f / 3.14159265f;
inline bool IsAkimbo(unsigned type) { return type == 22 || type == 26 || type == 28 || type == 32; }

// CClientPed::SetAimInterpolated / SetTargetTarget and Interpolate. On foot,
// blend from the pose on screen; vehicle SetAimingData applies immediately.
struct AimInterpolation {
    Aim from, to;
    int64_t began = 0;
    bool initialized = false;
    Aim sample(int64_t now) const {
        if (!initialized || !to.full || now >= began + 100 || to.inVehicle) return to;
        const float t = std::clamp(float(now - began) / 100.f, 0.f, 1.f);
        auto blend = [t](float a, float b) { return a + (b - a) * t; };
        auto component = [&](float a, float b) {
            return std::cos(blend(std::acos(std::clamp(a, -1.f, 1.f)), std::acos(std::clamp(b, -1.f, 1.f))));
        };
        Aim out = to;
        out.origin = {blend(from.origin.x, to.origin.x), blend(from.origin.y, to.origin.y), blend(from.origin.z, to.origin.z)};
        out.direction = {component(from.direction.x, to.direction.x), component(from.direction.y, to.direction.y),
                         component(from.direction.z, to.direction.z)};
        if (std::fabs(to.yaw - from.yaw) <= 5.f) out.yaw = blend(from.yaw, to.yaw);
        out.arm = static_cast<int16_t>(std::lround(blend(from.arm, to.arm)));
        out.akimboUp = from.akimboUp;
        return out;
    }
    void update(const Aim& aim, int64_t now) {
        from = initialized && to.full && !to.inVehicle ? sample(now) : aim;
        if (initialized && to.full) from.akimboUp = to.akimboUp;
        to = aim;
        began = now;
        initialized = true;
    }
};

struct LocalWeapon {
    bool valid = false;
    bool aiming = false;
    bool reloading = false;
    bool gangDriveby = false;
    uint8_t slot = 0;
    uint8_t type = 0;
    uint16_t total = 0;
    uint16_t clip = 0;
    std::array<uint16_t, 13> totalBySlot{};
    Aim aim;
};

struct RemoteWeapon {
    uint8_t slot = 0;
    bool ammoSynced = false;
    uint16_t total = 9999;  // CClientPed::UpdateKeysync sets presentation reserves.
    uint16_t clip = 0;
    Aim aim;
};

// Desktop CWeaponNames::DoesSlotHaveAmmo. The slot field is four bits on the wire.
inline bool SlotHasAmmo(unsigned slot) {
    return slot != 0 && slot != 1 && slot != 10 && slot != 11 && slot != 12;
}

inline bool HasAimOrFire(uint8_t buttons) { return (buttons & (kAim | kFire)) != 0; }

inline bool SetAimLine(Aim& aim, Vector origin, Vector target) {
    aim.full = false;
    constexpr float limit = 100000.f;  // SYNC_POSITION_LIMIT
    if (!(std::fabs(origin.x) < limit && std::fabs(origin.y) < limit && std::fabs(origin.z) < limit))
        return false;
    const Vector delta{target.x - origin.x, target.y - origin.y, target.z - origin.z};
    const float length = std::hypot(delta.x, delta.y, delta.z);
    if (!(length > 0.0001f) || !std::isfinite(length)) return false;
    aim.origin = origin;
    aim.direction = {delta.x / length, delta.y / length, delta.z / length};
    aim.full = true;
    return true;
}

// CClientPed::GetShotData advances first-person rays two units along the active camera.
inline bool SetFirstPersonAim(Aim& aim, Vector source, Vector front) {
    const float length = std::hypot(front.x, front.y, front.z);
    if (!(length > 0.0001f) || !std::isfinite(length)) return false;
    Vector direction{front.x / length, front.y / length, front.z / length};
    Vector origin{source.x + 2.f * direction.x, source.y + 2.f * direction.y, source.z + 2.f * direction.z};
    return SetAimLine(aim, origin, {origin.x + direction.x, origin.y + direction.y, origin.z + direction.z});
}

inline void WriteAim(Net::BitStreamWriter& w, const Aim& aim, bool full) {
    w.Write(static_cast<uint16_t>(aim.arm));
    if (!full) return;
    w.Write(aim.origin.x);
    w.Write(aim.origin.y);
    w.Write(aim.origin.z);
    // SWeaponAimSync is the only aim-vector encoding here with the order X, Z, Y.
    w.WriteNormVector(aim.direction.x, aim.direction.z, aim.direction.y);
}

inline bool ReadAim(Net::BitStreamReader& r, Aim& aim, bool full) {
    uint16_t arm = 0;
    if (!r.Read(arm)) return false;
    aim.arm = static_cast<int16_t>(arm);
    if (!full) return true;
    constexpr float limit = 100000.f;
    auto& origin = aim.origin;
    if (!(r.Read(origin.x) && std::fabs(origin.x) < limit &&
          r.Read(origin.y) && std::fabs(origin.y) < limit &&
          r.Read(origin.z) && std::fabs(origin.z) < limit)) return false;
    aim.full = r.ReadNormVector(aim.direction.x, aim.direction.z, aim.direction.y);
    return aim.full;
}

// CClientGame::UpdatePlayerWeapons, consumed by CRPCFunctions::PlayerWeapon.
inline void WriteSwitch(Net::BitStreamWriter& w, const LocalWeapon& weapon, bool emptiedPrevious) {
    const uint8_t empty = emptiedPrevious;
    w.WriteBits(&empty, 1);
    w.WriteBits(&weapon.slot, 4);
    if (SlotHasAmmo(weapon.slot)) {
        w.WriteCompressed(weapon.total, 2);
        w.WriteCompressed(weapon.clip, 2);
    }
}

// CNetAPI::WritePlayerPuresync, after the camera orientation and before damage.
inline void WritePure(Net::BitStreamWriter& w, const LocalWeapon& weapon, uint8_t buttons) {
    w.Write(weapon.type);
    w.WriteBits(&weapon.slot, 4);
    if (SlotHasAmmo(weapon.slot)) {
        w.WriteCompressed(weapon.total, 2);
        w.WriteCompressed(weapon.clip, 2);
        WriteAim(w, weapon.aim, HasAimOrFire(buttons));
    }
}

// CNetAPI::WriteKeysync, present only when aim or fire is set.
inline void WriteKeys(Net::BitStreamWriter& w, const LocalWeapon& weapon) {
    const uint8_t hasWeapon = weapon.valid;
    w.WriteBits(&hasWeapon, 1);
    if (!weapon.valid) return;
    w.Write(weapon.type);
    w.WriteBits(&weapon.slot, 4);
    if (SlotHasAmmo(weapon.slot)) {
        w.WriteCompressed(weapon.clip, 2);
        WriteAim(w, weapon.aim, true);
        w.Write(weapon.aim.drivebyDirection);
    }
}

// Server relays omit weapon type and total ammo. The slot refers to the inventory
// populated by PLAYER_LIST and weapon RPCs. Only the keysync relay ends in driveby direction.
inline bool ReadRelay(Net::BitStreamReader& r, RemoteWeapon& weapon, bool fullAim, bool keysync) {
    if (!r.ReadBits(&weapon.slot, 4) || weapon.slot > 12) return false;
    if (!SlotHasAmmo(weapon.slot)) return true;
    uint32_t clip = 0;
    if (!r.ReadCompressed(clip, 2) || !ReadAim(r, weapon.aim, fullAim)) return false;
    weapon.ammoSynced = true;
    weapon.clip = static_cast<uint16_t>(clip);
    return !keysync || (r.Read(weapon.aim.drivebyDirection) && weapon.aim.drivebyDirection <= 3);
}

}  // namespace WorldRPG::PlayerWeaponSync
