#include "SurfaceInfo.h"

#include "LuaVMStub.h"
#include "../gtasa/GameTasks.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace WorldRPG::Models {
namespace {

const unsigned char kOriginalSurfaces[kSurfaceCount * kSurfaceStride] = {
#include "SurfaceInfoOriginal.inc"
};

void SetFlag(unsigned char* record, int group, int bit, bool enabled, int count = 1) {
    std::uint32_t flags[2] = {};
    std::memcpy(flags, record + 2, sizeof flags);
    for (int left = count; left > 0; --left) {
        const int index = bit + left - 1;
        const std::uint32_t mask = 1u << index;
        if (enabled) flags[group] |= mask;
        else flags[group] &= ~mask;
    }
    std::memcpy(record + 2, flags, sizeof flags);
}

bool GetFlag(const unsigned char* record, int group, int bit) {
    std::uint32_t flags[2] = {};
    std::memcpy(flags, record + 2, sizeof flags);
    return ((flags[group] >> bit) & 1u) != 0;
}

void SetField(unsigned char* record, int shift, int width, std::uint32_t value) {
    std::uint32_t flags = 0;
    std::memcpy(&flags, record + 2, sizeof flags);
    const std::uint32_t mask = ((1u << width) - 1u) << shift;
    flags = (flags & ~mask) | ((value << shift) & mask);
    std::memcpy(record + 2, &flags, sizeof flags);
}

std::uint32_t GetField(const unsigned char* record, int shift, int width) {
    std::uint32_t flags = 0;
    std::memcpy(&flags, record + 2, sizeof flags);
    return (flags >> shift) & ((1u << width) - 1u);
}

std::string Lower(const char* text) {
    std::string out = text ? text : "";
    for (char& ch : out) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'A' && c <= 'Z') ch = static_cast<char>(c + 32);
    }
    return out;
}

const char* kAudio[] = {"concrete", "grass", "sand", "gravel", "wood", "water", "metal"};
const char* kBullet[] = {"metal", "sand", "wood", "concrete"};
const char* kWheel[] = {"grass", "gravel", "mud", "sand", "dust"};
const char* kSkid[] = {"asphalt", "dirt", "dust"};
const char* kAdhesion[] = {"rubber", "hard", "road", "loose", "sand", "wet"};

int Named(const char* const* names, int count, const std::string& key) {
    for (int i = 0; i < count; ++i) {
        if (key == names[i]) return i;
    }
    return -1;
}

#if defined(__ANDROID__)
bool SurfaceAbi() {
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return false;
    const auto* grip = static_cast<const std::uint32_t*>(dlsym(lib, "_ZN14SurfaceInfos_c11GetTyreGripEj"));
    void* table = dlsym(lib, "g_surfaceInfos");
    dlclose(lib);
    return grip && grip[0] == 0x321e07e8u && table;
}

unsigned char* SurfaceSlot(int surfaceId) {
    if (surfaceId < 0 || surfaceId >= kSurfaceCount) return nullptr;
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return nullptr;
    auto* table = static_cast<unsigned char*>(dlsym(lib, "g_surfaceInfos"));
    dlclose(lib);
    if (!table) return nullptr;
    return table + 0x90 + static_cast<std::size_t>(surfaceId) * kSurfaceStride;
}
#endif

bool WithRecord(int surfaceId, bool (*apply)(unsigned char*, void*), void* user) {
    if (surfaceId < 0 || surfaceId >= kSurfaceCount || !apply) return false;
#if !defined(__ANDROID__)
    (void)user;
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abi = false;
        std::call_once(once, [] { abi = SurfaceAbi(); });
        if (!abi) return;
        unsigned char* slot = SurfaceSlot(surfaceId);
        if (!slot) return;
        ok = apply(slot, user);
    });
    return ran && ok;
#endif
}

}  // namespace

