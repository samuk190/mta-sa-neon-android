#include "LuaApiExtra.h"

#include "LuaJsonApi.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <cstdio>
#include <cstring>
#include <string>

// Chat line sink (jni/JNI_OnLoad.cpp on Android, stubs/jni_bridge_stub.cpp in the headless build).
namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text);
}

namespace WorldRPG::Script {

namespace {

// Registry key of the table {[elementId] = {[key] = value}} written by setElementData.
constexpr const char* kElementDataKey = "worldrpg.elementData";

// Fixed results for natives the phone cannot answer yet (no engine hook). Each
// kind is the value the desktop native gives when it has nothing to report.
enum DefaultKind : int {
    kTrue,
    kFalse,
    kNil,
    kZero,
    kEmptyString,
    kEmptyTable,
    kZero3,
    kAlpha255,
};

int DefaultValue(lua_State* L) {
    switch (static_cast<DefaultKind>(lua_tointeger(L, lua_upvalueindex(1)))) {
        case kTrue: lua_pushboolean(L, 1); return 1;
        case kFalse: lua_pushboolean(L, 0); return 1;
        case kNil: lua_pushnil(L); return 1;
        case kZero: lua_pushinteger(L, 0); return 1;
        case kEmptyString: lua_pushstring(L, ""); return 1;
        case kEmptyTable: lua_newtable(L); return 1;
        case kZero3:
            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
            return 3;
        case kAlpha255: lua_pushinteger(L, 255); return 1;
    }
    return 0;
}

// Sets a global to a default native only when no core binding defines it.
void SetDefault(lua_State* L, const char* name, DefaultKind kind) {
    lua_getglobal(L, name);
    const bool defined = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (defined) return;
    lua_pushinteger(L, kind);
    lua_pushcclosure(L, DefaultValue, 1);
    lua_setglobal(L, name);
}

void SetGlobal(lua_State* L, const char* name, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    lua_setglobal(L, name);
}

int ClampColor(lua_Integer value) {
    return static_cast<int>(value < 0 ? 0 : (value > 255 ? 255 : value));
}

// outputChatBox(text, r, g, b, colorCoded): the same line the server chat uses,
// "#RRGGBB" followed by the text (GameActivity kind 0).
int OutputChatBox(lua_State* L) {
    size_t length = 0;
    const char* text = luaL_optlstring(L, 1, "", &length);
    char prefix[8];
    std::snprintf(prefix, sizeof(prefix), "#%02X%02X%02X", ClampColor(luaL_optinteger(L, 2, 255)),
                  ClampColor(luaL_optinteger(L, 3, 255)), ClampColor(luaL_optinteger(L, 4, 255)));
    const std::string line = std::string(prefix) + std::string(text, length);
    WorldRPG::JniBridge::PostGameLine(0, line.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

// iprint(...) and debugMessage(text, level, r, g, b): console output only.
int Iprint(lua_State* L) {
    std::string message;
    const int top = lua_gettop(L);
    for (int i = 1; i <= top; ++i) {
        size_t length = 0;
        const char* part = luaL_tolstring(L, i, &length);
        if (i > 1) message += "\t";
        message.append(part, length);
        lua_pop(L, 1);
    }
    LUA_LOGI("Lua: %s", message.c_str());
    return 0;
}

int DebugMessage(lua_State* L) {
    LUA_LOGI("Lua debug: %s", luaL_optstring(L, 1, ""));
    lua_pushboolean(L, 1);
    return 1;
}

// getElementData(element, key): values set with setElementData first, then server data.
int GetElementData(lua_State* L) {
    uint32_t id = WorldRPG::ServerWorld::LocalPlayer();
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "id");
        if (lua_isinteger(L, -1)) id = static_cast<uint32_t>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }
    const char* key = luaL_checkstring(L, 2);
    lua_settop(L, 2);
    lua_getfield(L, LUA_REGISTRYINDEX, kElementDataKey);
    if (lua_istable(L, -1)) {
        lua_pushinteger(L, static_cast<lua_Integer>(id));
        lua_rawget(L, -2);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, key);
            if (!lua_isnil(L, -1)) return 1;
        }
    }
    lua_settop(L, 2);
    double number = 0;
    std::string text;
    bool isNumber = false;
    if (!WorldRPG::ServerWorld::GetData(id, key, number, text, isNumber)) {
        lua_pushnil(L);
        return 1;
    }
    if (isNumber) lua_pushnumber(L, number);
    else lua_pushlstring(L, text.data(), text.size());
    return 1;
}

// setElementData(element, key, value): client-side value, kept for the session.
int SetElementData(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (id == 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_settop(L, 3);
    lua_getfield(L, LUA_REGISTRYINDEX, kElementDataKey);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kElementDataKey);
    }
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    lua_rawget(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(id));
        lua_pushvalue(L, -2);
        lua_rawset(L, -4);
    }
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, key);
    lua_pushboolean(L, 1);
    return 1;
}

