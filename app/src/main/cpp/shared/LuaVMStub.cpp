#include "LuaVMStub.h"
#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"
#include "../net/NeonJoinProtocol.h"
#include "../net/raknet/RakNetWrapper.h"

#include <zlib.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace WorldRPG::Script {

struct DxRuntime {
    lua_State* L = nullptr;
    int screenW = 1920;
    int screenH = 1080;
    bool chatHooked = false;
    std::unordered_map<std::string, std::vector<int>> handlers;
    struct Timer { int ref = 0; int times = 1; int interval = 50; std::uint64_t next = 0; };
    std::vector<Timer> timers;
    struct PendingChat { std::string text; int r = 255, g = 255, b = 255; };
    std::vector<PendingChat> pendingChat;
    std::string persistent;
    std::string frame;
};
DxRuntime g_dx;
static LuaVMStub* g_active = nullptr;

static std::uint64_t DxNow() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

static void fireHandlers(const char* name, int nargs) {
    if (!g_dx.L) return;
    auto found = g_dx.handlers.find(name);
    if (found == g_dx.handlers.end() || found->second.empty()) {
        lua_pop(g_dx.L, nargs);
        return;
    }
    // Args are already on the stack. Copy them for every handler.
    for (int ref : found->second) {
        lua_rawgeti(g_dx.L, LUA_REGISTRYINDEX, ref);
        for (int i = 0; i < nargs; ++i) lua_pushvalue(g_dx.L, -1 - nargs);
        if (lua_pcall(g_dx.L, nargs, 0, 0) != LUA_OK) {
            LUA_LOGE("Lua %s: %s", name, lua_tostring(g_dx.L, -1));
            lua_pop(g_dx.L, 1);
        }
    }
    lua_pop(g_dx.L, nargs);
}

static int l_addEventHandler(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    if (!lua_isfunction(L, 3)) return 0;
    lua_pushvalue(L, 3);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    g_dx.handlers[name].push_back(ref);
    if (std::strcmp(name, "onClientChatMessage") == 0) g_dx.chatHooked = true;
    return 0;
}

static int l_dxDrawRectangle(lua_State* L) {
    char line[96];
    std::snprintf(line, sizeof(line), "R %.1f %.1f %.1f %.1f %u\n",
        luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3),
        luaL_checknumber(L, 4), static_cast<unsigned>(luaL_optinteger(L, 5, 0xFFFFFFFF)));
    g_dx.frame += line;
    return 0;
}

static int l_dxDrawText(lua_State* L) {
    const char* text = luaL_optstring(L, 1, "");
    std::string safe(text ? text : "");
    for (char& ch : safe) if (ch == '\n' || ch == '\r') ch = ' ';
    char head[128];
    std::snprintf(head, sizeof(head), "T %.1f %.1f %.1f %.1f %.2f %u %d ",
        luaL_optnumber(L, 2, 0), luaL_optnumber(L, 3, 0),
        luaL_optnumber(L, 4, 0), luaL_optnumber(L, 5, 0),
        luaL_optnumber(L, 7, 1),
        static_cast<unsigned>(luaL_optinteger(L, 6, 0xFFFFFFFF)),
        lua_toboolean(L, 14));
    g_dx.frame += head;
    g_dx.frame += safe;
    g_dx.frame += '\n';
    return 0;
}

static int l_tocolor(lua_State* L) {
    const int r = static_cast<int>(luaL_optinteger(L, 1, 255)) & 255;
    const int g = static_cast<int>(luaL_optinteger(L, 2, 255)) & 255;
    const int b = static_cast<int>(luaL_optinteger(L, 3, 255)) & 255;
    const int a = static_cast<int>(luaL_optinteger(L, 4, 255)) & 255;
    lua_pushinteger(L, (static_cast<unsigned>(a) << 24) | (static_cast<unsigned>(r) << 16) |
                        (static_cast<unsigned>(g) << 8) | static_cast<unsigned>(b));
    return 1;
}

static int l_guiGetScreenSize(lua_State* L) {
    lua_pushinteger(L, g_dx.screenW);
    lua_pushinteger(L, g_dx.screenH);
    return 2;
}

static int l_getTickCount(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(DxNow()));
    return 1;
}

static int l_setTimer(lua_State* L) {
    if (!lua_isfunction(L, 1)) return 0;
    const int interval = std::max(1, static_cast<int>(luaL_optinteger(L, 2, 50)));
    const int times = static_cast<int>(luaL_optinteger(L, 3, 1));
    lua_pushvalue(L, 1);
    DxRuntime::Timer timer;
    timer.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    timer.times = times == 0 ? -1 : times;
    timer.interval = interval;
    timer.next = DxNow() + static_cast<std::uint64_t>(interval);
    g_dx.timers.push_back(timer);
    lua_pushinteger(L, timer.ref);
    return 1;
}

