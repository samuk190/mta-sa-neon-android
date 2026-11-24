// Desktop CLuaRocketSoccerDefs.cpp, function for function. Differences:
// - Desktop keeps one scene per resource VM in that VM's registry and frees it
//   with lua_close. Resources share a VM here, so each scene belongs to a
//   resource start (ActiveVmOwnerGeneration), is held by a registry reference
//   as on desktop (it lives until rocketSimDestroy, not until the script drops
//   its handle), and is freed by RocketSoccerDropResource when the resource
//   stops. A handle of a stopped resource then reports "destroyed".
// - rocketSimGamepad reads XInput and Sony HID devices on desktop. The phone
//   has no gamepad hook yet, so it takes desktop's no-device path (false).
#include "LuaRocketSoccerApi.h"

#include "LuaVMStub.h"
#include "LuaVmSet.h"
#include "RocketSoccerSimulation.h"

#include "lua.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <iterator>
#include <map>
#include <memory>

#ifdef __ANDROID__
#include <android/log.h>
#define ROCKET_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "RocketSoccer", __VA_ARGS__)
#else
#include <cstdio>
#define ROCKET_LOGI(...) (std::printf(__VA_ARGS__), std::printf("\n"))
#endif

namespace WorldRPG::Script {
namespace {

using RocketSoccer::Simulation;

constexpr const char* SceneType = "Neon.RocketSoccer.Scene";

struct Scene {
    uint32_t generation = 0;
    int ref = 0;  // registry reference to the handle (desktop SceneRegistryKey)
    std::unique_ptr<Simulation> simulation;
};
std::map<uint64_t, Scene> g_scenes;  // scene id -> scene
uint64_t g_nextScene = 1;

struct SceneHandle {
    uint64_t id;
};

Simulation* CheckHandle(lua_State* vm) {
    auto* handle = static_cast<SceneHandle*>(luaL_checkudata(vm, 1, SceneType));
    const auto found = g_scenes.find(handle->id);
    if (found == g_scenes.end() || !found->second.simulation) luaL_error(vm, "Rocket soccer scene has been destroyed");
    return found->second.simulation.get();
}

void Erase(std::map<uint64_t, Scene>::iterator it) {
    const int ref = it->second.ref;
    g_scenes.erase(it);
    if (ref > 0) Vms::Unref(ref);
}

int Collect(lua_State* vm) {
    auto* handle = static_cast<SceneHandle*>(luaL_checkudata(vm, 1, SceneType));
    if (const auto found = g_scenes.find(handle->id); found != g_scenes.end()) Erase(found);
    return 0;
}

float ReadAxis(lua_State* vm, const char* name) {
    lua_getfield(vm, 3, name);
    const float value = lua_isnumber(vm, -1) ? static_cast<float>(lua_tonumber(vm, -1)) : 0;
    lua_pop(vm, 1);
    return value;
}

bool ReadButton(lua_State* vm, const char* name) {
    lua_getfield(vm, 3, name);
    const bool value = lua_isboolean(vm, -1) && lua_toboolean(vm, -1);
    lua_pop(vm, 1);
    return value;
}

RocketSoccer::Controls ReadControls(lua_State* vm) {
    RocketSoccer::Controls controls;
    controls.throttle = ReadAxis(vm, "throttle");
    controls.steer = ReadAxis(vm, "steer");
    controls.pitch = ReadAxis(vm, "pitch");
    controls.yaw = ReadAxis(vm, "yaw");
    controls.roll = ReadAxis(vm, "roll");
    controls.jump = ReadButton(vm, "jump");
    controls.boost = ReadButton(vm, "boost");
    controls.handbrake = ReadButton(vm, "handbrake");
    return controls;
}

void PushVector(lua_State* vm, const char* key, const std::array<float, 3>& values) {
    lua_createtable(vm, 3, 0);
    for (int i = 0; i < 3; ++i) {
        lua_pushnumber(vm, values[i]);
        lua_rawseti(vm, -2, i + 1);
    }
    lua_setfield(vm, -2, key);
}

void PushBody(lua_State* vm, const char* key, const RocketSoccer::Body& body) {
    lua_createtable(vm, 0, 5);
    PushVector(vm, "position", body.position);
    PushVector(vm, "forward", body.forward);
    PushVector(vm, "right", body.right);
    PushVector(vm, "up", body.up);
    PushVector(vm, "velocity", body.velocity);
    if (key) lua_setfield(vm, -2, key);
}

void PushNumberField(lua_State* vm, const char* key, float value) {
    lua_pushnumber(vm, value);
    lua_setfield(vm, -2, key);
}

void PushBooleanField(lua_State* vm, const char* key, bool value) {
    lua_pushboolean(vm, value);
    lua_setfield(vm, -2, key);
}

void PushDecodedCar(lua_State* vm, const RocketSoccer::Body& body, const float* raw) {
    PushBody(vm, nullptr, body);
    PushVector(vm, "angularVelocity", {raw[15], raw[16], raw[17]});
    PushNumberField(vm, "boost", raw[18]);
    PushBooleanField(vm, "grounded", raw[19] == 1);
    PushBooleanField(vm, "supersonic", raw[20] == 1);
    PushBooleanField(vm, "demoed", raw[21] == 1);
    PushBooleanField(vm, "hasFlipOrJump", raw[22] == 1);
    PushBooleanField(vm, "boosting", raw[23] == 1);
    PushBooleanField(vm, "flipping", raw[24] == 1);
    PushVector(vm, "groundNormal", {raw[38], raw[39], raw[40]});

    lua_createtable(vm, 4, 0);
    for (int wheel = 0; wheel < 4; ++wheel) {
        const int offset = 26 + wheel * 3;
        lua_createtable(vm, 0, 3);
        PushNumberField(vm, "length", raw[offset]);
        PushNumberField(vm, "steer", raw[offset + 1]);
        PushBooleanField(vm, "contact", raw[offset + 2] == 1);
        lua_rawseti(vm, -2, wheel + 1);
    }
    lua_setfield(vm, -2, "wheels");

    lua_createtable(vm, 0, 11);
    PushNumberField(vm, "flipReset", raw[25]);
    PushNumberField(vm, "jump", raw[41]);
    PushNumberField(vm, "dodge", raw[42]);
    PushNumberField(vm, "doubleJump", raw[43]);
    PushNumberField(vm, "wheelImpact", raw[44]);
    PushNumberField(vm, "wheelImpactSpeed", raw[45]);
    PushNumberField(vm, "ballHit", raw[46]);
    PushNumberField(vm, "ballHitSpeed", raw[47]);
    PushNumberField(vm, "worldImpact", raw[48]);
    PushNumberField(vm, "worldImpactSpeed", raw[49]);
    PushNumberField(vm, "worldSurface", raw[50]);
    lua_setfield(vm, -2, "events");
}

int PushCompactSnapshot(lua_State* vm, const RocketSoccer::Snapshot& state) {
    lua_createtable(vm, 0, 12);
    const int stateIndex = lua_gettop(vm);
    PushBody(vm, "ball", state.ball);

    lua_createtable(vm, static_cast<int>(state.cars.size()), 0);
    const int carsIndex = lua_gettop(vm);
    for (size_t i = 0; i < state.cars.size(); ++i) {
        const float* raw = state.raw.data() + 22 + i * 51;
        PushDecodedCar(vm, state.cars[i], raw);
        if (i == 0) {
            lua_pushvalue(vm, -1);
            lua_setfield(vm, stateIndex, "car");
        }
        lua_rawseti(vm, carsIndex, static_cast<int>(i + 1));
    }
    lua_setfield(vm, stateIndex, "cars");

    const float* car = state.raw.data() + 22;
    PushVector(vm, "groundNormal", {car[38], car[39], car[40]});
    PushBooleanField(vm, "supersonic", car[20] == 1);
    PushNumberField(vm, "boost", car[18]);
    PushBooleanField(vm, "grounded", state.grounded);
    PushBooleanField(vm, "boosting", state.boosting);
    PushBooleanField(vm, "ballOnGround", state.ballOnGround);
    lua_pushinteger(vm, state.goalTeam);
    lua_setfield(vm, stateIndex, "goalTeam");
    lua_pushnumber(vm, static_cast<double>(state.ticks));
    lua_setfield(vm, stateIndex, "ticks");

    const int padCount = static_cast<int>(state.raw[3]);
    lua_createtable(vm, padCount, 0);
    for (int i = 0; i < padCount; ++i) {
        const int offset = 430 + i * 2;
        lua_createtable(vm, 0, 2);
        PushBooleanField(vm, "active", state.raw[offset] == 1);
        PushNumberField(vm, "cooldown", state.raw[offset + 1]);
        lua_rawseti(vm, -2, i + 1);
    }
    lua_setfield(vm, stateIndex, "pads");
    PushBooleanField(vm, "nativeDecoded", true);
    return 1;
}

int PushSnapshot(lua_State* vm, const RocketSoccer::Snapshot& state, bool compact = false) {
    if (compact) return PushCompactSnapshot(vm, state);
    lua_createtable(vm, 0, 6);
    PushBody(vm, "car", state.car);
    PushBody(vm, "ball", state.ball);
    lua_createtable(vm, static_cast<int>(state.cars.size()), 0);
    for (size_t i = 0; i < state.cars.size(); ++i) {
        PushBody(vm, nullptr, state.cars[i]);
        lua_rawseti(vm, -2, static_cast<int>(i + 1));
    }
    lua_setfield(vm, -2, "cars");
    lua_createtable(vm, static_cast<int>(state.raw.size()), 0);
    for (size_t i = 0; i < state.raw.size(); ++i) {
        lua_pushnumber(vm, state.raw[i]);
        lua_rawseti(vm, -2, static_cast<int>(i + 1));
    }
    lua_setfield(vm, -2, "raw");
    lua_pushboolean(vm, state.ballOnGround);
    lua_setfield(vm, -2, "ballOnGround");
    lua_pushboolean(vm, state.grounded);
    lua_setfield(vm, -2, "grounded");
    lua_pushboolean(vm, state.boosting);
    lua_setfield(vm, -2, "boosting");
    lua_pushinteger(vm, state.goalTeam);
    lua_setfield(vm, -2, "goalTeam");
    lua_pushnumber(vm, static_cast<double>(state.ticks));
    lua_setfield(vm, -2, "ticks");
    return 1;
}

// Native exceptions never cross Lua: each call catches, keeps the message,
// and raises after the catch (a Lua error longjmps past C++ destructors).
template <typename Fn>
bool Guard(lua_State* vm, Fn&& fn) {
    try {
        fn();
        return true;
    } catch (const std::exception& error) {
        lua_pushstring(vm, error.what());
    } catch (...) {
        lua_pushstring(vm, "Rocket soccer kernel failed");
    }
    return false;
}

int Create(lua_State* vm) {
    // One scene per resource, as desktop's one per resource VM.
    const uint32_t generation = ActiveVmOwnerGeneration();
    if (generation == 0) return luaL_error(vm, "rocketSimCreate needs a running resource");
    for (const auto& [id, scene] : g_scenes)
        if (scene.generation == generation && scene.simulation)
            return luaL_error(vm, "This resource already owns a rocket soccer scene");
    if (luaL_newmetatable(vm, SceneType)) {
        lua_pushcfunction(vm, Collect);
        lua_setfield(vm, -2, "__gc");
        lua_pushliteral(vm, "Rocket soccer scene");
        lua_setfield(vm, -2, "__metatable");
    }
    lua_pop(vm, 1);
    auto* handle = static_cast<SceneHandle*>(lua_newuserdata(vm, sizeof(SceneHandle)));
    handle->id = 0;
    luaL_getmetatable(vm, SceneType);
    lua_setmetatable(vm, -2);
    std::unique_ptr<Simulation> simulation;
    if (!Guard(vm, [&] { simulation = std::make_unique<Simulation>(); })) return lua_error(vm);
    handle->id = g_nextScene++;
    lua_pushvalue(vm, -1);
    const int ref = Vms::Ref(vm);
    g_scenes[handle->id] = Scene{generation, ref, std::move(simulation)};
    return 1;
}

int Step(lua_State* vm) {
    Simulation* simulation = CheckHandle(vm);
    const float seconds = static_cast<float>(luaL_checknumber(vm, 2));
    luaL_checktype(vm, 3, LUA_TTABLE);
    const RocketSoccer::Controls controls = ReadControls(vm);
    const bool compact = lua_toboolean(vm, 4) != 0;
    // Kept outside the stack frame: a Lua error while pushing longjmps past
    // C++ destructors, and a reused snapshot then leaks nothing.
    static RocketSoccer::Snapshot state;
    if (!Guard(vm, [&] { state = simulation->Step(seconds, controls); })) return lua_error(vm);
    return PushSnapshot(vm, state, compact);
}

int Reset(lua_State* vm) {
    Simulation* simulation = CheckHandle(vm);
    if (!Guard(vm, [&] { simulation->Reset(); })) return lua_error(vm);
    lua_pushboolean(vm, true);
    return 1;
}

int Destroy(lua_State* vm) {
    // Idempotent, so cleanup can run after a partial start failure.
    Collect(vm);
    lua_pushboolean(vm, true);
    return 1;
}

int GetArenaMesh(lua_State* vm) {
    const std::vector<RocketSoccer::Triangle>* mesh = nullptr;
    if (!Guard(vm, [&] { mesh = &RocketSoccer::GetArenaMesh(); })) return lua_error(vm);
    lua_createtable(vm, static_cast<int>(mesh->size()), 0);
    int index = 1;
    for (const auto& triangle : *mesh) {
        lua_createtable(vm, 10, 0);
        for (int i = 0; i < 9; ++i) {
            lua_pushnumber(vm, triangle.vertices[i]);
            lua_rawseti(vm, -2, i + 1);
        }
        lua_pushinteger(vm, triangle.material);
        lua_rawseti(vm, -2, 10);
        lua_rawseti(vm, -2, index++);
    }
    return 1;
}

int Command(lua_State* vm) {
    Simulation* simulation = CheckHandle(vm);
    const char* command = luaL_checkstring(vm, 2);
    // Read everything Lua can raise on before entering the guarded call.
    const bool b3 = lua_toboolean(vm, 3) != 0;
    const bool b4 = lua_toboolean(vm, 4) != 0;
    // Desktop's Lua 5.1 lua_tointeger truncates; read the number so both VMs do.
    const int i3 = lua_isnumber(vm, 3) ? static_cast<int>(lua_tonumber(vm, 3)) : 0;
    const bool opponent = !std::strcmp(command, "opponent");
    const RocketSoccer::Controls input = opponent && lua_istable(vm, 3) ? ReadControls(vm) : RocketSoccer::Controls{};
    int result = 1;
    const bool ok = Guard(vm, [&] {
        if (!std::strcmp(command, "configure")) simulation->Configure(b3, b4);
        else if (!std::strcmp(command, "unlimited")) simulation->SetUnlimitedBoost(b3);
        else if (!std::strcmp(command, "ball")) result = simulation->ControlBall(i3);
        else if (!std::strcmp(command, "goal")) result = simulation->PollGoal();
        else if (!std::strcmp(command, "resetCamera")) simulation->ResetCamera();
        else if (!std::strcmp(command, "kickoff")) simulation->Reset(i3);
        else if (opponent) simulation->SetControls(1, input);
        else result = 0;
    });
    if (!ok) return lua_error(vm);
    lua_pushinteger(vm, result);
    return 1;
}

int Pads(lua_State* vm) {
    Simulation* simulation = CheckHandle(vm);
    std::vector<float> pads;
    if (!Guard(vm, [&] { pads = simulation->Pads(); })) return lua_error(vm);
    lua_createtable(vm, static_cast<int>(pads.size()), 0);
    for (size_t i = 0; i < pads.size(); ++i) {
        lua_pushnumber(vm, pads[i]);
        lua_rawseti(vm, -2, static_cast<int>(i + 1));
    }
    return 1;
}

int CameraStep(lua_State* vm) {
    Simulation* simulation = CheckHandle(vm);
    luaL_checktype(vm, 2, LUA_TTABLE);
    std::array<double, 28> input{};
    for (int i = 0; i < 28; ++i) {
        lua_rawgeti(vm, 2, i + 1);
        input[i] = luaL_checknumber(vm, -1);
        lua_pop(vm, 1);
        if (!std::isfinite(input[i])) return luaL_error(vm, "Camera inputs must be finite");
    }
    std::array<double, 42> output{};
    if (!Guard(vm, [&] { output = simulation->Camera(input); })) return lua_error(vm);
    lua_createtable(vm, 42, 0);
    for (int i = 0; i < 42; ++i) {
        lua_pushnumber(vm, output[i]);
        lua_rawseti(vm, -2, i + 1);
    }
    return 1;
}

int Gamepad(lua_State* vm) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        ROCKET_LOGI("DIAG rocketSimGamepad: no gamepad hook on this client (desktop reads XInput / Sony HID)");
    }
    lua_pushboolean(vm, 0);
    return 1;
}

