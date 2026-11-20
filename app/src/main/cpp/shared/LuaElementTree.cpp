// The CLuaElementDefs calls that were still stubs or missing.
// Desktop: Client/mods/deathmatch/logic/luadefs/CLuaElementDefs.cpp and
// CStaticFunctionDefinitions.cpp. A step that needs a GTA body logs DIAG and
// keeps the value the desktop client stores when that body is absent.

#include "LuaElementTree.h"

#include "LuaApiExtra.h"
#include "LuaMarkerApi.h"
#include "LuaSpatialApi.h"
#include "LuaVMStub.h"
#include "LuaWorldObjectApi.h"
#include "PedQueries.h"
#include "ResourceLoader.h"
#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace WorldRPG::Script {
bool VehicleBodyFrozen(uint32_t id);

namespace {

constexpr uint32_t kMaxServerElements = 131072;

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    bool set = false;
};

struct Matrix {
    float row[4][3] = {};
};

struct Lease {
    uint32_t element = 0;
    uint32_t resourceRoot = 0;
};

struct BuildingLink {
    uint32_t low = 0;
    uint32_t high = 0;
};

std::unordered_map<uint32_t, Vec3> g_move;
std::unordered_map<uint32_t, Vec3> g_turn;
std::unordered_map<uint32_t, Matrix> g_matrix;
std::unordered_map<unsigned, Lease> g_leases;
std::unordered_map<uint32_t, unsigned short> g_streamRefs;
std::unordered_map<uint32_t, BuildingLink> g_buildings;
unsigned g_nextLease = 1;
bool g_diagMove = false;
bool g_diagTurn = false;
bool g_diagProjectile = false;
bool g_diagMatrix = false;
bool g_diagBounds = false;
bool g_diagRadius = false;
bool g_diagCom = false;
bool g_diagScreen = false;
bool g_diagGround = false;
bool g_diagFire = false;
bool g_diagObjectFire = false;
bool g_diagVehicleFire = false;
bool g_diagPickupCol = false;
bool g_diagServerLod = false;
bool g_diagBuildingLod = false;
bool g_diagStream = false;

bool ReadKnown(lua_State* L, int index, uint32_t& id);

void Diag(bool& once, const char* message) {
    if (once) return;
    once = true;
    LUA_LOGE("%s", message);
}

const char* LuaTypeName(int type) {
    switch (type) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TNUMBER: return "number";
        case LUA_TSTRING: return "string";
        case LUA_TTABLE: return "table";
        case LUA_TFUNCTION: return "function";
        case LUA_TTHREAD: return "thread";
        case LUA_TNONE: return "none";
        default: return "userdata";
    }
}

std::string ScriptGot(lua_State* L, int index) {
    const int type = lua_type(L, index);
    if (type == LUA_TTABLE) {
        uint32_t id = 0;
        if (ReadKnown(L, index, id)) return ClientElementTypeName(id);
    }
    if (type == LUA_TNUMBER || type == LUA_TSTRING) {
        const char* text = lua_tostring(L, index);
        if (text && text[0] != '\0') return std::string(LuaTypeName(type)) + " '" + text + "'";
    }
    return LuaTypeName(type);
}

std::string ParserGot(lua_State* L, int index) {
    const int type = lua_type(L, index);
    if (type == LUA_TBOOLEAN) return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
    if (type == LUA_TNUMBER) {
        const char* text = lua_tostring(L, index);
        return std::string("number (") + (text ? text : "0") + ")";
    }
    if (type == LUA_TSTRING) {
        const char* text = lua_tostring(L, index);
        std::string value = text ? text : "";
        if (value.size() > 10) {
            value.resize(10);
            value[7] = value[8] = value[9] = '.';
        }
        for (unsigned char ch : value) {
            if (ch < 32 || ch > 126) return "string";
        }
        return "string (\"" + value + "\")";
    }
    if (type == LUA_TNIL) return "nil";
    if (type == LUA_TNONE) return "none";
    if (type == LUA_TTABLE) return "table";
    if (type == LUA_TFUNCTION) return "function";
    if (type == LUA_TTHREAD) return "coroutine";
    return "userdata";
}

