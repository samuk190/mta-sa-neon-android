#include "VehicleAudio.h"

#include "LuaVMStub.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <string>

namespace WorldRPG::Script {
namespace {

std::string Bad(const char* function, const char* expected, int index, const std::string& got) {
    return std::string("Bad argument @ '") + function + "' [Expected " + expected + " at argument " + std::to_string(index) + ", got " + got + "]";
}

std::string Usage(const char* function, const char* category, const std::string& reason) {
    return category + std::string(" @ '") + function + "' [" + reason + "]";
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
        case LUA_TTABLE: return "table";
        case LUA_TUSERDATA: return "userdata";
        default: return lua_typename(L, lua_type(L, index));
    }
}

int LoadConfig(lua_State* L) {
    const int type = lua_type(L, 1);
    std::string input;
    std::string message;
    if (type != LUA_TSTRING && type != LUA_TNUMBER) {
        message = Bad("engineLoadVehicleAudioConfig", "string", 1, Got(L, 1));
    } else if (type == LUA_TNUMBER) {
        message = Usage("engineLoadVehicleAudioConfig", "Expected a non-empty vehicle audio configuration path", "");
    } else {
        size_t length = 0;
        const char* text = lua_tolstring(L, 1, &length);
        input.assign(text ? text : "", text ? length : 0);
        if (input.empty()) message = Usage("engineLoadVehicleAudioConfig", "Expected a non-empty vehicle audio configuration path", "");
        else message = Usage("engineLoadVehicleAudioConfig", "Unable to load vehicle audio configuration", input);
    }
    LUA_LOGE("%s", message.c_str());
    lua_pushboolean(L, 0);
    lua_pushstring(L, message.c_str());
    return 2;
}

int ReloadConfig(lua_State* L) {
    (void)L;
    if (ActiveVmOwnerNet() != 0) LUA_LOGI("DIAG engineReloadVehicleAudioConfig: CVehicleSoundManager is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int UnloadConfig(lua_State* L) {
    (void)L;
    if (ActiveVmOwnerNet() != 0) LUA_LOGI("DIAG engineUnloadVehicleAudioConfig: CVehicleSoundManager is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int PlayBackfire(lua_State* L) {
    const std::string message = Bad("enginePlayVehicleAudioBackfire", "vehicle", 1, Got(L, 1));
    LUA_LOGE("%s", message.c_str());
    lua_pushboolean(L, 0);
    return 1;
}

}  // namespace

void BindVehicleAudioLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"engineLoadVehicleAudioConfig", LoadConfig},
        {"engineReloadVehicleAudioConfig", ReloadConfig},
        {"engineUnloadVehicleAudioConfig", UnloadConfig},
        {"enginePlayVehicleAudioBackfire", PlayBackfire},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
