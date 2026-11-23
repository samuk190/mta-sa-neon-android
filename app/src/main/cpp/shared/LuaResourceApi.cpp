// Resource queries. Desktop CLuaResourceDefs.cpp 20-30 and 146-413.
// A resource is a table {resource = true, name = "..."}, not an element.
// getResourceRootElement returns the element. The phone has one Lua state,
// so getThisResource is the resource whose code is running (ActiveVmOwner).

#include "LuaResourceApi.h"

#include "LuaApiExtra.h"
#include "LuaGuiApi.h"
#include "LuaVMStub.h"
#include "ResourceLoader.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <algorithm>
#include <cstring>
#include <string>

namespace WorldRPG::Script {
namespace {

constexpr const char* kResourceTable = "worldrpg.resources";
char kResourceEnvironments;

void PushResource(lua_State* L, const ResourceLoader::ResourceView& view) {
    // One table per net id and start, so getThisResource() == getResourceFromName(name)
    // for that instance. A later start of the same net id does not reuse the table.
    lua_getfield(L, LUA_REGISTRYINDEX, kResourceTable);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kResourceTable);
    }
    lua_rawgeti(L, -1, view.netId);
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "gen");
        const bool same = lua_isinteger(L, -1) &&
            static_cast<uint32_t>(lua_tointeger(L, -1)) == view.generation;
        lua_pop(L, 1);
        if (same) {
            lua_remove(L, -2);
            return;
        }
    }
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "resource");
    lua_pushlstring(L, view.name.data(), view.name.size());
    lua_setfield(L, -2, "name");
    lua_pushinteger(L, view.netId);
    lua_setfield(L, -2, "netId");
    lua_pushinteger(L, view.startCounter);
    lua_setfield(L, -2, "start");
    lua_pushinteger(L, view.generation);
    lua_setfield(L, -2, "gen");
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, view.netId);
    lua_remove(L, -2);
}

bool ResourceAt(lua_State* L, int index, bool optional, ResourceLoader::ResourceView& view) {
    if (lua_isnoneornil(L, index)) {
        if (!optional) return false;
        return ResourceLoader::CopyCallerResource(ActiveVmOwnerNet(), ActiveVmOwnerGeneration(), ActiveVmOwner(), view);
    }
    if (!lua_istable(L, index)) return false;
    lua_getfield(L, index, "resource");
    const bool flag = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (!flag) return false;
    lua_getfield(L, index, "netId");
    const bool hasNet = lua_isinteger(L, -1);
    const uint16_t netId = hasNet ? static_cast<uint16_t>(lua_tointeger(L, -1)) : 0;
    lua_pop(L, 1);
    lua_getfield(L, index, "gen");
    const bool hasGen = lua_isinteger(L, -1);
    const uint32_t generation = hasGen ? static_cast<uint32_t>(lua_tointeger(L, -1)) : 0;
    lua_pop(L, 1);
    if (hasNet && hasGen) {
        if (!ResourceLoader::CopyResourceByGeneration(generation, view)) return false;
        return view.netId == netId;
    }
    lua_getfield(L, index, "name");
    size_t length = 0;
    const char* text = lua_tolstring(L, -1, &length);
    if (!text) {
        lua_pop(L, 1);
        return false;
    }
    const std::string name(text, length);
    lua_pop(L, 1);
    return ResourceLoader::CopyResource(name, view);
}

void PushFail(lua_State* L) { lua_pushboolean(L, 0); }

int l_getThisResource(lua_State* L) {
    // CLuaResourceDefs::GetThisResource (CLuaResourceDefs.cpp 146-165).
    ResourceLoader::ResourceView view;
    if (!ResourceLoader::CopyCallerResource(ActiveVmOwnerNet(), ActiveVmOwnerGeneration(), ActiveVmOwner(), view)) {
        PushFail(L);
        return 1;
    }
    PushResource(L, view);
    return 1;
}

int l_getResourceName(lua_State* L) {
    // GetResourceName (CLuaResourceDefs.cpp 226-232). No argument uses the running resource.
    ResourceLoader::ResourceView view;
    if (!ResourceAt(L, 1, true, view)) {
        PushFail(L);
        return 1;
    }
    lua_pushlstring(L, view.name.data(), view.name.size());
    return 1;
}

int l_getResourceFromName(lua_State* L) {
    // CLuaResourceDefs::GetResourceFromName (234-257).
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    ResourceLoader::ResourceView view;
    if (!text || !ResourceLoader::CopyResource(std::string(text, length), view)) {
        PushFail(L);
        return 1;
    }
    PushResource(L, view);
    return 1;
}

int l_getResourceRootElement(lua_State* L) {
    // CLuaResourceDefs::GetResourceRootElement (259-297). The argument is optional.
    ResourceLoader::ResourceView view;
    if (!ResourceAt(L, 1, true, view) || view.root == 0) {
        PushFail(L);
        return 1;
    }
    EnsureResourceNode(view.root, view.name);
    PushElement(L, view.root);
    return 1;
}

