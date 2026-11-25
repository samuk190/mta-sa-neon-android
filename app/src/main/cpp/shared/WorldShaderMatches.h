#pragma once

// Which shaders replace which world textures (engineApplyShaderToWorldTexture /
// engineRemoveShaderFromWorldTexture). Port of desktop CMatchChannelManager
// (Client/game_sa/CRenderWareSA.ShaderMatching.cpp/.h) and SWildcardMatchChain:
// the same match chains and the same base/layer result, computed directly instead
// of through its channel cache. The renderer asks Resolve for a texture it is about
// to draw; the Lua thread changes the matches. Guarded by one mutex.

#include <cstdint>
#include <string>
#include <vector>

namespace WorldRPG::WorldShaders {

// EEntityTypeMask (Client/sdk/game/CRenderWare.h:57-66).
enum TypeMask : int { kNone = 0, kWorld = 1, kPed = 2, kVehicle = 4, kObject = 8, kOther = 16, kAll = 127 };

// SShaderInfo (CRenderWareSA.ShaderSupport.h:68-84): fixed when the shader is first applied.
struct ShaderInfo {
    uint32_t shader = 0;      // dxCreateShader element id
    float priority = 0;       // dxCreateShader priority
    uint32_t createTime = 0;  // CShaderItem::m_uiCreateTime
    bool layered = false;
    int typeMask = kNone;
    bool usesVertexShader = false;
};

// SShaderItemLayers: base replaces the texture, layers draw over it, lowest order first.
struct Layers {
    uint32_t base = 0;
    std::vector<uint32_t> layers;
    bool usesVertexShader = false;
    bool operator==(const Layers&) const = default;
};

// SharedUtil::WildcardMatch (SharedUtil.Misc.h:1654-1699): '*' and '?'.
bool WildcardMatch(const char* wild, const char* text);

// CMatchChannelManager::AppendAdditiveMatch / AppendSubtractiveMatch. entity 0 is all
// entities. Patterns and texture names are compared in lower case.
void AppendAdditive(const ShaderInfo& info, uint32_t entity, const std::string& pattern, bool appendLayers);
void AppendSubtractive(uint32_t shader, uint32_t entity, const std::string& pattern);
// CMatchChannelManager::RemoveShaderRefs / RemoveClientEntityRefs.
void RemoveShaderRefs(uint32_t shader);
void RemoveEntityRefs(uint32_t entity);
// Entities with a match (m_KnownClientEntities), so stale ones can be dropped.
std::vector<uint32_t> KnownEntities();

// CMatchChannelManager::GetShaderForTexAndEntity: the shaders for a texture drawn
// for an entity of entityType (one TypeMask bit). An entity with no matches of its
// own uses the all-entities result.
Layers Resolve(const std::string& textureName, uint32_t entity, int entityType);

// True while any match exists, so the renderer can skip the lookup.
bool Active();
// Changes whenever a match is added or removed (renderer caches).
uint64_t Revision();
void Reset();

}  // namespace WorldRPG::WorldShaders
