// Client camera Lua. Desktop references, in call order:
//   CLuaCameraDefs.cpp LoadFunctions 68-119 and handlers 183-932
//   CStaticFunctionDefinitions.cpp 5124-5311
//   CClientCamera.cpp AcquireScriptCamera 637-671, BeginFileCutscene 673-696,
//     ReleaseScriptCamera / RestoreScriptCameraLease 739-815, view and clip
//     876-927, ShakeCamera 1187-1198
// A step with no libGTASA hook is kept and logged once with DIAG.

#include "LuaCameraApi.h"
#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace WorldRPG::Script {
namespace {

constexpr uint32_t kCameraId = 0x70000001u;

struct Vec3 { float x = 0, y = 0, z = 0; };

struct CameraState {
    bool fixed = false;
    Vec3 position{};
    Vec3 look{0.f, 1.f, 0.f};
    float roll = 0.f;
    float fov = 70.f;
    float playerFov = 70.f;
    float vehicleFov = 70.f;
    float vehicleMaxFov = 70.f;
    unsigned char interior = 0;
    unsigned char drunk = 0;
    unsigned char vehicleView = 0;  // eVehicleCamMode::BUMPER
    unsigned char pedView = 0;      // ePedCamMode::CLOSE
    bool clipObjects = true;
    bool clipVehicles = true;
    std::string goggle = "normal";
    float aimH = 0.f;
    float aimV = 0.f;
    bool aimSet = false;
    float shake = 0.f;

    bool lease = false;
    std::string owner;
    uint32_t token = 0;
    bool previousFixed = false;
    Vec3 previousPosition{};
    Vec3 previousLook{0.f, 1.f, 0.f};
    float previousFov = 70.f;
    bool fileLease = false;
    bool fileStarted = false;
    bool inhibit = true;

    bool fading = false;
    bool fadeIn = true;
    float fadeSeconds = 0.f;
    unsigned char fadeR = 0, fadeG = 0, fadeB = 0;
    std::chrono::steady_clock::time_point fadeStart{};

    bool moving = false;
    bool tracking = false;
    bool easeMove = true;
    bool easeTrack = true;
    Vec3 moveFrom{}, moveTo{}, trackFrom{}, trackTo{};
    int moveMs = 0;
    int trackMs = 0;
    std::chrono::steady_clock::time_point moveStart{};
    std::chrono::steady_clock::time_point trackStart{};
    bool persistPosition = false;
    bool persistTarget = false;
    bool widescreen = false;
    bool nearClip = false;
    float nearClipDistance = 0.3f;
};

CameraState g_cam;
bool g_diagFov = false;
bool g_diagInterior = false;
bool g_diagGoggle = false;
bool g_diagDrunk = false;
bool g_diagClip = false;
bool g_diagView = false;
bool g_diagShake = false;
bool g_diagAim = false;
bool g_diagFade = false;
bool g_diagCutscene = false;
bool g_diagScriptFixed = false;
bool g_diagOrbit = false;
bool g_diagFocus = false;

void Diag(bool& once, const char* message) {
    if (once) return;
    once = true;
    LUA_LOGE("%s", message);
}

bool Finite(float value) { return std::isfinite(value); }

bool ReadFloat(lua_State* L, int index, float& out) {
    if (!lua_isnumber(L, index)) return false;
    out = static_cast<float>(lua_tonumber(L, index));
    return Finite(out);
}

bool ReadVec3(lua_State* L, int index, Vec3& out) {
    return ReadFloat(L, index, out.x) && ReadFloat(L, index + 1, out.y) && ReadFloat(L, index + 2, out.z);
}

bool OptionalFloat(lua_State* L, int index, float& out, float fallback) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    return ReadFloat(L, index, out);
}

void ApplyFixed(const Vec3& position, const Vec3& look) {
    Game::NoteScriptCamera(position.x, position.y, position.z, look.x, look.y, look.z);
}

