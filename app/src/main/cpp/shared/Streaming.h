#pragma once

#include <cstdint>
#include <optional>

struct lua_State;

namespace WorldRPG::Models {

// CCore streaming-memory override. 0 means the 50 MB cvar
// (CClientVariables.cpp default "streaming_memory").
constexpr std::uint64_t kDefaultStreamingMemory = 50ull * 1024ull * 1024ull;

struct CacheLimits {
    bool vehiclesSet = false;
    bool pedsSet = false;
    std::uint64_t vehicles = 0;
    std::uint64_t peds = 0;
};

void SetStreamingMemory(std::uint64_t bytes);
void RestoreStreamingMemory();
std::uint64_t StreamingMemory();
void SetCacheLimits(std::optional<std::uint64_t> vehicles, std::optional<std::uint64_t> peds);
CacheLimits GetCacheLimits();

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
void BindStreamingLua(lua_State* L);
}
