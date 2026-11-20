// Browser Lua API. Desktop CLuaBrowserDefs.cpp.
// g_pCore->GetWebCore() is absent (no CEF and no Android WebView yet). Functions
// follow the desktop branch for that case: size errors are still Lua errors,
// getBrowserSettings still returns the three flags, and the rest return false or nil.

#include "LuaBrowserApi.h"
#include "LuaVMStub.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <cmath>
#include <cstring>

namespace WorldRPG::Script {
namespace {

bool WebCorePresent() { return false; }

int False(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

int MissingBrowser(lua_State* L) {
    if (!lua_istable(L, 1)) LUA_LOGE("browser function: expected a browser");
    return False(L);
}

bool Pair(lua_State* L, int index, double& x, double& y) {
    if (!lua_isnumber(L, index) || !lua_isnumber(L, index + 1)) return false;
    x = lua_tonumber(L, index);
    y = lua_tonumber(L, index + 1);
    return std::isfinite(x) && std::isfinite(y);
}

const char* SizeReason(double width, double height) {
    if (width < 0) return "Browser width is smaller than 0";
    if (height < 0) return "Browser height is smaller than 0";
    if (width == 0 || height == 0) return "A browser must be at least 1x1 in size.";
    return nullptr;
}

// CLuaBrowserDefs.cpp:114-178. A valid size still returns false without a web core.
int CreateBrowser(lua_State* L) {
    double width = 0, height = 0;
    if (!Pair(L, 1, width, height) || lua_type(L, 3) != LUA_TBOOLEAN)
        return luaL_error(L, "Bad argument @ 'createBrowser' [Expected number, number, boolean]");
    if (const char* reason = SizeReason(width, height))
        return luaL_error(L, "Invalid parameter @ 'createBrowser' [%s]", reason);
    return False(L);
}

// CLuaBrowserDefs.cpp:955-1018. Remote pages are off. A local browser cannot be created.
int GuiCreateBrowser(lua_State* L) {
    double x = 0, y = 0, width = 0, height = 0;
    if (!Pair(L, 1, x, y) || !Pair(L, 3, width, height) || lua_type(L, 5) != LUA_TBOOLEAN ||
        lua_type(L, 6) != LUA_TBOOLEAN)
        return luaL_error(L, "Bad argument @ 'guiCreateBrowser' [Expected position, size and booleans]");
    if (const char* reason = SizeReason(width, height))
        return luaL_error(L, "Invalid parameter @ 'guiCreateBrowser' [%s]", reason);
    if (!lua_toboolean(L, 5)) return False(L);
    return luaL_error(L, "Create browser @ 'guiCreateBrowser' [Failed to create browser element]");
}

// CLuaBrowserDefs.cpp:181-253. No web core, so the request is false. Bad arguments are false too.
int RequestBrowserDomains(lua_State* L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        LUA_LOGE("requestBrowserDomains: expected a table");
        return False(L);
    }
    return False(L);
}

// CLuaBrowserDefs.cpp:256-286. The system browser is not hooked, including the trusted https routes.
int OpenExternalURL(lua_State* L) {
    const char* url = lua_tostring(L, 1);
    if (!url) {
        LUA_LOGE("openExternalURL: expected a string");
        return False(L);
    }
    const bool https = std::strncmp(url, "https://", 8) == 0 && std::strlen(url) <= 4096;
    const char* coffee = "https://buymeacoffee.com/southlandfr/";
    const char* coffeeWww = "https://www.buymeacoffee.com/southlandfr/";
    const bool trusted = https && (std::strcmp(url, "https://discord.gg/gtabust") == 0 ||
        std::strncmp(url, coffee, std::strlen(coffee)) == 0 ||
        std::strncmp(url, coffeeWww, std::strlen(coffeeWww)) == 0);
    if (trusted) {
        static bool once = false;
        if (!once) {
            once = true;
            LUA_LOGI("DIAG openExternalURL: the Android browser intent is not hooked");
        }
    }
    return False(L);
}

// CLuaBrowserDefs.cpp:734-753. The three settings exist with web-core values, which are false here.
int GetBrowserSettings(lua_State* L) {
    (void)L;
    lua_createtable(L, 0, 3);
    lua_pushstring(L, "RemoteEnabled");
    lua_pushboolean(L, WebCorePresent() ? 1 : 0);
    lua_settable(L, -3);
    lua_pushstring(L, "RemoteJavascript");
    lua_pushboolean(L, 0);
    lua_settable(L, -3);
    lua_pushstring(L, "PluginsEnabled");
    lua_pushboolean(L, 0);
    lua_settable(L, -3);
    return 1;
}

// CLuaBrowserDefs.cpp:1135-1138.
int IsBrowserGPUEnabled(lua_State* L) {
    lua_pushboolean(L, WebCorePresent() ? 1 : 0);
    return 1;
}

// CLuaBrowserDefs.cpp:594-626. No web core pushes nil.
int IsBrowserDomainBlocked(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING && lua_type(L, 1) != LUA_TNUMBER) {
        LUA_LOGE("isBrowserDomainBlocked: expected a string");
    }
    lua_pushnil(L);
    return 1;
}

// CLuaBrowserDefs.cpp:563-591. A global volume number hits SetGlobalAudioVolume, which needs the web core.
int SetBrowserVolume(lua_State* L) {
    if (lua_isnumber(L, 1) && WebCorePresent()) {
        lua_pushboolean(L, 1);
        return 1;
    }
    return False(L);
}

}  // namespace

void InstallBrowserLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createBrowser", CreateBrowser},
        {"requestBrowserDomains", RequestBrowserDomains},
        {"openExternalURL", OpenExternalURL},
        {"loadBrowserURL", MissingBrowser},
        {"isBrowserLoading", MissingBrowser},
        {"injectBrowserMouseMove", MissingBrowser},
        {"injectBrowserMouseDown", MissingBrowser},
        {"injectBrowserMouseUp", MissingBrowser},
        {"injectBrowserMouseWheel", MissingBrowser},
        {"getBrowserTitle", MissingBrowser},
        {"getBrowserURL", MissingBrowser},
        {"setBrowserRenderingPaused", MissingBrowser},
        {"isBrowserRenderingPaused", MissingBrowser},
        {"executeBrowserJavascript", MissingBrowser},
        {"getBrowserVolume", MissingBrowser},
        {"setBrowserVolume", SetBrowserVolume},
        {"isBrowserDomainBlocked", IsBrowserDomainBlocked},
        {"focusBrowser", MissingBrowser},
        {"isBrowserFocused", MissingBrowser},
        {"setBrowserProperty", MissingBrowser},
        {"getBrowserProperty", MissingBrowser},
        {"getBrowserSettings", GetBrowserSettings},
        {"getBrowserSource", MissingBrowser},
        {"setBrowserAjaxHandler", MissingBrowser},
        {"canBrowserNavigateBack", MissingBrowser},
        {"canBrowserNavigateForward", MissingBrowser},
        {"navigateBrowserBack", MissingBrowser},
        {"navigateBrowserForward", MissingBrowser},
        {"reloadBrowserPage", MissingBrowser},
        {"toggleBrowserDevTools", MissingBrowser},
        {"resizeBrowser", MissingBrowser},
        {"guiCreateBrowser", GuiCreateBrowser},
        {"guiGetBrowser", MissingBrowser},
        {"isBrowserGPUEnabled", IsBrowserGPUEnabled},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
