#include "CullZones.h"

#include "LuaVMStub.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace WorldRPG::Models {
namespace {

constexpr std::size_t kAttributeCapacity = 4096;
constexpr std::size_t kTunnelCapacity = 256;
constexpr std::size_t kMirrorCapacity = 256;

struct NativeDef {
    std::int16_t cornerX = 0;
    std::int16_t cornerY = 0;
    std::int16_t vector1X = 0;
    std::int16_t vector1Y = 0;
    std::int16_t vector2X = 0;
    std::int16_t vector2Y = 0;
    std::int16_t minZ = 0;
    std::int16_t maxZ = 0;
};

struct Entry {
    std::uint32_t id = 0;
    CullZoneType type = CullZoneType::Attribute;
    CullZoneType originalType = CullZoneType::Attribute;
    NativeDef def;
    NativeDef originalDef;
    std::uint16_t flags = 0;
    std::uint16_t originalFlags = 0;
    float mirrorV = 0;
    float originalMirrorV = 0;
    std::int8_t normalX = 0;
    std::int8_t normalY = 0;
    std::int8_t normalZ = 100;
    std::int8_t originalNormalX = 0;
    std::int8_t originalNormalY = 0;
    std::int8_t originalNormalZ = 100;
    const void* owner = nullptr;
    bool enabled = true;
    bool original = false;
};

std::vector<Entry> g_entries;
std::uint32_t g_nextId = 1;
bool g_noted = false;

void NoteCatalog() {
    if (g_noted) return;
    g_noted = true;
    LUA_LOGI("DIAG engine cull zones: the GTA CULL IPL catalog is not loaded");
}

bool ToInt16(float value, std::int16_t& output) {
    if (!std::isfinite(value) || value < -32768.f || value > 32767.f) return false;
    output = static_cast<std::int16_t>(value);
    return true;
}

bool BuildNativeDef(const CullZone& input, NativeDef& output) {
    if (!std::isfinite(input.centerX) || !std::isfinite(input.centerY) || !std::isfinite(input.centerZ) || !std::isfinite(input.width) ||
        !std::isfinite(input.depth) || !std::isfinite(input.height) || !std::isfinite(input.rotationDegrees) || input.width <= 0.f ||
        input.depth <= 0.f || input.height <= 0.f)
        return false;
    const float radians = input.rotationDegrees * 3.14159265358979323846f / 180.f;
    const float halfWidth = input.width * 0.5f;
    const float halfDepth = input.depth * 0.5f;
    const float vector1X = std::cos(radians) * halfWidth;
    const float vector1Y = std::sin(radians) * halfWidth;
    const float vector2X = -std::sin(radians) * halfDepth;
    const float vector2Y = std::cos(radians) * halfDepth;
    return ToInt16(input.centerX - vector1X - vector2X, output.cornerX) && ToInt16(input.centerY - vector1Y - vector2Y, output.cornerY) &&
           ToInt16(vector1X * 2.f, output.vector1X) && ToInt16(vector1Y * 2.f, output.vector1Y) && ToInt16(vector2X * 2.f, output.vector2X) &&
           ToInt16(vector2Y * 2.f, output.vector2Y) && ToInt16(input.centerZ - input.height * 0.5f, output.minZ) &&
           ToInt16(input.centerZ + input.height * 0.5f, output.maxZ) && output.minZ < output.maxZ;
}

CullZone ToZone(const Entry& entry) {
    CullZone output;
    output.type = entry.type;
    output.centerX = entry.def.cornerX + (entry.def.vector1X + entry.def.vector2X) * 0.5f;
    output.centerY = entry.def.cornerY + (entry.def.vector1Y + entry.def.vector2Y) * 0.5f;
    output.centerZ = (entry.def.minZ + entry.def.maxZ) * 0.5f;
    output.width = std::hypot(static_cast<float>(entry.def.vector1X), static_cast<float>(entry.def.vector1Y));
    output.depth = std::hypot(static_cast<float>(entry.def.vector2X), static_cast<float>(entry.def.vector2Y));
    output.height = static_cast<float>(entry.def.maxZ - entry.def.minZ);
    output.rotationDegrees = std::atan2(static_cast<float>(entry.def.vector1Y), static_cast<float>(entry.def.vector1X)) * 180.f / 3.14159265358979323846f;
    output.flags = entry.type == CullZoneType::Mirror ? entry.flags & 0xFF : entry.flags;
    if (entry.type == CullZoneType::Mirror) {
        output.mirrorV = entry.mirrorV;
        output.mirrorNormalX = entry.normalX / 100.f;
        output.mirrorNormalY = entry.normalY / 100.f;
        output.mirrorNormalZ = entry.normalZ / 100.f;
    }
    output.id = entry.id;
    output.enabled = entry.enabled;
    output.original = entry.original;
    return output;
}

bool HasCapacity(CullZoneType type, const Entry* replaced = nullptr) {
    std::size_t count = 0;
    for (const Entry& entry : g_entries) {
        if (&entry != replaced && entry.enabled && entry.type == type) ++count;
    }
    const std::size_t capacity = type == CullZoneType::Attribute ? kAttributeCapacity : type == CullZoneType::Tunnel ? kTunnelCapacity : kMirrorCapacity;
    return count < capacity;
}

Entry* Find(std::uint32_t id) {
    for (Entry& entry : g_entries) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

bool CanClaim(Entry& entry, const void* owner) {
    if (!owner || (!entry.original && entry.owner != owner) || (entry.original && entry.owner && entry.owner != owner)) return false;
    entry.owner = owner;
    return true;
}

bool MirrorFits(const CullZone& definition) {
    return definition.flags <= 0xFF && std::isfinite(definition.mirrorV) && std::isfinite(definition.mirrorNormalX) &&
           std::isfinite(definition.mirrorNormalY) && std::isfinite(definition.mirrorNormalZ) && std::abs(definition.mirrorNormalX) <= 1.f &&
           std::abs(definition.mirrorNormalY) <= 1.f && std::abs(definition.mirrorNormalZ) <= 1.f;
}

}  // namespace

std::uint32_t CreateCullZone(const CullZone& definition, const void* owner) {
    NoteCatalog();
    if (!owner || !HasCapacity(definition.type)) return 0;
    Entry entry;
    entry.id = g_nextId++;
    entry.type = definition.type;
    entry.owner = owner;
    if (definition.type == CullZoneType::Mirror) {
        if (!MirrorFits(definition) || !BuildNativeDef(definition, entry.def)) return 0;
        entry.flags = definition.flags;
        entry.mirrorV = definition.mirrorV;
        entry.normalX = static_cast<std::int8_t>(definition.mirrorNormalX * 100.f);
        entry.normalY = static_cast<std::int8_t>(definition.mirrorNormalY * 100.f);
        entry.normalZ = static_cast<std::int8_t>(definition.mirrorNormalZ * 100.f);
    } else if (!BuildNativeDef(definition, entry.def)) {
        return 0;
    } else {
        entry.flags = definition.flags;
    }
    g_entries.push_back(entry);
    return entry.id;
}

bool SetCullZone(std::uint32_t id, const CullZone& definition, const void* owner) {
    NoteCatalog();
    Entry* entry = Find(id);
    if (!entry || !owner || (!entry->original && entry->owner != owner) || (entry->original && entry->owner && entry->owner != owner) ||
        (!entry->enabled && !HasCapacity(definition.type, entry)) ||
        (entry->enabled && entry->type != definition.type && !HasCapacity(definition.type, entry)))
        return false;
    NativeDef def;
    if (definition.type == CullZoneType::Mirror) {
        if (!MirrorFits(definition) || !BuildNativeDef(definition, def)) return false;
    } else if (!BuildNativeDef(definition, def)) {
        return false;
    }
    entry->owner = owner;
    entry->type = definition.type;
    entry->def = def;
    entry->flags = definition.flags;
    entry->enabled = true;
    if (definition.type == CullZoneType::Mirror) {
        entry->mirrorV = definition.mirrorV;
        entry->normalX = static_cast<std::int8_t>(definition.mirrorNormalX * 100.f);
        entry->normalY = static_cast<std::int8_t>(definition.mirrorNormalY * 100.f);
        entry->normalZ = static_cast<std::int8_t>(definition.mirrorNormalZ * 100.f);
    }
    return true;
}

bool SetCullZoneEnabled(std::uint32_t id, bool enabled, const void* owner) {
    NoteCatalog();
    Entry* entry = Find(id);
    if (!entry || !owner || (!entry->original && entry->owner != owner) || (entry->original && entry->owner && entry->owner != owner) ||
        (enabled && !entry->enabled && !HasCapacity(entry->type, entry)))
        return false;
    entry->owner = owner;
    entry->enabled = enabled;
    return true;
}

bool RemoveCullZone(std::uint32_t id, const void* owner) {
    NoteCatalog();
    for (auto it = g_entries.begin(); it != g_entries.end(); ++it) {
        if (it->id != id) continue;
        if (!CanClaim(*it, owner)) return false;
        if (it->original) it->enabled = false;
        else g_entries.erase(it);
        return true;
    }
    return false;
}

bool RestoreCullZone(std::uint32_t id, const void* owner) {
    NoteCatalog();
    Entry* entry = Find(id);
    if (!entry || !entry->original || entry->owner != owner || !HasCapacity(entry->originalType, entry)) return false;
    entry->type = entry->originalType;
    entry->def = entry->originalDef;
    entry->flags = entry->originalFlags;
    entry->mirrorV = entry->originalMirrorV;
    entry->normalX = entry->originalNormalX;
    entry->normalY = entry->originalNormalY;
    entry->normalZ = entry->originalNormalZ;
    entry->enabled = true;
    entry->owner = nullptr;
    return true;
}

std::vector<CullZone> ListCullZones(bool filtered, CullZoneType type) {
    NoteCatalog();
    std::vector<CullZone> list;
    for (const Entry& entry : g_entries) {
        if (!filtered || entry.type == type) list.push_back(ToZone(entry));
    }
    return list;
}

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
namespace {

std::string Bad(const char* function, const char* expected, int index, const std::string& got) {
    return std::string("Bad argument @ '") + function + "' [Expected " + expected + " at argument " + std::to_string(index) + ", got " + got + "]";
}

std::string Usage(const char* function, const char* reason) {
    return std::string("Bad usage @ '") + function + "' [" + reason + "]";
}

std::string Got(lua_State* L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TNONE: return "none";
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
        case LUA_TNUMBER: {
            const char* text = lua_tostring(L, index);
            return std::string("number (") + (text ? text : "") + ")";
        }
        case LUA_TSTRING: return std::string("string (\"") + (lua_tostring(L, index) ? lua_tostring(L, index) : "") + "\")";
        default: return lua_typename(L, lua_type(L, index));
    }
}

bool ReadNumber(lua_State* L, int index, const char* function, lua_Number& out, std::string& error, bool optional, lua_Number fallback) {
    const int type = lua_type(L, index);
    if (optional && (type == LUA_TNONE || type == LUA_TNIL)) {
        out = fallback;
        return true;
    }
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        error = Bad(function, "number", index, Got(L, index));
        return false;
    }
    if (!lua_isnumber(L, index)) {
        error = std::string("Bad argument @ '") + function + "' [Expected number, got non-convertible string]";
        return false;
    }
    out = lua_tonumber(L, index);
    if (std::isnan(out)) {
        error = std::string("Bad argument @ '") + function + "' [Expected number, got NaN]";
        return false;
    }
    if (out < 0 && !optional) {
        // Unsigned flag reads reject a negative. Signed coordinates do not.
    }
    return true;
}