int l_getResourceDynamicElementRoot(lua_State* L) {
    // CLuaResourceDefs::GetResourceDynamicElementRoot (337-358).
    ResourceLoader::ResourceView view;
    if (!ResourceAt(L, 1, false, view)) {
        PushFail(L);
        return 1;
    }
    if ((view.state != "running" && view.state != "stopping") || view.dynamic == 0) {
        LUA_LOGE("getResourceDynamicElementRoot: Resource %s Is Not Currently Running", view.name.c_str());
        PushFail(L);
        return 1;
    }
    EnsureResourceNode(view.root, view.name);
    EnsureDynamicNode(view.root, view.dynamic);
    PushElement(L, view.dynamic);
    return 1;
}

int l_getResourceGUIElement(lua_State* L) {
    // CLuaResourceDefs::GetResourceGUIElement (299-334). The argument is optional.
    ResourceLoader::ResourceView view;
    if (!ResourceAt(L, 1, true, view)) {
        PushFail(L);
        return 1;
    }
    const uint32_t gui = GuiEnsureResourceRoot(view.name, view.netId, view.generation);
    if (!gui) {
        PushFail(L);
        return 1;
    }
    PushElement(L, gui);
    return 1;
}

int l_getResourceExportedFunctions(lua_State* L) {
    // CLuaResourceDefs::GetResourceExportedFunctions (360-393).
    ResourceLoader::ResourceView view;
    if (!ResourceAt(L, 1, true, view)) {
        PushFail(L);
        return 1;
    }
    lua_createtable(L, static_cast<int>(view.exports.size()), 0);
    int index = 0;
    for (const std::string& name : view.exports) {
        lua_pushlstring(L, name.data(), name.size());
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

int l_getResources(lua_State* L) {
    // CLuaResourceDefs::GetResources (415-418), in RESOURCE_START order.
    const std::vector<ResourceLoader::ResourceView> list = ResourceLoader::CopyResources();
    lua_createtable(L, static_cast<int>(list.size()), 0);
    int index = 0;
    for (const ResourceLoader::ResourceView& view : list) {
        PushResource(L, view);
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

int l_getResourceState(lua_State* L) {
    // CLuaResourceDefs::GetResourceState (395-413).
    ResourceLoader::ResourceView view;
    if (!ResourceAt(L, 1, false, view)) {
        PushFail(L);
        return 1;
    }
    lua_pushlstring(L, view.state.data(), view.state.size());
    return 1;
}

// CLuaResourceDefs.cpp:68-144.
// var... call ( resource theResource, string theFunction, [arguments...] )
int l_call(lua_State* L) {
    ResourceLoader::ResourceView view;
    if (!ResourceAt(L, 1, false, view)) {
        if (lua_isstring(L, 1)) {
            const char* resName = lua_tostring(L, 1);
            if (!ResourceLoader::CopyResource(resName ? resName : "", view)) {
                PushFail(L);
                return 1;
            }
        } else {
            PushFail(L);
            return 1;
        }
    }
    if (!lua_isstring(L, 2)) {
        PushFail(L);
        return 1;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, 2, &length);
    const std::string funcName(text, length);
    if (std::find(view.exports.begin(), view.exports.end(), funcName) == view.exports.end()) {
        PushFail(L);
        return 1;
    }

    lua_rawgetp(L, LUA_REGISTRYINDEX, &kResourceEnvironments);
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, view.generation);
        lua_remove(L, -2);
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        PushFail(L);
        return 1;
    }

    // Read the target's own globals, skipping the metatable fallback to VM bindings.
    lua_pushlstring(L, funcName.data(), funcName.size());
    lua_rawget(L, -2);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        PushFail(L);
        return 1;
    }
    const int argCount = lua_gettop(L) - 3;
    lua_insert(L, 3);

    lua_getglobal(L, "sourceResource");
    const int oldSource = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getglobal(L, "sourceResourceRoot");
    const int oldRoot = luaL_ref(L, LUA_REGISTRYINDEX);
    ApplyEventSource(L, ActiveVmOwnerNet(), ActiveVmOwnerGeneration());

    int status = LUA_OK;
    ActiveVmRunAsOwner(view.name, view.netId, view.generation, [&] {
        status = lua_pcall(L, argCount, LUA_MULTRET, 0);
    });

    lua_rawgeti(L, LUA_REGISTRYINDEX, oldSource);
    lua_setglobal(L, "sourceResource");
    luaL_unref(L, LUA_REGISTRYINDEX, oldSource);
    lua_rawgeti(L, LUA_REGISTRYINDEX, oldRoot);
    lua_setglobal(L, "sourceResourceRoot");
    luaL_unref(L, LUA_REGISTRYINDEX, oldRoot);

    if (status != LUA_OK) {
        const char* error = lua_tostring(L, -1);
        LUA_LOGE("call: failed to call '%s:%s': %s", view.name.c_str(), funcName.c_str(), error ? error : "unknown error");
        lua_pop(L, 1);
        PushFail(L);
        return 1;
    }
    return lua_gettop(L) - 2;
}

// CLuaResourceDefs.cpp:470-548.
// func, err load ( callbackFunction or string [, string name ] )
int l_load(lua_State* L) {
    std::string name = "=(load)";
    if (lua_gettop(L) >= 2 && lua_isstring(L, 2)) {
        name = lua_tostring(L, 2);
    }
    std::string code;
    if (lua_isstring(L, 1)) {
        size_t len = 0;
        const char* s = lua_tolstring(L, 1, &len);
        code.assign(s ? s : "", len);
    } else if (lua_isfunction(L, 1)) {
        while (true) {
            lua_pushvalue(L, 1);
            if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
                lua_pushnil(L);
                lua_insert(L, -2);
                return 2;
            }
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            if (lua_isstring(L, -1)) {
                size_t partLen = 0;
                const char* part = lua_tolstring(L, -1, &partLen);
                if (partLen == 0) {
                    lua_pop(L, 1);
                    break;
                }
                code.append(part, partLen);
                lua_pop(L, 1);
                if (code.size() > 16 * 1024 * 1024) {
                    lua_pushnil(L);
                    lua_pushstring(L, "chunk too large");
                    return 2;
                }
            } else {
                lua_pop(L, 1);
                break;
            }
        }
    } else {
        lua_pushnil(L);
        lua_pushstring(L, "bad argument #1 to 'load' (function or string expected)");
        return 2;
    }
    if (luaL_loadbufferx(L, code.data(), code.size(), name.c_str(), "t") != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

// CLuaResourceDefs.cpp:167-224.
// xmlnode getResourceConfig ( string filePath )
int l_getResourceConfig(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        PushFail(L);
        return 1;
    }
    lua_getglobal(L, "xmlLoadFile");
    if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, 1);
        if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
            return 1;
        }
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }
    PushFail(L);
    return 1;
}

}  // namespace

