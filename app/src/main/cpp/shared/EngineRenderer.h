#pragma once

#include <cstdint>

struct lua_State;

namespace WorldRPG::Models {

// CCoronasSA.cpp:250-258 and 907-957. Capacity is MAX_DISTANT_LIGHT_CORONAS.
constexpr float kDistantLightDistance = 2000.f;
constexpr std::uint32_t kDistantLightCapacity = 25000;
constexpr float kDistantLightMin = 300.f;
constexpr float kDistantLightMax = 5000.f;

struct DistantLights {
    bool enabled = false;
    float distance = kDistantLightDistance;
    bool automatic = true;
};

const DistantLights& GetDistantLights();
bool SetDistantLightDistance(float distance);
void SetDistantLightsEnabled(bool enabled);
void SetDistantLightsAutomatic(bool enabled);

// CMultiplayer.h:226-237 and Common.h:34-36.
constexpr std::uint32_t kVisibleEntityCapacity = 8192;
constexpr std::uint32_t kVisibleLodCapacity = 8192;
constexpr std::uint32_t kStreamingObjectCapacity = 30000;

struct RendererStats {
    std::uint32_t visibleEntities = 0;
    std::uint32_t visibleEntityHighWater = 0;
    std::uint32_t visibleLods = 0;
    std::uint32_t visibleLodHighWater = 0;
    std::uint32_t streamingRwObjects = 0;
    std::uint32_t streamingRwObjectHighWater = 0;
};

RendererStats GetRendererStats();
void ResetRendererStats();

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
void BindEngineRendererLua(lua_State* L);
}
