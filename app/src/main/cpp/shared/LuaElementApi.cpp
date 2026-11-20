// Client element tree for the functions in CLuaElementDefs that do not call a GTA native.
//
// Desktop: Client/mods/deathmatch/logic/luadefs/CLuaElementDefs.cpp and
// CStaticFunctionDefinitions.cpp (the element get/set functions cited on each binding).
// Elements are tables {id, element = true}, the same shape PushElement already uses.
//
// The server root id and the resource dynamic-entity id are not kept after join
// (NeonJoinClient::Info and ServerWorld::OnResourceStart). The root here is the
// client root, and createElement parents to a map element of type "map" named
// "dynamic" under the running resource, which is the desktop parent chain
// (CResource.cpp 887-894). That map stands in for the server dynamic entity, so
// Streamed parent links and attachment are stored. Model changes update the stored id.
// CModelInfo::SetModel and onClientElementModelChange are not run.

#include "LuaApiExtra.h"
#include "LuaElementTree.h"
#include "LuaGuiApi.h"
#include "LuaVMStub.h"
#include "LuaCameraApi.h"
#include "LuaStoredElementApi.h"
#include "LuaDxApi.h"
#include "LuaSpatialApi.h"
#include "LuaMarkerApi.h"
#include "LuaBlipApi.h"
#include "LuaRadarApi.h"
#include "LuaPickupApi.h"
#include "LuaLightApi.h"
#include "LuaWaterApi.h"
#include "LuaFireApi.h"
#include "LuaBirdApi.h"
#include "LuaRopeApi.h"
#include "ResourceLoader.h"
#include "ServerWorld.h"
#include "PedQueries.h"
#include "EngineAssets.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/RemoteVehicle.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace WorldRPG::Script {
namespace {

// Same registry table setElementData writes (LuaApiExtra.cpp).
constexpr const char* kElementDataKey = "worldrpg.elementData";
// CCustomData.h MAX_CUSTOMDATA_NAME_LENGTH.
constexpr size_t kMaxDataName = 128;
// Shared/sdk/Common.h. Local entities are ids at or above this (CClientEntity.h IsLocalEntity).
constexpr uint32_t kMaxServerElements = 131072;
constexpr uint32_t kMaxClientElements = 131072;

struct Node {
    uint32_t id = 0;
    uint32_t parent = 0;
    std::string type;
    std::string name;
    std::vector<uint32_t> children;
    bool callPropagation = true;
    bool system = false;
    bool serverElement = false;  // not a client-created element, even if its id is in the client range
    bool ownsType = false;       // getElementType reports `type` (streamed elements keep ElementKindName)
    bool canDestroy = true;
    bool beingDeleted = false;
};

std::unordered_map<uint32_t, Node> g_nodes;
std::unordered_map<uint32_t, std::string> g_localNames;  // setElementID on a local element that is not in the tree
std::unordered_map<uint32_t, bool> g_callProp;           // call propagation for elements that are not in the tree
std::set<std::pair<uint32_t, uint32_t>> g_disabled;      // pairs that cannot collide, stored both ways via a sorted key
uint32_t g_nextId = kMaxServerElements;
uint32_t g_root = 0;
bool g_forceDestroy = false;

Node* Find(uint32_t id) {
    auto found = g_nodes.find(id);
    return found == g_nodes.end() ? nullptr : &found->second;
}

bool IsLocal(uint32_t id) {
    if (Node* node = Find(id)) return !node->serverElement && id >= kMaxServerElements;
    return id >= kMaxServerElements;
}

uint32_t AllocId() {
    if (g_nextId >= kMaxServerElements + kMaxClientElements) return 0;
    return g_nextId++;
}

void Link(uint32_t id, uint32_t parent) {
    auto it = g_nodes.find(id);
    if (it == g_nodes.end() || it->second.parent == parent) return;
    const uint32_t old = it->second.parent;
    if (old) {
        auto previous = g_nodes.find(old);
        if (previous != g_nodes.end()) {
            auto& kids = previous->second.children;
            kids.erase(std::remove(kids.begin(), kids.end(), id), kids.end());
        }
    }
    it = g_nodes.find(id);
    it->second.parent = parent;
    if (!parent) return;
    auto next = g_nodes.find(parent);
    if (next != g_nodes.end()) next->second.children.push_back(id);
}

void ForgetPairs(uint32_t id) {
    for (auto it = g_disabled.begin(); it != g_disabled.end();) {
        if (it->first == id || it->second == id) it = g_disabled.erase(it);
        else ++it;
    }
}

// CStaticFunctionDefinitions::DestroyElement (CStaticFunctionDefinitions.cpp 976-1006).
bool DestroyNode(uint32_t id) {
    // The joined player is a server element. Never delete that id, even if a node used it.
    if (id != 0 && id == ServerWorld::LocalPlayer()) return false;
    Node* node = Find(id);
    if (!node || node->beingDeleted || (!node->canDestroy && !g_forceDestroy)) return false;
    std::vector<uint32_t> kids = node->children;
    for (size_t i = 0; i < kids.size();) {
        if (DestroyNode(kids[i])) {
            node = Find(id);
            if (!node) return false;
            kids = node->children;
            i = 0;
        } else {
            ++i;
        }
    }
    node = Find(id);
    if (!node || node->beingDeleted || id == g_root) return false;
    if (!IsLocal(id) || node->system) return false;
    node->beingDeleted = true;
    Link(id, 0);
    SpatialRelease(id);
    MarkerRelease(id);
    BlipRelease(id);
    RadarRelease(id);
    PickupRelease(id);
    LightRelease(id);
    BirdRelease(id);
    RopeRelease(id);
    WaterRelease(id);
    FireRelease(id);
    ReleaseDffElement(id);
    g_nodes.erase(id);
    g_localNames.erase(id);
    g_callProp.erase(id);
    ForgetPairs(id);
    NoteClientElementDestroyed(id);
    return true;
}

uint32_t EnsureRoot() {
    if (g_root && Find(g_root)) return g_root;
    const uint32_t id = AllocId();
    if (!id) return 0;
    Node node;
    node.id = id;
    node.type = "root";
    node.ownsType = true;
    node.system = true;
    // Join assigns the real root a server id (CPacketHandler.cpp 425-433), so it is not local.
    node.serverElement = true;
    g_nodes.emplace(id, std::move(node));
    g_root = id;
    return id;
}

// CResource.cpp 887-894: resource element under root, dynamic map named "dynamic" under that.
uint32_t EnsureMap(uint32_t resourceId, const std::string& resourceName, uint32_t dynamicId) {
    const uint32_t root = EnsureRoot();
    if (!resourceId || !root) return 0;
    if (!Find(resourceId)) {
        Node resource;
        resource.id = resourceId;
        resource.type = "resource";
        resource.name = resourceName;
        resource.ownsType = true;
        resource.serverElement = true;
        resource.canDestroy = false;
        g_nodes.emplace(resourceId, std::move(resource));
        Link(resourceId, root);
    }
    if (dynamicId && Find(dynamicId)) return dynamicId;
    Node* resource = Find(resourceId);
    if (!resource) return 0;
    const std::vector<uint32_t> kids = resource->children;
    for (uint32_t child : kids) {
        Node* node = Find(child);
        if (node && node->serverElement && node->type == "map" && node->name == "dynamic") return child;
    }
    const uint32_t id = dynamicId ? dynamicId : AllocId();
    if (!id || Find(id)) return id && Find(id) ? id : 0;
    Node map;
    map.id = id;
    map.type = "map";
    map.name = "dynamic";
    map.ownsType = true;
    map.serverElement = true;
    map.canDestroy = false;
    g_nodes.emplace(id, std::move(map));
    Link(id, resourceId);
    return id;
}

void ResetCatalog() {
    g_nodes.clear();
    g_localNames.clear();
    g_callProp.clear();
    g_disabled.clear();
    g_nextId = kMaxServerElements;
    g_root = 0;
    g_forceDestroy = false;
    ResetElementTreeExtras();
}

bool KnownElement(uint32_t id) {
    if (id == 0) return false;
    if (Find(id)) return true;
    if (ServerWorld::IsKnownPlayer(id)) return true;
    if (id == ServerWorld::LocalPlayer() || id == ServerWorld::LastResourceRoot() || GuiIsElement(id) || DxElementType(id)) return true;
    if (Game::ScriptPedAlive(id) || SoundElementAlive(id) || CameraElementAlive(id) || FoliageElementAlive(id) ||
        EffectElementAlive(id) || StoredElementType(id) != nullptr)
        return true;
    float x = 0, y = 0, z = 0;
    return ServerWorld::ElementPose(id, x, y, z) || Game::CopyRemotePose(id, x, y, z);
}

// Same acceptance as l_isElement: a table whose id the phone already knows.
bool ReadElement(lua_State* L, int index, uint32_t& id) {
    id = ElementIdOf(L, index);
    return KnownElement(id);
}

bool ReadStringAt(lua_State* L, int index, std::string& out) {
    const int type = lua_type(L, index);
    if (type != LUA_TSTRING && type != LUA_TNUMBER) return false;
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    if (!text) return false;
    out.assign(text, length);
    return true;
}

// CScriptArgReader::ReadNumber for an unsigned value. `optional` uses the default on nil/none.
bool ReadUint(lua_State* L, int index, bool optional, unsigned& out, unsigned fallback) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        if (!optional) return false;
        out = fallback;
        return true;
    }
    if ((type != LUA_TNUMBER && type != LUA_TSTRING) || !lua_isnumber(L, index)) return false;
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number) || number < -static_cast<lua_Number>(FLT_EPSILON)) return false;
    out = static_cast<unsigned>(number);
    return true;
}

