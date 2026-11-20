// Client object Lua. Desktop sources:
//   CLuaObjectDefs.cpp 100-898
//   CStaticFunctionDefinitions.cpp 3971-3998 (SetElementFrozen) and 4128-4443
//   CClientObject.cpp 51-88, 113-155, 656-718, 1044-1090
//   CClientObjectManager.cpp 17-32 and 92-139
//   CDeathmatchObject.cpp 53-92 and CDeathmatchObject.h IsMoving
// CObject is not created. Break, gang-tag alpha, and texture replace fail closed.
// onClientObjectMoveStart / onClientObjectMoveStop are not dispatched.

#include "LuaWorldObjectApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ModelInfoRead.h"
#include "ResourceLoader.h"
#include "ServerWorld.h"

#include "lua.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldRPG::Script {
namespace {

constexpr uint32_t kServerModelMax = 65534;
constexpr float kPi = 3.14159265358979323846f;
constexpr int kMaterialSlots = 16;

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct Move {
    bool active = false;
    std::chrono::steady_clock::time_point start{};
    uint32_t durationMs = 0;
    Vec3 sourcePos{};
    Vec3 sourceRot{};
    Vec3 targetPos{};
    Vec3 targetRot{};
    bool linear = true;
};

struct Material {
    bool enabled = false;
    int source = 0;
    std::string txd;
    std::string texture;
    uint32_t color = 0;
};

struct Object {
    uint32_t id = 0;
    uint16_t model = 0;
    bool lowLod = false;
    uint32_t lowLodId = 0;
    Vec3 pos{};
    Vec3 rot{};
    Vec3 scale{1.f, 1.f, 1.f};
    bool frozen = false;
    bool breakDisabled = false;
    bool respawn = true;
    bool respawning = false;
    float mass = -1.f;
    float turnMass = -1.f;
    float air = -1.f;
    float elasticity = -1.f;
    float buoyancy = -1.f;
    Vec3 center{};
    Move move{};
    Material materials[kMaterialSlots]{};
    bool gangOwned = false;
    uint16_t gangNet = 0;
    uint32_t gangGeneration = 0;
    uint8_t gangProgress = 0;
    bool gangSpray = true;
};

std::unordered_map<uint32_t, Object> g_objects;
bool g_diagModel = false;
bool g_diagSpawn = false;
bool g_diagBreak = false;
bool g_diagRespawn = false;
bool g_diagBreakable = false;
bool g_diagAlpha = false;
bool g_diagTexture = false;
bool g_diagMove = false;
bool g_diagCurve = false;

const char* LuaTypeName(int type) {
    switch (type) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TLIGHTUSERDATA: return "userdata";
        case LUA_TNUMBER: return "number";
        case LUA_TSTRING: return "string";
        case LUA_TTABLE: return "table";
        case LUA_TFUNCTION: return "function";
        case LUA_TUSERDATA: return "userdata";
        case LUA_TTHREAD: return "thread";
        default: return "none";
    }
}

void LogFull(const char* function, const char* category, const char* message) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s @ '%s' [%s]", category, function, message);
    LUA_LOGI("WARNING: %s", buf);
}

void LogTypeError(lua_State* L, const char* function, const char* expected, int index) {
    const int type = lua_type(L, index);
    const char* value = nullptr;
    if (type == LUA_TNUMBER || type == LUA_TSTRING) value = lua_tostring(L, index);
    char buf[512];
    if (value && value[0] != '\0') {
        std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [Expected %s at argument %d, got %s '%.64s']",
                      function, expected, index, LuaTypeName(type), value);
    } else {
        std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [Expected %s at argument %d, got %s]", function, expected,
                      index, LuaTypeName(type));
    }
    LUA_LOGI("WARNING: %s", buf);
}

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

int Raise(lua_State* L, const char* function, const char* expected, int index) {
    return luaL_error(L, "Bad argument @ '%s' [Expected %s at argument %d, got %s]", function, expected, index,
                      LuaTypeName(lua_type(L, index)));
}

bool IsElementArg(lua_State* L, int index) {
    if (!lua_istable(L, index)) return false;
    lua_getfield(L, index, "element");
    const bool element = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return element;
}

bool FiniteAt(lua_State* L, int index, float& out) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) return false;
    if (!lua_isnumber(L, index)) return false;
    const lua_Number number = lua_tonumber(L, index);
    if (!std::isfinite(number)) return false;
    out = static_cast<float>(number);
    return true;
}

bool ReadFinite(lua_State* L, const char* function, int index, float& out) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        LogTypeError(L, function, "number", index);
        return false;
    }
    if (!lua_isnumber(L, index)) {
        LogFull(function, "Bad argument", "Expected number, got non-convertible string");
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        LogFull(function, "Bad argument", "Expected number, got NaN");
        return false;
    }
    if (!std::isfinite(number)) {
        LogFull(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    out = static_cast<float>(number);
    return true;
}

bool ReadOptionalFinite(lua_State* L, const char* function, int index, float& out, float fallback) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        out = fallback;
        return true;
    }
    return ReadFinite(L, function, index, out);
}