int Predict(lua_State* vm) {
    Simulation* simulation = CheckHandle(vm);
    size_t length = 0;
    const char* blob = "";
    if (!lua_isnil(vm, 2) && !(lua_isboolean(vm, 2) && !lua_toboolean(vm, 2))) blob = luaL_checklstring(vm, 2, &length);
    luaL_checktype(vm, 3, LUA_TTABLE);
    const size_t frameCount = lua_rawlen(vm, 3);
    const double requestedSlot = luaL_checknumber(vm, 4);
    luaL_checktype(vm, 5, LUA_TTABLE);
    const double fraction = luaL_checknumber(vm, 6);
    if (frameCount > 120 || (requestedSlot != 1 && requestedSlot != 2) || length > 512 * 1024 || !std::isfinite(fraction) ||
        fraction < 0 || fraction > 1)
        return luaL_error(vm, "Invalid prediction arguments");
    const int count = static_cast<int>(frameCount), slot = static_cast<int>(requestedSlot) - 1;
    // Validate Lua input before constructing objects that Lua errors would bypass.
    std::array<std::array<float, 8>, 121> values{};
    for (int frame = 0; frame <= count; ++frame) {
        if (frame == count) lua_pushvalue(vm, 5);
        else lua_rawgeti(vm, 3, frame + 1);
        luaL_checktype(vm, -1, LUA_TTABLE);
        for (int axis = 0; axis < 8; ++axis) {
            lua_rawgeti(vm, -1, axis + 1);
            const double value = lua_isnil(vm, -1) ? 0 : luaL_checknumber(vm, -1);
            if (!std::isfinite(value) || value < -1 || value > 1)
                return luaL_error(vm, "Prediction controls must be finite axes in [-1,1]");
            values[frame][axis] = axis >= 5 ? (value > 0 ? 1.f : 0.f) : static_cast<float>(value);
            lua_pop(vm, 1);
        }
        lua_pop(vm, 1);
    }
    static RocketSoccer::Snapshot state;  // see Step
    const bool ok = Guard(vm, [&] {
        const std::vector<std::array<float, 8>> frames(values.begin(), values.begin() + count);
        state = simulation->Predict({blob, length}, frames, slot, values[count], static_cast<float>(fraction));
    });
    if (!ok) return lua_error(vm);
    return PushSnapshot(vm, state);
}

