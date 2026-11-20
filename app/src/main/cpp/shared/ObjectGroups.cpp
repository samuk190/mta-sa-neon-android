#include "ObjectGroups.h"

#include "LuaVMStub.h"
#include "../gtasa/GameTasks.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace WorldRPG::Models {
namespace {

float ReadFloat(const unsigned char* bytes, std::size_t offset) {
    float value = 0.f;
    std::memcpy(&value, bytes + offset, sizeof value);
    return value;
}

void WriteFloat(unsigned char* bytes, std::size_t offset, float value) {
    std::memcpy(bytes + offset, &value, sizeof value);
}

const char* kProperties[] = {
    "mass", "turn_mass", "air_resistance", "elasticity", "buoyancy", "uproot_limit", "col_damage_multiplier",
    "col_damage_effect", "special_col_response", "avoid_camera", "cause_explosion", "fx_type", "fx_offset",
    "fx_system", "smash_multiplier", "break_velocity", "break_velocity_randomness", "break_mode", "sparks_on_impact",
};

std::string Lower(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'A' && c <= 'Z') ch = static_cast<char>(c + 32);
    }
    return out;
}

#if defined(__ANDROID__)
std::unordered_map<std::uint16_t, ModelGroupRecord> g_modelGroups;
std::unordered_map<int, GroupOriginal> g_objectGroups;

bool GroupAbi() {
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return false;
    const auto* init = static_cast<const std::uint32_t*>(dlsym(lib, "_ZN11CObjectData10InitialiseEPcb"));
    const auto* apply = static_cast<const std::uint32_t*>(dlsym(lib, "_ZN11CObjectData13SetObjectDataEiR7CObject"));
    void* table = dlsym(lib, "_ZN11CObjectData14ms_aObjectInfoE");
    void* models = dlsym(lib, "_ZN10CModelInfo16ms_modelInfoPtrsE");
    dlclose(lib);
    // Initialise+0x64 is mov w2, #0x58. SetObjectData+0xc is ldrsh [model, #0x2a].
    const bool stride = init && init[25] == 0x52800b02u;
    const bool index = apply && apply[3] == 0x7980552au;
    return stride && index && table && models;
}

unsigned char* GroupSlot(int groupId) {
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return nullptr;
    auto* table = static_cast<unsigned char*>(dlsym(lib, "_ZN11CObjectData14ms_aObjectInfoE"));
    dlclose(lib);
    if (!table || groupId < 0 || groupId >= kObjectGroupCount) return nullptr;
    return table + static_cast<std::size_t>(groupId) * kObjectGroupBytes;
}

void** ModelSlotTable() {
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return nullptr;
    auto** models = static_cast<void**>(dlsym(lib, "_ZN10CModelInfo16ms_modelInfoPtrsE"));
    dlclose(lib);
    return models;
}
#endif

}  // namespace

bool GroupProperty(std::string_view text, GroupValueKind& kind) {
    const std::string lower = Lower(text);
    int index = -1;
    for (int i = 0; i < static_cast<int>(sizeof(kProperties) / sizeof(kProperties[0])); ++i) {
        if (text == kProperties[i] || lower == kProperties[i]) index = i;
    }
    if (index < 0) return false;
    if (index <= 6 || index == 14 || index == 16) kind = GroupValueKind::Float;
    else if (index == 7) kind = GroupValueKind::Damage;
    else if (index == 8) kind = GroupValueKind::Response;
    else if (index == 9 || index == 10 || index == 18) kind = GroupValueKind::Bool;
    else if (index == 11) kind = GroupValueKind::FxType;
    else if (index == 12 || index == 15) kind = GroupValueKind::Vector;
    else if (index == 13) kind = GroupValueKind::FxSystem;
    else kind = GroupValueKind::BreakMode;
    return true;
}

