#pragma once
#include "shared/PedQueries.h"
#include "shared/CombatSync.h"
#include "shared/VehicleSync.h"
#include <optional>
namespace WorldRPG::Headless {
// Opt-in native snapshot substitute. Normal headless clients have no GTA body.
void SetPedQueryNative(uint32_t id, const PedQueries::Snapshot& snapshot);
void ClearPedQueryNative();
struct PlayerCombatUpdate {
    std::optional<PlayerWeaponSync::RemoteWeapon> weapon;
    uint32_t player = 0;
    uint8_t buttons = 0;
    bool fire = false;
    std::optional<Combat::SBulletSync> bullet;
};
PlayerCombatUpdate TakePlayerCombatUpdate();

struct PedSyncFields {
    uint32_t id = 0;
    std::optional<VehicleSync::Vec3> position;
    std::optional<float> heading;
    std::optional<VehicleSync::Vec3> velocity;
};
PedSyncFields TakePedSyncFields();
std::optional<VehicleSync::UnoccupiedUpdate> TakeUnoccupiedUpdate();
}
