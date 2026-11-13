#include "DebugFlags.h"
#include "LuaVMStub.h"
#include "LuaApiExtra.h"
#include "LuaGuiApi.h"
#include "ResourceLoader.h"
#include "ResourcePackets.h"
#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/HudComponents.h"
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
#include <cmath>
#include <ctime>
#include <vector>
#include <mutex>

namespace WorldRPG::Script {

struct DxRuntime {
    lua_State* L = nullptr;
    int screenW = 1920;
    int screenH = 1080;
    bool chatHooked = false;
    std::unordered_map<std::string, std::vector<int>> handlers;
    // Resource that registered each handler, keyed by registry ref ("" for core scripts).
    std::unordered_map<int, std::string> handlerOwner;
    struct Timer { int ref = 0; int times = 1; int interval = 50; std::uint64_t next = 0; std::string owner; };
    std::vector<Timer> timers;
    struct PendingChat { std::string text; int r = 255, g = 255, b = 255; };
    std::vector<PendingChat> pendingChat;
    std::string frame;
    std::string owner;  // resource whose code is running; tags the handlers and timers it registers
};
DxRuntime g_dx;

// Touches from the Android UI thread, applied on the VM thread in pumpDx (desktop: CClientGame mouse
// handling fires onClientClick only while the cursor is showing). action: 0 down, 1 up, 2 move.
struct CursorEvent { int action = 0; float x = 0, y = 0; };
static std::mutex g_cursorMutex;
static std::vector<CursorEvent> g_cursorQueue;
static float g_cursorX = 0.f, g_cursorY = 0.f;
static LuaVMStub* g_active = nullptr;

// Sets the running resource for the duration of a Lua call (a load or a callback).
struct OwnerScope {
    explicit OwnerScope(std::string owner) : saved_(std::move(g_dx.owner)) { g_dx.owner = std::move(owner); }
    ~OwnerScope() { g_dx.owner = std::move(saved_); }
    OwnerScope(const OwnerScope&) = delete;
    OwnerScope& operator=(const OwnerScope&) = delete;

private:
    std::string saved_;
};

static std::string OwnerOfHandler(int ref) {
    auto found = g_dx.handlerOwner.find(ref);
    return found == g_dx.handlerOwner.end() ? std::string() : found->second;
}

static std::uint64_t DxNow() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

static bool g_eventCancelled = false;

static int l_cancelEvent(lua_State* L) {
    bool cancel = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : true;
    g_eventCancelled = cancel;
    return 0;
}

static int l_wasEventCancelled(lua_State* L) {
    lua_pushboolean(L, g_eventCancelled ? 1 : 0);
    return 1;
}

static void fireHandlers(const char* name, int nargs) {
    if (!g_dx.L) return;
    auto found = g_dx.handlers.find(name);
    if (found == g_dx.handlers.end() || found->second.empty()) {
        lua_pop(g_dx.L, nargs);
        return;
    }
    g_eventCancelled = false;
    // Args are already on the stack. Copy them for every handler. Iterate over a copy
    // of the list, and skip handlers removed by an earlier handler of the same event.
    const std::vector<int> refs = found->second;
    for (int ref : refs) {
        const auto& live = g_dx.handlers[name];
        if (std::find(live.begin(), live.end(), ref) == live.end()) continue;
        const std::string owner = OwnerOfHandler(ref);
        if (std::strcmp(name, "onClientRender") == 0 && WorldRPG::Debug::Flag(("nodx:" + owner + ";").c_str())) continue;
        OwnerScope scope(owner);
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
    g_dx.handlerOwner[ref] = g_dx.owner;
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
    // The renderer draws scale * 15 px, so a font taller than the default is scaled here.
    const char* font = lua_type(L, 8) == LUA_TSTRING ? lua_tostring(L, 8) : "default";
    char head[128];
    std::snprintf(head, sizeof(head), "T %.1f %.1f %.1f %.1f %.2f %u %d ",
        luaL_optnumber(L, 2, 0), luaL_optnumber(L, 3, 0),
        luaL_optnumber(L, 4, 0), luaL_optnumber(L, 5, 0),
        luaL_optnumber(L, 7, 1) * DxFontScale(font),
        static_cast<unsigned>(luaL_optinteger(L, 6, 0xFFFFFFFF)),
        lua_toboolean(L, 14));
    g_dx.frame += head;
    g_dx.frame += safe;
    g_dx.frame += '\n';
    return 0;
}

static int l_removeEventHandler(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, 0);
    if (!lua_isfunction(L, 3)) return 1;
    auto found = g_dx.handlers.find(name);
    if (found == g_dx.handlers.end()) return 1;
    auto& refs = found->second;
    for (auto it = refs.begin(); it != refs.end(); ++it) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, *it);
        const bool same = lua_rawequal(L, -1, 3) != 0;
        lua_pop(L, 1);
        if (!same) continue;
        const int ref = *it;
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        g_dx.handlerOwner.erase(ref);
        refs.erase(it);
        lua_pushboolean(L, 1);
        break;
    }
    return 1;
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

