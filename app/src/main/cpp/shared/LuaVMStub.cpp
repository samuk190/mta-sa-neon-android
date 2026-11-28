#include "DebugFlags.h"
#include "LuaVMStub.h"
#include "LuaVmSet.h"
#include "WorldObjectFeed.h"
#include "LuaRocketSoccerApi.h"
extern "C" {
#include "LuaVm.h"
}
#include "EngineAssets.h"
#include "LuaElementTree.h"
#include "LuaCameraApi.h"
#include "LuaFieldApi.h"
#include "LuaUtilApi.h"
#include "LuaFileApi.h"
#include "LuaXmlApi.h"
#include "LuaBrowserApi.h"
#include "LuaDiscordApi.h"
#include "LuaStoredElementApi.h"
#include "LuaClothesApi.h"
#include "LuaWeaponApi.h"
#include "LuaCryptApi.h"
#include "LuaResourceApi.h"
#include "LuaAudioApi.h"
#include "VoiceRuntime.h"
#include "LuaMtaWindowApi.h"
#include "LuaEventRegistry.h"
#include "LuaKeyBindApi.h"
#include "LuaDxApi.h"
#include "LuaSpatialApi.h"
#include "LuaMarkerApi.h"
#include "LuaBlipApi.h"
#include "LuaRadarApi.h"
#include "LuaPickupApi.h"
#include "LuaLightApi.h"
#include "LuaBirdApi.h"
#include "LuaTeamApi.h"
#include "LuaRopeApi.h"
#include "LuaNativeUiApi.h"
#include "LuaWorldObjectApi.h"
#include "LuaWaterApi.h"
#include "LuaFireApi.h"
#include "LuaFxApi.h"
#include "LuaWorldQueryApi.h"
#include "LuaSightApi.h"
#include "LuaAmbientApi.h"
#include "LuaOopApi.h"
#include "DxShader.h"
#include "LuaSvgApi.h"
#include "LuaApiExtra.h"
#include "LuaGuiApi.h"
#include "ResourceLoader.h"
#include "ResourcePackets.h"
#include "ServerWorld.h"
#include "CorePackets.h"
#include "ServerRpc.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/ProjectileEngine.h"
#include "../gtasa/HudComponents.h"
#include "../net/HttpDownloader.h"
#include "../net/NeonJoinProtocol.h"
#include "../net/raknet/RakNetWrapper.h"

#include <zlib.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <array>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <ctime>
#include <variant>
#include <vector>
#include <mutex>
#include <optional>

namespace WorldRPG::JniBridge { void PostGameLine(int kind, const char* text); }

