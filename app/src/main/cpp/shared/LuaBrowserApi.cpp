// Browser Lua API (desktop CLuaBrowserDefs.cpp) backed by Android offscreen WebView (BrowserEngine.java).
// Each browser element is a live DX texture ("tx:<id>") usable in dxDrawImage, dxGetMaterialSize,
// dxGetTexturePixels, and guiCreateBrowser.

#include "LuaBrowserApi.h"
#include "LuaApiExtra.h"
#include "LuaFileApi.h"
#include "LuaGuiApi.h"
#include "LuaVMStub.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text);
}

namespace WorldRPG::Script {
namespace {

constexpr uint32_t kFirstBrowserId = 0x64000001u;
constexpr int kMaxBrowsers = 32;
constexpr unsigned kMaxBrowserSide = 4096;

struct BrowserState {
    uint32_t id = 0;
    unsigned width = 0;
    unsigned height = 0;
    bool isLocal = true;
    bool transparent = false;
    bool loading = false;
    bool renderingPaused = false;
    bool devTools = false;
    bool canBack = false;
    bool canForward = false;
    float volume = 1.0f;
    std::string url;
    std::string title;
    std::string lastHtmlSource;
    std::vector<std::string> history;
    int historyIndex = -1;
    std::unordered_map<std::string, std::string> properties;
    std::unordered_map<std::string, int> ajaxHandlerRefs;
    std::vector<int> sourceCallbacks;
    std::string owner;
    uint16_t ownerNetId = 0;
    uint32_t ownerGeneration = 0;
};

struct BrowserEvent {
    enum class Kind : uint8_t {
        Created,
        LoadingStart,
        DocumentReady,
        LoadingFailed,
        Navigate,
        Popup,
        SourceReady,
        InputFocus,
        WhitelistChange,
    };
    Kind kind = Kind::Created;
    uint32_t id = 0;
    bool flagA = false;
    bool flagB = false;
    int code = 0;
    std::string textA;
    std::string textB;
    std::vector<std::string> domains;
    int callbackRef = LUA_NOREF;
};

std::mutex g_browserMu;
std::vector<BrowserState> g_browsers;
uint32_t g_nextBrowserId = kFirstBrowserId;
uint32_t g_focusedBrowserId = 0;
float g_globalBrowserVolume = 1.0f;
std::set<std::string> g_whitelistedDomains;
std::vector<BrowserEvent> g_browserEvents;

std::string ToLowerStr(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string ExtractDomain(const std::string& raw, bool parseAsUrl) {
    std::string s = raw;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    if (s.empty()) return {};
    if (parseAsUrl || s.find("://") != std::string::npos) {
        const size_t scheme = s.find("://");
        if (scheme != std::string::npos) s = s.substr(scheme + 3);
        const size_t slash = s.find_first_of("/?#");
        if (slash != std::string::npos) s = s.substr(0, slash);
        const size_t at = s.rfind('@');
        if (at != std::string::npos) s = s.substr(at + 1);
        const size_t colon = s.find(':');
        if (colon != std::string::npos) s = s.substr(0, colon);
    } else {
        const size_t slash = s.find_first_of("/?#");
        if (slash != std::string::npos) s = s.substr(0, slash);
        const size_t colon = s.find(':');
        if (colon != std::string::npos) s = s.substr(0, colon);
    }
    return ToLowerStr(s);
}

bool IsLocalMtaUrl(const std::string& url) {
    const std::string lower = ToLowerStr(url);
    return lower.starts_with("http://mta/") || lower.starts_with("https://mta/") ||
           lower.starts_with("mta://") || lower.starts_with("about:blank") ||
           lower.starts_with("data:");
}

bool IsDomainWhitelistedLocked(const std::string& domain) {
    if (domain.empty() || domain == "mta" || domain == "local") return true;
    if (g_whitelistedDomains.count(domain) > 0) return true;
    // Match parent domain (e.g. "sub.example.com" allowed when "example.com" is whitelisted).
    size_t dot = domain.find('.');
    while (dot != std::string::npos && dot + 1 < domain.size()) {
        const std::string parent = domain.substr(dot + 1);
        if (g_whitelistedDomains.count(parent) > 0) return true;
        dot = domain.find('.', dot + 1);
    }
    return false;
}

BrowserState* FindBrowserLocked(uint32_t id) {
    if (!id) return nullptr;
    for (auto& b : g_browsers) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

void PostBrowserCmd(const std::string& cmd) {
    ::WorldRPG::JniBridge::PostGameLine(23, cmd.c_str());
}

void ReleaseRefsLocked(lua_State* L, BrowserState& b) {
    if (!L) return;
    for (int ref : b.sourceCallbacks) {
        if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    b.sourceCallbacks.clear();
    for (auto& [k, ref] : b.ajaxHandlerRefs) {
        if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    b.ajaxHandlerRefs.clear();
}

int False(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
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

uint32_t AllocBrowser(unsigned width, unsigned height, bool isLocal, bool transparent) {
    uint32_t id = 0;
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        if (static_cast<int>(g_browsers.size()) >= kMaxBrowsers) return 0;
        BrowserState state;
        state.id = id = g_nextBrowserId++;
        state.width = std::clamp(width, 1u, kMaxBrowserSide);
        state.height = std::clamp(height, 1u, kMaxBrowserSide);
        state.isLocal = isLocal;
        state.transparent = transparent;
        state.volume = g_globalBrowserVolume;
        state.owner = ActiveVmOwner();
        state.ownerNetId = ActiveVmOwnerNet();
        state.ownerGeneration = ActiveVmOwnerGeneration();
        g_browsers.push_back(std::move(state));

        BrowserEvent ev;
        ev.kind = BrowserEvent::Kind::Created;
        ev.id = id;
        g_browserEvents.push_back(std::move(ev));
    }
    char cmd[128];
    std::snprintf(cmd, sizeof(cmd), "create|%u|%u|%u|%d|%d",
                  id, std::clamp(width, 1u, kMaxBrowserSide), std::clamp(height, 1u, kMaxBrowserSide),
                  isLocal ? 1 : 0, transparent ? 1 : 0);
    PostBrowserCmd(cmd);
    return id;
}

std::string TryReadLocalHtmlSource(const std::string& url) {
    // Support http://mta/local/<path> and mta://local/<path> for host/local source lookup.
    std::string rel;
    const std::string lower = ToLowerStr(url);
    if (lower.starts_with("http://mta/local/")) rel = url.substr(17);
    else if (lower.starts_with("https://mta/local/")) rel = url.substr(18);
    else if (lower.starts_with("mta://local/")) rel = url.substr(12);
    else return {};
    const size_t q = rel.find_first_of("?#");
    if (q != std::string::npos) rel = rel.substr(0, q);
    std::filesystem::path resolved;
    if (!ResolveFilePath(rel, resolved)) return {};
    std::ifstream in(resolved, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// CLuaBrowserDefs.cpp:114-178.
int CreateBrowser(lua_State* L) {
    double width = 0, height = 0;
    if (!Pair(L, 1, width, height) || lua_type(L, 3) != LUA_TBOOLEAN)
        return luaL_error(L, "Bad argument @ 'createBrowser' [Expected number, number, boolean]");
    if (const char* reason = SizeReason(width, height))
        return luaL_error(L, "Invalid parameter @ 'createBrowser' [%s]", reason);
    const bool isLocal = lua_toboolean(L, 3) != 0;
    const bool transparent = lua_isboolean(L, 4) ? (lua_toboolean(L, 4) != 0) : false;
    const uint32_t id = AllocBrowser(static_cast<unsigned>(width), static_cast<unsigned>(height), isLocal, transparent);
    if (!id) return False(L);
    PushElement(L, id);
    return 1;
}

// CLuaBrowserDefs.cpp:955-1018.
int GuiCreateBrowser(lua_State* L) {
    double x = 0, y = 0, width = 0, height = 0;
    if (!Pair(L, 1, x, y) || !Pair(L, 3, width, height) || lua_type(L, 5) != LUA_TBOOLEAN ||
        lua_type(L, 6) != LUA_TBOOLEAN)
        return luaL_error(L, "Bad argument @ 'guiCreateBrowser' [Expected position, size and booleans]");
    if (const char* reason = SizeReason(width, height))
        return luaL_error(L, "Invalid parameter @ 'guiCreateBrowser' [%s]", reason);
    const bool isLocal = lua_toboolean(L, 5) != 0;
    const bool transparent = lua_toboolean(L, 6) != 0;
    const bool relative = lua_isboolean(L, 7) ? (lua_toboolean(L, 7) != 0) : false;
    uint32_t parentId = 0;
    if (lua_istable(L, 8)) parentId = ElementIdOf(L, 8);

    int screenW = 1280, screenH = 720;
    ActiveVmScreenSize(screenW, screenH);
    const unsigned pxW = static_cast<unsigned>(std::max(1.0, relative ? width * screenW : width));
    const unsigned pxH = static_cast<unsigned>(std::max(1.0, relative ? height * screenH : height));
    const uint32_t browserId = AllocBrowser(pxW, pxH, isLocal, transparent);
    if (!browserId) {
        return luaL_error(L, "Create browser @ 'guiCreateBrowser' [Failed to create browser element]");
    }
    const uint32_t guiId = GuiCreateBrowserHost(x, y, width, height, relative, parentId, browserId);
    if (!guiId) {
        BrowserDestroy(browserId);
        return False(L);
    }
    PushElement(L, guiId);
    return 1;
}

int GuiGetBrowser(lua_State* L) {
    const uint32_t guiId = ElementIdOf(L, 1);
    const uint32_t browserId = GuiGetBrowserId(guiId);
    if (!browserId || !BrowserAlive(browserId)) return False(L);
    PushElement(L, browserId);
    return 1;
}

// CLuaBrowserDefs.cpp:181-253.
int RequestBrowserDomains(lua_State* L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        LUA_LOGE("requestBrowserDomains: expected a table");
        return False(L);
    }
    const bool parseAsUrl = lua_isboolean(L, 2) ? (lua_toboolean(L, 2) != 0) : false;
    int callbackRef = LUA_NOREF;
    if (lua_isfunction(L, 3)) {
        lua_pushvalue(L, 3);
        callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    std::vector<std::string> requested;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        if (lua_isstring(L, -1)) {
            const std::string dom = ExtractDomain(lua_tostring(L, -1), parseAsUrl);
            if (!dom.empty()) requested.push_back(dom);
        }
        lua_pop(L, 1);
    }

    std::vector<std::string> added;
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        for (const auto& dom : requested) {
            if (g_whitelistedDomains.insert(dom).second) added.push_back(dom);
        }
        if (!added.empty() || callbackRef != LUA_NOREF) {
            BrowserEvent ev;
            ev.kind = BrowserEvent::Kind::WhitelistChange;
            ev.flagA = true;
            ev.domains = added;
            ev.callbackRef = callbackRef;
            g_browserEvents.push_back(std::move(ev));
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaBrowserDefs.cpp:256-286. Opens trusted https:// URLs via Android Intent.ACTION_VIEW.
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
    if (!trusted) return False(L);
    ::WorldRPG::JniBridge::PostGameLine(24, url);
    lua_pushboolean(L, 1);
    return 1;
}

int LoadBrowserURL(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!id || !lua_isstring(L, 2)) return False(L);
    const std::string url = lua_tostring(L, 2);
    if (url.empty()) return False(L);

    std::string localHtml;
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        BrowserState* b = FindBrowserLocked(id);
        if (!b) return False(L);

        const bool isLocalUrl = IsLocalMtaUrl(url);
        if (b->isLocal && !isLocalUrl) {
            BrowserEvent nav;
            nav.kind = BrowserEvent::Kind::Navigate;
            nav.id = id;
            nav.textA = url;
            nav.flagA = true;  // blocked
            nav.flagB = true;  // main frame
            g_browserEvents.push_back(std::move(nav));
            return False(L);
        }
        if (!b->isLocal && !isLocalUrl) {
            const std::string dom = ExtractDomain(url, true);
            if (!IsDomainWhitelistedLocked(dom)) {
                BrowserEvent nav;
                nav.kind = BrowserEvent::Kind::Navigate;
                nav.id = id;
                nav.textA = url;
                nav.flagA = true;
                nav.flagB = true;
                g_browserEvents.push_back(std::move(nav));
                return False(L);
            }
        }

        b->url = url;
        b->loading = true;
        if (b->historyIndex + 1 < static_cast<int>(b->history.size())) {
            b->history.resize(static_cast<size_t>(b->historyIndex + 1));
        }
        b->history.push_back(url);
        b->historyIndex = static_cast<int>(b->history.size()) - 1;
        b->canBack = b->historyIndex > 0;
        b->canForward = false;

        if (isLocalUrl) localHtml = TryReadLocalHtmlSource(url);
        if (!localHtml.empty()) b->lastHtmlSource = localHtml;

        BrowserEvent nav;
        nav.kind = BrowserEvent::Kind::Navigate;
        nav.id = id;
        nav.textA = url;
        nav.flagA = false;
        nav.flagB = true;
        g_browserEvents.push_back(std::move(nav));

        BrowserEvent start;
        start.kind = BrowserEvent::Kind::LoadingStart;
        start.id = id;
        start.textA = url;
        start.flagA = true;
        g_browserEvents.push_back(std::move(start));

#ifndef __ANDROID__
        // On host (no Android WebView), queue DocumentReady so the next VM pump completes the load.
        BrowserEvent ready;
        ready.kind = BrowserEvent::Kind::DocumentReady;
        ready.id = id;
        ready.textA = url;
        ready.textB = b->title;
        ready.flagA = b->canBack;
        ready.flagB = b->canForward;
        g_browserEvents.push_back(std::move(ready));
#endif
    }
    PostBrowserCmd("load|" + std::to_string(id) + "|" + url);
    lua_pushboolean(L, 1);
    return 1;
}

int IsBrowserLoading(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_browserMu);
    const BrowserState* b = FindBrowserLocked(id);
    if (!b) return False(L);
    lua_pushboolean(L, b->loading ? 1 : 0);
    return 1;
}

int InjectBrowserMouseMove(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    double x = 0, y = 0;
    if (!id || !Pair(L, 2, x, y)) return False(L);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        if (!FindBrowserLocked(id)) return False(L);
    }
    char cmd[96];
    std::snprintf(cmd, sizeof(cmd), "mouse_move|%u|%d|%d", id, static_cast<int>(x), static_cast<int>(y));
    PostBrowserCmd(cmd);
    lua_pushboolean(L, 1);
    return 1;
}

int InjectBrowserMouseButton(lua_State* L, const char* verb) {
    const uint32_t id = ElementIdOf(L, 1);
    const char* button = lua_isstring(L, 2) ? lua_tostring(L, 2) : "left";
    if (!id) return False(L);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        if (!FindBrowserLocked(id)) return False(L);
    }
    PostBrowserCmd(std::string(verb) + "|" + std::to_string(id) + "|" + ToLowerStr(button));
    lua_pushboolean(L, 1);
    return 1;
}

int InjectBrowserMouseDown(lua_State* L) { return InjectBrowserMouseButton(L, "mouse_down"); }
int InjectBrowserMouseUp(lua_State* L) { return InjectBrowserMouseButton(L, "mouse_up"); }

int InjectBrowserMouseWheel(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    double dy = 0, dx = 0;
    if (!id || !Pair(L, 2, dy, dx)) return False(L);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        if (!FindBrowserLocked(id)) return False(L);
    }
    char cmd[96];
    std::snprintf(cmd, sizeof(cmd), "wheel|%u|%d|%d", id, static_cast<int>(dy), static_cast<int>(dx));
    PostBrowserCmd(cmd);
    lua_pushboolean(L, 1);
    return 1;
}

int GetBrowserTitle(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_browserMu);
    const BrowserState* b = FindBrowserLocked(id);
    if (!b) return False(L);
    lua_pushstring(L, b->title.c_str());
    return 1;
}

int GetBrowserURL(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_browserMu);
    const BrowserState* b = FindBrowserLocked(id);
    if (!b) return False(L);
    lua_pushstring(L, b->url.c_str());
    return 1;
}