void Warn(const std::string& message) { LUA_LOGI("WARNING: %s", message.c_str()); }

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

int WarnAt(lua_State* L, const char* function, const char* expected, int index) {
    Warn(std::string("Bad argument @ '") + function + "' [Expected " + expected + " at argument " +
         std::to_string(index) + ", got " + ScriptGot(L, index) + "]");
    return Fail(L);
}

int RaiseAt(lua_State* L, const char* function, const char* expected, int index) {
    return luaL_error(L, "Bad argument @ '%s' [Expected %s at argument %d, got %s]", function, expected, index,
                      ParserGot(L, index).c_str());
}

bool ReadKnown(lua_State* L, int index, uint32_t& id) {
    id = ElementIdOf(L, index);
    return KnownClientElement(id);
}

bool ReadComponent(lua_State* L, const char* function, int index, float& out, bool optional, float fallback) {
    const int type = lua_type(L, index);
    if (optional && (type == LUA_TNONE || type == LUA_TNIL)) {
        out = fallback;
        return true;
    }
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        Warn(std::string("Bad argument @ '") + function + "' [Expected number at argument " + std::to_string(index) +
             ", got " + ScriptGot(L, index) + "]");
        return false;
    }
    if (!lua_isnumber(L, index)) {
        Warn(std::string("Bad argument @ '") + function + "' [Expected number, got non-convertible string]");
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        Warn(std::string("Bad argument @ '") + function + "' [Expected number, got NaN]");
        return false;
    }
    out = static_cast<float>(number);
    return true;
}

bool ReadVec(lua_State* L, const char* function, int index, float& x, float& y, float& z) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        WarnAt(L, function, "vector3", index);
        return false;
    }
    return ReadComponent(L, function, index, x, false, 0.f) && ReadComponent(L, function, index + 1, y, false, 0.f) &&
           ReadComponent(L, function, index + 2, z, false, 0.f);
}

bool ReadVecDefault(lua_State* L, const char* function, int index, float& x, float& y, float& z) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        x = y = z = 0.f;
        return true;
    }
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        WarnAt(L, function, "vector3", index);
        return false;
    }
    return ReadComponent(L, function, index, x, true, 0.f) && ReadComponent(L, function, index + 1, y, true, 0.f) &&
           ReadComponent(L, function, index + 2, z, true, 0.f);
}

enum class Motion { None, Full, Move };

Motion MotionOf(const std::string& type) {
    if (type == "ped" || type == "player" || type == "vehicle" || type == "object" || type == "weapon") return Motion::Full;
    if (type == "projectile" || type == "sound") return Motion::Move;
    return Motion::None;
}

bool BodyType(const std::string& type) {
    return type == "ped" || type == "player" || type == "vehicle" || type == "object" || type == "weapon" || type == "building";
}

bool RadiusType(const std::string& type) {
    return type == "ped" || type == "player" || type == "vehicle" || type == "object" || type == "weapon";
}

bool StreamingType(const std::string& type) {
    return type == "player" || type == "ped" || type == "vehicle" || type == "object" || type == "weapon" || type == "marker" ||
           type == "pickup" || type == "searchlight";
}

void Walk(uint32_t id, const std::function<void(uint32_t)>& fn, int depth) {
    if (depth > 64 || !ClientCallPropagationEnabled(id)) return;
    std::vector<uint32_t> kids;
    CopyClientChildren(id, kids);
    for (uint32_t child : kids) fn(child);
}

void StoreMove(uint32_t id, float x, float y, float z, int depth) {
    if (depth > 64) return;
    Walk(id, [&](uint32_t child) { StoreMove(child, x, y, z, depth + 1); }, depth);
    const std::string type = ClientElementTypeName(id);
    const Motion motion = MotionOf(type);
    if (motion == Motion::None) return;
    if (type == "vehicle" && VehicleBodyFrozen(id)) return;
    if (type == "projectile") {
        Diag(g_diagProjectile, "DIAG setElementVelocity: CClientProjectile::SetVelocity needs CProjectile; nothing is stored");
        return;
    }
    g_move[id] = Vec3{x, y, z, true};
    Diag(g_diagMove, "DIAG setElementVelocity: CPhysical::SetMoveSpeed is not called; the client vector is stored");
}

