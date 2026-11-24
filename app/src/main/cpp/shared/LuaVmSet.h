#pragma once
// The two script VMs (luaapi/README.md): the Lua 5.3 state every client
// session has, and a Lua 5.1 state created for the first resource that runs
// in 5.1 (desktop MTA scripts). Resources run in one of them; this is what
// lets code that stores callbacks or moves values work across both.
//
// Callback refs: Ref() returns a registry ref that also names its VM, so a
// timer, event handler or bind can be called later in the right state, and
// refs of the two VMs never compare equal. Use Ref/Unref/PushRef for every
// ref that outlives the C call that took it.
//
// Values: Copy() moves a value into the other VM as desktop MTA copies
// arguments between resource VMs (CLuaArguments): nil, booleans, numbers,
// strings and light userdata as they are, tables deep (shared and cyclic
// tables stay shared), elements and resources as the same object of the
// target VM. Functions, threads and full userdata do not cross and arrive as
// nil.

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace WorldRPG::Script::Vms {

void SetPrimary(lua_State* L);  // the Lua 5.3 state (LuaVMStub::initialize)
lua_State* Primary();
void SetLua51(lua_State* L);    // null when no 5.1 state exists
lua_State* Lua51();

// The main state of L's VM (L may be a coroutine).
lua_State* MainOf(lua_State* L);
bool Is51(lua_State* L);

// luaL_ref on L's registry, popping the value; LUA_NOREF/LUA_REFNIL as luaL_ref.
int Ref(lua_State* L);
void Unref(int ref);
lua_State* RefVm(int ref);  // null for LUA_NOREF/LUA_REFNIL or a VM that is gone
// Pushes the ref's value onto L when L is in the ref's VM, nil otherwise.
// Returns the pushed type.
int PushRef(lua_State* L, int ref);

// Pushes the ref's value onto L, copied when the ref lives in the other VM
// (stored values such as grid item data are copies on desktop too).
void PushRefCopy(lua_State* L, int ref);

// Pushes a copy of from[idx] onto to. Same VM: the value itself.
void Copy(lua_State* from, int idx, lua_State* to);
// Copies count values starting at from[first] onto to, in order.
void CopyRange(lua_State* from, int first, int count, lua_State* to);

// load/loadstring for scripts: compiles buf on L. On a 5.1 state, as desktop
// MTA: compiled chunks are accepted after netc's DeobfuscateScript (PC
// Network only), and the chunk gets the calling function's environment (its
// resource's globals; desktop has one VM per resource). On 5.3, text only,
// as before. Returns the luaL_loadbuffer status.
int LoadScriptChunk(lua_State* L, const char* buf, size_t len, const char* name);

// Runs the C function fn on the primary (5.3) state when L is a 5.1 state:
// the arguments are copied there and the results copied back, and an error
// is raised in L. For API functions whose data lives in the 5.3 state's
// registry (element data), so both VMs see one store as on desktop.
int ProxyToPrimary(lua_State* L, int (*fn)(lua_State*));
template <int (*Fn)(lua_State*)>
int OnPrimary(lua_State* L) {
    return ProxyToPrimary(L, Fn);
}

}  // namespace WorldRPG::Script::Vms
