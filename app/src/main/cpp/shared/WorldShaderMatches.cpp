#include "WorldShaderMatches.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <set>
#include <utility>

namespace WorldRPG::WorldShaders {
namespace {

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// SMatchType / SWildcardMatchChain (CRenderWareSA.ShaderMatching.h:35-104).
struct MatchType {
    std::string match;
    bool additive = false;
};

struct MatchChain {
    std::vector<MatchType> list;

    // The last pattern that matches decides.
    bool IsAdditiveMatch(const std::string& textureName) const {
        bool isMatch = false;
        for (const MatchType& type : list)
            if (WildcardMatch(type.match.c_str(), textureName.c_str())) isMatch = type.additive;
        return isMatch;
    }

    void Append(const std::string& match, bool additive) {
        list.erase(std::remove_if(list.begin(), list.end(), [&](const MatchType& t) { return t.match == match; }), list.end());
        list.push_back({match, additive});
        // Subtractive matches at the start can never matter.
        while (!list.empty() && !list.front().additive) list.erase(list.begin());
    }
};

// CShaderAndEntityPair with its channel's chain. bAppendLayers is kept from the pair's
// first additive match: desktop's single-user channel path (GetChannelOnlyUsedBy) keeps
// the stored key.
struct Pair {
    MatchChain chain;
    bool appendLayers = true;
};

// SOrderValue: higher priority first, then later creation.
bool OrderLess(const ShaderInfo& a, const ShaderInfo& b) {
    return a.priority < b.priority || (a.priority == b.priority && a.createTime < b.createTime);
}

struct Instance {
    const ShaderInfo* info = nullptr;
    bool mix = false;  // SShaderInfoInstance::bMixEntityAndNonEntity
};

struct Result {
    Instance base;
    std::vector<Instance> layers;
};

std::mutex g_mutex;
std::map<uint32_t, ShaderInfo> g_shaders;                     // m_ShaderInfoMap
std::map<std::pair<uint32_t, uint32_t>, Pair> g_pairs;       // (shader, entity) -> m_ChannelUsageMap
std::set<uint32_t> g_knownEntities;                           // m_KnownClientEntities
uint64_t g_revision = 1;

// CMatchChannel::GetBestShaderForEntity for every pair, i.e. CalcShaderForTexAndEntity.
Result Calc(const std::string& textureName, uint32_t entity, int entityType) {
    Result out;
    for (const auto& [key, pair] : g_pairs) {
        if (key.second != entity) continue;
        const ShaderInfo& info = g_shaders.at(key.first);
        if (!(info.typeMask & entityType) || !pair.chain.IsAdditiveMatch(textureName)) continue;
        if (!info.layered) {
            if (!out.base.info || OrderLess(*out.base.info, info)) out.base = {&info, pair.appendLayers};
        } else {
            out.layers.push_back({&info, pair.appendLayers});
        }
    }
    return out;
}

// CMatchChannelManager::FinalizeLayers.
Layers Finalize(Result& result) {
    std::sort(result.layers.begin(), result.layers.end(),
              [](const Instance& a, const Instance& b) { return OrderLess(*a.info, *b.info); });
    Layers out;
    if (result.base.info) {
        out.base = result.base.info->shader;
        out.usesVertexShader = result.base.info->usesVertexShader;
    }
    for (const Instance& layer : result.layers) {
        out.layers.push_back(layer.info->shader);
        out.usesVertexShader |= layer.info->usesVertexShader;
    }
    return out;
}

void Touch() { ++g_revision; }

}  // namespace

bool WildcardMatch(const char* wild, const char* string) {
    // SharedUtil::WildcardMatch, written by Jack Handy.
    const char *cp = nullptr, *mp = nullptr;
    while (*string && *wild != '*') {
        if (*wild != *string && *wild != '?') return false;
        wild++;
        string++;
    }
    while (*string) {
        if (*wild == '*') {
            if (!*++wild) return true;
            mp = wild;
            cp = string + 1;
        } else if (*wild == *string || *wild == '?') {
            wild++;
            string++;
        } else {
            wild = mp;
            string = cp++;
        }
    }
    while (*wild == '*') wild++;
    return !*wild;
}

void AppendAdditive(const ShaderInfo& info, uint32_t entity, const std::string& pattern, bool appendLayers) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_shaders.emplace(info.shader, info);  // GetShaderInfo(..., bAddIfRequired): first values stay
    auto [it, inserted] = g_pairs.try_emplace({info.shader, entity});
    if (inserted) it->second.appendLayers = appendLayers;
    it->second.chain.Append(Lower(pattern), true);
    if (entity) g_knownEntities.insert(entity);
    Touch();
}

void AppendSubtractive(uint32_t shader, uint32_t entity, const std::string& pattern) {
    std::lock_guard<std::mutex> lock(g_mutex);
    // Nothing to remove from a shader or pair that never had an additive match.
    auto it = g_pairs.find({shader, entity});
    if (it == g_pairs.end()) return;
    it->second.chain.Append(Lower(pattern), false);
    Touch();
}

void RemoveShaderRefs(uint32_t shader) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_shaders.erase(shader)) return;
    for (auto it = g_pairs.begin(); it != g_pairs.end();) {
        if (it->first.first == shader) it = g_pairs.erase(it);
        else ++it;
    }
    Touch();
}

void RemoveEntityRefs(uint32_t entity) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!entity || !g_knownEntities.erase(entity)) return;
    for (auto it = g_pairs.begin(); it != g_pairs.end();) {
        if (it->first.second == entity) it = g_pairs.erase(it);
        else ++it;
    }
    Touch();
}

std::vector<uint32_t> KnownEntities() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return {g_knownEntities.begin(), g_knownEntities.end()};
}

Layers Resolve(const std::string& textureName, uint32_t entity, int entityType) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::string name = Lower(textureName);
    // Ignore unknown client entities (GetShaderForTexAndEntity).
    if (entity && !g_knownEntities.count(entity)) entity = 0;
    Result result = Calc(name, entity, entityType);
    if (entity) {
        // UpdateTexShaderReplacement: inherit what the all-entities result marks to mix.
        const Result all = Calc(name, 0, entityType);
        if (!result.base.info && all.base.info && all.base.mix) result.base = all.base;
        for (const Instance& layer : all.layers)
            if (layer.mix) result.layers.push_back(layer);
    }
    return Finalize(result);
}

bool Active() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return !g_pairs.empty();
}

uint64_t Revision() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_revision;
}

void Reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_shaders.clear();
    g_pairs.clear();
    g_knownEntities.clear();
    Touch();
}

}  // namespace WorldRPG::WorldShaders