void ReadObjectGroup(const void* raw, ObjectGroup& group) {
    const auto* bytes = static_cast<const unsigned char*>(raw);
    group.mass = ReadFloat(bytes, 0);
    group.turnMass = ReadFloat(bytes, 4);
    group.airResistance = ReadFloat(bytes, 8);
    group.elasticity = ReadFloat(bytes, 12);
    group.buoyancy = ReadFloat(bytes, 16);
    group.uprootLimit = ReadFloat(bytes, 20);
    group.colDamageMultiplier = ReadFloat(bytes, 24);
    group.colDamageEffect = bytes[28];
    group.specialColResponse = bytes[29];
    group.cameraAvoid = bytes[30];
    group.causesExplosion = bytes[31];
    group.fxType = bytes[32];
    group.fxOffset[0] = ReadFloat(bytes, kGroupFxOffset);
    group.fxOffset[1] = ReadFloat(bytes, kGroupFxOffset + 4);
    group.fxOffset[2] = ReadFloat(bytes, kGroupFxOffset + 8);
    void* fx = nullptr;
    std::memcpy(&fx, bytes + kGroupFxPointer, sizeof fx);
    group.haveFx = fx != nullptr;
    group.smashMultiplier = ReadFloat(bytes, kGroupSmash);
    group.breakVelocity[0] = ReadFloat(bytes, kGroupBreakVelocity);
    group.breakVelocity[1] = ReadFloat(bytes, kGroupBreakVelocity + 4);
    group.breakVelocity[2] = ReadFloat(bytes, kGroupBreakVelocity + 8);
    group.breakVelocityRand = ReadFloat(bytes, kGroupBreakVelocityRand);
    std::memcpy(&group.breakMode, bytes + kGroupBreakMode, sizeof group.breakMode);
    std::memcpy(&group.sparksOnImpact, bytes + kGroupSparks, sizeof group.sparksOnImpact);
}

void WriteObjectGroup(const ObjectGroup& group, void* raw) {
    auto* bytes = static_cast<unsigned char*>(raw);
    WriteFloat(bytes, 0, group.mass);
    WriteFloat(bytes, 4, group.turnMass);
    WriteFloat(bytes, 8, group.airResistance);
    WriteFloat(bytes, 12, group.elasticity);
    WriteFloat(bytes, 16, group.buoyancy);
    WriteFloat(bytes, 20, group.uprootLimit);
    WriteFloat(bytes, 24, group.colDamageMultiplier);
    bytes[28] = group.colDamageEffect;
    bytes[29] = group.specialColResponse;
    bytes[30] = group.cameraAvoid;
    bytes[31] = group.causesExplosion;
    bytes[32] = group.fxType;
    WriteFloat(bytes, kGroupFxOffset, group.fxOffset[0]);
    WriteFloat(bytes, kGroupFxOffset + 4, group.fxOffset[1]);
    WriteFloat(bytes, kGroupFxOffset + 8, group.fxOffset[2]);
    WriteFloat(bytes, kGroupSmash, group.smashMultiplier);
    WriteFloat(bytes, kGroupBreakVelocity, group.breakVelocity[0]);
    WriteFloat(bytes, kGroupBreakVelocity + 4, group.breakVelocity[1]);
    WriteFloat(bytes, kGroupBreakVelocity + 8, group.breakVelocity[2]);
    WriteFloat(bytes, kGroupBreakVelocityRand, group.breakVelocityRand);
    std::memcpy(bytes + kGroupBreakMode, &group.breakMode, sizeof group.breakMode);
    std::memcpy(bytes + kGroupSparks, &group.sparksOnImpact, sizeof group.sparksOnImpact);
}

void RememberGroup(GroupOriginal& saved, const void* bytes) {
    if (saved.saved) return;
    std::memcpy(saved.bytes, bytes, kObjectGroupBytes);
    saved.saved = true;
}

bool NearFloat(float left, float right) { return std::fabs(left - right) < 0.0001f; }

void AssignGroupFloat(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, float value) {
    if (NearFloat(ReadFloat(bytes, offset), value)) return;
    RememberGroup(saved, bytes);
    WriteFloat(bytes, offset, value);
}

void AssignGroupByte(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, std::uint8_t value) {
    if (bytes[offset] == value) return;
    RememberGroup(saved, bytes);
    bytes[offset] = value;
}

void AssignGroupBoolByte(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, bool value) {
    if ((bytes[offset] != 0) == value) return;
    RememberGroup(saved, bytes);
    bytes[offset] = value ? 1 : 0;
}

void AssignGroupVector(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, const float value[3]) {
    if (NearFloat(ReadFloat(bytes, offset), value[0]) && NearFloat(ReadFloat(bytes, offset + 4), value[1]) &&
        NearFloat(ReadFloat(bytes, offset + 8), value[2]))
        return;
    RememberGroup(saved, bytes);
    WriteFloat(bytes, offset, value[0]);
    WriteFloat(bytes, offset + 4, value[1]);
    WriteFloat(bytes, offset + 8, value[2]);
}

void AssignGroupWord(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, std::uint32_t value) {
    std::uint32_t current = 0;
    std::memcpy(&current, bytes + offset, sizeof current);
    if (current == value) return;
    RememberGroup(saved, bytes);
    std::memcpy(bytes + offset, &value, sizeof value);
}

void AssignGroupBoolWord(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, bool value) {
    std::uint32_t current = 0;
    std::memcpy(&current, bytes + offset, sizeof current);
    if ((current != 0) == value) return;
    RememberGroup(saved, bytes);
    const std::uint32_t stored = value ? 1u : 0u;
    std::memcpy(bytes + offset, &stored, sizeof stored);
}