bool SurfacePropertyFromName(const char* text, SurfaceProperty& property) {
    const std::string key = Lower(text);
    if (key == "audio") property = SurfaceProperty::Audio;
    else if (key == "stepwatersplash") property = SurfaceProperty::StepWater;
    else if (key == "stepdust") property = SurfaceProperty::StepDust;
    else if (key == "canclimb") property = SurfaceProperty::Climbing;
    else if (key == "bulleteffect") property = SurfaceProperty::Bullet;
    else if (key == "shootthrough") property = SurfaceProperty::Shoot;
    else if (key == "seethrough") property = SurfaceProperty::See;
    else if (key == "skidmarktype") property = SurfaceProperty::Skid;
    else if (key == "tyregrip") property = SurfaceProperty::Tyre;
    else if (key == "wetgrip") property = SurfaceProperty::Wet;
    else if (key == "adhesiongroup") property = SurfaceProperty::Adhesion;
    else if (key == "wheeleffect") property = SurfaceProperty::Wheel;
    else if (key == "fractioneffect") property = SurfaceProperty::Fraction;
    else if (key == "stairs") property = SurfaceProperty::Stairs;
    else if (key == "roughness") property = SurfaceProperty::Rough;
    else if (key == "steepslope") property = SurfaceProperty::Steep;
    else if (key == "glass") property = SurfaceProperty::Glass;
    else if (key == "pavement") property = SurfaceProperty::Pavement;
    else if (key == "softlanding") property = SurfaceProperty::Soft;
    else if (key == "footeffect") property = SurfaceProperty::Foot;
    else return false;
    return true;
}

bool ReadSurfaceRecord(const unsigned char* record, SurfaceProperty property, SurfaceRead& out) {
    switch (property) {
        case SurfaceProperty::Audio:
            for (int bit = 10; bit <= 16; ++bit) {
                if (GetFlag(record, 1, bit)) {
                    out.kind = SurfaceReadKind::Text;
                    out.text = kAudio[bit - 10];
                    return true;
                }
            }
            out.kind = SurfaceReadKind::Text;
            out.text = "disabled";
            return true;
        case SurfaceProperty::StepWater:
            out.kind = SurfaceReadKind::Bool;
            out.flag = GetFlag(record, 0, 16);
            return true;
        case SurfaceProperty::Foot:
            out.kind = SurfaceReadKind::Bool;
            out.flag = GetFlag(record, 0, 29);
            return true;
        case SurfaceProperty::Climbing:
            out.kind = SurfaceReadKind::Bool;
            out.flag = GetFlag(record, 1, 9);
            return true;
        case SurfaceProperty::Bullet: {
            const std::uint32_t effect = GetField(record, 8, 3);
            out.kind = SurfaceReadKind::Text;
            out.text = effect == 0 || effect > 4 ? "disabled" : kBullet[effect - 1];
            return true;
        }
        case SurfaceProperty::Shoot:
            out.kind = SurfaceReadKind::Bool;
            out.flag = GetFlag(record, 0, 14);
            return true;
        case SurfaceProperty::Steep:
            out.kind = SurfaceReadKind::Bool;
            out.flag = GetFlag(record, 0, 18);
            return true;
        case SurfaceProperty::See:
            out.kind = SurfaceReadKind::Bool;
            out.flag = GetFlag(record, 0, 13);
            return true;
        case SurfaceProperty::Glass:
            out.kind = SurfaceReadKind::Bool;
            out.flag = GetFlag(record, 0, 19);
            return true;
        case SurfaceProperty::Pavement:
            out.kind = SurfaceReadKind::Bool;
            out.flag = GetFlag(record, 0, 22);
            return true;
        case SurfaceProperty::Soft:
            out.kind = SurfaceReadKind::Bool;
            out.flag = GetFlag(record, 0, 11);
            return true;
        case SurfaceProperty::Skid: {
            const std::uint32_t effect = GetField(record, 3, 2);
            out.kind = SurfaceReadKind::Text;
            out.text = effect == 3 ? "disabled" : kSkid[effect];
            return true;
        }
        case SurfaceProperty::Fraction:
            out.kind = SurfaceReadKind::Number;
            out.number = static_cast<int>(GetField(record, 5, 3));
            return true;
        case SurfaceProperty::Tyre:
            out.kind = SurfaceReadKind::Number;
            out.number = record[0];
            return true;
        case SurfaceProperty::Wet:
            out.kind = SurfaceReadKind::Number;
            out.number = record[1];
            return true;
        case SurfaceProperty::Adhesion: {
            const std::uint32_t group = GetField(record, 0, 3);
            if (group > 5) return false;
            out.kind = SurfaceReadKind::Text;
            out.text = kAdhesion[group];
            return true;
        }
        case SurfaceProperty::Wheel:
            for (int bit = 0; bit <= 5; ++bit) {
                if (!GetFlag(record, 1, bit)) continue;
                out.kind = SurfaceReadKind::Text;
                out.text = bit == 0 ? "disabled" : kWheel[bit - 1];
                return true;
            }
            out.kind = SurfaceReadKind::Text;
            out.text = "disabled";
            return true;
        case SurfaceProperty::Stairs:
            out.kind = SurfaceReadKind::Bool;
            out.flag = GetFlag(record, 0, 20);
            return true;
        case SurfaceProperty::Rough:
            out.kind = SurfaceReadKind::Number;
            out.number = static_cast<int>(GetField(record, 23, 2));
            return true;
        case SurfaceProperty::StepDust:
            return false;
    }
    return false;
}