// Desktop getCursorPosition: relative x/y, then the world position. False while the cursor is hidden.
static int l_getCursorPosition(lua_State* L) {
    if (!GuiCursorShown()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float px = 0, py = 0, pz = 0, heading = 0;
    WorldRPG::Game::CopyPose(px, py, pz, heading);
    lua_pushnumber(L, g_dx.screenW > 0 ? g_cursorX / g_dx.screenW : 0.0);
    lua_pushnumber(L, g_dx.screenH > 0 ? g_cursorY / g_dx.screenH : 0.0);
    lua_pushnumber(L, px);
    lua_pushnumber(L, py);
    lua_pushnumber(L, pz);
    return 5;
}

// Desktop setPlayerHudComponentVisible(component, show) and its old name showPlayerHudComponent.
static int l_setPlayerHudComponentVisible(lua_State* L) {
    const char* component = luaL_optstring(L, 1, "");
    const bool show = lua_toboolean(L, 2) != 0;
    const bool ok = WorldRPG::Game::SetHudComponentVisible(component, show);
    if (!ok) LUA_LOGE("setPlayerHudComponentVisible: component '%s' is not supported on this client", component);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int l_isPlayerHudComponentVisible(lua_State* L) {
    lua_pushboolean(L, WorldRPG::Game::IsHudComponentVisible(luaL_optstring(L, 1, "")) ? 1 : 0);
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
    timer.owner = g_dx.owner;
    g_dx.timers.push_back(timer);
    lua_pushinteger(L, timer.ref);
    return 1;
}

static int l_nil(lua_State* L) { lua_pushnil(L); return 1; }
static int l_num0(lua_State* L) { lua_pushinteger(L, 0); return 1; }

// Desktop MTA getTime(): game-clock hour and minute. The phone does not read the GTA clock yet,
// so this uses the device clock. Scripts only use it for coarse population time buckets.
static int l_getTime(lua_State* L) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    lua_pushinteger(L, local.tm_hour);
    lua_pushinteger(L, local.tm_min);
    return 2;
}

// Desktop MTA getElementVelocity(): x, y, z. Remote velocity is not exposed to Lua yet.
static int l_getElementVelocity(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 3;
}

// Desktop MTA getElementRotation(): rx, ry, rz. Not tracked on the phone yet.
static int l_getElementRotation(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 3;
}

// Desktop MTA getDistanceBetweenPoints2D/3D(x1, y1, [z1], x2, y2, [z2]).
static int l_getDistanceBetweenPoints3D(lua_State* L) {
    const double x1 = luaL_checknumber(L, 1), y1 = luaL_checknumber(L, 2), z1 = luaL_checknumber(L, 3);
    const double x2 = luaL_checknumber(L, 4), y2 = luaL_checknumber(L, 5), z2 = luaL_checknumber(L, 6);
    lua_pushnumber(L, std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1) + (z2 - z1) * (z2 - z1)));
    return 1;
}
static int l_getDistanceBetweenPoints2D(lua_State* L) {
    const double x1 = luaL_checknumber(L, 1), y1 = luaL_checknumber(L, 2);
    const double x2 = luaL_checknumber(L, 3), y2 = luaL_checknumber(L, 4);
    lua_pushnumber(L, std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1)));
    return 1;
}

// Desktop MTA getPedMoveState(): "stand" until the engine exposes the ped's move state.
static int l_getPedMoveState(lua_State* L) {
    lua_pushstring(L, "stand");
    return 1;
}