int SetBrowserRenderingPaused(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!id || lua_type(L, 2) != LUA_TBOOLEAN) return False(L);
    const bool paused = lua_toboolean(L, 2) != 0;
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        BrowserState* b = FindBrowserLocked(id);
        if (!b) return False(L);
        b->renderingPaused = paused;
    }
    PostBrowserCmd("pause|" + std::to_string(id) + (paused ? "|1" : "|0"));
    lua_pushboolean(L, 1);
    return 1;
}

int IsBrowserRenderingPaused(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_browserMu);
    const BrowserState* b = FindBrowserLocked(id);
    if (!b) return False(L);
    lua_pushboolean(L, b->renderingPaused ? 1 : 0);
    return 1;
}

int ExecuteBrowserJavascript(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!id || !lua_isstring(L, 2)) return False(L);
    const std::string code = lua_tostring(L, 2);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        if (!FindBrowserLocked(id)) return False(L);
    }
    PostBrowserCmd("js|" + std::to_string(id) + "|" + code);
    lua_pushboolean(L, 1);
    return 1;
}

int GetBrowserVolume(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_browserMu);
    const BrowserState* b = FindBrowserLocked(id);
    if (!b) return False(L);
    lua_pushnumber(L, b->volume);
    return 1;
}

// CLuaBrowserDefs.cpp:563-591. Supports both setBrowserVolume(volume) and setBrowserVolume(browser, volume).
int SetBrowserVolume(lua_State* L) {
    if (lua_isnumber(L, 1)) {
        const float vol = std::clamp(static_cast<float>(lua_tonumber(L, 1)), 0.0f, 1.0f);
        std::lock_guard<std::mutex> lock(g_browserMu);
        g_globalBrowserVolume = vol;
        for (auto& b : g_browsers) b.volume = vol;
        char cmd[64];
        std::snprintf(cmd, sizeof(cmd), "volume|0|%.3f", vol);
        PostBrowserCmd(cmd);
        lua_pushboolean(L, 1);
        return 1;
    }
    const uint32_t id = ElementIdOf(L, 1);
    if (!id || !lua_isnumber(L, 2)) return False(L);
    const float vol = std::clamp(static_cast<float>(lua_tonumber(L, 2)), 0.0f, 1.0f);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        BrowserState* b = FindBrowserLocked(id);
        if (!b) return False(L);
        b->volume = vol;
    }
    char cmd[64];
    std::snprintf(cmd, sizeof(cmd), "volume|%u|%.3f", id, vol);
    PostBrowserCmd(cmd);
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaBrowserDefs.cpp:594-626.
int IsBrowserDomainBlocked(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING && lua_type(L, 1) != LUA_TNUMBER) {
        LUA_LOGE("isBrowserDomainBlocked: expected a string");
        lua_pushnil(L);
        return 1;
    }
    const bool isUrl = lua_isboolean(L, 2) ? (lua_toboolean(L, 2) != 0) : false;
    const std::string dom = ExtractDomain(lua_tostring(L, 1), isUrl);
    std::lock_guard<std::mutex> lock(g_browserMu);
    lua_pushboolean(L, IsDomainWhitelistedLocked(dom) ? 0 : 1);
    return 1;
}