void RestoreLease(bool preserveFade) {
    if (!g_cam.lease) return;
    g_cam.fileLease = false;
    g_cam.fileStarted = false;
    g_cam.moving = false;
    g_cam.tracking = false;
    g_cam.lease = false;
    g_cam.owner.clear();
    g_cam.nearClip = false;
    g_cam.widescreen = false;
    g_cam.persistPosition = false;
    g_cam.persistTarget = false;
    if (!preserveFade) g_cam.fading = false;
    if (g_cam.previousFixed) {
        g_cam.fixed = true;
        g_cam.position = g_cam.previousPosition;
        g_cam.look = g_cam.previousLook;
        g_cam.fov = g_cam.previousFov;
        ApplyFixed(g_cam.position, g_cam.look);
    } else {
        g_cam.fixed = false;
        Game::ClearScriptCamera();
    }
}

bool Owned(unsigned token) {
    return g_cam.lease && token != 0 && token == g_cam.token && g_cam.owner == ActiveVmOwner();
}

bool OwnedFile(unsigned token) { return Owned(token) && g_cam.fileLease; }

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float Ease(float t, bool ease) {
    if (!ease) return t;
    return t * t * (3.f - 2.f * t);
}

int ElapsedMs(std::chrono::steady_clock::time_point start) {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count());
}

void PushCamera(lua_State* L) {
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(kCameraId));
    lua_setfield(L, -2, "id");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "element");
}

int l_getCamera(lua_State* L) {
    PushCamera(L);
    return 1;
}

int l_getCameraViewMode(lua_State* L) {
    lua_pushinteger(L, g_cam.vehicleView);
    lua_pushinteger(L, g_cam.pedView);
    return 2;
}

int l_getCameraMatrix(lua_State* L) {
    lua_pushnumber(L, g_cam.position.x);
    lua_pushnumber(L, g_cam.position.y);
    lua_pushnumber(L, g_cam.position.z);
    lua_pushnumber(L, g_cam.look.x);
    lua_pushnumber(L, g_cam.look.y);
    lua_pushnumber(L, g_cam.look.z);
    lua_pushnumber(L, g_cam.roll);
    lua_pushnumber(L, g_cam.fov);
    return 8;
}

int l_getCameraTarget(lua_State* L) {
    // CStaticFunctionDefinitions::GetCameraTarget: false while the camera is fixed.
    if (g_cam.fixed) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_getglobal(L, "localPlayer");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
    }
    return 1;
}

int l_getCameraInterior(lua_State* L) {
    lua_pushinteger(L, g_cam.interior);
    return 1;
}

int l_getCameraGoggleEffect(lua_State* L) {
    lua_pushstring(L, g_cam.goggle.c_str());
    return 1;
}