bool ReadBoolAt(lua_State* L, int index, bool optional, bool& out, bool fallback) {
    const int type = lua_type(L, index);
    if (type == LUA_TBOOLEAN) {
        out = lua_toboolean(L, index) != 0;
        return true;
    }
    if (optional && (type == LUA_TNONE || type == LUA_TNIL)) {
        out = fallback;
        return true;
    }
    return false;
}

void PushFail(lua_State* L) { lua_pushboolean(L, 0); }

std::string TypeOf(uint32_t id) {
    if (Node* node = Find(id); node && node->ownsType) return node->type;
    if (const char* gui = GuiElementType(id)) return gui;
    const char* kind = ServerWorld::ElementKindName(id);
    return kind ? kind : "element";
}

std::string NameOf(uint32_t id) {
    if (Node* node = Find(id)) return node->name;
    auto found = g_localNames.find(id);
    return found == g_localNames.end() ? std::string() : found->second;
}

bool CallPropOf(uint32_t id) {
    if (Node* node = Find(id)) return node->callPropagation;
    auto found = g_callProp.find(id);
    return found == g_callProp.end() ? true : found->second;
}

void SetCallProp(uint32_t id, bool enabled) {
    if (Node* node = Find(id)) node->callPropagation = enabled;
    else g_callProp[id] = enabled;
}

