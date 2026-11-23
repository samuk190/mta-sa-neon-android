// Managed birds. Desktop CLuaBirdDefs.cpp, CClientBird.cpp 35-107, and
// CClientBirdManager::HandleGunShot. The bird mesh is not drawn and DoPulse does not run,
// so a bird stays where it was created.

#include "LuaBirdApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"

#include "lua.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace WorldRPG::Script {
namespace {

struct Vec {
    float x = 0, y = 0, z = 0;
};

struct Bird {
    uint32_t id = 0;
    Vec position;
    Vec velocity;
    Vec targetVelocity;
    float size = 0.55f;
    float renderDistance = 80.f;
    uint32_t body = 0;
    uint32_t wing = 0;
    uint32_t wingBeat = 400;
    bool curved = false;
    bool shootable = true;
    bool movement = true;
    uint16_t dimension = 0;
    uint8_t interior = 0;
};

std::unordered_map<uint32_t, Bird> g_birds;
bool g_diag = false;

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

bool Finite(lua_State* L, int index, float& out) {
    if (!lua_isnumber(L, index)) return false;
    const lua_Number number = lua_tonumber(L, index);
    if (!std::isfinite(number)) return false;
    out = static_cast<float>(number);
    return true;
}

uint32_t PackColor(int r, int g, int b) {
    r = std::max(0, std::min(r, 255));
    g = std::max(0, std::min(g, 255));
    b = std::max(0, std::min(b, 255));
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

bool ReadVectorField(lua_State* L, int table, const char* field, Vec& out) {
    lua_getfield(L, table, field);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    auto component = [&](const char* name, int index, float& value) {
        lua_getfield(L, -1, name);
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 1);
            lua_rawgeti(L, -1, index);
        }
        if (!lua_isnumber(L, -1) || !std::isfinite(lua_tonumber(L, -1))) {
            lua_pop(L, 1);
            return false;
        }
        value = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        return true;
    };
    const bool ok = component("x", 1, out.x) && component("y", 2, out.y) && component("z", 3, out.z);
    lua_pop(L, 1);
    return ok;
}

bool ReadNumberField(lua_State* L, int table, const char* field, float& out) {
    lua_getfield(L, table, field);
    const bool ok = lua_isnumber(L, -1) && std::isfinite(lua_tonumber(L, -1));
    if (ok) out = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return ok;
}

bool ReadBoolField(lua_State* L, int table, const char* field, bool& out) {
    lua_getfield(L, table, field);
    const bool ok = lua_type(L, -1) == LUA_TBOOLEAN;
    if (ok) out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return ok;
}

bool ReadColorField(lua_State* L, int table, const char* field, uint32_t& color) {
    lua_getfield(L, table, field);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    int parts[3] = {};
    const char* names[] = {"r", "g", "b"};
    for (int i = 0; i < 3; ++i) {
        lua_getfield(L, -1, names[i]);
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 1);
            lua_rawgeti(L, -1, i + 1);
        }
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 2);
            return false;
        }
        parts[i] = static_cast<int>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    color = PackColor(parts[0], parts[1], parts[2]);
    return true;
}

Bird* Find(uint32_t id) {
    auto found = g_birds.find(id);
    if (found == g_birds.end()) return nullptr;
    if (!KnownClientElement(id)) {
        g_birds.erase(found);
        return nullptr;
    }
    return &g_birds.find(id)->second;
}

void PushVector(lua_State* L, const Vec& value) {
    lua_getglobal(L, "Vector3");
    lua_pushnumber(L, value.x);
    lua_pushnumber(L, value.y);
    lua_pushnumber(L, value.z);
    if (!lua_isfunction(L, -4) || lua_pcall(L, 3, 1, 0) != 0) {
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
    }
}