bool ReadBool(lua_State* L, const char* function, int index, bool& out, bool optional, bool fallback) {
    const int type = lua_type(L, index);
    if (type == LUA_TBOOLEAN) {
        out = lua_toboolean(L, index) != 0;
        return true;
    }
    if (optional && (type == LUA_TNONE || type == LUA_TNIL)) {
        out = fallback;
        return true;
    }
    LogTypeError(L, function, "boolean", index);
    return false;
}

// CScriptArgReader::ReadString. A number has lua_objlen 0, so the text is empty.
bool ReadScriptString(lua_State* L, const char* function, int index, std::string& out) {
    const int type = lua_type(L, index);
    if (type == LUA_TNUMBER) {
        (void)lua_rawlen(L, index);
        out.clear();
        return true;
    }
    if (type != LUA_TSTRING) {
        LogTypeError(L, function, "string", index);
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    out.assign(text ? text : "", text ? length : 0);
    return true;
}

bool ReadBoundedUint(lua_State* L, const char* function, int index, uint32_t& out, uint32_t min, uint32_t max) {
    float number = 0.f;
    if (!ReadFinite(L, function, index, number)) return false;
    if (number < -static_cast<float>(FLT_EPSILON)) {
        LogFull(function, "Bad argument", "Expected positive value, got negative");
        return false;
    }
    if (number < static_cast<float>(min) || number > static_cast<float>(max)) {
        LogFull(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    out = static_cast<uint32_t>(number);
    return true;
}

bool ReadInt(lua_State* L, const char* function, int index, int& out) {
    float number = 0.f;
    if (!ReadFinite(L, function, index, number)) return false;
    if (number < static_cast<float>(std::numeric_limits<int>::min()) ||
        number > static_cast<float>(std::numeric_limits<int>::max())) {
        LogFull(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

bool EqualsIgnoreCase(const std::string& text, const char* lower) {
    const size_t length = std::strlen(lower);
    if (text.size() != length) return false;
    for (size_t i = 0; i < length; ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) != lower[i]) return false;
    }
    return true;
}

float WrapOnce(float radians) {
    const float turn = 2.f * kPi;
    if (radians < 0.f) return radians + turn;
    if (radians >= turn) return radians - turn;
    return radians;
}

// CClientObjectManager::IsValidModel static exclusions. Ped 0..312, the unused
// gap 313..320, clothes 384..397, vehicles 400..611, and ids at or above the
// TXD base 20000. Cutscene objects 300..314 sit inside the ped and gap ranges.
bool StockObjectRange(uint32_t model) {
    if (model >= Models::kStockModelCount) return false;
    if (model <= 312) return false;
    if (model >= 313 && model <= 320) return false;
    if (model >= 384 && model <= 397) return false;
    if (model >= 400 && model <= 611) return false;
    return true;
}

bool ObjectModelAllowed(uint32_t model) {
    if (!StockObjectRange(model)) return false;
    Models::StockModelView view;
    const Models::LiveModelStatus status = Models::ReadLiveStockModel(static_cast<uint16_t>(model), view);
    if (status == Models::LiveModelStatus::Missing) return false;
    if (status == Models::LiveModelStatus::Unavailable && !g_diagModel) {
        g_diagModel = true;
        LUA_LOGE("DIAG createObject: CModelInfo is not readable; stock object ranges are used and archive allocation is not checked");
    }
    (void)view;
    return true;
}

bool BreakableModel(uint16_t model) {
    // CClientObjectManager.cpp g_usBreakableModelList, including the glass tail.
    static const uint16_t kIds[] = {
        625,  626,  627,  628,  629,  630,  631,  632,  633,  642,  643,  644,  646,  650,  716,  717,  737,  738,  792,  858,
        881,  882,  883,  884,  885,  886,  887,  888,  889,  890,  891,  892,  893,  894,  895,  904,  905,  941,  955,  956,
        959,  961,  990,  993,  996,  1209, 1211, 1213, 1219, 1220, 1221, 1223, 1224, 1225, 1226, 1227, 1228, 1229, 1230, 1231,
        1232, 1235, 1238, 1244, 1251, 1255, 1257, 1262, 1264, 1265, 1270, 1280, 1281, 1282, 1283, 1284, 1285, 1286, 1287, 1288,
        1289, 1290, 1291, 1293, 1294, 1297, 1300, 1302, 1315, 1328, 1329, 1330, 1338, 1350, 1351, 1352, 1370, 1373, 1374, 1375,
        1407, 1408, 1409, 1410, 1411, 1412, 1413, 1414, 1415, 1417, 1418, 1419, 1420, 1421, 1422, 1423, 1424, 1425, 1426, 1428,
        1429, 1431, 1432, 1433, 1436, 1437, 1438, 1440, 1441, 1443, 1444, 1445, 1446, 1447, 1448, 1449, 1450, 1451, 1452, 1456,
        1457, 1458, 1459, 1460, 1461, 1462, 1463, 1464, 1465, 1466, 1467, 1468, 1469, 1470, 1471, 1472, 1473, 1474, 1475, 1476,
        1477, 1478, 1479, 1480, 1481, 1482, 1483, 1514, 1517, 1520, 1534, 1543, 1544, 1545, 1551, 1553, 1554, 1558, 1564, 1568,
        1582, 1583, 1584, 1588, 1589, 1590, 1591, 1592, 1645, 1646, 1647, 1654, 1664, 1666, 1667, 1668, 1669, 1670, 1672, 1676,
        1684, 1686, 1775, 1776, 1949, 1950, 1951, 1960, 1961, 1962, 1975, 1976, 1977, 2647, 2663, 2682, 2683, 2885, 2886, 2887,
        2900, 2918, 2920, 2925, 2932, 2933, 2942, 2943, 2945, 2947, 2958, 2959, 2966, 2968, 2971, 2977, 2987, 2988, 2989, 2991,
        2994, 3006, 3018, 3019, 3020, 3021, 3022, 3023, 3024, 3029, 3032, 3036, 3058, 3059, 3067, 3083, 3091, 3221, 3260, 3261,
        3262, 3263, 3264, 3265, 3267, 3275, 3276, 3278, 3280, 3281, 3282, 3302, 3374, 3409, 3460, 3516, 3794, 3795, 3797, 3853,
        3855, 3864, 3872, 3884, 11103, 12840, 16627, 16628, 16629, 16630, 16631, 16632, 16633, 16634, 16635, 16636, 16732, 17968,
        3859, 3858, 3857, 1649, 3851};
    return std::find(std::begin(kIds), std::end(kIds), model) != std::end(kIds);
}

bool GangTagModel(uint16_t model) { return model == 1490 || (model >= 1524 && model <= 1531); }

const char* kEasing[] = {"Linear",     "InQuad",      "OutQuad",     "InOutQuad",  "OutInQuad",    "InElastic",   "OutElastic",
                         "InOutElastic", "OutInElastic", "InBack",    "OutBack",    "InOutBack",    "OutInBack",   "InBounce",
                         "OutBounce",  "InOutBounce", "OutInBounce", "SineCurve",  "CosineCurve"};

int EasingFromName(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < static_cast<int>(sizeof(kEasing) / sizeof(kEasing[0])); ++i) {
        if (std::strcmp(name, kEasing[i]) == 0) return i;
    }
    return -1;
}

enum class Property { All, Mass, TurnMass, Air, Elasticity, Center, Buoyancy };

bool PropertyFromName(const std::string& name, Property& out) {
    if (name == "all") out = Property::All;
    else if (name == "mass") out = Property::Mass;
    else if (name == "turn_mass") out = Property::TurnMass;
    else if (name == "air_resistance") out = Property::Air;
    else if (name == "elasticity") out = Property::Elasticity;
    else if (name == "center_of_mass") out = Property::Center;
    else if (name == "buoyancy") out = Property::Buoyancy;
    else return false;
    return true;
}

void Forget(uint32_t id) {
    for (auto& entry : g_objects) {
        if (entry.second.lowLodId == id) entry.second.lowLodId = 0;
    }
    g_objects.erase(id);
}

Object* Find(uint32_t id) {
    auto found = g_objects.find(id);
    if (found == g_objects.end()) return nullptr;
    if (!KnownClientElement(id)) {
        Forget(id);
        return nullptr;
    }
    return &found->second;
}

Object* Adopt(uint32_t id) {
    Object created;
    created.id = id;
    const int model = ServerWorld::ModelOf(id);
    if (model > 0 && model <= static_cast<int>(kServerModelMax)) created.model = static_cast<uint16_t>(model);
    float x = 0.f, y = 0.f, z = 0.f;
    if (ServerWorld::ElementPose(id, x, y, z)) created.pos = {x, y, z};
    auto [it, inserted] = g_objects.emplace(id, created);
    (void)inserted;
    return &it->second;
}

Object* Live(uint32_t id) {
    if (Object* found = Find(id)) return found;
    if (!KnownClientElement(id)) return nullptr;
    if (std::strcmp(ServerWorld::ElementKindName(id), "object") != 0) return nullptr;
    return Adopt(id);
}

void Sample(Object& object) {
    if (!object.move.active) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - object.move.start);
    const auto ms = elapsed.count() < 0 ? 0 : elapsed.count();
    if (static_cast<uint64_t>(ms) >= object.move.durationMs) {
        object.pos = object.move.targetPos;
        object.rot = object.move.targetRot;
        object.move.active = false;
        return;
    }
    const float t = static_cast<float>(ms) / static_cast<float>(object.move.durationMs);
    auto lerp = [t](float from, float to) { return from + (to - from) * t; };
    object.pos = {lerp(object.move.sourcePos.x, object.move.targetPos.x), lerp(object.move.sourcePos.y, object.move.targetPos.y),
                  lerp(object.move.sourcePos.z, object.move.targetPos.z)};
    object.rot = {lerp(object.move.sourceRot.x, object.move.targetRot.x), lerp(object.move.sourceRot.y, object.move.targetRot.y),
                  lerp(object.move.sourceRot.z, object.move.targetRot.z)};
}

bool SameGangOwner(const Object& object) {
    return object.gangOwned && object.gangNet == ActiveVmOwnerNet() && object.gangGeneration == ActiveVmOwnerGeneration();
}

bool HasResource() {
    if (ActiveVmOwner().empty() || ActiveVmOwnerNet() == 0) return false;
    ResourceLoader::ResourceView caller;
    return ResourceLoader::CopyCallerResource(ActiveVmOwnerNet(), ActiveVmOwnerGeneration(), ActiveVmOwner(), caller) && caller.root != 0;
}

template <typename Fn>
bool ApplyTree(uint32_t id, Fn&& fn, int depth = 0) {
    if (depth > 64) return false;
    if (ClientCallPropagationEnabled(id)) {
        std::vector<uint32_t> kids;
        CopyClientChildren(id, kids);
        for (uint32_t child : kids) ApplyTree(child, fn, depth + 1);
    }
    Object* object = Live(id);
    if (!object) return false;
    return fn(*object);
}

bool ReadObject(lua_State* L, const char* function, Object*& out) {
    if (!IsElementArg(L, 1) || !KnownClientElement(ElementIdOf(L, 1))) {
        LogTypeError(L, function, "object", 1);
        return false;
    }
    out = Live(ElementIdOf(L, 1));
    if (!out) {
        LogTypeError(L, function, "object", 1);
        return false;
    }
    return true;
}

bool RequireObject(lua_State* L, const char* function, Object*& out) {
    if (!IsElementArg(L, 1) || !KnownClientElement(ElementIdOf(L, 1)) || !(out = Live(ElementIdOf(L, 1)))) {
        Raise(L, function, "object", 1);
        return false;
    }
    return true;
}

void PushVec(lua_State* L, const Vec3& value) {
    lua_newtable(L);
    lua_pushnumber(L, value.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, value.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, value.z);
    lua_setfield(L, -2, "z");
}

void NoteMoveGap() {
    if (g_diagMove) return;
    g_diagMove = true;
    LUA_LOGE("DIAG moveObject: onClientObjectMoveStart and onClientObjectMoveStop are not dispatched");
}

void NoteCurveGap() {
    if (g_diagCurve) return;
    g_diagCurve = true;
    LUA_LOGE("DIAG moveObject: non-linear CEasingCurve samples are stored as Linear until the move ends");
}

bool StartMove(Object& object, uint32_t durationMs, const Vec3& target, const Vec3& deltaDeg, bool linear) {
    NoteMoveGap();
    if (!linear) NoteCurveGap();
    Vec3 source = object.rot;
    source.x = WrapOnce(source.x);
    source.y = WrapOnce(source.y);
    source.z = WrapOnce(source.z);
    const Vec3 delta{deltaDeg.x * kPi / 180.f, deltaDeg.y * kPi / 180.f, deltaDeg.z * kPi / 180.f};
    const Vec3 finalRot{source.x + delta.x, source.y + delta.y, source.z + delta.z};
    if (durationMs == 0) {
        object.pos = target;
        object.rot = finalRot;
        object.move.active = false;
        return true;
    }
    object.move.active = true;
    object.move.start = std::chrono::steady_clock::now();
    object.move.durationMs = durationMs;
    object.move.sourcePos = object.pos;
    object.move.sourceRot = source;
    object.move.targetPos = target;
    object.move.targetRot = finalRot;
    object.move.linear = linear;
    return true;
}

int CreateObject(lua_State* L) {
    // object createObject(int model, float x, float y, float z, [float rx, float ry, float rz, bool lowLOD])
    uint32_t model = 0;
    if (!ReadBoundedUint(L, "createObject", 1, model, 0, kServerModelMax)) return Fail(L);
    Vec3 pos{};
    Vec3 rotDeg{};
    if (!ReadFinite(L, "createObject", 2, pos.x) || !ReadFinite(L, "createObject", 3, pos.y) || !ReadFinite(L, "createObject", 4, pos.z))
        return Fail(L);
    // CScriptArgReader::ReadVector3D default is 0,0,0. A missing component stays 0.
    if (!ReadOptionalFinite(L, "createObject", 5, rotDeg.x, 0.f) || !ReadOptionalFinite(L, "createObject", 6, rotDeg.y, 0.f) ||
        !ReadOptionalFinite(L, "createObject", 7, rotDeg.z, 0.f))
        return Fail(L);
    bool lowLod = false;
    if (!ReadBool(L, "createObject", 8, lowLod, true, false)) return Fail(L);
    if (!ObjectModelAllowed(model)) {
        LogFull("createObject", "Bad usage", "Invalid model id");
        return Fail(L);
    }
    const uint32_t id = CreateResourceElement("object");
    if (!id) return Fail(L);
    Object created;
    created.id = id;
    created.model = static_cast<uint16_t>(model);
    created.lowLod = lowLod;
    created.pos = pos;
    created.rot = {rotDeg.x * kPi / 180.f, rotDeg.y * kPi / 180.f, rotDeg.z * kPi / 180.f};
    g_objects[id] = created;
    if (!g_diagSpawn) {
        g_diagSpawn = true;
        LUA_LOGE("DIAG createObject: CDeathmatchObject is stored; CObject is not created");
    }
    PushElement(L, id);
    return 1;
}

int IsObjectStatic(lua_State* L) {
    Object* object = nullptr;
    if (!ReadObject(L, "isObjectStatic", object)) return Fail(L);
    lua_pushboolean(L, object->frozen ? 1 : 0);
    return 1;
}

int SetObjectStatic(lua_State* L) {
    Object* object = nullptr;
    bool frozen = false;
    if (!ReadObject(L, "setObjectStatic", object) || !ReadBool(L, "setObjectStatic", 2, frozen, false, false)) return Fail(L);
    object->frozen = frozen;
    lua_pushboolean(L, 1);
    return 1;
}

int GetObjectScale(lua_State* L) {
    Object* object = nullptr;
    if (!ReadObject(L, "getObjectScale", object)) return Fail(L);
    lua_pushnumber(L, object->scale.x);
    lua_pushnumber(L, object->scale.y);
    lua_pushnumber(L, object->scale.z);
    return 3;
}

int SetObjectScale(lua_State* L) {
    Object* object = nullptr;
    if (!ReadObject(L, "setObjectScale", object)) return Fail(L);
    Vec3 scale{};
    const bool triple = FiniteAt(L, 2, scale.x) && FiniteAt(L, 3, scale.y) && FiniteAt(L, 4, scale.z);
    if (!triple) {
        if (!ReadFinite(L, "setObjectScale", 2, scale.x)) return Fail(L);
        if (!ReadOptionalFinite(L, "setObjectScale", 3, scale.y, scale.x)) return Fail(L);
        if (!ReadOptionalFinite(L, "setObjectScale", 4, scale.z, scale.x)) return Fail(L);
    }
    ApplyTree(object->id, [&](Object& item) {
        item.scale = scale;
        return true;
    });
    lua_pushboolean(L, 1);
    return 1;
}

int IsObjectBreakable(lua_State* L) {
    if (lua_type(L, 1) == LUA_TNUMBER || (lua_type(L, 1) == LUA_TSTRING && lua_isnumber(L, 1))) {
        uint32_t model = 0;
        if (!ReadBoundedUint(L, "isObjectBreakable", 1, model, 0, kServerModelMax)) return Fail(L);
        // CLuaObjectDefs.cpp 285-286. No server model is registered above the stock range.
        // Parent replacement needs CModelInfo, so a stock id is checked as itself.
        if (model >= Models::kStockModelCount) {
            LogFull("isObjectBreakable", "Bad usage", "invalid model ID");
            return Fail(L);
        }
        lua_pushboolean(L, BreakableModel(static_cast<uint16_t>(model)) ? 1 : 0);
        return 1;
    }
    Object* object = nullptr;
    if (!ReadObject(L, "isObjectBreakable", object)) return Fail(L);
    lua_pushboolean(L, BreakableModel(object->model) && !object->breakDisabled ? 1 : 0);
    return 1;
}

int SetObjectBreakable(lua_State* L) {
    Object* object = nullptr;
    bool breakable = false;
    if (!ReadObject(L, "setObjectBreakable", object) || !ReadBool(L, "setObjectBreakable", 2, breakable, false, false)) return Fail(L);
    const bool changed = ApplyTree(object->id, [&](Object& item) {
        if (!BreakableModel(item.model) || item.breakDisabled == !breakable) return false;
        item.breakDisabled = !breakable;
        if (!g_diagBreakable) {
            g_diagBreakable = true;
            LUA_LOGE("DIAG setObjectBreakable: CObjectRespawner::Respawn is not hooked");
        }
        return true;
    });
    lua_pushboolean(L, changed ? 1 : 0);
    return 1;
}

int GetObjectMass(lua_State* L) {
    Object* object = nullptr;
    if (!ReadObject(L, "getObjectMass", object)) return Fail(L);
    lua_pushnumber(L, object->mass);
    return 1;
}

bool SetMassValue(Object& object, float mass) {
    if (mass < 0.f) return false;
    object.mass = mass;
    return true;
}

int SetObjectMass(lua_State* L) {
    Object* object = nullptr;
    float mass = 0.f;
    if (!ReadObject(L, "setObjectMass", object) || !ReadFinite(L, "setObjectMass", 2, mass)) return Fail(L);
    bool ok = false;
    if (mass >= 0.f) ok = ApplyTree(object->id, [&](Object& item) { return SetMassValue(item, mass); });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int GetObjectProperty(lua_State* L) {
    Object* object = nullptr;
    std::string name;
    if (!ReadObject(L, "getObjectProperty", object) || !ReadScriptString(L, "getObjectProperty", 2, name)) return Fail(L);
    Property property{};
    if (!PropertyFromName(name, property)) {
        LogTypeError(L, "getObjectProperty", "object-property", 2);
        return Fail(L);
    }
    switch (property) {
        case Property::All:
            lua_newtable(L);
            lua_pushnumber(L, object->mass);
            lua_setfield(L, -2, "mass");
            lua_pushnumber(L, object->turnMass);
            lua_setfield(L, -2, "turn_mass");
            lua_pushnumber(L, object->air);
            lua_setfield(L, -2, "air_resistance");
            lua_pushnumber(L, object->elasticity);
            lua_setfield(L, -2, "elasticity");
            PushVec(L, object->center);
            lua_setfield(L, -2, "center_of_mass");
            lua_pushnumber(L, object->buoyancy);
            lua_setfield(L, -2, "buoyancy");
            return 1;
        case Property::Mass:
            lua_pushnumber(L, object->mass);
            return 1;
        case Property::TurnMass:
            lua_pushnumber(L, object->turnMass);
            return 1;
        case Property::Air:
            lua_pushnumber(L, object->air);
            return 1;
        case Property::Elasticity:
            lua_pushnumber(L, object->elasticity);
            return 1;
        case Property::Center:
            lua_pushnumber(L, object->center.x);
            lua_pushnumber(L, object->center.y);
            lua_pushnumber(L, object->center.z);
            return 3;
        case Property::Buoyancy:
            lua_pushnumber(L, object->buoyancy);
            return 1;
    }
    return Fail(L);
}

int SetObjectProperty(lua_State* L) {
    Object* object = nullptr;
    std::string name;
    if (!ReadObject(L, "setObjectProperty", object) || !ReadScriptString(L, "setObjectProperty", 2, name)) return Fail(L);
    Property property{};
    if (!PropertyFromName(name, property)) {
        LogTypeError(L, "setObjectProperty", "object-property", 2);
        return Fail(L);
    }
    if (property == Property::All) {
        LogTypeError(L, "setObjectProperty", "object-property", 2);
        return Fail(L);
    }
    bool ok = false;
    if (property == Property::Center) {
        Vec3 center{};
        if (!ReadFinite(L, "setObjectProperty", 3, center.x) || !ReadFinite(L, "setObjectProperty", 4, center.y) ||
            !ReadFinite(L, "setObjectProperty", 5, center.z))
            return Fail(L);
        ok = ApplyTree(object->id, [&](Object& item) {
            item.center = center;
            return true;
        });
    } else {
        float value = 0.f;
        if (!ReadFinite(L, "setObjectProperty", 3, value)) return Fail(L);
        ok = ApplyTree(object->id, [&](Object& item) {
            switch (property) {
                case Property::Mass: return SetMassValue(item, value);
                case Property::TurnMass:
                    if (value < 0.f) return false;
                    item.turnMass = value;
                    return true;
                case Property::Air:
                    item.air = value;
                    return true;
                case Property::Elasticity:
                    item.elasticity = value;
                    return true;
                case Property::Buoyancy:
                    item.buoyancy = value;
                    return true;
                default: return false;
            }
        });
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int IsObjectMoving(lua_State* L) {
    if (!IsElementArg(L, 1) || !KnownClientElement(ElementIdOf(L, 1))) return Raise(L, "isObjectMoving", "element", 1);
    Object* object = Live(ElementIdOf(L, 1));
    if (!object) return Fail(L);
    Sample(*object);
    lua_pushboolean(L, object->move.active ? 1 : 0);
    return 1;
}

int IsObjectRespawnable(lua_State* L) {
    if (!IsElementArg(L, 1) || !KnownClientElement(ElementIdOf(L, 1))) return Raise(L, "isObjectRespawnable", "element", 1);
    Object* object = Live(ElementIdOf(L, 1));
    if (!object) return Fail(L);
    lua_pushboolean(L, object->respawn ? 1 : 0);
    return 1;
}

int MoveObject(lua_State* L) {
    Object* object = nullptr;
    int time = 0;
    Vec3 target{};
    Vec3 delta{};
    if (!ReadObject(L, "moveObject", object) || !ReadInt(L, "moveObject", 2, time) || !ReadFinite(L, "moveObject", 3, target.x) ||
        !ReadFinite(L, "moveObject", 4, target.y) || !ReadFinite(L, "moveObject", 5, target.z))
        return Fail(L);
    if (!ReadOptionalFinite(L, "moveObject", 6, delta.x, 0.f) || !ReadOptionalFinite(L, "moveObject", 7, delta.y, 0.f) ||
        !ReadOptionalFinite(L, "moveObject", 8, delta.z, 0.f))
        return Fail(L);
    int easing = 0;
    if (!lua_isnoneornil(L, 9)) {
        if (lua_type(L, 9) != LUA_TSTRING || (easing = EasingFromName(lua_tostring(L, 9))) < 0) {
            LogTypeError(L, "moveObject", "easing-type", 9);
            return Fail(L);
        }
    }
    float period = 0.f, amplitude = 0.f, overshoot = 0.f;
    if (!ReadOptionalFinite(L, "moveObject", 10, period, 0.3f) || !ReadOptionalFinite(L, "moveObject", 11, amplitude, 1.f) ||
        !ReadOptionalFinite(L, "moveObject", 12, overshoot, 1.70158f))
        return Fail(L);
    (void)period;
    (void)amplitude;
    (void)overshoot;
    const uint32_t duration = static_cast<uint32_t>(time);
    const bool linear = easing == 0;
    ApplyTree(object->id, [&](Object& item) { return StartMove(item, duration, target, delta, linear); });
    lua_pushboolean(L, 1);
    return 1;
}

int StopObject(lua_State* L) {
    Object* object = nullptr;
    if (!ReadObject(L, "stopObject", object)) return Fail(L);
    NoteMoveGap();
    ApplyTree(object->id, [](Object& item) {
        Sample(item);
        item.move.active = false;
        return true;
    });
    lua_pushboolean(L, 1);
    return 1;
}

int BreakObject(lua_State* L) {
    Object* object = nullptr;
    if (!ReadObject(L, "breakObject", object)) return Fail(L);
    if (!g_diagBreak) {
        g_diagBreak = true;
        LUA_LOGE("DIAG breakObject: CObject::Break is not hooked");
    }
    ApplyTree(object->id, [](Object&) { return false; });
    return Fail(L);
}

int RespawnObject(lua_State* L) {
    Object* object = nullptr;
    if (!ReadObject(L, "respawnObject", object)) return Fail(L);
    if (!g_diagRespawn) {
        g_diagRespawn = true;
        LUA_LOGE("DIAG respawnObject: CObjectRespawner is not hooked");
    }
    const bool ok = ApplyTree(object->id, [](Object& item) {
        if (item.respawning) return false;
        item.respawning = true;
        return true;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int ToggleObjectRespawn(lua_State* L) {
    Object* object = nullptr;
    bool enabled = false;
    if (!ReadObject(L, "toggleObjectRespawn", object) || !ReadBool(L, "toggleObjectRespawn", 2, enabled, false, false)) return Fail(L);
    ApplyTree(object->id, [&](Object& item) {
        item.respawn = enabled;
        return true;
    });
    lua_pushboolean(L, 1);
    return 1;
}

int SetObjectMaterial(lua_State* L) {
    Object* object = nullptr;
    uint32_t slot = 0;
    int source = 0;
    std::string txd;
    std::string texture;
    if (!ReadObject(L, "setObjectMaterial", object) || !ReadBoundedUint(L, "setObjectMaterial", 2, slot, 0, 15) ||
        !ReadInt(L, "setObjectMaterial", 3, source) || !ReadScriptString(L, "setObjectMaterial", 4, txd) ||
        !ReadScriptString(L, "setObjectMaterial", 5, texture))
        return Fail(L);
    if (source > 0 && (static_cast<uint32_t>(source) > kServerModelMax || source >= static_cast<int>(Models::kStockModelCount))) {
        LogFull("setObjectMaterial", "Bad usage", "Invalid source model id");
        return Fail(L);
    }
    if (source != -1 && source != 0 && source <= 0) {
        LogFull("setObjectMaterial", "Bad usage", "Source model id must be -1, 0, or a valid model id");
        return Fail(L);
    }
    float colorNumber = 0.f;
    if (!ReadOptionalFinite(L, "setObjectMaterial", 6, colorNumber, 0.f)) return Fail(L);
    const uint32_t color = static_cast<uint32_t>(static_cast<int64_t>(colorNumber));
    const bool replace = source > 0 && !texture.empty() && !EqualsIgnoreCase(texture, "none");
    if (replace) {
        if (!g_diagTexture) {
            g_diagTexture = true;
            LUA_LOGE("DIAG setObjectMaterial: CRenderWare::AcquireModelTexture is not hooked");
        }
        LUA_LOGI("WARNING: Unable to resolve texture '%s' from source model %d", texture.c_str(), source);
        return Fail(L);
    }
    Material& material = object->materials[slot];
    material.enabled = true;
    material.source = source;
    material.txd = txd;
    material.texture = texture;
    material.color = color;
    lua_pushboolean(L, 1);
    return 1;
}

int RemoveObjectMaterial(lua_State* L) {
    Object* object = nullptr;
    uint32_t slot = 0;
    if (!ReadObject(L, "removeObjectMaterial", object) || !ReadBoundedUint(L, "removeObjectMaterial", 2, slot, 0, 15)) return Fail(L);
    object->materials[slot] = {};
    lua_pushboolean(L, 1);
    return 1;
}

int SetObjectGangTagAlpha(lua_State* L) {
    Object* object = nullptr;
    if (!RequireObject(L, "setObjectGangTagAlpha", object)) return 0;
    const int type = lua_type(L, 2);
    if (type == LUA_TBOOLEAN) {
        // Parsed, then CObject is required before the bool has an effect.
    } else if (type == LUA_TNUMBER || type == LUA_TSTRING) {
        float number = 0.f;
        if (!ReadFinite(L, "setObjectGangTagAlpha", 2, number)) return Fail(L);
        if (number < 0.f) {
            LogFull("setObjectGangTagAlpha", "Bad argument", "Expected positive value, got negative");
            return Fail(L);
        }
    } else {
        return Raise(L, "setObjectGangTagAlpha", "number", 2);
    }
    if (!g_diagAlpha) {
        g_diagAlpha = true;
        LUA_LOGE("DIAG setObjectGangTagAlpha: CObject is not created");
    }
    (void)object;
    return Fail(L);
}

int AcquireObjectGangTag(lua_State* L) {
    Object* object = nullptr;
    if (!RequireObject(L, "acquireObjectGangTag", object)) return 0;
    unsigned progress = 0;
    if (!lua_isnoneornil(L, 2)) {
        uint32_t value = 0;
        if (!ReadBoundedUint(L, "acquireObjectGangTag", 2, value, 0, 100000)) return Fail(L);
        if (value > 255) return Fail(L);
        progress = value;
    }
    bool spray = true;
    if (!lua_isnoneornil(L, 3)) {
        if (lua_type(L, 3) != LUA_TBOOLEAN) return Raise(L, "acquireObjectGangTag", "boolean", 3);
        spray = lua_toboolean(L, 3) != 0;
    }
    if (!HasResource()) return Fail(L);
    if (object->gangOwned && !SameGangOwner(*object)) return Fail(L);
    if (!GangTagModel(object->model)) return Fail(L);
    object->gangOwned = true;
    object->gangNet = ActiveVmOwnerNet();
    object->gangGeneration = ActiveVmOwnerGeneration();
    object->gangProgress = static_cast<uint8_t>(progress);
    object->gangSpray = spray;
    lua_pushboolean(L, 1);
    return 1;
}

int SetObjectGangTagProgress(lua_State* L) {
    Object* object = nullptr;
    if (!RequireObject(L, "setObjectGangTagProgress", object)) return 0;
    uint32_t progress = 0;
    if (!ReadBoundedUint(L, "setObjectGangTagProgress", 2, progress, 0, 100000)) return Fail(L);
    if (!HasResource() || !SameGangOwner(*object) || progress > 255) return Fail(L);
    object->gangProgress = static_cast<uint8_t>(progress);
    lua_pushboolean(L, 1);
    return 1;
}

int GetObjectGangTagProgress(lua_State* L) {
    Object* object = nullptr;
    if (!RequireObject(L, "getObjectGangTagProgress", object)) return 0;
    if (!object->gangOwned) return Fail(L);
    lua_pushinteger(L, object->gangProgress);
    return 1;
}

int ReleaseObjectGangTag(lua_State* L) {
    Object* object = nullptr;
    if (!RequireObject(L, "releaseObjectGangTag", object)) return 0;
    if (!HasResource() || !SameGangOwner(*object)) return Fail(L);
    object->gangOwned = false;
    object->gangNet = 0;
    object->gangGeneration = 0;
    object->gangProgress = 0;
    object->gangSpray = false;
    lua_pushboolean(L, 1);
    return 1;
}

int GetElementPositionObject(lua_State* L) {
    float x = 0.f, y = 0.f, z = 0.f;
    if (CopyObjectPose(ElementIdOf(L, 1), x, y, z)) {
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        lua_pushnumber(L, z);
        return 3;
    }
    if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
    const int count = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, count, LUA_MULTRET);
    return lua_gettop(L);
}

int SetElementPositionObject(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    Object* object = Find(id);
    if (!object) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    Vec3 pos{};
    if (!ReadFinite(L, "setElementPosition", 2, pos.x) || !ReadFinite(L, "setElementPosition", 3, pos.y) ||
        !ReadFinite(L, "setElementPosition", 4, pos.z))
        return Fail(L);
    ApplyTree(id, [&](Object& item) {
        Sample(item);
        if (!item.move.active) item.pos = pos;
        return true;
    });
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    g_objects.clear();
    const struct {
        const char* name;
        lua_CFunction fn;
    } binds[] = {
        {"createObject", CreateObject},
        {"isObjectStatic", IsObjectStatic},
        {"setObjectStatic", SetObjectStatic},
        {"getObjectScale", GetObjectScale},
        {"setObjectScale", SetObjectScale},
        {"isObjectBreakable", IsObjectBreakable},
        {"setObjectBreakable", SetObjectBreakable},
        {"getObjectMass", GetObjectMass},
        {"setObjectMass", SetObjectMass},
        {"getObjectProperty", GetObjectProperty},
        {"setObjectProperty", SetObjectProperty},
        {"isObjectMoving", IsObjectMoving},
        {"isObjectRespawnable", IsObjectRespawnable},
        {"moveObject", MoveObject},
        {"stopObject", StopObject},
        {"breakObject", BreakObject},
        {"respawnObject", RespawnObject},
        {"toggleObjectRespawn", ToggleObjectRespawn},
        {"setObjectMaterial", SetObjectMaterial},
        {"removeObjectMaterial", RemoveObjectMaterial},
        {"setObjectGangTagAlpha", SetObjectGangTagAlpha},
        {"acquireObjectGangTag", AcquireObjectGangTag},
        {"setObjectGangTagProgress", SetObjectGangTagProgress},
        {"getObjectGangTagProgress", GetObjectGangTagProgress},
        {"releaseObjectGangTag", ReleaseObjectGangTag},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_getglobal(L, "getElementPosition");
    lua_pushcclosure(L, GetElementPositionObject, 1);
    lua_setglobal(L, "getElementPosition");
    lua_getglobal(L, "setElementPosition");
    lua_pushcclosure(L, SetElementPositionObject, 1);
    lua_setglobal(L, "setElementPosition");
}

bool CopyPoseImpl(uint32_t id, float& x, float& y, float& z) {
    Object* object = Find(id);
    if (!object) return false;
    Sample(*object);
    x = object->pos.x;
    y = object->pos.y;
    z = object->pos.z;
    return true;
}

}  // namespace

bool CopyObjectPose(uint32_t id, float& x, float& y, float& z) { return CopyPoseImpl(id, x, y, z); }

bool CopyObjectLod(uint32_t id, bool& createdLow, uint32_t& linkedLow) {
    Object* object = Find(id);
    if (!object) return false;
    createdLow = object->lowLod;
    linkedLow = object->lowLodId;
    if (linkedLow && !Find(linkedLow)) {
        object = Find(id);
        if (!object) return false;
        object->lowLodId = 0;
        linkedLow = 0;
    }
    return true;
}

bool SetObjectLod(uint32_t id, uint32_t lowId, bool clear) {
    // CClientObject::SetLowLodObject (CClientObject.cpp 532-566).
    Object* self = Find(id);
    if (!self || self->lowLod) return false;
    if (clear || lowId == 0) {
        if (!self->lowLodId) return false;
        self->lowLodId = 0;
        return true;
    }
    Object* low = Find(lowId);
    if (!low || !low->lowLod) return false;
    self = Find(id);
    if (!self) return false;
    self->lowLodId = lowId;
    return true;
}

void InstallWorldObjectLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