int Rebase(lua_State* vm) {
    Simulation* simulation = CheckHandle(vm);
    size_t length = 0;
    const char* data = luaL_checklstring(vm, 2, &length);
    std::string normalized;
    if (!Guard(vm, [&] { normalized = simulation->Rebase({data, length}); })) return lua_error(vm);
    lua_pushlstring(vm, normalized.data(), normalized.size());
    return 1;
}

}  // namespace

void InstallRocketSoccerLua(lua_State* L) {
    // CLuaRocketSoccerDefs::LoadFunctions.
    static const struct {
        const char* name;
        lua_CFunction fn;
    } binds[] = {
        {"rocketSimGamepad", Gamepad}, {"rocketSimCreate", Create},   {"rocketSimCommand", Command},
        {"rocketSimPads", Pads},       {"rocketSimCameraStep", CameraStep}, {"rocketSimStep", Step},
        {"rocketSimPredict", Predict}, {"rocketSimRebase", Rebase},   {"rocketSimReset", Reset},
        {"rocketSimDestroy", Destroy}, {"rocketSimGetArenaMesh", GetArenaMesh},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

void RocketSoccerDropResource(uint32_t generation) {
    for (auto it = g_scenes.begin(); it != g_scenes.end();) {
        auto next = std::next(it);
        if (it->second.generation == generation) Erase(it);
        it = next;
    }
}

void RocketSoccerReset() {
    // The VMs are reset with the session, so their registry references go with them.
    g_scenes.clear();
}

}  // namespace WorldRPG::Script