void ClearGroupFx(unsigned char* bytes, GroupOriginal& saved) {
    void* fx = nullptr;
    std::memcpy(&fx, bytes + kGroupFxPointer, sizeof fx);
    if (!fx) return;
    RememberGroup(saved, bytes);
    void* clear = nullptr;
    std::memcpy(bytes + kGroupFxPointer, &clear, sizeof clear);
}

std::int16_t GroupIndexFromHalf(std::uint16_t bits) { return static_cast<std::int16_t>(bits); }

std::uint16_t HalfFromGroupIndex(int group) { return static_cast<std::uint16_t>(static_cast<std::int16_t>(group)); }

bool SetModelGroup(ModelGroupRecord& record, int group) {
    if (group < -1 || group > 159) return false;
    const std::int16_t next = static_cast<std::int16_t>(group);
    if (next == record.current) return true;
    if (!record.saved) {
        record.original = record.current;
        record.saved = true;
    }
    record.current = next;
    return true;
}

bool RestoreModelGroup(ModelGroupRecord& record) {
    if (!record.saved) return true;
    record.current = record.original;
    record.saved = false;
    return true;
}

bool ReadLiveModelGroup(std::uint16_t modelId, std::int16_t& group) {
    if (modelId >= 20000) return false;
#if !defined(__ANDROID__)
    (void)group;
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abi = false;
        std::call_once(once, [] { abi = GroupAbi(); });
        if (!abi) return;
        void** models = ModelSlotTable();
        if (!models || !models[modelId]) return;
        std::uint16_t bits = 0;
        std::memcpy(&bits, static_cast<unsigned char*>(models[modelId]) + 0x2a, sizeof bits);
        group = GroupIndexFromHalf(bits);
        ok = true;
    });
    return ran && ok;
#endif
}

bool WriteLiveModelGroup(std::uint16_t modelId, int group) {
    if (modelId >= 20000 || group < -1 || group > 159) return false;
#if !defined(__ANDROID__)
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abi = false;
        std::call_once(once, [] { abi = GroupAbi(); });
        if (!abi) return;
        void** models = ModelSlotTable();
        if (!models || !models[modelId]) return;
        std::uint16_t bits = 0;
        std::memcpy(&bits, static_cast<unsigned char*>(models[modelId]) + 0x2a, sizeof bits);
        ModelGroupRecord record = g_modelGroups.count(modelId) ? g_modelGroups[modelId] : ModelGroupRecord{};
        record.current = GroupIndexFromHalf(bits);
        if (!SetModelGroup(record, group)) return;
        bits = HalfFromGroupIndex(record.current);
        std::memcpy(static_cast<unsigned char*>(models[modelId]) + 0x2a, &bits, sizeof bits);
        g_modelGroups[modelId] = record;
        ok = true;
    });
    return ran && ok;
#endif
}

bool RestoreLiveModelGroup(std::uint16_t modelId) {
    if (modelId >= 20000) return false;
#if !defined(__ANDROID__)
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abi = false;
        std::call_once(once, [] { abi = GroupAbi(); });
        if (!abi) return;
        void** models = ModelSlotTable();
        if (!models || !models[modelId]) return;
        auto found = g_modelGroups.find(modelId);
        if (found != g_modelGroups.end() && found->second.saved) {
            const std::uint16_t bits = HalfFromGroupIndex(found->second.original);
            std::memcpy(static_cast<unsigned char*>(models[modelId]) + 0x2a, &bits, sizeof bits);
            g_modelGroups.erase(found);
        }
        ok = true;
    });
    return ran && ok;
#endif
}

bool ReadLiveObjectGroup(int groupId, ObjectGroup& group) {
    if (groupId < 0 || groupId >= kObjectGroupCount) return false;
#if !defined(__ANDROID__)
    (void)group;
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abi = false;
        std::call_once(once, [] { abi = GroupAbi(); });
        if (!abi) return;
        unsigned char* slot = GroupSlot(groupId);
        if (!slot) return;
        ReadObjectGroup(slot, group);
        ok = true;
    });
    return ran && ok;
#endif
}

bool WriteLiveObjectGroup(int groupId, const ObjectGroup& group, bool remember) {
    if (groupId < 0 || groupId >= kObjectGroupCount) return false;
#if !defined(__ANDROID__)
    (void)group;
    (void)remember;
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abi = false;
        std::call_once(once, [] { abi = GroupAbi(); });
        if (!abi) return;
        unsigned char* slot = GroupSlot(groupId);
        if (!slot) return;
        if (remember) RememberGroup(g_objectGroups[groupId], slot);
        void* fx = nullptr;
        std::memcpy(&fx, slot + 48, sizeof fx);
        WriteObjectGroup(group, slot);
        if (group.haveFx) std::memcpy(slot + 48, &fx, sizeof fx);
        else {
            void* clear = nullptr;
            std::memcpy(slot + 48, &clear, sizeof clear);
        }
        ok = true;
    });
    return ran && ok;