// Vector3(x, y, z): the desktop vector class reduced to its x, y, z fields.
int Vector3New(lua_State* L) {
    lua_newtable(L);
    lua_pushnumber(L, luaL_optnumber(L, 1, 0.0));
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, luaL_optnumber(L, 2, 0.0));
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, luaL_optnumber(L, 3, 0.0));
    lua_setfield(L, -2, "z");
    return 1;
}

// getCameraMatrix(): position, look-at, roll, fov. The phone does not expose the
// camera yet, so this is the default camera (no hook).
int GetCameraMatrix(lua_State* L) {
    const double values[] = {0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 70.0};
    for (double value : values) lua_pushnumber(L, value);
    return 8;
}

// dxGetTextWidth(text, scale, font) and dxGetFontHeight(scale, font): rough metrics
// until the phone's font metrics are wired (P4).
int DxGetTextWidth(lua_State* L) {
    size_t length = 0;
    luaL_optlstring(L, 1, "", &length);
    lua_pushnumber(L, static_cast<lua_Number>(length) * 7.0 * luaL_optnumber(L, 2, 1.0));
    return 1;
}

int DxGetFontHeight(lua_State* L) {
    lua_pushnumber(L, 14.0 * luaL_optnumber(L, 1, 1.0));
    return 1;
}

// setElementCollisionsEnabled and similar setters need no reply, but scripts expect a boolean.
int TrueResult(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

// createVehicle / createMarker / createObject: no client-side creation on the phone.
// Returns false, as the desktop does when the element cannot be created.
int FalseResult(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

// getAmbientPedPopulationProfile(): the desktop native reads the engine's density,
// zone and creation-distance values. The phone has no hook for them, so it reports
// the desktop failure shape (false, reason) instead of an invented profile.
int GetAmbientPedPopulationProfile(lua_State* L) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "engine-hook-missing");
    return 2;
}

// fileOpen(path): resource files are not readable from Lua yet.
int FileOpen(lua_State* L) {
    lua_pushnil(L);
    return 1;
}

} // namespace

void PushElement(lua_State* L, uint32_t id) {
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    lua_setfield(L, -2, "id");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "element");
}

uint32_t ElementIdOf(lua_State* L, int index) {
    if (!lua_istable(L, index)) return 0;
    lua_getfield(L, index, "id");
    const uint32_t id = lua_isinteger(L, -1) ? static_cast<uint32_t>(lua_tointeger(L, -1)) : 0;
    lua_pop(L, 1);
    return id;
}