void StoreTurn(uint32_t id, float x, float y, float z, int depth) {
    if (depth > 64) return;
    Walk(id, [&](uint32_t child) { StoreTurn(child, x, y, z, depth + 1); }, depth);
    if (MotionOf(ClientElementTypeName(id)) != Motion::Full) return;
    if (ClientElementTypeName(id) == "vehicle" && VehicleBodyFrozen(id)) return;
    g_turn[id] = Vec3{x, y, z, true};
    Diag(g_diagTurn, "DIAG setElementAngularVelocity: CPhysical::SetTurnSpeed is not called; the client vector is stored");
}

int GetElementAngularVelocity(lua_State* L) {
    // CLuaElementDefs.cpp 604-629 and CStaticFunctionDefinitions.cpp 510-539.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "getElementAngularVelocity", "element", 1);
    if (MotionOf(ClientElementTypeName(id)) != Motion::Full) return Fail(L);
    const auto found = g_turn.find(id);
    const Vec3 value = found == g_turn.end() ? Vec3{} : found->second;
    lua_pushnumber(L, value.x);
    lua_pushnumber(L, value.y);
    lua_pushnumber(L, value.z);
    return 3;
}

int SetElementVelocity(lua_State* L) {
    // CLuaElementDefs.cpp 1983-2010 and CStaticFunctionDefinitions.cpp 1172-1216.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "setElementVelocity", "element", 1);
    float x = 0.f, y = 0.f, z = 0.f;
    if (!ReadVec(L, "setElementVelocity", 2, x, y, z)) return Fail(L);
    if (ClientElementTypeName(id) == "radararea") z = 0.f;
    StoreMove(id, x, y, z, 0);
    lua_pushboolean(L, MotionOf(ClientElementTypeName(id)) != Motion::None ? 1 : 0);
    return 1;
}

int SetElementAngularVelocity(lua_State* L) {
    // CLuaElementDefs.cpp 2013-2037 and CStaticFunctionDefinitions.cpp 1219-1250.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "setElementAngularVelocity", "element", 1);
    float x = 0.f, y = 0.f, z = 0.f;
    if (!ReadVec(L, "setElementAngularVelocity", 2, x, y, z)) return Fail(L);
    StoreTurn(id, x, y, z, 0);
    lua_pushboolean(L, MotionOf(ClientElementTypeName(id)) == Motion::Full ? 1 : 0);
    return 1;
}

int GetElementBoundingBox(lua_State* L) {
    // CLuaElementDefs.cpp 1083-1110 and CStaticFunctionDefinitions.cpp 548-600.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "getElementBoundingBox", "element", 1);
    if (BodyType(ClientElementTypeName(id)))
        Diag(g_diagBounds, "DIAG getElementBoundingBox: CModelInfo::GetBoundingBox is not loaded");
    return Fail(L);
}

int GetElementRadius(lua_State* L) {
    // CLuaElementDefs.cpp 1113-1135 and CStaticFunctionDefinitions.cpp 603-639.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "getElementRadius", "element", 1);
    if (RadiusType(ClientElementTypeName(id)))
        Diag(g_diagRadius, "DIAG getElementRadius: CModelInfo::GetBoundingBox is not loaded");
    return Fail(L);
}

int GetElementDistanceFromCentreOfMassToBaseOfModel(lua_State* L) {
    // CLuaElementDefs.cpp 1234-1256 and CStaticFunctionDefinitions.cpp 654-682.
    // Unstreamed ped, vehicle, object and building methods return 0.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "getElementDistanceFromCentreOfMassToBaseOfModel", "element", 1);
    if (!BodyType(ClientElementTypeName(id))) return Fail(L);
    Diag(g_diagCom, "DIAG getElementDistanceFromCentreOfMassToBaseOfModel: the game entity is absent, so the distance is 0");
    lua_pushnumber(L, 0);
    return 1;
}