// CClientEntity::GetTypeID (CClientEntity.cpp 1210-1239). createElement rejects these names.
bool IsBuiltinType(const std::string& type) {
    static const char* kBuiltins[] = {
        "dummy", "ped", "player", "projectile", "vehicle", "object", "marker", "blip",
        "pickup", "radararea", "sound", "light", "svg",
    };
    for (const char* name : kBuiltins) {
        if (type == name) return true;
    }
    return false;
}

// CClientEntity::FindChild / FindChildIndex (CClientEntity.cpp 869-927).
uint32_t FindChildIndex(uint32_t id, const std::string& name, unsigned index, unsigned& current) {
    Node* node = Find(id);
    if (!node) return 0;
    const std::vector<uint32_t> kids = node->children;
    for (uint32_t child : kids) {
        Node* kid = Find(child);
        if (!kid) continue;
        if (kid->name == name) {
            if (current == index) return child;
            ++current;
        }
        if (uint32_t found = FindChildIndex(child, name, index, current)) return found;
    }
    return 0;
}

uint32_t FindByName(const std::string& name, unsigned index) {
    Node* root = Find(g_root);
    if (!root) return 0;
    unsigned current = 0;
    if (root->name == name) {
        if (index == 0) return g_root;
        ++current;
    }
    return FindChildIndex(g_root, name, index, current);
}

// CClientEntity::FindChildByTypeIndex (CClientEntity.cpp 952-984).
uint32_t FindTypeIndex(uint32_t id, const std::string& type, unsigned index, unsigned& current) {
    Node* node = Find(id);
    if (!node) return 0;
    const std::vector<uint32_t> kids = node->children;
    for (uint32_t child : kids) {
        Node* kid = Find(child);
        if (!kid) continue;
        if (kid->type == type) {
            if (current == index) return child;
            ++current;
        }
        if (uint32_t found = FindTypeIndex(child, type, index, current)) return found;
    }
    return 0;
}

uint32_t FindByType(const std::string& type, unsigned index) {
    Node* root = Find(g_root);
    if (!root) return 0;
    unsigned current = 0;
    if (root->type == type) {
        if (index == 0) return g_root;
        ++current;
    }
    return FindTypeIndex(g_root, type, index, current);
}

// true when `target` is `start` or an ancestor of `start` (CClientEntity::IsMyParent, 238-248).
bool IsAncestorOrSelf(uint32_t start, uint32_t target) {
    uint32_t current = start;
    std::unordered_set<uint32_t> seen;
    while (current && seen.insert(current).second) {
        if (current == target) return true;
        Node* node = Find(current);
        if (!node) return false;
        current = node->parent;
    }
    return false;
}

// Walk from `parent` up to, but not including, the root (CStaticFunctionDefinitions.cpp 1272-1285).
bool HasMapAncestor(uint32_t parent) {
    uint32_t current = parent;
    std::unordered_set<uint32_t> seen;
    while (current && current != g_root && seen.insert(current).second) {
        Node* node = Find(current);
        if (!node) return false;
        if (node->type == "map") return true;
        current = node->parent;
    }
    return false;
}

bool OwnData(lua_State* L, uint32_t id, const std::string& key) {
    const int top = lua_gettop(L);
    bool found = false;
    lua_getfield(L, LUA_REGISTRYINDEX, kElementDataKey);
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, static_cast<lua_Integer>(id));
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, key.c_str());
            found = !lua_isnil(L, -1);
        }
    }
    lua_settop(L, top);
    if (found) return true;
    double number = 0;
    std::string text;
    bool isNumber = false;
    return ServerWorld::GetData(id, key.c_str(), number, text, isNumber);
}

bool HasData(lua_State* L, uint32_t id, const std::string& key, bool inherit) {
    uint32_t current = id;
    std::unordered_set<uint32_t> seen;
    while (current && seen.insert(current).second) {
        if (OwnData(L, current, key)) return true;
        if (!inherit) return false;
        Node* node = Find(current);
        if (!node || node->parent == 0) return false;
        current = node->parent;
    }
    return false;
}

bool IsPhysical(const std::string& type) {
    return type == "player" || type == "ped" || type == "vehicle" || type == "object";
}

bool IsCollideSetType(const std::string& type) {
    return IsPhysical(type) || type == "weapon";
}

std::pair<uint32_t, uint32_t> PairKey(uint32_t a, uint32_t b) {
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}

// CClientEntity::SetCollidableWith (CClientEntity.cpp 1582-1600).
void SetPairCollide(uint32_t a, uint32_t b, bool can) {
    const auto key = PairKey(a, b);
    const bool disabled = g_disabled.count(key) != 0;
    if (disabled != can) return;
    if (can) g_disabled.erase(key);
    else g_disabled.insert(key);
}

// ---- bindings ----

// CLuaElementDefs::GetRootElement (CLuaElementDefs.cpp 242-248).
int GetRootElement(lua_State* L) {
    if (!EnsureRoot()) {
        PushFail(L);
        return 1;
    }
    PushElement(L, g_root);
    return 1;
}