int FocusBrowser(lua_State* L) {
    if (lua_isnoneornil(L, 1)) {
        std::lock_guard<std::mutex> lock(g_browserMu);
        g_focusedBrowserId = 0;
        PostBrowserCmd("focus|0");
        lua_pushboolean(L, 1);
        return 1;
    }
    const uint32_t id = ElementIdOf(L, 1);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        if (!FindBrowserLocked(id)) return False(L);
        g_focusedBrowserId = id;
    }
    PostBrowserCmd("focus|" + std::to_string(id));
    lua_pushboolean(L, 1);
    return 1;
}

int IsBrowserFocused(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_browserMu);
    if (!FindBrowserLocked(id)) return False(L);
    lua_pushboolean(L, g_focusedBrowserId == id ? 1 : 0);
    return 1;
}

int SetBrowserProperty(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!id || !lua_isstring(L, 2) || !lua_isstring(L, 3)) return False(L);
    const std::string key = ToLowerStr(lua_tostring(L, 2));
    const std::string val = lua_tostring(L, 3);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        BrowserState* b = FindBrowserLocked(id);
        if (!b) return False(L);
        b->properties[key] = val;
    }
    PostBrowserCmd("prop|" + std::to_string(id) + "|" + key + "|" + val);
    lua_pushboolean(L, 1);
    return 1;
}