bool WriteSurfaceAudio(unsigned char* record, const char* name) {
    const int index = Named(kAudio, 7, Lower(name));
    if (index < 0) return false;
    SetFlag(record, 1, 10, false, 6);
    SetFlag(record, 1, 10 + index, true);
    return true;
}

bool WriteSurfaceBool(unsigned char* record, SurfaceProperty property, bool enabled) {
    int group = 0;
    int bit = 0;
    switch (property) {
        case SurfaceProperty::StepWater: bit = 16; break;
        case SurfaceProperty::Foot: bit = 29; break;
        case SurfaceProperty::Shoot: bit = 14; break;
        case SurfaceProperty::See: bit = 13; break;
        case SurfaceProperty::Steep: bit = 18; break;
        case SurfaceProperty::Glass: bit = 19; break;
        case SurfaceProperty::Pavement: bit = 22; break;
        case SurfaceProperty::Soft: bit = 11; break;
        case SurfaceProperty::Stairs: bit = 20; break;
        case SurfaceProperty::Climbing: group = 1; bit = 9; break;
        default: return false;
    }
    SetFlag(record, group, bit, enabled);
    return true;
}

bool WriteSurfaceBullet(unsigned char* record, const char* name) {
    const std::string key = Lower(name);
    int value = 0;
    if (key == "disabled") value = 0;
    else {
        const int index = Named(kBullet, 4, key);
        if (index < 0) return false;
        value = index + 1;
    }
    SetField(record, 8, 3, static_cast<std::uint32_t>(value));
    return true;
}

bool WriteSurfaceSkid(unsigned char* record, const char* name) {
    const std::string key = Lower(name);
    int value = 0;
    if (key == "disabled") value = 3;
    else {
        const int index = Named(kSkid, 3, key);
        if (index < 0) return false;
        value = index;
    }
    SetField(record, 3, 2, static_cast<std::uint32_t>(value));
    return true;
}

bool WriteSurfaceAdhesion(unsigned char* record, const char* name) {
    const int index = Named(kAdhesion, 6, Lower(name));
    if (index < 0) return false;
    SetField(record, 0, 3, static_cast<std::uint32_t>(index));
    return true;
}

bool WriteSurfaceWheel(unsigned char* record, const char* name) {
    const std::string key = Lower(name);
    int bit = 0;
    if (key != "disabled") {
        const int index = Named(kWheel, 5, key);
        if (index < 0) return false;
        bit = index + 1;
    }
    SetFlag(record, 1, 0, false, 6);
    if (bit != 0) SetFlag(record, 1, bit, true);
    return true;
}

bool WriteSurfaceFraction(unsigned char* record, int value) {
    if (value < 0 || value > 7) return false;
    SetField(record, 5, 3, static_cast<std::uint32_t>(value));
    return true;
}

