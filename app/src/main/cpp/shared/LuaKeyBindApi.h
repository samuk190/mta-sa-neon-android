#pragma once

// Script key binds: desktop CScriptKeyBinds (Client/mods/deathmatch/logic/CScriptKeyBinds.cpp) and the
// bindKey family in lua/CLuaFunctionDefs.Input.cpp. Keys arrive from a hardware keyboard through
// the Activity (KeyBindsQueueKey). The phone's touch controls are not key presses, so a bind on a
// GTA control name is stored and reported but never called.

#include <cstdint>
#include <string>
#include <vector>

struct lua_State;

namespace WorldRPG::Script {

// Installs bindKey, unbindKey, getKeyState, getFunctionsBoundToKey, getKeyBoundToFunction,
// getCommandsBoundToKey and getKeyBoundToCommand, and drops the binds of the previous VM.
void InstallKeyBindLua(lua_State* L);

// CScriptKeyBinds::RemoveAllKeys(CLuaMain*) (CResource.cpp:228) plus the resource's command binds.
// netId or generation 0 matches any.
void KeyBindsDropResource(lua_State* L, const std::string& owner, uint16_t netId, uint32_t generation);

// A key went down or up (any thread). Unknown key names are ignored.
void KeyBindsQueueKey(const char* key, bool down);
// The key names that have a function or a command bound, in table order, joined by '|'.
// Any thread: the on-screen key list reads it.
std::string KeyBindsBoundKeys();
// Pops the oldest queued key in its table spelling and records its state for getKeyState. VM thread.
bool KeyBindsNextKey(std::string& key, bool& down);

// One bound function to call: registry refs of the function and its extra arguments.
struct KeyBindCall {
    // Serial of the bind. A registry ref can be reused after an unbind, a serial cannot.
    uint64_t id = 0;
    int ref = 0;
    std::vector<int> args;
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};
// One bound command to run.
struct KeyBindCommand {
    std::string command;
    std::string arguments;
};
// CScriptKeyBinds::ProcessKey for a key: the function binds of this key and state, in bind order,
// and the command binds of the key (CKeyBinds command binds).
void KeyBindsCollect(const std::string& key, bool down, std::vector<KeyBindCall>& calls,
                     std::vector<KeyBindCommand>& commands);
// False when the bind was removed after KeyBindsCollect (desktop beingDeleted).
bool KeyBindLive(uint64_t id);

// Binds the server owns. CInputRPCs::BindKey / UnbindKey add and remove the server's key function
// (CClientGame::StaticProcessServerKeyBind, CInputRPCs.cpp:30-110) and BindCommand / UnbindCommand call
// CStaticFunctionDefinitions::BindKey / UnbindKey (CInputRPCs.cpp:113-181). The command forms touch VM state and run
// on the VM thread (ServerRpc::Pump). A name that is neither a key nor a control is ignored (false).
bool KeyBindsServerBindFunction(const std::string& name, bool down);
bool KeyBindsServerUnbindFunction(const std::string& name, bool down);
bool KeyBindsServerBound(const std::string& name, bool down);
bool KeyBindsServerBindCommand(const std::string& key, const std::string& hitState, const std::string& command,
                               const std::string& arguments, const std::string& resource);
bool KeyBindsServerUnbindCommand(const std::string& key, const std::string& hitState, const std::string& command,
                                 const std::string& resource);
// A new session: every server bind and every unsent KEY_BIND event is dropped. Any thread.
void KeyBindsServerReset();

// CClientGame::ProcessServerKeyBind (CClientGame.cpp:2409-2434): a key with a server bind went down or up. The
// event is sent as the KEY_BIND RPC (bit control, bit state, the key name). VM thread queues, the JNI pulse takes.
struct ServerKeyEvent {
    std::string name;
    bool control = false;
    bool down = true;
};
void KeyBindsServerFire(const std::string& key, bool down);
std::vector<ServerKeyEvent> KeyBindsTakeServerEvents();

}  // namespace WorldRPG::Script
