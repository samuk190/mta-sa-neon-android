#include "CombatSync.h"

#include "../gtasa/GameEngine.h"
#include "../gtasa/RemoteCombat.h"
#include "../net/raknet/RakNetWrapper.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#ifdef __ANDROID__
#include <android/log.h>
#define COMBAT_LOG_TAG "WorldRPGNeon"
#define COMBAT_LOGI(...) __android_log_print(ANDROID_LOG_INFO, COMBAT_LOG_TAG, __VA_ARGS__)
#else
#define COMBAT_LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif

namespace WorldRPG::Combat {

namespace {

constexpr uint32_t kElementIdBits = 17;
constexpr uint32_t kInvalidElement = 0xFFFFFFFFu;
constexpr int kMaxDiagLines = 24;

int g_bulletDiag = 0;
int g_weaponDiag = 0;
int g_healthDiag = 0;
int g_settingsDiag = 0;

// Same 17-bit ElementID mapping as PacketHandler.cpp ReadElementId.
bool ReadElementId(WorldRPG::Net::BitStreamReader& reader, uint32_t& out) {
    uint8_t bytes[4] = {};
    if (!reader.ReadBits(bytes, kElementIdBits)) return false;
    uint32_t value = 0;
    std::memcpy(&value, bytes, 4);
    out = value == ((1u << kElementIdBits) - 1) ? kInvalidElement : value;
    return true;
}

bool ReadFlag(WorldRPG::Net::BitStreamReader& reader, bool& out) {
    uint8_t bit = 0;
    if (!reader.ReadBits(&bit, 1)) return false;
    out = (bit & 1u) != 0;
    return true;
}

bool ReadVector(WorldRPG::Net::BitStreamReader& reader, float v[3]) {
    return reader.Read(v[0]) && reader.Read(v[1]) && reader.Read(v[2]);
}

bool IsFiniteVector(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// Per-shooter duplicate state (reference CNetAPI m_vecPrevBulletSync*, m_ucPrevBulletSyncOrderCounter).
struct SPrevBullet {
    float start[3] = {0.f, 0.f, 0.f};
    float end[3] = {0.f, 0.f, 0.f};
    uint8_t order = 0;
};

std::unordered_map<uint32_t, SPrevBullet> g_prevBullets;
std::unordered_map<uint32_t, uint8_t> g_victimHealth;

bool SameVector(const float a[3], const float b[3]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

// Weapon ids 0..63 as one bit each. Ids above 63 are not representable and never report bullet sync.
constexpr uint64_t WeaponBit(uint8_t weapon) {
    return weapon < 64 ? (uint64_t{1} << weapon) : 0;
}

// Reference default (CClientWeaponManager::HasWeaponBulletSync range) until SYNC_SETTINGS arrives.
constexpr uint64_t kDefaultBulletSyncWeapons = [] {
    uint64_t mask = 0;
    for (uint8_t weapon = 22; weapon <= 34; ++weapon) mask |= WeaponBit(weapon);
    return mask;
}();

// Sender set from SYNC_SETTINGS. Written on the network thread, read on the game thread.
std::atomic<uint64_t> g_bulletSyncWeapons{kDefaultBulletSyncWeapons};

} // namespace

bool WeaponUsesBulletSync(uint8_t weapon) {
    return (g_bulletSyncWeapons.load(std::memory_order_relaxed) & WeaponBit(weapon)) != 0;
}

bool ReadSyncSettingsWeapons(WorldRPG::Net::BitStreamReader& reader) {
    uint8_t count = 0;
    if (!reader.Read(count)) return false;
    uint64_t mask = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint8_t weapon = 0;
        if (!reader.Read(weapon)) break; // desktop keeps the ids read so far
        mask |= WeaponBit(weapon);
    }
    g_bulletSyncWeapons.store(mask, std::memory_order_relaxed);
    if (g_settingsDiag++ < kMaxDiagLines)
        COMBAT_LOGI("DIAG SYNC_SETTINGS bullet sync weapon mask=0x%llx count=%u", static_cast<unsigned long long>(mask), count);
    return true;
}

bool ReceivesBulletSyncWeapon(uint8_t weapon) {
    return weapon >= 22 && weapon <= 34;
}

bool ReadPlayerBulletSync(WorldRPG::Net::BitStreamReader& reader, SBulletSync& out) {
    const size_t bitOffset = reader.GetReadOffsetBits();
    if (!ReadElementId(reader, out.shooter) || !reader.Read(out.weapon)) {
        if (g_bulletDiag++ < kMaxDiagLines)
            COMBAT_LOGI("DIAG BULLET_SYNC header read failed shooter=%u bit=%zu", out.shooter, bitOffset);
        return false;
    }
    if (!ReceivesBulletSyncWeapon(out.weapon)) {
        if (g_bulletDiag++ < kMaxDiagLines)
            COMBAT_LOGI("DIAG BULLET_SYNC shooter=%u weapon=%u has no bullet sync (ignored) bit=%zu", out.shooter, out.weapon, bitOffset);
        return false;
    }
    if (!ReadVector(reader, out.start) || !ReadVector(reader, out.end) || !IsFiniteVector(out.start) || !IsFiniteVector(out.end)) {
        if (g_bulletDiag++ < kMaxDiagLines)
            COMBAT_LOGI("DIAG BULLET_SYNC vectors invalid shooter=%u weapon=%u bit=%zu", out.shooter, out.weapon, bitOffset);
        return false;
    }
    if (!reader.Read(out.order)) {
        if (g_bulletDiag++ < kMaxDiagLines)
            COMBAT_LOGI("DIAG BULLET_SYNC order read failed shooter=%u bit=%zu", out.shooter, bitOffset);
        return false;
    }
    out.hit = false;
    if (!ReadFlag(reader, out.hit)) {
        if (g_bulletDiag++ < kMaxDiagLines)
            COMBAT_LOGI("DIAG BULLET_SYNC hit flag read failed shooter=%u bit=%zu", out.shooter, bitOffset);
        return false;
    }
    if (out.hit && (!reader.Read(out.damage) || !reader.Read(out.zone) || !ReadElementId(reader, out.victim))) {
        if (g_bulletDiag++ < kMaxDiagLines)
            COMBAT_LOGI("DIAG BULLET_SYNC hit body read failed shooter=%u bit=%zu", out.shooter, bitOffset);
        return false;
    }
    return true;
}

bool ReadWeaponBulletSync(WorldRPG::Net::BitStreamReader& reader, SWeaponBulletSync& out) {
    const size_t bitOffset = reader.GetReadOffsetBits();
    if (!ReadElementId(reader, out.weaponElement)) {
        if (g_weaponDiag++ < kMaxDiagLines)
            COMBAT_LOGI("DIAG WEAPON_BULLET_SYNC element read failed bit=%zu", bitOffset);
        return false;
    }
    if (!ReadVector(reader, out.start) || !ReadVector(reader, out.end) || !IsFiniteVector(out.start) || !IsFiniteVector(out.end)) {
        if (g_weaponDiag++ < kMaxDiagLines)
            COMBAT_LOGI("DIAG WEAPON_BULLET_SYNC vectors invalid element=%u bit=%zu", out.weaponElement, bitOffset);
        return false;
    }
    if (!reader.Read(out.order)) {
        if (g_weaponDiag++ < kMaxDiagLines)
            COMBAT_LOGI("DIAG WEAPON_BULLET_SYNC order read failed element=%u bit=%zu", out.weaponElement, bitOffset);
        return false;
    }
    return true;
}

bool AcceptPlayerBulletSync(const SBulletSync& bullet) {
    SPrevBullet& prev = g_prevBullets[bullet.shooter];
    bool duplicate = SameVector(bullet.start, prev.start) && SameVector(bullet.end, prev.end);
    std::memcpy(prev.start, bullet.start, sizeof(prev.start));
    std::memcpy(prev.end, bullet.end, sizeof(prev.end));
    // Reference: static_cast<char>(order - prevOrder) > 0 means a newer counter.
    if (static_cast<int8_t>(static_cast<uint8_t>(bullet.order - prev.order)) > 0) duplicate = false;
    prev.order = bullet.order;
    return !duplicate;
}

void ApplyBulletDamage(const SBulletSync& bullet, uint32_t localPlayer) {
    if (!bullet.hit || bullet.victim != localPlayer || bullet.shooter == localPlayer) return;
    if (!(bullet.damage > 0.f)) {
        if (g_bulletDiag++ < kMaxDiagLines)
            COMBAT_LOGI("DIAG BULLET_SYNC hit on local without damage value shooter=%u weapon=%u", bullet.shooter, bullet.weapon);
        return;
    }
    WorldRPG::Game::NoteDamage(bullet.shooter, bullet.damage, bullet.weapon);
    if (g_bulletDiag++ < kMaxDiagLines)
        COMBAT_LOGI("DIAG BULLET_SYNC local hit shooter=%u weapon=%u damage=%.1f zone=%u", bullet.shooter, bullet.weapon, bullet.damage, bullet.zone);
}

void RegisterVictimHealth(uint32_t id, uint8_t health, uint32_t localPlayer) {
    if (id == localPlayer || id == kInvalidElement) return;
    auto it = g_victimHealth.find(id);
    if (it != g_victimHealth.end() && it->second == health) return;
    g_victimHealth[id] = health;
    SetRemoteHealth(id, health);
    if (g_healthDiag++ < kMaxDiagLines)
        COMBAT_LOGI("DIAG VICTIM_HEALTH registered ped=%u health=%u", id, health);
}

} // namespace WorldRPG::Combat