#endif
}

bool RestoreLiveObjectGroup(int groupId) {
    if (groupId < 0 || groupId >= kObjectGroupCount) return false;
#if !defined(__ANDROID__)
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abi = false;
        std::call_once(once, [] { abi = GroupAbi(); });
        if (!abi) return;
        unsigned char* slot = GroupSlot(groupId);
        if (!slot) return;
        auto found = g_objectGroups.find(groupId);
        if (found != g_objectGroups.end() && found->second.saved) {
            std::memcpy(slot, found->second.bytes, kObjectGroupBytes);
            g_objectGroups.erase(found);
        }
        ok = true;
    });
    return ran && ok;
#endif
}

bool MutateLiveObjectGroup(int groupId, void (*apply)(unsigned char*, GroupOriginal&, void*), void* user) {
    if (groupId < 0 || groupId >= kObjectGroupCount || !apply) return false;
#if !defined(__ANDROID__)
    (void)user;
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abi = false;
        std::call_once(once, [] { abi = GroupAbi(); });
        if (!abi) return;
        unsigned char* slot = GroupSlot(groupId);
        if (!slot) return;
        GroupOriginal saved;
        const auto found = g_objectGroups.find(groupId);
        if (found != g_objectGroups.end()) saved = found->second;
        apply(slot, saved, user);
        if (saved.saved) g_objectGroups[groupId] = saved;
        ok = true;
    });
    return ran && ok;
#endif
}

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
namespace {

std::string Bad(const char* function, const char* expected, int index, const std::string& got) {
    return std::string("Bad argument @ '") + function + "' [Expected " + expected + " at argument " +
           std::to_string(index) + ", got " + got + "]";
}

std::string Describe(lua_State* L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TNONE: return "none";
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
        case LUA_TNUMBER: {
            const char* text = lua_tostring(L, index);
            return std::string("number (") + (text ? text : "") + ")";
        }
        case LUA_TSTRING: return std::string("string (\"") + (lua_tostring(L, index) ? lua_tostring(L, index) : "") + "\")";
        case LUA_TTABLE: return "table";
        default: return lua_typename(L, lua_type(L, index));
    }
}

std::string Usage(const char* function, const char* reason) {
    return std::string("Bad usage @ '") + function + "' [" + reason + "]";
}

std::string Argument(const char* function, const char* reason) {
    return std::string("Bad argument @ '") + function + "' [" + reason + "]";
}

bool ReadScriptNumber(lua_State* L, int index, const char* function, lua_Number& out, std::string& error) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        error = Bad(function, "number", index, Describe(L, index));
        return false;
    }
    if (!lua_isnumber(L, index)) {
        error = Argument(function, "Expected number, got non-convertible string");
        return false;
    }
    out = lua_tonumber(L, index);
    if (std::isnan(out)) {
        error = Argument(function, "Expected number, got NaN");
        return false;
    }
    if (std::isinf(out)) {
        error = Argument(function, "Expected number, got inf");
        return false;
    }
    return true;
}

bool ReadInt(lua_State* L, int index, const char* function, int& out, std::string& error) {
    lua_Number number = 0;
    if (!ReadScriptNumber(L, index, function, number, error)) return false;
    out = static_cast<int>(number);
    return true;
}

bool ReadBoundedModel(lua_State* L, const char* function, int& model, std::string& error) {
    const int type = lua_type(L, 1);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        error = Bad(function, "number", 1, Describe(L, 1));
        return false;
    }
    if (!lua_isnumber(L, 1)) {
        error = Argument(function, "Expected number, got non-convertible string");
        return false;
    }
    const lua_Number value = lua_tonumber(L, 1);
    if (std::isnan(value)) {
        error = Argument(function, "Expected number, got NaN");
        return false;
    }
    if (!std::isfinite(value) || value < 0.0 || value > 65534.0) {
        error = Argument(function, "Expected number within the supported range");
        return false;
    }
    model = static_cast<int>(value);
    return true;
}

bool ModelInObjectTable(const char* function, int model, std::string& error) {
    if (model >= 42341) {
        error = "Expected a valid model ID at argument 1";
        return false;
    }
    if (model >= 20000) {
        error = Usage(function, "Expected model ID in range [0-19999] at argument 1");
        return false;
    }
    return true;
}

const char* DamageName(std::uint8_t value) {
    switch (value) {
        case 0: return "none";
        case 1: return "change_model";
        case 20: return "smash";
        case 21: return "change_smash";
        case 200: return "breakable";
        case 202: return "breakable_remove";
        default: return nullptr;
    }
}