int CreateBird(lua_State* L) {
    // CLuaBirdDefs::CreateBird (183-277). A bad position returns false without a warning.
    float x = 0, y = 0, z = 0;
    if (!Finite(L, 1, x) || !Finite(L, 2, y) || !Finite(L, 3, z)) return Fail(L);
    float speed = 5.3f;
    float size = 0.55f;
    float renderDistance = 80.f;
    uint32_t body = PackColor(210, 210, 210);
    uint32_t wing = PackColor(235, 235, 235);
    uint32_t wingBeat = 400;
    bool curved = false;
    bool shootable = true;
    bool movement = true;
    if (lua_istable(L, 4)) {
        lua_getfield(L, 4, "preset");
        if (lua_isstring(L, -1)) {
            const std::string preset = lua_tostring(L, -1);
            if (preset == "water") {
                speed = 4.3f;
                size = 1.f;
                renderDistance = 90.f;
                wingBeat = 820;
                body = PackColor(130, 130, 130);
                wing = PackColor(220, 220, 220);
            } else if (preset == "desert") {
                speed = 3.3f;
                size = 2.2f;
                renderDistance = 160.f;
                wingBeat = 1320;
                curved = true;
                body = PackColor(70, 38, 25);
                wing = PackColor(120, 45, 28);
            }
        }
        lua_pop(L, 1);
        ReadNumberField(L, 4, "speed", speed);
        ReadNumberField(L, 4, "size", size);
        ReadNumberField(L, 4, "renderDistance", renderDistance);
        float beat = static_cast<float>(wingBeat);
        if (ReadNumberField(L, 4, "wingBeatTime", beat) && beat > 0.f) wingBeat = static_cast<uint32_t>(beat);
        ReadBoolField(L, 4, "curvedFlight", curved);
        ReadBoolField(L, 4, "shootable", shootable);
        ReadBoolField(L, 4, "movementEnabled", movement);
        ReadColorField(L, 4, "bodyColor", body);
        ReadColorField(L, 4, "wingColor", wing);
    }
    Vec velocity{0, speed, 0};
    Vec targetVelocity = velocity;
    if (lua_istable(L, 4)) {
        ReadVectorField(L, 4, "velocity", velocity);
        targetVelocity = velocity;
        ReadVectorField(L, 4, "targetVelocity", targetVelocity);
        Vec aim;
        if (ReadVectorField(L, 4, "target", aim)) {
            Vec direction{aim.x - x, aim.y - y, aim.z - z};
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
            if (length > 0.001f) {
                velocity = {direction.x / length * speed, direction.y / length * speed, direction.z / length * speed};
                targetVelocity = velocity;
            }
        }
    }
    if (!std::isfinite(size) || !std::isfinite(renderDistance) || size <= 0.f || size > 20.f || renderDistance <= 0.f ||
        renderDistance > 5000.f || wingBeat == 0)
        return Fail(L);
    const uint32_t id = CreateResourceElement("bird");
    if (!id) return Fail(L);
    Bird bird;
    bird.id = id;
    bird.position = {x, y, z};
    bird.velocity = velocity;
    bird.targetVelocity = targetVelocity;
    bird.size = size;
    bird.renderDistance = renderDistance;
    bird.body = body;
    bird.wing = wing;
    bird.wingBeat = wingBeat;
    bird.curved = curved;
    bird.shootable = shootable;
    bird.movement = movement;
    g_birds[id] = bird;
    if (!g_diag) {
        g_diag = true;
        LUA_LOGE("DIAG createBird: the bird mesh is not drawn and DoPulse does not run");
    }
    PushElement(L, id);
    return 1;
}

int GetBirdVelocity(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) return Fail(L);
    PushVector(L, bird->velocity);
    return 1;
}

int SetBirdVelocity(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    float x = 0, y = 0, z = 0;
    if (!bird || !Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z)) return Fail(L);
    bird->velocity = {x, y, z};
    lua_pushboolean(L, 1);
    return 1;
}

int GetBirdTargetVelocity(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) return Fail(L);
    PushVector(L, bird->targetVelocity);
    return 1;
}