const void* CurrentOwner() {
    const std::uint16_t net = ActiveVmOwnerNet();
    if (net == 0) return nullptr;
    static std::unordered_map<std::uint16_t, std::unique_ptr<int>> tokens;
    std::unique_ptr<int>& token = tokens[net];
    if (!token) token = std::make_unique<int>(net);
    return token.get();
}

bool ReadDefinition(lua_State* L, const char* function, int first, Models::CullZone& zone, std::string& error) {
    if (lua_type(L, first) != LUA_TSTRING) {
        error = Bad(function, "string", first, Got(L, first));
        return false;
    }
    const std::string type = lua_tostring(L, first) ? lua_tostring(L, first) : "";
    const bool known = type == "attribute" || type == "tunnel" || type == "mirror";
    if (type == "tunnel") zone.type = Models::CullZoneType::Tunnel;
    else if (type == "mirror") zone.type = Models::CullZoneType::Mirror;
    lua_Number value = 0;
    float* fields[] = {&zone.centerX, &zone.centerY, &zone.centerZ, &zone.width, &zone.depth, &zone.height};
    for (int i = 0; i < 6; ++i) {
        if (!ReadNumber(L, first + 1 + i, function, value, error, false, 0)) return false;
        *fields[i] = static_cast<float>(value);
    }
    if (!ReadNumber(L, first + 7, function, value, error, false, 0)) return false;
    if (value < 0) {
        error = std::string("Bad argument @ '") + function + "' [Expected positive value, got negative]";
        return false;
    }
    if (!known || value > 65535.0) {
        error = Usage(function, "Expected type 'attribute', 'tunnel', or 'mirror' and flags in range 0..65535");
        return false;
    }
    zone.flags = static_cast<std::uint16_t>(value);
    if (!ReadNumber(L, first + 8, function, value, error, true, 0)) return false;
    zone.rotationDegrees = static_cast<float>(value);
    if (!ReadNumber(L, first + 9, function, value, error, true, 0)) return false;
    zone.mirrorV = static_cast<float>(value);
    if (!ReadNumber(L, first + 10, function, value, error, true, 0)) return false;
    zone.mirrorNormalX = static_cast<float>(value);
    if (!ReadNumber(L, first + 11, function, value, error, true, 0)) return false;
    zone.mirrorNormalY = static_cast<float>(value);
    if (!ReadNumber(L, first + 12, function, value, error, true, 1)) return false;
    zone.mirrorNormalZ = static_cast<float>(value);
    return true;
}