int IsElementOnScreen(lua_State* L) {
    // CLuaElementDefs.cpp 1369-1374. ArgumentParserWarn<nullptr>: a bad argument is nil.
    // CClientEntity::IsOnScreen is false without a game entity. A marker without its
    // atomic is false (CClientMarker.cpp 549-552).
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) {
        Warn(std::string("Bad argument @ 'isElementOnScreen' [Expected element at argument 1, got ") + ParserGot(L, 1) + "]");
        lua_pushnil(L);
        return 1;
    }
    Diag(g_diagScreen, "DIAG isElementOnScreen: CEntity::IsOnScreen and CClientMarker::IsOnScreen are not read");
    lua_pushboolean(L, 0);
    return 1;
}

int IsElementWaitingForGroundToLoad(lua_State* L) {
    // CLuaElementDefs.cpp 2640-2660 and CStaticFunctionDefinitions.cpp 4105-4125.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "isElementWaitingForGroundToLoad", "element", 1);
    const std::string type = ClientElementTypeName(id);
    if (type != "player" && type != "ped" && type != "vehicle") return Fail(L);
    Diag(g_diagGround, "DIAG isElementWaitingForGroundToLoad: the frozen-waiting flag is not stored");
    lua_pushboolean(L, 0);
    return 1;
}

int GetElementColShape(lua_State* L) {
    // CLuaElementDefs.cpp 1426-1447 and CStaticFunctionDefinitions.cpp 7781-7795.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "getElementColShape", "element", 1);
    const std::string type = ClientElementTypeName(id);
    if (type == "marker") {
        const uint32_t shape = MarkerColShape(id);
        if (!shape) return Fail(L);
        PushElement(L, shape);
        return 1;
    }
    if (type == "pickup")
        Diag(g_diagPickupCol, "DIAG getElementColShape: CClientPickup creates its sphere when CPickup streams in");
    return Fail(L);
}

int IsElementWithinMarker(lua_State* L) {
    // CLuaElementDefs.cpp 899-920. A bad argument is nil. CollisionExists reads the cached list.
    uint32_t id = 0;
    uint32_t marker = 0;
    if (!ReadKnown(L, 1, id)) {
        WarnAt(L, "isElementWithinMarker", "element", 1);
        lua_pushnil(L);
        return 1;
    }
    if (!ReadKnown(L, 2, marker) || ClientElementTypeName(marker) != "marker") {
        WarnAt(L, "isElementWithinMarker", "marker", 2);
        lua_pushnil(L);
        return 1;
    }
    const uint32_t shape = MarkerColShape(marker);
    lua_pushboolean(L, shape && ElementInsideShape(id, shape) ? 1 : 0);
    return 1;
}

bool ReadMatrixTable(lua_State* L, int index, float m[4][4]) {
    // CLuaFunctionParseHelpers.cpp ReadMatrix 1229-1268.
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) m[row][col] = row == col ? 1.f : 0.f;
    if (!lua_istable(L, index)) return false;
    const int table = lua_absindex(L, index);
    unsigned rows = 0;
    unsigned cells = 0;
    lua_pushnil(L);
    while (lua_next(L, table) != 0) {
        if (lua_type(L, -1) == LUA_TTABLE && rows < 4) {
            unsigned cols = 0;
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                const int value = lua_type(L, -1);
                if ((value == LUA_TNUMBER || value == LUA_TSTRING) && rows < 4 && cols < 4)
                    m[rows][cols] = static_cast<float>(lua_tonumber(L, -1));
                lua_pop(L, 1);
                ++cols;
                ++cells;
            }
            if (cols != 4) {
                lua_pop(L, 1);
                return false;
            }
        }
        lua_pop(L, 1);
        ++rows;
    }
    return rows == 4 && cells == 16;
}

void ApplyPosition(lua_State* L, uint32_t id, float x, float y, float z) {
    lua_getglobal(L, "setElementPosition");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    PushElement(L, id);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    if (lua_pcall(L, 4, 0, 0) != LUA_OK) lua_pop(L, 1);
}