bool WriteSurfaceGrip(unsigned char* record, SurfaceProperty property, int value) {
    if (value < 0 || value > 255) return false;
    record[property == SurfaceProperty::Wet ? 1 : 0] = static_cast<unsigned char>(value);
    return true;
}

bool WriteSurfaceRoughness(unsigned char* record, int value) {
    if (value < 0 || value > 3) return false;
    SetField(record, 23, 2, static_cast<std::uint32_t>(value));
    return true;
}

void CopyOriginalSurface(int surfaceId, unsigned char* record) {
    if (surfaceId < 0 || surfaceId >= kSurfaceCount) return;
    std::memcpy(record, kOriginalSurfaces + static_cast<std::size_t>(surfaceId) * kSurfaceStride, kSurfaceStride);
}

namespace {

struct AudioUser { const char* name; };
struct BoolUser { SurfaceProperty property; bool enabled; };
struct TextUser { const char* name; };
struct IntUser { int value; SurfaceProperty property; };

bool ApplyAudio(unsigned char* record, void* user) { return WriteSurfaceAudio(record, static_cast<AudioUser*>(user)->name); }
bool ApplyBool(unsigned char* record, void* user) {
    const auto* request = static_cast<BoolUser*>(user);
    return WriteSurfaceBool(record, request->property, request->enabled);
}
bool ApplyBullet(unsigned char* record, void* user) { return WriteSurfaceBullet(record, static_cast<TextUser*>(user)->name); }
bool ApplySkid(unsigned char* record, void* user) { return WriteSurfaceSkid(record, static_cast<TextUser*>(user)->name); }
bool ApplyAdhesion(unsigned char* record, void* user) { return WriteSurfaceAdhesion(record, static_cast<TextUser*>(user)->name); }
bool ApplyWheel(unsigned char* record, void* user) { return WriteSurfaceWheel(record, static_cast<TextUser*>(user)->name); }
bool ApplyFraction(unsigned char* record, void* user) { return WriteSurfaceFraction(record, static_cast<IntUser*>(user)->value); }
bool ApplyGrip(unsigned char* record, void* user) {
    const auto* request = static_cast<IntUser*>(user);
    return WriteSurfaceGrip(record, request->property, request->value);
}
bool ApplyRough(unsigned char* record, void* user) { return WriteSurfaceRoughness(record, static_cast<IntUser*>(user)->value); }
bool ApplyReset(unsigned char* record, void* user) {
    CopyOriginalSurface(*static_cast<int*>(user), record);
    return true;
}

void NoteSurfaceGap() {
    static std::once_flag once;
    std::call_once(once, [] { LUA_LOGI("DIAG engine surface properties: g_surfaceInfos is not readable on this process"); });
}

}  // namespace

bool ReadLiveSurface(int surfaceId, SurfaceProperty property, SurfaceRead& out) {
    struct User { SurfaceProperty property; SurfaceRead* out; bool ok; } user{property, &out, false};
    auto apply = [](unsigned char* record, void* raw) {
        auto* request = static_cast<User*>(raw);
        request->ok = ReadSurfaceRecord(record, request->property, *request->out);
        return request->ok;
    };
    if (!WithRecord(surfaceId, apply, &user)) {
        NoteSurfaceGap();
        return false;
    }
    return user.ok;
}

