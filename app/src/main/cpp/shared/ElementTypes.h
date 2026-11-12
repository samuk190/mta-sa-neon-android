#pragma once

#include <cstddef>
#include <cstdint>

namespace WorldRPG::Sync {

/// Element type identifiers matching MTA's element hierarchy
enum class ElementType : uint8_t {
    PLAYER = 0,
    PED,
    VEHICLE,
    OBJECT,
    PICKUP,
    BLIP,
    RADAR_AREA,
    COUNT
};

/// Unique element identifier (server-assigned)
using ElementID = uint16_t;

constexpr ElementID INVALID_ELEMENT_ID = 0xFFFF;

/// Maximum pool sizes — fixed at compile time, no dynamic growth during gameplay
constexpr size_t MAX_PLAYERS = 256;
constexpr size_t MAX_VEHICLES = 2000;
constexpr size_t MAX_OBJECTS = 1000;
constexpr size_t MAX_PEDS = 256;
constexpr size_t MAX_PICKUPS = 512;
constexpr size_t MAX_BLIPS = 256;
constexpr size_t MAX_RADAR_AREAS = 64;

/// Element lifecycle state
enum class ElementState : uint8_t {
    FREE = 0,      ///< Slot is unused
    CREATED,       ///< Element created, awaiting first sync
    SYNCING,       ///< Actively receiving sync updates
    DESTROYED      ///< Marked for removal, pending cleanup
};

} // namespace WorldRPG::Sync