int SetBirdTargetVelocity(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    float x = 0, y = 0, z = 0;
    if (!bird || !Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z)) return Fail(L);
    bird->targetVelocity = {x, y, z};
    lua_pushboolean(L, 1);
    return 1;
}

int GetBirdSize(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) return Fail(L);
    lua_pushnumber(L, bird->size);
    return 1;
}

int SetBirdSize(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    float size = 0;
    if (!bird || !Finite(L, 2, size) || size <= 0.f || size > 20.f) return Fail(L);
    bird->size = size;
    lua_pushboolean(L, 1);
    return 1;
}

int GetBirdColors(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) return Fail(L);
    lua_pushnumber(L, (bird->body >> 16) & 0xFF);
    lua_pushnumber(L, (bird->body >> 8) & 0xFF);
    lua_pushnumber(L, bird->body & 0xFF);
    lua_pushnumber(L, (bird->wing >> 16) & 0xFF);
    lua_pushnumber(L, (bird->wing >> 8) & 0xFF);
    lua_pushnumber(L, bird->wing & 0xFF);
    return 6;
}

int SetBirdColors(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    float r = 0, g = 0, b = 0, wr = 0, wg = 0, wb = 0;
    if (!bird || !Finite(L, 2, r) || !Finite(L, 3, g) || !Finite(L, 4, b) || !Finite(L, 5, wr) || !Finite(L, 6, wg) || !Finite(L, 7, wb))
        return Fail(L);
    bird->body = PackColor(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
    bird->wing = PackColor(static_cast<int>(wr), static_cast<int>(wg), static_cast<int>(wb));
    lua_pushboolean(L, 1);
    return 1;
}

int GetBirdWingBeatTime(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) return Fail(L);
    lua_pushnumber(L, bird->wingBeat);
    return 1;
}

int SetBirdWingBeatTime(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    float value = 0;
    if (!bird || !Finite(L, 2, value) || value < 1.f || value > 600000.f) return Fail(L);
    bird->wingBeat = static_cast<uint32_t>(value);
    lua_pushboolean(L, 1);
    return 1;
}

int GetBirdRenderDistance(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) return Fail(L);
    lua_pushnumber(L, bird->renderDistance);
    return 1;
}

int SetBirdRenderDistance(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    float value = 0;
    if (!bird || !Finite(L, 2, value) || value <= 0.f || value > 5000.f) return Fail(L);
    bird->renderDistance = value;
    lua_pushboolean(L, 1);
    return 1;
}

int IsBirdCurvedFlightEnabled(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) return Fail(L);
    lua_pushboolean(L, bird->curved ? 1 : 0);
    return 1;
}

int SetBirdCurvedFlightEnabled(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird || lua_type(L, 2) != LUA_TBOOLEAN) return Fail(L);
    bird->curved = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int IsBirdShootable(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) return Fail(L);
    lua_pushboolean(L, bird->shootable ? 1 : 0);
    return 1;
}

int SetBirdShootable(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird || lua_type(L, 2) != LUA_TBOOLEAN) return Fail(L);
    bird->shootable = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int IsBirdMovementEnabled(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) return Fail(L);
    lua_pushboolean(L, bird->movement ? 1 : 0);
    return 1;
}