int GetBrowserProperty(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!id || !lua_isstring(L, 2)) return False(L);
    const std::string key = ToLowerStr(lua_tostring(L, 2));
    std::lock_guard<std::mutex> lock(g_browserMu);
    const BrowserState* b = FindBrowserLocked(id);
    if (!b) return False(L);
    auto it = b->properties.find(key);
    if (it == b->properties.end()) {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushstring(L, it->second.c_str());
    return 1;
}

// CLuaBrowserDefs.cpp:734-753.
int GetBrowserSettings(lua_State* L) {
    lua_createtable(L, 0, 3);
    lua_pushstring(L, "RemoteEnabled");
    lua_pushboolean(L, 1);
    lua_settable(L, -3);
    lua_pushstring(L, "RemoteJavascript");
    lua_pushboolean(L, 1);
    lua_settable(L, -3);
    lua_pushstring(L, "PluginsEnabled");
    lua_pushboolean(L, 0);
    lua_settable(L, -3);
    return 1;
}

int GetBrowserSource(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!id || !lua_isfunction(L, 2)) return False(L);
    lua_pushvalue(L, 2);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        BrowserState* b = FindBrowserLocked(id);
        if (!b) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
            return False(L);
        }
        b->sourceCallbacks.push_back(ref);
#ifndef __ANDROID__
        if (!b->lastHtmlSource.empty()) {
            BrowserEvent ev;
            ev.kind = BrowserEvent::Kind::SourceReady;
            ev.id = id;
            ev.textA = b->lastHtmlSource;
            g_browserEvents.push_back(std::move(ev));
        }