const char* ResponseName(std::uint8_t value) {
    const char* names[] = {"none", "lamppost", "small_box", "big_box", "fence_part", "grenade", "swingdoor", "lockdoor", "hanging", "poolball"};
    return value < 10 ? names[value] : nullptr;
}

const char* FxName(std::uint8_t value) {
    const char* names[] = {"none", "play_on_hit", "play_on_destroyed", "play_on_hitdestroyed"};
    return value < 4 ? names[value] : nullptr;
}

const char* BreakName(std::uint32_t value) {
    const char* names[] = {"not_by_gun", "by_gun", "smashable"};
    return value < 3 ? names[value] : nullptr;
}

void PushVector(lua_State* L, const float* value) {
    lua_newtable(L);
    lua_pushnumber(L, value[0]); lua_setfield(L, -2, "x");
    lua_pushnumber(L, value[1]); lua_setfield(L, -2, "y");
    lua_pushnumber(L, value[2]); lua_setfield(L, -2, "z");
}

int GetObjectGroupProperty(lua_State* L) {
    int groupId = 0;
    std::string error;
    if (!ReadInt(L, 1, "engineGetObjectGroupPhysicalProperty", groupId, error)) return luaL_error(L, "%s", error.c_str());
    if (lua_type(L, 2) != LUA_TSTRING) return luaL_error(L, "%s", Bad("engineGetObjectGroupPhysicalProperty", "objectgroup-modifiable", 2, Describe(L, 2)).c_str());
    Models::GroupValueKind kind;
    if (!Models::GroupProperty(lua_tostring(L, 2), kind))
        return luaL_error(L, "%s", Bad("engineGetObjectGroupPhysicalProperty", "objectgroup-modifiable", 2, Describe(L, 2)).c_str());
    if (groupId < 0 || groupId > 159)
        return luaL_error(L, "%s", Usage("engineGetObjectGroupPhysicalProperty", "Expected group ID in range [0-159] at argument 1").c_str());
    if (kind == Models::GroupValueKind::FxSystem)
        return luaL_error(L, "%s", Usage("engineGetObjectGroupPhysicalProperty", "Fx system name isn't possible to be extracted.").c_str());
    Models::ObjectGroup group;
    if (!Models::ReadLiveObjectGroup(groupId, group))
        return luaL_error(L, "%s", Usage("engineGetObjectGroupPhysicalProperty", "Expected valid group ID at argument 1").c_str());
    std::string key = lua_tostring(L, 2) ? lua_tostring(L, 2) : "";
    for (char& ch : key) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'A' && c <= 'Z') ch = static_cast<char>(c + 32);
    }
    switch (kind) {
        case Models::GroupValueKind::Float: {
            float selected = group.mass;
            if (key == "turn_mass") selected = group.turnMass;
            else if (key == "air_resistance") selected = group.airResistance;
            else if (key == "elasticity") selected = group.elasticity;
            else if (key == "buoyancy") selected = group.buoyancy;
            else if (key == "uproot_limit") selected = group.uprootLimit;
            else if (key == "col_damage_multiplier") selected = group.colDamageMultiplier;
            else if (key == "smash_multiplier") selected = group.smashMultiplier;
            else if (key == "break_velocity_randomness") selected = group.breakVelocityRand;
            lua_pushnumber(L, selected);
            return 1;
        }
        case Models::GroupValueKind::Bool:
            lua_pushboolean(L, key == "cause_explosion" ? group.causesExplosion != 0 :
                               key == "sparks_on_impact" ? group.sparksOnImpact != 0 : group.cameraAvoid != 0);
            return 1;
        case Models::GroupValueKind::Vector:
            PushVector(L, key == "break_velocity" ? group.breakVelocity : group.fxOffset);
            return 1;
        case Models::GroupValueKind::Damage: {
            const char* name = DamageName(group.colDamageEffect);
            if (!name) return luaL_error(L, " @ 'engineGetObjectGroupPhysicalProperty' [No error]");
            lua_pushstring(L, name);
            return 1;
        }
        case Models::GroupValueKind::Response: {
            const char* name = ResponseName(group.specialColResponse);
            if (!name) return luaL_error(L, " @ 'engineGetObjectGroupPhysicalProperty' [No error]");
            lua_pushstring(L, name);
            return 1;
        }
        case Models::GroupValueKind::FxType: {
            const char* name = FxName(group.fxType);
            if (!name) return luaL_error(L, " @ 'engineGetObjectGroupPhysicalProperty' [No error]");
            lua_pushstring(L, name);
            return 1;
        }
        case Models::GroupValueKind::BreakMode: {
            const char* name = BreakName(group.breakMode);
            if (!name) return luaL_error(L, " @ 'engineGetObjectGroupPhysicalProperty' [No error]");
            lua_pushstring(L, name);
            return 1;
        }
        case Models::GroupValueKind::FxSystem:
            break;
    }
    return luaL_error(L, "Expected valid group ID at argument 1");
}