namespace WorldRPG::Script {

struct DxRuntime {
    lua_State* L = nullptr;
    int screenW = 1920;
    int screenH = 1080;
    bool chatHooked = false;
    struct Handler {
        int ref = 0;
        uint32_t element = 0;
        bool propagated = true;
        uint16_t netId = 0;
        uint32_t generation = 0;
    };
    std::unordered_map<std::string, std::vector<Handler>> handlers;
    // Resource that registered each handler, keyed by registry ref ("" for core scripts).
    std::unordered_map<int, std::string> handlerOwner;
    struct Timer {
        int ref = 0;
        int times = 1;
        int interval = 50;
        std::uint64_t next = 0;
        std::uint64_t left = 0;
        bool paused = false;
        std::string owner;
        uint16_t netId = 0;
        uint32_t generation = 0;
    };
    std::vector<Timer> timers;
    struct PendingChat { std::string text; int r = 255, g = 255, b = 255; };
    std::vector<PendingChat> pendingChat;
    std::string frame;
    std::string postGuiFrame;
    std::string owner;  // resource whose code is running; tags the handlers and timers it registers
    // The device self-test script was looked for in this VM (RunDeviceSelfTest).
    bool selfTestRan = false;
    uint16_t ownerNetId = 0;
    uint32_t ownerGeneration = 0;
};
DxRuntime g_dx;

// g_dx.L is the current VM: the Lua 5.3 state, or the Lua 5.1 state while a
// callback that lives there runs (LuaVmSet.h). Code that pushes onto g_dx.L
// and calls fireHandlers keeps working whichever VM is current.
struct VmScope {
    explicit VmScope(lua_State* vm) : saved_(g_dx.L) {
        if (vm) g_dx.L = vm;
    }
    ~VmScope() { g_dx.L = saved_; }
    VmScope(const VmScope&) = delete;
    VmScope& operator=(const VmScope&) = delete;

private:
    lua_State* saved_;
};

// The VM a stored callback lives in, or fallback for refs that are not real.
static lua_State* RefVmOr(int ref, lua_State* fallback) {
    lua_State* vm = Vms::RefVm(ref);
    return vm ? vm : fallback;
}

// ActiveVmSetResourceLua: 0 Automatic, 1 Lua 5.3, 2 Lua 5.1.
static int g_resourceLua = 0;

// Script errors raised by resource code since the last ActiveVmTakeScriptErrors
// ("where: message"). Kept for the host tests; capped so a script that fails
// every frame cannot grow it without bound.
static std::vector<std::string> g_scriptErrors;
static void NoteScriptError(const char* where, const char* message) {
    if (g_scriptErrors.size() >= 256) return;
    g_scriptErrors.push_back(std::string(where ? where : "?") + ": " + (message ? message : "(error object is not a string)"));
}

// Which VM each started resource runs in, by generation.
static std::unordered_map<uint32_t, lua_State*> g_resourceVm;
static lua_State* ResourceVm(uint32_t generation) {
    const auto found = g_resourceVm.find(generation);
    return found != g_resourceVm.end() && found->second ? found->second : g_dx.L;
}

// Touches from the Android UI thread, applied on the VM thread in pumpDx (desktop: CClientGame mouse
// handling fires onClientClick only while the cursor is showing). action: 0 down, 1 up, 2 move.
struct CursorEvent { int action = 0; float x = 0, y = 0; };
// Soft-keyboard edits from the UI thread. text is inserted after backspaces.
struct ScriptInput { std::string text; int backspaces = 0; std::string key; };
static std::mutex g_inputMutex;
using UiInput = std::variant<CursorEvent, ScriptInput, NativeWeaponFire>;
static std::vector<UiInput> g_inputQueue;
// The IME action button asked to leave the field. Holds the keyboard closed until the next tap.
static bool g_imeDismissed = false;
static float g_cursorX = 0.f, g_cursorY = 0.f;
static LuaVMStub* g_active = nullptr;

// Sets the running resource for the duration of a Lua call (a load or a callback).
struct OwnerScope {
    OwnerScope(std::string owner, uint16_t netId, uint32_t generation)
        : saved_(std::move(g_dx.owner)), savedNet_(g_dx.ownerNetId), savedGen_(g_dx.ownerGeneration) {
        g_dx.owner = std::move(owner);
        g_dx.ownerNetId = netId;
        g_dx.ownerGeneration = generation;
    }
    ~OwnerScope() {
        g_dx.owner = std::move(saved_);
        g_dx.ownerNetId = savedNet_;
        g_dx.ownerGeneration = savedGen_;
    }
    OwnerScope(const OwnerScope&) = delete;
    OwnerScope& operator=(const OwnerScope&) = delete;

private:
    std::string saved_;
    uint16_t savedNet_ = 0;
    uint32_t savedGen_ = 0;
};

// Saves sourceResource and sourceResourceRoot, then sets them for this call.
struct EventSourceScope {
    EventSourceScope(uint16_t netId, uint32_t generation) : state_(g_dx.L) {
        if (!state_) return;
        lua_getglobal(state_, "sourceResource");
        resourceRef_ = luaL_ref(state_, LUA_REGISTRYINDEX);
        lua_getglobal(state_, "sourceResourceRoot");
        rootRef_ = luaL_ref(state_, LUA_REGISTRYINDEX);
        ApplyEventSource(state_, netId, generation);
    }
    ~EventSourceScope() {
        if (!state_) return;
        lua_rawgeti(state_, LUA_REGISTRYINDEX, rootRef_);
        lua_setglobal(state_, "sourceResourceRoot");
        lua_rawgeti(state_, LUA_REGISTRYINDEX, resourceRef_);
        lua_setglobal(state_, "sourceResource");
        luaL_unref(state_, LUA_REGISTRYINDEX, rootRef_);
        luaL_unref(state_, LUA_REGISTRYINDEX, resourceRef_);
    }
    EventSourceScope(const EventSourceScope&) = delete;
    EventSourceScope& operator=(const EventSourceScope&) = delete;

private:
    lua_State* state_ = nullptr;
    int resourceRef_ = LUA_NOREF;
    int rootRef_ = LUA_NOREF;
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
static bool g_wasEventCancelled = false;

static int l_cancelEvent(lua_State* L) {
    bool cancel = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : true;
    g_eventCancelled = cancel;
    return 0;
}

static int l_wasEventCancelled(lua_State* L) {
    // CEvents::WasEventCancelled reads the flag PostEventPulse stored (CEvents.cpp 143-157).
    lua_pushboolean(L, g_wasEventCancelled ? 1 : 0);
    return 1;
}

// CallEvent's minClientVersion (CMapEventManager.cpp:169-173): when set, only
// handlers of resources whose meta.xml asks for at least that client version run.
static const char* g_eventMinClient = nullptr;

// CMtaVersion::operator< (CMtaVersion.h:39-48): compared as strings with the
// build type character (index 6) set to 9 on both sides.
static bool MtaVersionLess(std::string a, std::string b) {
    if (a.size() > 6) a[6] = '9';
    if (b.size() > 6) b[6] = '9';
    return a < b;
}

static bool HandlerMeetsMinClient(const DxRuntime::Handler& handler, const char* minClient) {
    if (!minClient) return true;
    ResourceLoader::ResourceView view;
    if (!ResourceLoader::CopyResourceByGeneration(handler.generation, view)) return false;
    return !MtaVersionLess(view.minClientRequirement, minClient);
}

static bool HandlerLive(const std::vector<DxRuntime::Handler>& live, int ref) {
    for (const DxRuntime::Handler& handler : live)
        if (handler.ref == ref) return true;
    return false;
}

// CEvents::PreEventPulse / PostEventPulse (CEvents.cpp 136-147) around
// CClientEntity::CallParentEvent (CClientEntity.cpp 811-823). Returns false when
// cancelEvent ran. A nested call saves the outer cancel flag and restores it.
// from is the state the nargs arguments were pushed on (default: the current
// VM). Each handler runs in its own VM; the arguments are copied there.
// callOnChildren is CallEvent's bCallOnChildren: after the element and its
// parents, the propagated handlers on its descendants run, in tree order
// (CClientEntity::CallEvent 746-781, CallEventNoParent 783-809).
static bool fireHandlers(const char* name, int nargs, uint32_t base, lua_State* from = nullptr,
                         bool callOnChildren = false) {
    if (!from) from = g_dx.L;
    if (!from) return true;
    // This call's minimum client version; events the handlers fire have none.
    const char* const minClient = g_eventMinClient;
    g_eventMinClient = nullptr;
    const bool saved = g_eventCancelled;
    g_eventCancelled = false;
    g_wasEventCancelled = false;
    const int argBase = lua_gettop(from) - nargs + 1;
    std::optional<EventSourceScope> source;
    {
        VmScope vm(Vms::MainOf(from));
        source.emplace(g_dx.ownerNetId, g_dx.ownerGeneration);
    }
    auto finish = [&](bool cancelled) {
        g_wasEventCancelled = cancelled;
        g_eventCancelled = saved;
        {
            VmScope vm(Vms::MainOf(from));
            source.reset();
        }
        lua_pop(from, nargs);
        return !cancelled;
    };
    auto found = g_dx.handlers.find(name);
    if (found == g_dx.handlers.end() || found->second.empty() || base == 0) return finish(false);
    std::vector<uint32_t> chain;
    CollectEventChain(base, chain);
    const std::vector<DxRuntime::Handler> copy = found->second;
    std::vector<int> order;
    for (size_t i = 0; i < chain.size(); ++i) {
        for (const DxRuntime::Handler& handler : copy) {
            if (handler.element != chain[i]) continue;
            if (i > 0 && !handler.propagated) continue;
            if (HandlerMeetsMinClient(handler, minClient)) order.push_back(handler.ref);
        }
    }
    if (callOnChildren) {
        // Only handlers below base can run here; the tree is walked only when one exists.
        std::vector<const DxRuntime::Handler*> below;
        for (const DxRuntime::Handler& handler : copy)
            if (handler.propagated && handler.element != base &&
                std::find(chain.begin(), chain.end(), handler.element) == chain.end() &&
                ClientDescendantOf(handler.element, base))
                below.push_back(&handler);
        if (!below.empty()) {
            std::vector<uint32_t> tree;
            CollectEventDescendants(base, tree);
            for (uint32_t element : tree)
                for (const DxRuntime::Handler* handler : below)
                    if (handler->element == element && HandlerMeetsMinClient(*handler, minClient)) order.push_back(handler->ref);
            // Server elements outside the client tree are children of the root.
            if (base == ClientRootElement())
                for (const DxRuntime::Handler* handler : below)
                    if (!ClientElementInTree(handler->element) && HandlerMeetsMinClient(*handler, minClient))
                        order.push_back(handler->ref);
        }
    }
    lua_State* fromVm = Vms::MainOf(from);
    for (int ref : order) {
        const auto live = g_dx.handlers.find(name);
        if (live == g_dx.handlers.end() || !HandlerLive(live->second, ref)) continue;
        const std::string owner = OwnerOfHandler(ref);
        if (std::strcmp(name, "onClientRender") == 0 && WorldRPG::Debug::Flag(("nodx:" + owner + ";").c_str())) continue;
        uint16_t netId = 0;
        uint32_t generation = 0;
        for (const DxRuntime::Handler& handler : copy)
            if (handler.ref == ref) {
                netId = handler.netId;
                generation = handler.generation;
            }
        lua_State* T = RefVmOr(ref, fromVm);
        VmScope vm(T);
        // A handler in the other VM: that VM gets this event's source and
        // sourceResource too (the caller set them in its own VM).
        std::optional<EventSourceScope> otherSource;
        const bool other = T != fromVm;
        if (other) {
            lua_getglobal(T, "source");
            PushElement(T, base);
            lua_setglobal(T, "source");
            otherSource.emplace(g_dx.ownerNetId, g_dx.ownerGeneration);
        }
        {
            OwnerScope scope(owner, netId, generation);
            Vms::PushRef(T, ref);
            Vms::CopyRange(from, argBase, nargs, T);
            if (lua_pcall(T, nargs, 0, 0) != LUA_OK) {
                LUA_LOGE("Lua %s: %s", name, lua_tostring(T, -1));
                NoteScriptError(name, lua_tostring(T, -1));
                lua_pop(T, 1);
            }
        }
        if (other) {
            otherSource.reset();
            lua_setglobal(T, "source");
        }
    }
    return finish(g_eventCancelled);
}

// Desktop onClientKey is one down and one up (CClientGame key handler). state is the press boolean.
static void FireKey(const char* key) {
    // source is the root element (CClientGame.cpp 2326: m_pRootEntity->CallEvent).
    lua_getglobal(g_dx.L, "source");
    PushElement(g_dx.L, ClientRootElement());
    lua_setglobal(g_dx.L, "source");
    lua_pushstring(g_dx.L, key);
    lua_pushboolean(g_dx.L, 1);
    fireHandlers("onClientKey", 2, ClientRootElement());
    lua_pushstring(g_dx.L, key);
    lua_pushboolean(g_dx.L, 0);
    fireHandlers("onClientKey", 2, ClientRootElement());
    lua_setglobal(g_dx.L, "source");
}

// source is the element. Changed and Accepted also take it as their argument
// (CClientGame.cpp 2811-2812). Focus and blur take none (2826-2827).
static void FireElementEvent(const char* name, uint32_t id, bool withArg) {
    if (!g_dx.L || id == 0) return;
    lua_getglobal(g_dx.L, "source");
    PushElement(g_dx.L, id);
    lua_setglobal(g_dx.L, "source");
    if (withArg) PushElement(g_dx.L, id);
    fireHandlers(name, withArg ? 1 : 0, id, nullptr, true);  // CClientGUIElement events call children
    lua_setglobal(g_dx.L, "source");
}

static void OnGuiFocusHook(uint32_t id, bool focused) {
    static int depth = 0;
    if (depth > 4 || !g_dx.L) return;
    ++depth;
    FireElementEvent(focused ? "onClientGUIFocus" : "onClientGUIBlur", id, false);
    --depth;
}

static size_t CodePointLength(const std::string& text, size_t index) {
    if (index >= text.size()) return 0;
    const auto lead = static_cast<unsigned char>(text[index]);
    size_t n = 1;
    if ((lead & 0xE0) == 0xC0) n = 2;
    else if ((lead & 0xF0) == 0xE0) n = 3;
    else if ((lead & 0xF8) == 0xF0) n = 4;
    if (index + n > text.size()) return 1;
    return n;
}

// CClientGame::CharacterKeyHandler (2355-2381) for a DX edit, and the CEGUI edit
// handlers for a focused gui-edit (onClientGUIChanged / onClientGUIAccepted).
// A focused gui-edit consumes the keys, so onClientCharacter does not also run.
static void ApplyScriptInput(const ScriptInput& ev) {
    const uint32_t edit = GuiFocusedEdit();
    bool enter = ev.key == "enter";
    // The soft-keyboard action button is "tab": next DX field, not submit.
    // Enter stays available for an explicit enter key. The button also closes the IME.
    const bool tab = ev.key == "tab";
    if (tab) g_imeDismissed = true;
    if (edit) {
        bool changed = false;
        const bool memo = GuiFocusedMemo();
        bool textNewline = false;
        for (int i = 0; i < ev.backspaces; ++i) {
            if (GuiEditErase()) changed = true;
        }
        for (size_t i = 0; i < ev.text.size();) {
            const size_t n = CodePointLength(ev.text, i);
            if (n == 0) break;
            const unsigned char lead = static_cast<unsigned char>(ev.text[i]);
            if (ev.text[i] == '\n' || ev.text[i] == '\r') {
                if (memo) {
                    if (ev.text[i] != '\n' || i == 0 || ev.text[i - 1] != '\r') {
                        if (GuiEditAppend("\n",1)) changed = true;
                    }
                    textNewline = true;
                } else enter = true;
            }
            else if (lead >= 32 && GuiEditAppend(ev.text.data() + i, n)) changed = true;
            i += n;
        }
        if (memo && enter && !textNewline && GuiEditAppend("\n",1)) changed = true;
        if (changed) FireElementEvent("onClientGUIChanged", edit, true);
        if (enter && !memo) {
            const uint32_t still = GuiFocusedEdit();
            FireElementEvent("onClientGUIAccepted", still ? still : edit, true);
        }
        return;
    }
    if (tab) FireKey("tab");
    if (!GuiKeyCapture()) return;
    for (int i = 0; i < ev.backspaces; ++i) FireKey("backspace");
    for (size_t i = 0; i < ev.text.size();) {
        const size_t n = CodePointLength(ev.text, i);
        if (n == 0) break;
        const unsigned char lead = static_cast<unsigned char>(ev.text[i]);
        if (ev.text[i] != '\n' && ev.text[i] != '\r' && lead >= 32) {
            lua_pushlstring(g_dx.L, ev.text.data() + i, n);
            fireHandlers("onClientCharacter", 1, ClientRootElement());
        }
        i += n;
    }
    if (enter) FireKey("enter");
}

// Desktop CRegisteredCommands::ProcessCommand: every handler of the command, called as the resource
// that registered it, with the registered name and the argument string. The matches are taken
// first because a handler may add or remove command handlers.
bool ActiveVmRunCommand(const char* name, const char* args) {
    if (!g_dx.L || !name) return false;
    lua_State* L = g_dx.L;
    std::vector<CommandCall> calls;
    CommandHandlersCollect(name, calls);
    bool handled = false;
    for (const CommandCall& call : calls) {
        if (!CommandHandlerLive(call.id)) continue;
        OwnerScope scope(call.owner, call.netId, call.generation);
        lua_State* L = RefVmOr(call.ref, g_dx.L);
        VmScope vm(L);
        Vms::PushRef(L, call.ref);
        lua_pushstring(L, call.name.c_str());
        // CRegisteredCommands::CallCommandHandler (CRegisteredCommands.cpp:183-206): one string per
        // space-separated word, as strtok splits them.
        int count = 1;
        const std::string text = args ? args : "";
        for (size_t at = 0; at < text.size();) {
            const size_t start = text.find_first_not_of(' ', at);
            if (start == std::string::npos) break;
            const size_t end = text.find(' ', start);
            const size_t stop = end == std::string::npos ? text.size() : end;
            lua_pushlstring(L, text.data() + start, stop - start);
            ++count;
            at = stop;
        }
        if (lua_pcall(L, count, 0, 0) != LUA_OK) {
            LUA_LOGE("Lua command %s: %s", call.name.c_str(), lua_tostring(L, -1));
            NoteScriptError(call.name.c_str(), lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        handled = true;
    }
    if (!handled) {
        ActiveVmOnCoreCommand(name, args ? args : "");
    }
    return handled;
}

// Device testing without a server change: after the join, the script
// files/neon_selftest.lua in the app's private directory runs once per VM as the resource
// "neon_selftest". Put it there with
//   adb shell run-as com.saonline.neonlauncher sh -c 'cat > files/neon_selftest.lua' < test.lua
// No file, no effect. Its print() lines go to logcat.
static void RunDeviceSelfTest() {
#ifdef __ANDROID__
    if (g_dx.selfTestRan || !g_dx.L || WorldRPG::ServerWorld::LocalPlayer() == 0) return;
    g_dx.selfTestRan = true;
    const char* path = "/data/data/com.saonline.neonlauncher/files/neon_selftest.lua";
    std::FILE* probe = std::fopen(path, "rb");
    if (!probe) return;
    std::fclose(probe);
    lua_State* L = g_dx.L;
    OwnerScope scope("neon_selftest", 0, 0);
    if (luaL_loadfile(L, path) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
        LUA_LOGE("SELFTEST failed: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    LUA_LOGI("SELFTEST script loaded");
#endif
}

// Desktop CClientVectorGraphic::OnUpdate: after an svg's picture changed, its update callback is
// called with the svg element, as the resource that owns it. It runs here, at the next pump, so
// the call that changed the svg has already returned.
static void ApplySvgUpdates() {
    if (!g_dx.L) return;
    lua_State* L = g_dx.L;
    std::vector<SvgUpdate> updates;
    SvgTakeUpdates(updates);
    for (const SvgUpdate& update : updates) {
        if (!SvgCallbackLive(update.id, update.ref)) continue;
        OwnerScope scope(update.owner, update.netId, update.generation);
        lua_State* L = RefVmOr(update.ref, g_dx.L);
        VmScope vm(L);
        Vms::PushRef(L, update.ref);
        PushElement(L, update.id);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            LUA_LOGE("Lua svg update callback: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
}

static std::mutex g_commandMutex;
static std::vector<std::string> g_commandQueue;
// VM thread only.
static std::vector<std::string> g_serverCommands;

void ActiveVmQueueCommand(const char* line) {
    if (!line || !*line) return;
    std::lock_guard<std::mutex> lock(g_commandMutex);
    if (g_commandQueue.size() < 64) g_commandQueue.emplace_back(line);
}

std::vector<std::string> ActiveVmTakeServerCommands() {
    std::vector<std::string> out;
    out.swap(g_serverCommands);
    return out;
}

// Desktop COMMAND_Executed (ClientCommands.cpp:32-90) for a command the core did not handle: the
// registered handlers, then onClientConsole on the local player with the whole line ("login" is
// censored). Sending the line to the server is the caller's part.
static void ProcessClientCommand(const std::string& name, const std::string& args) {
    if (!g_dx.L || name.empty()) return;
    lua_State* L = g_dx.L;
    ActiveVmRunCommand(name.c_str(), args.c_str());
    const uint32_t local = WorldRPG::ServerWorld::LocalPlayer();
    if (local == 0) return;
    std::string line = args.empty() ? name : name + " " + args;
    if (strcasecmp(name.c_str(), "login") == 0) line = name + " ***";
    lua_getglobal(L, "source");
    PushElement(L, local);
    lua_setglobal(L, "source");
    lua_pushstring(L, line.c_str());
    fireHandlers("onClientConsole", 1, local);
    lua_setglobal(L, "source");
}

static void ApplyChatCommands() {
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(g_commandMutex);
        lines.swap(g_commandQueue);
    }
    for (const std::string& line : lines) {
        const size_t space = line.find(' ');
        const std::string name = line.substr(0, space);
        const std::string args = space == std::string::npos ? std::string() : line.substr(space + 1);
        ProcessClientCommand(name, args);
    }
}

// A hardware key (KeyBindsQueueKey). Desktop CClientGame::KeyStrokeHandler fires onClientKey first, and a
// cancelled event stops the binds. CKeyBinds then calls the bound functions and commands unless the
// input goes to a GUI (INPUTMODE_NO_BINDS) or the chat box. CScriptKeyBinds::Call passes the key
// name, "down" or "up", then the bind's own arguments.
static void ApplyKeyEvents() {
    if (!g_dx.L) return;
    lua_State* L = g_dx.L;
    std::string key;
    bool down = false;
    while (KeyBindsNextKey(key, down)) {
        // source is the root element (CClientGame.cpp 2326: m_pRootEntity->CallEvent).
        lua_getglobal(L, "source");
        PushElement(L, ClientRootElement());
        lua_setglobal(L, "source");
        lua_pushstring(L, key.c_str());
        lua_pushboolean(L, down ? 1 : 0);
        const bool allowed = fireHandlers("onClientKey", 2, ClientRootElement());
        lua_setglobal(L, "source");
        if (!allowed) continue;
        if (GuiBlocksBinds() || MtaWindowChatInputActive()) continue;
        std::vector<KeyBindCall> calls;
        std::vector<KeyBindCommand> commands;
        KeyBindsCollect(key, down, calls, commands);
        for (const KeyBindCall& call : calls) {
            if (!KeyBindLive(call.id)) continue;
            OwnerScope scope(call.owner, call.netId, call.generation);
            lua_State* B = RefVmOr(call.ref, L);
            VmScope vm(B);
            Vms::PushRef(B, call.ref);
            lua_pushstring(B, key.c_str());
            lua_pushstring(B, down ? "down" : "up");
            for (int ref : call.args) Vms::PushRef(B, ref);
            if (lua_pcall(B, 2 + static_cast<int>(call.args.size()), 0, 0) != LUA_OK) {
                LUA_LOGE("Lua bindKey %s: %s", key.c_str(), lua_tostring(B, -1));
                NoteScriptError(key.c_str(), lua_tostring(B, -1));
                lua_pop(B, 1);
            }
        }
        for (const KeyBindCommand& command : commands) {
            // COMMAND_Executed: the client handlers and onClientConsole here, then the line goes
            // to the server through ActiveVmTakeServerCommands.
            ProcessClientCommand(command.command, command.arguments);
            if (g_serverCommands.size() < 64) {
                g_serverCommands.push_back(command.arguments.empty() ? command.command
                                                                      : command.command + " " + command.arguments);
            }
        }
        // CKeyBinds::ProcessKeyStroke also calls the server's key functions: the press goes to the server as KEY_BIND.
        KeyBindsServerFire(key, down);
    }
}

bool SoundElementAlive(uint32_t id) { return AudioSoundAlive(id); }

static void PumpEndedSounds() {
    if (!g_dx.L) return;
    // Update 3D spatial sound attenuation & stereo panning from camera view matrix or local player pose.
    float lx = 0.0f, ly = 0.0f, lz = 0.0f;
    float rx = 1.0f, ry = 0.0f, rz = 0.0f;
    float viewM[16];
    if (Game::CopyCameraViewMatrix(viewM)) {
        // Row-major view matrix: camera right vector is column 0; camera world pos is -t * R^T.
        rx = viewM[0];
        ry = viewM[4];
        rz = viewM[8];
        lx = -(viewM[12] * viewM[0] + viewM[13] * viewM[1] + viewM[14] * viewM[2]);
        ly = -(viewM[12] * viewM[4] + viewM[13] * viewM[5] + viewM[14] * viewM[6]);
        lz = -(viewM[12] * viewM[8] + viewM[13] * viewM[9] + viewM[14] * viewM[10]);
    } else {
        const uint32_t lp = WorldRPG::ServerWorld::LocalPlayer();
        if (lp != 0 && lp != 0xFFFFFFFFu) {
            WorldRPG::ServerWorld::ElementPose(lp, lx, ly, lz);
        }
    }
    AudioUpdateSpatial3D(lx, ly, lz, rx, ry, rz);

    for (const auto& ev : AudioCollectEvents()) {
        lua_getglobal(g_dx.L, "source");
        PushElement(g_dx.L, ev.id);
        lua_setglobal(g_dx.L, "source");
        if (ev.kind == SoundLifecycleEvent::Kind::Started) {
            lua_pushstring(g_dx.L, ev.reasonOrName.c_str());
            fireHandlers("onClientSoundStarted", 1, ev.id);
        } else if (ev.kind == SoundLifecycleEvent::Kind::Stream) {
            lua_pushboolean(g_dx.L, ev.success ? 1 : 0);
            lua_pushnumber(g_dx.L, ev.length);
            lua_pushstring(g_dx.L, ev.reasonOrName.c_str());
            if (!ev.success && !ev.errorMessage.empty()) {
                lua_pushstring(g_dx.L, ev.errorMessage.c_str());
                fireHandlers("onClientSoundStream", 4, ev.id);
            } else {
                fireHandlers("onClientSoundStream", 3, ev.id);
            }
        } else if (ev.kind == SoundLifecycleEvent::Kind::ChangedMeta) {
            lua_pushstring(g_dx.L, ev.reasonOrName.c_str());
            fireHandlers("onClientSoundChangedMeta", 1, ev.id);
        } else if (ev.kind == SoundLifecycleEvent::Kind::FinishedDownload) {
            lua_pushnumber(g_dx.L, ev.length);
            fireHandlers("onClientSoundFinishedDownload", 1, ev.id);
        } else if (ev.kind == SoundLifecycleEvent::Kind::Beat) {
            lua_pushnumber(g_dx.L, ev.length);
            fireHandlers("onClientSoundBeat", 1, ev.id);
        } else {
            lua_pushstring(g_dx.L, ev.reasonOrName.empty() ? "finished" : ev.reasonOrName.c_str());
            fireHandlers("onClientSoundStopped", 1, ev.id);
        }
        lua_setglobal(g_dx.L, "source");
    }
}

static int l_addEventHandler(lua_State* L) {
    // CLuaFunctionDefs::AddEventHandler (lua/CLuaFunctionDefs.Event.cpp 45-105).
    // Priority is not stored. getPropagated defaults to true.
    const char* name = lua_tostring(L, 1);
    const uint32_t element = ElementIdOf(L, 2);
    if (!name || element == 0 || !lua_isfunction(L, 3)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // CStaticFunctionDefinitions::AddEventHandler (CStaticFunctionDefinitions.cpp:129-143):
    // the event must be built in or added with addEvent.
    if (!Events::Exists(name)) {
        LUA_LOGI("WARNING: addEventHandler: event '%s' is not added (addEvent)", name);
        lua_pushboolean(L, 0);
        return 1;
    }
    bool propagated = true;
    if (!lua_isnoneornil(L, 4)) {
        if (!lua_isboolean(L, 4)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        propagated = lua_toboolean(L, 4) != 0;
    }
    auto& list = g_dx.handlers[name];
    for (const DxRuntime::Handler& handler : list) {
        if (handler.element != element) continue;
        Vms::PushRef(L, handler.ref);
        const bool same = lua_rawequal(L, -1, 3) != 0;
        lua_pop(L, 1);
        if (!same) continue;
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushvalue(L, 3);
    const int ref = Vms::Ref(L);
    DxRuntime::Handler added{ref, element, propagated, g_dx.ownerNetId, g_dx.ownerGeneration};
    list.push_back(added);
    g_dx.handlerOwner[ref] = g_dx.owner;
    if (std::strcmp(name, "onClientChatMessage") == 0) g_dx.chatHooked = true;
    lua_pushboolean(L, 1);
    return 1;
}

static int l_getEventHandlers(lua_State* L) {
    // CLuaFunctionDefs::GetEventHandlers (lua/CLuaFunctionDefs.Event.cpp 142-171).
    const char* name = lua_tostring(L, 1);
    const uint32_t element = ElementIdOf(L, 2);
    if (!name || element == 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_newtable(L);
    const auto found = g_dx.handlers.find(name);
    if (found == g_dx.handlers.end()) return 1;
    int index = 0;
    for (const DxRuntime::Handler& handler : found->second) {
        if (handler.element != element) continue;
        // Functions of the other VM cannot be returned here (desktop: each
        // resource VM only sees its own).
        if (Vms::RefVm(handler.ref) != Vms::MainOf(L)) continue;
        lua_pushinteger(L, ++index);
        Vms::PushRef(L, handler.ref);
        lua_settable(L, -3);
    }
    return 1;
}

static int l_triggerEvent(lua_State* L) {
    // CLuaFunctionDefs::TriggerEvent (lua/CLuaFunctionDefs.Event.cpp 174-201).
    const char* name = lua_tostring(L, 1);
    const uint32_t id = ElementIdOf(L, 2);
    // CStaticFunctionDefinitions::TriggerEvent (CStaticFunctionDefinitions.cpp:162-174): nil when
    // no such event exists.
    if (!name || id == 0 || !Events::Exists(name)) {
        lua_pushnil(L);
        return 1;
    }
    const int top = lua_gettop(L);
    const int nargs = top - 2;
    lua_getglobal(L, "source");
    PushElement(L, id);
    lua_setglobal(L, "source");
    for (int i = 3; i <= top; ++i) lua_pushvalue(L, i);
    const bool ran = fireHandlers(name, nargs, id, L, true);  // CStaticFunctionDefinitions.cpp:168
    lua_setglobal(L, "source");
    lua_pushboolean(L, ran ? 1 : 0);
    return 1;
}

// Desktop ReadNumber accepts numbers and numeric strings. The Canvas bridge
// requires finite floats so one bad call cannot poison a draw command.
static bool ReadDrawNumber(lua_State* L, int index, double& value) {
    if (!lua_isnumber(L, index)) return false;
    value = lua_tonumber(L, index);
    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
}
static bool ReadDrawOptional(lua_State* L, int index, double fallback, double& value) {
    if (lua_isnoneornil(L, index)) { value = fallback; return true; }
    return ReadDrawNumber(L, index, value);
}
static bool ReadDrawBool(lua_State* L, int index, bool& value) {
    value = false;
    if (lua_isnoneornil(L, index)) return true;
    if (!lua_isboolean(L, index)) return false;
    value = lua_toboolean(L, index) != 0;
    return true;
}
static unsigned DrawColor(double value) {
    double wrapped = std::fmod(std::trunc(value), 4294967296.0);
    if (wrapped < 0) wrapped += 4294967296.0;
    return static_cast<unsigned>(wrapped);
}
static int DrawResult(lua_State* L, bool success) { lua_pushboolean(L, success); return 1; }

// CLuaDrawingDefs.cpp:412-441; CGraphics.cpp:975-991.
static int l_dxDrawRectangle(lua_State* L) {
    double x, y, w, h, color;
    bool postGUI, subPixel;
    if (!ReadDrawNumber(L,1,x) || !ReadDrawNumber(L,2,y) || !ReadDrawNumber(L,3,w) || !ReadDrawNumber(L,4,h) ||
        !ReadDrawOptional(L,5,4294967295.0,color) || !ReadDrawBool(L,6,postGUI) || !ReadDrawBool(L,7,subPixel))
        return DrawResult(L,false);
    char line[256];
    std::snprintf(line, sizeof(line), "R %.1f %.1f %.1f %.1f %u\n",
        x, y, w, h, DrawColor(color));
    DxAppendDraw(line,postGUI);
    return DrawResult(L,true);
}

static int ReadDrawAlign(lua_State* L, int index, const char* first, const char* last) {
    if (lua_isnoneornil(L,index)) return 0;
    if (lua_type(L,index) != LUA_TSTRING) return -1;
    const char* value = lua_tostring(L,index);
    if (std::strcmp(value,first) == 0) return 0;
    if (std::strcmp(value,"center") == 0) return 1;
    return std::strcmp(value,last) == 0 ? 2 : -1;
}

static std::string EncodeDrawText(const char* text, size_t length) {
    std::string encoded;
    encoded.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        switch (text[i]) {
            case '\\': encoded += "\\\\"; break;
            case '\n': encoded += "\\n"; break;
            case '\r': encoded += "\\r"; break;
            case '\t': encoded += "\\t"; break;
            default:
                encoded += static_cast<unsigned char>(text[i]) < 32 ? ' ' : text[i];
        }
    }
    return encoded;
}

// CLuaDrawingDefs.cpp:322-410; CGraphics.cpp:1230-1345. X is the DX text
// command with layout options; GUI keeps its existing T command format.
static int l_dxDrawText(lua_State* L) {
    double left, top, right, bottom, color, scaleX, scaleY, rotation, pivotX, pivotY, lineHeight;
    bool postGUI, colorCoded, clip, wordBreak, subPixel;
    if (!lua_isstring(L,1) || !ReadDrawNumber(L,2,left) || !ReadDrawNumber(L,3,top) ||
        !ReadDrawOptional(L,4,left,right) || !ReadDrawOptional(L,5,top,bottom) ||
        !ReadDrawOptional(L,6,4294967295.0,color) || !ReadDrawOptional(L,7,1,scaleX)) return DrawResult(L,false);
    scaleY = scaleX;
    int fontIndex = 8;
    // Desktop permits a second numeric scale before the font argument.
    if (lua_type(L,fontIndex) == LUA_TNUMBER) {
        if (!ReadDrawNumber(L,fontIndex++,scaleY)) return DrawResult(L,false);
    }
    if (lua_istable(L,fontIndex)) {
        const char* kind = DxElementType(ElementIdOf(L,fontIndex));
        if (!kind || std::strcmp(kind,"dx-font") != 0) return DrawResult(L,false);
    } else if (!lua_isnoneornil(L,fontIndex)) {
        if (lua_type(L,fontIndex) != LUA_TSTRING) return DrawResult(L,false);
        // MixedReadDxFontString leaves FONT_DEFAULT when an unknown name does
        // not convert. Numeric strings are font names, never the second scale.
    }
    const int alignX = ReadDrawAlign(L,fontIndex+1,"left","right");
    const int alignY = ReadDrawAlign(L,fontIndex+2,"top","bottom");
    if (alignX < 0 || alignY < 0 || !ReadDrawBool(L,fontIndex+3,clip) || !ReadDrawBool(L,fontIndex+4,wordBreak) ||
        !ReadDrawBool(L,fontIndex+5,postGUI) || !ReadDrawBool(L,fontIndex+6,colorCoded) || !ReadDrawBool(L,fontIndex+7,subPixel) ||
        !ReadDrawOptional(L,fontIndex+8,0,rotation) || !ReadDrawOptional(L,fontIndex+9,(left+right)*0.5,pivotX) ||
        !ReadDrawOptional(L,fontIndex+10,(top+bottom)*0.5,pivotY) || !ReadDrawOptional(L,fontIndex+11,0,lineHeight))
        return DrawResult(L,false);
    size_t length = 0;
    const char* text = lua_tolstring(L,1,&length);
    const std::string safe = EncodeDrawText(text,length);
    // The renderer draws scale * 15 px, so a font taller than the default is scaled here.
    const char* font = lua_type(L, fontIndex) == LUA_TSTRING ? lua_tostring(L, fontIndex) : "default";
    // A dx-font element (dxCreateFont): "F height bold file" selects it for the next text line,
    // and a bare "F" returns to the default font. The text scale then multiplies the font height.
    const DxFont custom = DxResolveFont(L, fontIndex, 1.0);
    if (std::abs(scaleX * custom.pixels) > std::numeric_limits<float>::max() ||
        std::abs(scaleY * custom.pixels) > std::numeric_limits<float>::max()) return DrawResult(L,false);
    std::string commands;
    if (!custom.file.empty()) {
        char select[64];
        std::snprintf(select, sizeof(select), "F %.1f %d ", custom.pixels, custom.bold ? 1 : 0);
        commands += select;
        commands += custom.file;
        commands += '\n';
        font = "default";
    }
    char head[1024];
    std::snprintf(head, sizeof(head), "X %.3f %.3f %.3f %.3f %.4f %.4f %u %d %d %d %d %d %d %.3f %.3f %.3f %.3f ",
        left, top, right, bottom, scaleX * DxFontScale(font), scaleY * DxFontScale(font), DrawColor(color),
        colorCoded ? 1 : 0, alignX, alignY, clip ? 1 : 0, wordBreak ? 1 : 0, subPixel ? 1 : 0,
        rotation,pivotX,pivotY,lineHeight);
    commands += head;
    commands += safe;
    commands += '\n';
    if (!custom.file.empty()) commands += "F\n";
    DxAppendDraw(commands,postGUI);
    return DrawResult(L,true);
}

// Resource file for a dxDrawImage path. "radar/a.png" is the running resource;
// ":other/radar/a.png" names another resource. ".." is rejected. The result is
// HttpDownloader::CachedFilePath, which is empty until a cache root is set.
static std::string DxImageFile(const std::string& raw) {
    if (g_dx.owner.empty() || raw.empty()) return {};
    std::string path;
    path.reserve(raw.size());
    for (char ch : raw) path.push_back(ch == '\\' ? '/' : ch);
    std::string resource = g_dx.owner;
    if (path[0] == ':') {
        const auto slash = path.find('/');
        if (slash == std::string::npos || slash < 2) return {};
        resource = path.substr(1, slash - 1);
        path = path.substr(slash + 1);
    }
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    if (resource.empty() || path.empty()) return {};
    bool any = false;
    for (size_t i = 0; i < path.size();) {
        const auto slash = path.find('/', i);
        const auto end = slash == std::string::npos ? path.size() : slash;
        const std::string part = path.substr(i, end - i);
        if (part == "..") return {};
        if (!part.empty() && part != ".") any = true;
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    if (!any) return {};
    for (unsigned char ch : resource)
        if (ch < 32 || ch >= 127 || ch == '/') return {};
    for (unsigned char ch : path)
        if (ch < 32 || ch >= 127) return {};
    return WorldRPG::Net::HttpDownloader::CachedFilePath(resource, path);
}

// Desktop CLuaDrawingDefs::DxDrawImage / DxDrawImageSection. One line for MtaDxView:
//   I dx dy dw dh sx sy sw sh color rot ox oy <cache path>
// dxDrawImage uses sx=sy=0 and sw=sh=-1 (the whole PNG). File paths need a
// running resource; an existing texture element is independent of the caller.
static int AppendImageDraw(lua_State* L, bool section) {
    const int pathIndex = section ? 9 : 5;
    // A texture element (dxCreateTexture) or a file path (desktop MixedReadMaterialString).
    std::string file;
    const uint32_t shader=lua_istable(L,pathIndex)?ElementIdOf(L,pathIndex):0;
    if (lua_istable(L, pathIndex)) {
        if (!WorldRPG::DxShader::Exists(shader) && !DxTextureFile(ElementIdOf(L, pathIndex), file)) {
            lua_pushboolean(L, 0);
            return 1;
        }
    } else if (lua_type(L, pathIndex) == LUA_TSTRING) {
        size_t pathLength = 0;
        const char* path = lua_tolstring(L, pathIndex, &pathLength);
        file = DxImageFile(std::string(path ? path : "", pathLength));
    }
    if (file.empty() && !WorldRPG::DxShader::Exists(shader)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int rotIndex = pathIndex + 1;
    double x, y, w, h, sx = 0, sy = 0, sw = -1, sh = -1, color, rotation, ox, oy;
    bool postGUI;
    if (!ReadDrawNumber(L,1,x) || !ReadDrawNumber(L,2,y) || !ReadDrawNumber(L,3,w) || !ReadDrawNumber(L,4,h) ||
        (section && (!ReadDrawNumber(L,5,sx) || !ReadDrawNumber(L,6,sy) || !ReadDrawNumber(L,7,sw) || !ReadDrawNumber(L,8,sh))) ||
        !ReadDrawOptional(L,rotIndex,0,rotation) || !ReadDrawOptional(L,rotIndex+1,0,ox) ||
        !ReadDrawOptional(L,rotIndex+2,0,oy) || !ReadDrawOptional(L,rotIndex+3,4294967295.0,color) ||
        !ReadDrawBool(L,rotIndex+4,postGUI)) return DrawResult(L,false);
    if (WorldRPG::DxShader::Exists(shader)) {
        WorldRPG::DxShader::Tile tile;
        tile.x=x;tile.y=y;tile.width=w;tile.height=h;tile.rotation=rotation;
        tile.offsetX=ox;tile.offsetY=oy;tile.color=DrawColor(color);
        if(section){tile.u=sx;tile.v=sy;tile.usize=sw;tile.vsize=sh;}
        return DrawResult(L,WorldRPG::DxShader::DrawImage(shader,tile,section,postGUI));
    }
    char head[768];
    std::snprintf(head, sizeof(head), "I %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %u %.1f %.1f %.1f ",
                  x, y, w, h, sx, sy, sw, sh, DrawColor(color), rotation, ox, oy);
    DxAppendDraw(std::string(head) + file + '\n',postGUI);
    return DrawResult(L,true);
}

static int l_dxDrawImage(lua_State* L) { return AppendImageDraw(L, false); }
static int l_dxDrawImageSection(lua_State* L) { return AppendImageDraw(L, true); }

static int l_removeEventHandler(lua_State* L) {
    // CLuaFunctionDefs::RemoveEventHandler (lua/CLuaFunctionDefs.Event.cpp 107-140).
    const char* name = lua_tostring(L, 1);
    const uint32_t element = ElementIdOf(L, 2);
    if (!name || element == 0 || !lua_isfunction(L, 3)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    auto found = g_dx.handlers.find(name);
    if (found == g_dx.handlers.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    auto& refs = found->second;
    for (auto it = refs.begin(); it != refs.end(); ++it) {
        if (it->element != element) continue;
        Vms::PushRef(L, it->ref);
        const bool same = lua_rawequal(L, -1, 3) != 0;
        lua_pop(L, 1);
        if (!same) continue;
        Vms::Unref(it->ref);
        g_dx.handlerOwner.erase(it->ref);
        refs.erase(it);
        if (std::strcmp(name, "onClientChatMessage") == 0) g_dx.chatHooked = !refs.empty();
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int l_tocolor(lua_State* L) {
    // CLuaUtilDefs::tocolor accepts fractional channels and optional alpha.
    uint32_t channels[] = {0, 0, 0, 255};
    for (int i = 0; i < 4; ++i) {
        if (i == 3 && lua_isnoneornil(L, 4)) break;
        int numeric = 0;
        const lua_Number value = lua_tonumberx(L, i + 1, &numeric);
        if (!numeric || std::isnan(value)) {
            lua_pushinteger(L, 0xff000000u);
            return 1;
        }
        channels[i] = static_cast<uint32_t>(std::clamp(value, lua_Number(0), lua_Number(255)));
    }
    lua_pushinteger(L, channels[3] << 24 | channels[0] << 16 | channels[1] << 8 | channels[2]);
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

static int l_setCursorPosition(lua_State* L) {
    // CLuaFunctionDefs::SetCursorPosition writes absolute pixels. The phone keeps that point for getCursorPosition.
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cursorX = static_cast<float>(lua_tonumber(L, 1));
    g_cursorY = static_cast<float>(lua_tonumber(L, 2));
    lua_pushboolean(L, 1);
    return 1;
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

static int l_getMobileCrosshairPosition(lua_State* L) {
    float x = 0.f, y = 0.f;
    if (!WorldRPG::Game::GetMobileCrosshairPosition(x, y)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
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
    timer.ref = Vms::Ref(L);
    timer.times = times == 0 ? -1 : times;
    timer.interval = interval;
    timer.next = DxNow() + static_cast<std::uint64_t>(interval);
    timer.left = static_cast<std::uint64_t>(interval);
    timer.owner = g_dx.owner;
    timer.netId = g_dx.ownerNetId;
    timer.generation = g_dx.ownerGeneration;
    g_dx.timers.push_back(timer);
    lua_pushinteger(L, timer.ref);
    return 1;
}

// CLuaTimerDefs::IsTimer (209-224). The phone's setTimer returns the registry ref,
// which is the handle these two functions accept.
static DxRuntime::Timer* FindTimer(int ref) {
    for (auto& timer : g_dx.timers)
        if (timer.ref == ref && timer.times != 0) return &timer;
    return nullptr;
}

static int l_isTimer(lua_State* L) {
    if (!lua_isinteger(L, 1) && !lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, FindTimer(static_cast<int>(lua_tointeger(L, 1))) ? 1 : 0);
    return 1;
}

static int l_killTimer(lua_State* L) {
    // CLuaTimerDefs::KillTimer (95-118). Removing the timer also drops its callback.
    if (!lua_isinteger(L, 1) && !lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int ref = static_cast<int>(lua_tointeger(L, 1));
    DxRuntime::Timer* timer = FindTimer(ref);
    if (!timer) {
        lua_pushboolean(L, 0);
        return 1;
    }
    timer->times = 0;
    Vms::Unref(ref);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_resetTimer(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    DxRuntime::Timer* timer = FindTimer(static_cast<int>(lua_tointeger(L, 1)));
    if (!timer) {
        lua_pushboolean(L, 0);
        return 1;
    }
    timer->left = static_cast<std::uint64_t>(timer->interval);
    timer->next = DxNow() + timer->left;
    lua_pushboolean(L, 1);
    return 1;
}

static int l_setTimerPaused(lua_State* L) {
    if (!lua_isnumber(L, 1) || lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    DxRuntime::Timer* timer = FindTimer(static_cast<int>(lua_tointeger(L, 1)));
    if (!timer) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool pause = lua_toboolean(L, 2) != 0;
    const auto now = DxNow();
    if (pause && !timer->paused)
        timer->left = timer->next > now ? timer->next - now : 0;
    else if (!pause && timer->paused)
        timer->next = now + timer->left;
    timer->paused = pause;
    lua_pushboolean(L, 1);
    return 1;
}

static int l_isTimerPaused(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    DxRuntime::Timer* timer = FindTimer(static_cast<int>(lua_tointeger(L, 1)));
    if (!timer) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, timer->paused ? 1 : 0);
    return 1;
}

static int l_getTimerDetails(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    DxRuntime::Timer* timer = FindTimer(static_cast<int>(lua_tointeger(L, 1)));
    if (!timer) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto now = DxNow();
    const auto left = timer->paused ? timer->left : (timer->next > now ? timer->next - now : 0);
    lua_pushinteger(L, static_cast<lua_Integer>(left));
    lua_pushinteger(L, timer->times < 0 ? 0 : timer->times);
    lua_pushinteger(L, timer->interval);
    return 3;
}

static int l_getTimers(lua_State* L) {
    double limit = 0;
    if (!lua_isnoneornil(L, 1)) {
        if (!lua_isnumber(L, 1)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        limit = lua_tonumber(L, 1);
    }
    const auto now = DxNow();
    lua_newtable(L);
    int index = 0;
    for (const auto& timer : g_dx.timers) {
        if (timer.times == 0) continue;
        const auto left = timer.paused ? timer.left : (timer.next > now ? timer.next - now : 0);
        if (limit != 0 && static_cast<double>(left) > limit) continue;
        lua_pushinteger(L, ++index);
        lua_pushinteger(L, timer.ref);
        lua_settable(L, -3);
    }
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

// Missing element or the local player. Remote bodies are not read here.
static bool IsLocalStatElement(lua_State* L, int index) {
    const uint32_t id = ElementIdOf(L, index);
    return id == 0 || id == ServerWorld::LocalPlayer();
}

// Desktop CStaticFunctionDefinitions::GetElementVelocity (466-516).
static int l_getElementVelocity(lua_State* L) {
    float x = 0, y = 0, z = 0, heading = 0, vx = 0, vy = 0, vz = 0, health = 0;
    const uint32_t id = ElementIdOf(L, 1);
    Game::HostedCar traffic;
    if (Game::CopyTrafficVehicleState(id, traffic)) {
        lua_pushnumber(L, traffic.vx);
        lua_pushnumber(L, traffic.vy);
        lua_pushnumber(L, traffic.vz);
        return 3;
    }
    if (WorldRPG::Script::CopyScriptMoveSpeed(id == 0 ? ServerWorld::LocalPlayer() : id, vx, vy, vz)) {
        lua_pushnumber(L, vx);
        lua_pushnumber(L, vy);
        lua_pushnumber(L, vz);
        return 3;
    }
    Game::LocalCarState car;
    if (IsLocalStatElement(L, 1)) {
        Game::CopyBody(x, y, z, heading, vx, vy, vz, health);
    } else if (Game::CopyLocalCar(car) && car.vehicleId == id) {
        vx = car.vx;
        vy = car.vy;
        vz = car.vz;
    } else {
        Game::CopyRemotePedMotion(id, heading, vx, vy, vz);
    }
    lua_pushnumber(L, vx);
    lua_pushnumber(L, vy);
    lua_pushnumber(L, vz);
    return 3;
}

// Desktop CLuaPedDefs::GetPedRotation: ConvertRadiansToDegrees(GetCurrentRotation()),
// wrapped to [0, 360). The phone uses FindPlayerHeading (forward row), not the
// pure-sync sample. 0 is north, 90 is west. The square radar draws arrow.png
// with -heading so a clockwise dxDrawImage points the way the ped faces.
static int l_getPedRotation(lua_State* L) {
    auto wrapDegrees = [](double radians) {
        const double turns = radians * (180.0 / 3.14159265358979323846) + 360.0;
        return turns - 360.0 * std::floor(turns / 360.0);
    };
    double degrees = 0;
    if (IsLocalStatElement(L, 1)) {
        degrees = wrapDegrees(static_cast<double>(Game::CopyRadarHeading()));
    } else if (const uint32_t id = ElementIdOf(L, 1); id != 0) {
        int model = 0;
        float radians = 0.f, vx = 0.f, vy = 0.f, vz = 0.f;
        bool frozen = false;
        ServerWorld::ActorSpawn visual;
        if (Game::CopyScriptPedFacts(id, model, radians, frozen) ||
            Game::CopyRemotePedMotion(id, radians, vx, vy, vz)) {
            degrees = wrapDegrees(static_cast<double>(radians));
        } else if (ServerWorld::CopyVisual(id, visual) && (visual.kind == 1 || visual.kind == 14)) {
            degrees = wrapDegrees(static_cast<double>(visual.heading));
        }
    }
    lua_pushnumber(L, degrees);
    return 1;
}

// Desktop getPedArmor for the local ped. The value is sampled in SampleLocalPose.
static int l_getPedArmor(lua_State* L) {
    lua_pushnumber(L, IsLocalStatElement(L, 1) ? Game::CopyLocalArmor() : 0.0);
    return 1;
}

static const char* GroundLuaTypeName(int type) {
    switch (type) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TNUMBER: return "number";
        case LUA_TSTRING: return "string";
        case LUA_TTABLE: return "table";
        case LUA_TFUNCTION: return "function";
        case LUA_TUSERDATA: return "userdata";
        case LUA_TLIGHTUSERDATA: return "light userdata";
        case LUA_TTHREAD: return "thread";
        default: return "none";
    }
}

// CScriptArgReader::SetTypeError / GetFullErrorMessage (CScriptArgReader.h 1468-1529, 1589).
static void GroundTypeError(char* buf, size_t n, const char* expected, int index, lua_State* L) {
    const char* got = GroundLuaTypeName(lua_type(L, index));
    const char* value = lua_tostring(L, index);
    if (value && value[0] != '\0') {
        std::snprintf(buf, n, "Bad argument @ 'getGroundPosition' [Expected %s at argument %d, got %s '%s']",
                      expected, index, got, value);
    } else {
        std::snprintf(buf, n, "Bad argument @ 'getGroundPosition' [Expected %s at argument %d, got %s]",
                      expected, index, got);
    }
}

// CScriptArgReader::ReadVector3D (CScriptArgReader.h 308-350) then ReadNumber (56-97).
// Desktop accepts a number/string triple or vector userdata. A table is an error.
static bool ReadGroundVector(lua_State* L, float& x, float& y, float& z, char* err, size_t errN) {
    const int first = lua_type(L, 1);
    if (first != LUA_TNUMBER && first != LUA_TSTRING) {
        GroundTypeError(err, errN, "vector3", 1, L);
        return false;
    }
    float* coords[3] = {&x, &y, &z};
    for (int i = 0; i < 3; ++i) {
        const int index = i + 1;
        const int type = lua_type(L, index);
        if (type != LUA_TNUMBER && type != LUA_TSTRING) {
            GroundTypeError(err, errN, "number", index, L);
            return false;
        }
        if (!lua_isnumber(L, index)) {
            std::snprintf(err, errN, "Bad argument @ 'getGroundPosition' [Expected number, got non-convertible string]");
            return false;
        }
        const lua_Number number = lua_tonumber(L, index);
        if (std::isnan(number)) {
            std::snprintf(err, errN, "Bad argument @ 'getGroundPosition' [Expected number, got NaN]");
            return false;
        }
        *coords[i] = static_cast<float>(number);
    }
    return true;
}

// CLuaWorldDefs::GetGroundPosition (CLuaWorldDefs.cpp 872-893). The world query is
// CWorldSA::FindGroundZFor3DPosition (CWorldSA.cpp 708-729), run on the game thread.
static int l_getGroundPosition(lua_State* L) {
    float x = 0, y = 0, z = 0;
    char err[256];
    if (!ReadGroundVector(L, x, y, z, err, sizeof(err))) {
        LUA_LOGI("WARNING: %s", err);
        lua_pushboolean(L, 0);
        return 1;
    }
    float ground = 0;
    if (!Game::FindGroundZFor3DPosition(x, y, z, ground)) {
        // Symbol missing or the game thread did not run the call (DIAG is in
        // FindGroundZFor3DPosition). False matches this function's error result.
        // native-ped-traffic client.lua 42-43 then keeps the player's Z.
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(ground));
    return 1;
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
    w.WriteCompressed(value, bytes);
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

struct LatentTransferEntry {
    uint32_t handle = 0;
    std::chrono::steady_clock::time_point startTime{};
    uint32_t bandwidth = 5000;
    uint32_t totalSize = 0;
    uint32_t estDurationMs = 0;
    bool cancelled = false;
};

static std::mutex g_latentMutex;
static uint32_t g_nextLatentHandle = 1;
static std::vector<LatentTransferEntry> g_latentTransfers;

// CLuaFunctionDefs.Event.cpp:291-338 and CStaticFunctionDefinitions.cpp:204-233.
// triggerLatentServerEvent ( string event, [int bandwidth=5000,] [bool persist=false,] element theElement, [arguments...] )
static int l_triggerLatentServerEvent(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushboolean(L, 0);
        return 1;
    }
    size_t nameLength = 0;
    const char* name = lua_tolstring(L, 1, &nameLength);
    if (!name || nameLength == 0 || nameLength > 256) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int argIdx = 2;
    int bandwidth = 5000;
    if (lua_type(L, argIdx) == LUA_TNUMBER) {
        bandwidth = static_cast<int>(lua_tointeger(L, argIdx));
        ++argIdx;
    }
    if (bandwidth < 500) bandwidth = 500;
    bool persist = false;
    if (lua_type(L, argIdx) == LUA_TBOOLEAN) {
        persist = lua_toboolean(L, argIdx) != 0;
        ++argIdx;
    }
    (void)persist;
    uint32_t targetId = ElementIdOf(L, argIdx);
    if (targetId == 0) targetId = ServerWorld::LastResourceRoot();
    ++argIdx;

    Net::BitStreamWriter packet;
    packet.Write(static_cast<uint8_t>(81));
    WriteCompressed(packet, static_cast<uint32_t>(nameLength), 2);
    packet.WriteBytes(reinterpret_cast<const uint8_t*>(name), nameLength);
    WriteBits(packet, targetId & 0x1FFFFu, 17);
    const int top = lua_gettop(L);
    const int args = std::max(0, top - argIdx + 1);
    WriteCompressed(packet, static_cast<uint32_t>(args), 4);
    for (int i = argIdx; i <= top; ++i) WriteLuaValue(L, i, packet, 0);

    if (!WorldRPG::Debug::Flag("notrigger")) {
        Net::SendGamePacket(packet, true);
    }

    const uint32_t totalSize = static_cast<uint32_t>(packet.GetByteLength() > 0 ? packet.GetByteLength() : 64);
    uint32_t estDurationMs = static_cast<uint32_t>((static_cast<uint64_t>(totalSize) * 1000ULL) / static_cast<uint64_t>(bandwidth));
    if (estDurationMs < 50) estDurationMs = 50;

    {
        std::lock_guard<std::mutex> lock(g_latentMutex);
        LatentTransferEntry entry;
        entry.handle = g_nextLatentHandle++;
        entry.startTime = std::chrono::steady_clock::now();
        entry.bandwidth = static_cast<uint32_t>(bandwidth);
        entry.totalSize = totalSize;
        entry.estDurationMs = estDurationMs;
        entry.cancelled = false;
        g_latentTransfers.push_back(entry);
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaFunctionDefs.Event.cpp:340-365.
// table getLatentEventHandles ()
static int l_getLatentEventHandles(lua_State* L) {
    std::lock_guard<std::mutex> lock(g_latentMutex);
    const auto now = std::chrono::steady_clock::now();
    std::vector<uint32_t> active;
    for (auto it = g_latentTransfers.begin(); it != g_latentTransfers.end();) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->startTime).count();
        if (it->cancelled || elapsed > static_cast<int64_t>(it->estDurationMs + 10000)) {
            it = g_latentTransfers.erase(it);
            continue;
        }
        if (elapsed <= static_cast<int64_t>(it->estDurationMs + 200)) {
            active.push_back(it->handle);
        }
        ++it;
    }
    lua_createtable(L, static_cast<int>(active.size()), 0);
    for (size_t i = 0; i < active.size(); ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(active[i]));
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

// CLuaFunctionDefs.Event.cpp:367-406.
// table getLatentEventStatus ( int handle )
static int l_getLatentEventStatus(lua_State* L) {
    if (lua_type(L, 1) != LUA_TNUMBER) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
    std::lock_guard<std::mutex> lock(g_latentMutex);
    const auto now = std::chrono::steady_clock::now();
    for (const auto& tx : g_latentTransfers) {
        if (tx.handle == handle && !tx.cancelled) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - tx.startTime).count();
            if (elapsed > static_cast<int64_t>(tx.estDurationMs + 500)) {
                lua_pushboolean(L, 0);
                return 1;
            }
            double pct = tx.estDurationMs > 0 ? static_cast<double>(elapsed) / static_cast<double>(tx.estDurationMs) : 1.0;
            if (pct < 0.0) pct = 0.0;
            if (pct > 1.0) pct = 1.0;
            lua_createtable(L, 0, 4);
            lua_pushinteger(L, static_cast<lua_Integer>(-elapsed));
            lua_setfield(L, -2, "tickStart");
            lua_pushinteger(L, static_cast<lua_Integer>(std::max<int64_t>(0, static_cast<int64_t>(tx.estDurationMs) - elapsed)));
            lua_setfield(L, -2, "tickEnd");
            lua_pushinteger(L, static_cast<lua_Integer>(tx.totalSize));
            lua_setfield(L, -2, "totalSize");
            lua_pushnumber(L, pct);
            lua_setfield(L, -2, "percentComplete");
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaFunctionDefs.Event.cpp:408-430.
// bool cancelLatentEvent ( int handle )
static int l_cancelLatentEvent(lua_State* L) {
    if (lua_type(L, 1) != LUA_TNUMBER) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
    std::lock_guard<std::mutex> lock(g_latentMutex);
    for (auto& tx : g_latentTransfers) {
        if (tx.handle == handle && !tx.cancelled) {
            tx.cancelled = true;
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int l_getNeonTrafficCapability(lua_State* L) {
    lua_pushboolean(L, Game::HasNativeTrafficCapability(luaL_checkstring(L, 1)));
    return 1;
}

static int l_setPedWander(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const char* movement = luaL_optstring(L, 2, "walk");
    const int direction = luaL_optinteger(L, 3, -1);
    const int moveState = std::strcmp(movement, "walk") == 0 ? 4 : std::strcmp(movement, "run") == 0 ? 6 : -1;
    const bool sensible = lua_isnoneornil(L, 4) || lua_toboolean(L, 4);
    const bool valid = moveState >= 0 && direction >= -1 && direction <= 7 && id != 0;
    lua_pushboolean(L, valid && Game::StartPedWander(id, moveState, direction, sensible));
    return 1;
}

static int l_isElement(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    float x = 0, y = 0, z = 0;
    const bool known = id != 0 && (id == ServerWorld::LocalPlayer() || id == ServerWorld::LastResourceRoot() ||
                                    ServerWorld::IsKnownPlayer(id) || GuiIsElement(id) ||
                                    Game::ScriptPedAlive(id) || SoundElementAlive(id) ||
                                    ServerWorld::ElementPose(id, x, y, z) || Game::CopyRemotePose(id, x, y, z));
    lua_pushboolean(L, known ? 1 : 0);
    return 1;
}

static int l_isElementStreamedIn(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const char* kind = ServerWorld::ElementKindName(id);
    if (std::strcmp(kind, "ped") != 0 && std::strcmp(kind, "vehicle") != 0 &&
        std::strcmp(kind, "player") != 0 && !Game::ScriptPedAlive(id)) return l_isElement(L);
    lua_pushboolean(L, Game::IsNativeActorStreamed(id));
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
    if (Game::ScriptPedAlive(id)) {
        lua_pushstring(L, "ped");
        return 1;
    }
    if (SoundElementAlive(id)) {
        lua_pushstring(L, "sound");
        return 1;
    }
    lua_pushstring(L, id == 0 ? "userdata" : ServerWorld::ElementKindName(id));
    return 1;
}

static bool CopyUnattachedElementTransform(uint32_t id, float pos[3], float rotDeg[3]) {
    pos[0] = pos[1] = pos[2] = 0.f;
    rotDeg[0] = rotDeg[1] = rotDeg[2] = 0.f;
    float heading = 0.f;
    Game::LocalCarState car;
    if (id == 0 || id == ServerWorld::LocalPlayer()) {
        Game::CopyPose(pos[0], pos[1], pos[2], heading);
        rotDeg[2] = heading;
        return true;
    }
    if (Game::CopyLocalCar(car) && car.vehicleId == id) {
        pos[0] = car.x;
        pos[1] = car.y;
        pos[2] = car.z;
        rotDeg[0] = car.rotX;
        rotDeg[1] = car.rotY;
        rotDeg[2] = car.rotZ;
        return true;
    }
    Game::HostedCar traffic;
    if (Game::CopyTrafficVehicleState(id, traffic)) {
        pos[0] = traffic.x;
        pos[1] = traffic.y;
        pos[2] = traffic.z;
        rotDeg[0] = traffic.rotX;
        rotDeg[1] = traffic.rotY;
        rotDeg[2] = traffic.rotZ;
        return true;
    }
    float objPos[3] = {}, objRotRad[3] = {}, objScale[3] = {};
    bool moving = false;
    if (CopyObjectState(id, objPos, objRotRad, objScale, moving)) {
        constexpr float kRadToDeg = 57.29577951308232f;
        pos[0] = objPos[0];
        pos[1] = objPos[1];
        pos[2] = objPos[2];
        rotDeg[0] = objRotRad[0] * kRadToDeg;
        rotDeg[1] = objRotRad[1] * kRadToDeg;
        rotDeg[2] = objRotRad[2] * kRadToDeg;
        return true;
    }
    Spatial::Element shape;
    if (CopySpatialShape(id, shape)) {
        pos[0] = shape.position.x;
        pos[1] = shape.position.y;
        pos[2] = shape.position.z;
        return true;
    }
    Projectiles::Snapshot projectile;
    if (Projectiles::Copy(id, projectile)) {
        pos[0] = projectile.position.x;
        pos[1] = projectile.position.y;
        pos[2] = projectile.position.z;
        return true;
    }
    ServerWorld::ActorSpawn vis;
    const bool hasVisual = ServerWorld::CopyVisual(id, vis);
    if (Game::CopyRemotePose(id, pos[0], pos[1], pos[2])) {
        if (hasVisual) {
            rotDeg[0] = vis.rotX;
            rotDeg[1] = vis.rotY;
            rotDeg[2] = vis.heading;
        }
        return true;
    }
    if (hasVisual) {
        pos[0] = vis.x;
        pos[1] = vis.y;
        pos[2] = vis.z;
        rotDeg[0] = vis.rotX;
        rotDeg[1] = vis.rotY;
        rotDeg[2] = vis.heading;
        return true;
    }
    return ServerWorld::ElementPose(id, pos[0], pos[1], pos[2]);
}

static int l_getElementPosition(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    float attPos[3] = {}, attRot[3] = {};
    if (id != 0 && ServerWorld::ResolveAttachedTransform(id, CopyUnattachedElementTransform, attPos, attRot)) {
        lua_pushnumber(L, attPos[0]);
        lua_pushnumber(L, attPos[1]);
        lua_pushnumber(L, attPos[2]);
        return 3;
    }
    Spatial::Element shape;
    if(CopySpatialShape(id,shape)) {
        lua_pushnumber(L,shape.position.x); lua_pushnumber(L,shape.position.y); lua_pushnumber(L,shape.position.z); return 3;
    }
    Projectiles::Snapshot projectile;
    if (Projectiles::Copy(id, projectile)) {
        lua_pushnumber(L, projectile.position.x); lua_pushnumber(L, projectile.position.y); lua_pushnumber(L, projectile.position.z);
        return 3;
    }
    float x = 0, y = 0, z = 0, heading = 0;
    Game::LocalCarState car;
    if (id == 0 || id == ServerWorld::LocalPlayer()) {
        Game::CopyPose(x, y, z, heading);
    } else if (Game::CopyLocalCar(car) && car.vehicleId == id) {
        x = car.x;
        y = car.y;
        z = car.z;
    } else if (!Game::CopyRemotePose(id, x, y, z) && !ServerWorld::ElementPose(id, x, y, z)) {
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
    // Desktop GetElementsByType (CLuaElementDefs.cpp 798-848): optional 3rd arg streamedIn.
    const bool streamedInOnly = lua_toboolean(L, 3) != 0;
    lua_newtable(L);
    int index = 1;
    if (std::strcmp(kind, "player") == 0) {
        const uint32_t localId = ServerWorld::LocalPlayer();
        if (localId != 0) {
            PushElement(L, localId);
            lua_rawseti(L, -2, index++);
        }
        uint32_t players[128];
        const int count = ServerWorld::CopyPlayers(players, 128);
        for (int i = 0; i < count; ++i) {
            if (streamedInOnly && !Game::IsNativeActorStreamed(players[i])) continue;
            PushElement(L, players[i]);
            lua_rawseti(L, -2, index++);
        }
        return 1;
    }
    // Desktop GetElementsByType (CLuaElementDefs.cpp 798-848) asks FindAllChildrenByType for
    // one type name. The phone still returns at most 96 and ignores the start-element argument.
    // The copy is that one kind, so vehicles cannot fill a "ped" query.
    int pedLimit = -1;
    int vehicleLimit = -1;
    if (std::strcmp(kind, "ped") == 0) {
        pedLimit = 96;
        vehicleLimit = 0;
    } else if (std::strcmp(kind, "vehicle") == 0) {
        pedLimit = 0;
        vehicleLimit = 96;
    }
    ServerWorld::ActorSpawn actors[96];
    const int count = ServerWorld::CopyActors(actors, 96, pedLimit, vehicleLimit);
    for (int i = 0; i < count; ++i) {
        const bool ped = actors[i].kind == 14 && std::strcmp(kind, "ped") == 0;
        const bool vehicle = actors[i].kind == 2 && std::strcmp(kind, "vehicle") == 0;
        if (!ped && !vehicle) continue;
        if (streamedInOnly && !Game::IsNativeActorStreamed(actors[i].id)) continue;
        PushElement(L, actors[i].id);
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

// CLuaFunctionDefs::AddEvent (lua/CLuaFunctionDefs.Event.cpp 13-43) and
// CStaticFunctionDefinitions::AddEvent (CStaticFunctionDefinitions.cpp:115-127).
// bool addEvent ( string eventName [, bool allowRemoteTrigger = false ] )
static int l_addEvent(lua_State* L) {
    const int nameType = lua_type(L, 1);
    const int remoteType = lua_type(L, 2);
    const bool remoteOk = remoteType == LUA_TBOOLEAN || remoteType == LUA_TNONE || remoteType == LUA_TNIL;
    if ((nameType != LUA_TSTRING && nameType != LUA_TNUMBER) || !remoteOk) {
        LUA_LOGI("WARNING: Bad argument @ 'addEvent' [Expected %s at argument %d, got %s]",
                 remoteOk ? "string" : "bool", remoteOk ? 1 : 2, luaL_typename(L, remoteOk ? 1 : 2));
        lua_pushboolean(L, 0);
        return 1;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    const std::string name(text ? text : "", length);
    const bool allowRemoteTrigger = remoteType == LUA_TBOOLEAN && lua_toboolean(L, 2) != 0;
    const bool added = !name.empty() &&
                       Events::Add(name, allowRemoteTrigger, g_dx.owner, g_dx.ownerNetId, g_dx.ownerGeneration);
    lua_pushboolean(L, added ? 1 : 0);
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

static void SetNetworkNumber(lua_State* L, const char* key, double value) {
    lua_pushstring(L, key);
    lua_pushnumber(L, value);
    lua_settable(L, -3);
}

// Desktop CLuaFunctionDefs::GetNetworkStats (CLuaFunctionDefs.Util.cpp 114-169).
// encryptionStatus is the desktop constant 1, not a transport counter.
static int l_getNetworkStats(lua_State* L) {
    auto* net = Net::RakNetWrapper::Live();
    if (!net) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Net::NetworkStatistics stats{};
    if (!net->GetNetworkStatistics(stats)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_createtable(L, 0, 11);
    SetNetworkNumber(L, "bytesReceived", static_cast<double>(stats.bytesReceived));
    SetNetworkNumber(L, "bytesSent", static_cast<double>(stats.bytesSent));
    SetNetworkNumber(L, "packetsReceived", stats.packetsReceived);
    SetNetworkNumber(L, "packetsSent", stats.packetsSent);
    SetNetworkNumber(L, "packetlossTotal", stats.packetlossTotal);
    SetNetworkNumber(L, "packetlossLastSecond", stats.packetlossLastSecond);
    SetNetworkNumber(L, "messagesInSendBuffer", stats.messagesInSendBuffer);
    SetNetworkNumber(L, "messagesInResendBuffer", stats.messagesInResendBuffer);
    SetNetworkNumber(L, "isLimitedByCongestionControl", stats.isLimitedByCongestionControl ? 1 : 0);
    SetNetworkNumber(L, "isLimitedByOutgoingBandwidthLimit", stats.isLimitedByOutgoingBandwidthLimit ? 1 : 0);
    SetNetworkNumber(L, "encryptionStatus", 1);
    return 1;
}

static void PushUsageDirection(lua_State* L, const std::array<Net::PacketUsage, 256>& slots) {
    lua_createtable(L, 0, 2);
    lua_pushstring(L, "bits");
    lua_createtable(L, 255, 1);
    for (int i = 0; i < 256; ++i) {
        lua_pushnumber(L, static_cast<double>(slots[static_cast<size_t>(i)].totalBytes) * 8.0);
        lua_rawseti(L, -2, i);
    }
    lua_rawset(L, -3);

    lua_pushstring(L, "count");
    lua_createtable(L, 255, 1);
    for (int i = 0; i < 256; ++i) {
        lua_pushnumber(L, slots[static_cast<size_t>(i)].count);
        lua_rawseti(L, -2, i);
    }
    lua_rawset(L, -3);
}

// Desktop CLuaFunctionDefs::GetNetworkUsageData (CLuaFunctionDefs.Util.cpp 54-111).
// Slots are the wire packet id. bits = totalBytes * 8.
static int l_getNetworkUsageData(lua_State* L) {
    std::array<Net::PacketUsage, 256> incoming{};
    std::array<Net::PacketUsage, 256> outgoing{};
    if (auto* net = Net::RakNetWrapper::Live()) net->CopyPacketUsage(incoming, outgoing);
    lua_createtable(L, 0, 2);
    lua_pushstring(L, "in");
    PushUsageDirection(L, incoming);
    lua_rawset(L, -3);
    lua_pushstring(L, "out");
    PushUsageDirection(L, outgoing);
    lua_rawset(L, -3);
    return 1;
}

extern "C" void InstallCategoryLua(lua_State* L);

// Registration wardrobe. Desktop order is createPed, setTime, setWeather,
// setCameraMatrix every frame, addPedClothes / removePedClothes, setElementRotation.
// setCameraTarget(localPlayer) ends the fixed camera.
static int l_createPed(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int model = static_cast<int>(lua_tointeger(L, 1));
    const float rot = lua_isnumber(L, 5) ? static_cast<float>(lua_tonumber(L, 5)) : 0.f;
    const uint32_t id = Game::NoteScriptPed(model, static_cast<float>(lua_tonumber(L, 2)),
        static_cast<float>(lua_tonumber(L, 3)), static_cast<float>(lua_tonumber(L, 4)), rot);
    if (!id) {
        lua_pushboolean(L, 0);
        return 1;
    }
    PushElement(L, id);
    return 1;
}

static int l_addPedClothes(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const char* texture = lua_tostring(L, 2);
    const char* model = lua_tostring(L, 3);
    const int type = static_cast<int>(lua_tointeger(L, 4));
    if (!Game::ScriptPedAlive(id) || !texture || !model || type < 0 || type >= 18) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Game::NoteScriptClothes(id, texture, model, type, false);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_removePedClothes(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const int type = static_cast<int>(lua_tointeger(L, 2));
    if (!Game::ScriptPedAlive(id) || type < 0 || type >= 18) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Game::NoteScriptClothes(id, nullptr, nullptr, type, true);
    lua_pushboolean(L, 1);
    return 1;
}

static void ConvertEulerOrderToZXY(const char* order, double& rx, double& ry, double& rz) {
    if (!order || std::strcmp(order, "default") == 0 || std::strcmp(order, "ZXY") == 0) return;
    double a = rx, b = ry, c = rz;
    if (std::strcmp(order, "-ZYX") == 0) {
        a = -a;
        b = -b;
        c = -c;
    }
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
    const double sA = std::sin(a * kDegToRad), cA = std::cos(a * kDegToRad);
    const double sB = std::sin(b * kDegToRad), cB = std::cos(b * kDegToRad);
    const double sC = std::sin(c * kDegToRad), cC = std::cos(c * kDegToRad);
    const double sinX = std::clamp(sA * cB, -1.0, 1.0);
    rx = std::asin(sinX) * kRadToDeg;
    ry = std::atan2(sB, cA * cB) * kRadToDeg;
    rz = std::atan2(cA * sC - sA * sB * cC, cA * cC + sA * sB * sC) * kRadToDeg;
}

static int l_setElementRotation(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (IsWorldObject(id)) {
        // CStaticFunctionDefinitions::SetElementRotation, CCLIENTOBJECT.
        double rx = luaL_optnumber(L, 2, 0), ry = luaL_optnumber(L, 3, 0), rz = luaL_optnumber(L, 4, 0);
        const char* order = lua_type(L, 5) == LUA_TSTRING ? lua_tostring(L, 5) : "default";
        if (!std::isfinite(rx) || !std::isfinite(ry) || !std::isfinite(rz) ||
            (std::strcmp(order, "default") != 0 && std::strcmp(order, "ZXY") != 0 &&
             std::strcmp(order, "ZYX") != 0 && std::strcmp(order, "-ZYX") != 0)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        ConvertEulerOrderToZXY(order, rx, ry, rz);
        constexpr double k = 3.14159265358979323846 / 180.0;
        const bool ok = SetObjectRotation(id, float(rx * k), float(ry * k), float(rz * k));
        if (ok) ClearScriptMatrix(id);
        lua_pushboolean(L, ok ? 1 : 0);
        return 1;
    }
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") == 0) {
        double rx = luaL_checknumber(L, 2), ry = luaL_checknumber(L, 3), rz = luaL_checknumber(L, 4);
        const char* order = lua_type(L, 5) == LUA_TSTRING ? lua_tostring(L, 5) : "default";
        ConvertEulerOrderToZXY(order, rx, ry, rz);
        const bool ok = Game::SetTrafficVehicleRotation(id, static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz));
        if (ok) ClearScriptMatrix(id);
        lua_pushboolean(L, ok);
        return 1;
    }
    if (std::strcmp(ServerWorld::ElementKindName(id), "ped") == 0 && ServerWorld::ActorHosted(id)) {
        const float rz = luaL_checknumber(L, 4);
        if (!std::isfinite(rz)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        Game::RpcSetRotation(id, rz * (3.14159265f / 180.f));
        ClearScriptMatrix(id);
        lua_pushboolean(L, 1);
        return 1;
    }
    if (Game::ScriptPedAlive(id) && lua_isnumber(L, 4)) {
        Game::NoteScriptRotation(id, static_cast<float>(lua_tonumber(L, 4)));
        ClearScriptMatrix(id);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int l_setElementFrozen(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (IsWorldObject(id)) {
        lua_pushboolean(L, SetObjectFrozen(id, lua_toboolean(L, 2) != 0) ? 1 : 0);
        return 1;
    }
    if (Game::ScriptPedAlive(id)) Game::NoteScriptFrozen(id, lua_toboolean(L, 2) != 0);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_setCameraMatrix(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float lx = lua_isnumber(L, 4) ? static_cast<float>(lua_tonumber(L, 4)) : 0.f;
    const float ly = lua_isnumber(L, 5) ? static_cast<float>(lua_tonumber(L, 5)) : 0.f;
    const float lz = lua_isnumber(L, 6) ? static_cast<float>(lua_tonumber(L, 6)) : 0.f;
    Game::NoteScriptCamera(static_cast<float>(lua_tonumber(L, 1)), static_cast<float>(lua_tonumber(L, 2)),
        static_cast<float>(lua_tonumber(L, 3)), lx, ly, lz);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_setCameraTarget(lua_State* L) {
    (void)L;
    Game::ClearScriptCamera();
    lua_pushboolean(L, 1);
    return 1;
}

static int l_setTime(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, Game::NoteScriptTime(static_cast<int>(lua_tointeger(L, 1)),
        static_cast<int>(lua_tointeger(L, 2))) ? 1 : 0);
    return 1;
}

static int l_setWeather(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, Game::NoteScriptWeather(static_cast<int>(lua_tointeger(L, 1))) ? 1 : 0);
    return 1;
}

static void installWardrobeBindings(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createPed", l_createPed},
        {"addPedClothes", l_addPedClothes},
        {"removePedClothes", l_removePedClothes},
        {"setElementRotation", l_setElementRotation},
        // CLuaCompatibilityDefs.cpp:54 and 59. Same setter as setElementRotation.
        {"setObjectRotation", l_setElementRotation},
        {"setVehicleRotation", l_setElementRotation},
        {"setElementFrozen", l_setElementFrozen},
        {"setCameraMatrix", l_setCameraMatrix},
        {"setCameraTarget", l_setCameraTarget},
        {"setTime", l_setTime},
        {"setWeather", l_setWeather},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

static void installDxBindings(lua_State* L) {
    g_dx.L = L;
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"cancelEvent", l_cancelEvent},
        {"wasEventCancelled", l_wasEventCancelled},
        {"addEventHandler", l_addEventHandler},
        {"getEventHandlers", l_getEventHandlers},
        {"triggerEvent", l_triggerEvent},
        {"removeEventHandler", l_removeEventHandler},
        {"dxDrawRectangle", l_dxDrawRectangle},
        {"dxDrawText", l_dxDrawText},
        {"dxDrawImage", l_dxDrawImage},
        {"dxDrawImageSection", l_dxDrawImageSection},
        {"tocolor", l_tocolor},
        {"guiGetScreenSize", l_guiGetScreenSize},
        {"getCursorPosition", l_getCursorPosition},
        {"setCursorPosition", l_setCursorPosition},
        {"getTickCount", l_getTickCount},
        {"getNetworkStats", l_getNetworkStats},
        {"getNetworkUsageData", l_getNetworkUsageData},
        {"setTimer", l_setTimer},
        {"getElementHealth", l_getElementHealth},
        {"getPedArmor", l_getPedArmor},
        // CLuaCompatibilityDefs.cpp:40. Same function as getPedArmor.
        {"getPlayerArmor", l_getPedArmor},
        {"getTime", l_getTime},
        {"getPlayerWantedLevel", l_num0},
        {"getElementModel", l_num0},
        {"getElementVelocity", l_getElementVelocity},
        {"getPlayerName", l_getPlayerName},
        {"isTimer", l_isTimer},
        {"killTimer", l_killTimer},
        {"resetTimer", l_resetTimer},
        {"setTimerPaused", l_setTimerPaused},
        {"isTimerPaused", l_isTimerPaused},
        {"getTimerDetails", l_getTimerDetails},
        {"getTimers", l_getTimers},
        {"getDistanceBetweenPoints3D", l_getDistanceBetweenPoints3D},
        {"getDistanceBetweenPoints2D", l_getDistanceBetweenPoints2D},
        {"getPedMoveState", l_getPedMoveState},
        // Ped and element natives the ped-traffic client calls. Defaults until the engine exposes them.
        {"getPedRotation", l_getPedRotation},
        // CLuaCompatibilityDefs.cpp:17. Same function as getPedRotation.
        {"getPlayerRotation", l_getPedRotation},
        {"getPedAmmoInClip", l_num0},
        {"getPedTotalAmmo", l_num0},
        {"getPedWeapon", l_num0},
        {"getPedWeaponSlot", l_num0},
        {"getPedWeaponAccuracy", l_num0},
        {"getPedWeaponShootingRate", l_num0},
        {"getPedStat", l_num0},
        // CLuaCompatibilityDefs.cpp:44. Same stub as getPedStat until that query is ported.
        {"getPlayerStat", l_num0},
        {"getVehicleType", l_num0},
        {"getGroundPosition", l_getGroundPosition},
        {"getPedTask", l_false},
        // CLuaCompatibilityDefs.cpp:27.
        {"getPlayerTask", l_false},
        {"getPedTarget", l_false},
        // CLuaCompatibilityDefs.cpp:30.
        {"getPlayerTarget", l_false},
        {"getPedControlState", l_false},
        // CLuaCompatibilityDefs.cpp:71.
        {"getControlState", l_false},
        {"getPedStayInSamePlace", l_false},
        {"isElementFrozen", l_false},
        {"isElementOnScreen", l_false},
        {"isPedOnGround", l_true},
        // CLuaCompatibilityDefs.cpp:26.
        {"isPlayerOnGround", l_true},
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
        // CLuaCompatibilityDefs.cpp:70.
        {"setControlState", l_true},
        {"setPedJump", l_true},
        {"setPedNavigateTo", l_true},
        {"setPedRotation", l_true},
        // CLuaCompatibilityDefs.cpp:47.
        {"setPlayerRotation", l_true},
        {"setPedStandStill", l_true},
        {"setPedStayInSamePlace", l_true},
        {"setPedWeaponSlot", l_true},
        // CLuaCompatibilityDefs.cpp:45.
        {"setPlayerWeaponSlot", l_true},
        {"setPedWeaponAccuracy", l_true},
        {"setPedWeaponShootingRate", l_true},
        // setCameraMatrix is installed by installWardrobeBindings, which runs after this
        // table. A stub here would be the binding the parity scan keeps.
        // InstallAmbientLua binds the ambient population calls. isWorldRPGMobilePopulation
        // stays true until FindZoneByLabel, popcycle, and CCamera::IsSphereVisible exist.
        // The traffic scripts use that flag, not the absence of these names, to keep the
        // phone puppet and the polyfills.
        {"processLineOfSight", l_false},
        {"getPlayerMoney", l_getPlayerMoney},
        {"triggerServerEvent", l_triggerServerEvent},
        {"triggerLatentServerEvent", l_triggerLatentServerEvent},
        {"getLatentEventHandles", l_getLatentEventHandles},
        {"getLatentEventStatus", l_getLatentEventStatus},
        {"cancelLatentEvent", l_cancelLatentEvent},
        {"addEvent", l_addEvent},
        {"setPedWander", l_setPedWander},
        {"getNeonTrafficCapability", l_getNeonTrafficCapability},
        {"isElement", l_isElement},
        {"isElementStreamedIn", l_isElementStreamedIn},
        {"isElementSyncer", l_isElementSyncer},
        {"getElementType", l_getElementType},
        {"getElementPosition", l_getElementPosition},
        {"getElementDimension", l_num0},
        {"getElementInterior", l_num0},
        {"getElementsByType", l_getElementsByType},
        {"getLocalPlayer", l_getLocalPlayer},
        {"isPedDead", l_false},
        {"setElementAlpha", l_true},
        {"setPlayerHudComponentVisible", l_setPlayerHudComponentVisible},
        {"showPlayerHudComponent", l_setPlayerHudComponentVisible},
        {"isPlayerHudComponentVisible", l_isPlayerHudComponentVisible},
        {"getMobileCrosshairPosition", l_getMobileCrosshairPosition},
        {"outputDebugString", l_debug},
        {"addCommandHandler", l_true},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_newtable(L); lua_setglobal(L, "root");
    lua_newtable(L); lua_setglobal(L, "resourceRoot");
    PushElement(L, WorldRPG::ServerWorld::LocalPlayer());
    lua_setglobal(L, "localPlayer");
    InstallExtraBindings(L);
    installWardrobeBindings(L);
    InstallAudioLua(L);
    InstallCategoryLua(L);
    InstallCameraLua(L);
    InstallFieldLua(L);
    InstallUtilLua(L);
    InstallFileLua(L);
    InstallXmlLua(L);
    InstallBrowserLua(L);
    InstallDiscordLua(L);
    InstallRocketSoccerLua(L);
    InstallStoredElementLua(L);
    InstallClothesLua(L);
    InstallWeaponLua(L);
    InstallResourceLua(L);
    InstallMtaWindowLua(L);
    InstallKeyBindLua(L);
    InstallDxLua(L);
    InstallSvgLua(L);
    InstallSpatialLua(L);
    InstallWorldObjectLua(L);
    InstallMarkerLua(L);
    InstallBlipLua(L);
    InstallRadarLua(L);
    InstallPickupLua(L);
    InstallLightLua(L);
    InstallBirdLua(L);
    InstallTeamLua(L);
    InstallRopeLua(L);
    InstallNativeUiLua(L);
    InstallWaterLua(L);
    InstallFireLua(L);
    InstallFxLua(L);
    InstallWorldQueryLua(L);
    InstallSightLua(L);
    InstallAmbientLua(L);
    // These two stay on this file even if extra bindings register the same names.
    lua_pushcfunction(L, l_getNetworkStats);
    lua_setglobal(L, "getNetworkStats");
    lua_pushcfunction(L, l_getNetworkUsageData);
    lua_setglobal(L, "getNetworkUsageData");
    // Install MTA OOP class metatables and Vector2/Vector3/Vector4/Matrix constructors last
    // so every procedural MTA global they wrap is already registered.
    InstallOopLua(L);
}

static void refreshLocalPlayer(lua_State* L) {
    PushElement(L, WorldRPG::ServerWorld::LocalPlayer());
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
    lua_State* L = nullptr;    // Lua 5.3: the session's state
    lua_State* L51 = nullptr;  // Lua 5.1: desktop MTA resources (lua51())
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

    void registerStdLibs(lua_State* L) {
        if (!L) return;
        const bool is51 = Vms::Is51(L);
        // Match the desktop client boundaries for scripts received from servers.
        // Register only the libraries needed by resources, plus restricted OS/debug tables.
        const luaL_Reg libraries[] = {
            {"_G", luaopen_base},
            {LUA_COLIBNAME, luaopen_coroutine},
            {LUA_TABLIBNAME, luaopen_table},
            {LUA_STRLIBNAME, luaopen_string},
            {LUA_MATHLIBNAME, luaopen_math},
            {LUA_UTF8LIBNAME, luaopen_utf8},
#if defined(LUA_COMPAT_BITLIB)
            {LUA_BITLIBNAME, luaopen_bit32},
#endif
        };
        for (const auto& lib : libraries) {
            if (is51 && lib.func == luaopen_bit32) continue;  // not in Lua 5.1
            luaL_requiref(L, lib.name, lib.func, 1);
            lua_pop(L, 1);
        }

        luaopen_os(L);
        lua_newtable(L);
        for (const char* name : {"clock", "date", "difftime", "time"}) {
            lua_getfield(L, -2, name);
            lua_setfield(L, -2, name);
        }
        lua_setglobal(L, LUA_OSLIBNAME);
        lua_pop(L, 1);

        luaopen_debug(L);
        lua_newtable(L);
        lua_getfield(L, -2, "traceback");
        lua_setfield(L, -2, "traceback");
        lua_setglobal(L, LUA_DBLIBNAME);
        lua_pop(L, 1);

        // Lua 5.3 builds without LUA_COMPAT_MATHLIB have no math.atan2. Traffic's
        // onClientPreRender calls it every frame and errors before the login camera runs.
        lua_getglobal(L, "math");
        lua_getfield(L, -1, "atan2");
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            lua_getfield(L, -1, "atan");
            lua_setfield(L, -2, "atan2");
        } else {
            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        // Override print() with custom version that uses output callback
        lua_pushcfunction(L, lua_custom_print);
        lua_setglobal(L, "print");

        // Scripts written for the Android Lua 5.3 client that parse as 5.1 run in
        // the 5.1 VM (ChooseResourceVm). These 5.3 library functions, missing in
        // 5.1, keep the common ones working there; desktop scripts that define or
        // feature-test them (table.unpack or unpack) behave as on desktop.
        if (is51) {
            static const char kCompat53[] = R"LUA(
                table.unpack = table.unpack or unpack
                table.pack = table.pack or function(...) return {n = select('#', ...), ...} end
                table.move = table.move or function(a1, f, e, t, a2)
                    a2 = a2 or a1
                    if e >= f then
                        if t > e or t <= f or a1 ~= a2 then
                            for i = 0, e - f do a2[t + i] = a1[f + i] end
                        else
                            for i = e - f, 0, -1 do a2[t + i] = a1[f + i] end
                        end
                    end
                    return a2
                end
                local floor, huge = math.floor, math.huge
                local function integral(x) return x == x and x ~= huge and x ~= -huge and x == floor(x) end
                math.type = math.type or function(x)
                    if type(x) ~= 'number' then return nil end
                    return integral(x) and 'integer' or 'float'
                end
                math.tointeger = math.tointeger or function(x)
                    x = tonumber(x)
                    if x and integral(x) then return x end
                    return nil
                end
            )LUA";
            if (luaL_loadbufferx(L, kCompat53, sizeof(kCompat53) - 1, "=(neon 5.3 compat)", "t") != LUA_OK ||
                lua_pcall(L, 0, 0, 0) != LUA_OK) {
                LUA_LOGE("LuaVM: 5.3 library additions: %s", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }

        // Desktop CLuaMain::InitSecurity (CLuaMain.cpp 77-92): the os table above
        // holds only the functions it leaves (clock, date, difftime, time), and
        // InstallUtilLua replaces dofile, loadfile, require, loadlib, getfenv and
        // newproxy, on both VMs.
    }

    void registerAllFunctions(lua_State* L) {
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

// ServerWorld lifecycle hooks (defined with the other network events below).
static void OnPlayerJoinHook(uint32_t id);
static void OnPlayerQuitHook(uint32_t id, const char* reason);
static void OnElementDestroyHook(uint32_t id);
static void OnDataChangeHook(const ServerWorld::ElementDataChange& change);

bool LuaVMStub::initialize() {
    if (pImpl_->initialized) return true;

    pImpl_->L = luaL_newstate();
    if (!pImpl_->L) {
        LUA_LOGE("LuaVM: failed to create lua_State");
        return false;
    }

    Vms::SetPrimary(pImpl_->L);
    pImpl_->registerStdLibs(pImpl_->L);

    // Names still without an implementation go in registeredFunctions before
    // registerAllFunctions. The list is empty: getResourceName is a real function now.
    // installDxBindings and LuaApiExtra bind everything else.
    pImpl_->registerAllFunctions(pImpl_->L);

    g_active = this;
    g_dx.L = pImpl_->L;
    // CClientGame constructor: m_bFocused = g_pCore->IsFocused(), with no focus event.
    MtaWindowAdoptFocus();
    GuiSetFocusHook(OnGuiFocusHook);
    {
        ServerWorld::LifecycleHooks hooks;
        hooks.playerJoin = OnPlayerJoinHook;
        hooks.playerQuit = OnPlayerQuitHook;
        hooks.elementDestroy = OnElementDestroyHook;
        hooks.dataChange = OnDataChangeHook;
        ServerWorld::SetLifecycleHooks(hooks);
    }
    pImpl_->initialized = true;
    // The Lua 5.1 VM is created now, before the session has elements, binds or
    // commands: installing the script API resets those (ResetCatalog,
    // KeyBinds, ResetSpatial, DxReset).
    lua51();
    pImpl_->log("INIT", "LuaVM initialized with real Lua 5.3.6 interpreter");
    LUA_LOGI("LuaVM: registered %zu MTA client function stubs",
             pImpl_->registeredFunctions.size());
    return true;
}

lua_State* LuaVMStub::lua51() {
    if (pImpl_->L51 || !pImpl_->initialized) return pImpl_->L51;
    lua_State* L = neon_lua_newstate51();
    if (!L) {
        LUA_LOGE("LuaVM: failed to create the Lua 5.1 state");
        return nullptr;
    }
    pImpl_->L51 = L;
    Vms::SetLua51(L);
    {
        // installDxBindings makes the state it installs on current; the 5.3
        // state stays the session's.
        VmScope vm(L);
        pImpl_->registerStdLibs(L);
        pImpl_->registerAllFunctions(L);
    }
    LUA_LOGI("LuaVM: Lua 5.1 VM created for desktop MTA resources");
    return L;
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
    refreshLocalPlayer(pImpl_->L);

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
    pImpl_->registerAllFunctions(pImpl_->L);
    MtaWindowAdoptFocus();
    GuiReset();
    RocketSoccerReset();
    WorldObjects::Reset();

    pImpl_->loadedScripts.clear();
    LUA_LOGI("LuaVM: reset complete");
}

void LuaVMStub::setScreenSize(int width, int height) {
    if (width > 0) g_dx.screenW = width;
    if (height > 0) g_dx.screenH = height;
}

// One resource script, ready to run in the resource's VM.
struct PreparedScript {
    std::string chunk;
    std::vector<uint8_t> source;
    int compiled = LUA_NOREF;  // the chunk, already compiled in the chosen VM (raw registry ref)
};

static size_t Utf8BomLength(const std::vector<uint8_t>& s) {
    return s.size() >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF ? 3 : 0;
}

// SharedUtil::IsLuaObfuscatedScript (0x1C, luac.mtasa.com encryption) and
// IsLuaCompiledScript (0x1B after an optional BOM).
static bool IsObfuscatedScript(const std::vector<uint8_t>& s) { return !s.empty() && s[0] == 0x1C; }
static bool IsCompiledScript(const std::vector<uint8_t>& s) {
    const size_t bom = Utf8BomLength(s);
    return IsObfuscatedScript(s) || (s.size() > bom && s[bom] == 0x1B);
}

// Compiles one text script in L, leaving the chunk on the stack.
static int LoadResourceScript(lua_State* L, const PreparedScript& script) {
    const size_t bom = Utf8BomLength(script.source);
    const char* data = reinterpret_cast<const char*>(script.source.data()) + bom;
    return luaL_loadbufferx(L, data, script.source.size() - bom, ("@" + script.chunk).c_str(), "t");
}

// Which VM a resource runs in (lua51/README.md, luaapi/README.md): Lua 5.1, as
// desktop MTA, unless one of its text scripts only parses as Lua 5.3 (//, the
// bitwise operators, goto, integer literals), which marks a resource written
// for the Android 5.3 client. 5.1 bytecode or encrypted chunks keep it 5.1.
// The chunks compiled while choosing are kept for RunResourceJob.
static lua_State* ChooseResourceVm(const WorldRPG::Script::ResourceLoadJob& job, std::vector<PreparedScript>& scripts,
                                   std::string& reason) {
    lua_State* L53 = Vms::Primary();
    if (g_resourceLua == 1) {
        reason = "Lua 5.3 chosen";
        return L53;
    }
    lua_State* L51 = g_active ? g_active->lua51() : nullptr;
    if (!L51) {
        reason = "the Lua 5.1 VM is unavailable";
        return L53;
    }
    bool needs51 = false;
    std::string only53;
    for (PreparedScript& script : scripts) {
        const size_t bom = Utf8BomLength(script.source);
        const uint8_t first = script.source.size() > bom ? script.source[bom] : 0;
        (void)first;
        if (IsCompiledScript(script.source)) {
            needs51 = true;
            if (reason.empty()) reason = script.chunk + " is compiled";
            continue;
        }
        if (LoadResourceScript(L51, script) == LUA_OK) {
            script.compiled = luaL_ref(L51, LUA_REGISTRYINDEX);
            continue;
        }
        lua_pop(L51, 1);
        if (only53.empty() && LoadResourceScript(L53, script) == LUA_OK) {
            lua_pop(L53, 1);
            only53 = script.chunk;
        }
    }
    if (g_resourceLua == 2) {
        reason = "Lua 5.1 chosen";
        return L51;
    }
    if (!only53.empty() && !needs51) {
        for (PreparedScript& script : scripts) {
            if (script.compiled != LUA_NOREF) luaL_unref(L51, LUA_REGISTRYINDEX, script.compiled);
            script.compiled = LUA_NOREF;
        }
        reason = only53 + " is Lua 5.3";
        return L53;
    }
    if (!only53.empty()) reason += "; " + only53 + " is Lua 5.3 and will fail";
    if (reason.empty()) reason = "desktop MTA scripts";
    (void)job;
    return L51;
}

#ifndef __ANDROID__
// Host tools: NEON_DUMP_SCRIPTS=<dir> saves each resource script as it runs, so
// a server's client scripts can be read when porting what they call.
static void DumpScriptForHost(const PreparedScript& script) {
    const char* dir = std::getenv("NEON_DUMP_SCRIPTS");
    if (!dir || !*dir || script.source.empty()) return;
    std::string name = script.chunk;
    for (char& c : name)
        if (c == '/' || c == '\\' || c == ':') c = '_';
    if (std::FILE* f = std::fopen((std::string(dir) + "/" + name).c_str(), "wb")) {
        std::fwrite(script.source.data(), 1, script.source.size(), f);
        std::fclose(f);
    }
}
#endif

// Runs one resource script with the resource's shared environment: _ENV in
// Lua 5.3, the chunk's environment (setfenv) in Lua 5.1. Globals a file
// defines are visible to the other files of the same resource (desktop: one
// Lua state per resource). Reads fall through to the VM globals.
static bool RunResourceChunk(lua_State* L, PreparedScript& script, int envRef) {
#ifndef __ANDROID__
    DumpScriptForHost(script);
#endif
    if (script.compiled != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, script.compiled);
        luaL_unref(L, LUA_REGISTRYINDEX, script.compiled);
        script.compiled = LUA_NOREF;
    } else if (IsCompiledScript(script.source)) {
        // Compiled scripts load as on desktop (CLuaMain::LoadScriptFromBuffer):
        // netc's CNet::DeobfuscateScript accepts or decrypts them first, which
        // only the PC Network session has. Lua 5.1 does not verify bytecode, so
        // none is loaded without it.
        PreparedScript plain{script.chunk, {}};
        if (!Vms::Is51(L) ||
            !Net::DeobfuscateScriptForSession(script.source.data(), script.source.size(), script.chunk, plain.source)) {
            LUA_LOGE("Resource script %s is compiled (luac.mtasa.com): it loads only with PC Network compatibility",
                     script.chunk.c_str());
            NoteScriptError(script.chunk.c_str(), "compiled script without PC Network");
            return false;
        }
        const size_t bom = Utf8BomLength(plain.source);
        const int status = luaL_loadbufferx(L, reinterpret_cast<const char*>(plain.source.data()) + bom,
                                            plain.source.size() - bom, ("@" + script.chunk).c_str(), "bt");
        std::fill(plain.source.begin(), plain.source.end(), 0);
        if (status != LUA_OK) {
            LUA_LOGE("Resource script %s failed: %s", script.chunk.c_str(), lua_tostring(L, -1));
            NoteScriptError(script.chunk.c_str(), lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }
    } else if (LoadResourceScript(L, script) != LUA_OK) {
        LUA_LOGE("Resource script %s failed: %s", script.chunk.c_str(), lua_tostring(L, -1));
        NoteScriptError(script.chunk.c_str(), lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, envRef);
    if (Vms::Is51(L)) neon_lua51_setfenv(L, -2);
    else lua_setupvalue(L, -2, 1);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        LUA_LOGE("Resource script %s failed: %s", script.chunk.c_str(), lua_tostring(L, -1));
        NoteScriptError(script.chunk.c_str(), lua_tostring(L, -1));
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
static void RunResourceJob(const WorldRPG::Script::ResourceLoadJob& job) {
    if (WorldRPG::Debug::Flag("nolua")) return;
    // Desktop order: no-cache scripts first, then the CLIENT_SCRIPT files.
    std::vector<PreparedScript> scripts;
    int failed = 0;
    for (const ResourceScript& script : job.noCacheScripts) scripts.push_back({script.chunkName, script.source});
    const size_t noCacheCount = scripts.size();
    for (const ResourceScript& script : job.clientScripts) {
        PreparedScript prepared{script.chunkName, {}};
        if (!ReadCachedScript(script.cachePath, prepared.source) || prepared.source.empty()) {
            LUA_LOGE("Resource script %s: cached file unreadable (%s)", script.chunkName.c_str(), script.cachePath.c_str());
            ++failed;
            continue;
        }
        scripts.push_back(std::move(prepared));
    }
    std::string reason;
    lua_State* L = ChooseResourceVm(job, scripts, reason);
    if (!L) return;
    g_resourceVm[job.generation] = L;
    LUA_LOGI("LuaVM: resource %s runs in Lua %s (%s)", job.name.c_str(), Vms::Is51(L) ? "5.1" : "5.3", reason.c_str());
    VmScope vm(L);
    OwnerScope owner(job.name, job.netId, job.generation);
    refreshLocalPlayer(L);
    lua_newtable(L);                       // resource environment
    lua_newtable(L);                       // its metatable: reads fall through to the VM globals
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    PushElement(L, job.rootElement);
    lua_setfield(L, -2, "resourceRoot");
    KeepResourceEnvironment(L, job.generation, -1);
    const int envRef = luaL_ref(L, LUA_REGISTRYINDEX);
    int cachedOk = 0, noCacheOk = 0;
    for (size_t i = 0; i < scripts.size(); ++i) {
        if (RunResourceChunk(L, scripts[i], envRef)) ++(i < noCacheCount ? noCacheOk : cachedOk);
        else ++failed;
    }
    luaL_unref(L, LUA_REGISTRYINDEX, envRef);
    PushElement(L, job.rootElement);
    lua_setglobal(L, "source");
    // CResource::Load (CResource.cpp 1626-1628): the argument is the resource, and
    // the event is called on the resource element so it reaches its parents.
    const int startArgs = PushResourceInstance(L, job.netId, job.generation) ? 1 : 0;
    fireHandlers("onClientResourceStart", startArgs, job.rootElement ? job.rootElement : ClientRootElement(), nullptr, true);
    LUA_LOGI("LuaVM: resource %s started: %d cached scripts, %d no-cache scripts", job.name.c_str(), cachedOk, noCacheOk);
    if (failed) LUA_LOGE("LuaVM: resource %s: %d script(s) failed to load", job.name.c_str(), failed);
    SendResourceStarted(job);
}

static void FlushPendingChat(lua_State* L) {
    if (!g_dx.chatHooked || g_dx.pendingChat.empty()) return;
    if (g_dx.handlers.find("onClientChatMessage") != g_dx.handlers.end()) {
        for (const auto& chat : g_dx.pendingChat) {
            lua_pushlstring(L, chat.text.data(), chat.text.size());
            lua_pushinteger(L, chat.r);
            lua_pushinteger(L, chat.g);
            lua_pushinteger(L, chat.b);
            lua_pushinteger(L, 0);
            // outputChatBox calls the event on the root (CStaticFunctionDefinitions.cpp 292).
            fireHandlers("onClientChatMessage", 5, ClientRootElement());
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

    lua_pushstring(L, text);
    lua_pushinteger(L, red);
    lua_pushinteger(L, green);
    lua_pushinteger(L, blue);
    lua_pushinteger(L, 0);
    return fireHandlers("onClientChatMessage", 5, ClientRootElement());
}

// Fires onClientClick (and onClientCursorMove) for the queued touches. Signature as on desktop:
// CClientGame GUI mouse handlers (CClientGame.cpp 6660-6812), on the widget
// GuiPick finds, with the source's children called. Touch is the left button.
static constexpr const char* kMinClientGuiClickDown = "1.7.0-7.26369";  // MIN_CLIENT_REQ_ON_MOUSE_BUTTON_DOWN
static constexpr uint64_t kGuiDoubleClickMs = 330;                    // CEGUI System default double-click timeout
static uint32_t g_guiPressed = 0;      // widget under the last press (CEGUI click tracker)
static uint32_t g_guiHover = 0;        // widget currently hovered by cursor
static uint32_t g_guiLastPress = 0;    // for double clicks
static uint64_t g_guiLastPressMs = 0;
static uint64_t g_rootLastClickMs = 0;

static void FireGuiMouseEvent(const char* name, uint32_t widget, const char* state, double x, double y,
                              const char* minClient = nullptr) {
    lua_State* L = g_dx.L;
    if (!L || !widget || !KnownClientElement(widget)) return;
    lua_getglobal(L, "source");
    PushElement(L, widget);
    lua_setglobal(L, "source");
    lua_pushstring(L, "left");
    if (state) lua_pushstring(L, state);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    const char* saved = g_eventMinClient;
    g_eventMinClient = minClient;
    fireHandlers(name, state ? 4 : 3, widget, nullptr, true);
    g_eventMinClient = saved;
    lua_setglobal(L, "source");
}

// OnMouseButtonDown: onClientGUIMouseDown, then onClientGUIClick "down" for
// resources that ask for client 1.7.0-7.26369 or newer; a second press on the
// same widget within the timeout is also a double click (OnMouseDoubleClick).
// OnMouseButtonUp: onClientGUIMouseUp, then OnMouseClick's onClientGUIClick
// "up" when the release is on the widget that was pressed.
static void ApplyGuiMouse(const CursorEvent& ev) {
    const uint32_t hit = GuiPick(ev.x, ev.y);
    if (ev.action == 0) {
        g_guiPressed = hit;
        if (!hit) return;
        GuiHandleClick(hit, ev.x, ev.y);
        const uint64_t now = DxNow();
        const bool doubleClick = hit == g_guiLastPress && now - g_guiLastPressMs <= kGuiDoubleClickMs;
        g_guiLastPress = doubleClick ? 0 : hit;
        g_guiLastPressMs = now;
        FireGuiMouseEvent("onClientGUIMouseDown", hit, nullptr, ev.x, ev.y);
        FireGuiMouseEvent("onClientGUIClick", hit, "down", ev.x, ev.y, kMinClientGuiClickDown);
        if (doubleClick) FireGuiMouseEvent("onClientGUIDoubleClick", hit, "up", ev.x, ev.y);
        return;
    }
    const uint32_t pressed = g_guiPressed;
    g_guiPressed = 0;
    if (!hit) return;
    FireGuiMouseEvent("onClientGUIMouseUp", hit, nullptr, ev.x, ev.y);
    if (hit == pressed) FireGuiMouseEvent("onClientGUIClick", hit, "up", ev.x, ev.y);
}

// button, state, absoluteX, absoluteY, worldX, worldY, worldZ, clickedElement.
static void ApplyCursorEvent(const CursorEvent& ev) {
    float px = 0, py = 0, pz = 0, heading = 0;
    WorldRPG::Game::CopyPose(px, py, pz, heading);
    if (!GuiCursorShown()) return;  // a handler closed the panel; the rest of the gesture is not a click
    g_cursorX = ev.x;
    g_cursorY = ev.y;
    // Mouse-down activates the widget under the cursor (CEGUI). A gui-edit takes
    // the soft keyboard; a tap anywhere else blurs the edit that had it.
    if (ev.action == 0) {
        g_imeDismissed = false;
        const uint32_t hit = GuiPick(ev.x, ev.y);
        if (GuiIsEdit(hit)) GuiSetFocused(hit);
        else if (GuiFocusedEdit()) GuiSetFocused(0);
    }
    lua_State* L = g_dx.L;
    if (ev.action == 2) {
        const uint32_t hit = GuiPick(ev.x, ev.y);
        if (hit != g_guiHover) {
            const uint32_t prev = g_guiHover;
            g_guiHover = hit;
            if (prev && KnownClientElement(prev)) {
                lua_pushnumber(L, ev.x);
                lua_pushnumber(L, ev.y);
                if (hit && KnownClientElement(hit)) PushElement(L, hit);
                else lua_pushnil(L);
                ActiveVmFireElementEvent(L, "onClientMouseLeave", prev, 3);
            }
            if (hit && KnownClientElement(hit)) {
                lua_pushnumber(L, ev.x);
                lua_pushnumber(L, ev.y);
                if (prev && KnownClientElement(prev)) PushElement(L, prev);
                else lua_pushnil(L);
                ActiveVmFireElementEvent(L, "onClientMouseEnter", hit, 3);
            }
        }
        if (hit && KnownClientElement(hit)) {
            lua_pushnumber(L, ev.x);
            lua_pushnumber(L, ev.y);
            ActiveVmFireElementEvent(L, "onClientMouseMove", hit, 2);
        }
        if (g_dx.handlers.find("onClientCursorMove") == g_dx.handlers.end()) return;
        lua_pushnumber(L, g_dx.screenW > 0 ? ev.x / g_dx.screenW : 0.0);
        lua_pushnumber(L, g_dx.screenH > 0 ? ev.y / g_dx.screenH : 0.0);
        lua_pushnumber(L, ev.x);
        lua_pushnumber(L, ev.y);
        lua_pushnumber(L, px);
        lua_pushnumber(L, py);
        lua_pushnumber(L, pz);
        fireHandlers("onClientCursorMove", 7, ClientRootElement());
        return;
    }
    // The GUI sees the message first (CMessageLoopHook::ProcessMessage 512-523),
    // then the client's onClientClick, which desktop fires over widgets too.
    ApplyGuiMouse(ev);
    if (!GuiCursorShown()) return;
    lua_pushstring(L, "left");
    lua_pushstring(L, ev.action == 0 ? "down" : "up");
    lua_pushnumber(L, ev.x);
    lua_pushnumber(L, ev.y);
    lua_pushnumber(L, px);
    lua_pushnumber(L, py);
    lua_pushnumber(L, pz);
    lua_pushboolean(L, 0);
    fireHandlers("onClientClick", 8, ClientRootElement());
    if (ev.action == 0) {
        const uint64_t now = DxNow();
        const bool dbl = (g_rootLastClickMs != 0 && now - g_rootLastClickMs <= kGuiDoubleClickMs);
        g_rootLastClickMs = dbl ? 0 : now;
        if (dbl) {
            lua_pushstring(L, "left");
            lua_pushnumber(L, ev.x);
            lua_pushnumber(L, ev.y);
            lua_pushnumber(L, px);
            lua_pushnumber(L, py);
            lua_pushnumber(L, pz);
            lua_pushboolean(L, 0);
            fireHandlers("onClientDoubleClick", 7, ClientRootElement());
        }
    }
}

void ActiveVmOnMouseWheel(int upOrDown, float x, float y) {
    lua_State* L = g_dx.L;
    if (!L || !GuiCursorShown()) return;
    uint32_t hit = GuiPick(x, y);
    if (!hit) hit = g_guiHover;
    if (!hit || !KnownClientElement(hit)) return;
    lua_pushinteger(L, upOrDown >= 0 ? 1 : -1);
    ActiveVmFireElementEvent(L, "onClientMouseWheel", hit, 1);
}

void ActiveVmOnPaste(const char* text) {
    lua_State* L = g_dx.L;
    if (!L) return;
    lua_pushstring(L, text ? text : "");
    ActiveVmFireElementEvent(L, "onClientPaste", ClientRootElement(), 1);
}

bool ActiveVmOnCoreCommand(const char* command, const char* args) {
    lua_State* L = g_dx.L;
    if (!L || !command) return true;
    lua_pushstring(L, command);
    lua_pushstring(L, args ? args : "");
    return ActiveVmFireElementEvent(L, "onClientCoreCommand", ClientRootElement(), 2);
}

void ActiveVmOnPlayerTarget(uint32_t targetId) {
    lua_State* L = g_dx.L;
    if (!L) return;
    const uint32_t local = WorldRPG::ServerWorld::LocalPlayer();
    if (!local) return;
    if (targetId) PushElement(L, targetId);
    else lua_pushboolean(L, 0);
    ActiveVmFireElementEvent(L, "onClientPlayerTarget", local, 1);
}

void ActiveVmOnPlayerStunt(bool start, const char* stuntType, int timeMs, float distance) {
    lua_State* L = g_dx.L;
    if (!L) return;
    const uint32_t local = WorldRPG::ServerWorld::LocalPlayer();
    if (!local) return;
    lua_pushstring(L, stuntType ? stuntType : "");
    if (start) {
        ActiveVmFireElementEvent(L, "onClientPlayerStuntStart", local, 1);
    } else {
        lua_pushinteger(L, timeMs);
        lua_pushnumber(L, distance);
        ActiveVmFireElementEvent(L, "onClientPlayerStuntFinish", local, 3);
    }
}

bool ActiveVmOnNativeDamageAttempt(uint32_t pedId, bool isPlayer, uint32_t attackerId, int weapon, int bodypart,
                                   float loss) {
    lua_State* L = g_dx.L;
    if (!L || !pedId) return true;
    if (attackerId) PushElement(L, attackerId);
    else lua_pushnil(L);
    lua_pushinteger(L, weapon);
    lua_pushinteger(L, bodypart);
    lua_pushnumber(L, loss);
    if (isPlayer) return ActiveVmFireElementEvent(L, "onClientPlayerNativeDamageAttempt", pedId, 4);
    return ActiveVmFireElementEvent(L, "onClientPedNativeDamageAttempt", pedId, 4);
}

bool ActiveVmOnWeaponReload(uint32_t pedId, bool isPlayer, int weapon, int ammoInClip) {
    lua_State* L = g_dx.L;
    if (!L || !pedId) return true;
    lua_pushinteger(L, weapon);
    lua_pushinteger(L, ammoInClip);
    if (isPlayer) return ActiveVmFireElementEvent(L, "onClientPlayerWeaponReload", pedId, 2);
    return ActiveVmFireElementEvent(L, "onClientPedWeaponReload", pedId, 2);
}

bool ActiveVmOnPedChoke(uint32_t pedId, bool isPlayer, int weapon, uint32_t responsiblePedId) {
    lua_State* L = g_dx.L;
    if (!L || !pedId) return true;
    lua_pushinteger(L, weapon);
    if (responsiblePedId) PushElement(L, responsiblePedId);
    else lua_pushnil(L);
    if (isPlayer) return ActiveVmFireElementEvent(L, "onClientPlayerChoke", pedId, 2);
    return ActiveVmFireElementEvent(L, "onClientPedChoke", pedId, 2);
}

bool ActiveVmOnPlayerStealthKill(uint32_t targetPedId) {
    lua_State* L = g_dx.L;
    if (!L) return true;
    const uint32_t local = WorldRPG::ServerWorld::LocalPlayer();
    if (!local || !targetPedId) return true;
    PushElement(L, targetPedId);
    return ActiveVmFireElementEvent(L, "onClientPlayerStealthKill", local, 1);
}

bool ActiveVmOnHitByWaterCannon(uint32_t cannonVehId, uint32_t pedId, bool isPlayer) {
    lua_State* L = g_dx.L;
    if (!L || !cannonVehId || !pedId) return true;
    PushElement(L, pedId);
    if (isPlayer) return ActiveVmFireElementEvent(L, "onClientPlayerHitByWaterCannon", cannonVehId, 1);
    return ActiveVmFireElementEvent(L, "onClientPedHitByWaterCannon", cannonVehId, 1);
}

bool ActiveVmOnHeliKilled(uint32_t pedId, bool isPlayer, uint32_t heliVehId) {
    lua_State* L = g_dx.L;
    if (!L || !pedId) return true;
    if (heliVehId) PushElement(L, heliVehId);
    else lua_pushnil(L);
    if (isPlayer) return ActiveVmFireElementEvent(L, "onClientPlayerHeliKilled", pedId, 1);
    return ActiveVmFireElementEvent(L, "onClientPedHeliKilled", pedId, 1);
}

bool ActiveVmOnPedNativeBikeJackAttempt(uint32_t jackerId, uint32_t vehicleId) {
    lua_State* L = g_dx.L;
    if (!L || !jackerId) return true;
    if (vehicleId) PushElement(L, vehicleId);
    else lua_pushnil(L);
    return ActiveVmFireElementEvent(L, "onClientPedNativeBikeJackAttempt", jackerId, 1);
}

void ActiveVmOnPedCarryStateChange(uint32_t pedId, bool carrying, uint32_t objectId) {
    lua_State* L = g_dx.L;
    if (!L || !pedId) return;
    lua_pushboolean(L, carrying ? 1 : 0);
    if (objectId) PushElement(L, objectId);
    else lua_pushnil(L);
    ActiveVmFireElementEvent(L, "onClientPedCarryStateChange", pedId, 2);
}

void ActiveVmOnPedStep(uint32_t pedId, bool leftFoot) {
    lua_State* L = g_dx.L;
    if (!L || !pedId) return;
    lua_pushboolean(L, leftFoot ? 1 : 0);
    ActiveVmFireElementEvent(L, "onClientPedStep", pedId, 1);
}

void ActiveVmOnVehicleCollision(uint32_t vehicleId, uint32_t hitElementId, float force, int bodyPart,
                                float x, float y, float z, float nx, float ny, float nz,
                                float hitForce, int model) {
    lua_State* L = g_dx.L;
    if (!L || !vehicleId) return;
    if (hitElementId) PushElement(L, hitElementId);
    else lua_pushnil(L);
    lua_pushnumber(L, force);
    lua_pushinteger(L, bodyPart);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    lua_pushnumber(L, nx);
    lua_pushnumber(L, ny);
    lua_pushnumber(L, nz);
    lua_pushnumber(L, hitForce);
    lua_pushinteger(L, model);
    ActiveVmFireElementEvent(L, "onClientVehicleCollision", vehicleId, 11);
}

void ActiveVmOnVehicleWeaponHit(uint32_t vehicleId, int weaponType, uint32_t hitElementId,
                                float x, float y, float z, int model, int material) {
    lua_State* L = g_dx.L;
    if (!L || !vehicleId) return;
    lua_pushinteger(L, weaponType);
    if (hitElementId) PushElement(L, hitElementId);
    else lua_pushnil(L);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    lua_pushinteger(L, model);
    lua_pushinteger(L, material);
    ActiveVmFireElementEvent(L, "onClientVehicleWeaponHit", vehicleId, 7);
}

std::string LuaVMStub::pumpDx(bool renderFrame) {
    if (!pImpl_->L) return {};
    ServerRpc::Pump();       // server RPCs that write Lua-owned state (camera, world, binds, objects)
    WorldRPG::Audio::VoiceRuntime::Pump([L = pImpl_->L](const char* eventName, uint32_t playerId) {
        lua_getglobal(L, "source");
        PushElement(L, playerId);
        lua_setglobal(L, "source");
        const bool allowed = fireHandlers(eventName, 0, playerId, L);
        lua_setglobal(L, "source");
        return allowed;
    });
    // CClientGame::OnWindowFocusChange (CClientGame.cpp:8069-8078). One report per pump.
    bool windowFocused = false;
    if (MtaWindowTakeFocusChange(windowFocused)) {
        lua_getglobal(pImpl_->L, "source");
        PushElement(pImpl_->L, ClientRootElement());
        lua_setglobal(pImpl_->L, "source");
        lua_pushboolean(pImpl_->L, windowFocused ? 1 : 0);
        fireHandlers("onClientMTAFocusChange", 1, ClientRootElement());
        if (!windowFocused) {
            fireHandlers("onClientMinimize", 0, ClientRootElement());
        } else {
            lua_pushboolean(pImpl_->L, 0);
            fireHandlers("onClientRestore", 1, ClientRootElement());
        }
        lua_setglobal(pImpl_->L, "source");
    }
    const auto now = DxNow();
    // Index loop: a timer callback may call setTimer, which reallocates g_dx.timers.
    for (size_t i = 0; i < g_dx.timers.size(); ++i) {
        if (g_dx.timers[i].times == 0 || g_dx.timers[i].paused || now < g_dx.timers[i].next) continue;
        {
            OwnerScope scope(g_dx.timers[i].owner, g_dx.timers[i].netId, g_dx.timers[i].generation);
            lua_State* T = RefVmOr(g_dx.timers[i].ref, pImpl_->L);
            VmScope vm(T);
            Vms::PushRef(T, g_dx.timers[i].ref);
            if (lua_pcall(T, 0, 0, 0) != LUA_OK) {
                LUA_LOGE("Lua timer: %s", lua_tostring(T, -1));
                NoteScriptError("timer", lua_tostring(T, -1));
                lua_pop(T, 1);
            }
        }
        g_dx.timers[i].next = now + static_cast<std::uint64_t>(g_dx.timers[i].interval);
        if (g_dx.timers[i].times > 0 && --g_dx.timers[i].times == 0) Vms::Unref(g_dx.timers[i].ref);  // expired
    }
    g_dx.timers.erase(std::remove_if(g_dx.timers.begin(), g_dx.timers.end(),
        [](const DxRuntime::Timer& timer) { return timer.times == 0; }), g_dx.timers.end());
    std::vector<UiInput> inputs;
    {
        std::lock_guard lock(g_inputMutex);
        inputs.swap(g_inputQueue);
    }
    for (const auto& input : inputs) {
        if (const auto* touch = std::get_if<CursorEvent>(&input)) ApplyCursorEvent(*touch);
        else if (const auto* keyboard = std::get_if<ScriptInput>(&input)) ApplyScriptInput(*keyboard);
        else {
            const auto& shot = std::get<NativeWeaponFire>(input);
            if (shot.player ? shot.ped != ServerWorld::LocalPlayer() : !ServerWorld::ActorHosted(shot.ped)) continue;
            lua_State* L = pImpl_->L;
            lua_getglobal(L, "source");
            PushElement(L, shot.ped);
            lua_setglobal(L, "source");
            lua_pushinteger(L, shot.weapon);
            lua_pushinteger(L, shot.total);
            lua_pushinteger(L, shot.clip);
            lua_pushnumber(L, shot.x);
            lua_pushnumber(L, shot.y);
            lua_pushnumber(L, shot.z);
            if (shot.hit != 0xFFFFFFFFu) PushElement(L, shot.hit);
            else lua_pushnil(L);
            if (shot.player) {
                lua_pushnumber(L, shot.sx);
                lua_pushnumber(L, shot.sy);
                lua_pushnumber(L, shot.sz);
            }
            fireHandlers(shot.player ? "onClientPlayerWeaponFire" : "onClientPedWeaponFire", shot.player ? 10 : 7, shot.ped, nullptr, true);
            lua_setglobal(L, "source");
        }
    }
    ApplyKeyEvents();
    ApplyChatCommands();
    PumpEndedSounds();
    BrowserPumpEvents(g_dx.L);
    RunDeviceSelfTest();
    ApplySvgUpdates();
    g_dx.frame.clear();
    g_dx.postGuiFrame.clear();
    if (!renderFrame) {
        PulseSpatial();
        PublishClientObjects();
        return {};
    }
    DxBeginFrame(g_dx.frame);
    PulseCamera();
    PulseSpatial();
    PulseLights();
    // login_camera.lua moves the tour in onClientPreRender, before the DX menu.
    // CClientGame.cpp:915 + CMapEvent.cpp:26: fire onClientHUDRender before onClientRender.
    DxSetAspectRatioAdjustmentAllowed(true);
    fireHandlers("onClientPreRender", 0, ClientRootElement());
    DxSetAspectRatioAdjustmentAllowed(false);
    fireHandlers("onClientPedsProcessed", 0, ClientRootElement());
    if (!WorldRPG::Debug::Flag("nodx")) {
        DxSetAspectRatioAdjustmentAllowed(true);
        fireHandlers("onClientHUDRender", 0, ClientRootElement());
        DxSetAspectRatioAdjustmentAllowed(false);
        DxSetAspectRatioAdjustmentAllowed(true);
        fireHandlers("onClientRender", 0, ClientRootElement());
        DxSetAspectRatioAdjustmentAllowed(false);
    }
    // Publish after pre-render/render so objects repositioned in onClientPreRender
    // (e.g. attached weapon holsters/bandoliers) reach the game thread on the same pulse.
    PublishClientObjects();
    DxEndRender(g_dx.frame);
    GuiAppendDraws(g_dx.frame);
    NativeUiAppendDraws(g_dx.frame, g_dx.screenW, g_dx.screenH);
    g_dx.frame += g_dx.postGuiFrame;
    WorldRPG::CorePackets::AppendServerTextItems(g_dx.frame, g_dx.screenW, g_dx.screenH);
    // "C" line: the cursor is showing, so the Android overlay takes touches (MtaDxView).
    // "K" line: a gui-edit has focus, or a script called guiSetInputEnabled(true), so the
    // activity opens the soft keyboard. EngineHostActivity reads that line.
    std::string out = GuiCursorShown() ? "C\n" + g_dx.frame : g_dx.frame;
    const bool wantKeys = !g_imeDismissed && (GuiFocusedEdit() != 0 || (GuiCursorShown() && GuiKeyCapture()));
    if (wantKeys) {
        const char* keyboard = GuiFocusedMemo() ? "K multiline\n" : "K\n";
        if (out.compare(0, 2, "C\n") == 0) out.insert(2, keyboard);
        else out.insert(0, keyboard);
    }
    if (!out.empty()) {
        // Frame-local UI coordinates: Java scales the screen canvas and inversely
        // maps touches. Render targets retain their explicit bitmap dimensions.
        const std::string viewport = "V " + std::to_string(g_dx.screenW) + " " + std::to_string(g_dx.screenH) + "\n";
        out.insert(out.compare(0, 2, "C\n") == 0 ? 2 : 0, viewport);
    }
    if (out.size() > 24000) {
        // Cut at the end of a line, so the overlay never parses half a draw command.
        const size_t cut = out.rfind('\n', 24000);
        out.resize(cut == std::string::npos ? 0 : cut + 1);
    }
    std::string clean;
    clean.reserve(out.size());
    for (unsigned char ch : out) {
        if (ch == '\n' || (ch >= 32 && ch != 127)) clean.push_back(static_cast<char>(ch));
    }
    return clean;
}

void LuaVMStub::shutdown() {
    {
        std::lock_guard lock(g_inputMutex);
        g_inputQueue.clear();
    }
    if (!pImpl_->initialized) return;
    ResetModelAssets();
    AudioStopOwnedSounds();
    Projectiles::Reset();
    ResetSpatial();
    DxReset();
    if (g_active == this) g_active = nullptr;
    g_imeDismissed = false;
    g_dx = DxRuntime{};
    Events::Reset();
    GuiReset();
    RocketSoccerReset();
    WorldObjects::Reset();
    WeaponsReset();

    g_resourceVm.clear();
    if (pImpl_->L51) {
        lua_close(pImpl_->L51);
        pImpl_->L51 = nullptr;
        Vms::SetLua51(nullptr);
    }
    if (pImpl_->L) {
        lua_close(pImpl_->L);
        pImpl_->L = nullptr;
        Vms::SetPrimary(nullptr);
        // __gc handlers can call installed DX functions during lua_close.
        // Their late handles must also be invalidated after the registry dies.
        DxReset();
    }

    AudioResetSession();
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
    return reader.ReadCompressed(out, bytes);
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

void ActiveVmFirePlayerNetworkStatus(uint32_t player, uint8_t type, uint32_t ticks) {
    lua_State* L = g_dx.L;
    if (!L) return;
    refreshLocalPlayer(L);
    lua_getglobal(L, "source");
    l_getLocalPlayer(L);
    lua_setglobal(L, "source");
    lua_pushnumber(L, type);
    lua_pushnumber(L, ticks);
    fireHandlers("onClientPlayerNetworkStatus", 2, player);
    lua_setglobal(L, "source");
}

bool ActiveVmFireElementEvent(lua_State* L, const char* name, uint32_t source, int nargs) {
    if (!L || !source || source == 0xFFFFFFFFu) {
        if (L) lua_pop(L, nargs);
        return true;
    }
    const auto found = g_dx.handlers.find(name);
    if (found == g_dx.handlers.end() || found->second.empty()) {
        lua_pop(L, nargs);
        return true;
    }
    lua_getglobal(L, "source");
    lua_insert(L, -nargs - 1);
    PushElement(L, source);
    lua_setglobal(L, "source");
    const bool ran = fireHandlers(name, nargs, source, L, true);
    lua_setglobal(L, "source");
    return ran;
}

static bool KnownEventElement(uint32_t id) {
    return id != 0 && id != 0xFFFFFFFFu &&
           (id == ServerWorld::LocalPlayer() || ServerWorld::IsKnownPlayer(id) || ServerWorld::ModelOf(id) >= 0 ||
            KnownClientElement(id));
}

void ActiveVmFirePlayerSpawn(uint32_t player, uint32_t team) {
    lua_State* L = g_dx.L;
    if (!L) return;
    // CPacketHandler::Packet_PlayerSpawn (CPacketHandler.cpp 1267-1273): the team, or false.
    if (KnownEventElement(team)) PushElement(L, team);
    else lua_pushboolean(L, 0);
    ActiveVmFireElementEvent(L, "onClientPlayerSpawn", player, 1);
}

void ActiveVmFireWasted(bool player, uint32_t ped, uint32_t killer, int weapon, int bodypart, bool stealth,
                        int animGroup, int animId) {
    lua_State* L = g_dx.L;
    if (!L) return;
    // CPacketHandler::Packet_PlayerWasted (CPacketHandler.cpp 1336-1357). The 6-bit weapon and the
    // 3-bit body part (SBodypartSync adds 3) are never 0xFF on the wire, so both are numbers.
    if (KnownEventElement(killer)) PushElement(L, killer);
    else lua_pushboolean(L, 0);
    lua_pushnumber(L, weapon);
    lua_pushnumber(L, bodypart);
    lua_pushboolean(L, stealth ? 1 : 0);
    lua_pushnumber(L, animGroup);
    lua_pushnumber(L, animId);
    ActiveVmFireElementEvent(L, player ? "onClientPlayerWasted" : "onClientPedWasted", ped, 6);
}

static void OnPlayerJoinHook(uint32_t id) { ActiveVmFireElementEvent(g_dx.L, "onClientPlayerJoin", id, 0); }

static void OnPlayerQuitHook(uint32_t id, const char* reason) {
    if (!g_dx.L) return;
    lua_pushstring(g_dx.L, reason);
    ActiveVmFireElementEvent(g_dx.L, "onClientPlayerQuit", id, 1);
    DropClientElementData(Vms::Primary(), id);
}

static void OnElementDestroyHook(uint32_t id) {
    ActiveVmFireElementEvent(g_dx.L, "onClientElementDestroy", id, 0);
    DropClientElementData(Vms::Primary(), id);
}

void ActiveVmFireDestroySubtree(lua_State* L, uint32_t id, bool (*deletes)(uint32_t)) {
    // CElementDeleter::Delete: the element's event, then DeleteClientChildren deletes
    // (and fires for) each child, in tree order. Only elements that will be deleted.
    if (!deletes(id)) return;
    std::vector<uint32_t> doomed{id};
    std::vector<uint32_t> below;
    CollectEventDescendants(id, below);
    for (uint32_t child : below)
        if (deletes(child)) doomed.push_back(child);
    for (uint32_t element : doomed) ActiveVmFireElementEvent(L, "onClientElementDestroy", element, 0);
    for (uint32_t element : doomed) DropClientElementData(Vms::Primary(), element);
}

static void OnDataChangeHook(const ServerWorld::ElementDataChange& change) {
    if (Vms::Primary()) ApplyServerElementData(Vms::Primary(), change);
}

void ActiveVmFireVehicleSeat(bool entered, uint32_t ped, uint32_t vehicle, uint8_t seat) {
    lua_State* L = g_dx.L;
    if (!L) return;
    const uint32_t local = ServerWorld::LocalPlayer();
    refreshLocalPlayer(L);
    auto pushActor = [&](uint32_t id) {
        if (id == local) l_getLocalPlayer(L);
        else PushElement(L, id);
    };
    const bool player = std::strcmp(ServerWorld::ElementKindName(ped), "player") == 0;
    auto dispatch = [&](const char* name, uint32_t source, uint32_t other) {
        lua_getglobal(L, "source");
        pushActor(source);
        lua_setglobal(L, "source");
        pushActor(other);
        lua_pushnumber(L, seat);
        if (!entered) lua_pushboolean(L, 0);  // jacker
        fireHandlers(name, entered ? 2 : 3, source, nullptr, true);
        lua_setglobal(L, "source");
    };
    if (entered) {
        dispatch(player ? "onClientPlayerVehicleEnter" : "onClientPedVehicleEnter", ped, vehicle);
        dispatch("onClientVehicleEnter", vehicle, ped);
    } else {
        dispatch(player ? "onClientPlayerVehicleExit" : "onClientPedVehicleExit", ped, vehicle);
        dispatch("onClientVehicleExit", vehicle, ped);
    }
}

bool ActiveVmFireVehicleStartInOut(bool entering, uint32_t ped, uint32_t vehicle, uint8_t seat, uint8_t door) {
    if (!g_dx.L || !ped || !vehicle) return true;
    lua_State* L = g_dx.L;
    const uint32_t local = ServerWorld::LocalPlayer();
    refreshLocalPlayer(L);
    lua_getglobal(L, "source");
    PushElement(L, vehicle);
    lua_setglobal(L, "source");
    if (ped == local) l_getLocalPlayer(L);
    else PushElement(L, ped);
    lua_pushnumber(L, seat);
    lua_pushnumber(L, door);
    const bool allowed = fireHandlers(entering ? "onClientVehicleStartEnter" : "onClientVehicleStartExit",
                                      3, vehicle, nullptr, true);
    lua_setglobal(L, "source");
    return allowed;
}

void ActiveVmFireElementStream(uint32_t id, bool streamedIn) {
    if (!g_dx.L || !id) return;
    lua_State* L = g_dx.L;
    lua_getglobal(L, "source");
    PushElement(L, id);
    lua_setglobal(L, "source");
    fireHandlers(streamedIn ? "onClientElementStreamIn" : "onClientElementStreamOut", 0, id, nullptr, true);
    lua_setglobal(L, "source");
}

void ActiveVmFireSpatialEvent(const char* name,uint32_t source,uint32_t other,bool matching) {
    if(!g_dx.L) return;
    lua_State* L=g_dx.L;
    lua_getglobal(L,"source"); PushElement(L,source); lua_setglobal(L,"source");
    PushElement(L,other); lua_pushboolean(L,matching);
    fireHandlers(name, 2, source, nullptr, true);
    lua_setglobal(L,"source");
}

void QueueNativeWeaponFire(NativeWeaponFire shot) {
    std::lock_guard lock(g_inputMutex);
    if (g_inputQueue.size() < 256) g_inputQueue.emplace_back(shot);
}

bool ActiveVmFireBirdShot(uint32_t bird, uint32_t attacker, int weapon, float x, float y, float z) {
    if (!g_dx.L) return true;
    lua_State* L = g_dx.L;
    lua_getglobal(L, "source");
    PushElement(L, bird);
    lua_setglobal(L, "source");
    if (attacker) PushElement(L, attacker);
    else lua_pushnil(L);
    lua_pushnumber(L, weapon);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    const bool allowed = fireHandlers("onClientBirdShot", 5, bird, nullptr, true);
    lua_setglobal(L, "source");
    return allowed;
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
    // CPacketHandler::Packet_LuaEvent (CPacketHandler.cpp:5226-5244): the event must exist and
    // be marked as remotely triggerable.
    const Events::Remote remote = Events::RemoteState(name);
    if (remote != Events::Remote::Allowed) {
        LUA_LOGE(remote == Events::Remote::NotAdded
                     ? "ERROR: Server triggered clientside event %s, but event is not added clientside"
                     : "ERROR: Server triggered clientside event %s, but event is not marked as remotely triggerable",
                 name.c_str());
        lua_settop(L, top);
        return true;
    }
    PushElement(L, source);
    lua_setglobal(L, "source");
    const auto handlers = g_dx.handlers.find(name);
    if (handlers == g_dx.handlers.end() || handlers->second.empty()) {
        LUA_LOGI("LuaVM: server event %s has no client handler (%u args)", name.c_str(), static_cast<unsigned>(count));
    }
    // CPacketHandler::Packet_LuaEvent (CPacketHandler.cpp:5231): CallEvent(..., true).
    fireHandlers(name.c_str(), static_cast<int>(count), source ? source : ClientRootElement(), nullptr, true);
    return true;
}

// Removes the handlers and timers that a resource registered, and the resource's
// Lua state with them (its environment is only referenced by those closures).
static void DropResourceState(lua_State* L, const std::string& name, uint16_t netId, uint32_t generation) {
    SpatialDropResource(name, netId, generation);
    Projectiles::DropOwner(name, netId, generation);
    AudioStopOwnedSounds(&name, netId, generation);
    GuiDropResource(name, netId, generation);
    RocketSoccerDropResource(generation);
    NativeUiDropResource(name, netId, generation);
    DropResourceModelAssets(generation);
    DropResourceEnvironment(ResourceVm(generation), generation);
    Events::DropOwner(name, netId, generation);
    KeyBindsDropResource(L, name, netId, generation);
    CommandHandlersDropResource(L, name, netId, generation);
    DxDropResource(name, netId, generation);
    WeaponsDropOwner(name, netId, generation);
    CryptDropOwner(name, netId, generation);
    g_resourceVm.erase(generation);
    for (auto& [event, refs] : g_dx.handlers) {
        refs.erase(std::remove_if(refs.begin(), refs.end(), [&](const DxRuntime::Handler& handler) {
            if (OwnerOfHandler(handler.ref) != name) return false;
            if (netId != 0 && handler.netId != 0 && handler.netId != netId) return false;
            if (generation != 0 && handler.generation != 0 && handler.generation != generation) return false;
            Vms::Unref(handler.ref);
            g_dx.handlerOwner.erase(handler.ref);
            return true;
        }), refs.end());
    }
    for (auto& timer : g_dx.timers) {
        if (timer.times == 0) continue;  // expired this pulse: its ref is already released
        if (timer.owner != name) continue;
        if (netId != 0 && timer.netId != 0 && timer.netId != netId) continue;
        if (generation != 0 && timer.generation != 0 && timer.generation != generation) continue;
        Vms::Unref(timer.ref);
        timer.times = 0;
    }
    g_dx.timers.erase(std::remove_if(g_dx.timers.begin(), g_dx.timers.end(),
        [](const DxRuntime::Timer& timer) { return timer.times == 0; }), g_dx.timers.end());
    const auto chat = g_dx.handlers.find("onClientChatMessage");
    g_dx.chatHooked = chat != g_dx.handlers.end() && !chat->second.empty();
}

// Desktop CResource::Stop: onClientResourceStop runs with the resource root as source and
// argument, then the resource's state is dropped. Only that resource's handlers run.
static void RunResourceStop(lua_State* L, const std::string& name, uint32_t root, uint16_t netId, uint32_t generation) {
    PushElement(L, root);
    lua_setglobal(L, "source");
    const auto found = g_dx.handlers.find("onClientResourceStop");
    if (found != g_dx.handlers.end()) {
        const std::vector<DxRuntime::Handler> refs = found->second;
        std::vector<uint32_t> chain;
        CollectEventChain(root ? root : ClientRootElement(), chain);
        for (const DxRuntime::Handler& handler : refs) {
            const auto live = g_dx.handlers.find("onClientResourceStop");
            if (live == g_dx.handlers.end() || !HandlerLive(live->second, handler.ref)) continue;
            if (OwnerOfHandler(handler.ref) != name) continue;
            if (netId != 0 && handler.netId != 0 && handler.netId != netId) continue;
            if (generation != 0 && handler.generation != 0 && handler.generation != generation) continue;
            bool match = false;
            for (size_t i = 0; i < chain.size(); ++i) {
                if (handler.element != chain[i]) continue;
                if (i > 0 && !handler.propagated) break;
                match = true;
                break;
            }
            if (!match) continue;
            OwnerScope scope(name, handler.netId, handler.generation);
            lua_State* T = RefVmOr(handler.ref, L);
            VmScope vm(T);
            const bool other = T != L;  // source was set in L above
            if (other) {
                lua_getglobal(T, "source");
                PushElement(T, root);
                lua_setglobal(T, "source");
            }
            Vms::PushRef(T, handler.ref);
            // CResource::Stop (CResource.cpp 1651-1653) passes the resource. source
            // stays the resource root, set above.
            const bool pushed = PushResourceInstance(T, netId, generation);
            if (!pushed) PushElement(T, root);
            if (lua_pcall(T, 1, 0, 0) != LUA_OK) {
                LUA_LOGE("Lua onClientResourceStop (%s): %s", name.c_str(), lua_tostring(T, -1));
                NoteScriptError(name.c_str(), lua_tostring(T, -1));
                lua_pop(T, 1);
            }
            if (other) lua_setglobal(T, "source");
        }
    }
    DropResourceState(L, name, netId, generation);
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
        if (gone.started) RunResourceStop(g_dx.L, gone.name, gone.rootElement, netId, gone.generation);
        else DropResourceState(g_dx.L, gone.name, netId, gone.generation);
        ReleaseResourceElements(gone.rootElement, gone.dynamicElement);
    }
    ResourceLoader::FinishStop(netId, gone.generation);
    ReleaseCameraOwner(gone.name);
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
    std::vector<ResourceLoader::FileDownloadEvent> downloads;
    for (const auto& job : ResourceLoader::Pump(downloads)) RunResourceJob(job);
    for (const auto& done : downloads) {
        ResourceLoader::ResourceView target;
        if (!ResourceLoader::CopyResourceByGeneration(done.generation, target) || !target.root) continue;
        lua_State* L = g_dx.L;
        lua_getglobal(L, "source");
        PushElement(L, target.root);
        lua_setglobal(L, "source");
        lua_pushlstring(L, done.file.data(), done.file.size());
        lua_pushboolean(L, done.ok);
        ResourceLoader::ResourceView requester;
        if (!ResourceLoader::CopyResourceByGeneration(done.requesterGeneration, requester) ||
            !PushResourceInstance(L, requester.netId, done.requesterGeneration))
            lua_pushboolean(L, 0);
        fireHandlers("onClientFileDownloadComplete", 3, target.root);
        lua_pushlstring(L, done.file.data(), done.file.size());
        lua_pushboolean(L, done.ok);
        if (!ResourceLoader::CopyResourceByGeneration(done.requesterGeneration, requester) ||
            !PushResourceInstance(L, requester.netId, done.requesterGeneration))
            lua_pushboolean(L, 0);
        fireHandlers("onClientResourceFileDownload", 3, target.root);
        lua_setglobal(L, "source");
    }
    FlushPendingChat(g_dx.L);
    return !wasHooked && g_dx.chatHooked;
}
void ActiveVmResetSession() {
    WorldRPG::Script::ResourceLoader::Reset();
    WorldRPG::Script::MtaWindowResetSession();
    WorldRPG::Audio::VoiceRuntime::Reset();
    if (!g_active) return;
    // A new session starts from a clean VM: no handlers, timers or globals from the old one.
    const int width = g_dx.screenW, height = g_dx.screenH;
    // The old session's scripts may have hidden HUD components; the new one starts with all shown.
    WorldRPG::Game::ResetHudComponents();
    // shutdown() clears g_active, so keep the VM pointer to initialize it again.
    LuaVMStub* const vm = g_active;
    vm->shutdown();
    vm->initialize();
    g_dx.screenW = width;
    g_dx.screenH = height;
}
bool ActiveVmOnChat(const char* text, int red, int green, int blue) {
    return g_active && g_active->onChatMessage(text, red, green, blue);
}
void ActiveVmAppendDraw(const std::string& lines) { g_dx.frame += lines; }
void ActiveVmAppendPostGuiDraw(const std::string& lines) { g_dx.postGuiFrame += lines; }
std::string ActiveVmResourceFile(const std::string& path) { return DxImageFile(path); }
lua_State* ActiveVmState() { return g_dx.L; }

void ActiveVmSetResourceLua(int mode) { g_resourceLua = mode >= 0 && mode <= 2 ? mode : 0; }

std::vector<std::string> ActiveVmTakeScriptErrors() {
    std::vector<std::string> errors;
    errors.swap(g_scriptErrors);
    return errors;
}

lua_State* ActiveVmResourceVm(uint32_t generation) {
    const auto found = g_resourceVm.find(generation);
    return found == g_resourceVm.end() ? nullptr : found->second;
}

void ActiveVmRunIn(lua_State* vm, const std::function<void()>& fn) {
    VmScope scope(vm);
    fn();
}

std::string ActiveVmPump(bool renderFrame) {
    return g_active ? g_active->pumpDx(renderFrame) : std::string();
}
void ActiveVmScreenSize(int& width, int& height) {
    width = g_dx.screenW;
    height = g_dx.screenH;
}
const std::string& ActiveVmOwner() {
    return g_dx.owner;
}

uint16_t ActiveVmOwnerNet() {
    return g_dx.ownerNetId;
}

uint32_t ActiveVmOwnerGeneration() {
    return g_dx.ownerGeneration;
}

void ActiveVmRunAsOwner(const std::string& owner, uint16_t netId, uint32_t generation, const std::function<void()>& fn) {
    OwnerScope scope(owner, netId, generation);
    fn();
}
void ActiveVmQueueCursor(int action, float x, float y) {
    std::lock_guard lock(g_inputMutex);
    if (action == 2 && !g_inputQueue.empty()) {
        if (auto* previous = std::get_if<CursorEvent>(&g_inputQueue.back()); previous && previous->action == 2) {
            *previous = {action, x, y};
            return;
        }
    }
    if (g_inputQueue.size() < 320) g_inputQueue.emplace_back(CursorEvent{action, x, y});
}
// onClientTransferBoxProgressChange(downloadedSizeTotal, downloadTotalBytes) and
// onClientTransferBoxVisibilityChange(isVisible) on the root, source the root
// (g_pClientGame->GetRootEntity()->CallEvent). Registered in CClientGame.cpp:2912-2913.
void ActiveVmFireTransferProgress(double downloadedBytes, double totalBytes) {
    if (!g_dx.L || g_dx.handlers.find("onClientTransferBoxProgressChange") == g_dx.handlers.end()) return;
    lua_getglobal(g_dx.L, "source");
    PushElement(g_dx.L, ClientRootElement());
    lua_setglobal(g_dx.L, "source");
    lua_pushnumber(g_dx.L, downloadedBytes);
    lua_pushnumber(g_dx.L, totalBytes);
    fireHandlers("onClientTransferBoxProgressChange", 2, ClientRootElement());
    lua_setglobal(g_dx.L, "source");
}

void ActiveVmFireTransferVisibility(bool visible) {
    if (!g_dx.L || g_dx.handlers.find("onClientTransferBoxVisibilityChange") == g_dx.handlers.end()) return;
    lua_getglobal(g_dx.L, "source");
    PushElement(g_dx.L, ClientRootElement());
    lua_setglobal(g_dx.L, "source");
    lua_pushboolean(g_dx.L, visible ? 1 : 0);
    fireHandlers("onClientTransferBoxVisibilityChange", 1, ClientRootElement());
    lua_setglobal(g_dx.L, "source");
}

void ActiveVmNoteSoundEnded(uint32_t id) { AudioNoteSoundEnded(id); }

void ActiveVmNoteSoundLength(uint32_t id, double seconds) { AudioNoteSoundLength(id, seconds); }

void ActiveVmNoteSoundStarted(uint32_t id, const char* reason) { AudioNoteSoundStarted(id, reason); }

void ActiveVmNoteSoundStream(uint32_t id, bool success, double length, const char* streamName, const char* errorMsg) {
    AudioNoteSoundStream(id, success, length, streamName, errorMsg);
}

void ActiveVmNoteSoundAnalysis(uint32_t id, int leftLevel, int rightLevel,
                               const float* fft, int fftCount,
                               const float* wave, int waveCount) {
    AudioNoteSoundAnalysis(id, leftLevel, rightLevel, fft, fftCount, wave, waveCount);
}

void ActiveVmQueueScriptInput(const char* text, int backspaces, const char* key) {
    ScriptInput ev;
    if (text) {
        const size_t n = std::strlen(text);
        ev.text.assign(text, n > 1024 ? 1024 : n);
    }
    ev.backspaces = std::clamp(backspaces, 0, 256);
    if (key && key[0]) {
        const size_t n = std::strlen(key);
        ev.key.assign(key, n > 32 ? 32 : n);
    }
    if (ev.text.empty() && ev.backspaces == 0 && ev.key.empty()) return;
    std::lock_guard<std::mutex> lock(g_inputMutex);
    if (g_inputQueue.size() < 320) g_inputQueue.emplace_back(std::move(ev));
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
