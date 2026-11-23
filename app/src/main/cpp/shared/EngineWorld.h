#pragma once

#include <cstdint>

struct lua_State;

namespace WorldRPG::Models {

// CPoolsSA::GetPoolDefaultModdedCapacity (CPoolsSA.cpp:879-924).
// engineGetPoolDefaultCapacity returns these values, not the vanilla sizes.
constexpr int kPoolCount = 20;
constexpr int kBuildingPool = 0;
constexpr int kPointerSingleLinkPool = 16;

enum class PoolSetDecision { BelowDefault, BelowUsed, NotChangeable, Resize };

int PoolDefaultCapacity(int pool);
PoolSetDecision DecidePoolCapacity(int pool, std::uint32_t size, std::uint32_t used);

void SetAsyncLoading(bool enabled, bool forced);
bool AsyncLoadingEnabled();
bool AsyncLoadingForced();

// CLuaPostfxDefs.cpp:29-69 and CClientVariables.cpp:432-442.
enum class PostFX { Gamma, Brightness, Contrast, Saturation };
float PostFXValue(PostFX type);
bool PostFXEnabled(PostFX type);
int PostFXMode();
void SetPostFXValue(PostFX type, float value);
void SetPostFXEnabled(PostFX type, bool enabled);
void SetPostFXMode(bool fullscreen, bool windowed);

// CClientObjectManager.cpp:39-40 and 305-330. The shared cap is MAX_OBJECTS_MTA (1000).
constexpr std::uint32_t kMaxStreamedObjects = 1000;
bool SetObjectStreamingLimits(std::uint32_t objects, std::uint32_t lowLod);
std::uint32_t MaxStreamedObjects();
std::uint32_t MaxLowLodObjects();

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
void BindEngineWorldLua(lua_State* L);
}
