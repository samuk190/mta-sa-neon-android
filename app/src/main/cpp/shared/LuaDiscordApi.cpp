// Discord Rich Presence. Desktop CLuaDiscordDefs.cpp.
// g_pCore->GetDiscord() is absent, so IsDiscordRPCEnabled() is false and every
// setter returns false after the same length checks. ArgumentParser turns
// std::invalid_argument into luaL_error with that text (CLuaFunctionParser.h 813-829).

#include "LuaDiscordApi.h"
#include "LuaVMStub.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <cstring>
#include <string>

namespace WorldRPG::Script {
namespace {

bool DiscordReady() { return false; }

int NotReady(lua_State* L) {
    lua_pushboolean(L, DiscordReady() ? 1 : 0);
    return 1;
}

bool Text(lua_State* L, int index, const char* name, std::string& out) {
    if (lua_type(L, index) != LUA_TSTRING && lua_type(L, index) != LUA_TNUMBER) {
        return luaL_error(L, "Bad argument @ '%s' [Expected string at argument %d]", name, index) != 0;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    if (!text) return luaL_error(L, "Bad argument @ '%s' [Expected string at argument %d]", name, index) != 0;
    out.assign(text, length);
    return true;
}

int SetDetails(lua_State* L) {
    std::string text;
    Text(L, 1, "setDiscordRichPresenceDetails", text);
    if (text.size() > 128) return luaL_error(L, "Details length must be less than/equal to 128");
    return NotReady(L);
}

int SetState(lua_State* L) {
    std::string text;
    Text(L, 1, "setDiscordRichPresenceState", text);
    if (text.size() > 128) return luaL_error(L, "State must be less than/equal to 128");
    return NotReady(L);
}

int SetAppID(lua_State* L) {
    std::string text;
    Text(L, 1, "setDiscordApplicationID", text);
    if (text.empty() || text.size() > 32)
        return luaL_error(L, "Application ID must be greater than 0, or less than/equal to 32");
    return NotReady(L);
}

int SetAsset(lua_State* L, const char* name) {
    std::string asset;
    std::string hover;
    Text(L, 1, name, asset);
    Text(L, 2, name, hover);
    if (asset.size() > 32) return luaL_error(L, "Asset name length must be less than/equal to 32");
    if (hover.size() > 128) return luaL_error(L, "Asset text length must be less than/equal to 128");
    return NotReady(L);
}

int SetLargeAsset(lua_State* L) { return SetAsset(L, "setDiscordRichPresenceAsset"); }
int SetSmallAsset(lua_State* L) { return SetAsset(L, "setDiscordRichPresenceSmallAsset"); }

int SetButtons(lua_State* L) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "Bad argument @ 'setDiscordRichPresenceButton' [Expected number at argument 1]");
    const double index = lua_tonumber(L, 1);
    std::string name;
    std::string url;
    Text(L, 2, "setDiscordRichPresenceButton", name);
    Text(L, 3, "setDiscordRichPresenceButton", url);
    if (index < 1.0 || index >= 3.0) return NotReady(L);
    const unsigned button = static_cast<unsigned>(index);
    if (button < 1 || button > 2) return NotReady(L);
    if (name.size() > 32) return luaL_error(L, "Button name length must be less than/equal to 32");
    if (url.size() > 128) return luaL_error(L, "Button URL length must be less than/equal to 128");
    if (url.compare(0, 8, "https://") != 0 && url.compare(0, 8, "mtasa://") != 0)
        return luaL_error(L, "Button URL should include the https:// or mtasa:// link");
    return NotReady(L);
}

int SetTime(lua_State* L, const char* name) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "Bad argument @ '%s' [Expected number at argument 1]", name);
    return NotReady(L);
}

int SetStartTime(lua_State* L) { return SetTime(L, "setDiscordRichPresenceStartTime"); }
int SetEndTime(lua_State* L) { return SetTime(L, "setDiscordRichPresenceEndTime"); }

int SetPartySize(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2))
        return luaL_error(L, "Bad argument @ 'setDiscordRichPresencePartySize' [Expected number]");
    if (lua_tonumber(L, 1) > lua_tonumber(L, 2))
        return luaL_error(L, "Party size must be less than or equal to max party size");
    return NotReady(L);
}

int GetUserID(lua_State* L) {
    // CLuaDiscordDefs::GetDiscordUserID returns an empty string when RPC is off.
    (void)L;
    if (DiscordReady()) {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushstring(L, "");
    return 1;
}

}  // namespace

void InstallDiscordLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"setDiscordApplicationID", SetAppID},
        {"setDiscordRichPresenceDetails", SetDetails},
        {"setDiscordRichPresenceState", SetState},
        {"setDiscordRichPresenceAsset", SetLargeAsset},
        {"setDiscordRichPresenceSmallAsset", SetSmallAsset},
        {"setDiscordRichPresenceButton", SetButtons},
        {"setDiscordRichPresenceStartTime", SetStartTime},
        {"setDiscordRichPresenceEndTime", SetEndTime},
        {"setDiscordRichPresencePartySize", SetPartySize},
        {"resetDiscordRichPresenceData", NotReady},
        {"isDiscordRichPresenceConnected", NotReady},
        {"getDiscordRichPresenceUserID", GetUserID},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