int GetModelPhysicalGroup(lua_State* L) {
    int model = 0;
    std::string error;
    if (!ReadBoundedModel(L, "engineGetModelPhysicalPropertiesGroup", model, error)) return luaL_error(L, "%s", error.c_str());
    if (!ModelInObjectTable("engineGetModelPhysicalPropertiesGroup", model, error)) return luaL_error(L, "%s", error.c_str());
    std::int16_t group = -1;
    if (!Models::ReadLiveModelGroup(static_cast<std::uint16_t>(model), group))
        return luaL_error(L, "%s", Usage("engineGetModelPhysicalPropertiesGroup", "Expected valid model ID at argument 1").c_str());
    lua_pushnumber(L, group);
    return 1;
}

}  // namespace

int SetModelPhysicalGroup(lua_State* L) {
    int model = 0;
    int group = 0;
    std::string error;
    if (!ReadBoundedModel(L, "engineSetModelPhysicalPropertiesGroup", model, error)) return luaL_error(L, "%s", error.c_str());
    if (!ReadInt(L, 2, "engineSetModelPhysicalPropertiesGroup", group, error)) return luaL_error(L, "%s", error.c_str());
    if (!ModelInObjectTable("engineSetModelPhysicalPropertiesGroup", model, error)) return luaL_error(L, "%s", error.c_str());
    if (group < -1 || group > 159)
        return luaL_error(L, "%s", Usage("engineSetModelPhysicalPropertiesGroup", "Expected group ID in range [0-159] or -1 at argument 1").c_str());
    if (!Models::WriteLiveModelGroup(static_cast<std::uint16_t>(model), group))
        return luaL_error(L, "%s", Usage("engineSetModelPhysicalPropertiesGroup", "Expected valid model ID at argument 1").c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int RestoreModelPhysicalGroup(lua_State* L) {
    int model = 0;
    std::string error;
    if (!ReadBoundedModel(L, "engineRestoreModelPhysicalPropertiesGroup", model, error)) return luaL_error(L, "%s", error.c_str());
    if (!ModelInObjectTable("engineRestoreModelPhysicalPropertiesGroup", model, error)) return luaL_error(L, "%s", error.c_str());
    if (!Models::RestoreLiveModelGroup(static_cast<std::uint16_t>(model)))
        return luaL_error(L, "%s", Usage("engineRestoreModelPhysicalPropertiesGroup", "Expected valid model ID at argument 1").c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int RestoreObjectGroup(lua_State* L) {
    int groupId = 0;
    std::string error;
    if (!ReadInt(L, 1, "engineRestoreObjectGroupPhysicalProperties", groupId, error)) return luaL_error(L, "%s", error.c_str());
    if (groupId < 0 || groupId > 159)
        return luaL_error(L, "%s", Usage("engineRestoreObjectGroupPhysicalProperties", "Expected group ID in range [0-159] at argument 1").c_str());
    if (!Models::RestoreLiveObjectGroup(groupId))
        return luaL_error(L, "%s", Usage("engineRestoreObjectGroupPhysicalProperties", "Expected valid group ID at argument 1").c_str());
    lua_pushboolean(L, 1);
    return 1;
}

enum class AssignKind { Float, Byte, BoolByte, Vector, Word, BoolWord, ClearFx };

struct AssignRequest {
    AssignKind kind = AssignKind::Float;
    std::size_t offset = 0;
    float scalar = 0;
    float vector[3] = {};
    std::uint32_t word = 0;
    bool flag = false;
};

void ApplyAssign(unsigned char* bytes, Models::GroupOriginal& saved, void* user) {
    const auto* request = static_cast<const AssignRequest*>(user);
    switch (request->kind) {
        case AssignKind::Float: Models::AssignGroupFloat(bytes, saved, request->offset, request->scalar); break;
        case AssignKind::Byte: Models::AssignGroupByte(bytes, saved, request->offset, static_cast<std::uint8_t>(request->word)); break;
        case AssignKind::BoolByte: Models::AssignGroupBoolByte(bytes, saved, request->offset, request->flag); break;
        case AssignKind::Vector: Models::AssignGroupVector(bytes, saved, request->offset, request->vector); break;
        case AssignKind::Word: Models::AssignGroupWord(bytes, saved, request->offset, request->word); break;
        case AssignKind::BoolWord: Models::AssignGroupBoolWord(bytes, saved, request->offset, request->flag); break;
        case AssignKind::ClearFx: Models::ClearGroupFx(bytes, saved); break;
    }
}

std::string LowerKey(const char* text) {
    std::string key = text ? text : "";
    for (char& ch : key) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'A' && c <= 'Z') ch = static_cast<char>(c + 32);
    }
    return key;
}

bool MatchName(const std::string& key, const char* const* names, int count, int& out) {
    for (int i = 0; i < count; ++i) {
        if (key == names[i]) {
            out = i;
            return true;
        }
    }
    return false;
}

bool DamageValue(const std::string& key, std::uint8_t& out) {
    if (key == "none") out = 0;
    else if (key == "change_model") out = 1;
    else if (key == "smash") out = 20;
    else if (key == "change_smash") out = 21;
    else if (key == "breakable") out = 200;
    else if (key == "breakable_remove") out = 202;
    else return false;
    return true;
}

std::size_t FloatOffset(const std::string& key) {
    if (key == "turn_mass") return Models::kGroupTurnMass;
    if (key == "air_resistance") return Models::kGroupAirResistance;
    if (key == "elasticity") return Models::kGroupElasticity;
    if (key == "buoyancy") return Models::kGroupBuoyancy;
    if (key == "uproot_limit") return Models::kGroupUprootLimit;
    if (key == "col_damage_multiplier") return Models::kGroupColDamageMultiplier;
    if (key == "smash_multiplier") return Models::kGroupSmash;
    if (key == "break_velocity_randomness") return Models::kGroupBreakVelocityRand;
    return Models::kGroupMass;
}

int SetObjectGroupProperty(lua_State* L) {
    int groupId = 0;
    std::string error;
    if (!ReadInt(L, 1, "engineSetObjectGroupPhysicalProperty", groupId, error)) return luaL_error(L, "%s", error.c_str());
    if (lua_type(L, 2) != LUA_TSTRING)
        return luaL_error(L, "%s", Bad("engineSetObjectGroupPhysicalProperty", "objectgroup-modifiable", 2, Describe(L, 2)).c_str());
    Models::GroupValueKind kind;
    if (!Models::GroupProperty(lua_tostring(L, 2), kind))
        return luaL_error(L, "%s", Bad("engineSetObjectGroupPhysicalProperty", "objectgroup-modifiable", 2, Describe(L, 2)).c_str());
    if (groupId < 0 || groupId > 159)
        return luaL_error(L, "%s", Usage("engineSetObjectGroupPhysicalProperty", "Expected group ID in range [0-159] at argument 1").c_str());
    Models::ObjectGroup existing;
    if (!Models::ReadLiveObjectGroup(groupId, existing))
        return luaL_error(L, "%s", Usage("engineSetObjectGroupPhysicalProperty", "Expected valid group ID at argument 1").c_str());
    (void)existing;
    const std::string key = LowerKey(lua_tostring(L, 2));
    AssignRequest request;
    switch (kind) {
        case Models::GroupValueKind::Float: {
            lua_Number number = 0;
            if (!ReadScriptNumber(L, 3, "engineSetObjectGroupPhysicalProperty", number, error)) return luaL_error(L, "%s", error.c_str());
            request.kind = AssignKind::Float;
            request.offset = FloatOffset(key);
            request.scalar = static_cast<float>(number);
            break;
        }
        case Models::GroupValueKind::Bool: {
            if (lua_type(L, 3) != LUA_TBOOLEAN)
                return luaL_error(L, "%s", Bad("engineSetObjectGroupPhysicalProperty", "bool", 3, Describe(L, 3)).c_str());
            request.flag = lua_toboolean(L, 3) != 0;
            if (key == "sparks_on_impact") {
                request.kind = AssignKind::BoolWord;
                request.offset = Models::kGroupSparks;
            } else {
                request.kind = AssignKind::BoolByte;
                request.offset = key == "cause_explosion" ? Models::kGroupCausesExplosion : Models::kGroupCameraAvoid;
            }
            break;
        }
        case Models::GroupValueKind::Vector: {
            const int first = lua_type(L, 3);
            if (first != LUA_TNUMBER && first != LUA_TSTRING)
                return luaL_error(L, "%s", Bad("engineSetObjectGroupPhysicalProperty", "vector3", 3, Describe(L, 3)).c_str());
            for (int i = 0; i < 3; ++i) {
                lua_Number number = 0;
                if (!ReadScriptNumber(L, 3 + i, "engineSetObjectGroupPhysicalProperty", number, error)) return luaL_error(L, "%s", error.c_str());
                request.vector[i] = static_cast<float>(number);
            }
            request.kind = AssignKind::Vector;
            request.offset = key == "break_velocity" ? Models::kGroupBreakVelocity : Models::kGroupFxOffset;
            break;
        }
        case Models::GroupValueKind::Damage:
        case Models::GroupValueKind::Response:
        case Models::GroupValueKind::FxType:
        case Models::GroupValueKind::BreakMode: {
            const char* expected = kind == Models::GroupValueKind::Damage ? "objectgroup-damageeffect" :
                                   kind == Models::GroupValueKind::Response ? "objectgroup-collisionresponse" :
                                   kind == Models::GroupValueKind::FxType ? "objectgroup-fxtype" : "objectgroup-breakmode";
            if (lua_type(L, 3) != LUA_TSTRING)
                return luaL_error(L, "%s", Bad("engineSetObjectGroupPhysicalProperty", expected, 3, Describe(L, 3)).c_str());
            const std::string value = LowerKey(lua_tostring(L, 3));
            std::uint8_t byte = 0;
            int ordinal = 0;
            const char* responseNames[] = {"none", "lamppost", "small_box", "big_box", "fence_part", "grenade", "swingdoor", "lockdoor", "hanging", "poolball"};
            const char* fxNames[] = {"none", "play_on_hit", "play_on_destroyed", "play_on_hitdestroyed"};
            const char* breakNames[] = {"not_by_gun", "by_gun", "smashable"};
            if (kind == Models::GroupValueKind::Damage) {
                if (!DamageValue(value, byte))
                    return luaL_error(L, "%s", Bad("engineSetObjectGroupPhysicalProperty", expected, 3, Describe(L, 3)).c_str());
                request.kind = AssignKind::Byte;
                request.offset = Models::kGroupColDamageEffect;
                request.word = byte;
            } else if (kind == Models::GroupValueKind::Response) {
                if (!MatchName(value, responseNames, 10, ordinal))
                    return luaL_error(L, "%s", Bad("engineSetObjectGroupPhysicalProperty", expected, 3, Describe(L, 3)).c_str());
                request.kind = AssignKind::Byte;
                request.offset = Models::kGroupSpecialColResponse;
                request.word = static_cast<std::uint32_t>(ordinal);
            } else if (kind == Models::GroupValueKind::FxType) {
                if (!MatchName(value, fxNames, 4, ordinal))
                    return luaL_error(L, "%s", Bad("engineSetObjectGroupPhysicalProperty", expected, 3, Describe(L, 3)).c_str());
                request.kind = AssignKind::Byte;
                request.offset = Models::kGroupFxType;
                request.word = static_cast<std::uint32_t>(ordinal);
            } else {
                if (!MatchName(value, breakNames, 3, ordinal))
                    return luaL_error(L, "%s", Bad("engineSetObjectGroupPhysicalProperty", expected, 3, Describe(L, 3)).c_str());
                request.kind = AssignKind::Word;
                request.offset = Models::kGroupBreakMode;
                request.word = static_cast<std::uint32_t>(ordinal);
            }
            break;
        }
        case Models::GroupValueKind::FxSystem: {
            if (lua_type(L, 3) == LUA_TBOOLEAN) {
                if (lua_toboolean(L, 3))
                    return luaL_error(L, "%s", Usage("engineSetObjectGroupPhysicalProperty", "Expected either false, or valid fx system name at argument 3").c_str());
                request.kind = AssignKind::ClearFx;
                break;
            }
            if (lua_type(L, 3) != LUA_TSTRING)
                return luaL_error(L, "%s", Bad("engineSetObjectGroupPhysicalProperty", "string", 3, Describe(L, 3)).c_str());
            static std::once_flag once;
            std::call_once(once, [] {
                LUA_LOGI("DIAG engineSetObjectGroupPhysicalProperty: FxManager blueprint names are not hooked");
            });
            return luaL_error(L, "%s", Usage("engineSetObjectGroupPhysicalProperty", "Expected valid fx system name at argument 3").c_str());
        }
    }
    if (!Models::MutateLiveObjectGroup(groupId, ApplyAssign, &request))
        return luaL_error(L, "%s", Usage("engineSetObjectGroupPhysicalProperty", "Expected valid group ID at argument 1").c_str());
    lua_pushboolean(L, 1);
    return 1;
}

void BindObjectGroupLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"engineGetObjectGroupPhysicalProperty", GetObjectGroupProperty},
        {"engineSetObjectGroupPhysicalProperty", SetObjectGroupProperty},
        {"engineRestoreObjectGroupPhysicalProperties", RestoreObjectGroup},
        {"engineGetModelPhysicalPropertiesGroup", GetModelPhysicalGroup},
        {"engineSetModelPhysicalPropertiesGroup", SetModelPhysicalGroup},
        {"engineRestoreModelPhysicalPropertiesGroup", RestoreModelPhysicalGroup},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