int SetBirdMovementEnabled(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird || lua_type(L, 2) != LUA_TBOOLEAN) return Fail(L);
    bird->movement = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int ProcessBirdGunShot(lua_State* L) {
    // CClientBirdManager::HandleGunShot. A hit fires onClientBirdShot and destroys the bird
    // unless the event is cancelled. No local player, or a zero-length shot, returns false.
    float x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    if (!Finite(L, 1, x0) || !Finite(L, 2, y0) || !Finite(L, 3, z0) || !Finite(L, 4, x1) || !Finite(L, 5, y1) || !Finite(L, 6, z1))
        return Fail(L);
    int weapon = 0;
    if (!lua_isnoneornil(L, 7)) {
        float weaponNumber = 0;
        if (!Finite(L, 7, weaponNumber)) return Fail(L);
        weapon = static_cast<int>(weaponNumber);
    }
    const uint32_t player = ServerWorld::LocalPlayer();
    if (!player) return Fail(L);
    const float sx = x1 - x0, sy = y1 - y0, sz = z1 - z0;
    const float lengthSq = sx * sx + sy * sy + sz * sz;
    if (lengthSq <= 0.000001f) return Fail(L);
    Bird* best = nullptr;
    float bestT = 1e30f;
    float hitX = 0, hitY = 0, hitZ = 0;
    const uint16_t dimension = ServerWorld::DimensionOf(player);
    const uint8_t interior = ServerWorld::InteriorOf(player);
    for (auto& [id, bird] : g_birds) {
        (void)id;
        if (!KnownClientElement(bird.id) || !bird.shootable || bird.dimension != dimension || bird.interior != interior) continue;
        const float tx = bird.position.x - x0, ty = bird.position.y - y0, tz = bird.position.z - z0;
        float t = (tx * sx + ty * sy + tz * sz) / lengthSq;
        t = std::max(0.f, std::min(t, 1.f));
        const float cx = x0 + sx * t, cy = y0 + sy * t, cz = z0 + sz * t;
        const float dx = cx - bird.position.x, dy = cy - bird.position.y, dz = cz - bird.position.z;
        if (dx * dx + dy * dy + dz * dz > 0.25f || t >= bestT) continue;
        best = &bird;
        bestT = t;
        hitX = cx;
        hitY = cy;
        hitZ = cz;
    }
    if (!best) return Fail(L);
    const uint32_t id = best->id;
    if (!ActiveVmFireBirdShot(id, player, weapon, hitX, hitY, hitZ)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    if (KnownClientElement(id)) DestroyClientElement(id);
    g_birds.erase(id);
    lua_pushboolean(L, 1);
    return 1;
}

int GetElementPositionBird(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    lua_pushnumber(L, bird->position.x);
    lua_pushnumber(L, bird->position.y);
    lua_pushnumber(L, bird->position.z);
    return 3;
}

int SetElementPositionBird(lua_State* L) {
    Bird* bird = Find(ElementIdOf(L, 1));
    if (!bird) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    float x = 0, y = 0, z = 0;
    if (!Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z)) return Fail(L);
    bird->position = {x, y, z};
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    g_birds.clear();
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createBird", CreateBird},
        {"getBirdVelocity", GetBirdVelocity},
        {"setBirdVelocity", SetBirdVelocity},
        {"getBirdTargetVelocity", GetBirdTargetVelocity},
        {"setBirdTargetVelocity", SetBirdTargetVelocity},
        {"getBirdSize", GetBirdSize},
        {"setBirdSize", SetBirdSize},
        {"getBirdColors", GetBirdColors},
        {"setBirdColors", SetBirdColors},
        {"getBirdWingBeatTime", GetBirdWingBeatTime},
        {"setBirdWingBeatTime", SetBirdWingBeatTime},
        {"getBirdRenderDistance", GetBirdRenderDistance},
        {"setBirdRenderDistance", SetBirdRenderDistance},
        {"isBirdCurvedFlightEnabled", IsBirdCurvedFlightEnabled},
        {"setBirdCurvedFlightEnabled", SetBirdCurvedFlightEnabled},
        {"isBirdShootable", IsBirdShootable},
        {"setBirdShootable", SetBirdShootable},
        {"isBirdMovementEnabled", IsBirdMovementEnabled},
        {"setBirdMovementEnabled", SetBirdMovementEnabled},
        {"processBirdGunShot", ProcessBirdGunShot},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_getglobal(L, "getElementPosition");
    lua_pushcclosure(L, GetElementPositionBird, 1);
    lua_setglobal(L, "getElementPosition");
    lua_getglobal(L, "setElementPosition");
    lua_pushcclosure(L, SetElementPositionBird, 1);
    lua_setglobal(L, "setElementPosition");
}

}  // namespace

void BirdRelease(uint32_t id) { g_birds.erase(id); }
void InstallBirdLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
