#pragma once

// State of a custom weapon element, ported from the desktop CClientWeapon
// (Client/mods/deathmatch/logic/CClientWeapon.cpp / .h).
//
// Header only and free of Lua and GTA calls, so the host tests can build it
// without the headless core. The Lua glue is in LuaWeaponApi.cpp. Native calls
// the phone does not have yet are passed in through Hooks and logged as DIAG by
// the caller.

#include <cmath>
#include <cstdint>
#include <string>

namespace WorldRPG::Weapons {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// CClientWeapon.h:20-25.
enum class TargetType : uint8_t { Vector = 0, Entity = 1, Fixed = 2 };

// eWeaponState. CLuaFunctionParseHelpers.cpp:374-378 names only these three.
enum class State : uint8_t { Ready = 0, Firing = 1, Reloading = 2 };

// eWeaponFlags (CClientWeapon.h:36-43) and its Lua names (CLuaFunctionParseHelpers.cpp:380-386).
enum class Flag : uint8_t { Flags, ShootIfOutOfRange, ShootIfTargetBlocked, DisableModel, InstantReload, Invalid };

inline Flag FlagFromName(const char* name) {
    if (!name) return Flag::Invalid;
    const std::string text(name);
    if (text == "disable_model") return Flag::DisableModel;
    if (text == "flags") return Flag::Flags;
    if (text == "instant_reload") return Flag::InstantReload;
    if (text == "shoot_if_out_of_range") return Flag::ShootIfOutOfRange;
    if (text == "shoot_if_blocked") return Flag::ShootIfTargetBlocked;
    return Flag::Invalid;
}

inline bool StateFromName(const char* name, State& out) {
    if (!name) return false;
    const std::string text(name);
    if (text == "ready") out = State::Ready;
    else if (text == "firing") out = State::Firing;
    else if (text == "reloading") out = State::Reloading;
    else return false;
    return true;
}

inline const char* StateName(State state) {
    switch (state) {
        case State::Ready: return "ready";
        case State::Firing: return "firing";
        case State::Reloading: return "reloading";
    }
    return "";
}

// SLineOfSightFlags, with the values the CClientWeapon constructor sets (CClientWeapon.cpp:52-60).
struct LineOfSightFlags {
    bool checkBuildings = true;
    bool checkCarTires = true;
    bool checkDummies = true;
    bool checkObjects = true;
    bool checkPeds = true;
    bool checkVehicles = true;
    bool seeThroughStuff = false;
    bool shootThroughStuff = false;
    bool ignoreSomeObjectsForCamera = false;
};

// SWeaponConfiguration (CClientWeapon.h:27-34), constructor values (CClientWeapon.cpp:49-51).
// bInstantReload is not set by the desktop constructor; false here.
struct Config {
    bool shootIfTargetOutOfRange = false;
    bool shootIfTargetBlocked = false;
    bool disableWeaponModel = false;
    bool instantReload = false;
    LineOfSightFlags flags;
};

// The parts of CWeaponStat (WEAPONSKILL_STD, CreateWeaponStatUnlisted) that CClientWeapon reads.
struct Stat {
    bool known = false;  // false: the phone has no weapon.dat row for this type
    int damage = 0;
    float accuracy = 0, targetRange = 0, weaponRange = 0;
    int maximumClipAmmo = 0;
    float animLoopStart = 0, animLoopStop = 0;
    int flags = 0;
    Vec3 fireOffset;
};

// CWeaponInfo flag bits used by GetWeaponReloadTime (gta-reversed WeaponInfo.h:68-72).
constexpr int kFlagTwinPistol = 0x800;
constexpr int kFlagReload = 0x1000;
constexpr int kFlagLongReload = 0x8000;

constexpr int kMaxWheels = 4;  // MAX_WHEELS
constexpr int kBonePelvis = 1; // eBone::BONE_PELVIS

struct Weapon {
    uint32_t id = 0;
    int type = 0;
    State state = State::Ready;
    State previousState = State::Ready;
    Vec3 position;
    Vec3 rotation;  // radians, as SetRotationRadians
    TargetType targetType = TargetType::Fixed;
    uint32_t target = 0;
    int targetBone = kBonePelvis;
    int targetWheel = kMaxWheels + 1;
    Vec3 vecTarget;
    Config config;
    Stat stat;       // m_pWeaponStat (per weapon, script can change it)
    Stat info;       // m_pWeaponInfo (the shared STD info; Fire reads its range)
    uint32_t owner = 0;
    int ammoTotal = 9999;
    int ammoInClip = 0;
    uint32_t fireTimerStart = 0;
    uint32_t reloadTimerStart = 0;
    int fireRate = 0;
    Vec3 fireRotationNoTarget;  // radians
    bool visible = true;
    unsigned char counter = 0;
};

// CClientWeapon::GetWeaponFireTime(CWeaponStat*) (CClientWeapon.cpp:692-696).
inline int WeaponFireTime(const Stat& stat) {
    const float time = (stat.animLoopStop - stat.animLoopStart) * 1000.0f;
    return static_cast<int>(time);
}

// CWeaponInfo::GetWeaponReloadTime (0x743D70, gta-reversed WeaponInfo.cpp:442-456).
// The last branch reads ms_WeaponAimOffsets, which the phone data does not have;
// it returns the native floor of 400 ms. Every gun with a clip has the reload bit.
inline int WeaponReloadTime(const Stat& stat) {
    if (stat.flags & kFlagReload) return (stat.flags & kFlagTwinPistol) ? 2000 : 1000;
    if (stat.flags & kFlagLongReload) return 1000;
    return 400;
}

// The constructor body (CClientWeapon.cpp:19-67) without the native object.
inline void Initialize(Weapon& weapon, uint32_t id, int type, const Stat& stdStat, const Vec3& position, uint32_t now) {
    weapon = Weapon{};
    weapon.id = id;
    weapon.type = type;
    weapon.state = State::Ready;
    weapon.position = position;
    weapon.info = stdStat;
    weapon.stat = stdStat;
    weapon.targetType = TargetType::Fixed;  // ResetWeaponTarget
    weapon.target = 0;
    weapon.targetBone = kBonePelvis;
    weapon.targetWheel = kMaxWheels + 1;
    weapon.ammoInClip = stdStat.maximumClipAmmo;
    weapon.counter = 0;
    weapon.ammoTotal = 9999;
    weapon.fireRate = WeaponFireTime(stdStat);
    weapon.fireTimerStart = now;  // CElapsedTime starts when constructed
    weapon.reloadTimerStart = now;
}

// CClientWeapon::SetWeaponTarget(CClientEntity*, int) (CClientWeapon.cpp:541-557).
inline void SetWeaponTarget(Weapon& weapon, uint32_t target, int subTarget, bool targetIsPed, bool targetIsVehicle) {
    weapon.target = target;
    weapon.targetType = TargetType::Entity;
    if (targetIsPed) weapon.targetBone = subTarget == 255 ? kBonePelvis : subTarget;
    if (targetIsVehicle) weapon.targetWheel = subTarget;
}

// CClientWeapon::SetWeaponTarget(CVector) (CClientWeapon.cpp:558-562).
inline void SetWeaponTarget(Weapon& weapon, const Vec3& target) {
    weapon.vecTarget = target;
    weapon.targetType = TargetType::Vector;
}

// CClientWeapon::ResetWeaponTarget (CClientWeapon.cpp:564-570).
inline void ResetWeaponTarget(Weapon& weapon) {
    weapon.target = 0;
    weapon.vecTarget = Vec3{};
    weapon.targetType = TargetType::Fixed;
    weapon.targetBone = kBonePelvis;
}

// CClientWeapon::SetFlags(eWeaponFlags, bool) (CClientWeapon.cpp:572-598).
inline bool SetFlag(Weapon& weapon, Flag flag, bool value) {
    switch (flag) {
        case Flag::ShootIfOutOfRange: weapon.config.shootIfTargetOutOfRange = value; return true;
        case Flag::ShootIfTargetBlocked: weapon.config.shootIfTargetBlocked = value; return true;
        case Flag::DisableModel:
            weapon.config.disableWeaponModel = value;
            weapon.visible = !value;  // SetVisible(false / true)
            return true;
        case Flag::InstantReload: weapon.config.instantReload = value; return true;
        default: return false;
    }
}

// CClientWeapon::GetFlags(eWeaponFlags, bool&) (CClientWeapon.cpp:606-624).
inline bool GetFlag(const Weapon& weapon, Flag flag, bool& value) {
    switch (flag) {
        case Flag::ShootIfOutOfRange: value = weapon.config.shootIfTargetOutOfRange; return true;
        case Flag::ShootIfTargetBlocked: value = weapon.config.shootIfTargetBlocked; return true;
        case Flag::DisableModel: value = weapon.config.disableWeaponModel; return true;
        case Flag::InstantReload: value = weapon.config.instantReload; return true;
        default: return false;
    }
}

// CVector::ToRotation (Shared/sdk/CVector.h:87-96).
inline Vec3 ToRotation(const Vec3& direction) {
    Vec3 rotation;
    rotation.z = std::atan2(direction.y, direction.x);
    float tx = std::hypot(direction.x, direction.y), ty = direction.z;
    const float length = std::sqrt(tx * tx + ty * ty);
    if (length > 1e-6f) {
        tx /= length;
        ty /= length;
    }
    constexpr float pi = 3.14159265358979323846f;
    rotation.y = std::atan2(tx, ty) - pi / 2;
    return rotation;
}

// World access the weapon needs. Each returns false when the element is gone.
struct Hooks {
    // CClientEntity::GetPosition. Peds and players: CClientPed::GetBonePosition(bone).
    bool (*targetPosition)(uint32_t id, int bone, Vec3& out) = nullptr;
    bool (*isPed)(uint32_t id) = nullptr;
    // CClientWeapon::FireInstantHit (CClientWeapon.cpp:343-491): no native weapon on the phone yet.
    void (*fireInstantHit)(const Weapon& weapon, const Vec3& origin, const Vec3& target, bool fixedTarget) = nullptr;
};

inline float Distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Types that CClientWeapon::Fire handles (CClientWeapon.cpp:222-240, SHOTGUN_TEST off).
inline bool FiresInstantHit(int type) {
    switch (type) {
        case 22: case 23: case 24:              // pistol, silenced, desert eagle
        case 28: case 29: case 30: case 31: case 32:  // uzi, mp5, ak47, m4, tec9
        case 33: case 34: case 38:              // country rifle, sniper, minigun
            return true;
        default:
            return false;
    }
}

enum class FireResult { NotAllowed, TypeIgnored, OutOfRange, Fired };

// CClientWeapon::Fire(bool bServerFire) (CClientWeapon.cpp:216-339).
// isLocal: IsLocalEntity(). localPlayer: g_pClientGame->GetLocalPlayer().
// The desktop guard also returns when m_pWeapon or m_pObject is null; the phone
// has neither yet, so that part is the caller's DIAG, not an early return.
inline FireResult Fire(Weapon& weapon, bool isLocal, uint32_t localPlayer, bool serverFire, uint32_t now, const Hooks& hooks) {
    if (!isLocal && weapon.owner != localPlayer && !serverFire) return FireResult::NotAllowed;
    if (!FiresInstantHit(weapon.type)) return FireResult::TypeIgnored;

    const Vec3 origin = weapon.position;  // matOrigin.GetPosition()
    Vec3 target;
    const float distance = weapon.info.weaponRange;  // m_pWeaponInfo->GetWeaponRange()
    bool fixed = false;
    if (weapon.targetType == TargetType::Entity) {
        // A destroyed target counts as the desktop null pointer (entity refs clear m_pTarget):
        // the target is reset and the shot goes to the zero vector with no range check.
        const bool ped = weapon.target && hooks.isPed && hooks.isPed(weapon.target);
        // Vehicle with a wheel (m_itargetWheel <= MAX_WHEELS): CVehicle::GetWheelPosition is not
        // ported, so the vehicle position is used, as the desktop does for any other wheel value.
        if (weapon.target && hooks.targetPosition && hooks.targetPosition(weapon.target, ped ? weapon.targetBone : -1, target)) {
            if (!weapon.config.shootIfTargetOutOfRange && Distance(origin, target) >= distance)
                return FireResult::OutOfRange;
        } else {
            ResetWeaponTarget(weapon);
            target = Vec3{};
        }
    } else if (weapon.targetType == TargetType::Vector) {
        target = weapon.vecTarget;
        if (!weapon.config.shootIfTargetOutOfRange && Distance(origin, target) >= distance)
            return FireResult::OutOfRange;
    } else {
        fixed = true;
    }
    if (fixed) {
        // matOrigin = CMatrix(fireOffset, fireRotationNoTarget) * matOrigin; target = matOrigin * (range, 0, 0).
        // Only the heading of the weapon is applied here; the native hit will need the full matrix.
        const float yaw = weapon.rotation.z + weapon.fireRotationNoTarget.z;
        target = Vec3{origin.x + std::cos(yaw) * distance, origin.y + std::sin(yaw) * distance, origin.z};
    }

    // The desktop swaps the per-weapon stat into CWeaponInfo around FireInstantHit (282-296, 318-321).
    if (hooks.fireInstantHit) hooks.fireInstantHit(weapon, origin, target, fixed);

    weapon.ammoInClip--;
    weapon.fireTimerStart = now;
    return FireResult::Fired;
}

// CClientWeapon::DoPulse (CClientWeapon.cpp:84-187).
// Returns true when the pulse fired the weapon.
inline bool DoPulse(Weapon& weapon, bool isLocal, uint32_t localPlayer, uint32_t now, const Hooks& hooks) {
    if (weapon.targetType != TargetType::Fixed) {
        Vec3 targetPosition;
        bool haveTarget = true;
        if (weapon.targetType == TargetType::Entity) {
            const bool ped = weapon.target && hooks.isPed && hooks.isPed(weapon.target);
            if (!weapon.target || !hooks.targetPosition ||
                !hooks.targetPosition(weapon.target, ped ? weapon.targetBone : -1, targetPosition)) {
                ResetWeaponTarget(weapon);
                haveTarget = false;
            }
        } else {
            targetPosition = weapon.vecTarget;
        }
        // Attached weapons (SetAttachedOffsets branch, 132-148) are not ported: a local weapon cannot be attached yet.
        // After a reset the desktop still turns toward the zero vector it computed; that is kept.
        Vec3 direction{targetPosition.x - weapon.position.x, targetPosition.y - weapon.position.y,
                       targetPosition.z - weapon.position.z};
        if (!haveTarget) direction = Vec3{-weapon.position.x, -weapon.position.y, -weapon.position.z};
        weapon.rotation = ToRotation(direction);
    }

    const int maxClip = weapon.stat.maximumClipAmmo;
    if (weapon.ammoInClip <= 0) {
        if (!weapon.config.instantReload) {
            if (weapon.ammoTotal >= maxClip && weapon.state != State::Reloading) {
                weapon.previousState = weapon.state;
                weapon.state = State::Reloading;
                weapon.reloadTimerStart = now;
            } else if (weapon.state == State::Reloading &&
                       static_cast<int>(now - weapon.reloadTimerStart) >= WeaponReloadTime(weapon.stat)) {
                weapon.state = weapon.previousState;
                weapon.ammoInClip = maxClip;
                weapon.ammoTotal -= maxClip;
            } else {
                return false;
            }
        } else if (weapon.ammoTotal >= maxClip && weapon.state != State::Reloading) {
            weapon.ammoInClip = maxClip;
            weapon.ammoTotal -= maxClip;
        }
    }
    if (weapon.ammoInClip > 0 && (isLocal || weapon.owner == localPlayer)) {
        if (weapon.state == State::Firing && static_cast<int>(now - weapon.fireTimerStart) >= weapon.fireRate) {
            return Fire(weapon, isLocal, localPlayer, false, now, hooks) == FireResult::Fired;
        }
    }
    return false;
}

}  // namespace WorldRPG::Weapons