bool WriteLiveSurfaceAudio(int surfaceId, const char* name) {
    AudioUser user{name};
    if (!WithRecord(surfaceId, ApplyAudio, &user)) { NoteSurfaceGap(); return false; }
    return true;
}
bool WriteLiveSurfaceBool(int surfaceId, SurfaceProperty property, bool enabled) {
    BoolUser user{property, enabled};
    if (!WithRecord(surfaceId, ApplyBool, &user)) { NoteSurfaceGap(); return false; }
    return true;
}
bool WriteLiveSurfaceBullet(int surfaceId, const char* name) {
    TextUser user{name};
    if (!WithRecord(surfaceId, ApplyBullet, &user)) { NoteSurfaceGap(); return false; }
    return true;
}
bool WriteLiveSurfaceSkid(int surfaceId, const char* name) {
    TextUser user{name};
    if (!WithRecord(surfaceId, ApplySkid, &user)) { NoteSurfaceGap(); return false; }
    return true;
}
bool WriteLiveSurfaceAdhesion(int surfaceId, const char* name) {
    TextUser user{name};
    if (!WithRecord(surfaceId, ApplyAdhesion, &user)) { NoteSurfaceGap(); return false; }
    return true;
}
bool WriteLiveSurfaceWheel(int surfaceId, const char* name) {
    TextUser user{name};
    if (!WithRecord(surfaceId, ApplyWheel, &user)) { NoteSurfaceGap(); return false; }
    return true;
}
bool WriteLiveSurfaceFraction(int surfaceId, int value) {
    IntUser user{value, SurfaceProperty::Fraction};
    if (!WithRecord(surfaceId, ApplyFraction, &user)) { NoteSurfaceGap(); return false; }
    return true;
}
bool WriteLiveSurfaceGrip(int surfaceId, SurfaceProperty property, int value) {
    IntUser user{value, property};
    if (!WithRecord(surfaceId, ApplyGrip, &user)) { NoteSurfaceGap(); return false; }
    return true;
}
bool WriteLiveSurfaceRoughness(int surfaceId, int value) {
    IntUser user{value, SurfaceProperty::Rough};
    if (!WithRecord(surfaceId, ApplyRough, &user)) { NoteSurfaceGap(); return false; }
    return true;
}
bool ResetLiveSurface(int surfaceId) {
    if (!WithRecord(surfaceId, ApplyReset, &surfaceId)) { NoteSurfaceGap(); return false; }
    return true;
}
bool ResetLiveSurfaces() {
    for (int id = 0; id < kSurfaceCount; ++id) {
        if (!ResetLiveSurface(id)) return false;
    }
    return true;
}

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
namespace {

std::string Bad(const char* function, const char* expected, int index, const std::string& got) {
    return std::string("Bad argument @ '") + function + "' [Expected " + expected + " at argument " +
           std::to_string(index) + ", got " + got + "]";
}

std::string Usage(const char* function, const char* reason) {
    return std::string("Bad usage @ '") + function + "' [" + reason + "]";
}

std::string Describe(lua_State* L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TNONE: return "none";
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TNUMBER: {
            const char* text = lua_tostring(L, index);
            return std::string("number '") + (text ? text : "") + "'";
        }
        case LUA_TSTRING: return std::string("string '") + (lua_tostring(L, index) ? lua_tostring(L, index) : "") + "'";
        case LUA_TTABLE: return "table";
        default: return lua_typename(L, lua_type(L, index));
    }
}

int FailNil(lua_State* L, const char* function, const std::string& error) {
    LUA_LOGE("%s: %s", function, error.c_str());
    lua_pushnil(L);
    return 1;
}

int FailFalse(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

bool ReadNumber(lua_State* L, int index, const char* function, lua_Number& out, std::string& error) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        error = Bad(function, "number", index, Describe(L, index));
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
    return true;
}

bool SurfaceId(lua_State* L, const char* function, int& id, std::string& error) {
    lua_Number number = 0;
    if (!ReadNumber(L, 1, function, number, error)) return false;
    id = static_cast<int>(number);
    if (id < 0 || id > Models::kSurfaceIdLimit) {
        error = Usage(function, "Expected valid surface ID ( 0 - 179 ) at argument 1");
        return false;
    }
    return true;
}