// CLuaElementDefs.cpp:1138-1232,1281-1311; CStaticFunctionDefinitions.cpp:642-647,685-691.
// Server attachment metadata only; physical attachment and attachment RPCs are separate hooks.
int IsElementAttached(lua_State* L) {
    uint32_t id = 0;
    ServerWorld::Attachment attachment;
    if (ReadElement(L, 1, id) && ServerWorld::CopyAttachment(id, attachment) && attachment.parent != 0xFFFFFFFFu)
        lua_pushboolean(L, 1);
    else lua_pushnil(L);
    return 1;
}
int GetElementAttachedTo(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) { lua_pushnil(L); return 1; }
    ServerWorld::Attachment attachment;
    if (ServerWorld::CopyAttachment(id, attachment) && attachment.parent != 0xFFFFFFFFu) PushElement(L, attachment.parent);
    else lua_pushboolean(L, 0);
    return 1;
}
int GetAttachedElements(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) { PushFail(L); return 1; }
    lua_newtable(L);
    int index = 0;
    for (uint32_t child : ServerWorld::CopyAttachedElements(id)) {
        PushElement(L, child); lua_rawseti(L, -2, ++index);
    }
    return 1;
}
int GetElementAttachedOffsets(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) { PushFail(L); return 1; }
    ServerWorld::Attachment attachment;
    ServerWorld::CopyAttachment(id, attachment);
    for (float value : attachment.position) lua_pushnumber(L, value);
    for (float value : attachment.rotation) lua_pushnumber(L, value);
    return 6;
}

// CLuaElementDefs::GetElementID (CLuaElementDefs.cpp 268-289).
int GetElementID(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) {
        PushFail(L);
        return 1;
    }
    const std::string name = NameOf(id);
    if (name.empty()) {
        PushFail(L);
        return 1;
    }
    lua_pushlstring(L, name.data(), name.size());
    return 1;
}

// CLuaElementDefs::GetElementByID (CLuaElementDefs.cpp 292-315).
int GetElementByID(lua_State* L) {
    std::string name;
    unsigned index = 0;
    if (!ReadStringAt(L, 1, name) || !ReadUint(L, 2, true, index, 0)) {
        PushFail(L);
        return 1;
    }
    EnsureRoot();
    const uint32_t id = FindByName(name, index);
    if (!id) {
        PushFail(L);
        return 1;
    }
    PushElement(L, id);
    return 1;
}

// CLuaElementDefs::GetElementByIndex (CLuaElementDefs.cpp 318-341).
int GetElementByIndex(lua_State* L) {
    std::string type;
    unsigned index = 0;
    if (!ReadStringAt(L, 1, type) || !ReadUint(L, 2, true, index, 0)) {
        PushFail(L);
        return 1;
    }
    EnsureRoot();
    const uint32_t id = FindByType(type, index);
    if (!id) {
        PushFail(L);
        return 1;
    }
    PushElement(L, id);
    return 1;
}

// CLuaElementDefs::GetElementChildren (CLuaElementDefs.cpp 679-721).
int GetElementChildren(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) {
        PushFail(L);
        return 1;
    }
    const bool filter = lua_type(L, 2) == LUA_TSTRING;
    std::string type;
    if (filter && !ReadStringAt(L, 2, type)) {
        PushFail(L);
        return 1;
    }
    lua_newtable(L);
    Node* node = Find(id);
    if (!node) return 1;
    const std::vector<uint32_t> kids = node->children;
    int index = 1;
    for (uint32_t child : kids) {
        Node* kid = Find(child);
        if (!kid || kid->beingDeleted) continue;
        if (filter && kid->type != type) continue;
        lua_pushnumber(L, static_cast<lua_Number>(index++));
        PushElement(L, child);
        lua_settable(L, -3);
    }
    return 1;
}

// CLuaElementDefs::GetElementChild (CLuaElementDefs.cpp 724-748).
int GetElementChild(lua_State* L) {
    uint32_t id = 0;
    unsigned index = 0;
    if (!ReadElement(L, 1, id) || !ReadUint(L, 2, false, index, 0)) {
        PushFail(L);
        return 1;
    }
    Node* node = Find(id);
    if (!node) {
        PushFail(L);
        return 1;
    }
    unsigned current = 0;
    for (uint32_t child : node->children) {
        if (current == index) {
            PushElement(L, child);
            return 1;
        }
        ++current;
    }
    PushFail(L);
    return 1;
}

// CLuaElementDefs::GetElementChildrenCount (CLuaElementDefs.cpp 751-770).
int GetElementChildrenCount(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) {
        PushFail(L);
        return 1;
    }
    Node* node = Find(id);
    lua_pushnumber(L, static_cast<lua_Number>(node ? node->children.size() : 0));
    return 1;
}

// CLuaElementDefs::GetElementParent (CLuaElementDefs.cpp 773-795).
int GetElementParent(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) {
        PushFail(L);
        return 1;
    }
    Node* node = Find(id);
    if (!node || node->parent == 0) {
        PushFail(L);
        return 1;
    }
    PushElement(L, node->parent);
    return 1;
}

// CLuaElementDefs::IsElementLocal (CLuaElementDefs.cpp 1259-1278).
int IsElementLocal(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, IsLocal(id) ? 1 : 0);
    return 1;
}

// CLuaElementDefs::HasElementData (CLuaElementDefsShared.cpp 71-101). Argument errors raise.
int HasElementData(lua_State* L) {
    uint32_t id = 0;
    std::string key;
    bool inherit = true;
    if (!ReadElement(L, 1, id) || !ReadStringAt(L, 2, key) || !ReadBoolAt(L, 3, true, inherit, true))
        return luaL_error(L, "Bad argument @ 'hasElementData'");
    if (key.size() > kMaxDataName) {
        LUA_LOGI("Lua: hasElementData key truncated to %zu characters", kMaxDataName);
        key.resize(kMaxDataName);
    }
    lua_pushboolean(L, HasData(L, id, key, inherit) ? 1 : 0);
    return 1;
}