static int l_getElementData(lua_State* L) {
    uint32_t id = WorldRPG::ServerWorld::LocalPlayer();
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "id");
        if (lua_isinteger(L, -1)) id = static_cast<uint32_t>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }
    const char* key = luaL_checkstring(L, 2);
    double number = 0;
    std::string text;
    bool isNumber = false;
    if (!WorldRPG::ServerWorld::GetData(id, key, number, text, isNumber)) {
        lua_pushnil(L);
        return 1;
    }
    if (isNumber) lua_pushnumber(L, number);
    else lua_pushlstring(L, text.data(), text.size());
    return 1;
}

static int l_nil(lua_State* L) { lua_pushnil(L); return 1; }
static int l_num0(lua_State* L) { lua_pushinteger(L, 0); return 1; }
static int l_false(lua_State* L) { lua_pushboolean(L, 0); return 1; }
static int l_true(lua_State* L) { lua_pushboolean(L, 1); return 1; }
static int l_debug(lua_State* L) {
    if (lua_isstring(L, 1)) LUA_LOGI("Lua: %s", lua_tostring(L, 1));
    return 0;
}

static int l_guiCreateWindow(lua_State* L) {
    double x = luaL_optnumber(L, 1, 0);
    double y = luaL_optnumber(L, 2, 0);
    double w = luaL_optnumber(L, 3, 200);
    double h = luaL_optnumber(L, 4, 120);
    const char* title = luaL_optstring(L, 5, "");
    if (lua_toboolean(L, 6)) {
        x *= g_dx.screenW; y *= g_dx.screenH; w *= g_dx.screenW; h *= g_dx.screenH;
    }
    char line[256];
    std::snprintf(line, sizeof(line), "R %.1f %.1f %.1f %.1f %u\n", x, y, w, h, 0xE0101820u);
    g_dx.persistent += line;
    std::snprintf(line, sizeof(line), "T %.1f %.1f %.1f %.1f %.2f %u %d %s\n",
        x + 8, y + 4, x + w - 8, y + 28, 1.0, 0xFFFFFFFFu, 0, title ? title : "");
    g_dx.persistent += line;
    lua_pushinteger(L, 1);
    return 1;
}

static uint32_t ElementIdOf(lua_State* L, int index) {
    if (!lua_istable(L, index)) return 0;
    lua_getfield(L, index, "id");
    const uint32_t id = lua_isinteger(L, -1) ? static_cast<uint32_t>(lua_tointeger(L, -1)) : 0;
    lua_pop(L, 1);
    return id;
}

static void PushElement(lua_State* L, uint32_t id) {
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    lua_setfield(L, -2, "id");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "element");
}

static bool IsElementTable(lua_State* L, int index) {
    if (!lua_istable(L, index)) return false;
    lua_getfield(L, index, "element");
    const bool element = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return element;
}

static void WriteBits(Net::BitStreamWriter& w, uint32_t value, int bits) {
    uint8_t bytes[4] = {};
    for (int i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    w.WriteBits(bytes, static_cast<size_t>(bits));
}

static void WriteCompressed(Net::BitStreamWriter& w, uint32_t value, int bytes) {
    uint8_t raw[4] = {};
    for (int i = 0; i < 4; ++i) raw[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    for (int i = bytes - 1; i > 0; --i) {
        if (raw[i] == 0) WriteBits(w, 1, 1);
        else {
            WriteBits(w, 0, 1);
            w.WriteBits(raw, static_cast<size_t>((i + 1) * 8));
            return;
        }
    }
    if ((raw[0] & 0xF0) == 0) {
        WriteBits(w, 1, 1);
        WriteBits(w, raw[0], 4);
    } else {
        WriteBits(w, 0, 1);
        w.Write(raw[0]);
    }
}

static void WriteLuaValue(lua_State* L, int index, Net::BitStreamWriter& w, int depth) {
    if (depth > 6) {
        WriteBits(w, 0, 4);
        return;
    }
    if (index < 0) index = lua_gettop(L) + index + 1;
    if (IsElementTable(L, index)) {
        WriteBits(w, 7, 4);
        WriteBits(w, ElementIdOf(L, index) & 0x1FFFFu, 17);
        return;
    }
    switch (lua_type(L, index)) {
        case LUA_TBOOLEAN:
            WriteBits(w, 1, 4);
            WriteBits(w, lua_toboolean(L, index) ? 1u : 0u, 1);
            return;
        case LUA_TNUMBER:
            WriteBits(w, 3, 4);
            if (lua_isinteger(L, index)) {
                WriteBits(w, 0, 1);
                WriteCompressed(w, static_cast<uint32_t>(static_cast<int32_t>(lua_tointeger(L, index))), 4);
            } else {
                WriteBits(w, 1, 1);
                WriteBits(w, 0, 1);
                w.Write(static_cast<float>(lua_tonumber(L, index)));
            }
            return;
        case LUA_TSTRING: {
            size_t length = 0;
            const char* text = lua_tolstring(L, index, &length);
            WriteBits(w, 4, 4);
            const auto clipped = static_cast<uint32_t>(std::min(length, size_t{65535}));
            WriteCompressed(w, clipped, 2);
            if (clipped && text) w.WriteBytes(reinterpret_cast<const uint8_t*>(text), clipped);
            return;
        }
        case LUA_TTABLE: {
            int count = 0;
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                lua_pop(L, 1);
                ++count;
            }
            WriteBits(w, 5, 4);
            WriteCompressed(w, static_cast<uint32_t>(count * 2), 4);
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                WriteLuaValue(L, -2, w, depth + 1);
                WriteLuaValue(L, -1, w, depth + 1);
                lua_pop(L, 1);
            }
            return;
        }
        default:
            WriteBits(w, 0, 4);
            return;
    }
}