int GetSurface(lua_State* L) {
    lua_Number rawId = 0;
    std::string error;
    if (!ReadNumber(L, 1, "engineGetSurfaceProperties", rawId, error)) return FailNil(L, "engineGetSurfaceProperties", error);
    if (lua_type(L, 2) != LUA_TSTRING)
        return FailNil(L, "engineGetSurfaceProperties", Bad("engineGetSurfaceProperties", "surface-property-type", 2, Describe(L, 2)));
    Models::SurfaceProperty property;
    if (!Models::SurfacePropertyFromName(lua_tostring(L, 2), property))
        return FailNil(L, "engineGetSurfaceProperties", Bad("engineGetSurfaceProperties", "surface-property-type", 2, Describe(L, 2)));
    const int id = static_cast<int>(rawId);
    if (id < 0 || id > Models::kSurfaceIdLimit)
        return FailNil(L, "engineGetSurfaceProperties", Usage("engineGetSurfaceProperties", "Expected valid surface ID ( 0 - 179 ) at argument 1"));
    if (id >= Models::kSurfaceCount) return FailFalse(L);
    Models::SurfaceRead read;
    if (!Models::ReadLiveSurface(id, property, read)) return FailFalse(L);
    if (read.kind == Models::SurfaceReadKind::Bool) {
        lua_pushboolean(L, read.flag ? 1 : 0);
        return 1;
    }
    if (read.kind == Models::SurfaceReadKind::Number) {
        lua_pushnumber(L, read.number);
        return 1;
    }
    if (read.kind == Models::SurfaceReadKind::Text) {
        lua_pushstring(L, read.text);
        return 1;
    }
    return FailFalse(L);
}