// CLuaElementDefs::IsElementCallPropagationEnabled (CLuaElementDefs.cpp 2616-2637).
int IsElementCallPropagationEnabled(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) {
        PushFail(L);
        return 1;
    }
    lua_pushboolean(L, CallPropOf(id) ? 1 : 0);
    return 1;
}

// CLuaElementDefs::IsElementCollidableWith (CLuaElementDefs.cpp 1496-1518).
// Types the phone reports as "element" (objects included) are not player/ped/vehicle/object,
// so they take the desktop default and return false.
int IsElementCollidableWith(lua_State* L) {
    uint32_t id = 0;
    uint32_t with = 0;
    if (!ReadElement(L, 1, id) || !ReadElement(L, 2, with)) {
        PushFail(L);
        return 1;
    }
    if (!IsPhysical(TypeOf(id)) || !IsPhysical(TypeOf(with))) {
        PushFail(L);
        return 1;
    }
    lua_pushboolean(L, g_disabled.count(PairKey(id, with)) == 0 ? 1 : 0);
    return 1;
}

// CLuaElementDefs::CreateElement (CLuaElementDefs.cpp 1664-1714).
int CreateElement(lua_State* L) {
    std::string type;
    if (!ReadStringAt(L, 1, type)) {
        PushFail(L);
        return 1;
    }
    const std::string& owner = ActiveVmOwner();
    ResourceLoader::ResourceView caller;
    if (owner.empty() || !ResourceLoader::CopyCallerResource(ActiveVmOwnerNet(), ActiveVmOwnerGeneration(), owner, caller) || caller.root == 0) {
        PushFail(L);
        return 1;
    }
    if (type.empty() || IsBuiltinType(type)) {
        LUA_LOGE("createElement; unable to create more elements");
        PushFail(L);
        return 1;
    }
    std::string elementId;
    if (lua_type(L, 2) == LUA_TSTRING) ReadStringAt(L, 2, elementId);
    const uint32_t map = EnsureMap(caller.root, caller.name, caller.dynamic);
    const uint32_t id = map ? AllocId() : 0;
    if (!map || !id) {
        LUA_LOGE("createElement; unable to create more elements");
        PushFail(L);
        return 1;
    }
    Node node;
    node.id = id;
    node.type = type;
    node.name = std::move(elementId);
    node.ownsType = true;
    g_nodes.emplace(id, std::move(node));
    Link(id, map);
    PushElement(L, id);
    return 1;
}

// CLuaElementDefs::SetElementID (CLuaElementDefs.cpp 1741-1762).
int SetElementID(lua_State* L) {
    uint32_t id = 0;
    std::string name;
    if (!ReadElement(L, 1, id) || !ReadStringAt(L, 2, name) || !IsLocal(id)) {
        PushFail(L);
        return 1;
    }
    if (Node* node = Find(id)) node->name = name;
    else g_localNames[id] = name;
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaElementDefs::SetElementParent (CLuaElementDefs.cpp 2040-2066).
int SetElementParent(lua_State* L) {
    uint32_t id = 0;
    uint32_t parent = 0;
    if (!ReadElement(L, 1, id) || !ReadElement(L, 2, parent)) {
        PushFail(L);
        return 1;
    }
    // IsMyChild(&Parent, true): parent is this element, or this element is its ancestor.
    if (id == parent || IsAncestorOrSelf(parent, id)) {
        PushFail(L);
        return 1;
    }
    if (GuiIsElement(id)) {
        if (!GuiIsElement(parent)) {
            PushFail(L);
            return 1;
        }
        // GUI parent is stored in LuaGuiApi and has no write from here.
        static bool logged = false;
        if (!logged) {
            logged = true;
            LUA_LOGI("DIAG setElementParent: GUI parent write is not wired (LuaGuiApi)");
        }
        PushFail(L);
        return 1;
    }
    if (HasMapAncestor(parent) && IsLocal(id) && Find(id)) {
        Link(id, parent);
        lua_pushboolean(L, 1);
        return 1;
    }
    PushFail(L);
    return 1;
}

// CLuaElementDefs::SetElementCallPropagationEnabled (CLuaElementDefs.cpp 2591-2613).
int SetElementCallPropagationEnabled(lua_State* L) {
    uint32_t id = 0;
    bool enabled = false;
    if (!ReadElement(L, 1, id) || !ReadBoolAt(L, 2, false, enabled, false)) {
        PushFail(L);
        return 1;
    }
    if (CallPropOf(id) != enabled && IsLocal(id) && id != g_root) {
        SetCallProp(id, enabled);
        lua_pushboolean(L, 1);
        return 1;
    }
    PushFail(L);
    return 1;
}

// CLuaElementDefs::SetElementCollidableWith (CLuaElementDefs.cpp 2294-2317).
int SetElementCollidableWith(lua_State* L) {
    uint32_t id = 0;
    uint32_t with = 0;
    bool can = false;
    if (!ReadElement(L, 1, id) || !ReadElement(L, 2, with) || !ReadBoolAt(L, 3, false, can, true)) {
        PushFail(L);
        return 1;
    }
    if (!IsCollideSetType(TypeOf(id)) || !IsCollideSetType(TypeOf(with))) {
        PushFail(L);
        return 1;
    }
    SetPairCollide(id, with, can);
    lua_pushboolean(L, 1);
    return 1;
}

// Existing l_isElement, plus elements this file created.
int IsElement(lua_State* L) {
    lua_pushboolean(L, KnownElement(ElementIdOf(L, 1)) ? 1 : 0);
    return 1;
}

std::string ElementTypeName(uint32_t id) {
    if (id == 0) return "userdata";
    if (Node* node = Find(id); node && node->ownsType) return node->type;
    if (const char* gui = GuiElementType(id)) return gui;
    if (CameraElementAlive(id)) return "camera";
    if (SoundElementAlive(id)) return "sound";
    if (const char* stored = StoredElementType(id)) return stored;
    if (Game::ScriptPedAlive(id)) return "ped";
    const char* kind = ServerWorld::ElementKindName(id);
    return kind ? kind : "element";
}

// Existing l_getElementType, plus types this file owns (root, resource, map, createElement).
int GetElementType(lua_State* L) {
    const std::string type = ElementTypeName(ElementIdOf(L, 1));
    lua_pushlstring(L, type.data(), type.size());
    return 1;
}

// CLuaElementDefs::DestroyElement (CLuaElementDefs.cpp 1717-1738).
// False for a non-element, the root, the local player, a server element, and an
// element that is already gone. True only when DestroyNode deletes a local element.
// GUI ids are handled by DestroyElementDispatch before this binding runs.
int DestroyElement(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (ReleaseStoredElement(id)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    if (!KnownElement(id) || !Find(id)) {
        PushFail(L);
        return 1;
    }
    lua_pushboolean(L, DestroyNode(id) ? 1 : 0);
    return 1;
}

// CLuaElementDefs::GetAllElementData (CLuaElementDefsShared.cpp 64-68) and
// CClientEntity::GetAllCustomData (CClientEntity.cpp 305-316). Own keys only.
// setElementData values overlay the server copy, matching getElementData.
int GetAllElementData(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) {
        PushFail(L);
        return 1;
    }
    lua_newtable(L);
    const int table = lua_gettop(L);
    std::vector<std::pair<std::string, ServerWorld::ElementDataValue>> rows;
    ServerWorld::CopyElementData(id, rows);
    for (const auto& row : rows) {
        if (row.second.boolean) lua_pushboolean(L, row.second.value != 0 ? 1 : 0);
        else if (row.second.number) lua_pushnumber(L, row.second.value);
        else lua_pushlstring(L, row.second.text.data(), row.second.text.size());
        lua_setfield(L, table, row.first.c_str());
    }
    lua_getfield(L, LUA_REGISTRYINDEX, kElementDataKey);
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, static_cast<lua_Integer>(id));
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_type(L, -2) == LUA_TSTRING && !lua_isnil(L, -1)) {
                    lua_pushvalue(L, -2);
                    lua_pushvalue(L, -2);
                    lua_rawset(L, table);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 1;
}