void StoreMatrix(lua_State* L, uint32_t id, const float m[4][3], int depth) {
    if (depth > 64) return;
    Walk(id, [&](uint32_t child) { StoreMatrix(L, child, m, depth + 1); }, depth);
    Matrix stored;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 3; ++col) stored.row[row][col] = m[row][col];
    g_matrix[id] = stored;
    ApplyPosition(L, id, m[3][0], m[3][1], m[3][2]);
    Diag(g_diagMatrix, "DIAG setElementMatrix: CEntity::SetMatrix and ConvertMatrixToEulerAngles are not run");
}

int SetElementMatrix(lua_State* L) {
    // CLuaElementDefs.cpp 1847-1882 and CStaticFunctionDefinitions.cpp 1063-1069.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "setElementMatrix", "element", 1);
    float table[4][4];
    float rows[4][3];
    if (lua_istable(L, 2)) {
        if (!ReadMatrixTable(L, 2, table)) {
            Warn("Bad usage @ 'setElementMatrix' [Matrix is not 4 x 4]");
            return Fail(L);
        }
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 3; ++col) rows[row][col] = table[row][col];
    } else if (lua_type(L, 2) == LUA_TNUMBER || lua_type(L, 2) == LUA_TSTRING) {
        // CScriptArgReader::ReadMatrix number order: front, position, right, up.
        float front[3], pos[3], right[3], up[3];
        if (!ReadVec(L, "setElementMatrix", 2, front[0], front[1], front[2]) ||
            !ReadVec(L, "setElementMatrix", 5, pos[0], pos[1], pos[2]) ||
            !ReadVec(L, "setElementMatrix", 8, right[0], right[1], right[2]) ||
            !ReadVec(L, "setElementMatrix", 11, up[0], up[1], up[2]))
            return Fail(L);
        for (int col = 0; col < 3; ++col) {
            rows[0][col] = right[col];
            rows[1][col] = front[col];
            rows[2][col] = up[col];
            rows[3][col] = pos[col];
        }
    } else {
        return WarnAt(L, "setElementMatrix", "matrix", 2);
    }
    StoreMatrix(L, id, rows, 0);
    lua_pushboolean(L, 1);
    return 1;
}

int SetElementAttachedOffsets(lua_State* L) {
    // CLuaElementDefs.cpp 2241-2267 and CStaticFunctionDefinitions.cpp 1437-1443.
    // Offsets are stored in degrees because getElementAttachedOffsets already returns degrees.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "setElementAttachedOffsets", "element", 1);
    float x = 0.f, y = 0.f, z = 0.f, rx = 0.f, ry = 0.f, rz = 0.f;
    if (!ReadVecDefault(L, "setElementAttachedOffsets", 2, x, y, z) ||
        !ReadVecDefault(L, "setElementAttachedOffsets", 5, rx, ry, rz))
        return Fail(L);
    const auto apply = [&](auto&& self, uint32_t element, int depth) -> void {
        if (depth > 64) return;
        Walk(element, [&](uint32_t child) { self(self, child, depth + 1); }, depth);
        ServerWorld::NoteAttachedOffsets(element, x, y, z, rx, ry, rz);
    };
    apply(apply, id, 0);
    static bool once = false;
    Diag(once, "DIAG setElementAttachedOffsets: CPhysical::SetAttachedOffsets is not called");
    lua_pushboolean(L, 1);
    return 1;
}

void UnlinkBuilding(uint32_t id) {
    const auto self = g_buildings.find(id);
    if (self == g_buildings.end()) return;
    if (self->second.low) {
        const auto other = g_buildings.find(self->second.low);
        if (other != g_buildings.end() && other->second.high == id) other->second.high = 0;
    }
    if (self->second.high) {
        const auto other = g_buildings.find(self->second.high);
        if (other != g_buildings.end() && other->second.low == id) other->second.low = 0;
    }
    g_buildings.erase(self);
}