static int l_triggerServerEvent(lua_State* L) {
    size_t nameLength = 0;
    const char* name = luaL_checklstring(L, 1, &nameLength);
    if (!name || nameLength == 0 || nameLength > 256) return 0;
    Net::BitStreamWriter packet;
    packet.Write(static_cast<uint8_t>(81));
    WriteCompressed(packet, static_cast<uint32_t>(nameLength), 2);
    packet.WriteBytes(reinterpret_cast<const uint8_t*>(name), nameLength);
    WriteBits(packet, ElementIdOf(L, 2) & 0x1FFFFu, 17);
    const int top = lua_gettop(L);
    const int args = std::max(0, top - 2);
    WriteCompressed(packet, static_cast<uint32_t>(args), 4);
    for (int i = 3; i <= top; ++i) WriteLuaValue(L, i, packet, 0);
    lua_pushboolean(L, Net::SendGamePacket(packet, true) ? 1 : 0);
    return 1;
}

static int l_setPedWander(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    int direction = static_cast<int>(luaL_optinteger(L, 3, 0));
    if (direction < 0) direction = 0;
    Game::RequestWander(id, direction);
    lua_pushboolean(L, id != 0 ? 1 : 0);
    return 1;
}

static int l_isElement(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    float x = 0, y = 0, z = 0;
    const bool known = id != 0 && (id == ServerWorld::LocalPlayer() || id == ServerWorld::LastResourceRoot() ||
                                    ServerWorld::ElementPose(id, x, y, z) || Game::CopyRemotePose(id, x, y, z));
    lua_pushboolean(L, known ? 1 : 0);
    return 1;
}

static int l_isElementSyncer(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    lua_pushboolean(L, id != 0 && (id == ServerWorld::LocalPlayer() || ServerWorld::ActorHosted(id)) ? 1 : 0);
    return 1;
}

static int l_getElementType(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    lua_pushstring(L, id == 0 ? "userdata" : ServerWorld::ElementKindName(id));
    return 1;
}

