// Element fields, world settings, and the getters that match them.
// Desktop: CLuaElementDefs.cpp Get/Set interior 850, 2069; dimension 1012, 2121;
//   alpha 1311, 2369; model 1402; rotation 497; frozen 1562; health 2394.
//   CStaticFunctionDefinitions.cpp interior 542 and 1298, dimension 1319,
//   alpha 692 and 1446, model 758, frozen 928, health 1487.
//   CLuaWorldDefs.cpp weather 1193, gravity 1238 and 1645, game speed 1251 and 1668,
//   minute duration 1264 and 1691, far clip 2333-2395, fog 2523-2556.
//   CStaticFunctionDefinitions.cpp 6800-6839 and 7113-7146.
// Engine writes that are not hooked stay as DIAG. The value is still stored.

#include "LuaFieldApi.h"
#include "LuaElementTree.h"
#include "LuaSpatialApi.h"
#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldRPG::Script {
namespace {

uint64_t AreaGeneration(uint32_t id) {
    const auto generation=ServerWorld::ElementGeneration(id);
    return !generation&&Game::ScriptPedAlive(id)?id:generation;
}
struct Fields {
    uint64_t areaGeneration=0;
    bool alphaSet = false;
    unsigned char alpha = 255;
    bool interiorSet = false;
    unsigned char interior = 0;
    bool dimensionSet = false;
    unsigned short dimension = 0;
    bool frozenSet = false;
    bool frozen = false;
    bool healthSet = false;
    float health = 100.f;
    bool collisionsSet = false;
    bool collisions = true;
    bool doubleSided = false;
    bool streamable = false;
    bool lightingSet = false;
    float lighting = 0.f;
};

std::unordered_map<uint32_t, Fields> g_fields;
float g_gravity = 0.008f;
float g_gameSpeed = 1.f;
unsigned g_minuteMs = 1000;
bool g_farSet = false;
float g_far = 800.f;
bool g_fogSet = false;
float g_fog = 150.f;
int g_weather = 0;
bool g_weatherSet = false;
bool g_diagAlpha = false;
bool g_diagInterior = false;
bool g_diagDimension = false;
bool g_diagHealth = false;
bool g_diagGravity = false;
bool g_diagSpeed = false;
bool g_diagMinute = false;
bool g_diagFar = false;
bool g_diagFog = false;
bool g_diagOrder = false;
bool g_diagCollision = false;
bool g_nearSet = false;
float g_near = 0.3f;
bool g_clouds = true;
bool g_birds = true;
float g_rain = 0.f;
int g_blur = 36;
int g_moon = 3;
float g_sunSize = 1.f;
bool g_sunColorSet = false;
int g_sunR = 0, g_sunG = 0, g_sunB = 0, g_sunA = 255;
bool g_skySet = false;
int g_sky[8] = {0, 0, 0, 0, 0, 0, 0, 0};
bool g_windSet = false;
float g_windX = 0.f, g_windY = 0.f, g_windZ = 0.f;
float g_wave = 0.f;
int g_fps = 0;
bool g_diagWorld = false;
float g_jetpack = 100.f;
float g_aircraft = 800.f;
bool g_occlusions = true;
bool g_interiorSounds = true;
bool g_furniture[5] = {true, true, true, true, true};
int g_grain = 0;
float g_grainMul = 1.f;
int g_water[4] = {0, 0, 0, 200};
bool g_waterSet = false;
float g_pedsLod = 500.f;
float g_vehiclesLod = 500.f;
float g_trainsLod = 500.f;
int g_cursorAlpha = 255;
struct WorldProp { const char* name; bool enabled; };
WorldProp g_worldProps[] = {
    {"hovercars", false}, {"aircars", false}, {"extrabunny", false}, {"extrajump", false}, {"randomfoliage", true},
    {"snipermoon", false}, {"extraairresistance", true}, {"underworldwarp", true}, {"vehiclesunglare", false},
    {"coronaztest", true}, {"watercreatures", true}, {"burnflippedcars", true}, {"fireballdestruct", true},
    {"roadsignstext", true}, {"extendedwatercannons", true}, {"tunnelweatherblend", true}, {"ignorefirestate", false},
    {"flyingcomponents", true}, {"vehicleburnexplosions", true},     {"vehicle_engine_autostart", true},
};
bool g_timeFrozen = false;
float g_aircraftVelocity = 200.f;
int g_traffic = 0;
bool g_trafficLocked = false;
int g_corona = 0;
bool g_dynamicShadows = true;
bool g_volumetric = false;
bool g_waterLast = false;
bool g_targetMarker = true;
bool g_blendSet = false;
int g_weatherBlend = 0;
int g_heat[8] = {0, 0, 12, 18, 75, 80, 80, 85};
bool g_heatInside = false;
int g_filter[8] = {};
bool g_garage[50] = {};
struct PropValue { bool color = false; float v[3] = {}; };
std::unordered_map<std::string, PropValue> g_props;

void Diag(bool& once, const char* message) {
    if (once) return;
    once = true;
    LUA_LOGE("%s", message);
}

bool Number(lua_State* L, int index, float& out) {
    if (!lua_isnumber(L, index)) return false;
    out = static_cast<float>(lua_tonumber(L, index));
    return std::isfinite(out);
}

enum class Kind { None, Player, Ped, Vehicle, Marker };

Kind KindOf(uint32_t id) {
    if (id == 0) return Kind::None;
    int model = 0;
    float heading = 0.f;
    bool frozen = false;
    if (Game::CopyScriptPedFacts(id, model, heading, frozen)) return Kind::Ped;
    if (id == ServerWorld::LocalPlayer()) return Kind::Player;
    ServerWorld::ActorSpawn visual;
    if (!ServerWorld::CopyVisual(id, visual)) return Kind::None;
    if (visual.kind == 1) return Kind::Player;
    if (visual.kind == 2) return Kind::Vehicle;
    if (visual.kind == 14) return Kind::Ped;
    if (visual.kind == 4) return Kind::Marker;
    return Kind::None;
}

bool BodyKind(Kind kind) { return kind == Kind::Player || kind == Kind::Ped || kind == Kind::Vehicle; }

float Wrap360(float degrees) {
    degrees = std::fmod(degrees, 360.f);
    if (degrees < 0.f) degrees += 360.f;
    return degrees;
}

// Vehicle roll and pitch are degrees on the spawn record (ServerWorld.h ActorSpawn).
void VehicleTilt(uint32_t id, float& rx, float& ry) {
    rx = 0.f;
    ry = 0.f;
    ServerWorld::ActorSpawn visual;
    if (ServerWorld::CopyVisual(id, visual) && visual.kind == 2) {
        rx = visual.rotX;
        ry = visual.rotY;
    }
}

bool HeadingOf(uint32_t id, float& degrees) {
    int model = 0;
    float radians = 0.f;
    bool frozen = false;
    if (Game::CopyScriptPedFacts(id, model, radians, frozen)) {
        degrees = Wrap360(radians * (180.f / 3.14159265f));
        return true;
    }
    if (id == ServerWorld::LocalPlayer()) {
        float x = 0, y = 0, z = 0;
        Game::CopyPose(x, y, z, radians);
        degrees = Wrap360(radians * (180.f / 3.14159265f));
        return true;
    }
    ServerWorld::ActorSpawn visual;
    if (!ServerWorld::CopyVisual(id, visual)) return false;
    if (visual.kind != 1 && visual.kind != 2 && visual.kind != 14) return false;
    degrees = Wrap360(visual.heading * (180.f / 3.14159265f));
    return true;
}

int l_getElementModel(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const Kind kind = KindOf(id);
    if (!BodyKind(kind) && kind != Kind::Marker) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int model = 0;
    float heading = 0.f;
    bool frozen = false;
    if (Game::CopyScriptPedFacts(id, model, heading, frozen)) {
        lua_pushinteger(L, model);
        return 1;
    }
    const int stored = ServerWorld::ModelOf(id);
    lua_pushinteger(L, stored >= 0 ? stored : 0);
    return 1;
}

int l_getElementRotation(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (lua_type(L, 2) == LUA_TSTRING && std::strcmp(lua_tostring(L, 2), "default") != 0)
        Diag(g_diagOrder, "DIAG getElementRotation: non-default Euler order is not converted");
    else if (!lua_isnoneornil(L, 2) && lua_type(L, 2) != LUA_TSTRING) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Game::LocalCarState car;
    if (Game::CopyLocalCar(car) && car.vehicleId == id) {
        lua_pushnumber(L, car.rotX);
        lua_pushnumber(L, car.rotY);
        lua_pushnumber(L, car.rotZ);
        return 3;
    }
    float z = 0.f;
    if (!HeadingOf(id, z)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float rx = 0.f, ry = 0.f;
    VehicleTilt(id, rx, ry);
    lua_pushnumber(L, rx);
    lua_pushnumber(L, ry);
    lua_pushnumber(L, z);
    return 3;
}

int l_getElementMatrix(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    float stored[4][3];
    if (CopyScriptMatrix(id, stored)) {
        // Same table shape as the heading path below. Desktop writes 1 in the
        // first three rows' W when the legacy flag is left at its default
        // (CLuaElementDefs.cpp 360-374). This getter already writes 0 there.
        const float rows[4][4] = {
            {stored[0][0], stored[0][1], stored[0][2], 0.f},
            {stored[1][0], stored[1][1], stored[1][2], 0.f},
            {stored[2][0], stored[2][1], stored[2][2], 0.f},
            {stored[3][0], stored[3][1], stored[3][2], 1.f},
        };
        lua_newtable(L);
        for (int row = 0; row < 4; ++row) {
            lua_pushinteger(L, row + 1);
            lua_newtable(L);
            for (int col = 0; col < 4; ++col) {
                lua_pushinteger(L, col + 1);
                lua_pushnumber(L, rows[row][col]);
                lua_settable(L, -3);
            }
            lua_settable(L, -3);
        }
        return 1;
    }
    float degrees = 0.f;
    if (!HeadingOf(id, degrees)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float x = 0.f, y = 0.f, z = 0.f;
    float poseHeading = 0.f;
    if (id == ServerWorld::LocalPlayer()) Game::CopyPose(x, y, z, poseHeading);
    else ServerWorld::ElementPose(id, x, y, z);
    (void)poseHeading;
    float rx = 0.f, ry = 0.f;
    VehicleTilt(id, rx, ry);
    // CMatrix::SetRotation (Shared/sdk/CMatrix.h 253-278). Angles are radians, and X/Y are negated.
    const float toRad = 3.14159265f / 180.f;
    const float fCosX = std::cos(-rx * toRad);
    const float fCosY = std::cos(-ry * toRad);
    const float fCosZ = std::cos(degrees * toRad);
    const float fSinX = std::sin(-rx * toRad);
    const float fSinY = std::sin(-ry * toRad);
    const float fSinZ = std::sin(degrees * toRad);
    const float rows[4][4] = {
        {fCosY * fCosZ, fCosY * fSinZ, fSinY, 0.f},
        {fSinX * fSinY * fCosZ - fCosX * fSinZ, fSinX * fSinY * fSinZ + fCosX * fCosZ, -fSinX * fCosY, 0.f},
        {-(fCosX * fSinY * fCosZ + fSinX * fSinZ), fCosZ * fSinX - fCosX * fSinY * fSinZ, fCosX * fCosY, 0.f},
        {x, y, z, 1.f},
    };
    lua_newtable(L);
    for (int row = 0; row < 4; ++row) {
        lua_pushinteger(L, row + 1);
        lua_newtable(L);
        for (int col = 0; col < 4; ++col) {
            lua_pushinteger(L, col + 1);
            lua_pushnumber(L, rows[row][col]);
            lua_settable(L, -3);
        }
        lua_settable(L, -3);
    }
    return 1;
}

int l_getElementAlpha(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const Kind kind = KindOf(id);
    if (!BodyKind(kind) && kind != Kind::Marker) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto found = g_fields.find(id);
    lua_pushinteger(L, found != g_fields.end() && found->second.alphaSet ? found->second.alpha : 255);
    return 1;
}

int l_setElementAlpha(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    float value = 0.f;
    const Kind kind = KindOf(id);
    if ((!BodyKind(kind) && kind != Kind::Marker) || !Number(L, 2, value) || value < 0.f || value > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_fields[id].alphaSet = true;
    g_fields[id].alpha = static_cast<unsigned char>(value);
    Diag(g_diagAlpha, "DIAG setElementAlpha: CEntity::SetAlpha is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getElementInterior(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (id == 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Spatial::Element shape;
    if(CopySpatialShape(id,shape)) { lua_pushinteger(L,shape.interior); return 1; }
    const auto found = g_fields.find(id);
    if (found != g_fields.end() && found->second.areaGeneration==AreaGeneration(id) && found->second.interiorSet) {
        lua_pushinteger(L, found->second.interior);
        return 1;
    }
    ServerWorld::ActorSpawn visual;
    if (ServerWorld::CopyVisual(id, visual) || id == ServerWorld::LocalPlayer() || KindOf(id) != Kind::None) {
        lua_pushinteger(L, ServerWorld::InteriorOf(id));
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_setElementInterior(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    float value = 0.f;
    if (id == 0 || !Number(L, 2, value) || value < 0.f || value > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if(SpatialSetArea(id,static_cast<unsigned>(value),false)) { lua_pushboolean(L,1); return 1; }
    if(ServerWorld::ElementGeneration(id)) {
        ServerWorld::NoteElementInterior(id,static_cast<uint8_t>(value));
        g_fields[id].interiorSet=false;
        Diag(g_diagInterior,"DIAG setElementInterior: client area updated; native CWorld::SetCurrentArea remains unported");
        lua_pushboolean(L,1); return 1;
    }
    auto& area=g_fields[id];
    const auto generation=AreaGeneration(id);
    if(area.areaGeneration!=generation) { area.interiorSet=false; area.dimensionSet=false; area.areaGeneration=generation; }
    g_fields[id].interiorSet = true;
    g_fields[id].interior = static_cast<unsigned char>(value);
    Diag(g_diagInterior, "DIAG setElementInterior: CWorld::SetCurrentArea is not hooked; the area is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getElementDimension(lua_State* L) {
    const uint32_t id=ElementIdOf(L,1);
    if(!id) { lua_pushboolean(L,0); return 1; }
    Spatial::Element shape;
    if(CopySpatialShape(id,shape)) { lua_pushinteger(L,shape.dimension); return 1; }
    uint8_t interior=0; uint16_t dimension=0;
    ServerWorld::ActorSpawn visual;
    if(ServerWorld::CopyVisual(id,visual)||id==ServerWorld::LocalPlayer()) dimension=ServerWorld::DimensionOf(id);
    ApplySpatialArea(id,AreaGeneration(id),interior,dimension);
    lua_pushinteger(L,dimension); return 1;
}

int l_setElementDimension(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    float value = 0.f;
    if (id == 0 || !Number(L, 2, value)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // CLuaElementDefs::SetElementDimension: -1 is objects only. Range is 0..65535.
    if (value == -1.f || value < 0.f || value > 65535.f) {
        LUA_LOGE("setElementDimension: invalid dimension range");
        lua_pushboolean(L, 0);
        return 1;
    }
    if(SpatialSetArea(id,static_cast<unsigned>(value),true)) { lua_pushboolean(L,1); return 1; }
    if(ServerWorld::ElementGeneration(id)) {
        ServerWorld::NoteElementDimension(id,static_cast<uint16_t>(value));
        g_fields[id].dimensionSet=false;
        Diag(g_diagDimension,"DIAG setElementDimension: client dimension updated; native vehicle streaming by dimension remains unported");
        lua_pushboolean(L,1); return 1;
    }
    auto& area=g_fields[id];
    const auto generation=AreaGeneration(id);
    if(area.areaGeneration!=generation) { area.interiorSet=false; area.dimensionSet=false; area.areaGeneration=generation; }
    g_fields[id].dimensionSet = true;
    g_fields[id].dimension = static_cast<unsigned short>(value);
    Diag(g_diagDimension, "DIAG setElementDimension: dimension is stored; streamer distance is not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

int l_isElementFrozen(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const Kind kind = KindOf(id);
    if (!BodyKind(kind)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto found = g_fields.find(id);
    if (found != g_fields.end() && found->second.frozenSet) {
        lua_pushboolean(L, found->second.frozen ? 1 : 0);
        return 1;
    }
    int model = 0;
    float heading = 0.f;
    bool frozen = false;
    if (Game::CopyScriptPedFacts(id, model, heading, frozen)) {
        lua_pushboolean(L, frozen ? 1 : 0);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_setElementHealth(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    float health = 0.f;
    const Kind kind = KindOf(id);
    if (!BodyKind(kind) || !Number(L, 2, health)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (kind == Kind::Player || kind == Kind::Ped) {
        if (health < 0.f) health = 0.f;
        if (health > 200.f) health = 200.f;
    }
    g_fields[id].healthSet = true;
    g_fields[id].health = health;
    if (id == ServerWorld::LocalPlayer()) Game::NoteLocalHealth(health);
    else Diag(g_diagHealth, "DIAG setElementHealth: CPed::SetHealth is applied for the local player only");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getElementHealth(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const Kind kind = KindOf(id);
    if (!BodyKind(kind)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto found = g_fields.find(id);
    if (found != g_fields.end() && found->second.healthSet) {
        lua_pushnumber(L, found->second.health);
        return 1;
    }
    Game::LocalCarState car;
    if (kind == Kind::Vehicle && Game::CopyLocalCar(car) && car.vehicleId == id) {
        lua_pushnumber(L, car.health);
        return 1;
    }
    if (id == ServerWorld::LocalPlayer()) {
        float x = 0, y = 0, z = 0, heading = 0, vx = 0, vy = 0, vz = 0, health = 100.f;
        Game::CopyBody(x, y, z, heading, vx, vy, vz, health);
        lua_pushnumber(L, health);
        return 1;
    }
    ServerWorld::ActorSpawn visual;
    if (ServerWorld::CopyVisual(id, visual) && visual.health >= 0.f) {
        lua_pushnumber(L, visual.health);
        return 1;
    }
    lua_pushnumber(L, 100);
    return 1;
}

int l_getWeather(lua_State* L) {
    int weather = 0;
    if (!Game::CopyScriptWeather(weather)) weather = g_weatherSet ? g_weather : 0;
    lua_pushnumber(L, weather);
    lua_pushnil(L);
    if (g_blendSet) {
        lua_pop(L, 1);
        lua_pushinteger(L, g_weatherBlend);
    }
    return 2;
}

int l_setWeather(lua_State* L) {
    float weather = 0.f;
    if (!Number(L, 1, weather) || weather < 0.f || weather > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_weather = static_cast<int>(weather);
    g_weatherSet = true;
    g_blendSet = false;
    Game::NoteScriptWeather(g_weather);
    lua_pushboolean(L, 1);
    return 1;
}

int l_getGravity(lua_State* L) {
    lua_pushnumber(L, g_gravity);
    return 1;
}

int l_setGravity(lua_State* L) {
    float gravity = 0.f;
    if (!Number(L, 1, gravity) || gravity < -1.f || gravity > 1.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_gravity = gravity;
    Diag(g_diagGravity, "DIAG setGravity: CGame::SetGravity is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getGameSpeed(lua_State* L) {
    lua_pushnumber(L, g_gameSpeed);
    return 1;
}

int l_setGameSpeed(lua_State* L) {
    float speed = 0.f;
    if (!Number(L, 1, speed) || speed < 0.f || speed > 10.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_gameSpeed = speed;
    Diag(g_diagSpeed, "DIAG setGameSpeed: CGame::SetGameSpeed is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getMinuteDuration(lua_State* L) {
    lua_pushinteger(L, g_minuteMs);
    return 1;
}

int l_setMinuteDuration(lua_State* L) {
    float ms = 0.f;
    if (!Number(L, 1, ms) || ms <= 0.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_minuteMs = static_cast<unsigned>(ms);
    Diag(g_diagMinute, "DIAG setMinuteDuration: CGame::SetMinuteDuration is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getFarClipDistance(lua_State* L) {
    lua_pushnumber(L, g_far);
    return 1;
}

int l_setFarClipDistance(lua_State* L) {
    float distance = 0.f;
    if (!Number(L, 1, distance)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_far = distance;
    g_farSet = true;
    Diag(g_diagFar, "DIAG setFarClipDistance: CMultiplayer::SetFarClipDistance is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_resetFarClipDistance(lua_State* L) {
    (void)L;
    g_far = 800.f;
    g_farSet = false;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getFogDistance(lua_State* L) {
    lua_pushnumber(L, g_fog);
    return 1;
}

int l_setFogDistance(lua_State* L) {
    float distance = 0.f;
    if (!Number(L, 1, distance)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_fog = distance;
    g_fogSet = true;
    Diag(g_diagFog, "DIAG setFogDistance: CMultiplayer::SetFogDistance is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_resetFogDistance(lua_State* L) {
    (void)L;
    g_fog = 150.f;
    g_fogSet = false;
    lua_pushboolean(L, 1);
    return 1;
}

int l_setElementCollisionsEnabled(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (id == 0 || lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_fields[id].collisionsSet = true;
    g_fields[id].collisions = lua_toboolean(L, 2) != 0;
    Diag(g_diagCollision, "DIAG setElementCollisionsEnabled: CPhysical::SetUsesCollision is not hooked; the flag is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getElementCollisionsEnabled(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (id == 0 || KindOf(id) == Kind::None) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto found = g_fields.find(id);
    lua_pushboolean(L, found != g_fields.end() && found->second.collisionsSet ? found->second.collisions : 1);
    return 1;
}

bool WorldFlag(lua_State* L, bool& slot) {
    if (lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return false;
    }
    slot = lua_toboolean(L, 1) != 0;
    Diag(g_diagWorld, "DIAG world visual: the CGame write is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return true;
}

int l_getNearClipDistance(lua_State* L) {
    if (!g_nearSet) return 0;
    lua_pushnumber(L, g_near);
    return 1;
}

int l_setNearClipDistance(lua_State* L) {
    float distance = 0.f;
    if (!Number(L, 1, distance) || distance <= 0.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_near = distance;
    g_nearSet = true;
    Diag(g_diagWorld, "DIAG setNearClipDistance: CMultiplayer::SetNearClipDistance is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_resetNearClipDistance(lua_State* L) {
    (void)L;
    g_near = 0.3f;
    g_nearSet = false;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getCloudsEnabled(lua_State* L) { lua_pushboolean(L, g_clouds); return 1; }
int l_setCloudsEnabled(lua_State* L) { return WorldFlag(L, g_clouds) ? 1 : 1; }
int l_getBirdsEnabled(lua_State* L) { lua_pushboolean(L, g_birds); return 1; }
int l_setBirdsEnabled(lua_State* L) { return WorldFlag(L, g_birds) ? 1 : 1; }

int l_getRainLevel(lua_State* L) { lua_pushnumber(L, g_rain); return 1; }
int l_setRainLevel(lua_State* L) {
    float level = 0.f;
    if (!Number(L, 1, level) || level < 0.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_rain = level;
    Diag(g_diagWorld, "DIAG setRainLevel: CGame::SetRainLevel is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getBlurLevel(lua_State* L) { lua_pushinteger(L, g_blur); return 1; }
int l_setBlurLevel(lua_State* L) {
    float level = 0.f;
    if (!Number(L, 1, level) || level < 0.f || level > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_blur = static_cast<int>(level);
    Diag(g_diagWorld, "DIAG setBlurLevel: CGame::SetBlurLevel is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getMoonSize(lua_State* L) { lua_pushinteger(L, g_moon); return 1; }
int l_setMoonSize(lua_State* L) {
    float size = 0.f;
    if (!Number(L, 1, size) || size < 0.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_moon = static_cast<int>(size);
    Diag(g_diagWorld, "DIAG setMoonSize: CGame::SetMoonSize is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSunSize(lua_State* L) { lua_pushnumber(L, g_sunSize); return 1; }
int l_setSunSize(lua_State* L) {
    float size = 0.f;
    if (!Number(L, 1, size) || size < 0.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_sunSize = size;
    Diag(g_diagWorld, "DIAG setSunSize: CGame::SetSunSize is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSunColor(lua_State* L) {
    lua_pushinteger(L, g_sunR);
    lua_pushinteger(L, g_sunG);
    lua_pushinteger(L, g_sunB);
    lua_pushinteger(L, g_sunA);
    return 4;
}

int l_setSunColor(lua_State* L) {
    float r = 0, g = 0, b = 0, a = 255;
    if (!Number(L, 1, r) || !Number(L, 2, g) || !Number(L, 3, b) ||
        r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!lua_isnoneornil(L, 4)) {
        if (!Number(L, 4, a) || a < 0 || a > 255) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    g_sunR = static_cast<int>(r);
    g_sunG = static_cast<int>(g);
    g_sunB = static_cast<int>(b);
    g_sunA = static_cast<int>(a);
    g_sunColorSet = true;
    Diag(g_diagWorld, "DIAG setSunColor: CGame::SetSunColor is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSkyGradient(lua_State* L) {
    for (int value : g_sky) lua_pushinteger(L, value);
    return 8;
}

int l_setSkyGradient(lua_State* L) {
    float color[8] = {};
    for (int i = 0; i < 8; ++i) {
        if (!Number(L, i + 1, color[i]) || color[i] < 0.f || color[i] > 255.f) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    for (int i = 0; i < 8; ++i) g_sky[i] = static_cast<int>(color[i]);
    g_skySet = true;
    Diag(g_diagWorld, "DIAG setSkyGradient: CGame::SetSkyGradient is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_resetSkyGradient(lua_State* L) {
    (void)L;
    for (int& value : g_sky) value = 0;
    g_skySet = false;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getWindVelocity(lua_State* L) {
    lua_pushnumber(L, g_windX);
    lua_pushnumber(L, g_windY);
    lua_pushnumber(L, g_windZ);
    return 3;
}

int l_setWindVelocity(lua_State* L) {
    float x = 0, y = 0, z = 0;
    if (!Number(L, 1, x) || !Number(L, 2, y) || !Number(L, 3, z)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_windX = x;
    g_windY = y;
    g_windZ = z;
    g_windSet = true;
    Diag(g_diagWorld, "DIAG setWindVelocity: CGame::SetWindVelocity is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getWaveHeight(lua_State* L) { lua_pushnumber(L, g_wave); return 1; }
int l_setWaveHeight(lua_State* L) {
    float height = 0.f;
    if (!Number(L, 1, height) || height < 0.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_wave = height;
    Diag(g_diagWorld, "DIAG setWaveHeight: CGame::SetWaveHeight is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getFPSLimit(lua_State* L) { lua_pushinteger(L, g_fps); return 1; }
int l_setFPSLimit(lua_State* L) {
    // CLuaWorldDefs::SetFPSLimit (2741-2759) stores any uint16. The limiter is not hooked.
    float limit = 0.f;
    if (!Number(L, 1, limit) || limit < 0.f || limit > 65535.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_fps = static_cast<int>(limit);
    Diag(g_diagWorld, "DIAG setFPSLimit: CFPSLimiter::SetClientEnforcedFPS is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

WorldProp* FindWorldProp(const char* name) {
    if (!name) return nullptr;
    for (auto& prop : g_worldProps)
        if (std::strcmp(prop.name, name) == 0) return &prop;
    return nullptr;
}

int l_isWorldSpecialPropertyEnabled(lua_State* L) {
    WorldProp* prop = FindWorldProp(lua_tostring(L, 1));
    if (!prop) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, prop->enabled ? 1 : 0);
    return 1;
}

int l_setWorldSpecialPropertyEnabled(lua_State* L) {
    WorldProp* prop = FindWorldProp(lua_tostring(L, 1));
    if (!prop || lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    prop->enabled = lua_toboolean(L, 2) != 0;
    Diag(g_diagWorld, "DIAG setWorldSpecialPropertyEnabled: the CGame write and the player packet are not hooked; the flag is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_resetRainLevel(lua_State* L) {
    (void)L;
    g_rain = 0.f;
    lua_pushboolean(L, 1);
    return 1;
}
int l_resetMoonSize(lua_State* L) { (void)L; g_moon = 3; lua_pushboolean(L, 1); return 1; }
int l_resetSunSize(lua_State* L) { (void)L; g_sunSize = 1.f; lua_pushboolean(L, 1); return 1; }
int l_resetSunColor(lua_State* L) {
    (void)L;
    g_sunR = g_sunG = g_sunB = 0;
    g_sunA = 255;
    g_sunColorSet = false;
    lua_pushboolean(L, 1);
    return 1;
}
int l_resetWindVelocity(lua_State* L) {
    (void)L;
    g_windX = g_windY = g_windZ = 0.f;
    g_windSet = false;
    lua_pushboolean(L, 1);
    return 1;
}
int l_resetBlurLevel(lua_State* L) { (void)L; g_blur = 36; lua_pushboolean(L, 1); return 1; }

int l_getJetpackMaxHeight(lua_State* L) { lua_pushnumber(L, g_jetpack); return 1; }
int l_setJetpackMaxHeight(lua_State* L) {
    float height = 0.f;
    if (!Number(L, 1, height) || height < -20.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_jetpack = height;
    Diag(g_diagWorld, "DIAG setJetpackMaxHeight: CWorld::SetJetpackMaxHeight is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getAircraftMaxHeight(lua_State* L) { lua_pushnumber(L, g_aircraft); return 1; }
int l_setAircraftMaxHeight(lua_State* L) {
    float height = 0.f;
    if (!Number(L, 1, height) || height < 0.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_aircraft = height;
    Diag(g_diagWorld, "DIAG setAircraftMaxHeight: CWorld::SetAircraftMaxHeight is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getOcclusionsEnabled(lua_State* L) { lua_pushboolean(L, g_occlusions); return 1; }
int l_setOcclusionsEnabled(lua_State* L) { return WorldFlag(L, g_occlusions) ? 1 : 1; }
int l_getInteriorSoundsEnabled(lua_State* L) { lua_pushboolean(L, g_interiorSounds); return 1; }
int l_setInteriorSoundsEnabled(lua_State* L) { return WorldFlag(L, g_interiorSounds) ? 1 : 1; }

int l_setInteriorFurnitureEnabled(lua_State* L) {
    float room = 0.f;
    if (!Number(L, 1, room) || lua_type(L, 2) != LUA_TBOOLEAN || room < 0.f || room > 4.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_furniture[static_cast<int>(room)] = lua_toboolean(L, 2) != 0;
    Diag(g_diagWorld, "DIAG setInteriorFurnitureEnabled: the interior furniture write is not hooked; the flag is stored");
    lua_pushboolean(L, 1);
    return 1;
}
int l_getInteriorFurnitureEnabled(lua_State* L) {
    float room = 0.f;
    if (!Number(L, 1, room) || room < 0.f || room > 4.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, g_furniture[static_cast<int>(room)] ? 1 : 0);
    return 1;
}

int l_getGrainLevel(lua_State* L) { lua_pushinteger(L, g_grain); return 1; }
int l_setGrainLevel(lua_State* L) {
    float level = 0.f;
    if (!Number(L, 1, level) || level < 0.f || level > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_grain = static_cast<int>(level);
    Diag(g_diagWorld, "DIAG setGrainLevel: CGame::SetGrainLevel is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}
int l_getGrainMultiplier(lua_State* L) { lua_pushnumber(L, g_grainMul); return 1; }
int l_setGrainMultiplier(lua_State* L) {
    float value = 0.f;
    if (!Number(L, 1, value) || value < 0.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_grainMul = value;
    Diag(g_diagWorld, "DIAG setGrainMultiplier: CGame::SetGrainMultiplier is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getWaterColor(lua_State* L) {
    for (int value : g_water) lua_pushinteger(L, value);
    return 4;
}
int l_setWaterColor(lua_State* L) {
    float color[4] = {};
    for (int i = 0; i < 4; ++i) {
        if (!Number(L, i + 1, color[i]) || color[i] < 0.f || color[i] > 255.f) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    for (int i = 0; i < 4; ++i) g_water[i] = static_cast<int>(color[i]);
    g_waterSet = true;
    Diag(g_diagWorld, "DIAG setWaterColor: CMultiplayer::SetWaterColor is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}
int l_resetWaterColor(lua_State* L) {
    (void)L;
    g_water[0] = g_water[1] = g_water[2] = 0;
    g_water[3] = 200;
    g_waterSet = false;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getPedsLODDistance(lua_State* L) { lua_pushnumber(L, g_pedsLod); return 1; }
int l_setPedsLODDistance(lua_State* L) {
    float distance = 0.f;
    if (!Number(L, 1, distance)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (distance < 0.f) distance = 0.f;
    if (distance > 500.f) distance = 500.f;
    g_pedsLod = distance;
    Diag(g_diagWorld, "DIAG setPedsLODDistance: the ped LOD write is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}
int l_resetPedsLODDistance(lua_State* L) { (void)L; g_pedsLod = 500.f; lua_pushboolean(L, 1); return 1; }

int l_getVehiclesLODDistance(lua_State* L) {
    lua_pushnumber(L, g_vehiclesLod);
    lua_pushnumber(L, g_trainsLod);
    return 2;
}
int l_setVehiclesLODDistance(lua_State* L) {
    float vehicles = 0.f;
    if (!Number(L, 1, vehicles)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (vehicles < 0.f) vehicles = 0.f;
    if (vehicles > 500.f) vehicles = 500.f;
    float trains = vehicles * 2.14f;
    if (trains > 500.f) trains = 500.f;
    if (!lua_isnoneornil(L, 2)) {
        if (!Number(L, 2, trains)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        if (trains < 0.f) trains = 0.f;
        if (trains > 500.f) trains = 500.f;
    }
    g_vehiclesLod = vehicles;
    g_trainsLod = trains;
    Diag(g_diagWorld, "DIAG setVehiclesLODDistance: the vehicle LOD write is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}
int l_resetVehiclesLODDistance(lua_State* L) {
    (void)L;
    g_vehiclesLod = 500.f;
    g_trainsLod = 500.f;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getCursorAlpha(lua_State* L) { lua_pushinteger(L, g_cursorAlpha); return 1; }
int l_setCursorAlpha(lua_State* L) {
    float alpha = 0.f;
    if (!Number(L, 1, alpha) || alpha < 0.f || alpha > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cursorAlpha = static_cast<int>(alpha);
    Diag(g_diagWorld, "DIAG setCursorAlpha: the cursor paint is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_setElementDoubleSided(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (id == 0 || lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_fields[id].doubleSided = lua_toboolean(L, 2) != 0;
    Diag(g_diagWorld, "DIAG setElementDoubleSided: CEntity double-sided is not hooked; the flag is stored");
    lua_pushboolean(L, 1);
    return 1;
}
int l_isElementDoubleSided(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (id == 0 || KindOf(id) == Kind::None) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto found = g_fields.find(id);
    lua_pushboolean(L, found != g_fields.end() && found->second.doubleSided ? 1 : 0);
    return 1;
}
int l_setElementStreamable(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (id == 0 || lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_fields[id].streamable = lua_toboolean(L, 2) != 0;
    Diag(g_diagWorld, "DIAG setElementStreamable: the streamer flag is stored; streaming distance is not hooked");
    lua_pushboolean(L, 1);
    return 1;
}
int l_isElementStreamable(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (id == 0 || KindOf(id) == Kind::None) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto found = g_fields.find(id);
    lua_pushboolean(L, found != g_fields.end() && found->second.streamable ? 1 : 0);
    return 1;
}
int l_setElementLighting(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    float value = 0.f;
    if (id == 0 || !Number(L, 2, value)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_fields[id].lightingSet = true;
    g_fields[id].lighting = value;
    Diag(g_diagWorld, "DIAG setElementLighting: the entity light scale is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}
int l_getElementLighting(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const auto found = g_fields.find(id);
    if (id == 0 || found == g_fields.end() || !found->second.lightingSet) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, found->second.lighting);
    return 1;
}

bool ColorProp(const char* name) {
    return name && (std::strcmp(name, "AmbientColor") == 0 || std::strcmp(name, "AmbientObjColor") == 0 ||
                    std::strcmp(name, "DirectionalColor") == 0 || std::strcmp(name, "LowCloudsColor") == 0 ||
                    std::strcmp(name, "BottomCloudsColor") == 0);
}

bool KnownProp(const char* name) {
    static const char* names[] = {
        "AmbientColor", "AmbientObjColor", "DirectionalColor", "SpriteSize", "SpriteBrightness", "PoleShadowStrength",
        "ShadowStrength", "ShadowsOffset", "LightsOnGround", "LowCloudsColor", "BottomCloudsColor", "CloudsAlpha",
        "Illumination", "WetRoads", "Foggyness", "Fog", "RainFog", "WaterFog", "Sandstorm", "Rainbow"};
    if (!name) return false;
    for (const char* known : names)
        if (std::strcmp(known, name) == 0) return true;
    return false;
}

int l_getWorldProperty(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    if (!KnownProp(name)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto found = g_props.find(name);
    const float* value = found == g_props.end() ? nullptr : found->second.v;
    if (ColorProp(name)) {
        lua_pushnumber(L, value ? value[0] : 0);
        lua_pushnumber(L, value ? value[1] : 0);
        lua_pushnumber(L, value ? value[2] : 0);
        return 3;
    }
    lua_pushnumber(L, value ? value[0] : 0);
    return 1;
}

int l_setWorldProperty(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    float a = 0, b = 0, c = 0;
    if (!KnownProp(name) || !Number(L, 2, a)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool color = ColorProp(name);
    if (color) {
        if (!Number(L, 3, b) || !Number(L, 4, c) || a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    PropValue stored;
    stored.color = color;
    stored.v[0] = a;
    stored.v[1] = b;
    stored.v[2] = c;
    g_props[name] = stored;
    Diag(g_diagWorld, "DIAG setWorldProperty: the CMultiplayer write is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_resetWorldProperty(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    if (!KnownProp(name)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_props.erase(name);
    lua_pushboolean(L, 1);
    return 1;
}

int l_resetWorldProperties(lua_State* L) {
    (void)L;
    g_props.clear();
    lua_pushboolean(L, 1);
    return 1;
}

int OptionalInt(lua_State* L, int index, int fallback) {
    float value = 0.f;
    return Number(L, index, value) ? static_cast<int>(value) : fallback;
}

int l_setHeatHaze(lua_State* L) {
    float intensity = 0.f;
    if (!Number(L, 1, intensity) || intensity < 0.f || intensity > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_heat[0] = static_cast<int>(intensity);
    g_heat[1] = OptionalInt(L, 2, 0);
    g_heat[2] = OptionalInt(L, 3, 12);
    g_heat[3] = OptionalInt(L, 4, 18);
    g_heat[4] = OptionalInt(L, 5, 75);
    g_heat[5] = OptionalInt(L, 6, 80);
    g_heat[6] = OptionalInt(L, 7, 80);
    g_heat[7] = OptionalInt(L, 8, 85);
    if (lua_type(L, 9) == LUA_TBOOLEAN) g_heatInside = lua_toboolean(L, 9) != 0;
    Diag(g_diagWorld, "DIAG setHeatHaze: CMultiplayer::SetHeatHaze is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getHeatHaze(lua_State* L) {
    for (int value : g_heat) lua_pushinteger(L, value);
    lua_pushboolean(L, g_heatInside ? 1 : 0);
    return 9;
}

int l_resetHeatHaze(lua_State* L) {
    (void)L;
    g_heat[0] = 0; g_heat[1] = 0; g_heat[2] = 12; g_heat[3] = 18;
    g_heat[4] = 75; g_heat[5] = 80; g_heat[6] = 80; g_heat[7] = 85;
    g_heatInside = false;
    lua_pushboolean(L, 1);
    return 1;
}

int l_setColorFilter(lua_State* L) {
    float color[8] = {};
    for (int i = 0; i < 8; ++i) {
        if (!Number(L, i + 1, color[i]) || color[i] < 0.f || color[i] > 255.f) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    for (int i = 0; i < 8; ++i) g_filter[i] = static_cast<int>(color[i]);
    Diag(g_diagWorld, "DIAG setColorFilter: CMultiplayer::SetColorFilter is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getColorFilter(lua_State* L) {
    const bool original = lua_toboolean(L, 1) != 0 && lua_type(L, 1) == LUA_TBOOLEAN;
    for (int i = 0; i < 8; ++i) lua_pushinteger(L, original ? 0 : g_filter[i]);
    return 8;
}

int l_resetColorFilter(lua_State* L) {
    (void)L;
    for (int& value : g_filter) value = 0;
    lua_pushboolean(L, 1);
    return 1;
}

int TrafficColor(const char* name) {
    if (!name) return -1;
    if (std::strcmp(name, "green") == 0) return 0;
    if (std::strcmp(name, "yellow") == 0) return 1;
    if (std::strcmp(name, "red") == 0) return 2;
    return -1;
}

int l_setTrafficLightState(lua_State* L) {
    if (lua_isnumber(L, 1) && lua_isnoneornil(L, 2)) {
        const int state = static_cast<int>(lua_tointeger(L, 1));
        if (state < 0 || state > 9) {
            lua_pushboolean(L, 0);
            return 1;
        }
        g_traffic = state;
    } else if (lua_isstring(L, 1) && lua_isnoneornil(L, 2)) {
        const char* state = lua_tostring(L, 1);
        if (std::strcmp(state, "auto") == 0) {
            g_trafficLocked = false;
            g_traffic = 0;
        } else if (std::strcmp(state, "disabled") == 0) {
            g_trafficLocked = true;
            g_traffic = 9;
        } else {
            lua_pushboolean(L, 0);
            return 1;
        }
    } else {
        const int ns = TrafficColor(lua_tostring(L, 1));
        const int ew = TrafficColor(lua_tostring(L, 2));
        if (ns < 0 || ew < 0) {
            lua_pushboolean(L, 0);
            return 1;
        }
        static const int table[3][3] = {{5, 7, 3}, {8, 6, 4}, {0, 1, 2}};
        g_trafficLocked = true;
        g_traffic = table[ew][ns];
    }
    Diag(g_diagWorld, "DIAG setTrafficLightState: CMultiplayer::SetTrafficLightState is not hooked; the state is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getTrafficLightState(lua_State* L) { lua_pushinteger(L, g_traffic); return 1; }
int l_areTrafficLightsLocked(lua_State* L) { lua_pushboolean(L, g_trafficLocked); return 1; }
int l_setTrafficLightsLocked(lua_State* L) {
    if (lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_trafficLocked = lua_toboolean(L, 1) != 0;
    Diag(g_diagWorld, "DIAG setTrafficLightsLocked: the lock is stored; the traffic-light write is not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

int l_isTimeFrozen(lua_State* L) { lua_pushboolean(L, g_timeFrozen); return 1; }
int l_setTimeFrozen(lua_State* L) { return WorldFlag(L, g_timeFrozen) ? 1 : 1; }
int l_resetTimeFrozen(lua_State* L) { (void)L; g_timeFrozen = false; lua_pushboolean(L, 1); return 1; }

int l_getAircraftMaxVelocity(lua_State* L) { lua_pushnumber(L, g_aircraftVelocity); return 1; }
int l_setAircraftMaxVelocity(lua_State* L) {
    float velocity = 0.f;
    if (!Number(L, 1, velocity) || velocity < 0.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_aircraftVelocity = velocity;
    Diag(g_diagWorld, "DIAG setAircraftMaxVelocity: CWorld::SetAircraftMaxVelocity is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getCoronaReflectionsEnabled(lua_State* L) { lua_pushinteger(L, g_corona); return 1; }
int l_setCoronaReflectionsEnabled(lua_State* L) {
    float value = 0.f;
    if (!Number(L, 1, value) || value < 0.f || value > 2.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_corona = static_cast<int>(value);
    Diag(g_diagWorld, "DIAG setCoronaReflectionsEnabled: CCoronas::SetCoronaReflectionsEnabled is not hooked; the value is stored");
    lua_pushboolean(L, 1);
    return 1;
}
int l_resetCoronaReflectionsEnabled(lua_State* L) { (void)L; g_corona = 0; lua_pushboolean(L, 1); return 1; }

int l_isDynamicPedShadowsEnabled(lua_State* L) { lua_pushboolean(L, g_dynamicShadows); return 1; }
int l_setDynamicPedShadowsEnabled(lua_State* L) { return WorldFlag(L, g_dynamicShadows) ? 1 : 1; }
int l_resetDynamicPedShadows(lua_State* L) { (void)L; g_dynamicShadows = true; lua_pushboolean(L, 1); return 1; }
int l_isVolumetricShadowsEnabled(lua_State* L) { lua_pushboolean(L, g_volumetric); return 1; }
int l_setVolumetricShadowsEnabled(lua_State* L) { return WorldFlag(L, g_volumetric) ? 1 : 1; }
int l_resetVolumetricShadows(lua_State* L) { (void)L; g_volumetric = false; lua_pushboolean(L, 1); return 1; }
int l_isWaterDrawnLast(lua_State* L) { lua_pushboolean(L, g_waterLast); return 1; }
int l_setWaterDrawnLast(lua_State* L) { return WorldFlag(L, g_waterLast) ? 1 : 1; }
int l_isPedTargetingMarkerEnabled(lua_State* L) { lua_pushboolean(L, g_targetMarker); return 1; }
int l_setPedTargetingMarkerEnabled(lua_State* L) { return WorldFlag(L, g_targetMarker) ? 1 : 1; }

int l_setWeatherBlended(lua_State* L) {
    float weather = 0.f;
    if (!Number(L, 1, weather) || weather < 0.f || weather > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_weatherBlend = static_cast<int>(weather);
    g_blendSet = true;
    Diag(g_diagWorld, "DIAG setWeatherBlended: CBlendedWeather blend is not hooked; the target is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_setGarageOpen(lua_State* L) {
    float id = 0.f;
    if (!Number(L, 1, id) || lua_type(L, 2) != LUA_TBOOLEAN || id < 0.f || id >= 50.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_garage[static_cast<int>(id)] = lua_toboolean(L, 2) != 0;
    Diag(g_diagWorld, "DIAG setGarageOpen: CGarage::Open is not hooked; the flag is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_isGarageOpen(lua_State* L) {
    float id = 0.f;
    if (!Number(L, 1, id) || id < 0.f || id >= 50.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, g_garage[static_cast<int>(id)] ? 1 : 0);
    return 1;
}

float g_waterLevel = 0.f;
struct Occlusion { float x1, y1, x2, y2; };
std::vector<Occlusion> g_occlusionRegions;

int l_setWaterLevel(lua_State* L) {
    // CLuaWaterDefs::SetWaterLevel (193-276). A water element is not created. The level is stored.
    float a = 0, b = 0, c = 0, level = 0;
    if (Number(L, 1, a) && Number(L, 2, b) && Number(L, 3, c) && Number(L, 4, level)) {
        g_waterLevel = level;
        Diag(g_diagWorld, "DIAG setWaterLevel: CWaterManager::SetPositionWaterLevel is not hooked; the level is stored");
        lua_pushboolean(L, 1);
        return 1;
    }
    if (!Number(L, 1, level)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_waterLevel = level;
    Diag(g_diagWorld, "DIAG setWaterLevel: CWaterManager::SetWorldWaterLevel is not hooked; the level is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_resetWaterLevel(lua_State* L) {
    (void)L;
    g_waterLevel = 0.f;
    lua_pushboolean(L, 1);
    return 1;
}

int l_setOcclusionRegion(lua_State* L) {
    // CLuaWorldDefs::SetOcclusionRegion (3120-3139). The region is stored and the count is returned.
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!Number(L, 1, x1) || !Number(L, 2, y1) || !Number(L, 3, x2) || !Number(L, 4, y2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_occlusionRegions.push_back({x1, y1, x2, y2});
    Diag(g_diagWorld, "DIAG setOcclusionRegion: CWorld::SetOcclusionRegion is not hooked; the region is stored");
    lua_pushinteger(L, static_cast<lua_Integer>(g_occlusionRegions.size()));
    return 1;
}

int l_resetOcclusionRegion(lua_State* L) {
    (void)L;
    g_occlusionRegions.clear();
    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

void ApplySpatialArea(uint32_t id,uint64_t generation,uint8_t& interior,uint16_t& dimension) {
    auto it=g_fields.find(id);
    if(it==g_fields.end() || it->second.areaGeneration!=generation) return;
    if(it->second.interiorSet) interior=it->second.interior;
    if(it->second.dimensionSet) dimension=it->second.dimension;
}

void InstallFieldLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"getElementModel", l_getElementModel},
        // CLuaCompatibilityDefs.cpp:41, 50, 51 and 56. Same function as getElementModel.
        {"getPlayerSkin", l_getElementModel},
        {"getPedSkin", l_getElementModel},
        {"getVehicleModel", l_getElementModel},
        {"getVehicleID", l_getElementModel},
        {"getElementRotation", l_getElementRotation},
        // CLuaCompatibilityDefs.cpp:53 and 57. Same function as getElementRotation.
        {"getObjectRotation", l_getElementRotation},
        {"getVehicleRotation", l_getElementRotation},
        {"getElementMatrix", l_getElementMatrix},
        {"getElementAlpha", l_getElementAlpha},
        {"setElementAlpha", l_setElementAlpha},
        {"getElementInterior", l_getElementInterior},
        {"setElementInterior", l_setElementInterior},
        {"getElementDimension", l_getElementDimension},
        {"setElementDimension", l_setElementDimension},
        {"isElementFrozen", l_isElementFrozen},
        {"setElementHealth", l_setElementHealth},
        {"getElementHealth", l_getElementHealth},
        {"getWeather", l_getWeather},
        {"setWeather", l_setWeather},
        {"getGravity", l_getGravity},
        {"setGravity", l_setGravity},
        {"getGameSpeed", l_getGameSpeed},
        {"setGameSpeed", l_setGameSpeed},
        {"getMinuteDuration", l_getMinuteDuration},
        {"setMinuteDuration", l_setMinuteDuration},
        {"getFarClipDistance", l_getFarClipDistance},
        {"setFarClipDistance", l_setFarClipDistance},
        {"resetFarClipDistance", l_resetFarClipDistance},
        {"getFogDistance", l_getFogDistance},
        {"setFogDistance", l_setFogDistance},
        {"resetFogDistance", l_resetFogDistance},
        {"setElementCollisionsEnabled", l_setElementCollisionsEnabled},
        {"getElementCollisionsEnabled", l_getElementCollisionsEnabled},
        {"getNearClipDistance", l_getNearClipDistance},
        {"setNearClipDistance", l_setNearClipDistance},
        {"resetNearClipDistance", l_resetNearClipDistance},
        {"getCloudsEnabled", l_getCloudsEnabled},
        {"setCloudsEnabled", l_setCloudsEnabled},
        {"getBirdsEnabled", l_getBirdsEnabled},
        {"setBirdsEnabled", l_setBirdsEnabled},
        {"getRainLevel", l_getRainLevel},
        {"setRainLevel", l_setRainLevel},
        {"getBlurLevel", l_getBlurLevel},
        {"setBlurLevel", l_setBlurLevel},
        {"getMoonSize", l_getMoonSize},
        {"setMoonSize", l_setMoonSize},
        {"getSunSize", l_getSunSize},
        {"setSunSize", l_setSunSize},
        {"getSunColor", l_getSunColor},
        {"setSunColor", l_setSunColor},
        {"getSkyGradient", l_getSkyGradient},
        {"setSkyGradient", l_setSkyGradient},
        {"resetSkyGradient", l_resetSkyGradient},
        {"getWindVelocity", l_getWindVelocity},
        {"setWindVelocity", l_setWindVelocity},
        {"getWaveHeight", l_getWaveHeight},
        {"setWaveHeight", l_setWaveHeight},
        {"getFPSLimit", l_getFPSLimit},
        {"setFPSLimit", l_setFPSLimit},
        {"isWorldSpecialPropertyEnabled", l_isWorldSpecialPropertyEnabled},
        {"setWorldSpecialPropertyEnabled", l_setWorldSpecialPropertyEnabled},
        {"resetRainLevel", l_resetRainLevel},
        {"resetMoonSize", l_resetMoonSize},
        {"resetSunSize", l_resetSunSize},
        {"resetSunColor", l_resetSunColor},
        {"resetWindVelocity", l_resetWindVelocity},
        {"resetBlurLevel", l_resetBlurLevel},
        {"getJetpackMaxHeight", l_getJetpackMaxHeight},
        {"setJetpackMaxHeight", l_setJetpackMaxHeight},
        {"getAircraftMaxHeight", l_getAircraftMaxHeight},
        {"setAircraftMaxHeight", l_setAircraftMaxHeight},
        {"getOcclusionsEnabled", l_getOcclusionsEnabled},
        {"setOcclusionsEnabled", l_setOcclusionsEnabled},
        {"getInteriorSoundsEnabled", l_getInteriorSoundsEnabled},
        {"setInteriorSoundsEnabled", l_setInteriorSoundsEnabled},
        {"getInteriorFurnitureEnabled", l_getInteriorFurnitureEnabled},
        {"setInteriorFurnitureEnabled", l_setInteriorFurnitureEnabled},
        {"getGrainLevel", l_getGrainLevel},
        {"setGrainLevel", l_setGrainLevel},
        {"getGrainMultiplier", l_getGrainMultiplier},
        {"setGrainMultiplier", l_setGrainMultiplier},
        {"getWaterColor", l_getWaterColor},
        {"setWaterColor", l_setWaterColor},
        {"resetWaterColor", l_resetWaterColor},
        {"getPedsLODDistance", l_getPedsLODDistance},
        {"setPedsLODDistance", l_setPedsLODDistance},
        {"resetPedsLODDistance", l_resetPedsLODDistance},
        {"getVehiclesLODDistance", l_getVehiclesLODDistance},
        {"setVehiclesLODDistance", l_setVehiclesLODDistance},
        {"resetVehiclesLODDistance", l_resetVehiclesLODDistance},
        {"getCursorAlpha", l_getCursorAlpha},
        {"setCursorAlpha", l_setCursorAlpha},
        {"setElementDoubleSided", l_setElementDoubleSided},
        {"isElementDoubleSided", l_isElementDoubleSided},
        {"setElementStreamable", l_setElementStreamable},
        {"isElementStreamable", l_isElementStreamable},
        {"setElementLighting", l_setElementLighting},
        {"getElementLighting", l_getElementLighting},
        {"getWorldProperty", l_getWorldProperty},
        {"setWorldProperty", l_setWorldProperty},
        {"resetWorldProperty", l_resetWorldProperty},
        {"resetWorldProperties", l_resetWorldProperties},
        {"setHeatHaze", l_setHeatHaze},
        {"getHeatHaze", l_getHeatHaze},
        {"resetHeatHaze", l_resetHeatHaze},
        {"setColorFilter", l_setColorFilter},
        {"getColorFilter", l_getColorFilter},
        {"resetColorFilter", l_resetColorFilter},
        {"setTrafficLightState", l_setTrafficLightState},
        {"getTrafficLightState", l_getTrafficLightState},
        {"areTrafficLightsLocked", l_areTrafficLightsLocked},
        {"setTrafficLightsLocked", l_setTrafficLightsLocked},
        {"isTimeFrozen", l_isTimeFrozen},
        {"setTimeFrozen", l_setTimeFrozen},
        {"resetTimeFrozen", l_resetTimeFrozen},
        {"getAircraftMaxVelocity", l_getAircraftMaxVelocity},
        {"setAircraftMaxVelocity", l_setAircraftMaxVelocity},
        {"getCoronaReflectionsEnabled", l_getCoronaReflectionsEnabled},
        {"setCoronaReflectionsEnabled", l_setCoronaReflectionsEnabled},
        {"resetCoronaReflectionsEnabled", l_resetCoronaReflectionsEnabled},
        {"isDynamicPedShadowsEnabled", l_isDynamicPedShadowsEnabled},
        {"setDynamicPedShadowsEnabled", l_setDynamicPedShadowsEnabled},
        {"resetDynamicPedShadows", l_resetDynamicPedShadows},
        {"isVolumetricShadowsEnabled", l_isVolumetricShadowsEnabled},
        {"setVolumetricShadowsEnabled", l_setVolumetricShadowsEnabled},
        {"resetVolumetricShadows", l_resetVolumetricShadows},
        {"isWaterDrawnLast", l_isWaterDrawnLast},
        {"setWaterDrawnLast", l_setWaterDrawnLast},
        {"isPedTargetingMarkerEnabled", l_isPedTargetingMarkerEnabled},
        {"setPedTargetingMarkerEnabled", l_setPedTargetingMarkerEnabled},
        {"setWeatherBlended", l_setWeatherBlended},
        {"setGarageOpen", l_setGarageOpen},
        {"isGarageOpen", l_isGarageOpen},
        {"setWaterLevel", l_setWaterLevel},
        {"resetWaterLevel", l_resetWaterLevel},
        {"setOcclusionRegion", l_setOcclusionRegion},
        {"resetOcclusionRegion", l_resetOcclusionRegion},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