void Bind(lua_State* L, const char* name, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    lua_setglobal(L, name);
}

}  // namespace

uint32_t ClientRootElement() { return g_root; }
bool KnownClientElement(uint32_t id) { return KnownElement(id); }
bool ClientDescendantOf(uint32_t id,uint32_t ancestor) {
    for(unsigned depth=0;id&&depth<64;++depth) {
        if(id==ancestor) return true;
        Node* node=Find(id);
        id=node&&node->parent?node->parent:(id!=g_root?g_root:0);
    }
    return false;
}
bool DestroyClientElement(uint32_t id) { return DestroyNode(id); }
void SetClientElementDestroyable(uint32_t id, bool can) {
    if (Node* node = Find(id)) node->canDestroy = can;
}
bool DestroyClientElementForced(uint32_t id) {
    const bool saved = g_forceDestroy;
    g_forceDestroy = true;
    const bool ok = DestroyNode(id);
    g_forceDestroy = saved;
    return ok;
}
std::string ClientElementTypeName(uint32_t id) { return ElementTypeName(id); }
uint32_t CreateResourceElement(const char* type) {
    const std::string owner=ActiveVmOwner(); ResourceLoader::ResourceView caller;
    if(owner.empty()||!ResourceLoader::CopyCallerResource(ActiveVmOwnerNet(),ActiveVmOwnerGeneration(),owner,caller)||!caller.root) return 0;
    const uint32_t map=EnsureMap(caller.root,caller.name,caller.dynamic);
    const uint32_t id=map?AllocId():0;
    if(!id) return 0;
    Node node; node.id=id; node.type=type; node.ownsType=true;
    g_nodes.emplace(id,std::move(node)); Link(id,map); return id;
}

void CopyClientChildren(uint32_t id, std::vector<uint32_t>& out) {
    out.clear();
    Node* node = Find(id);
    if (!node) return;
    for (uint32_t child : node->children) {
        Node* kid = Find(child);
        if (kid && !kid->beingDeleted) out.push_back(child);
    }
}

bool ClientCallPropagationEnabled(uint32_t id) { return CallPropOf(id); }


void EnsureResourceNode(uint32_t resourceRoot, const std::string& name) {
    if (!resourceRoot || Find(resourceRoot)) return;
    Node resource;
    resource.id = resourceRoot;
    resource.type = "resource";
    resource.name = name;
    resource.ownsType = true;
    resource.serverElement = true;
    resource.canDestroy = false;
    g_nodes.emplace(resourceRoot, std::move(resource));
    Link(resourceRoot, EnsureRoot());
}

void EnsureDynamicNode(uint32_t resourceRoot, uint32_t dynamicRoot) {
    if (!resourceRoot || !dynamicRoot || Find(dynamicRoot)) return;
    EnsureResourceNode(resourceRoot, "");
    Node map;
    map.id = dynamicRoot;
    map.type = "map";
    map.name = "dynamic";
    map.ownsType = true;
    map.serverElement = true;
    map.canDestroy = false;
    g_nodes.emplace(dynamicRoot, std::move(map));
    Link(dynamicRoot, resourceRoot);
}