// Desktop MTA engineStreamingGetModelLoadState(): "loaded" means the model is resident. Assume loaded.
static int l_engineStreamingGetModelLoadState(lua_State* L) {
    lua_pushstring(L, "loaded");
    return 1;
}

// Desktop MTA getPlayerName(): local nick from the launcher settings. Fixed text for now.
static int l_getPlayerName(lua_State* L) {
    lua_pushstring(L, "Player");
    return 1;
}
static int l_false(lua_State* L) { lua_pushboolean(L, 0); return 1; }
static int l_true(lua_State* L) { lua_pushboolean(L, 1); return 1; }
static int l_debug(lua_State* L) {
    if (lua_isstring(L, 1)) LUA_LOGI("Lua: %s", lua_tostring(L, 1));
    return 0;
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
    if (WorldRPG::Debug::Flag("notrigger")) return 0;
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
                                    GuiIsElement(id) ||
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
    if (const char* guiType = GuiElementType(id)) {
        lua_pushstring(L, guiType);
        return 1;
    }
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
        {"cancelEvent", l_cancelEvent},
        {"wasEventCancelled", l_wasEventCancelled},
        {"addEventHandler", l_addEventHandler},
        {"removeEventHandler", l_removeEventHandler},
        {"dxDrawRectangle", l_dxDrawRectangle},
        {"dxDrawText", l_dxDrawText},
        {"tocolor", l_tocolor},
        {"guiGetScreenSize", l_guiGetScreenSize},
        {"getCursorPosition", l_getCursorPosition},
        {"getTickCount", l_getTickCount},
        {"setTimer", l_setTimer},
        {"getElementHealth", l_getElementHealth},
        {"getPedArmor", l_num0},
        {"getTime", l_getTime},
        {"getPedOccupiedVehicle", l_false},
        {"getPedOccupiedVehicleSeat", l_false},
        {"getPlayerWantedLevel", l_num0},
        {"getElementModel", l_num0},
        {"getElementVelocity", l_getElementVelocity},
        {"getElementRotation", l_getElementRotation},
        {"getPlayerName", l_getPlayerName},
        {"bindKey", l_true},
        {"unbindKey", l_true},
        {"isTimer", l_false},
        {"killTimer", l_true},
        {"getDistanceBetweenPoints3D", l_getDistanceBetweenPoints3D},
        {"getDistanceBetweenPoints2D", l_getDistanceBetweenPoints2D},
        {"getPedMoveState", l_getPedMoveState},
        {"engineStreamingGetModelLoadState", l_engineStreamingGetModelLoadState},
        // Ped and element natives the ped-traffic client calls. Defaults until the engine exposes them.
        {"getPedRotation", l_num0},
        {"getPedAmmoInClip", l_num0},
        {"getPedTotalAmmo", l_num0},
        {"getPedWeapon", l_num0},
        {"getPedWeaponSlot", l_num0},
        {"getPedWeaponAccuracy", l_num0},
        {"getPedWeaponShootingRate", l_num0},
        {"getPedStat", l_num0},
        {"getVehicleType", l_num0},
        {"getGroundPosition", l_num0},
        {"getPedTask", l_false},
        {"getPedTarget", l_false},
        {"getPedControlState", l_false},
        {"getPedStayInSamePlace", l_false},
        {"isElementFrozen", l_false},
        {"isElementOnScreen", l_false},
        {"isPedOnGround", l_true},
        {"isAmbientPedSphereVisible", l_false},
        {"isPedNativeCoupleActive", l_false},
        {"isPedNativeCouplePresentationActive", l_false},
        {"isPedNativeGroupActive", l_false},
        {"isPedNativeAmbientCopWanderTask", l_false},
        {"acquirePedNativeCouple", l_false},
        {"acquirePedNativeCouplePresentation", l_false},
        {"acquirePedNativeGroup", l_false},
        {"releasePedNativeCouple", l_true},
        {"releasePedNativeCouplePresentation", l_true},
        {"releasePedNativeGroup", l_true},
        {"validatePedNativeCouple", l_false},
        {"updatePedNativeCouplePresentation", l_true},
        {"addPedNativeDamageResponseEvent", l_true},
        {"getPedNativeCoupleDiagnostic", l_false},
        {"getPedNativeGroupDiagnostic", l_false},
        {"removePedFromVehicle", l_true},
        {"setElementDimension", l_true},
        {"setElementInterior", l_true},
        {"setPedControlState", l_true},
        {"setPedJump", l_true},
        {"setPedNavigateTo", l_true},
        {"setPedRotation", l_true},
        {"setPedStandStill", l_true},
        {"setPedStayInSamePlace", l_true},
        {"setPedWeaponSlot", l_true},
        {"setPedWeaponAccuracy", l_true},
        {"setPedWeaponShootingRate", l_true},
        {"setCameraMatrix", l_true},
        {"setAmbientPedPopulationZoneState", l_true},
        {"resetAmbientPedPopulationModels", l_true},
        {"resetAmbientPedPopulationZonesToBootstrap", l_true},
        {"updateAmbientPedPopulationModels", l_true},
        {"processLineOfSight", l_false},
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
        {"setPlayerHudComponentVisible", l_setPlayerHudComponentVisible},
        {"showPlayerHudComponent", l_setPlayerHudComponentVisible},
        {"isPlayerHudComponentVisible", l_isPlayerHudComponentVisible},
        {"outputDebugString", l_debug},
        {"addCommandHandler", l_true},
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
    InstallExtraBindings(L);
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
    // Names still without an implementation. Anything else is bound in installDxBindings
    // or LuaApiExtra.cpp, which replace these stubs.
    const char* mtaClientFunctions[] = {
        "getResourceName"
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
    GuiReset();

    pImpl_->loadedScripts.clear();
    LUA_LOGI("LuaVM: reset complete");
}

void LuaVMStub::setScreenSize(int width, int height) {
    if (width > 0) g_dx.screenW = width;
    if (height > 0) g_dx.screenH = height;
}

// Runs one resource script with the resource's shared environment as _ENV.
// Globals a file defines are visible to the other files of the same resource
// (desktop: one Lua state per resource). Reads fall through to the VM globals.
static bool RunResourceChunk(lua_State* L, const std::string& chunk, const uint8_t* source, size_t length, int envRef) {
    if (luaL_loadbuffer(L, reinterpret_cast<const char*>(source), length, ("@" + chunk).c_str()) != LUA_OK) {
        LUA_LOGE("Resource script %s failed: %s", chunk.c_str(), lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, envRef);
    lua_setupvalue(L, -2, 1);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        LUA_LOGE("Resource script %s failed: %s", chunk.c_str(), lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

static bool ReadCachedScript(const std::string& path, std::vector<uint8_t>& out) {
    constexpr size_t kMaxScript = 4u * 1024u * 1024u;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    out.clear();
    uint8_t buffer[16384];
    size_t got = 0;
    while (out.size() <= kMaxScript && (got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        out.insert(out.end(), buffer, buffer + got);
    }
    const bool ok = std::ferror(file) == 0 && out.size() <= kMaxScript;
    std::fclose(file);
    return ok;
}

// Desktop CResource::Load (CResource.cpp 1636): PLAYER_RESOURCE_START (108) after onClientResourceStart,
// at high priority, reliable ordered. The root check mirrors desktop's "if (m_pResourceEntity)".
static void SendResourceStarted(const WorldRPG::Script::ResourceLoadJob& job) {
    if (job.rootElement == 0) {
        LUA_LOGI("LuaVM: resource %s has no root element: PLAYER_RESOURCE_START not sent", job.name.c_str());
        return;
    }
    Net::BitStreamWriter packet;
    ResourcePackets::BuildPlayerResourceStart(packet, job.netId, job.startCounter);
    const bool sent = Net::SendGamePacketHighPriority(packet);
    LUA_LOGI("LuaVM: PLAYER_RESOURCE_START %s netId=%u counter=%u (%s): %s", job.name.c_str(),
             static_cast<unsigned>(job.netId), static_cast<unsigned>(job.startCounter),
             job.startCounterFromWire ? "from RESOURCE_START" : "assumed first start, mobile shim stripped it",
             sent ? "sent" : "not sent");
}

// Desktop CResource::Load: no-cache scripts first, then the CLIENT_SCRIPT files in
// list order, then onClientResourceStart with the resource root as source.
static void RunResourceJob(lua_State* L, const WorldRPG::Script::ResourceLoadJob& job) {
    if (WorldRPG::Debug::Flag("nolua")) return;
    OwnerScope owner(job.name);
    refreshLocalPlayer(L);
    lua_newtable(L);                       // resource environment
    lua_newtable(L);                       // its metatable: reads fall through to the VM globals
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    PushElement(L, job.rootElement);
    lua_setfield(L, -2, "resourceRoot");
    const int envRef = luaL_ref(L, LUA_REGISTRYINDEX);
    int cachedOk = 0, noCacheOk = 0, failed = 0;
    for (const ResourceScript& script : job.noCacheScripts) {
        if (RunResourceChunk(L, script.chunkName, script.source.data(), script.source.size(), envRef)) ++noCacheOk;
        else ++failed;
    }
    for (const ResourceScript& script : job.clientScripts) {
        std::vector<uint8_t> source;
        if (!ReadCachedScript(script.cachePath, source) || source.empty()) {
            LUA_LOGE("Resource script %s: cached file unreadable (%s)", script.chunkName.c_str(), script.cachePath.c_str());
            ++failed;
            continue;
        }
        if (RunResourceChunk(L, script.chunkName, source.data(), source.size(), envRef)) ++cachedOk;
        else ++failed;
    }
    luaL_unref(L, LUA_REGISTRYINDEX, envRef);
    PushElement(L, job.rootElement);
    lua_setglobal(L, "source");
    fireHandlers("onClientResourceStart", 0);
    LUA_LOGI("LuaVM: resource %s started: %d cached scripts, %d no-cache scripts", job.name.c_str(), cachedOk, noCacheOk);
    if (failed) LUA_LOGE("LuaVM: resource %s: %d script(s) failed to load", job.name.c_str(), failed);
    SendResourceStarted(job);
}

static void FlushPendingChat(lua_State* L) {
    if (!g_dx.chatHooked || g_dx.pendingChat.empty()) return;
    auto found = g_dx.handlers.find("onClientChatMessage");
    if (found != g_dx.handlers.end()) {
        for (const auto& chat : g_dx.pendingChat) {
            for (int ref : found->second) {
                OwnerScope scope(OwnerOfHandler(ref));
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
    }
    g_dx.pendingChat.clear();
}

bool LuaVMStub::onChatMessage(const char* text, int red, int green, int blue) {
    if (!text) return true;
    if (!g_dx.chatHooked || !pImpl_->L) {
        return true;
    }
    lua_State* L = pImpl_->L;
    auto found = g_dx.handlers.find("onClientChatMessage");
    if (found == g_dx.handlers.end() || found->second.empty()) return true;

    g_eventCancelled = false;
    for (int ref : found->second) {
        OwnerScope scope(OwnerOfHandler(ref));
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
    return !g_eventCancelled;
}

// Fires onClientClick (and onClientCursorMove) for the queued touches. Signature as on desktop:
// button, state, absoluteX, absoluteY, worldX, worldY, worldZ, clickedElement.
static void PumpCursorEvents() {
    std::vector<CursorEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_cursorMutex);
        events.swap(g_cursorQueue);
    }
    if (events.empty() || !g_dx.L) return;
    float px = 0, py = 0, pz = 0, heading = 0;
    WorldRPG::Game::CopyPose(px, py, pz, heading);
    for (const CursorEvent& ev : events) {
        if (!GuiCursorShown()) break;  // a handler closed the panel; the rest of the gesture is not a click
        g_cursorX = ev.x;
        g_cursorY = ev.y;
        lua_State* L = g_dx.L;
        if (ev.action == 2) {
            if (g_dx.handlers.find("onClientCursorMove") == g_dx.handlers.end()) continue;
            lua_pushnumber(L, g_dx.screenW > 0 ? ev.x / g_dx.screenW : 0.0);
            lua_pushnumber(L, g_dx.screenH > 0 ? ev.y / g_dx.screenH : 0.0);
            lua_pushnumber(L, ev.x);
            lua_pushnumber(L, ev.y);
            lua_pushnumber(L, px);
            lua_pushnumber(L, py);
            lua_pushnumber(L, pz);
            fireHandlers("onClientCursorMove", 7);
            continue;
        }
        lua_pushstring(L, "left");
        lua_pushstring(L, ev.action == 0 ? "down" : "up");
        lua_pushnumber(L, ev.x);
        lua_pushnumber(L, ev.y);
        lua_pushnumber(L, px);
        lua_pushnumber(L, py);
        lua_pushnumber(L, pz);
        lua_pushboolean(L, 0);
        fireHandlers("onClientClick", 8);
    }
}

std::string LuaVMStub::pumpDx() {
    if (!pImpl_->L) return {};
    const auto now = DxNow();
    // Index loop: a timer callback may call setTimer, which reallocates g_dx.timers.
    for (size_t i = 0; i < g_dx.timers.size(); ++i) {
        if (g_dx.timers[i].times == 0 || now < g_dx.timers[i].next) continue;
        {
            OwnerScope scope(g_dx.timers[i].owner);
            lua_rawgeti(pImpl_->L, LUA_REGISTRYINDEX, g_dx.timers[i].ref);
            if (lua_pcall(pImpl_->L, 0, 0, 0) != LUA_OK) {
                LUA_LOGE("Lua timer: %s", lua_tostring(pImpl_->L, -1));
                lua_pop(pImpl_->L, 1);
            }
        }
        g_dx.timers[i].next = now + static_cast<std::uint64_t>(g_dx.timers[i].interval);
        if (g_dx.timers[i].times > 0) --g_dx.timers[i].times;
    }
    g_dx.timers.erase(std::remove_if(g_dx.timers.begin(), g_dx.timers.end(),
        [](const DxRuntime::Timer& timer) { return timer.times == 0; }), g_dx.timers.end());
    PumpCursorEvents();
    g_dx.frame.clear();
    if (!WorldRPG::Debug::Flag("nodx")) fireHandlers("onClientRender", 0);
    GuiAppendDraws(g_dx.frame);
    // "C" line: the cursor is showing, so the Android overlay takes touches (MtaDxView).
    std::string out = GuiCursorShown() ? "C\n" + g_dx.frame : g_dx.frame;
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
    GuiReset();

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
    if (!ReadCompressedBits(reader, 2, nameLength) || nameLength == 0 || nameLength > 256) {
        LUA_LOGE("LuaVM: server event: name length did not parse (%zu bytes)", length);
        return false;
    }
    std::string name(nameLength, '\0');
    if (!reader.ReadBits(reinterpret_cast<uint8_t*>(name.data()), nameLength * 8)) {
        LUA_LOGE("LuaVM: server event: name did not parse (%zu bytes)", length);
        return false;
    }
    uint32_t source = 0;
    if (!ReadElementBits(reader, source)) {
        LUA_LOGE("LuaVM: server event %s: source did not parse", name.c_str());
        return false;
    }
    uint32_t count = 0;
    if (!ReadCompressedBits(reader, 4, count) || count > 32) {
        LUA_LOGE("LuaVM: server event %s: argument count did not parse", name.c_str());
        return false;
    }
    lua_State* L = g_dx.L;
    const int top = lua_gettop(L);
    for (uint32_t i = 0; i < count; ++i) {
        if (!ReadLuaArg(L, reader, 0)) {
            LUA_LOGE("LuaVM: server event %s: argument %u of %u did not parse", name.c_str(), i + 1, count);
            lua_settop(L, top);
            return false;
        }
    }
    PushElement(L, source);
    lua_setglobal(L, "source");
    const auto handlers = g_dx.handlers.find(name);
    if (handlers == g_dx.handlers.end() || handlers->second.empty()) {
        LUA_LOGI("LuaVM: server event %s has no client handler (%u args)", name.c_str(), static_cast<unsigned>(count));
    }
    fireHandlers(name.c_str(), static_cast<int>(count));
    return true;
}

// Removes the handlers and timers that a resource registered, and the resource's
// Lua state with them (its environment is only referenced by those closures).
static void DropResourceState(lua_State* L, const std::string& name) {
    GuiDropResource(name);
    for (auto& [event, refs] : g_dx.handlers) {
        refs.erase(std::remove_if(refs.begin(), refs.end(), [&](int ref) {
            if (OwnerOfHandler(ref) != name) return false;
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
            g_dx.handlerOwner.erase(ref);
            return true;
        }), refs.end());
    }
    for (auto& timer : g_dx.timers) {
        if (timer.owner != name) continue;
        luaL_unref(L, LUA_REGISTRYINDEX, timer.ref);
        timer.times = 0;
    }
    g_dx.timers.erase(std::remove_if(g_dx.timers.begin(), g_dx.timers.end(),
        [](const DxRuntime::Timer& timer) { return timer.times == 0; }), g_dx.timers.end());
    const auto chat = g_dx.handlers.find("onClientChatMessage");
    g_dx.chatHooked = chat != g_dx.handlers.end() && !chat->second.empty();
}

// Desktop CResource::Stop: onClientResourceStop runs with the resource root as source and
// argument, then the resource's state is dropped. Only that resource's handlers run.
static void RunResourceStop(lua_State* L, const std::string& name, uint32_t root) {
    PushElement(L, root);
    lua_setglobal(L, "source");
    const auto found = g_dx.handlers.find("onClientResourceStop");
    if (found != g_dx.handlers.end()) {
        const std::vector<int> refs = found->second;
        for (int ref : refs) {
            const auto& live = g_dx.handlers["onClientResourceStop"];
            if (std::find(live.begin(), live.end(), ref) == live.end()) continue;
            if (OwnerOfHandler(ref) != name) continue;
            OwnerScope scope(name);
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            PushElement(L, root);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                LUA_LOGE("Lua onClientResourceStop (%s): %s", name.c_str(), lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
    }
    DropResourceState(L, name);
}

// RESOURCE_STOP (83), desktop CPacketHandler::Packet_ResourceStop: the resource is removed
// from the loader, and its scripts are stopped if they had run.
void ActiveVmOnResourceStop(const uint8_t* body, size_t length, size_t bits) {
    uint16_t netId = 0;
    if (!ResourcePackets::ParseResourceStop(body, length, bits, netId)) {
        LUA_LOGE("LuaVM: RESOURCE_STOP body too short");
        return;
    }
    const ForgottenResource gone = ResourceLoader::Forget(netId);
    if (!gone.found) {
        LUA_LOGI("LuaVM: RESOURCE_STOP for unknown resource net id %u", static_cast<unsigned>(netId));
        return;
    }
    if (g_dx.L) {
        if (gone.started) RunResourceStop(g_dx.L, gone.name, gone.rootElement);
        else DropResourceState(g_dx.L, gone.name);
    }
    LUA_LOGI("LuaVM: resource %s stopped (net id %u, scripts ran: %s)", gone.name.c_str(), static_cast<unsigned>(netId),
             gone.started ? "yes" : "no");
}

bool ActiveVmFeedScripts(const uint8_t* body, size_t length, size_t bits) {
    if (WorldRPG::Debug::Flag("noresources")) return true;
    return WorldRPG::Script::ResourceLoader::OnClientScripts(body, length, bits);
}
void ActiveVmOnResourceStart(const WorldRPG::Net::ResourceStartInfo& info) {
    WorldRPG::Script::ResourceLoader::OnResourceStart(info);
}
bool ActiveVmPulseResources() {
    if (!g_active || !g_active->isReady() || !g_dx.L) return false;
    const bool wasHooked = g_dx.chatHooked;
    for (const auto& job : WorldRPG::Script::ResourceLoader::Pump()) RunResourceJob(g_dx.L, job);
    FlushPendingChat(g_dx.L);
    return !wasHooked && g_dx.chatHooked;
}
void ActiveVmResetSession() {
    WorldRPG::Script::ResourceLoader::Reset();
    if (!g_active) return;
    // A new session starts from a clean VM: no handlers, timers or globals from the old one.
    const int width = g_dx.screenW, height = g_dx.screenH;
    // The old session's scripts may have hidden HUD components; the new one starts with all shown.
    WorldRPG::Game::ResetHudComponents();
    g_active->shutdown();
    g_active->initialize();
    g_dx.screenW = width;
    g_dx.screenH = height;
}
bool ActiveVmOnChat(const char* text, int red, int green, int blue) {
    return g_active && g_active->onChatMessage(text, red, green, blue);
}
std::string ActiveVmPump() {
    return g_active ? g_active->pumpDx() : std::string();
}
void ActiveVmScreenSize(int& width, int& height) {
    width = g_dx.screenW;
    height = g_dx.screenH;
}
const std::string& ActiveVmOwner() {
    return g_dx.owner;
}
void ActiveVmQueueCursor(int action, float x, float y) {
    std::lock_guard<std::mutex> lock(g_cursorMutex);
    if (g_cursorQueue.size() < 256) g_cursorQueue.push_back(CursorEvent{action, x, y});
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