const char* TypeName(Models::CullZoneType type) {
    switch (type) {
        case Models::CullZoneType::Tunnel: return "tunnel";
        case Models::CullZoneType::Mirror: return "mirror";
        default: return "attribute";
    }
}

void PushZone(lua_State* L, const Models::CullZone& zone) {
    lua_newtable(L);
    const auto number = [&](const char* name, double value) {
        lua_pushnumber(L, value);
        lua_setfield(L, -2, name);
    };
    number("id", zone.id);
    lua_pushstring(L, TypeName(zone.type));
    lua_setfield(L, -2, "type");
    number("x", zone.centerX);
    number("y", zone.centerY);
    number("z", zone.centerZ);
    number("width", zone.width);
    number("depth", zone.depth);
    number("height", zone.height);
    number("rotation", zone.rotationDegrees);
    number("flags", zone.flags);
    number("mirrorV", zone.mirrorV);
    number("normalX", zone.mirrorNormalX);
    number("normalY", zone.mirrorNormalY);
    number("normalZ", zone.mirrorNormalZ);
    lua_pushboolean(L, zone.enabled ? 1 : 0);
    lua_setfield(L, -2, "enabled");
    lua_pushboolean(L, zone.original ? 1 : 0);
    lua_setfield(L, -2, "original");
}

