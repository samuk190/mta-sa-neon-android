#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct lua_State;

namespace WorldRPG::Models {

enum class CullZoneType : std::uint8_t { Attribute, Tunnel, Mirror };

struct CullZone {
    std::uint32_t id = 0;
    CullZoneType type = CullZoneType::Attribute;
    float centerX = 0;
    float centerY = 0;
    float centerZ = 0;
    float width = 0;
    float depth = 0;
    float height = 0;
    float rotationDegrees = 0;
    std::uint16_t flags = 0;
    float mirrorV = 0;
    float mirrorNormalX = 0;
    float mirrorNormalY = 0;
    float mirrorNormalZ = 1;
    bool enabled = true;
    bool original = false;
};

std::uint32_t CreateCullZone(const CullZone& definition, const void* owner);
bool SetCullZone(std::uint32_t id, const CullZone& definition, const void* owner);
bool SetCullZoneEnabled(std::uint32_t id, bool enabled, const void* owner);
bool RemoveCullZone(std::uint32_t id, const void* owner);
bool RestoreCullZone(std::uint32_t id, const void* owner);
std::vector<CullZone> ListCullZones(bool filtered, CullZoneType type);

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
void BindCullZoneLua(lua_State* L);
}