void DestroyClientChildren(uint32_t id) {
    // CResource::DeleteClientChildren (CResource.cpp 1700-1704): local elements under
    // the resource go away with it. Server elements stay.
    Node* node = Find(id);
    if (!node) return;
    const std::vector<uint32_t> kids = node->children;
    for (uint32_t child : kids) {
        DestroyClientChildren(child);
        DestroyNode(child);
    }
}

void ReleaseResourceElements(uint32_t resourceRoot, uint32_t dynamicRoot) {
    ReleaseElementStreamingLeases(resourceRoot);
    if (dynamicRoot) DestroyClientChildren(dynamicRoot);
    if (resourceRoot) DestroyClientChildren(resourceRoot);
    auto drop = [](uint32_t id, const char* type) {
        Node* node = Find(id);
        if (!node || node->type != type) return;
        const std::vector<uint32_t> kids = node->children;
        for (uint32_t child : kids) Link(child, 0);
        Link(id, 0);
        g_nodes.erase(id);
    };
    if (dynamicRoot) {
        drop(dynamicRoot, "map");
        ServerWorld::ForgetPlaceholder(dynamicRoot);
    }
    if (resourceRoot) {
        drop(resourceRoot, "resource");
        ServerWorld::ForgetPlaceholder(resourceRoot);
    }
}

void CollectEventChain(uint32_t base, std::vector<uint32_t>& out) {
    // CClientEntity::CallParentEvent (CClientEntity.cpp 811-823): the element, then its parent.
    out.clear();
    uint32_t current = base;
    for (int guard = 0; current && guard < 64; ++guard) {
        out.push_back(current);
        Node* node = Find(current);
        bool propagate = true;
        if (node) propagate = node->callPropagation;
        else if (const auto flag = g_callProp.find(current); flag != g_callProp.end()) propagate = flag->second;
        if (!propagate) return;
        const uint32_t parent = node ? node->parent : 0;
        if (parent) {
            current = parent;
            continue;
        }
        if (g_root && g_root != current) out.push_back(g_root);
        return;
    }
}

bool StockPedModel(unsigned model) {
    return model <= 312 && model != 74 && model != 149 && model != 208;
}

int CurrentModel(uint32_t id) {
    int model = 0;
    float heading = 0.f;
    bool frozen = false;
    if (Game::CopyScriptPedFacts(id, model, heading, frozen)) return model;
    return ServerWorld::ModelOf(id);
}

