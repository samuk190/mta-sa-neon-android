#pragma once

#include <cstdint>

namespace WorldRPG::Entity {

// CPacketHandler::Packet_EntityAdd WEAPON tail (CPacketHandler.cpp:3258-3345).
// Snapshot only: no native weapon is created and no firing is simulated.
struct WeaponSpawn {
    uint8_t targetType = 2;  // eTargetType: vector=0, entity=1, fixed=2
    uint32_t target = 0xFFFFFFFFu;
    uint8_t subTarget = 0;
    float targetX = 0, targetY = 0, targetZ = 0;
    bool statsChanged = false;
    uint16_t damage = 0;
    float accuracy = 0, targetRange = 0, weaponRange = 0;
    bool flags[13] = {};
    uint8_t state = 0;
    uint16_t ammo = 0, clipAmmo = 0;
    uint32_t owner = 0xFFFFFFFFu;
};

} // namespace WorldRPG::Entity
