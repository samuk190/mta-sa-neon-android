#include "LuaKeyBindApi.h"

#include "LuaVMStub.h"

#include "lua.hpp"

#include <algorithm>
#include <mutex>
#include <strings.h>
#include <unordered_set>

namespace WorldRPG::Script {
namespace {

// CScriptKeyBinds.cpp:16-122 (g_bkKeys). F8 is the desktop console key and is not bindable.
constexpr const char* kKeys[] = {
    "mouse1", "mouse2", "mouse3", "mouse_wheel_up", "mouse_wheel_down", "mouse4", "mouse5",
    "backspace", "tab", "lshift", "rshift", "lctrl", "rctrl", "lalt", "ralt", "pause", "capslock",
    "enter", "space", "pgup", "pgdn", "end", "home", "arrow_l", "arrow_u", "arrow_r", "arrow_d",
    "insert", "delete", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
    "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
    "num_0", "num_1", "num_2", "num_3", "num_4", "num_5", "num_6", "num_7", "num_8", "num_9",
    "num_mul", "num_add", "num_enter", "num_sep", "num_sub", "num_dec", "num_div",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F9", "F10", "F11", "F12",
    "scroll", ";", "=", ",", "-", ".", "/", "'", "[", "\\", "]", "#",
};

// CScriptKeyBinds.cpp:124-169 (g_bcControls).
constexpr const char* kControls[] = {
    "fire", "next_weapon", "previous_weapon", "forwards", "backwards", "left", "right", "zoom_in",
    "zoom_out", "enter_exit", "change_camera", "jump", "sprint", "look_behind", "crouch", "action",
    "walk", "vehicle_fire", "vehicle_secondary_fire", "vehicle_left", "vehicle_right",
    "steer_forward", "steer_back", "accelerate", "brake_reverse", "radio_next", "radio_previous",
    "radio_user_track_skip", "horn", "sub_mission", "handbrake", "vehicle_look_left",
    "vehicle_look_right", "vehicle_look_behind", "vehicle_mouse_look", "special_control_left",
    "special_control_right", "special_control_down", "special_control_up", "aim_weapon",
    "conversation_yes", "conversation_no", "group_control_forwards", "group_control_back",
};

// CScriptKeyBinds::GetBindableFromKey / GetBindableFromControl: case-insensitive, table spelling.
template <size_t N>
const char* Find(const char* const (&table)[N], const char* name) {
    if (!name) return nullptr;
    for (const char* entry : table)
        if (strcasecmp(entry, name) == 0) return entry;
    return nullptr;
}

struct FunctionBind {
    uint64_t id = 0;
    const char* name = nullptr;  // points into kKeys or kControls
    bool control = false;
    bool down = true;
    int ref = LUA_NOREF;
    std::vector<int> args;
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};

struct CommandBind {
    const char* key = nullptr;
    std::string command;
    std::string arguments;
    bool down = true;
    std::string resource;
    // The instance that bound it, so stopping an old start keeps the binds of a newer one.
    uint16_t netId = 0;
    uint32_t generation = 0;
};

// VM thread only.
std::vector<FunctionBind> g_functions;
uint64_t g_nextBindId = 1;
std::vector<CommandBind> g_commands;
std::unordered_set<std::string> g_pressed;

struct KeyEvent {
    const char* key;
    bool down;
};
std::mutex g_queueMutex;
std::vector<KeyEvent> g_queue;

// Copy of the bound key names for the UI thread (KeyBindsBoundKeys).
std::mutex g_boundMutex;
std::string g_boundKeys;

// Call after every change to g_functions or g_commands.
void RefreshBoundKeys() {
    std::string out;
    for (const char* key : kKeys) {
        bool bound = false;
        for (const FunctionBind& bind : g_functions)
            if (!bind.control && bind.name == key) bound = true;
        for (const CommandBind& bind : g_commands)
            if (bind.key == key) bound = true;
        if (!bound) continue;
        if (!out.empty()) out.push_back('|');
        out += key;
    }
    std::lock_guard<std::mutex> lock(g_boundMutex);
    g_boundKeys = std::move(out);
}

void Unref(lua_State* L, const FunctionBind& bind) {
    luaL_unref(L, LUA_REGISTRYINDEX, bind.ref);
    for (int ref : bind.args) luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

bool SameInstance(const FunctionBind& bind) {
    return bind.owner == ActiveVmOwner() && bind.netId == ActiveVmOwnerNet() &&
           bind.generation == ActiveVmOwnerGeneration();
}

bool SameFunction(lua_State* L, int ref, int index) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    const bool same = lua_rawequal(L, -1, index) != 0;
    lua_pop(L, 1);
    return same;
}

// CScriptArgReader::ReadString / NextIsString: a string or a number.
bool IsStringArg(lua_State* L, int index) {
    const int type = lua_type(L, index);
    return type == LUA_TSTRING || type == LUA_TNUMBER;
}

int Fail(lua_State* L, const char* function, const char* expected, int index) {
    LUA_LOGI("WARNING: Bad argument @ '%s' [Expected %s at argument %d, got %s]", function, expected, index,
             lua_isnone(L, index) ? "none" : luaL_typename(L, index));
    lua_pushboolean(L, 0);
    return 1;
}

// "down" / "up" filter of unbindKey and the getters: any other text checks no state.
void HitFilter(const char* state, bool& check, bool& down) {
    check = false;
    down = true;
    if (!state) return;
    if (strcasecmp(state, "down") == 0) check = true;
    else if (strcasecmp(state, "up") == 0) {
        check = true;
        down = false;
    }
}

// CKeyBinds::CommandExists with key, state, arguments and resource checked.
bool CommandExists(const char* key, const char* command, bool down, const char* arguments, const std::string& resource) {
    for (const CommandBind& bind : g_commands) {
        if (bind.key == key && bind.down == down && bind.command == command && bind.arguments == arguments &&
            bind.resource == resource)
            return true;
    }
    return false;
}

// CStaticFunctionDefinitions::BindKey, command form (CStaticFunctionDefinitions.cpp:7278-7320).
bool BindCommand(const char* key, const char* state, const char* command, const char* arguments,
                 const std::string& resource) {
    bool success = false;
    if (CommandExists(key, command, true, arguments, resource)) return true;
    if (strcasecmp(state, "down") == 0 || strcasecmp(state, "both") == 0) {
        g_commands.push_back(CommandBind{key, command, arguments, true, resource, ActiveVmOwnerNet(), ActiveVmOwnerGeneration()});
        success = true;
    }
    if (CommandExists(key, command, false, arguments, resource)) return true;
    if (strcasecmp(state, "up") == 0 || strcasecmp(state, "both") == 0) {
        g_commands.push_back(CommandBind{key, command, arguments, false, resource, ActiveVmOwnerNet(), ActiveVmOwnerGeneration()});
        success = true;
    }
    return success;
}

// CLuaFunctionDefs::BindKey (lua/CLuaFunctionDefs.Input.cpp 148-200).
int BindKey(lua_State* L) {
    if (!IsStringArg(L, 1)) return Fail(L, "bindKey", "string", 1);
    if (!IsStringArg(L, 2)) return Fail(L, "bindKey", "string", 2);
    const char* name = lua_tostring(L, 1);
    const char* state = lua_tostring(L, 2);
    const char* key = Find(kKeys, name);

    if (IsStringArg(L, 3)) {
        // bindKey ( string key, string keyState, string commandName, [ string arguments ] )
        if (!lua_isnoneornil(L, 4) && !IsStringArg(L, 4)) return Fail(L, "bindKey", "string", 4);
        const char* arguments = lua_isnoneornil(L, 4) ? "" : lua_tostring(L, 4);
        const bool bound = key && BindCommand(key, state, lua_tostring(L, 3), arguments, ActiveVmOwner());
        RefreshBoundKeys();
        lua_pushboolean(L, bound ? 1 : 0);
        return 1;
    }

    // bindKey ( string key, string keyState, function handlerFunction, [ var arguments, ... ] )
    if (!lua_isfunction(L, 3)) return Fail(L, "bindKey", "function", 3);
    // CStaticFunctionDefinitions::BindKey (CStaticFunctionDefinitions.cpp:7229-7276): a key, else a control.
    const char* control = key ? nullptr : Find(kControls, name);
    const char* target = key ? key : control;
    const bool both = strcasecmp(state, "both") == 0;
    const bool wantDown = both || strcasecmp(state, "down") == 0;
    const bool wantUp = both || strcasecmp(state, "up") == 0;
    if (!target || (!wantDown && !wantUp)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int top = lua_gettop(L);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0 ? !wantDown : !wantUp) continue;
        FunctionBind bind;
        bind.id = g_nextBindId++;
        bind.name = target;
        bind.control = control != nullptr;
        bind.down = pass == 0;
        lua_pushvalue(L, 3);
        bind.ref = luaL_ref(L, LUA_REGISTRYINDEX);
        for (int i = 4; i <= top; ++i) {
            lua_pushvalue(L, i);
            bind.args.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
        }
        bind.owner = ActiveVmOwner();
        bind.netId = ActiveVmOwnerNet();
        bind.generation = ActiveVmOwnerGeneration();
        g_functions.push_back(std::move(bind));
    }
    RefreshBoundKeys();
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaFunctionDefs::UnbindKey (lua/CLuaFunctionDefs.Input.cpp 202-254).
int UnbindKey(lua_State* L) {
    if (!IsStringArg(L, 1)) return Fail(L, "unbindKey", "string", 1);
    const char* name = lua_tostring(L, 1);
    const char* key = Find(kKeys, name);

    if (IsStringArg(L, 3)) {
        // bool unbindKey ( string key, string keyState, string command )
        // CStaticFunctionDefinitions::UnbindKey (CStaticFunctionDefinitions.cpp:7361-7401).
        if (!IsStringArg(L, 2)) return Fail(L, "unbindKey", "string", 2);
        const char* state = lua_tostring(L, 2);
        const std::string command = lua_tostring(L, 3);
        bool check = false, down = true;
        HitFilter(state, check, down);
        bool success = false;
        auto remove = [&](bool hitState) {
            const auto before = g_commands.size();
            g_commands.erase(std::remove_if(g_commands.begin(), g_commands.end(), [&](const CommandBind& bind) {
                return bind.key == key && bind.command == command && bind.resource == ActiveVmOwner() &&
                       (!check || bind.down == hitState);
            }), g_commands.end());
            return g_commands.size() != before;
        };
        if (key) {
            if ((strcasecmp(state, "down") == 0 || strcasecmp(state, "both") == 0) && remove(down)) success = true;
            if ((strcasecmp(state, "up") == 0 || strcasecmp(state, "both") == 0) && remove(false)) success = true;
        }
        RefreshBoundKeys();
        lua_pushboolean(L, success ? 1 : 0);
        return 1;
    }

    // bool unbindKey ( string key, [ string keyState, function handler ] )
    // CStaticFunctionDefinitions::UnbindKey (7322-7359) and CScriptKeyBinds::RemoveKeyFunction.
    if (!lua_isnoneornil(L, 2) && !IsStringArg(L, 2)) return Fail(L, "unbindKey", "string", 2);
    if (!lua_isnoneornil(L, 3) && !lua_isfunction(L, 3)) return Fail(L, "unbindKey", "function", 3);
    const char* state = lua_isnoneornil(L, 2) ? nullptr : lua_tostring(L, 2);
    if (state && state[0] == '\0') state = nullptr;
    bool check = false, down = true;
    HitFilter(state, check, down);
    const bool anyFunction = lua_isnoneornil(L, 3);
    const char* control = key ? nullptr : Find(kControls, name);
    const char* target = key ? key : control;
    bool found = false;
    if (target) {
        g_functions.erase(std::remove_if(g_functions.begin(), g_functions.end(), [&](const FunctionBind& bind) {
            if (bind.name != target || !SameInstance(bind)) return false;
            if (check && bind.down != down) return false;
            if (!anyFunction && !SameFunction(L, bind.ref, 3)) return false;
            Unref(L, bind);
            found = true;
            return true;
        }), g_functions.end());
    }
    RefreshBoundKeys();
    lua_pushboolean(L, found ? 1 : 0);
    return 1;
}

// CLuaFunctionDefs::GetKeyState (256-276) and CKeyBinds::GetKeyStateByName: false for an unknown key.
int GetKeyState(lua_State* L) {
    if (!IsStringArg(L, 1)) return Fail(L, "getKeyState", "string", 1);
    const char* key = Find(kKeys, lua_tostring(L, 1));
    lua_pushboolean(L, key && g_pressed.count(key) != 0 ? 1 : 0);
    return 1;
}

// CLuaFunctionDefs.Input.cpp:363-418 and CKeyBinds.cpp:1030-1046, 1388-1405.
int GetBoundKeys(lua_State* L) {
    if (!IsStringArg(L, 1)) return Fail(L, "getBoundKeys", "string", 1);
    const char* name = lua_tostring(L, 1);
    // GTA_CONTROL binds map physical keys to controls; FunctionBind entries for
    // "fire" etc. are script callbacks, not such mappings. The phone currently
    // has no GTA_CONTROL keyboard mappings. A control name takes precedence over
    // a command of the same name, as on desktop, so never fall through here.
    if (Find(kControls, name)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    bool found = false;
    lua_newtable(L);
    for (const CommandBind& bind : g_commands) {
        if (strcasecmp(bind.command.c_str(), name) != 0) continue;
        // Deliberately not filtered by owner: desktop queries all command binds.
        // Later binds overwrite earlier states of the same key (including both).
        lua_pushstring(L, bind.key);
        lua_pushstring(L, bind.down ? "down" : "up");
        lua_settable(L, -3);
        found = true;
    }
    if (!found) {
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
    }
    return 1;
}

// CLuaFunctionDefs::GetFunctionsBoundToKey (420-497). The name is compared in table spelling.
int GetFunctionsBoundToKey(lua_State* L) {
    if (!IsStringArg(L, 1)) return Fail(L, "getFunctionsBoundToKey", "string", 1);
    if (!lua_isnoneornil(L, 2) && !IsStringArg(L, 2)) return Fail(L, "getFunctionsBoundToKey", "string", 2);
    const std::string name = lua_tostring(L, 1);
    bool check = false, down = true;
    HitFilter(lua_isnoneornil(L, 2) ? nullptr : lua_tostring(L, 2), check, down);
    lua_newtable(L);
    int index = 0;
    for (const FunctionBind& bind : g_functions) {
        if (check && bind.down != down) continue;
        if (name != bind.name) continue;
        lua_pushinteger(L, ++index);
        lua_rawgeti(L, LUA_REGISTRYINDEX, bind.ref);
        lua_settable(L, -3);
    }
    return 1;
}

// CLuaFunctionDefs::GetKeyBoundToFunction (499-554).
int GetKeyBoundToFunction(lua_State* L) {
    if (!lua_isfunction(L, 1)) return Fail(L, "getKeyBoundToFunction", "function", 1);
    for (const FunctionBind& bind : g_functions) {
        if (!SameFunction(L, bind.ref, 1)) continue;
        lua_pushstring(L, bind.name);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaFunctionDefs::GetCommandsBoundToKey (556-617): command name to arguments.
int GetCommandsBoundToKey(lua_State* L) {
    if (!IsStringArg(L, 1)) return Fail(L, "getCommandsBoundToKey", "string", 1);
    if (!lua_isnoneornil(L, 2) && !IsStringArg(L, 2)) return Fail(L, "getCommandsBoundToKey", "string", 2);
    const std::string name = lua_tostring(L, 1);
    bool check = false, down = true;
    HitFilter(lua_isnoneornil(L, 2) ? nullptr : lua_tostring(L, 2), check, down);
    lua_newtable(L);
    for (const CommandBind& bind : g_commands) {
        if (check && bind.down != down) continue;
        if (name != bind.key) continue;
        lua_pushstring(L, bind.command.c_str());
        lua_pushstring(L, bind.arguments.c_str());
        lua_settable(L, -3);
    }
    return 1;
}

// CLuaFunctionDefs::GetKeyBoundToCommand (619-655).
int GetKeyBoundToCommand(lua_State* L) {
    if (!IsStringArg(L, 1)) return Fail(L, "getKeyBoundToCommand", "string", 1);
    const std::string command = lua_tostring(L, 1);
    for (const CommandBind& bind : g_commands) {
        if (bind.command != command) continue;
        lua_pushstring(L, bind.key);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

}  // namespace

void KeyBindsDropResource(lua_State* L, const std::string& owner, uint16_t netId, uint32_t generation) {
    g_functions.erase(std::remove_if(g_functions.begin(), g_functions.end(), [&](const FunctionBind& bind) {
        if (bind.owner != owner) return false;
        if (netId != 0 && bind.netId != 0 && bind.netId != netId) return false;
        if (generation != 0 && bind.generation != 0 && bind.generation != generation) return false;
        Unref(L, bind);
        return true;
    }), g_functions.end());
    g_commands.erase(std::remove_if(g_commands.begin(), g_commands.end(), [&](const CommandBind& bind) {
        if (bind.resource != owner) return false;
        if (netId != 0 && bind.netId != 0 && bind.netId != netId) return false;
        if (generation != 0 && bind.generation != 0 && bind.generation != generation) return false;
        return true;
    }), g_commands.end());
    RefreshBoundKeys();
}

std::string KeyBindsBoundKeys() {
    std::lock_guard<std::mutex> lock(g_boundMutex);
    return g_boundKeys;
}

void KeyBindsQueueKey(const char* key, bool down) {
    const char* entry = Find(kKeys, key);
    if (!entry) return;
    std::lock_guard<std::mutex> lock(g_queueMutex);
    if (g_queue.size() < 256) g_queue.push_back(KeyEvent{entry, down});
}

bool KeyBindsNextKey(std::string& key, bool& down) {
    KeyEvent event{};
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        if (g_queue.empty()) return false;
        event = g_queue.front();
        g_queue.erase(g_queue.begin());
    }
    key = event.key;
    down = event.down;
    if (down) g_pressed.insert(key);
    else g_pressed.erase(key);
    return true;
}

void KeyBindsCollect(const std::string& key, bool down, std::vector<KeyBindCall>& calls,
                     std::vector<KeyBindCommand>& commands) {
    for (const FunctionBind& bind : g_functions) {
        if (bind.control || bind.down != down || key != bind.name) continue;
        calls.push_back(KeyBindCall{bind.id, bind.ref, bind.args, bind.owner, bind.netId, bind.generation});
    }
    for (const CommandBind& bind : g_commands) {
        if (bind.down != down || key != bind.key) continue;
        commands.push_back(KeyBindCommand{bind.command, bind.arguments});
    }
}

bool KeyBindLive(uint64_t id) {
    for (const FunctionBind& bind : g_functions)
        if (bind.id == id) return true;
    return false;
}

void InstallKeyBindLua(lua_State* L) {
    // The refs belonged to the previous Lua state, which is already closed.
    g_functions.clear();
    g_commands.clear();
    g_pressed.clear();
    RefreshBoundKeys();
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_queue.clear();
    }
    static const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"bindKey", BindKey},
        {"unbindKey", UnbindKey},
        {"getKeyState", GetKeyState},
        {"getBoundKeys", GetBoundKeys},
        {"getFunctionsBoundToKey", GetFunctionsBoundToKey},
        {"getKeyBoundToFunction", GetKeyBoundToFunction},
        {"getCommandsBoundToKey", GetCommandsBoundToKey},
        {"getKeyBoundToCommand", GetKeyBoundToCommand},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