int GetZones(lua_State* L) {
    std::string filter;
    const int type = lua_type(L, 1);
    if (type == LUA_TNUMBER) {
        return luaL_error(L, "%s", Bad("engineGetCullZones", "string", 1, Got(L, 1)).c_str());
    }
    if (type == LUA_TSTRING) filter = lua_tostring(L, 1) ? lua_tostring(L, 1) : "";
    else if (type != LUA_TNONE && type != LUA_TNIL) {
        return luaL_error(L, "%s", Bad("engineGetCullZones", "string", 1, Got(L, 1)).c_str());
    }
    bool filtered = !filter.empty();
    Models::CullZoneType kind = Models::CullZoneType::Attribute;
    if (filtered) {
        if (filter == "attribute") kind = Models::CullZoneType::Attribute;
        else if (filter == "tunnel") kind = Models::CullZoneType::Tunnel;
        else if (filter == "mirror") kind = Models::CullZoneType::Mirror;
        else return luaL_error(L, "Expected type 'attribute', 'tunnel', or 'mirror'");
    }
    const std::vector<Models::CullZone> zones = Models::ListCullZones(filtered, kind);
    lua_newtable(L);
    for (std::size_t i = 0; i < zones.size(); ++i) {
        PushZone(L, zones[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

int CreateZone(lua_State* L) {
    Models::CullZone zone;
    std::string error;
    if (!ReadDefinition(L, "engineCreateCullZone", 1, zone, error)) return luaL_error(L, "%s", error.c_str());
    const std::uint32_t id = Models::CreateCullZone(zone, CurrentOwner());
    if (!id) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, id);
    return 1;
}

int SetZone(lua_State* L) {
    lua_Number id = 0;
    std::string error;
    if (!ReadNumber(L, 1, "engineSetCullZone", id, error, false, 0)) return luaL_error(L, "%s", error.c_str());
    Models::CullZone zone;
    if (!ReadDefinition(L, "engineSetCullZone", 2, zone, error)) return luaL_error(L, "%s", error.c_str());
    lua_pushboolean(L, Models::SetCullZone(static_cast<std::uint32_t>(id), zone, CurrentOwner()) ? 1 : 0);
    return 1;
}

int SetZoneEnabled(lua_State* L) {
    lua_Number id = 0;
    std::string error;
    if (!ReadNumber(L, 1, "engineSetCullZoneEnabled", id, error, false, 0)) return luaL_error(L, "%s", error.c_str());
    if (lua_type(L, 2) != LUA_TBOOLEAN) return luaL_error(L, "%s", Bad("engineSetCullZoneEnabled", "bool", 2, Got(L, 2)).c_str());
    lua_pushboolean(L, Models::SetCullZoneEnabled(static_cast<std::uint32_t>(id), lua_toboolean(L, 2) != 0, CurrentOwner()) ? 1 : 0);
    return 1;
}

int RemoveZone(lua_State* L) {
    lua_Number id = 0;
    std::string error;
    if (!ReadNumber(L, 1, "engineRemoveCullZone", id, error, false, 0)) return luaL_error(L, "%s", error.c_str());
    lua_pushboolean(L, Models::RemoveCullZone(static_cast<std::uint32_t>(id), CurrentOwner()) ? 1 : 0);
    return 1;
}

int RestoreZone(lua_State* L) {
    lua_Number id = 0;
    std::string error;
    if (!ReadNumber(L, 1, "engineRestoreCullZone", id, error, false, 0)) return luaL_error(L, "%s", error.c_str());
    lua_pushboolean(L, Models::RestoreCullZone(static_cast<std::uint32_t>(id), CurrentOwner()) ? 1 : 0);
    return 1;
}

}  // namespace

void BindCullZoneLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"engineGetCullZones", GetZones},
        {"engineCreateCullZone", CreateZone},
        {"engineSetCullZone", SetZone},
        {"engineSetCullZoneEnabled", SetZoneEnabled},
        {"engineRemoveCullZone", RemoveZone},
        {"engineRestoreCullZone", RestoreZone},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