static int l_getElementPosition(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    float x = 0, y = 0, z = 0, heading = 0;
    if (id == 0 || id == ServerWorld::LocalPlayer()) Game::CopyPose(x, y, z, heading);
    else if (!Game::CopyRemotePose(id, x, y, z) && !ServerWorld::ElementPose(id, x, y, z)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    return 3;
}

static int l_getElementHealth(lua_State* L) {
    float x = 0, y = 0, z = 0, heading = 0, vx = 0, vy = 0, vz = 0, health = 100.f;
    Game::CopyBody(x, y, z, heading, vx, vy, vz, health);
    (void)ElementIdOf(L, 1);
    lua_pushnumber(L, health);
    return 1;
}

static int l_getPlayerMoney(lua_State* L) {
    double number = 0;
    std::string text;
    bool isNumber = false;
    if (ServerWorld::GetData(ServerWorld::LocalPlayer(), "money", number, text, isNumber) && isNumber) {
        lua_pushnumber(L, number);
        return 1;
    }
    lua_pushinteger(L, 0);
    return 1;
}

static int l_getElementsByType(lua_State* L) {
    const char* kind = luaL_optstring(L, 1, "");
    lua_newtable(L);
    int index = 1;
    if (std::strcmp(kind, "player") == 0) {
        PushElement(L, ServerWorld::LocalPlayer());
        lua_rawseti(L, -2, index++);
    }
    ServerWorld::ActorSpawn actors[96];
    const int count = ServerWorld::CopyActors(actors, 96);
    for (int i = 0; i < count; ++i) {
        const bool ped = actors[i].kind == 14 && std::strcmp(kind, "ped") == 0;
        const bool vehicle = actors[i].kind == 2 && std::strcmp(kind, "vehicle") == 0;
        const bool player = actors[i].kind == 1 && std::strcmp(kind, "player") == 0;
        if (!ped && !vehicle && !player) continue;
        PushElement(L, actors[i].id);
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

static int l_addEvent(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

static int l_getLocalPlayer(lua_State* L) {
    lua_getglobal(L, "localPlayer");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        PushElement(L, ServerWorld::LocalPlayer());
    }
    return 1;
}

static void installDxBindings(lua_State* L) {
    g_dx.L = L;
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"addEventHandler", l_addEventHandler},
        {"dxDrawRectangle", l_dxDrawRectangle},
        {"dxDrawText", l_dxDrawText},
        {"tocolor", l_tocolor},
        {"guiGetScreenSize", l_guiGetScreenSize},
        {"getTickCount", l_getTickCount},
        {"setTimer", l_setTimer},
        {"getElementData", l_getElementData},
        {"getElementHealth", l_getElementHealth},
        {"getPedArmor", l_num0},
        {"getPlayerMoney", l_getPlayerMoney},
        {"triggerServerEvent", l_triggerServerEvent},
        {"addEvent", l_addEvent},
        {"setPedWander", l_setPedWander},
        {"isElement", l_isElement},
        {"isElementStreamedIn", l_isElement},
        {"isElementSyncer", l_isElementSyncer},
        {"getElementType", l_getElementType},
        {"getElementPosition", l_getElementPosition},
        {"getElementDimension", l_num0},
        {"getElementInterior", l_num0},
        {"getElementsByType", l_getElementsByType},
        {"getLocalPlayer", l_getLocalPlayer},
        {"isPedDead", l_false},
        {"setElementAlpha", l_true},
        {"isChatBoxInputActive", l_false},
        {"setPlayerHudComponentVisible", l_true},
        {"outputDebugString", l_debug},
        {"addCommandHandler", l_true},
        {"guiCreateWindow", l_guiCreateWindow},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_newtable(L); lua_setglobal(L, "root");
    lua_newtable(L); lua_setglobal(L, "resourceRoot");
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(WorldRPG::ServerWorld::LocalPlayer()));
    lua_setfield(L, -2, "id");
    lua_setglobal(L, "localPlayer");
}

static void refreshLocalPlayer(lua_State* L) {
    lua_getglobal(L, "localPlayer");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    lua_pushinteger(L, static_cast<lua_Integer>(WorldRPG::ServerWorld::LocalPlayer()));
    lua_setfield(L, -2, "id");
    lua_setglobal(L, "localPlayer");
}

static bool looksLikeLua(const uint8_t* data, size_t length) {
    const size_t n = std::min(length, size_t{240});
    std::string head(reinterpret_cast<const char*>(data), n);
    return head.find("function") != std::string::npos || head.find("local ") != std::string::npos ||
           head.find("addEvent") != std::string::npos;
}

// C-compatible wrapper for std::function<int()> stored as light userdata
static int lua_stdfunc_wrapper(lua_State* L) {
    auto* fn = static_cast<std::function<int()>*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!fn || !*fn) return 0;
    return (*fn)();
}

// Custom print() that redirects to output callback via registry key
static int lua_custom_print(lua_State* L) {
    int n = lua_gettop(L);
    std::string msg;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) msg += "\t";
        msg.append(s, len);
        lua_pop(L, 1); // pop the string from luaL_tolstring
    }
    msg += "\n";

    // Retrieve output callback from registry
    lua_pushstring(L, "__output_callback");
    lua_gettable(L, LUA_REGISTRYINDEX);
    if (lua_islightuserdata(L, -1)) {
        auto* cb = static_cast<LuaOutputCallback*>(lua_touserdata(L, -1));
        if (cb && *cb) {
            (*cb)(msg.c_str());
        }
    }
    lua_pop(L, 1);
    return 0;
}

struct LuaVMStub::Impl {
    lua_State* L = nullptr;
    bool initialized = false;
    LuaOutputCallback outputCallback;
    std::unordered_map<std::string, std::function<int()>> registeredFunctions;
    std::vector<std::string> loadedScripts;

    void log(const char* level, const char* msg) {
        if (outputCallback) {
            outputCallback(msg);
        } else {
            LUA_LOGI("[%s] %s", level, msg);
        }
    }

    void registerStdLibs() {
        if (!L) return;
        luaL_openlibs(L);

        // Override print() with custom version that uses output callback
        lua_pushcfunction(L, lua_custom_print);
        lua_setglobal(L, "print");
    }

    void registerAllFunctions() {
        if (!L) return;
        for (const auto& [name, handler] : registeredFunctions) {
            // Store a copy of the function as upvalue via light userdata
            // We allocate on heap and let Lua manage lifetime via __gc
            auto* fnCopy = new std::function<int()>(handler);
            lua_pushlightuserdata(L, fnCopy);
            lua_pushcclosure(L, lua_stdfunc_wrapper, 1);
            lua_setglobal(L, name.c_str());
        }
        installDxBindings(L);
    }
};