void InstallExtraBindings(lua_State* L) {
    InstallJsonBindings(L);
    SetGlobal(L, "outputChatBox", OutputChatBox);
    SetGlobal(L, "iprint", Iprint);
    SetGlobal(L, "debugMessage", DebugMessage);
    SetGlobal(L, "getElementData", GetElementData);
    SetGlobal(L, "setElementData", SetElementData);
    SetGlobal(L, "Vector3", Vector3New);
    SetGlobal(L, "getCameraMatrix", GetCameraMatrix);
    SetGlobal(L, "dxGetTextWidth", DxGetTextWidth);
    SetGlobal(L, "dxGetFontHeight", DxGetFontHeight);
    SetGlobal(L, "fileOpen", FileOpen);
    SetGlobal(L, "getAmbientPedPopulationProfile", GetAmbientPedPopulationProfile);
    SetGlobal(L, "createVehicle", FalseResult);
    SetGlobal(L, "createMarker", FalseResult);
    SetGlobal(L, "createColSphere", FalseResult);
    SetGlobal(L, "createObject", FalseResult);
    SetGlobal(L, "createBlip", FalseResult);
    SetGlobal(L, "createPed", FalseResult);
    SetGlobal(L, "setElementPosition", TrueResult);
    SetGlobal(L, "setElementHealth", TrueResult);
    SetGlobal(L, "destroyElement", TrueResult);
    SetGlobal(L, "setPedAnimation", TrueResult);

    // Defaults for natives with no engine hook yet. Same values as the existing
    // ped-traffic stubs in LuaVMStub.cpp; the core bindings take precedence.
    const struct { const char* name; DefaultKind kind; } defaults[] = {
        {"setElementVelocity", kTrue}, {"setElementRotation", kTrue}, {"setElementAngularVelocity", kTrue},
        {"setElementFrozen", kTrue}, {"setElementCollisionsEnabled", kTrue}, {"getElementCollisionsEnabled", kTrue},
        {"getElementAlpha", kAlpha255}, {"getElementAngularVelocity", kZero3},
        {"getElementDistanceFromCentreOfMassToBaseOfModel", kZero}, {"getElementMatrix", kNil},
        {"getVehicleController", kFalse}, {"getVehicleOccupant", kFalse}, {"getVehicleOccupants", kEmptyTable},
        {"getVehicleStraightLineDistance", kZero}, {"getVehicleEngineState", kFalse}, {"getVehicleOverrideLights", kZero},
        {"isVehicleLocked", kFalse}, {"isVehicleBlown", kFalse}, {"isVehicleOnGround", kFalse},
        {"isVehicleRecordingLoaded", kFalse}, {"isVehiclePlaybackActive", kFalse}, {"requestVehicleRecording", kFalse},
        {"startVehiclePlayback", kFalse}, {"stopVehiclePlayback", kTrue}, {"setVehiclePlaybackSpeed", kTrue},
        {"setVehicleLoadCollisionFlag", kTrue}, {"setVehicleStraightLineDistance", kTrue},
        {"setVehicleTyresCanBurst", kTrue}, {"setVehicleOverrideLights", kTrue}, {"setVehicleEngineState", kTrue},
        {"setVehicleColor", kTrue},
        {"isPedInVehicle", kFalse}, {"isPedDoingTask", kFalse}, {"isPedMissionActor", kFalse},
        {"setPedMissionActor", kTrue}, {"getPedSimplestTask", kEmptyString}, {"getPedTaskSequenceProgress", kZero},
        {"getPedPhysicalProofs", kFalse}, {"getPedSuffersCriticalHits", kFalse}, {"canPedBeKnockedOffBike", kFalse},
        {"canPedBeDraggedOut", kFalse}, {"isPedOnlyDamagedByPlayer", kFalse}, {"isPedNeverTargeted", kFalse},
        {"isPedDoingGangDriveby", kFalse}, {"getPedNativeVehicleResponse", kNil}, {"getPedCameraRotation", kZero},
        {"getPedBonePosition", kZero3}, {"getPedNativeGroupDiagnostic", kFalse},
        {"setPedPhysicalProofs", kTrue}, {"setPedCanBeKnockedOffBike", kTrue}, {"setPedSuffersCriticalHits", kTrue},
        {"setPedOnlyDamagedByPlayer", kTrue}, {"setPedNeverTargeted", kTrue}, {"setPedDriveBy", kTrue},
        {"setPedDriveMission", kTrue}, {"setPedKillOnFoot", kTrue}, {"setPedDriveTo", kTrue}, {"setPedGoTo", kTrue},
        {"setPedTurnToFace", kTrue}, {"setPedAimAt", kTrue}, {"setPedHandsUp", kTrue}, {"setPedEnterVehicle", kTrue},
        {"setPedExitVehicle", kTrue}, {"setPedTaskSequence", kTrue}, {"setPedCanBeDraggedOut", kTrue},
        {"restorePedNativeVehicleResponse", kTrue}, {"releasePedNativePointArm", kTrue},
        {"acquirePedNativePointArm", kFalse}, {"isLineOfSightClear", kFalse}, {"getWaterLevel", kNil},
        {"getScreenFromWorldPosition", kNil}, {"isControlEnabled", kTrue}, {"toggleControl", kTrue},
        {"isConsoleActive", kFalse}, {"isMainMenuActive", kFalse}, {"showCursor", kTrue}, {"showChat", kTrue},
        {"executeCommandHandler", kFalse}, {"attachElements", kTrue}, {"detachElements", kTrue},
        {"setObjectMaterial", kTrue},
        {"playSoundFrontEnd", kFalse}, {"playSound3D", kFalse}, {"playSound", kFalse}, {"stopSound", kTrue},
        {"setSoundVolume", kTrue}, {"setSoundMaxDistance", kTrue}, {"setSoundMinDistance", kTrue},
        {"setSoundPosition", kTrue}, {"setSoundDopplerEnabled", kTrue},
        {"setCameraTarget", kTrue}, {"setCameraInterior", kTrue}, {"setCameraFieldOfView", kTrue},
        {"fadeCamera", kTrue}, {"getPedCameraRotation", kZero},
        {"guiSetText", kTrue}, {"guiGetText", kEmptyString}, {"guiCreateButton", kZero},
        {"dxDrawImage", kTrue}, {"dxDrawCircle", kTrue}, {"dxDrawPrimitive", kTrue},
        {"engineLoadIMG", kNil}, {"engineAddImage", kNil}, {"engineLoadCOL", kNil}, {"engineParseSAMPMap", kNil},
        {"engineFreeTXD", kTrue}, {"engineFreeModel", kTrue}, {"engineImageLinkTXD", kTrue},
        {"engineImageLinkDFF", kTrue}, {"engineSetModelTXDID", kTrue}, {"engineReplaceCOL", kTrue},
        {"engineSetModelLODDistance", kTrue}, {"engineSetModelFlags", kTrue}, {"engineRequestTXD", kZero},
        {"engineGetTXDIDFromName", kZero}, {"engineRequestModel", kZero},
        {"restoreWorldModel", kTrue}, {"removeWorldModel", kTrue},
        {"fileRead", kEmptyString}, {"fileGetSize", kZero}, {"fileClose", kTrue},
        {"wasEventCancelled", kFalse}, {"updatePedNativePointArm", kFalse},
        {"fxAddBlood", kTrue},
    };
    for (const auto& entry : defaults) SetDefault(L, entry.name, entry.kind);
}

} // namespace WorldRPG::Script
