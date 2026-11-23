#include "CombatSync.h"

#include "../gtasa/GameEngine.h"
#include "../gtasa/RemoteCombat.h"
#include "../net/raknet/RakNetWrapper.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
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

std::atomic<int> g_bulletDiag{0};
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

std::mutex g_stateMutex;
bool g_interrupted = false;
std::chrono::steady_clock::time_point g_backupUntil{};
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
        if (!reader.Read(weapon)) return false;
        mask |= WeaponBit(weapon);
    }
    g_bulletSyncWeapons.store(mask, std::memory_order_relaxed);
    if (g_settingsDiag++ < kMaxDiagLines)
        COMBAT_LOGI("DIAG SYNC_SETTINGS bullet sync weapon mask=0x%llx count=%u", static_cast<unsigned long long>(mask), count);
    return true;
}

void ResetBulletSync() {
    std::lock_guard lock(g_stateMutex);
    g_interrupted = false;
    g_backupUntil = {};
    g_prevBullets.clear();
    g_victimHealth.clear();
    g_bulletSyncWeapons.store(kDefaultBulletSyncWeapons, std::memory_order_relaxed);
}

void ForgetShooter(uint32_t id) {
    std::lock_guard lock(g_stateMutex);
    g_prevBullets.erase(id);
    g_victimHealth.erase(id);
}

void WriteBulletHit(Net::BitStreamWriter& w, const BulletHit& hit) {
    // CBulletsyncPacket::ReadOptionalDamage rejects out-of-range backup damage.
    // An unavailable backup must still leave a valid trajectory for the victim to trace.
    const bool valid = hit.victim > 0 && hit.victim < 0x1FFFFu && hit.damage > 0.f &&
                       hit.damage <= 200.f && hit.zone <= 9;
    const uint8_t present = valid;
    w.WriteBits(&present, 1);
    if (valid) {
        w.Write(hit.damage);
        w.Write(hit.zone);
        w.WriteBits(reinterpret_cast<const uint8_t*>(&hit.victim), kElementIdBits);
    }
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
    std::lock_guard lock(g_stateMutex);
    SPrevBullet& prev = g_prevBullets[bullet.shooter];
    bool duplicate = SameVector(bullet.start, prev.start) && SameVector(bullet.end, prev.end);
    std::memcpy(prev.start, bullet.start, sizeof(prev.start));
    std::memcpy(prev.end, bullet.end, sizeof(prev.end));
    // Reference: static_cast<char>(order - prevOrder) > 0 means a newer counter.
    if (static_cast<int8_t>(static_cast<uint8_t>(bullet.order - prev.order)) > 0) duplicate = false;
    prev.order = bullet.order;
    return !duplicate;
}

bool ApplyBackupDamage(float amount, float& health, float& armor) {
    if (!(amount > 0.f && amount <= 200.f) || !(health > 0.f) || !std::isfinite(health) ||
        !std::isfinite(armor) || armor < 0.f) return false;
    const float absorbed = std::min(armor, amount);
    armor -= absorbed;
    health = std::max(0.f, health - (amount - absorbed));
    return true;
}

void SetLocalNetworkInterrupted(bool interrupted) {
    std::lock_guard lock(g_stateMutex);
    g_interrupted = interrupted;
    if (!interrupted) g_backupUntil = std::chrono::steady_clock::now() + std::chrono::seconds(1);
}

float RemainingBackupDamage(const SBulletSync& bullet, uint32_t localPlayer, float nativeDamage,
                            std::chrono::steady_clock::time_point now) {
    if (!bullet.hit || !localPlayer || bullet.victim != localPlayer || bullet.shooter == localPlayer ||
        !(bullet.damage > 0.f && bullet.damage <= 200.f) || bullet.zone > 9 ||
        !std::isfinite(nativeDamage) || nativeDamage < 0.f) return 0.f;
    std::lock_guard lock(g_stateMutex);
    if (!g_interrupted && now >= g_backupUntil) return 0.f;
    return std::max(0.f, bullet.damage - nativeDamage);
}

void RegisterVictimHealth(uint32_t id, uint8_t health, uint32_t localPlayer) {
    if (id == localPlayer || id == kInvalidElement) return;
    std::lock_guard lock(g_stateMutex);
    auto it = g_victimHealth.find(id);
    if (it != g_victimHealth.end() && it->second == health) return;
    g_victimHealth[id] = health;
    SetRemoteHealth(id, health);
    if (g_healthDiag++ < kMaxDiagLines)
        COMBAT_LOGI("DIAG VICTIM_HEALTH registered ped=%u health=%u", id, health);
}

} // namespace WorldRPG::Combat