bool PushResourceInstance(lua_State* L, uint16_t netId, uint32_t generation) {
    ResourceLoader::ResourceView view;
    if (generation != 0) {
        if (!ResourceLoader::CopyResourceByGeneration(generation, view) || view.netId != netId) return false;
    } else if (!ResourceLoader::CopyResourceByNet(netId, view)) {
        return false;
    }
    PushResource(L, view);
    return true;
}

void ApplyEventSource(lua_State* L, uint16_t netId, uint32_t generation) {
    // CMapEventManager.cpp 227-244: sourceResource is the resource that triggered
    // the event. netId 0 is an engine call, and both globals become nil.
    ResourceLoader::ResourceView view;
    const bool found = generation != 0
        ? ResourceLoader::CopyResourceByGeneration(generation, view) && view.netId == netId
        : ResourceLoader::CopyResourceByNet(netId, view);
    if (netId != 0 && found) {
        PushResource(L, view);
        lua_setglobal(L, "sourceResource");
        EnsureResourceNode(view.root, view.name);
        if (view.root != 0) PushElement(L, view.root);
        else lua_pushnil(L);
        lua_setglobal(L, "sourceResourceRoot");
        return;
    }
    lua_pushnil(L);
    lua_setglobal(L, "sourceResource");
    lua_pushnil(L);
    lua_setglobal(L, "sourceResourceRoot");
}

void KeepResourceEnvironment(lua_State* L, uint32_t generation, int index) {
    const int environment = lua_absindex(L, index);
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kResourceEnvironments);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &kResourceEnvironments);
    }
    lua_pushvalue(L, environment);
    lua_rawseti(L, -2, generation);
    lua_pop(L, 1);
}

void DropResourceEnvironment(lua_State* L, uint32_t generation) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kResourceEnvironments);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_rawseti(L, -2, generation);
    }
    lua_pop(L, 1);
}

void InstallResourceLua(lua_State* L) {
    lua_pushcfunction(L, l_getThisResource);
    lua_setglobal(L, "getThisResource");
    lua_pushcfunction(L, l_getResourceName);
    lua_setglobal(L, "getResourceName");
    lua_pushcfunction(L, l_getResourceFromName);
    lua_setglobal(L, "getResourceFromName");
    lua_pushcfunction(L, l_getResourceRootElement);
    lua_setglobal(L, "getResourceRootElement");
    lua_pushcfunction(L, l_getResourceDynamicElementRoot);
    lua_setglobal(L, "getResourceDynamicElementRoot");
    lua_pushcfunction(L, l_getResourceGUIElement);
    lua_setglobal(L, "getResourceGUIElement");
    lua_pushcfunction(L, l_getResourceExportedFunctions);
    lua_setglobal(L, "getResourceExportedFunctions");
    lua_pushcfunction(L, l_getResources);
    lua_setglobal(L, "getResources");
    lua_pushcfunction(L, l_getResourceState);
    lua_setglobal(L, "getResourceState");
    lua_pushcfunction(L, l_call);
    lua_setglobal(L, "call");
    lua_pushcfunction(L, l_load);
    lua_setglobal(L, "load");
    lua_pushcfunction(L, l_getResourceConfig);
    lua_setglobal(L, "getResourceConfig");
}

}  // namespace WorldRPG::Script