#endif
    }
    PostBrowserCmd("source|" + std::to_string(id));
    lua_pushboolean(L, 1);
    return 1;
}

int SetBrowserAjaxHandler(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!id || !lua_isstring(L, 2)) return False(L);
    const std::string route = lua_tostring(L, 2);
    std::lock_guard<std::mutex> lock(g_browserMu);
    BrowserState* b = FindBrowserLocked(id);
    if (!b || !b->isLocal) return False(L);
    auto it = b->ajaxHandlerRefs.find(route);
    if (it != b->ajaxHandlerRefs.end()) {
        if (it->second != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, it->second);
        b->ajaxHandlerRefs.erase(it);
    }
    if (lua_isfunction(L, 3)) {
        lua_pushvalue(L, 3);
        b->ajaxHandlerRefs[route] = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_pushboolean(L, 1);
    return 1;
}

int CanBrowserNavigateBack(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_browserMu);
    const BrowserState* b = FindBrowserLocked(id);
    if (!b) return False(L);
    lua_pushboolean(L, b->canBack ? 1 : 0);
    return 1;
}

int CanBrowserNavigateForward(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_browserMu);
    const BrowserState* b = FindBrowserLocked(id);
    if (!b) return False(L);
    lua_pushboolean(L, b->canForward ? 1 : 0);
    return 1;
}

int NavigateBrowserBack(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        BrowserState* b = FindBrowserLocked(id);
        if (!b || !b->canBack) return False(L);
        if (b->historyIndex > 0) {
            --b->historyIndex;
            b->url = b->history[static_cast<size_t>(b->historyIndex)];
            b->canBack = b->historyIndex > 0;
            b->canForward = b->historyIndex + 1 < static_cast<int>(b->history.size());
        }
    }
    PostBrowserCmd("back|" + std::to_string(id));
    lua_pushboolean(L, 1);
    return 1;
}

int NavigateBrowserForward(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        BrowserState* b = FindBrowserLocked(id);
        if (!b || !b->canForward) return False(L);
        if (b->historyIndex + 1 < static_cast<int>(b->history.size())) {
            ++b->historyIndex;
            b->url = b->history[static_cast<size_t>(b->historyIndex)];
            b->canBack = b->historyIndex > 0;
            b->canForward = b->historyIndex + 1 < static_cast<int>(b->history.size());
        }
    }
    PostBrowserCmd("forward|" + std::to_string(id));
    lua_pushboolean(L, 1);
    return 1;
}