int SetSurface(lua_State* L) {
    lua_Number rawId = 0;
    std::string error;
    if (!ReadNumber(L, 1, "engineSetSurfaceProperties", rawId, error)) return FailNil(L, "engineSetSurfaceProperties", error);
    if (lua_type(L, 2) != LUA_TSTRING)
        return FailNil(L, "engineSetSurfaceProperties", Bad("engineSetSurfaceProperties", "surface-property-type", 2, Describe(L, 2)));
    Models::SurfaceProperty property;
    if (!Models::SurfacePropertyFromName(lua_tostring(L, 2), property))
        return FailNil(L, "engineSetSurfaceProperties", Bad("engineSetSurfaceProperties", "surface-property-type", 2, Describe(L, 2)));
    const int id = static_cast<int>(rawId);
    if (id < 0 || id > Models::kSurfaceIdLimit)
        return FailNil(L, "engineSetSurfaceProperties", Usage("engineSetSurfaceProperties", "Expected valid surface ID ( 0 - 179 ) at argument 1"));
    if (id >= Models::kSurfaceCount || property == Models::SurfaceProperty::StepDust) return FailFalse(L);
    bool ok = false;
    switch (property) {
        case Models::SurfaceProperty::Audio:
        case Models::SurfaceProperty::Bullet:
        case Models::SurfaceProperty::Skid:
        case Models::SurfaceProperty::Adhesion:
        case Models::SurfaceProperty::Wheel: {
            const char* expected = property == Models::SurfaceProperty::Audio ? "surface-audio-type" :
                                   property == Models::SurfaceProperty::Bullet ? "surface-bullet-effect" :
                                   property == Models::SurfaceProperty::Skid ? "surface-skidmark-type" :
                                   property == Models::SurfaceProperty::Adhesion ? "surface-adhesion-group" : "surface-wheel-effect";
            if (lua_type(L, 3) != LUA_TSTRING) return FailNil(L, "engineSetSurfaceProperties", Bad("engineSetSurfaceProperties", expected, 3, Describe(L, 3)));
            const char* name = lua_tostring(L, 3);
            unsigned char probe[Models::kSurfaceStride] = {};
            const bool named = property == Models::SurfaceProperty::Audio ? Models::WriteSurfaceAudio(probe, name) :
                               property == Models::SurfaceProperty::Bullet ? Models::WriteSurfaceBullet(probe, name) :
                               property == Models::SurfaceProperty::Skid ? Models::WriteSurfaceSkid(probe, name) :
                               property == Models::SurfaceProperty::Adhesion ? Models::WriteSurfaceAdhesion(probe, name) :
                               Models::WriteSurfaceWheel(probe, name);
            if (!named) return FailNil(L, "engineSetSurfaceProperties", Bad("engineSetSurfaceProperties", expected, 3, Describe(L, 3)));
            if (property == Models::SurfaceProperty::Audio) ok = Models::WriteLiveSurfaceAudio(id, name);
            else if (property == Models::SurfaceProperty::Bullet) ok = Models::WriteLiveSurfaceBullet(id, name);
            else if (property == Models::SurfaceProperty::Skid) ok = Models::WriteLiveSurfaceSkid(id, name);
            else if (property == Models::SurfaceProperty::Adhesion) ok = Models::WriteLiveSurfaceAdhesion(id, name);
            else ok = Models::WriteLiveSurfaceWheel(id, name);
            break;
        }
        case Models::SurfaceProperty::Tyre:
        case Models::SurfaceProperty::Wet: {
            lua_Number number = 0;
            if (!ReadNumber(L, 3, "engineSetSurfaceProperties", number, error)) return FailNil(L, "engineSetSurfaceProperties", error);
            if (number < 0) return FailNil(L, "engineSetSurfaceProperties", std::string("Bad argument @ 'engineSetSurfaceProperties' [Expected positive value, got negative]"));
            const int value = static_cast<int>(number);
            if (value > 255)
                return FailNil(L, "engineSetSurfaceProperties", Usage("engineSetSurfaceProperties", property == Models::SurfaceProperty::Wet ? "Expected valid wet grip ( 0 - 255 ) at argument 3" : "Expected valid tyre grip ( 0 - 255 ) at argument 3"));
            ok = Models::WriteLiveSurfaceGrip(id, property, value);
            break;
        }
        case Models::SurfaceProperty::Fraction: {
            lua_Number number = 0;
            if (!ReadNumber(L, 3, "engineSetSurfaceProperties", number, error)) return FailNil(L, "engineSetSurfaceProperties", error);
            const int value = static_cast<int>(number);
            if (value < 0 || value > 7)
                return FailNil(L, "engineSetSurfaceProperties", Usage("engineSetSurfaceProperties", "Expected valid fraction effect ID ( 0 - 7 ) at argument 3"));
            ok = Models::WriteLiveSurfaceFraction(id, value);
            break;
        }
        case Models::SurfaceProperty::Rough: {
            lua_Number number = 0;
            if (!ReadNumber(L, 3, "engineSetSurfaceProperties", number, error)) return FailNil(L, "engineSetSurfaceProperties", error);
            const int value = static_cast<int>(number);
            if (value < 0 || value > 3)
                return FailNil(L, "engineSetSurfaceProperties", Usage("engineSetSurfaceProperties", "Expected valid roughness ( 0 - 3 ) at argument 3"));
            ok = Models::WriteLiveSurfaceRoughness(id, value);
            break;
        }
        default: {
            if (lua_type(L, 3) != LUA_TBOOLEAN)
                return FailNil(L, "engineSetSurfaceProperties", Bad("engineSetSurfaceProperties", "bool", 3, Describe(L, 3)));
            ok = Models::WriteLiveSurfaceBool(id, property, lua_toboolean(L, 3) != 0);
            break;
        }
    }
    if (!ok) return FailFalse(L);
    lua_pushboolean(L, 1);
    return 1;
}

int ResetSurface(lua_State* L) {
    if (lua_type(L, 1) != LUA_TNUMBER) {
        lua_pushboolean(L, Models::ResetLiveSurfaces() ? 1 : 0);
        return 1;
    }
    int id = 0;
    std::string error;
    if (!SurfaceId(L, "engineResetSurfaceProperties", id, error)) return FailNil(L, "engineResetSurfaceProperties", error);
    if (id >= Models::kSurfaceCount) return FailFalse(L);
    lua_pushboolean(L, Models::ResetLiveSurface(id) ? 1 : 0);
    return 1;
}

}  // namespace

void BindSurfaceLua(lua_State* L) {
    lua_pushcfunction(L, GetSurface);
    lua_setglobal(L, "engineGetSurfaceProperties");
    lua_pushcfunction(L, SetSurface);
    lua_setglobal(L, "engineSetSurfaceProperties");
    lua_pushcfunction(L, ResetSurface);
    lua_setglobal(L, "engineResetSurfaceProperties");
}

}  // namespace WorldRPG::Script