bool SetBuildingLod(uint32_t id, uint32_t low, bool clear) {
    // CClientBuilding::SetLowLodBuilding (CClientBuilding.cpp 174-205). It returns true
    // even when the link is already clear. CBuilding::SetLod is not called.
    if (clear || low == 0) {
        auto& self = g_buildings[id];
        if (self.low) {
            const auto other = g_buildings.find(self.low);
            if (other != g_buildings.end() && other->second.high == id) other->second.high = 0;
            self.low = 0;
        }
        Diag(g_diagBuildingLod, "DIAG setLowLODElement: CBuilding::SetLod is not called");
        return true;
    }
    SetBuildingLod(id, 0, true);
    auto& lod = g_buildings[low];
    if (lod.high && lod.high != id) g_buildings[lod.high].low = 0;
    g_buildings[id].low = low;
    g_buildings[low].high = id;
    Diag(g_diagBuildingLod, "DIAG setLowLODElement: CBuilding::SetLod is not called");
    return true;
}

bool ApplyLowLod(uint32_t id, uint32_t low, bool clear, int depth) {
    if (depth > 64) return false;
    Walk(id, [&](uint32_t child) { ApplyLowLod(child, low, clear, depth + 1); }, depth);
    const std::string type = ClientElementTypeName(id);
    if (!clear && low != 0 && ClientElementTypeName(low) != type) return false;
    if (type == "object") {
        bool created = false;
        uint32_t linked = 0;
        if (!CopyObjectLod(id, created, linked)) {
            Diag(g_diagServerLod, "DIAG setLowLODElement: the ENTITY_ADD low LOD link is not stored");
            return false;
        }
        return SetObjectLod(id, low, clear);
    }
    if (type == "building") return SetBuildingLod(id, low, clear);
    return false;
}

int GetLowLodElement(lua_State* L) {
    // CLuaElementDefs.cpp 2529-2550. A missing link is nil. Another type is false.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "getLowLODElement", "element", 1);
    const std::string type = ClientElementTypeName(id);
    if (type == "object") {
        bool created = false;
        uint32_t linked = 0;
        if (!CopyObjectLod(id, created, linked)) {
            Diag(g_diagServerLod, "DIAG getLowLODElement: the ENTITY_ADD low LOD link is not stored");
            lua_pushnil(L);
            return 1;
        }
        if (created || linked == 0 || !KnownClientElement(linked)) lua_pushnil(L);
        else PushElement(L, linked);
        return 1;
    }
    if (type == "building") {
        const auto found = g_buildings.find(id);
        const uint32_t linked = found == g_buildings.end() ? 0 : found->second.low;
        if (linked == 0 || !KnownClientElement(linked)) lua_pushnil(L);
        else PushElement(L, linked);
        return 1;
    }
    return Fail(L);
}

int IsElementLowLod(lua_State* L) {
    // CLuaElementDefs.cpp 2567-2588. Object uses the create flag. Building uses the high-LOD back link.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "isElementLowLOD", "element", 1);
    const std::string type = ClientElementTypeName(id);
    if (type == "object") {
        bool created = false;
        uint32_t linked = 0;
        if (!CopyObjectLod(id, created, linked)) {
            Diag(g_diagServerLod, "DIAG isElementLowLOD: the ENTITY_ADD low LOD flag is not stored");
            return Fail(L);
        }
        lua_pushboolean(L, created ? 1 : 0);
        return 1;
    }
    if (type == "building") {
        const auto found = g_buildings.find(id);
        lua_pushboolean(L, found != g_buildings.end() && found->second.high != 0 ? 1 : 0);
        return 1;
    }
    return Fail(L);
}

int SetLowLodElement(lua_State* L) {
    // ArgumentParser. CLuaElementDefs.cpp 2553-2556 and CStaticFunctionDefinitions.cpp 4001-4032.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return RaiseAt(L, "setLowLODElement", "element", 1);
    bool clear = false;
    uint32_t low = 0;
    if (lua_isnoneornil(L, 2)) clear = true;
    else if (!ReadKnown(L, 2, low)) return RaiseAt(L, "setLowLODElement", "element", 2);
    lua_pushboolean(L, ApplyLowLod(id, low, clear, 0) ? 1 : 0);
    return 1;
}