LuaVMStub::LuaVMStub() : pImpl_(std::make_unique<Impl>()) {}

LuaVMStub::~LuaVMStub() {
    shutdown();
}

bool LuaVMStub::initialize() {
    if (pImpl_->initialized) return true;

    pImpl_->L = luaL_newstate();
    if (!pImpl_->L) {
        LUA_LOGE("LuaVM: failed to create lua_State");
        return false;
    }

    pImpl_->registerStdLibs();

    // Register default MTA client-side function stubs
    const char* mtaClientFunctions[] = {
        "outputChatBox", "triggerServerEvent", "addEventHandler",
        "removeEventHandler", "getLocalPlayer", "getElementPosition",
        "setElementPosition", "getElementHealth", "setElementHealth",
        "createVehicle", "destroyElement", "getPedOccupiedVehicle",
        "setPedAnimation", "guiCreateWindow", "guiCreateButton",
        "guiSetText", "guiGetText", "dxDrawText", "dxDrawRectangle",
        "dxDrawImage", "getTickCount", "setTimer", "killTimer",
        "getResourceName", "fileOpen", "fileRead", "fileClose",
        "toJSON", "fromJSON", "iprint", "debugMessage"
    };

    for (const char* fn : mtaClientFunctions) {
        std::string name(fn);
        pImpl_->registeredFunctions[name] = [name]() -> int {
            LUA_LOGI("LuaVM: call to unimplemented MTA function '%s'", name.c_str());
            return 0;
        };
    }

    pImpl_->registerAllFunctions();

    g_active = this;
    g_dx.L = pImpl_->L;
    pImpl_->initialized = true;
    pImpl_->log("INIT", "LuaVM initialized with real Lua 5.3.6 interpreter");
    LUA_LOGI("LuaVM: registered %zu MTA client function stubs",
             pImpl_->registeredFunctions.size());
    return true;
}

bool LuaVMStub::isReady() const {
    return pImpl_->initialized && pImpl_->L != nullptr;
}

LuaExecResult LuaVMStub::executeBuffer(const char* code, size_t length, const char* chunkName) {
    LuaExecResult result;
    if (!pImpl_->initialized || !pImpl_->L) {
        result.errorMessage = "LuaVM not initialized";
        return result;
    }
    if (!code || length == 0) {
        result.errorMessage = "Empty script buffer";
        return result;
    }

    // Set output callback in registry before execution
    if (pImpl_->outputCallback) {
        lua_pushstring(pImpl_->L, "__output_callback");
        lua_pushlightuserdata(pImpl_->L, &pImpl_->outputCallback);
        lua_settable(pImpl_->L, LUA_REGISTRYINDEX);
    }

    const char* name = chunkName ? chunkName : "<buffer>";
    int status = luaL_loadbuffer(pImpl_->L, code, length, name);
    if (status != LUA_OK) {
        result.errorMessage = lua_tostring(pImpl_->L, -1);
        lua_pop(pImpl_->L, 1);
        LUA_LOGE("LuaVM: load error in '%s': %s", name, result.errorMessage.c_str());
        return result;
    }

    status = lua_pcall(pImpl_->L, 0, 0, 0);
    if (status != LUA_OK) {
        result.errorMessage = lua_tostring(pImpl_->L, -1);
        lua_pop(pImpl_->L, 1);
        LUA_LOGE("LuaVM: exec error in '%s': %s", name, result.errorMessage.c_str());
        return result;
    }

    pImpl_->loadedScripts.push_back(name);
    result.success = true;
    LUA_LOGI("LuaVM: executed buffer '%s' (%zu bytes)", name, length);
    return result;
}

LuaExecResult LuaVMStub::executeFile(const char* filePath) {
    LuaExecResult result;
    if (!pImpl_->initialized || !pImpl_->L) {
        result.errorMessage = "LuaVM not initialized";
        return result;
    }
    if (!filePath) {
        result.errorMessage = "Null file path";
        return result;
    }

    // Set output callback in registry before execution
    if (pImpl_->outputCallback) {
        lua_pushstring(pImpl_->L, "__output_callback");
        lua_pushlightuserdata(pImpl_->L, &pImpl_->outputCallback);
        lua_settable(pImpl_->L, LUA_REGISTRYINDEX);
    }

    int status = luaL_loadfile(pImpl_->L, filePath);
    if (status != LUA_OK) {
        result.errorMessage = lua_tostring(pImpl_->L, -1);
        lua_pop(pImpl_->L, 1);
        LUA_LOGE("LuaVM: load error in '%s': %s", filePath, result.errorMessage.c_str());
        return result;
    }

    status = lua_pcall(pImpl_->L, 0, 0, 0);
    if (status != LUA_OK) {
        result.errorMessage = lua_tostring(pImpl_->L, -1);
        lua_pop(pImpl_->L, 1);
        LUA_LOGE("LuaVM: exec error in '%s': %s", filePath, result.errorMessage.c_str());
        return result;
    }

    pImpl_->loadedScripts.push_back(filePath);
    result.success = true;
    LUA_LOGI("LuaVM: executed file '%s'", filePath);
    return result;
}

