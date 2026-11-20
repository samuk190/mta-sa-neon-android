#include "LuaApiExtra.h"

#include "LuaGuiApi.h"
#include "LuaJsonApi.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"
#include "../net/RemoteCalls.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <strings.h>
#include <unordered_map>

// Chat line sink (jni/JNI_OnLoad.cpp on Android, stubs/jni_bridge_stub.cpp in the headless build).
namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text);
}

namespace WorldRPG::Script {

namespace {

// Registry key of the table {[elementId] = {[key] = value}} written by setElementData.
constexpr const char* kElementDataKey = "worldrpg.elementData";

// Fixed results for natives the phone cannot answer yet (no engine hook). Each
// kind is the value the desktop native gives when it has nothing to report.
enum DefaultKind : int {
    kTrue,
    kFalse,
    kNil,
    kZero,
    kEmptyString,
    kEmptyTable,
    kZero3,
    kAlpha255,
};

int DefaultValue(lua_State* L) {
    switch (static_cast<DefaultKind>(lua_tointeger(L, lua_upvalueindex(1)))) {
        case kTrue: lua_pushboolean(L, 1); return 1;
        case kFalse: lua_pushboolean(L, 0); return 1;
        case kNil: lua_pushnil(L); return 1;
        case kZero: lua_pushinteger(L, 0); return 1;
        case kEmptyString: lua_pushstring(L, ""); return 1;
        case kEmptyTable: lua_newtable(L); return 1;
        case kZero3:
            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
            return 3;
        case kAlpha255: lua_pushinteger(L, 255); return 1;
    }
    return 0;
}

// Sets a global to a default native only when no core binding defines it.
void SetDefault(lua_State* L, const char* name, DefaultKind kind) {
    lua_getglobal(L, name);
    const bool defined = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (defined) return;
    lua_pushinteger(L, kind);
    lua_pushcclosure(L, DefaultValue, 1);
    lua_setglobal(L, name);
}

void SetGlobal(lua_State* L, const char* name, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    lua_setglobal(L, name);
}

int ClampColor(lua_Integer value) {
    return static_cast<int>(value < 0 ? 0 : (value > 255 ? 255 : value));
}

// outputChatBox(text, r, g, b, colorCoded): the same line the server chat uses,
// "#RRGGBB" followed by the text (GameActivity kind 0).
int OutputChatBox(lua_State* L) {
    size_t length = 0;
    const char* text = luaL_optlstring(L, 1, "", &length);
    char prefix[8];
    std::snprintf(prefix, sizeof(prefix), "#%02X%02X%02X", ClampColor(luaL_optinteger(L, 2, 255)),
                  ClampColor(luaL_optinteger(L, 3, 255)), ClampColor(luaL_optinteger(L, 4, 255)));
    const std::string line = std::string(prefix) + std::string(text, length);
    WorldRPG::JniBridge::PostGameLine(0, line.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

static bool g_chatVisible = true;

int ShowChat(lua_State* L) {
    bool show = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : true;
    g_chatVisible = show;
    WorldRPG::JniBridge::PostGameLine(10, show ? "1" : "0");
    lua_pushboolean(L, 1);
    return 1;
}

int IsChatVisible(lua_State* L) {
    lua_pushboolean(L, g_chatVisible ? 1 : 0);
    return 1;
}

// toggleControl(control, enabled) / isControlEnabled(control). The state is kept for every name;
// only "radar" acts on the phone: F11 is bound to the "radar" control (desktop CKeyBinds.cpp:238),
// and the player map opens on F11 only while that control is enabled. Kind 20 tells the activity.
static std::mutex g_controlsMutex;
static std::set<std::string> g_disabledControls;
static bool g_allMtaControlsEnabled = true;

static const char* const kAllGtaControls[] = {
    "fire", "next_weapon", "previous_weapon", "forwards", "backwards", "left", "right",
    "zoom_in", "zoom_out", "enter_exit", "change_camera", "jump", "sprint", "look_behind",
    "crouch", "action", "walk", "vehicle_fire", "vehicle_secondary_fire", "vehicle_left",
    "vehicle_right", "steer_forward", "steer_back", "accelerate", "brake_reverse",
    "radio_next", "radio_previous", "radio_user_track_skip", "horn", "sub_mission",
    "handbrake", "vehicle_look_left", "vehicle_look_right", "vehicle_look_behind",
    "vehicle_mouse_look", "special_control_left", "special_control_right",
    "special_control_down", "special_control_up", "aim_weapon", "conversation_yes",
    "conversation_no", "group_control_forwards", "group_control_back", "radar"
};

static const char* const kAnalogControls[] = {
    "left", "right", "forwards", "backwards",
    "vehicle_left", "vehicle_right", "steer_forward", "steer_back",
    "accelerate", "brake_reverse",
    "special_control_left", "special_control_right", "special_control_up", "special_control_down"
};

static const char* CanonicalAnalogControl(const char* name) {
    if (!name) return nullptr;
    for (const char* ctrl : kAnalogControls) {
        if (strcasecmp(ctrl, name) == 0) return ctrl;
    }
    return nullptr;
}

static const char* AnalogOppositeControl(const char* canonical) {
    if (!canonical) return nullptr;
    if (strcasecmp(canonical, "forwards") == 0) return "backwards";
    if (strcasecmp(canonical, "backwards") == 0) return "forwards";
    if (strcasecmp(canonical, "left") == 0) return "right";
    if (strcasecmp(canonical, "right") == 0) return "left";
    if (strcasecmp(canonical, "vehicle_left") == 0) return "vehicle_right";
    if (strcasecmp(canonical, "vehicle_right") == 0) return "vehicle_left";
    if (strcasecmp(canonical, "steer_forward") == 0) return "steer_back";
    if (strcasecmp(canonical, "steer_back") == 0) return "steer_forward";
    if (strcasecmp(canonical, "special_control_left") == 0) return "special_control_right";
    if (strcasecmp(canonical, "special_control_right") == 0) return "special_control_left";
    if (strcasecmp(canonical, "special_control_down") == 0) return "special_control_up";
    if (strcasecmp(canonical, "special_control_up") == 0) return "special_control_down";
    return nullptr;
}

static std::unordered_map<std::string, float> g_analogOverrides;

int ToggleControl(lua_State* L) {
    const char* control = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    const bool enabled = lua_toboolean(L, 2) != 0;
    {
        std::lock_guard<std::mutex> lock(g_controlsMutex);
        if (enabled) g_disabledControls.erase(control);
        else g_disabledControls.insert(control);
    }
    if (std::strcmp(control, "radar") == 0) {
        LUA_LOGI("toggleControl radar %d", enabled ? 1 : 0);
        WorldRPG::JniBridge::PostGameLine(20, enabled ? "radar|1" : "radar|0");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int IsControlEnabled(lua_State* L) {
    const char* control = luaL_checkstring(L, 1);
    std::lock_guard<std::mutex> lock(g_controlsMutex);
    lua_pushboolean(L, g_disabledControls.count(control) == 0 ? 1 : 0);
    return 1;
}

// CLuaFunctionDefs.Input.cpp:680-702 and CKeyBinds.cpp:1280-1309.
int ToggleAllControls(lua_State* L) {
    if (lua_type(L, 1) != LUA_TBOOLEAN) {
        LUA_LOGE("Bad argument @ 'toggleAllControls' [Expected bool at argument 1]");
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool enabled = lua_toboolean(L, 1) != 0;
    bool gtaControls = true;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        if (lua_type(L, 2) != LUA_TBOOLEAN) {
            LUA_LOGE("Bad argument @ 'toggleAllControls' [Expected bool at argument 2]");
            lua_pushboolean(L, 0);
            return 1;
        }
        gtaControls = lua_toboolean(L, 2) != 0;
    }
    bool mtaControls = true;
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
        if (lua_type(L, 3) != LUA_TBOOLEAN) {
            LUA_LOGE("Bad argument @ 'toggleAllControls' [Expected bool at argument 3]");
            lua_pushboolean(L, 0);
            return 1;
        }
        mtaControls = lua_toboolean(L, 3) != 0;
    }
    if (gtaControls) {
        std::lock_guard<std::mutex> lock(g_controlsMutex);
        if (!enabled) {
            for (const char* ctrl : kAllGtaControls) {
                g_disabledControls.insert(ctrl);
            }
        } else {
            g_disabledControls.clear();
        }
        WorldRPG::JniBridge::PostGameLine(20, enabled ? "radar|1" : "radar|0");
    }
    if (mtaControls) {
        g_allMtaControlsEnabled = enabled;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaFunctionDefs.Input.cpp:278-300 and CStaticFunctionDefinitions.cpp:7436-7452.
int GetAnalogControlState(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        LUA_LOGE("Bad argument @ 'getAnalogControlState' [Expected string at argument 1]");
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* rawName = lua_tostring(L, 1);
    const char* canonical = CanonicalAnalogControl(rawName);
    if (!canonical) {
        LUA_LOGE("Bad argument @ 'getAnalogControlState' [Invalid control name '%s']", rawName ? rawName : "");
        lua_pushboolean(L, 0);
        return 1;
    }
    bool rawInput = false;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        if (lua_type(L, 2) != LUA_TBOOLEAN) {
            LUA_LOGE("Bad argument @ 'getAnalogControlState' [Expected bool at argument 2]");
            lua_pushboolean(L, 0);
            return 1;
        }
        rawInput = lua_toboolean(L, 2) != 0;
    }
    if (!rawInput) {
        std::lock_guard<std::mutex> lock(g_controlsMutex);
        auto it = g_analogOverrides.find(canonical);
        if (it != g_analogOverrides.end()) {
            lua_pushnumber(L, it->second);
            return 1;
        }
    }
    WorldRPG::Game::OnFootKeys keys{};
    WorldRPG::Game::CopyOnFoot(0.f, 0.f, 0.f, keys);
    float val = 0.0f;
    if (std::strcmp(canonical, "left") == 0) val = keys.stickX < 0 ? static_cast<float>(-keys.stickX) / 128.0f : 0.0f;
    else if (std::strcmp(canonical, "right") == 0) val = keys.stickX > 0 ? static_cast<float>(keys.stickX) / 128.0f : 0.0f;
    else if (std::strcmp(canonical, "forwards") == 0) val = keys.stickY < 0 ? static_cast<float>(-keys.stickY) / 128.0f : 0.0f;
    else if (std::strcmp(canonical, "backwards") == 0) val = keys.stickY > 0 ? static_cast<float>(keys.stickY) / 128.0f : 0.0f;
    else if (std::strcmp(canonical, "accelerate") == 0) val = keys.sprint ? 1.0f : 0.0f;
    else if (std::strcmp(canonical, "brake_reverse") == 0) val = keys.jump ? 1.0f : 0.0f;
    val = std::max(0.0f, std::min(1.0f, val));
    lua_pushnumber(L, val);
    return 1;
}

// CLuaFunctionDefs.Input.cpp:302-339 and CClientPad.cpp:595-640.
int SetAnalogControlState(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        LUA_LOGE("Bad argument @ 'setAnalogControlState' [Expected string at argument 1]");
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* rawName = lua_tostring(L, 1);
    const char* canonical = CanonicalAnalogControl(rawName);
    if (!canonical) {
        LUA_LOGE("Bad argument @ 'setAnalogControlState' [Invalid control name '%s']", rawName ? rawName : "");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (lua_gettop(L) < 2 || lua_isnil(L, 2)) {
        std::lock_guard<std::mutex> lock(g_controlsMutex);
        g_analogOverrides.erase(canonical);
        lua_pushboolean(L, 1);
        return 1;
    }
    if (lua_type(L, 2) != LUA_TNUMBER) {
        LUA_LOGE("Bad argument @ 'setAnalogControlState' [Expected number at argument 2]");
        lua_pushboolean(L, 0);
        return 1;
    }
    const double state = lua_tonumber(L, 2);
    if (!std::isfinite(state)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float clamped = static_cast<float>(std::max(0.0, std::min(1.0, state)));
    {
        std::lock_guard<std::mutex> lock(g_controlsMutex);
        g_analogOverrides[canonical] = clamped;
        if (const char* opp = AnalogOppositeControl(canonical)) {
            g_analogOverrides[opp] = 0.0f;
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

// Android only: setMobileRadarArea(x, y, width, height) names the screen rectangle of a script-drawn
// radar, in the same pixels as dxDraw*. A tap there acts like F11, as a tap on the radar does in
// GTA SA mobile. setMobileRadarArea(false) removes it. Kind 21: "x|y|w|h", or empty to remove.
int SetMobileRadarArea(lua_State* L) {
    if (lua_isnoneornil(L, 1) || (lua_isboolean(L, 1) && !lua_toboolean(L, 1))) {
        WorldRPG::JniBridge::PostGameLine(21, "");
        lua_pushboolean(L, 1);
        return 1;
    }
    const lua_Number x = luaL_checknumber(L, 1);
    const lua_Number y = luaL_checknumber(L, 2);
    const lua_Number w = luaL_checknumber(L, 3);
    const lua_Number h = luaL_checknumber(L, 4);
    if (!(std::isfinite(x) && std::isfinite(y) && std::isfinite(w) && std::isfinite(h)) || w <= 0 || h <= 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    char line[96];
    std::snprintf(line, sizeof(line), "%.1f|%.1f|%.1f|%.1f", x, y, w, h);
    LUA_LOGI("setMobileRadarArea %s", line);
    WorldRPG::JniBridge::PostGameLine(21, line);
    lua_pushboolean(L, 1);
    return 1;
}

int ClearChatBox(lua_State* L) {
    WorldRPG::JniBridge::PostGameLine(8, "");
    lua_pushboolean(L, 1);
    return 1;
}

// iprint(...) and debugMessage(text, level, r, g, b): console output only.
int Iprint(lua_State* L) {
    std::string message;
    const int top = lua_gettop(L);
    for (int i = 1; i <= top; ++i) {
        size_t length = 0;
        const char* part = luaL_tolstring(L, i, &length);
        if (i > 1) message += "\t";
        message.append(part, length);
        lua_pop(L, 1);
    }
    LUA_LOGI("Lua: %s", message.c_str());
    return 0;
}

int DebugMessage(lua_State* L) {
    LUA_LOGI("Lua debug: %s", luaL_optstring(L, 1, ""));
    lua_pushboolean(L, 1);
    return 1;
}

// getElementData(element, key): values set with setElementData first, then server data.
int GetElementData(lua_State* L) {
    uint32_t id = WorldRPG::ServerWorld::LocalPlayer();
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "id");
        if (lua_isinteger(L, -1)) id = static_cast<uint32_t>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }
    const char* key = luaL_checkstring(L, 2);
    lua_settop(L, 2);
    lua_getfield(L, LUA_REGISTRYINDEX, kElementDataKey);
    if (lua_istable(L, -1)) {
        lua_pushinteger(L, static_cast<lua_Integer>(id));
        lua_rawget(L, -2);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, key);
            if (!lua_isnil(L, -1)) return 1;
        }
    }
    lua_settop(L, 2);
    double number = 0;
    std::string text;
    bool isNumber = false;
    bool isBoolean = false;
    if (!WorldRPG::ServerWorld::GetData(id, key, number, text, isNumber, &isBoolean)) {
        lua_pushnil(L);
        return 1;
    }
    if (isBoolean) lua_pushboolean(L, number != 0 ? 1 : 0);
    else if (isNumber) lua_pushnumber(L, number);
    else lua_pushlstring(L, text.data(), text.size());
    return 1;
}

// setElementData(element, key, value): client-side value, kept for the session.
int SetElementData(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (id == 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_settop(L, 3);
    lua_getfield(L, LUA_REGISTRYINDEX, kElementDataKey);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kElementDataKey);
    }
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    lua_rawget(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(id));
        lua_pushvalue(L, -2);
        lua_rawset(L, -4);
    }
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, key);
    lua_pushboolean(L, 1);
    return 1;
}

// Vector3(x, y, z): the desktop vector class reduced to its x, y, z fields.
int Vector3New(lua_State* L) {
    lua_newtable(L);
    lua_pushnumber(L, luaL_optnumber(L, 1, 0.0));
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, luaL_optnumber(L, 2, 0.0));
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, luaL_optnumber(L, 3, 0.0));
    lua_setfield(L, -2, "z");
    return 1;
}

// getCameraMatrix(): position, look-at, roll, fov. The phone does not expose the
// camera yet, so this is the default camera (no hook).
int GetCameraMatrix(lua_State* L) {
    const double values[] = {0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 70.0};
    for (double value : values) lua_pushnumber(L, value);
    return 8;
}

// setElementCollisionsEnabled and similar setters need no reply, but scripts expect a boolean.
int TrueResult(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

// createVehicle / createMarker: no client-side creation on the phone.
// createObject is replaced later by InstallWorldObjectLua.
// Returns false, as the desktop does when the element cannot be created.
int FalseResult(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

// fileOpen(path): resource files are not readable from Lua yet.
int FileOpen(lua_State* L) {
    lua_pushnil(L);
    return 1;
}

// CLuaBitDefs.cpp (Shared/mods/deathmatch/logic/luadefs/CLuaBitDefs.cpp 30-408).
// ReadNumber(uint, false) allows a negative value and keeps the low 32 bits.
// Extra arguments after the required ones are included only when they are numbers
// (CScriptArgReader::NextIsNumber), not numeric strings.

void LogBit(const char* function, const char* category, const char* message) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s @ '%s' [%s]", category, function, message);
    LUA_LOGI("WARNING: %s", buf);
}

void LogBitType(lua_State* L, const char* function, int index) {
    const int type = lua_type(L, index);
    const char* name = "none";
    switch (type) {
        case LUA_TNIL: name = "nil"; break;
        case LUA_TBOOLEAN: name = "boolean"; break;
        case LUA_TNUMBER: name = "number"; break;
        case LUA_TSTRING: name = "string"; break;
        case LUA_TTABLE: name = "table"; break;
        case LUA_TFUNCTION: name = "function"; break;
        case LUA_TUSERDATA: name = "userdata"; break;
        default: break;
    }
    const char* value = (type == LUA_TNUMBER || type == LUA_TSTRING) ? lua_tostring(L, index) : nullptr;
    char buf[256];
    if (value && value[0] != '\0') {
        std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [Expected number at argument %d, got %s '%.64s']",
                      function, index, name, value);
    } else {
        std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [Expected number at argument %d, got %s]",
                      function, index, name);
    }
    LUA_LOGI("WARNING: %s", buf);
}

// checkSign false matches ReadNumber(uint, false). Signed int uses ReadNumber(int).
bool ReadBitUint(lua_State* L, int index, const char* function, uint32_t& out) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        LogBitType(L, function, index);
        return false;
    }
    if (!lua_isnumber(L, index)) {
        LogBit(function, "Bad argument", "Expected number, got non-convertible string");
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        LogBit(function, "Bad argument", "Expected number, got NaN");
        return false;
    }
    // int64 bounds. 2^63 is the first magnitude a double can hold past int64_t.
    if (!std::isfinite(number) || number >= 9223372036854775808.0 || number < -9223372036854775808.0) {
        LogBit(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    out = static_cast<uint32_t>(static_cast<int64_t>(number));
    return true;
}

bool ReadBitInt(lua_State* L, int index, const char* function, int& out, bool optional, int fallback) {
    const int type = lua_type(L, index);
    if (optional && (type == LUA_TNONE || type == LUA_TNIL)) {
        out = fallback;
        return true;
    }
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        LogBitType(L, function, index);
        return false;
    }
    if (!lua_isnumber(L, index)) {
        LogBit(function, "Bad argument", "Expected number, got non-convertible string");
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        LogBit(function, "Bad argument", "Expected number, got NaN");
        return false;
    }
    if (!std::isfinite(number) || number > static_cast<lua_Number>(std::numeric_limits<int>::max()) ||
        number < static_cast<lua_Number>(std::numeric_limits<int>::min())) {
        LogBit(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

int BitFail(lua_State* L, bool nilOnError) {
    if (nilOnError) lua_pushnil(L);
    else lua_pushboolean(L, 0);
    return 1;
}

// Fold further number arguments. Stops at the first non-number, as NextIsNumber does.
bool FoldBits(lua_State* L, const char* function, int start, uint32_t& value, uint32_t (*op)(uint32_t, uint32_t)) {
    for (int index = start; lua_type(L, index) == LUA_TNUMBER; ++index) {
        uint32_t next = 0;
        if (!ReadBitUint(L, index, function, next)) return false;
        value = op(value, next);
    }
    return true;
}

uint32_t BitAndOp(uint32_t a, uint32_t b) { return a & b; }
uint32_t BitOrOp(uint32_t a, uint32_t b) { return a | b; }
uint32_t BitXorOp(uint32_t a, uint32_t b) { return a ^ b; }

int BitAnd(lua_State* L) {
    uint32_t a = 0, b = 0;
    if (!ReadBitUint(L, 1, "bitAnd", a) || !ReadBitUint(L, 2, "bitAnd", b)) return BitFail(L, false);
    uint32_t result = a & b;
    if (!FoldBits(L, "bitAnd", 3, result, BitAndOp)) return BitFail(L, false);
    lua_pushnumber(L, result);
    return 1;
}

int BitNot(lua_State* L) {
    uint32_t value = 0;
    if (!ReadBitUint(L, 1, "bitNot", value)) return BitFail(L, false);
    lua_pushnumber(L, static_cast<lua_Number>(~value));
    return 1;
}

int BitOr(lua_State* L) {
    uint32_t a = 0, b = 0;
    if (!ReadBitUint(L, 1, "bitOr", a) || !ReadBitUint(L, 2, "bitOr", b)) return BitFail(L, false);
    uint32_t result = a | b;
    if (!FoldBits(L, "bitOr", 3, result, BitOrOp)) return BitFail(L, false);
    lua_pushnumber(L, result);
    return 1;
}

// bitTest pushes nil on a bad argument (CLuaBitDefs.cpp 141).
int BitTest(lua_State* L) {
    uint32_t a = 0, b = 0;
    if (!ReadBitUint(L, 1, "bitTest", a) || !ReadBitUint(L, 2, "bitTest", b)) return BitFail(L, true);
    uint32_t result = a & b;
    if (!FoldBits(L, "bitTest", 3, result, BitAndOp)) return BitFail(L, true);
    lua_pushboolean(L, result != 0 ? 1 : 0);
    return 1;
}

int BitXor(lua_State* L) {
    uint32_t a = 0, b = 0;
    if (!ReadBitUint(L, 1, "bitXor", a) || !ReadBitUint(L, 2, "bitXor", b)) return BitFail(L, false);
    uint32_t result = a ^ b;
    if (!FoldBits(L, "bitXor", 3, result, BitXorOp)) return BitFail(L, false);
    lua_pushnumber(L, result);
    return 1;
}

int BitLRotate(lua_State* L) {
    uint32_t value = 0;
    int disp = 0;
    if (!ReadBitUint(L, 1, "bitLRotate", value) || !ReadBitInt(L, 2, "bitLRotate", disp, false, 0)) return BitFail(L, false);
    disp &= 31;
    const uint32_t result = disp == 0 ? value : ((value << disp) | (value >> (32 - disp)));
    lua_pushnumber(L, result);
    return 1;
}

int BitRRotate(lua_State* L) {
    uint32_t value = 0;
    int disp = 0;
    if (!ReadBitUint(L, 1, "bitRRotate", value) || !ReadBitInt(L, 2, "bitRRotate", disp, false, 0)) return BitFail(L, false);
    disp &= 31;
    const uint32_t result = disp == 0 ? value : ((value >> disp) | (value << (32 - disp)));
    lua_pushnumber(L, result);
    return 1;
}

int BitLShift(lua_State* L) {
    uint32_t value = 0;
    int disp = 0;
    if (!ReadBitUint(L, 1, "bitLShift", value) || !ReadBitInt(L, 2, "bitLShift", disp, false, 0)) return BitFail(L, false);
    // CLuaBitDefs.cpp 240-243: disp >= 32 yields 0, otherwise `value << disp`.
    // A negative count is undefined in that shift. A 32-bit shift uses the low 5 bits.
    uint32_t result = 0;
    if (disp >= 32) result = 0;
    else if (disp < 0) result = value << (static_cast<unsigned>(disp) & 31u);
    else result = value << static_cast<unsigned>(disp);
    lua_pushnumber(L, result);
    return 1;
}

int BitRShift(lua_State* L) {
    uint32_t value = 0;
    int disp = 0;
    if (!ReadBitUint(L, 1, "bitRShift", value) || !ReadBitInt(L, 2, "bitRShift", disp, false, 0)) return BitFail(L, false);
    uint32_t result = 0;
    if (disp >= 32) result = 0;
    else if (disp < 0) result = value >> (static_cast<unsigned>(disp) & 31u);
    else result = value >> static_cast<unsigned>(disp);
    lua_pushnumber(L, result);
    return 1;
}

// CLuaBitDefs::bitArShift (CLuaBitDefs.cpp 284-327).
int BitArShift(lua_State* L) {
    uint32_t value = 0;
    int disp = 0;
    if (!ReadBitUint(L, 1, "bitArShift", value) || !ReadBitInt(L, 2, "bitArShift", disp, false, 0)) return BitFail(L, false);
    uint32_t result = 0;
    if (disp < 0 || (value & 0x80000000u) == 0) {
        if (disp <= -32 || disp >= 32) result = 0;
        else if (disp < 0) result = value << static_cast<unsigned>(-disp);
        else result = value >> static_cast<unsigned>(disp);
    } else if (disp >= 32) {
        result = 0xFFFFFFFFu;
    } else {
        result = (value >> static_cast<unsigned>(disp)) | ~(~0u >> static_cast<unsigned>(disp));
    }
    lua_pushnumber(L, result);
    return 1;
}

uint32_t BitFieldMask(int width) {
    if (width >= 32) return 0xFFFFFFFFu;
    if (width <= 0) return 0;
    return (1u << static_cast<unsigned>(width)) - 1u;
}

int BitExtract(lua_State* L) {
    uint32_t value = 0;
    int field = 0;
    int width = 1;
    if (!ReadBitUint(L, 1, "bitExtract", value) || !ReadBitInt(L, 2, "bitExtract", field, false, 0) ||
        !ReadBitInt(L, 3, "bitExtract", width, true, 1))
        return BitFail(L, false);
    if (static_cast<long long>(field) + static_cast<long long>(width) > 32) {
        LogBit("bitExtract", "Bad usage", "Trying to access non-exisistent bits");
        return BitFail(L, false);
    }
    if (field < 0) {
        LogBit("bitExtract", "Bad usage", "field cannot be negative");
        return BitFail(L, false);
    }
    if (width <= 0) {
        LogBit("bitExtract", "Bad usage", "width must be positive");
        return BitFail(L, false);
    }
    lua_pushnumber(L, (value >> static_cast<unsigned>(field)) & BitFieldMask(width));
    return 1;
}

// On a bad argument the desktop function pushes false and then `return false`, which is 0 results.
int BitReplace(lua_State* L) {
    uint32_t value = 0;
    uint32_t replace = 0;
    int field = 0;
    int width = 1;
    if (!ReadBitUint(L, 1, "bitReplace", value) || !ReadBitUint(L, 2, "bitReplace", replace) ||
        !ReadBitInt(L, 3, "bitReplace", field, false, 0) || !ReadBitInt(L, 4, "bitReplace", width, true, 1))
        return 0;
    const long long span = static_cast<long long>(field) + static_cast<long long>(width);
    if (span > 32 || field < 0 || width <= 0) {
        if (span > 32) LogBit("bitReplace", "Bad usage", "Trying to access non-exisistent bits");
        else if (field < 0) LogBit("bitReplace", "Bad usage", "field cannot be negative");
        else LogBit("bitReplace", "Bad usage", "width must be positive");
        return 0;
    }
    const uint32_t mask = BitFieldMask(width);
    replace &= mask;
    lua_pushnumber(L, (value & ~(mask << static_cast<unsigned>(field))) | (replace << static_cast<unsigned>(field)));
    return 1;
}

} // namespace

void PushElement(lua_State* L, uint32_t id) {
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    lua_setfield(L, -2, "id");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "element");
}

uint32_t ElementIdOf(lua_State* L, int index) {
    if (!lua_istable(L, index)) return 0;
    lua_getfield(L, index, "id");
    const uint32_t id = lua_isinteger(L, -1) ? static_cast<uint32_t>(lua_tointeger(L, -1)) : 0;
    lua_pop(L, 1);
    return id;
}

void InstallExtraBindings(lua_State* L) {
    InstallJsonBindings(L);
    WorldRPG::Net::RemoteCalls::BindState(L);
    SetGlobal(L, "fetchRemote", WorldRPG::Net::RemoteCalls::LuaFetchRemote);
    SetGlobal(L, "getRemoteRequests", WorldRPG::Net::RemoteCalls::LuaGetRemoteRequests);
    SetGlobal(L, "getRemoteRequestInfo", WorldRPG::Net::RemoteCalls::LuaGetRemoteRequestInfo);
    SetGlobal(L, "abortRemoteRequest", WorldRPG::Net::RemoteCalls::LuaAbortRemoteRequest);
    SetGlobal(L, "getServerIp", WorldRPG::Net::RemoteCalls::LuaGetServerIp);
    SetGlobal(L, "outputChatBox", OutputChatBox);
    SetGlobal(L, "showChat", ShowChat);
    SetGlobal(L, "isChatVisible", IsChatVisible);
    SetGlobal(L, "clearChatBox", ClearChatBox);
    SetGlobal(L, "toggleControl", ToggleControl);
    SetGlobal(L, "isControlEnabled", IsControlEnabled);
    SetGlobal(L, "toggleAllControls", ToggleAllControls);
    SetGlobal(L, "getAnalogControlState", GetAnalogControlState);
    SetGlobal(L, "setAnalogControlState", SetAnalogControlState);
    SetGlobal(L, "setMobileRadarArea", SetMobileRadarArea);
    SetGlobal(L, "iprint", Iprint);
    SetGlobal(L, "debugMessage", DebugMessage);
    SetGlobal(L, "getElementData", GetElementData);
    SetGlobal(L, "setElementData", SetElementData);
    SetGlobal(L, "Vector3", Vector3New);
    SetGlobal(L, "getCameraMatrix", GetCameraMatrix);
    InstallGuiBindings(L);
    SetGlobal(L, "fileOpen", FileOpen);
    SetGlobal(L, "bitAnd", BitAnd);
    SetGlobal(L, "bitNot", BitNot);
    SetGlobal(L, "bitOr", BitOr);
    SetGlobal(L, "bitTest", BitTest);
    SetGlobal(L, "bitXor", BitXor);
    SetGlobal(L, "bitLRotate", BitLRotate);
    SetGlobal(L, "bitRRotate", BitRRotate);
    SetGlobal(L, "bitLShift", BitLShift);
    SetGlobal(L, "bitRShift", BitRShift);
    SetGlobal(L, "bitArShift", BitArShift);
    SetGlobal(L, "bitExtract", BitExtract);
    SetGlobal(L, "bitReplace", BitReplace);
    SetGlobal(L, "createVehicle", FalseResult);
    SetGlobal(L, "createMarker", FalseResult);
    SetGlobal(L, "createColSphere", FalseResult);
    SetGlobal(L, "createObject", FalseResult);
    SetGlobal(L, "createBlip", FalseResult);
    SetGlobal(L, "createPed", FalseResult);
    SetGlobal(L, "setElementPosition", TrueResult);
    SetGlobal(L, "setElementHealth", TrueResult);
    SetGlobal(L, "setPedAnimation", TrueResult);

    // Defaults for natives with no engine hook yet. Same values as the existing
    // ped-traffic stubs in LuaVMStub.cpp; the core bindings take precedence.
    const struct { const char* name; DefaultKind kind; } defaults[] = {
        {"setElementVelocity", kTrue}, {"setElementRotation", kTrue}, {"setElementAngularVelocity", kTrue},
        {"setElementFrozen", kTrue}, {"setElementCollisionsEnabled", kTrue}, {"getElementCollisionsEnabled", kTrue},
        {"getElementAlpha", kAlpha255}, {"getElementAngularVelocity", kZero3},
        {"getElementDistanceFromCentreOfMassToBaseOfModel", kZero}, {"getElementMatrix", kNil},
        {"getVehicleStraightLineDistance", kZero}, {"getVehicleEngineState", kFalse}, {"getVehicleOverrideLights", kZero},
        {"isVehicleLocked", kFalse}, {"isVehicleBlown", kFalse}, {"isVehicleOnGround", kFalse},
        {"isVehicleRecordingLoaded", kFalse}, {"isVehiclePlaybackActive", kFalse}, {"requestVehicleRecording", kFalse},
        {"startVehiclePlayback", kFalse}, {"stopVehiclePlayback", kTrue}, {"setVehiclePlaybackSpeed", kTrue},
        {"setVehicleLoadCollisionFlag", kTrue}, {"setVehicleStraightLineDistance", kTrue},
        {"setVehicleTyresCanBurst", kTrue}, {"setVehicleOverrideLights", kTrue}, {"setVehicleEngineState", kTrue},
        {"setVehicleColor", kTrue},
        // CLuaCompatibilityDefs.cpp:28-29. Same defaults as the ped task queries.
        {"isPedDoingTask", kFalse}, {"isPlayerDoingTask", kFalse}, {"isPedMissionActor", kFalse},
        {"setPedMissionActor", kTrue}, {"getPedSimplestTask", kEmptyString}, {"getPlayerSimplestTask", kEmptyString},
        {"getPedTaskSequenceProgress", kZero},
        {"getPedPhysicalProofs", kFalse}, {"getPedSuffersCriticalHits", kFalse}, {"canPedBeKnockedOffBike", kFalse},
        // CLuaCompatibilityDefs.cpp:18. Same default as canPedBeKnockedOffBike.
        {"canPlayerBeKnockedOffBike", kFalse},
        {"canPedBeDraggedOut", kFalse}, {"isPedOnlyDamagedByPlayer", kFalse}, {"isPedNeverTargeted", kFalse},
        {"isPedDoingGangDriveby", kFalse}, {"getPedNativeVehicleResponse", kNil}, {"getPedCameraRotation", kZero},
        {"getPedBonePosition", kZero3}, {"getPedNativeGroupDiagnostic", kFalse},
        {"setPedPhysicalProofs", kTrue}, {"setPedCanBeKnockedOffBike", kTrue},
        // CLuaCompatibilityDefs.cpp:48.
        {"setPlayerCanBeKnockedOffBike", kTrue}, {"setPedSuffersCriticalHits", kTrue},
        {"setPedOnlyDamagedByPlayer", kTrue}, {"setPedNeverTargeted", kTrue}, {"setPedDriveBy", kTrue},
        {"setPedDriveMission", kTrue}, {"setPedKillOnFoot", kTrue}, {"setPedDriveTo", kTrue}, {"setPedGoTo", kTrue},
        {"setPedTurnToFace", kTrue}, {"setPedAimAt", kTrue}, {"setPedHandsUp", kTrue}, {"setPedEnterVehicle", kTrue},
        {"setPedExitVehicle", kTrue}, {"setPedTaskSequence", kTrue}, {"setPedCanBeDraggedOut", kTrue},
        {"restorePedNativeVehicleResponse", kTrue}, {"releasePedNativePointArm", kTrue},
        {"acquirePedNativePointArm", kFalse}, {"isLineOfSightClear", kFalse}, {"getWaterLevel", kNil},
        {"getScreenFromWorldPosition", kNil},
        // Physical attachment has no mobile hook yet; do not report a successful mutation.
        {"executeCommandHandler", kFalse}, {"attachElements", kFalse}, {"detachElements", kFalse},
        // CLuaCompatibilityDefs.cpp:60-61. Same defaults as attachElements and detachElements.
        {"attachElementToElement", kFalse}, {"detachElementFromElement", kFalse},
        {"setObjectMaterial", kTrue},
        {"playSoundFrontEnd", kFalse}, {"playSound3D", kFalse}, {"playSound", kFalse}, {"stopSound", kTrue},
        {"setSoundVolume", kTrue}, {"setSoundMaxDistance", kTrue}, {"setSoundMinDistance", kTrue},
        {"setSoundPosition", kTrue}, {"setSoundDopplerEnabled", kTrue},
        {"setCameraTarget", kTrue}, {"setCameraInterior", kTrue}, {"setCameraFieldOfView", kTrue},
        {"fadeCamera", kTrue}, {"getPedCameraRotation", kZero},
        {"engineLoadIMG", kNil}, {"engineAddImage", kNil}, {"engineLoadCOL", kNil}, {"engineParseSAMPMap", kNil},
        {"engineFreeTXD", kTrue}, {"engineFreeModel", kTrue}, {"engineImageLinkTXD", kTrue},
        {"engineImageLinkDFF", kTrue}, {"engineSetModelTXDID", kTrue}, {"engineReplaceCOL", kTrue},
        {"engineSetModelLODDistance", kTrue}, {"engineSetModelFlags", kTrue}, {"engineRequestTXD", kZero},
        {"engineGetTXDIDFromName", kZero}, {"engineRequestModel", kZero},
        {"restoreWorldModel", kTrue}, {"removeWorldModel", kTrue},
        {"fileRead", kEmptyString}, {"fileGetSize", kZero}, {"fileClose", kTrue},
        {"updatePedNativePointArm", kFalse},
        {"fxAddBlood", kTrue},
    };
    for (const auto& entry : defaults) SetDefault(L, entry.name, entry.kind);
}

} // namespace WorldRPG::Script