int ReloadBrowserPage(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const bool ignoreCache = lua_isboolean(L, 2) ? (lua_toboolean(L, 2) != 0) : false;
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        if (!FindBrowserLocked(id)) return False(L);
    }
    PostBrowserCmd("reload|" + std::to_string(id) + (ignoreCache ? "|1" : "|0"));
    lua_pushboolean(L, 1);
    return 1;
}

int ToggleBrowserDevTools(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!id || lua_type(L, 2) != LUA_TBOOLEAN) return False(L);
    const bool show = lua_toboolean(L, 2) != 0;
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        BrowserState* b = FindBrowserLocked(id);
        if (!b) return False(L);
        b->devTools = show;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int ResizeBrowser(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    double w = 0, h = 0;
    if (!id || !Pair(L, 2, w, h)) return False(L);
    if (const char* reason = SizeReason(w, h))
        return luaL_error(L, "Invalid parameter @ 'resizeBrowser' [%s]", reason);
    const unsigned uw = std::clamp(static_cast<unsigned>(w), 1u, kMaxBrowserSide);
    const unsigned uh = std::clamp(static_cast<unsigned>(h), 1u, kMaxBrowserSide);
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        BrowserState* b = FindBrowserLocked(id);
        if (!b) return False(L);
        b->width = uw;
        b->height = uh;
    }
    char cmd[96];
    std::snprintf(cmd, sizeof(cmd), "resize|%u|%u|%u", id, uw, uh);
    PostBrowserCmd(cmd);
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaBrowserDefs.cpp:1135-1138.
std::atomic<bool> g_browserGpuEnabled{true};

int IsBrowserGPUEnabled(lua_State* L) {
    lua_pushboolean(L, g_browserGpuEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    return 1;
}

}  // namespace

bool BrowserInfo(uint32_t id, std::string& bitmap, unsigned& width, unsigned& height) {
    std::lock_guard<std::mutex> lock(g_browserMu);
    const BrowserState* b = FindBrowserLocked(id);
    if (!b) return false;
    bitmap = "tx:" + std::to_string(b->id);
    width = b->width;
    height = b->height;
    return true;
}

bool BrowserAlive(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_browserMu);
    return FindBrowserLocked(id) != nullptr;
}

bool BrowserDestroy(uint32_t id) {
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        for (auto it = g_browsers.begin(); it != g_browsers.end(); ++it) {
            if (it->id != id) continue;
            ReleaseRefsLocked(ActiveVmState(), *it);
            if (g_focusedBrowserId == id) g_focusedBrowserId = 0;
            g_browsers.erase(it);
            PostBrowserCmd("destroy|" + std::to_string(id));
            return true;
        }
    }
    return false;
}

void BrowserDropResource(lua_State* L, const std::string& owner, uint16_t netId, uint32_t generation) {
    std::vector<uint32_t> doomed;
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        for (auto it = g_browsers.begin(); it != g_browsers.end(); ) {
            if (it->owner == owner && (!netId || it->ownerNetId == netId) &&
                (!generation || it->ownerGeneration == generation)) {
                doomed.push_back(it->id);
                ReleaseRefsLocked(L, *it);
                if (g_focusedBrowserId == it->id) g_focusedBrowserId = 0;
                it = g_browsers.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (uint32_t id : doomed) PostBrowserCmd("destroy|" + std::to_string(id));
}

void BrowserReset(lua_State* L) {
    std::vector<uint32_t> doomed;
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        for (auto& b : g_browsers) {
            doomed.push_back(b.id);
            ReleaseRefsLocked(L, b);
        }
        g_browsers.clear();
        for (auto& ev : g_browserEvents) {
            if (L && ev.callbackRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ev.callbackRef);
        }
        g_browserEvents.clear();
        g_focusedBrowserId = 0;
        g_globalBrowserVolume = 1.0f;
        g_whitelistedDomains.clear();
    }
    for (uint32_t id : doomed) PostBrowserCmd("destroy|" + std::to_string(id));
}

void BrowserInjectGuiClick(uint32_t browserId, int x, int y) {
    if (!BrowserAlive(browserId)) return;
    char cmd[96];
    std::snprintf(cmd, sizeof(cmd), "mouse_move|%u|%d|%d", browserId, x, y);
    PostBrowserCmd(cmd);
    PostBrowserCmd("mouse_down|" + std::to_string(browserId) + "|left");
    PostBrowserCmd("mouse_up|" + std::to_string(browserId) + "|left");
}

void BrowserNoteCreated(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_browserMu);
    if (!FindBrowserLocked(id)) return;
    BrowserEvent ev;
    ev.kind = BrowserEvent::Kind::Created;
    ev.id = id;
    g_browserEvents.push_back(std::move(ev));
}

void BrowserNoteLoadingStart(uint32_t id, const char* url, bool isMainFrame) {
    std::lock_guard<std::mutex> lock(g_browserMu);
    BrowserState* b = FindBrowserLocked(id);
    if (!b) return;
    b->loading = true;
    if (url && url[0]) b->url = url;
    BrowserEvent ev;
    ev.kind = BrowserEvent::Kind::LoadingStart;
    ev.id = id;
    ev.textA = url ? url : "";
    ev.flagA = isMainFrame;
    g_browserEvents.push_back(std::move(ev));
}