void LuaVMStub::registerFunction(const char* name, std::function<int()> handler) {
    if (!name || !handler) return;
    pImpl_->registeredFunctions[std::string(name)] = handler;

    // If VM is already initialized, register immediately
    if (pImpl_->L) {
        auto* fnCopy = new std::function<int()>(handler);
        lua_pushlightuserdata(pImpl_->L, fnCopy);
        lua_pushcclosure(pImpl_->L, lua_stdfunc_wrapper, 1);
        lua_setglobal(pImpl_->L, name);
    }
    LUA_LOGI("LuaVM: registered function '%s'", name);
}

void LuaVMStub::setOutputCallback(LuaOutputCallback callback) {
    pImpl_->outputCallback = std::move(callback);
}

std::vector<std::string> LuaVMStub::getRegisteredFunctions() const {
    std::vector<std::string> names;
    names.reserve(pImpl_->registeredFunctions.size());
    for (const auto& [name, _] : pImpl_->registeredFunctions) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void LuaVMStub::reset() {
    if (!pImpl_->L) return;

    // Clear all globals except standard libraries
    lua_pushglobaltable(pImpl_->L);
    lua_pushnil(pImpl_->L);
    while (lua_next(pImpl_->L, -2) != 0) {
        // Keep standard lib tables, remove everything else
        if (!lua_istable(pImpl_->L, -1)) {
            lua_pop(pImpl_->L, 1); // pop value
            lua_pushvalue(pImpl_->L, -1); // copy key
            lua_pushnil(pImpl_->L);
            lua_settable(pImpl_->L, -4); // set global[key] = nil
        } else {
            lua_pop(pImpl_->L, 1); // pop value, keep key for next iteration
        }
    }
    lua_pop(pImpl_->L, 1); // pop global table

    // Re-register MTA functions
    pImpl_->registerAllFunctions();

    pImpl_->loadedScripts.clear();
    LUA_LOGI("LuaVM: reset complete");
}

void LuaVMStub::setScreenSize(int width, int height) {
    if (width > 0) g_dx.screenW = width;
    if (height > 0) g_dx.screenH = height;
}

bool LuaVMStub::feedClientScripts(const uint8_t* body, size_t length, size_t bits) {
    if (!pImpl_->L || !body || length < 8) return false;
    auto runBlob = [&](const uint8_t* data, uint32_t len, int index) {
        const uint8_t* source = data;
        uint32_t sourceLength = len;
        std::vector<uint8_t> inflated;
        if (len > 8 && data[4] == 0x78) {
            const uint32_t original = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
                                      (static_cast<uint32_t>(data[2]) << 8) | data[3];
            if (original > 0 && original < 2 * 1024 * 1024) {
                inflated.resize(original);
                uLongf dest = original;
                if (uncompress(inflated.data(), &dest, data + 4, len - 4) == Z_OK) {
                    source = inflated.data();
                    sourceLength = static_cast<uint32_t>(dest);
                }
            }
        }
        if (!looksLikeLua(source, sourceLength)) return;
        refreshLocalPlayer(pImpl_->L);
        char name[32];
        std::snprintf(name, sizeof(name), "resource_%d", index);
        const uint32_t root = WorldRPG::ServerWorld::LastResourceRoot();
        lua_State* L = pImpl_->L;
        if (luaL_loadbuffer(L, reinterpret_cast<const char*>(source), sourceLength, name) != LUA_OK) {
            LUA_LOGE("Resource script %s failed: %s", name, lua_tostring(L, -1));
            lua_pop(L, 1);
            return;
        }
        lua_newtable(L);
        lua_newtable(L);
        lua_pushglobaltable(L);
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
        PushElement(L, root);
        lua_setfield(L, -2, "resourceRoot");
        const char* upvalue = lua_getupvalue(L, -2, 1);
        if (upvalue && std::strcmp(upvalue, "_ENV") == 0) {
            lua_pop(L, 1);
            lua_setupvalue(L, -2, 1);
        } else {
            if (upvalue) lua_pop(L, 1);
            lua_pop(L, 1);
        }
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            LUA_LOGE("Resource script %s failed: %s", name, lua_tostring(L, -1));
            lua_pop(L, 1);
            return;
        }
        PushElement(L, root);
        lua_setglobal(L, "source");
        fireHandlers("onClientResourceStart", 0);
    };
    Net::BitStreamReader reader(body, length, bits);
    uint16_t resource = 0, count = 0;
    if (!reader.Read(resource) || !reader.Read(count) || count == 0 || count > 64) return false;
    for (uint16_t i = 0; i < count; ++i) {
        uint32_t len = 0;
        if (!reader.Read(len) || len == 0 || len > reader.GetRemainingBits() / 8) return false;
        std::vector<uint8_t> blob(len);
        if (!reader.ReadBytes(blob.data(), len)) return false;
        runBlob(blob.data(), len, i);
    }
    if (g_dx.chatHooked) {
        for (const auto& chat : g_dx.pendingChat) {
            lua_State* L = pImpl_->L;
            auto found = g_dx.handlers.find("onClientChatMessage");
            if (found == g_dx.handlers.end()) break;
            for (int ref : found->second) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                lua_pushlstring(L, chat.text.data(), chat.text.size());
                lua_pushinteger(L, chat.r);
                lua_pushinteger(L, chat.g);
                lua_pushinteger(L, chat.b);
                lua_pushinteger(L, 0);
                if (lua_pcall(L, 5, 0, 0) != LUA_OK) {
                    LUA_LOGE("Lua chat: %s", lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            }
        }
        g_dx.pendingChat.clear();
    }
    LUA_LOGI("LuaVM: loaded %u client script(s) from resource %u", count, resource);
    return true;
}

bool LuaVMStub::onChatMessage(const char* text, int red, int green, int blue) {
    if (!text) return false;
    if (!g_dx.chatHooked || !pImpl_->L) {
        g_dx.pendingChat.push_back({text, red, green, blue});
        return false;
    }
    lua_State* L = pImpl_->L;
    auto found = g_dx.handlers.find("onClientChatMessage");
    if (found == g_dx.handlers.end()) return false;
    for (int ref : found->second) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_pushstring(L, text);
        lua_pushinteger(L, red);
        lua_pushinteger(L, green);
        lua_pushinteger(L, blue);
        lua_pushinteger(L, 0);
        if (lua_pcall(L, 5, 0, 0) != LUA_OK) {
            LUA_LOGE("Lua chat: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    return true;
}

std::string LuaVMStub::pumpDx() {
    if (!pImpl_->L) return {};
    const auto now = DxNow();
    for (auto& timer : g_dx.timers) {
        if (timer.times == 0 || now < timer.next) continue;
        lua_rawgeti(pImpl_->L, LUA_REGISTRYINDEX, timer.ref);
        if (lua_pcall(pImpl_->L, 0, 0, 0) != LUA_OK) {
            LUA_LOGE("Lua timer: %s", lua_tostring(pImpl_->L, -1));
            lua_pop(pImpl_->L, 1);
        }
        timer.next = now + static_cast<std::uint64_t>(timer.interval);
        if (timer.times > 0) --timer.times;
    }
    g_dx.timers.erase(std::remove_if(g_dx.timers.begin(), g_dx.timers.end(),
        [](const DxRuntime::Timer& timer) { return timer.times == 0; }), g_dx.timers.end());
    g_dx.frame.clear();
    fireHandlers("onClientRender", 0);
    std::string out = g_dx.persistent + g_dx.frame;
    if (out.size() > 24000) out.resize(24000);
    std::string clean;
    clean.reserve(out.size());
    for (unsigned char ch : out) {
        if (ch == '\n' || (ch >= 32 && ch < 127)) clean.push_back(static_cast<char>(ch));
    }
    return clean;
}

void LuaVMStub::shutdown() {
    if (!pImpl_->initialized) return;
    if (g_active == this) g_active = nullptr;
    g_dx = DxRuntime{};

    if (pImpl_->L) {
        lua_close(pImpl_->L);
        pImpl_->L = nullptr;
    }

    pImpl_->registeredFunctions.clear();
    pImpl_->loadedScripts.clear();
    pImpl_->outputCallback = nullptr;
    pImpl_->initialized = false;
    LUA_LOGI("LuaVM: shutdown complete");
}

static bool ReadFlagBit(Net::BitStreamReader& reader, bool& out) {
    uint8_t bit = 0;
    if (!reader.ReadBits(&bit, 1)) return false;
    out = (bit & 1u) != 0;
    return true;
}

static bool ReadCompressedBits(Net::BitStreamReader& reader, int bytes, uint32_t& out) {
    out = 0;
    for (int i = bytes - 1; i > 0; --i) {
        bool empty = false;
        if (!ReadFlagBit(reader, empty)) return false;
        if (!empty) {
            uint8_t buf[4] = {};
            if (!reader.ReadBits(buf, static_cast<size_t>((i + 1) * 8))) return false;
            for (int b = 0; b <= i; ++b) out |= static_cast<uint32_t>(buf[b]) << (8 * b);
            return true;
        }
    }
    bool highEmpty = false;
    if (!ReadFlagBit(reader, highEmpty)) return false;
    if (!highEmpty) {
        uint8_t low = 0;
        if (!reader.Read(low)) return false;
        out = low;
        return true;
    }
    uint8_t nibble = 0;
    if (!reader.ReadBits(&nibble, 4)) return false;
    out = nibble & 0x0Fu;
    return true;
}

static bool ReadElementBits(Net::BitStreamReader& reader, uint32_t& out) {
    uint8_t bytes[4] = {};
    if (!reader.ReadBits(bytes, 17)) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) out |= static_cast<uint32_t>(bytes[i]) << (8 * i);
    if (out == 0x1FFFFu) out = 0;
    return true;
}

static bool ReadLuaArg(lua_State* L, Net::BitStreamReader& reader, int depth) {
    if (depth > 8) return false;
    uint8_t kind = 0;
    if (!reader.ReadBits(&kind, 4)) return false;
    kind &= 0x0Fu;
    if (kind == 0) {
        lua_pushnil(L);
        return true;
    }
    if (kind == 1) {
        bool value = false;
        if (!ReadFlagBit(reader, value)) return false;
        lua_pushboolean(L, value ? 1 : 0);
        return true;
    }
    if (kind == 2 || kind == 7) {
        uint32_t id = 0;
        if (!ReadElementBits(reader, id)) return false;
        PushElement(L, id);
        return true;
    }
    if (kind == 3) {
        bool wide = false;
        if (!ReadFlagBit(reader, wide)) return false;
        if (!wide) {
            uint32_t integer = 0;
            if (!ReadCompressedBits(reader, 4, integer)) return false;
            lua_pushinteger(L, static_cast<int32_t>(integer));
            return true;
        }
        bool asDouble = false;
        if (!ReadFlagBit(reader, asDouble)) return false;
        if (asDouble) {
            uint8_t bytes[8] = {};
            if (!reader.ReadBits(bytes, 64)) return false;
            double number = 0;
            std::memcpy(&number, bytes, 8);
            lua_pushnumber(L, number);
            return true;
        }
        float number = 0;
        if (!reader.Read(number)) return false;
        lua_pushnumber(L, number);
        return true;
    }
    if (kind == 4 || kind == 9) {
        uint32_t length = 0;
        if (!ReadCompressedBits(reader, kind == 4 ? 2 : 4, length) || length > 65536) return false;
        std::string text(length, '\0');
        if (length && !reader.ReadBits(reinterpret_cast<uint8_t*>(text.data()), length * 8)) return false;
        lua_pushlstring(L, text.data(), length);
        return true;
    }
    if (kind == 5) {
        uint32_t count = 0;
        if (!ReadCompressedBits(reader, 4, count) || count > 4096) return false;
        lua_newtable(L);
        for (uint32_t i = 0; i + 1 < count; i += 2) {
            if (!ReadLuaArg(L, reader, depth + 1) || !ReadLuaArg(L, reader, depth + 1)) return false;
            lua_settable(L, -3);
        }
        return true;
    }
    if (kind == 10) {
        uint32_t ref = 0;
        if (!ReadCompressedBits(reader, 4, ref)) return false;
        lua_pushnil(L);
        return true;
    }
    return false;
}

bool ActiveVmOnServerEvent(const uint8_t* body, size_t length, size_t bits) {
    if (!g_dx.L || !body) return false;
    Net::BitStreamReader reader(body, length, bits);
    uint32_t nameLength = 0;
    if (!ReadCompressedBits(reader, 2, nameLength) || nameLength == 0 || nameLength > 256) return false;
    std::string name(nameLength, '\0');
    if (!reader.ReadBits(reinterpret_cast<uint8_t*>(name.data()), nameLength * 8)) return false;
    uint32_t source = 0;
    if (!ReadElementBits(reader, source)) return false;
    uint32_t count = 0;
    if (!ReadCompressedBits(reader, 4, count) || count > 32) return false;
    lua_State* L = g_dx.L;
    const int top = lua_gettop(L);
    for (uint32_t i = 0; i < count; ++i) {
        if (!ReadLuaArg(L, reader, 0)) {
            lua_settop(L, top);
            return false;
        }
    }
    PushElement(L, source);
    lua_setglobal(L, "source");
    fireHandlers(name.c_str(), static_cast<int>(count));
    return true;
}

bool ActiveVmFeedScripts(const uint8_t* body, size_t length, size_t bits) {
    return g_active && g_active->feedClientScripts(body, length, bits);
}
bool ActiveVmOnChat(const char* text, int red, int green, int blue) {
    return g_active && g_active->onChatMessage(text, red, green, blue);
}
std::string ActiveVmPump() {
    return g_active ? g_active->pumpDx() : std::string();
}
void ActiveVmSetScreen(int width, int height) {
    if (g_active) g_active->setScreenSize(width, height);
    else {
        if (width > 0) g_dx.screenW = width;
        if (height > 0) g_dx.screenH = height;
    }
}
bool ActiveVmChatHooked() { return g_dx.chatHooked; }

} // namespace WorldRPG::Script