int l_getCameraFieldOfView(lua_State* L) {
    const char* mode = lua_tostring(L, 1);
    if (!mode) {
        LUA_LOGE("getCameraFieldOfView: expected a mode");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (std::strcmp(mode, "player") == 0) lua_pushnumber(L, g_cam.playerFov);
    else if (std::strcmp(mode, "vehicle") == 0) lua_pushnumber(L, g_cam.vehicleFov);
    else if (std::strcmp(mode, "vehicle_max") == 0) lua_pushnumber(L, g_cam.vehicleMaxFov);
    else {
        LUA_LOGE("getCameraFieldOfView: unknown mode %s", mode);
        lua_pushboolean(L, 0);
    }
    return 1;
}

int l_getCameraDrunkLevel(lua_State* L) {
    lua_pushinteger(L, g_cam.drunk);
    return 1;
}

int l_setCameraMatrix(lua_State* L) {
    // CLuaCameraDefs::SetCameraMatrix returns false while a script lease is active.
    if (g_cam.lease) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Vec3 position;
    if (!ReadVec3(L, 1, position)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Vec3 look = g_cam.look;
    const bool lookValid = ReadVec3(L, 4, look);
    float roll = 0.f;
    float fov = 70.f;
    if (!OptionalFloat(L, lookValid ? 7 : 4, roll, 0.f)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!OptionalFloat(L, lookValid ? 8 : 5, fov, 70.f)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (fov <= 0.f || fov >= 180.f) fov = 70.f;
    g_cam.fixed = true;
    g_cam.position = position;
    if (lookValid) g_cam.look = look;
    g_cam.roll = roll;
    g_cam.fov = fov;
    ApplyFixed(g_cam.position, g_cam.look);
    lua_pushboolean(L, 1);
    return 1;
}

int l_setCameraFieldOfView(lua_State* L) {
    if (g_cam.lease) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* mode = lua_tostring(L, 1);
    float fov = 0.f;
    if (!mode || !ReadFloat(L, 2, fov) || fov < 0.f || fov > 179.f) {
        LUA_LOGE("setCameraFieldOfView: expected mode and FOV in 0-179");
        lua_pushboolean(L, 0);
        return 1;
    }
    float* slot = nullptr;
    if (std::strcmp(mode, "player") == 0) slot = &g_cam.playerFov;
    else if (std::strcmp(mode, "vehicle") == 0) slot = &g_cam.vehicleFov;
    else if (std::strcmp(mode, "vehicle_max") == 0) slot = &g_cam.vehicleMaxFov;
    if (!slot) {
        LUA_LOGE("setCameraFieldOfView: unknown mode %s", mode);
        lua_pushboolean(L, 0);
        return 1;
    }
    *slot = fov;
    Diag(g_diagFov, "DIAG setCameraFieldOfView: CSettings field-of-view write is not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

int l_setCameraTarget(lua_State* L) {
    if (g_cam.lease) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (lua_istable(L, 1)) {
        const uint32_t id = ElementIdOf(L, 1);
        const bool ped = Game::ScriptPedAlive(id);
        const char* kind = ServerWorld::ElementKindName(id);
        const bool player = id == ServerWorld::LocalPlayer() || (kind && std::strcmp(kind, "player") == 0);
        const bool vehicle = kind && std::strcmp(kind, "vehicle") == 0;
        const bool serverPed = kind && std::strcmp(kind, "ped") == 0;
        if (!player && !ped && !vehicle && !serverPed) {
            lua_pushboolean(L, 0);
            return 1;
        }
        g_cam.fixed = false;
        Game::ClearScriptCamera();
        if (!player)
            Diag(g_diagFocus, "DIAG setCameraTarget: CClientCamera::SetFocus is not hooked for a remote element");
        lua_pushboolean(L, 1);
        return 1;
    }
    Vec3 target;
    if (!ReadVec3(L, 1, target)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.fixed = true;
    g_cam.look = target;
    ApplyFixed(g_cam.position, g_cam.look);
    Diag(g_diagOrbit, "DIAG setCameraTarget: CClientCamera::SetOrbitTarget uses the fixed look-at");
    lua_pushboolean(L, 1);
    return 1;
}

int l_setCameraInterior(lua_State* L) {
    float value = 0.f;
    if (!ReadFloat(L, 1, value) || value < 0.f || value > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.interior = static_cast<unsigned char>(value);
    Diag(g_diagInterior, "DIAG setCameraInterior: CWorld::SetCurrentArea is not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

int l_fadeCamera(lua_State* L) {
    if (g_cam.lease) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (lua_type(L, 1) != LUA_TBOOLEAN) {
        LUA_LOGE("fadeCamera: expected a boolean");
        lua_pushboolean(L, 0);
        return 1;
    }
    float seconds = 1.f;
    if (!OptionalFloat(L, 2, seconds, 1.f) || seconds < 0.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float red = 0.f, green = 0.f, blue = 0.f;
    if (!OptionalFloat(L, 3, red, 0.f) || !OptionalFloat(L, 4, green, 0.f) || !OptionalFloat(L, 5, blue, 0.f)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.fading = seconds > 0.f;
    g_cam.fadeIn = lua_toboolean(L, 1) != 0;
    g_cam.fadeSeconds = seconds;
    g_cam.fadeR = static_cast<unsigned char>(red < 0.f ? 0.f : (red > 255.f ? 255.f : red));
    g_cam.fadeG = static_cast<unsigned char>(green < 0.f ? 0.f : (green > 255.f ? 255.f : green));
    g_cam.fadeB = static_cast<unsigned char>(blue < 0.f ? 0.f : (blue > 255.f ? 255.f : blue));
    g_cam.fadeStart = std::chrono::steady_clock::now();
    Diag(g_diagFade, "DIAG fadeCamera: CCamera fade is not hooked; the timer still reports IsFading");
    lua_pushboolean(L, 1);
    return 1;
}

int l_setCameraClip(lua_State* L) {
    if (!lua_isnoneornil(L, 1) && lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!lua_isnoneornil(L, 2) && lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.clipObjects = lua_isnoneornil(L, 1) ? true : lua_toboolean(L, 1) != 0;
    g_cam.clipVehicles = lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2) != 0;
    Diag(g_diagClip, "DIAG setCameraClip: CCamera::SetCameraClip is not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getCameraClip(lua_State* L) {
    lua_pushboolean(L, g_cam.clipObjects ? 1 : 0);
    lua_pushboolean(L, g_cam.clipVehicles ? 1 : 0);
    return 2;
}

int l_setCameraViewMode(lua_State* L) {
    auto readMode = [&](int index, unsigned char& slot, bool& set) {
        if (lua_isnoneornil(L, index)) return true;
        float value = 0.f;
        if (!ReadFloat(L, index, value) || value < 0.f || value > 255.f) return false;
        slot = static_cast<unsigned char>(value);
        set = true;
        return true;
    };
    unsigned char vehicle = g_cam.vehicleView;
    unsigned char ped = g_cam.pedView;
    bool setVehicle = false;
    bool setPed = false;
    if (!readMode(1, vehicle, setVehicle) || !readMode(2, ped, setPed)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.vehicleView = vehicle;
    g_cam.pedView = ped;
    if (setVehicle || setPed)
        Diag(g_diagView, "DIAG setCameraViewMode: CCamera vehicle and ped view modes are not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

int l_setCameraGoggleEffect(lua_State* L) {
    const char* mode = lua_tostring(L, 1);
    if (!mode) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (std::strcmp(mode, "nightvision") != 0 && std::strcmp(mode, "thermalvision") != 0 &&
        std::strcmp(mode, "normal") != 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.goggle = mode;
    Diag(g_diagGoggle, "DIAG setCameraGoggleEffect: night vision and thermal vision are not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

int l_setCameraDrunkLevel(lua_State* L) {
    float value = 0.f;
    if (!ReadFloat(L, 1, value) || value < 0.f || value > 255.f) {
        LUA_LOGE("setCameraDrunkLevel: invalid range (0-255)");
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.drunk = static_cast<unsigned char>(value);
    Diag(g_diagDrunk, "DIAG setCameraDrunkLevel: CPlayerInfo::SetCamDrunkLevel is not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

int l_shakeCamera(lua_State* L) {
    float radius = 0.f;
    if (!ReadFloat(L, 1, radius)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Vec3 at = g_cam.position;
    if (lua_isnumber(L, 2) || lua_isnumber(L, 3) || lua_isnumber(L, 4)) {
        if (!ReadVec3(L, 2, at)) {
            lua_pushboolean(L, 0);
            return 1;
        }
    } else {
        float heading = 0.f;
        Game::CopyPose(at.x, at.y, at.z, heading);
    }
    g_cam.shake = radius;
    (void)at;
    Diag(g_diagShake, "DIAG shakeCamera: CCamera::ShakeCamera is not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

int l_resetShakeCamera(lua_State* L) {
    (void)L;
    g_cam.shake = 0.f;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getCameraAimDirection(lua_State* L) {
    if (!g_cam.aimSet) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, g_cam.aimH);
    lua_pushnumber(L, g_cam.aimV);
    return 2;
}

int l_setCameraAimDirection(lua_State* L) {
    float horizontal = 0.f;
    float vertical = 0.f;
    if (!ReadFloat(L, 1, horizontal) || !ReadFloat(L, 2, vertical)) {
        LUA_LOGE("setCameraAimDirection: expected two numbers");
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.aimH = horizontal;
    g_cam.aimV = vertical;
    g_cam.aimSet = true;
    Diag(g_diagAim, "DIAG setCameraAimDirection: the local aim camera is not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

int l_acquireScriptCamera(lua_State* L) {
    // CClientCamera::AcquireScriptCamera refuses a second lease and a missing resource.
    if (g_cam.lease || ActiveVmOwner().empty()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!lua_isnoneornil(L, 1) && lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.previousFixed = g_cam.fixed;
    g_cam.previousPosition = g_cam.position;
    g_cam.previousLook = g_cam.look;
    g_cam.previousFov = g_cam.fov;
    g_cam.fixed = false;
    Game::ClearScriptCamera();
    g_cam.inhibit = lua_isnoneornil(L, 1) ? true : lua_toboolean(L, 1) != 0;
    if (++g_cam.token == 0) ++g_cam.token;
    g_cam.owner = ActiveVmOwner();
    g_cam.lease = true;
    g_cam.fileLease = false;
    g_cam.fileStarted = false;
    g_cam.moving = false;
    g_cam.tracking = false;
    lua_pushinteger(L, static_cast<lua_Integer>(g_cam.token));
    return 1;
}

int l_releaseScriptCamera(lua_State* L) {
    float token = 0.f;
    if (!ReadFloat(L, 1, token) || !Owned(static_cast<unsigned>(token))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool preserve = false;
    if (!lua_isnoneornil(L, 2)) {
        if (lua_type(L, 2) != LUA_TBOOLEAN) {
            lua_pushboolean(L, 0);
            return 1;
        }
        preserve = lua_toboolean(L, 2) != 0;
    }
    RestoreLease(preserve);
    lua_pushboolean(L, 1);
    return 1;
}

int l_isScriptCameraLeaseActive(lua_State* L) {
    float token = 0.f;
    lua_pushboolean(L, ReadFloat(L, 1, token) && Owned(static_cast<unsigned>(token)) ? 1 : 0);
    return 1;
}

int l_setScriptCameraFixed(lua_State* L) {
    float token = 0.f;
    Vec3 position;
    Vec3 target;
    if (!ReadFloat(L, 1, token) || !Owned(static_cast<unsigned>(token)) || !ReadVec3(L, 2, position) ||
        !ReadVec3(L, 5, target)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.fixed = true;
    g_cam.position = position;
    g_cam.look = target;
    ApplyFixed(position, target);
    Diag(g_diagScriptFixed, "DIAG setScriptCameraFixed: CCamera::SetScriptFixedCamera uses the fixed-mode camera");
    lua_pushboolean(L, 1);
    return 1;
}

int l_moveScriptCamera(lua_State* L) {
    float token = 0.f;
    Vec3 from;
    Vec3 to;
    float duration = 0.f;
    if (!ReadFloat(L, 1, token) || !Owned(static_cast<unsigned>(token)) || !ReadVec3(L, 2, from) ||
        !ReadVec3(L, 5, to) || !ReadFloat(L, 8, duration)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int ms = static_cast<int>(duration);
    if (ms <= 0 || ms > 600000) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.moving = true;
    g_cam.moveFrom = from;
    g_cam.moveTo = to;
    g_cam.moveMs = ms;
    g_cam.moveStart = std::chrono::steady_clock::now();
    g_cam.easeMove = lua_isnoneornil(L, 9) ? true : lua_toboolean(L, 9) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int l_trackScriptCamera(lua_State* L) {
    float token = 0.f;
    Vec3 from;
    Vec3 to;
    float duration = 0.f;
    if (!ReadFloat(L, 1, token) || !Owned(static_cast<unsigned>(token)) || !ReadVec3(L, 2, from) ||
        !ReadVec3(L, 5, to) || !ReadFloat(L, 8, duration)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int ms = static_cast<int>(duration);
    if (ms <= 0 || ms > 600000) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.tracking = true;
    g_cam.trackFrom = from;
    g_cam.trackTo = to;
    g_cam.trackMs = ms;
    g_cam.trackStart = std::chrono::steady_clock::now();
    g_cam.easeTrack = lua_isnoneornil(L, 9) ? true : lua_toboolean(L, 9) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int l_setScriptCameraPersist(lua_State* L) {
    float token = 0.f;
    if (!ReadFloat(L, 1, token) || !Owned(static_cast<unsigned>(token)) || lua_type(L, 2) != LUA_TBOOLEAN ||
        lua_type(L, 3) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.persistPosition = lua_toboolean(L, 2) != 0;
    g_cam.persistTarget = lua_toboolean(L, 3) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int l_resetScriptCamera(lua_State* L) {
    float token = 0.f;
    if (!ReadFloat(L, 1, token) || !Owned(static_cast<unsigned>(token))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.moving = false;
    g_cam.tracking = false;
    g_cam.persistPosition = false;
    g_cam.persistTarget = false;
    g_cam.widescreen = false;
    g_cam.nearClip = false;
    lua_pushboolean(L, 1);
    return 1;
}

int l_fadeScriptCamera(lua_State* L) {
    float token = 0.f;
    float seconds = 0.f;
    if (!ReadFloat(L, 1, token) || !Owned(static_cast<unsigned>(token)) || lua_type(L, 2) != LUA_TBOOLEAN ||
        !ReadFloat(L, 3, seconds) || seconds < 0.f || seconds > 60.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.fading = seconds > 0.f;
    g_cam.fadeIn = lua_toboolean(L, 2) != 0;
    g_cam.fadeSeconds = seconds;
    g_cam.fadeStart = std::chrono::steady_clock::now();
    lua_pushboolean(L, 1);
    return 1;
}

int l_isScriptCameraFading(lua_State* L) {
    float token = 0.f;
    lua_pushboolean(L, ReadFloat(L, 1, token) && Owned(static_cast<unsigned>(token)) && g_cam.fading ? 1 : 0);
    return 1;
}

int l_isScriptCameraMoveRunning(lua_State* L) {
    float token = 0.f;
    lua_pushboolean(L, ReadFloat(L, 1, token) && Owned(static_cast<unsigned>(token)) && g_cam.moving ? 1 : 0);
    return 1;
}

int l_isScriptCameraTrackRunning(lua_State* L) {
    float token = 0.f;
    lua_pushboolean(L, ReadFloat(L, 1, token) && Owned(static_cast<unsigned>(token)) && g_cam.tracking ? 1 : 0);
    return 1;
}

int l_setScriptCameraWidescreen(lua_State* L) {
    float token = 0.f;
    if (!ReadFloat(L, 1, token) || !Owned(static_cast<unsigned>(token)) || lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.widescreen = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int l_setScriptCameraNearClip(lua_State* L) {
    float token = 0.f;
    if (!ReadFloat(L, 1, token) || !Owned(static_cast<unsigned>(token))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (lua_type(L, 2) == LUA_TBOOLEAN) {
        if (lua_toboolean(L, 2) != 0) {
            lua_pushboolean(L, 0);
            return 1;
        }
        g_cam.nearClip = false;
        lua_pushboolean(L, 1);
        return 1;
    }
    float distance = 0.f;
    if (!ReadFloat(L, 2, distance) || distance < 0.01f || distance > 100.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.nearClip = true;
    g_cam.nearClipDistance = distance;
    lua_pushboolean(L, 1);
    return 1;
}

int l_requestFileCutscene(lua_State* L) {
    // CLuaCameraDefs::RequestFileCutscene acquires a lease, then BeginFileCutscene.
    // LoadFileCutscene is not on this phone, so the acquire is released and the
    // call returns false, the same as a DAT the desktop cannot load.
    const char* name = lua_tostring(L, 1);
    if (!name || name[0] == '\0' || ActiveVmOwner().empty() || g_cam.lease) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!lua_isnoneornil(L, 2)) {
        float area = 0.f;
        if (!ReadFloat(L, 2, area) || area < 0.f || area > 255.f) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    Diag(g_diagCutscene, "DIAG requestFileCutscene: CGame::LoadFileCutscene is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int l_isFileCutsceneLeaseActive(lua_State* L) {
    float token = 0.f;
    lua_pushboolean(L, ReadFloat(L, 1, token) && OwnedFile(static_cast<unsigned>(token)) ? 1 : 0);
    return 1;
}

int l_isFileCutsceneLoaded(lua_State* L) {
    float token = 0.f;
    lua_pushboolean(L, ReadFloat(L, 1, token) && OwnedFile(static_cast<unsigned>(token)) ? 1 : 0);
    return 1;
}

int l_startFileCutscene(lua_State* L) {
    float token = 0.f;
    if (!ReadFloat(L, 1, token) || !OwnedFile(static_cast<unsigned>(token)) || g_cam.fileStarted) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Diag(g_diagCutscene, "DIAG startFileCutscene: CGame::StartFileCutscene is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int l_fadeFileCutscene(lua_State* L) {
    float token = 0.f;
    float seconds = 0.f;
    if (!ReadFloat(L, 1, token) || !OwnedFile(static_cast<unsigned>(token)) || lua_type(L, 2) != LUA_TBOOLEAN ||
        !ReadFloat(L, 3, seconds) || seconds < 0.f || seconds > 60.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_cam.fading = seconds > 0.f;
    g_cam.fadeIn = lua_toboolean(L, 2) != 0;
    g_cam.fadeSeconds = seconds;
    g_cam.fadeStart = std::chrono::steady_clock::now();
    lua_pushboolean(L, 1);
    return 1;
}

int l_isFileCutsceneFading(lua_State* L) {
    float token = 0.f;
    lua_pushboolean(L, ReadFloat(L, 1, token) && OwnedFile(static_cast<unsigned>(token)) && g_cam.fading ? 1 : 0);
    return 1;
}

int l_isFileCutsceneFinished(lua_State* L) {
    float token = 0.f;
    // HasFileCutsceneFinished is false until this lease has started playback.
    lua_pushboolean(L, ReadFloat(L, 1, token) && OwnedFile(static_cast<unsigned>(token)) && g_cam.fileStarted ? 1 : 0);
    return 1;
}

int l_isFileCutsceneSkipInputPressed(lua_State* L) {
    float token = 0.f;
    lua_pushboolean(L, ReadFloat(L, 1, token) && OwnedFile(static_cast<unsigned>(token)) && g_cam.fileStarted ? 1 : 0);
    return 1;
}

int l_wasFileCutsceneSkipped(lua_State* L) {
    float token = 0.f;
    lua_pushboolean(L, ReadFloat(L, 1, token) && OwnedFile(static_cast<unsigned>(token)) && g_cam.fileStarted ? 1 : 0);
    return 1;
}

int l_skipFileCutscene(lua_State* L) {
    float token = 0.f;
    if (!ReadFloat(L, 1, token) || !OwnedFile(static_cast<unsigned>(token)) || !g_cam.fileStarted) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Diag(g_diagCutscene, "DIAG skipFileCutscene: CGame::SkipFileCutscene is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int l_releaseFileCutscene(lua_State* L) {
    float token = 0.f;
    if (!ReadFloat(L, 1, token) || !OwnedFile(static_cast<unsigned>(token))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool preserve = false;
    if (!lua_isnoneornil(L, 2) && lua_type(L, 2) == LUA_TBOOLEAN) preserve = lua_toboolean(L, 2) != 0;
    RestoreLease(preserve);
    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

bool CameraElementAlive(uint32_t id) { return id == kCameraId; }

void PulseCamera() {
    const auto now = std::chrono::steady_clock::now();
    if (g_cam.fading) {
        const float elapsed = std::chrono::duration<float>(now - g_cam.fadeStart).count();
        if (elapsed >= g_cam.fadeSeconds) g_cam.fading = false;
    }
    bool moved = false;
    if (g_cam.moving) {
        float t = static_cast<float>(ElapsedMs(g_cam.moveStart)) / static_cast<float>(g_cam.moveMs);
        if (t >= 1.f) {
            t = 1.f;
            g_cam.moving = false;
        }
        t = Ease(t, g_cam.easeMove);
        g_cam.position.x = Lerp(g_cam.moveFrom.x, g_cam.moveTo.x, t);
        g_cam.position.y = Lerp(g_cam.moveFrom.y, g_cam.moveTo.y, t);
        g_cam.position.z = Lerp(g_cam.moveFrom.z, g_cam.moveTo.z, t);
        g_cam.fixed = true;
        moved = true;
    }
    if (g_cam.tracking) {
        float t = static_cast<float>(ElapsedMs(g_cam.trackStart)) / static_cast<float>(g_cam.trackMs);
        if (t >= 1.f) {
            t = 1.f;
            g_cam.tracking = false;
        }
        t = Ease(t, g_cam.easeTrack);
        g_cam.look.x = Lerp(g_cam.trackFrom.x, g_cam.trackTo.x, t);
        g_cam.look.y = Lerp(g_cam.trackFrom.y, g_cam.trackTo.y, t);
        g_cam.look.z = Lerp(g_cam.trackFrom.z, g_cam.trackTo.z, t);
        g_cam.fixed = true;
        moved = true;
    }
    if (moved) ApplyFixed(g_cam.position, g_cam.look);
}

void ReleaseCameraOwner(const std::string& owner) {
    if (g_cam.lease && g_cam.owner == owner) RestoreLease(false);
}

void InstallCameraLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"getCamera", l_getCamera},
        {"getCameraViewMode", l_getCameraViewMode},
        {"getCameraMatrix", l_getCameraMatrix},
        {"getCameraTarget", l_getCameraTarget},
        {"getCameraInterior", l_getCameraInterior},
        {"getCameraGoggleEffect", l_getCameraGoggleEffect},
        {"getCameraFieldOfView", l_getCameraFieldOfView},
        {"getCameraDrunkLevel", l_getCameraDrunkLevel},
        // CLuaCompatibilityDefs.cpp:73. Same function as getCameraDrunkLevel.
        {"getCameraShakeLevel", l_getCameraDrunkLevel},
        {"setCameraMatrix", l_setCameraMatrix},
        {"setCameraFieldOfView", l_setCameraFieldOfView},
        {"setCameraTarget", l_setCameraTarget},
        {"setCameraInterior", l_setCameraInterior},
        {"fadeCamera", l_fadeCamera},
        {"setCameraClip", l_setCameraClip},
        {"getCameraClip", l_getCameraClip},
        {"setCameraViewMode", l_setCameraViewMode},
        {"setCameraGoggleEffect", l_setCameraGoggleEffect},
        {"setCameraDrunkLevel", l_setCameraDrunkLevel},
        // CLuaCompatibilityDefs.cpp:72.
        {"setCameraShakeLevel", l_setCameraDrunkLevel},
        {"shakeCamera", l_shakeCamera},
        {"resetShakeCamera", l_resetShakeCamera},
        {"getCameraAimDirection", l_getCameraAimDirection},
        {"setCameraAimDirection", l_setCameraAimDirection},
        {"acquireScriptCamera", l_acquireScriptCamera},
        {"releaseScriptCamera", l_releaseScriptCamera},
        {"isScriptCameraLeaseActive", l_isScriptCameraLeaseActive},
        {"setScriptCameraFixed", l_setScriptCameraFixed},
        {"moveScriptCamera", l_moveScriptCamera},
        {"trackScriptCamera", l_trackScriptCamera},
        {"setScriptCameraPersist", l_setScriptCameraPersist},
        {"resetScriptCamera", l_resetScriptCamera},
        {"fadeScriptCamera", l_fadeScriptCamera},
        {"isScriptCameraFading", l_isScriptCameraFading},
        {"isScriptCameraMoveRunning", l_isScriptCameraMoveRunning},
        {"isScriptCameraTrackRunning", l_isScriptCameraTrackRunning},
        {"setScriptCameraWidescreen", l_setScriptCameraWidescreen},
        {"setScriptCameraNearClip", l_setScriptCameraNearClip},
        {"requestFileCutscene", l_requestFileCutscene},
        {"isFileCutsceneLeaseActive", l_isFileCutsceneLeaseActive},
        {"isFileCutsceneLoaded", l_isFileCutsceneLoaded},
        {"startFileCutscene", l_startFileCutscene},
        {"fadeFileCutscene", l_fadeFileCutscene},
        {"isFileCutsceneFading", l_isFileCutsceneFading},
        {"isFileCutsceneFinished", l_isFileCutsceneFinished},
        {"isFileCutsceneSkipInputPressed", l_isFileCutsceneSkipInputPressed},
        {"wasFileCutsceneSkipped", l_wasFileCutsceneSkipped},
        {"skipFileCutscene", l_skipFileCutscene},
        {"releaseFileCutscene", l_releaseFileCutscene},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