bool MayBurn(uint32_t id) {
    if (id == ServerWorld::LocalPlayer() || Game::ScriptPedAlive(id)) return true;
    return id >= kMaxServerElements;
}

int SetElementOnFire(lua_State* L) {
    // ArgumentParser. CLuaElementDefs.cpp 2559-2564. Only a local element or the local player.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return RaiseAt(L, "setElementOnFire", "element", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return RaiseAt(L, "setElementOnFire", "boolean", 2);
    if (!MayBurn(id)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool onFire = lua_toboolean(L, 2) != 0;
    const std::string type = ClientElementTypeName(id);
    if (type == "ped" || type == "player") {
        PedQueries::OnFire(id, onFire);
        Game::SetPedOnFire(id, onFire);
        Diag(g_diagFire, "DIAG setElementOnFire: CPed::SetOnFire is not called; the flag is stored");
        lua_pushboolean(L, 1);
        return 1;
    }
    if (type == "vehicle")
        Diag(g_diagVehicleFire, "DIAG setElementOnFire: CVehicle::SetOnFire is not called");
    else if (type == "object" || type == "weapon")
        Diag(g_diagObjectFire, "DIAG setElementOnFire: CObject::SetOnFire is not called");
    lua_pushboolean(L, 0);
    return 1;
}

int IsElementOnFire(lua_State* L) {
    // ArgumentParser. CLuaElementDefs.cpp 2701-2703. An unstreamed vehicle or object is false.
    // An unstreamed ped returns the stored flag (CClientPed.h IsOnFire).
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return RaiseAt(L, "isElementOnFire", "element", 1);
    const std::string type = ClientElementTypeName(id);
    if (type == "ped" || type == "player") {
        lua_pushboolean(L, PedQueries::ReadPresentation(id).onFire ? 1 : 0);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

bool ResourceRoot(uint32_t& root) {
    const std::string owner = ActiveVmOwner();
    ResourceLoader::ResourceView caller;
    if (owner.empty() || !ResourceLoader::CopyCallerResource(ActiveVmOwnerNet(), ActiveVmOwnerGeneration(), owner, caller) ||
        !caller.root)
        return false;
    root = caller.root;
    return true;
}

bool ReleaseToken(unsigned token) {
    const auto found = g_leases.find(token);
    if (found == g_leases.end()) return false;
    const uint32_t element = found->second.element;
    g_leases.erase(found);
    const auto refs = g_streamRefs.find(element);
    if (refs != g_streamRefs.end() && refs->second > 0) {
        --refs->second;
        if (refs->second == 0) {
            g_streamRefs.erase(refs);
            Diag(g_diagStream, "DIAG releaseElementStreamingLease: CClientStreamer::OnElementForceStreamOut is not called");
        }
    }
    return true;
}

int AcquireElementStreamingLease(lua_State* L) {
    // CLuaElementDefs.cpp 2479-2503 and CResource.cpp 304-319.
    uint32_t id = 0;
    if (!ReadKnown(L, 1, id)) return WarnAt(L, "acquireElementStreamingLease", "element", 1);
    if (!StreamingType(ClientElementTypeName(id))) return Fail(L);
    uint32_t root = 0;
    if (!ResourceRoot(root)) return Fail(L);
    const unsigned short refs = g_streamRefs[id];
    if (refs == 0xFFFF) return Fail(L);
    unsigned token = 0;
    do {
        token = g_nextLease++;
    } while (token == 0 || g_leases.contains(token));
    g_leases.emplace(token, Lease{id, root});
    g_streamRefs[id] = static_cast<unsigned short>(refs + 1);
    if (refs == 0)
        Diag(g_diagStream, "DIAG acquireElementStreamingLease: CClientStreamer::OnElementForceStreamIn is not called");
    lua_pushnumber(L, token);
    return 1;
}

int ReleaseElementStreamingLease(lua_State* L) {
    // CLuaElementDefs.cpp 2506-2526 and CResource.cpp 322-333.
    const int type = lua_type(L, 1);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) return WarnAt(L, "releaseElementStreamingLease", "number", 1);
    if (!lua_isnumber(L, 1)) {
        Warn("Bad argument @ 'releaseElementStreamingLease' [Expected number, got non-convertible string]");
        return Fail(L);
    }
    const lua_Number number = lua_tonumber(L, 1);
    if (std::isnan(number)) {
        Warn("Bad argument @ 'releaseElementStreamingLease' [Expected number, got NaN]");
        return Fail(L);
    }
    if (number < 0.0 || number >= 4294967296.0) {
        Warn("Bad argument @ 'releaseElementStreamingLease' [Expected number within the destination type range, got out-of-range number]");
        return Fail(L);
    }
    lua_pushboolean(L, ReleaseToken(static_cast<unsigned>(number)) ? 1 : 0);
    return 1;
}

void Bind(lua_State* L, const char* name, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    lua_setglobal(L, name);
}

}  // namespace

void NoteClientElementDestroyed(uint32_t id) {
    g_move.erase(id);
    g_turn.erase(id);
    g_matrix.erase(id);
    g_streamRefs.erase(id);
    for (auto it = g_leases.begin(); it != g_leases.end();) {
        if (it->second.element == id) it = g_leases.erase(it);
        else ++it;
    }
    UnlinkBuilding(id);
}

void ResetElementTreeExtras() {
    g_move.clear();
    g_turn.clear();
    g_matrix.clear();
    g_leases.clear();
    g_streamRefs.clear();
    g_buildings.clear();
    g_nextLease = 1;
}

void ReleaseElementStreamingLeases(uint32_t resourceRoot) {
    if (!resourceRoot) return;
    std::vector<unsigned> tokens;
    for (const auto& [token, lease] : g_leases)
        if (lease.resourceRoot == resourceRoot) tokens.push_back(token);
    for (unsigned token : tokens) ReleaseToken(token);
}

bool CopyScriptMoveSpeed(uint32_t id, float& x, float& y, float& z) {
    if (VehicleBodyFrozen(id)) {
        x = y = z = 0.f;
        return true;
    }
    const auto found = g_move.find(id);
    if (found == g_move.end() || !found->second.set) return false;
    x = found->second.x;
    y = found->second.y;
    z = found->second.z;
    return true;
}

bool CopyScriptMatrix(uint32_t id, float rows[4][3]) {
    const auto found = g_matrix.find(id);
    if (found == g_matrix.end()) return false;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 3; ++col) rows[row][col] = found->second.row[row][col];
    return true;
}

