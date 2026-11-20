#include "PedQueries.h"
#include "../gtasa/GameEngine.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace WorldRPG::PedQueries {
namespace {
struct Entry { Snapshot state; };
std::mutex mutex;
std::unordered_map<uint32_t, Entry> entries;
std::unordered_map<uint32_t, Presentation> presentation;
uint64_t generation = 0;
Entry New(bool player, const Entity::PedSpawn* spawn) {
    Entry e;
    e.state.generation = ++generation;
    e.state.player = player;
    if (spawn) {
        e.state.health = spawn->health;
        e.state.currentSlot = spawn->currentSlot < Slots ? spawn->currentSlot : 0;
        for (const auto& w : spawn->weapons) {
            if (w.slot >= Slots) continue;
            e.state.weapons[w.slot].type = w.type;
            e.state.weapons[w.slot].total = w.totalAmmo;
            e.state.cachedAmmo[w.slot] = w.totalAmmo;
        }
    }
    return e;
}
bool Valid(uint32_t id) { return id && id != UINT32_MAX; }
void ApplyPresentation(uint32_t id, Snapshot& out) {
    auto found = presentation.find(id);
    if (found == presentation.end()) return;
    const Presentation& flags = found->second;
    out.onFire = flags.onFire;
    out.inWater = flags.inWater;
    out.ducked = flags.ducked;
    out.choking = flags.choking;
    out.aim = flags.aim;
    out.aimX = flags.aimX;
    out.aimY = flags.aimY;
    out.aimZ = flags.aimZ;
}
Presentation& Flags(uint32_t id) { return presentation[id]; }
}
// CWeaponNames.cpp:94-97. Includes the unused/invalid weapon holes.
int WeaponSlot(unsigned type) {
    static constexpr int slots[] = {0,0,1,1,1,1,1,1,1,1,10,10,10,-1,10,10,
        8,8,8,-1,-1,-1,2,2,2,3,3,3,4,4,5,5,4,6,6,7,7,7,7,8,12,9,9,9,11,11,11};
    return type < std::size(slots) ? slots[type] : -1;
}
void Ensure(uint32_t id, bool player) {
    if (!Valid(id)) return;
    std::lock_guard lock(mutex);
    if (!entries.count(id)) entries.emplace(id, New(player, nullptr));
}
void Create(uint32_t id, bool player, const Entity::PedSpawn* spawn) {
    if (!Valid(id)) return;
    std::lock_guard lock(mutex);
    entries[id] = New(player, spawn);
}
void Forget(uint32_t id) {
    std::lock_guard lock(mutex);
    entries.erase(id);
    presentation.erase(id);
}
void Reset() {
    std::lock_guard lock(mutex);
    entries.clear();
    presentation.clear();
}
bool Copy(uint32_t id, Snapshot& out) {
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    if (it == entries.end()) return false;
    out = it->second.state;
    ApplyPresentation(id, out);
    return true;
}
bool CopyRestore(uint32_t id, Snapshot& out) {
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    if (it == entries.end()) return false;
    out = it->second.state;
    for (unsigned i = 0; i < Slots; ++i) out.weapons[i].total = out.cachedAmmo[i];
    ApplyPresentation(id, out);
    return true;
}
bool Current(uint32_t id, uint64_t generation) {
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    return it != entries.end() && it->second.state.generation == generation;
}
uint64_t Generation(uint32_t id) {
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    return it == entries.end() ? 0 : it->second.state.generation;
}
// CWeaponRPCs.cpp:26-260; CClientPed.cpp:3989-4240. The cached total belongs
// to CCLIENTPED; a player's unstreamed inventory does not acquire ped-only ammo.
uint64_t WeaponRpc(uint32_t id, const Game::PedRpcOp& op) {
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    if (it == entries.end()) return 0;
    auto& s = it->second.state;
    using K = Game::PedRpcOp::Kind;
    const int slot = WeaponSlot(op.weapon);
    switch (op.kind) {
        case K::GiveWeapon:
            if (slot < 0) return 0;
            s.weapons[slot].type = op.weapon;
            if (!s.player && op.weapon) s.cachedAmmo[slot] = static_cast<uint16_t>(s.cachedAmmo[slot] + op.ammo);
            if (op.setCurrent) s.currentSlot = slot;
            break;
        case K::TakeWeaponAmmo:
            if (slot >= 0 && !s.player) s.cachedAmmo[slot] -= std::min<unsigned>(op.ammo, s.cachedAmmo[slot]);
            break;
        case K::SetWeaponAmmo:
            if (slot >= 0 && !s.player) s.cachedAmmo[slot] = static_cast<uint16_t>(op.ammo);
            break;
        case K::RemoveWeapon:
            for (unsigned i = 0; i < Slots; ++i) if (s.weapons[i].type == static_cast<unsigned>(op.weapon)) {
                s.weapons[i] = {};
                s.cachedAmmo[i] = 0;
                if (s.currentSlot == i) s.currentSlot = 0;
            }
            break;
        case K::TakeAllWeapons:
            s.weapons = {};
            s.cachedAmmo = {};
            s.currentSlot = 0;
            break;
        case K::SetWeaponSlot:
            if (op.slot >= 0 && op.slot < static_cast<int>(Slots)) s.currentSlot = op.slot;
            break;
        default: return 0;
    }
    return s.generation;
}
Presentation ReadPresentation(uint32_t id) {
    if (!Valid(id)) return {};
    std::lock_guard lock(mutex);
    auto found = presentation.find(id);
    return found == presentation.end() ? Presentation{} : found->second;
}
void OnFire(uint32_t id, bool onFire) {
    if (!Valid(id)) return;
    std::lock_guard lock(mutex);
    Flags(id).onFire = onFire;
}
void InWater(uint32_t id, bool inWater) {
    if (!Valid(id)) return;
    std::lock_guard lock(mutex);
    Flags(id).inWater = inWater;
}
void Keys(uint32_t id, bool ducked, bool choking) {
    if (!Valid(id)) return;
    std::lock_guard lock(mutex);
    Presentation& flags = Flags(id);
    flags.ducked = ducked;
    flags.choking = choking;
}
void Aim(uint32_t id, float x, float y, float z) {
    if (!Valid(id)) return;
    std::lock_guard lock(mutex);
    Presentation& flags = Flags(id);
    flags.aim = true;
    flags.aimX = x;
    flags.aimY = y;
    flags.aimZ = z;
}
void Health(uint32_t id, float health, bool clearDead) {
    if (!std::isfinite(health)) return;
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    if (it == entries.end()) return;
    it->second.state.health = std::max(health, 0.f);
    if (clearDead && health > 0.f) it->second.state.dead = false;
}
void Wasted(uint32_t id) {
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    if (it == entries.end()) return;
    auto& s = it->second.state;
    s.dead = true;
    s.health = 0.f;
    s.networkDead = s.player;
}
void Spawn(uint32_t id) {
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    if (it != entries.end()) it->second = New(it->second.state.player, nullptr);
    presentation.erase(id);
}
void StreamOut(uint32_t id, const Snapshot& native) {
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    if (it == entries.end() || native.generation != it->second.state.generation || !native.streamed) return;
    auto& s = it->second.state;
    // _DestroyModel stores ammo only for known ped weapons (CClientPed.cpp:6258-6274).
    s.currentSlot = native.currentSlot;
    if (!s.player) for (unsigned i = 0; i < Slots; ++i)
        if (s.weapons[i].type && s.weapons[i].type == native.weapons[i].type)
            s.cachedAmmo[i] = static_cast<uint16_t>(native.weapons[i].total);
}
bool IsDead(const Snapshot& s) {
    // CClientPed::IsDead/IsDying (3774-3804), CLuaPedDefs::IsPedDead (2168-2198).
    bool dead = s.streamed && s.taskType >= 0 ? s.taskType == 218 : s.dead;
    dead = dead || (s.streamed && s.taskType >= 212 && s.taskType <= 217);
    return dead || (s.player && s.networkDead && s.health <= 0.f);
}
bool Ammo(const Snapshot& s, unsigned slot, bool clip, uint16_t& out) {
    if (slot >= Slots) return false;
    if (clip) {
        if (!s.streamed) return false;
        out = static_cast<uint16_t>(s.weapons[slot].clip);
        return true;
    }
    if (!s.streamed && !s.cachedAmmo[slot]) return false;
    const bool hasAmmo = slot != 0 && slot != 1 && slot != 10 && slot != 11 && slot != 12;
    out = hasAmmo ? static_cast<uint16_t>(s.streamed ? s.weapons[slot].total : s.cachedAmmo[slot]) : 1;
    return true;
}
bool SetCurrentSlot(uint32_t id, unsigned slot, bool player) {
    if (!Valid(id) || slot >= Slots) return false;
    std::lock_guard lock(mutex);
    auto it = entries.find(id);
    if (it == entries.end()) it = entries.emplace(id, New(player, nullptr)).first;
    it->second.state.currentSlot = slot;
    return true;
}
}