// CLuaElementDefs.cpp:1450-1470 and CStaticFunctionDefinitions.cpp:806-834.
// A ped in a vehicle uses the vehicle's IsInWater. That bit is not stored.
int IsElementInWater(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const char* kind = ServerWorld::ElementKindName(id);
    const bool ped = kind && (std::strcmp(kind, "ped") == 0 || std::strcmp(kind, "player") == 0);
    const bool vehicle = kind && std::strcmp(kind, "vehicle") == 0;
    if (!ped && !vehicle) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (ped) {
        uint32_t occupied = 0;
        uint8_t seat = 0;
        if (WorldRPG::RemoteVehicle::FindOccupant(id, occupied, seat) ||
            (id == ServerWorld::LocalPlayer() && Game::LocalVehicleId() != 0 &&
             Game::LocalVehicleId() != 0xFFFFFFFFu)) {
            static bool once = false;
            if (!once) {
                once = true;
                LUA_LOGI("DIAG isElementInWater: CClientVehicle::IsInWater is not stored");
            }
            lua_pushboolean(L, 0);
            return 1;
        }
        PedQueries::Presentation flags = PedQueries::ReadPresentation(id);
        lua_pushboolean(L, flags.inWater ? 1 : 0);
        return 1;
    }
    static bool once = false;
    if (!once) {
        once = true;
        LUA_LOGI("DIAG isElementInWater: CClientVehicle::IsInWater is not stored");
    }
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaElementDefs.cpp:2419-2430 and CStaticFunctionDefinitions.cpp:1530-1712.
// Stock ped and vehicle ids are stored. Object models need CModelInfo. The native
// body is not rebuilt and onClientElementModelChange is not fired. Children are not walked.
int SetElementModel(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        LUA_LOGE("setElementModel: expected a number");
        lua_pushboolean(L, 0);
        return 1;
    }
    const double raw = lua_tonumber(L, 2);
    if (!std::isfinite(raw) || raw < 0 || raw > 65535) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const unsigned model = static_cast<unsigned>(raw);
    const char* kind = ServerWorld::ElementKindName(id);
    const bool scriptPed = Game::ScriptPedAlive(id);
    const bool ped = scriptPed || (kind && (std::strcmp(kind, "ped") == 0 || std::strcmp(kind, "player") == 0));
    const bool vehicle = kind && std::strcmp(kind, "vehicle") == 0;
    if (!ped && !vehicle) {
        static bool once = false;
        if (!once) {
            once = true;
            LUA_LOGI("DIAG setElementModel: CModelInfo is not available for objects");
        }
        lua_pushboolean(L, 0);
        return 1;
    }
    if (ped && !StockPedModel(model)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (vehicle && (model < 400 || model > 611)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int current = CurrentModel(id);
    if (current == static_cast<int>(model)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool stored = false;
    if (scriptPed) stored = Game::NoteScriptPedModel(id, static_cast<int>(model));
    else if (kind && std::strcmp(kind, "player") == 0) {
        ServerWorld::NotePlayerSkin(id, static_cast<int>(model));
        stored = ServerWorld::ModelOf(id) == static_cast<int>(model);
    } else {
        ServerWorld::NoteElementModel(id, static_cast<int>(model));
        stored = ServerWorld::ModelOf(id) == static_cast<int>(model);
    }
    if (!stored) {
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    if (!once) {
        once = true;
        LUA_LOGI("DIAG setElementModel: CPed/CVehicle::SetModel and onClientElementModelChange are not run");
    }
    lua_pushboolean(L, 1);
    return 1;
}

bool ReadOffset(lua_State* L, int index, float& x, float& y, float& z) {
    if (lua_isnoneornil(L, index)) {
        x = y = z = 0.f;
        return true;
    }
    if (!lua_isnumber(L, index) || !lua_isnumber(L, index + 1) || !lua_isnumber(L, index + 2)) return false;
    x = static_cast<float>(lua_tonumber(L, index));
    y = static_cast<float>(lua_tonumber(L, index + 1));
    z = static_cast<float>(lua_tonumber(L, index + 2));
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

// CLuaElementDefs.cpp:2184-2211. Physical attachment is not hooked, so a well-formed
// call returns false and does not change the server attachment snapshot.
int AttachElements(lua_State* L) {
    uint32_t id = 0, parent = 0;
    if (!ReadElement(L, 1, id) || !ReadElement(L, 2, parent)) {
        LUA_LOGE("attachElements: expected two elements");
        lua_pushboolean(L, 0);
        return 1;
    }
    float x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
    if (!ReadOffset(L, 3, x, y, z) || !ReadOffset(L, 6, rx, ry, rz)) {
        LUA_LOGE("attachElements: expected offset numbers");
        lua_pushboolean(L, 0);
        return 1;
    }
    (void)x;
    (void)y;
    (void)z;
    (void)rx;
    (void)ry;
    (void)rz;
    static bool once = false;
    if (!once) {
        once = true;
        LUA_LOGI("DIAG attachElements: physical attachment and onClientElementAttach are not run");
    }
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaElementDefs.cpp:2214-2227. Same rule: do not clear a server attachment.
int DetachElements(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, 1, id)) {
        LUA_LOGE("detachElements: expected an element");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!lua_isnoneornil(L, 2)) {
        uint32_t parent = 0;
        if (!ReadElement(L, 2, parent)) {
            LUA_LOGE("detachElements: expected an element");
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    static bool once = false;
    if (!once) {
        once = true;
        LUA_LOGI("DIAG detachElements: physical detach is not run");
    }
    lua_pushboolean(L, 0);
    return 1;
}

void InstallElementBindings(lua_State* L) {
    ResetCatalog();
    EnsureRoot();
    if (g_root) {
        PushElement(L, g_root);
        lua_setglobal(L, "root");
    }
    Bind(L, "getRootElement", GetRootElement);
    Bind(L, "getElementID", GetElementID);
    Bind(L, "isElementAttached", IsElementAttached);
    Bind(L, "getElementAttachedTo", GetElementAttachedTo);
    Bind(L, "getAttachedElements", GetAttachedElements);
    Bind(L, "getElementAttachedOffsets", GetElementAttachedOffsets);
    Bind(L, "getElementByID", GetElementByID);
    Bind(L, "getElementByIndex", GetElementByIndex);
    Bind(L, "getElementChildren", GetElementChildren);
    Bind(L, "getElementChild", GetElementChild);
    Bind(L, "getElementChildrenCount", GetElementChildrenCount);
    Bind(L, "getElementParent", GetElementParent);
    Bind(L, "isElementLocal", IsElementLocal);
    Bind(L, "hasElementData", HasElementData);
    Bind(L, "isElementCallPropagationEnabled", IsElementCallPropagationEnabled);
    Bind(L, "isElementCollidableWith", IsElementCollidableWith);
    Bind(L, "createElement", CreateElement);
    Bind(L, "setElementID", SetElementID);
    Bind(L, "setElementParent", SetElementParent);
    Bind(L, "setElementCallPropagationEnabled", SetElementCallPropagationEnabled);
    Bind(L, "setElementCollidableWith", SetElementCollidableWith);
    Bind(L, "isElement", IsElement);
    Bind(L, "getElementType", GetElementType);
    Bind(L, "destroyElement", DestroyElement);
    Bind(L, "getAllElementData", GetAllElementData);
    Bind(L, "isElementInWater", IsElementInWater);
    // CLuaCompatibilityDefs.cpp:22-23. Same function as isElementInWater.
    Bind(L, "isPedInWater", IsElementInWater);
    Bind(L, "isPlayerInWater", IsElementInWater);
    Bind(L, "setElementModel", SetElementModel);
    // CLuaCompatibilityDefs.cpp:46, 49 and 52.
    Bind(L, "setPlayerSkin", SetElementModel);
    Bind(L, "setVehicleModel", SetElementModel);
    Bind(L, "setPedSkin", SetElementModel);
    Bind(L, "attachElements", AttachElements);
    // CLuaCompatibilityDefs.cpp:60. Same function as attachElements.
    Bind(L, "attachElementToElement", AttachElements);
    Bind(L, "detachElements", DetachElements);
    // CLuaCompatibilityDefs.cpp:61.
    Bind(L, "detachElementFromElement", DetachElements);
    InstallElementTreeLua(L);
}

}  // namespace WorldRPG::Script

extern "C" void InstallElementLua(lua_State* L) {
    WorldRPG::Script::InstallElementBindings(L);
}
