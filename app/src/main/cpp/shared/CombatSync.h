#pragma once

// Combat sync (S6): bullet packet decoders and damage registration.
// Read orders follow mta_neon CNetAPI::ReadBulletsync / ReadWeaponBulletsync.

#include <cstddef>
#include <cstdint>

namespace WorldRPG::Net {
class BitStreamReader;
}

namespace WorldRPG::Combat {

// PLAYER_BULLETSYNC (98), after the 17-bit ElementID of the shooter:
// weapon u8, start 3xfloat, end 3xfloat, order u8, hit bit,
// [damage float, zone u8, victim ElementID(17 bits)] when hit.
struct SBulletSync {
    uint32_t shooter = 0;
    uint8_t weapon = 0;
    uint8_t order = 0;
    float start[3] = {0.f, 0.f, 0.f};
    float end[3] = {0.f, 0.f, 0.f};
    bool hit = false;
    float damage = 0.f;
    uint8_t zone = 0;
    uint32_t victim = 0xFFFFFFFFu;
};

// WEAPON_BULLETSYNC (100): ElementID of the weapon, start, end, order u8.
struct SWeaponBulletSync {
    uint32_t weaponElement = 0;
    uint8_t order = 0;
    float start[3] = {0.f, 0.f, 0.f};
    float end[3] = {0.f, 0.f, 0.f};
};

bool ReadPlayerBulletSync(WorldRPG::Net::BitStreamReader& reader, SBulletSync& out);
bool ReadWeaponBulletSync(WorldRPG::Net::BitStreamReader& reader, SWeaponBulletSync& out);

// Reference duplicate filter: same vectors and no newer order counter means duplicate.
// Updates the per-shooter state. Returns false for duplicates.
bool AcceptPlayerBulletSync(const SBulletSync& bullet);

// Damage to the local player only, and only from a packet hit with a damage value.
// No damage is fabricated from a weapon table.
void ApplyBulletDamage(const SBulletSync& bullet, uint32_t localPlayer);

// Victim health from PLAYER_PURESYNC. Remote players only. The shooter's
// record is never written from this value.
void RegisterVictimHealth(uint32_t id, uint8_t health, uint32_t localPlayer);

// Reference CClientWeaponManager::HasWeaponBulletSync: weapon ids 22..34.
bool WeaponUsesBulletSync(uint8_t weapon);

} // namespace WorldRPG::Combat