void BrowserNoteDocumentReady(uint32_t id, const char* url, const char* title, bool canBack, bool canForward) {
    std::lock_guard<std::mutex> lock(g_browserMu);
    BrowserState* b = FindBrowserLocked(id);
    if (!b) return;
    b->loading = false;
    if (url && url[0]) b->url = url;
    if (title) b->title = title;
    b->canBack = canBack;
    b->canForward = canForward;
    BrowserEvent ev;
    ev.kind = BrowserEvent::Kind::DocumentReady;
    ev.id = id;
    ev.textA = b->url;
    ev.textB = b->title;
    ev.flagA = canBack;
    ev.flagB = canForward;
    g_browserEvents.push_back(std::move(ev));
}

void BrowserNoteLoadingFailed(uint32_t id, const char* url, int errorCode, const char* errorDescription) {
    std::lock_guard<std::mutex> lock(g_browserMu);
    BrowserState* b = FindBrowserLocked(id);
    if (!b) return;
    b->loading = false;
    BrowserEvent ev;
    ev.kind = BrowserEvent::Kind::LoadingFailed;
    ev.id = id;
    ev.code = errorCode;
    ev.textA = url ? url : "";
    ev.textB = errorDescription ? errorDescription : "";
    g_browserEvents.push_back(std::move(ev));
}

void BrowserNoteNavigate(uint32_t id, const char* targetUrl, bool isBlocked, bool isMainFrame) {
    std::lock_guard<std::mutex> lock(g_browserMu);
    if (!FindBrowserLocked(id)) return;
    BrowserEvent ev;
    ev.kind = BrowserEvent::Kind::Navigate;
    ev.id = id;
    ev.textA = targetUrl ? targetUrl : "";
    ev.flagA = isBlocked;
    ev.flagB = isMainFrame;
    g_browserEvents.push_back(std::move(ev));
}

void BrowserNotePopup(uint32_t id, const char* targetUrl, const char* openerUrl) {
    std::lock_guard<std::mutex> lock(g_browserMu);
    if (!FindBrowserLocked(id)) return;
    BrowserEvent ev;
    ev.kind = BrowserEvent::Kind::Popup;
    ev.id = id;
    ev.textA = targetUrl ? targetUrl : "";
    ev.textB = openerUrl ? openerUrl : "";
    g_browserEvents.push_back(std::move(ev));
}

void BrowserNoteSource(uint32_t id, const char* html) {
    std::lock_guard<std::mutex> lock(g_browserMu);
    BrowserState* b = FindBrowserLocked(id);
    if (!b) return;
    b->lastHtmlSource = html ? html : "";
    BrowserEvent ev;
    ev.kind = BrowserEvent::Kind::SourceReady;
    ev.id = id;
    ev.textA = b->lastHtmlSource;
    g_browserEvents.push_back(std::move(ev));
}

void BrowserNoteInputFocus(uint32_t id, bool gainedFocus) {
    std::lock_guard<std::mutex> lock(g_browserMu);
    if (!FindBrowserLocked(id)) return;
    BrowserEvent ev;
    ev.kind = BrowserEvent::Kind::InputFocus;
    ev.id = id;
    ev.flagA = gainedFocus;
    g_browserEvents.push_back(std::move(ev));
}