void InstallElementTreeLua(lua_State* L) {
    Bind(L, "getElementAngularVelocity", GetElementAngularVelocity);
    Bind(L, "setElementVelocity", SetElementVelocity);
    Bind(L, "setElementAngularVelocity", SetElementAngularVelocity);
    Bind(L, "getElementBoundingBox", GetElementBoundingBox);
    Bind(L, "getElementRadius", GetElementRadius);
    Bind(L, "getElementDistanceFromCentreOfMassToBaseOfModel", GetElementDistanceFromCentreOfMassToBaseOfModel);
    Bind(L, "isElementOnScreen", IsElementOnScreen);
    Bind(L, "isElementWaitingForGroundToLoad", IsElementWaitingForGroundToLoad);
    Bind(L, "getElementColShape", GetElementColShape);
    Bind(L, "isElementWithinMarker", IsElementWithinMarker);
    Bind(L, "setElementMatrix", SetElementMatrix);
    Bind(L, "setElementAttachedOffsets", SetElementAttachedOffsets);
    Bind(L, "getLowLODElement", GetLowLodElement);
    Bind(L, "isElementLowLOD", IsElementLowLod);
    Bind(L, "setLowLODElement", SetLowLodElement);
    Bind(L, "setElementOnFire", SetElementOnFire);
    Bind(L, "isElementOnFire", IsElementOnFire);
    Bind(L, "acquireElementStreamingLease", AcquireElementStreamingLease);
    Bind(L, "releaseElementStreamingLease", ReleaseElementStreamingLease);
}

}  // namespace WorldRPG::Script