void BrowserPumpEvents(lua_State* L) {
    if (!L) return;
    std::vector<BrowserEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_browserMu);
        if (g_browserEvents.empty()) return;
        events.swap(g_browserEvents);
    }
    for (auto& ev : events) {
        if (ev.kind == BrowserEvent::Kind::SourceReady) {
            std::vector<int> refs;
            {
                std::lock_guard<std::mutex> lock(g_browserMu);
                if (BrowserState* b = FindBrowserLocked(ev.id)) {
                    refs.swap(b->sourceCallbacks);
                }
            }
            for (int ref : refs) {
                if (ref == LUA_NOREF) continue;
                lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
                if (lua_isfunction(L, -1)) {
                    lua_pushlstring(L, ev.textA.data(), ev.textA.size());
                    PushElement(L, ev.id);
                    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                        LUA_LOGE("getBrowserSource callback error: %s", lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                } else {
                    lua_pop(L, 1);
                }
            }
            continue;
        }
        if (ev.kind == BrowserEvent::Kind::WhitelistChange) {
            if (ev.callbackRef != LUA_NOREF) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, ev.callbackRef);
                luaL_unref(L, LUA_REGISTRYINDEX, ev.callbackRef);
                if (lua_isfunction(L, -1)) {
                    lua_pushboolean(L, ev.flagA ? 1 : 0);
                    lua_createtable(L, static_cast<int>(ev.domains.size()), 0);
                    for (size_t i = 0; i < ev.domains.size(); ++i) {
                        lua_pushstring(L, ev.domains[i].c_str());
                        lua_rawseti(L, -2, static_cast<int>(i) + 1);
                    }
                    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                        LUA_LOGE("requestBrowserDomains callback error: %s", lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                } else {
                    lua_pop(L, 1);
                }
            }
            if (!ev.domains.empty()) {
                lua_createtable(L, static_cast<int>(ev.domains.size()), 0);
                for (size_t i = 0; i < ev.domains.size(); ++i) {
                    lua_pushstring(L, ev.domains[i].c_str());
                    lua_rawseti(L, -2, static_cast<int>(i) + 1);
                }
                ActiveVmFireElementEvent(L, "onClientBrowserWhitelistChange", ClientRootElement(), 1);
            }
            continue;
        }
        if (!BrowserAlive(ev.id)) continue;
        switch (ev.kind) {
            case BrowserEvent::Kind::Created:
                ActiveVmFireElementEvent(L, "onClientBrowserCreated", ev.id, 0);
                break;
            case BrowserEvent::Kind::LoadingStart:
                lua_pushstring(L, ev.textA.c_str());
                lua_pushboolean(L, ev.flagA ? 1 : 0);
                ActiveVmFireElementEvent(L, "onClientBrowserLoadingStart", ev.id, 2);
                break;
            case BrowserEvent::Kind::DocumentReady:
                {
                    std::lock_guard<std::mutex> lock(g_browserMu);
                    if (BrowserState* b = FindBrowserLocked(ev.id)) b->loading = false;
                }
                lua_pushstring(L, ev.textA.c_str());
                ActiveVmFireElementEvent(L, "onClientBrowserDocumentReady", ev.id, 1);
                break;
            case BrowserEvent::Kind::LoadingFailed:
                lua_pushstring(L, ev.textA.c_str());
                lua_pushinteger(L, ev.code);
                lua_pushstring(L, ev.textB.c_str());
                ActiveVmFireElementEvent(L, "onClientBrowserLoadingFailed", ev.id, 3);
                break;
            case BrowserEvent::Kind::Navigate:
                lua_pushstring(L, ev.textA.c_str());
                lua_pushboolean(L, ev.flagA ? 1 : 0);
                lua_pushboolean(L, ev.flagB ? 1 : 0);
                ActiveVmFireElementEvent(L, "onClientBrowserNavigate", ev.id, 3);
                break;
            case BrowserEvent::Kind::Popup:
                lua_pushstring(L, ev.textA.c_str());
                lua_pushstring(L, ev.textB.c_str());
                ActiveVmFireElementEvent(L, "onClientBrowserPopup", ev.id, 2);
                break;
            case BrowserEvent::Kind::InputFocus:
                lua_pushboolean(L, ev.flagA ? 1 : 0);
                ActiveVmFireElementEvent(L, "onClientBrowserInputFocusChanged", ev.id, 1);
                break;
            default:
                break;
        }
    }
}

void InstallBrowserLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createBrowser", CreateBrowser},
        {"requestBrowserDomains", RequestBrowserDomains},
        {"openExternalURL", OpenExternalURL},
        {"loadBrowserURL", LoadBrowserURL},
        {"isBrowserLoading", IsBrowserLoading},
        {"injectBrowserMouseMove", InjectBrowserMouseMove},
        {"injectBrowserMouseDown", InjectBrowserMouseDown},
        {"injectBrowserMouseUp", InjectBrowserMouseUp},
        {"injectBrowserMouseWheel", InjectBrowserMouseWheel},
        {"getBrowserTitle", GetBrowserTitle},
        {"getBrowserURL", GetBrowserURL},
        {"setBrowserRenderingPaused", SetBrowserRenderingPaused},
        {"isBrowserRenderingPaused", IsBrowserRenderingPaused},
        {"executeBrowserJavascript", ExecuteBrowserJavascript},
        {"getBrowserVolume", GetBrowserVolume},
        {"setBrowserVolume", SetBrowserVolume},
        {"isBrowserDomainBlocked", IsBrowserDomainBlocked},
        {"focusBrowser", FocusBrowser},
        {"isBrowserFocused", IsBrowserFocused},
        {"setBrowserProperty", SetBrowserProperty},
        {"getBrowserProperty", GetBrowserProperty},
        {"getBrowserSettings", GetBrowserSettings},
        {"getBrowserSource", GetBrowserSource},
        {"setBrowserAjaxHandler", SetBrowserAjaxHandler},
        {"canBrowserNavigateBack", CanBrowserNavigateBack},
        {"canBrowserNavigateForward", CanBrowserNavigateForward},
        {"navigateBrowserBack", NavigateBrowserBack},
        {"navigateBrowserForward", NavigateBrowserForward},
        {"reloadBrowserPage", ReloadBrowserPage},
        {"toggleBrowserDevTools", ToggleBrowserDevTools},
        {"resizeBrowser", ResizeBrowser},
        {"guiCreateBrowser", GuiCreateBrowser},
        {"guiGetBrowser", GuiGetBrowser},
        {"isBrowserGPUEnabled", IsBrowserGPUEnabled},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
