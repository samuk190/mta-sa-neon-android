// Stock model names and the CBaseModelInfo fields the phone can read.
// DFF/TXD/COL replacement is not in this file.

#include "LuaEngineApi.h"

#include "LuaVMStub.h"
#include "ObjectGroups.h"
#include "SurfaceInfo.h"
#include "Streaming.h"
#include "EngineWorld.h"
#include "EngineRenderer.h"
#include "CullZones.h"
#include "VehicleAudio.h"
#include "BreakEffects.h"
#include "EngineAssets.h"
#include "ModelInfoRead.h"
#include "ModelNames.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>

namespace WorldRPG::Script {
namespace {

constexpr double kServerModelIdMax = 65534.0;
constexpr std::uint32_t kServerModelMin = 42341;

const char* FindServerModelName(std::uint16_t) { return nullptr; }

bool FindServerModelId(const std::string&, std::uint16_t&) { return false; }

bool ReadBoundedModelId(lua_State* L, int& modelId, const char*& error) {
    const int type = lua_type(L, 1);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        error = "Expected number at argument 1";
        return false;
    }
    if (!lua_isnumber(L, 1)) {
        error = "Expected number, got non-convertible string";
        return false;
    }
    const lua_Number value = lua_tonumber(L, 1);
    if (std::isnan(value)) {
        error = "Expected number, got NaN";
        return false;
    }
    if (!std::isfinite(value) || value < 0.0 || value > kServerModelIdMax) {
        error = "Expected number within the supported range";
        return false;
    }
    modelId = static_cast<int>(value);
    return true;
}

// CScriptArgReader::ReadString (CScriptArgReader.h:607-647). A number is accepted,
// then lua_strlen (lua_objlen) is 0, so the copied text is empty.
bool ReadModelName(lua_State* L, std::string& name, const char*& error) {
    const int type = lua_type(L, 1);
    if (type == LUA_TNUMBER) {
        name.clear();
        return true;
    }
    if (type != LUA_TSTRING) {
        error = "Expected string at argument 1";
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text) {
        error = "Expected string at argument 1";
        return false;
    }
    name.assign(text, length);
    return true;
}

int Fail(lua_State* L, const char* function, const char* error) {
    LUA_LOGE("%s: %s", function, error);
    lua_pushboolean(L, 0);
    return 1;
}

void NoteModelInfoGap() {
    static std::once_flag once;
    std::call_once(once, [] {
        LUA_LOGI("DIAG engine model info: CBaseModelInfo is not readable on this process");
    });
}

// CLuaEngineDefs::EngineGetModelNameFromID (CLuaEngineDefs.cpp:2192-2225).
int EngineGetModelNameFromID(lua_State* L) {
    int modelId = 0;
    const char* error = nullptr;
    if (!ReadBoundedModelId(L, modelId, error)) return Fail(L, "engineGetModelNameFromID", error);

    if (modelId >= 0 && modelId <= static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
        if (const char* serverName = FindServerModelName(static_cast<std::uint16_t>(modelId))) {
            lua_pushstring(L, serverName);
            return 1;
        }
    }

    const char* name = WorldRPG::Models::ModelNameFromId(static_cast<std::uint32_t>(modelId));
    if (name != nullptr && name[0] != '\0') {
        lua_pushstring(L, name);
        return 1;
    }
    return Fail(L, "engineGetModelNameFromID", "Expected valid model ID at argument 1");
}

// CLuaEngineDefs::EngineGetModelIDFromName (CLuaEngineDefs.cpp:2228-2270).
int EngineGetModelIDFromName(lua_State* L) {
    std::string name;
    const char* error = nullptr;
    if (!ReadModelName(L, name, error)) return Fail(L, "engineGetModelIDFromName", error);

    std::uint16_t logicalId = 0;
    if (FindServerModelId(name, logicalId)) {
        lua_pushnumber(L, static_cast<lua_Number>(logicalId));
        return 1;
    }
    if (name.find(':') == std::string::npos && ActiveVmOwnerNet() != 0) {
        const std::string qualified = ActiveVmOwner() + ":" + name;
        if (FindServerModelId(qualified, logicalId)) {
            lua_pushnumber(L, static_cast<lua_Number>(logicalId));
            return 1;
        }
    }

    const std::uint32_t modelId = WorldRPG::Models::ModelIdFromName(name);
    if (modelId != WorldRPG::Models::kInvalidModelId) {
        lua_pushnumber(L, static_cast<lua_Number>(modelId));
        return 1;
    }
    return Fail(L, "engineGetModelIDFromName", "Expected valid model name at argument 1");
}

bool ParseDecimal(std::string_view text, std::uint32_t& value) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool BeginsWithTxd(std::string_view text) {
    return text.size() >= 3 && (text[0] == 't' || text[0] == 'T') && (text[1] == 'x' || text[1] == 'X') &&
           (text[2] == 'd' || text[2] == 'D');
}

// CModelNames::ResolveModelID (CModelNames.cpp:29326-29343) then
// CClientModelManager::ResolveModelID for a client with no server models
// (CClientModelManager.cpp:326-368). Reserved native-world ids are not installed.
bool ResolveRuntime(std::uint32_t supplied, std::uint16_t& runtime) {
    if (supplied > std::numeric_limits<std::uint16_t>::max()) return false;
    if (supplied >= kServerModelMin) return false;
    runtime = static_cast<std::uint16_t>(supplied);
    return true;
}

bool ResolveModelText(const std::string& text, std::uint16_t& runtime) {
    std::uint32_t parsed = 0;
    if (ParseDecimal(text, parsed)) return ResolveRuntime(parsed, runtime);
    if (BeginsWithTxd(text)) {
        std::uint32_t txdId = 0;
        if (ParseDecimal(std::string_view(text).substr(3), txdId) && txdId != 0 && txdId < Models::kStockTxdCount)
            return ResolveRuntime(Models::kStockTxdBase + txdId, runtime);
    }
    return ResolveRuntime(Models::ModelIdFromName(text), runtime);
}

std::string Describe(lua_State* L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TNONE:
            return "none";
        case LUA_TNIL:
            return "nil";
        case LUA_TBOOLEAN:
            return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
        case LUA_TNUMBER: {
            const char* text = lua_tostring(L, index);
            return std::string("number (") + (text ? text : "") + ")";
        }
        case LUA_TSTRING: {
            size_t length = 0;
            const char* text = lua_tolstring(L, index, &length);
            std::string value(text ? text : "", text ? length : 0);
            bool printable = true;
            for (unsigned char ch : value) {
                if (ch < 32 || ch > 126) printable = false;
            }
            if (!printable) return "string";
            if (value.size() > 10) {
                value.resize(10);
                value[7] = '.';
                value[8] = '.';
                value[9] = '.';
            }
            return "string (\"" + value + "\")";
        }
        case LUA_TTABLE:
            return "table";
        case LUA_TFUNCTION:
            return "function";
        case LUA_TTHREAD:
            return "coroutine";
        default:
            return lua_typename(L, lua_type(L, index));
    }
}

std::string BadArgument(const char* function, const char* expected, int index, const std::string& got) {
    return std::string("Bad argument @ '") + function + "' [Expected " + expected + " at argument " +
           std::to_string(index) + ", got " + got + "]";
}

// ArgumentParser numeric pop (CLuaFunctionParser.h:236-237, 436-450).
bool ReadParserUint(lua_State* L, int index, const char* function, std::uint32_t& out, std::string& error) {
    if (!lua_isnumber(L, index)) {
        error = BadArgument(function, "number", index, Describe(L, index));
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        error = BadArgument(function, "number", index, "NaN");
        return false;
    }
    if (std::isinf(number)) {
        error = BadArgument(function, "number", index, "inf");
        return false;
    }
    const lua_Number upper = std::ldexp(1.0, std::numeric_limits<std::uint32_t>::digits);
    if (number < 0.0 || number >= upper) {
        error = BadArgument(function, "number within the destination type range", index, "out-of-range number");
        return false;
    }
    out = static_cast<std::uint32_t>(number);
    return true;
}

bool ReadParserFlag(lua_State* L, const char* function, Models::IdeFlag& flag, std::string& error) {
    if (lua_type(L, 2) != LUA_TSTRING) {
        error = BadArgument(function, "enum", 2, Describe(L, 2));
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, 2, &length);
    const std::string value(text ? text : "", text ? length : 0);
    if (!Models::IdeFlagFromName(value, flag)) {
        error = BadArgument(function, "model-ide-flag", 2, Describe(L, 2));
        return false;
    }
    return true;
}

bool LoadModel(std::uint16_t runtime, Models::StockModelView& view) {
    const Models::LiveModelStatus status = Models::ReadLiveStockModel(runtime, view);
    if (status == Models::LiveModelStatus::Ok) return true;
    if (status == Models::LiveModelStatus::Unavailable) NoteModelInfoGap();
    return false;
}

// CLuaEngineDefs.cpp:1395-1496. No server model is registered, so a parsed id
// has no runtime, server id, parent, or type. A bad argument is logged.
int EngineModelIdentityMiss(lua_State* L, const char* function) {
    int modelId = 0;
    const char* error = nullptr;
    if (!ReadBoundedModelId(L, modelId, error)) return Fail(L, function, error);
    (void)modelId;
    lua_pushboolean(L, 0);
    return 1;
}

bool ReadEngineText(lua_State* L, int index, const char* function, std::string& out, std::string& error) {
    const int type = lua_type(L, index);
    if (type != LUA_TSTRING && type != LUA_TNUMBER) {
        error = BadArgument(function, "string", index, Describe(L, index));
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    if (!text) {
        error = BadArgument(function, "string", index, Describe(L, index));
        return false;
    }
    out.assign(text, length);
    return true;
}

bool ReadSignedByte(lua_State* L, int index, const char* function, int& out, std::string& error) {
    if (!lua_isnumber(L, index)) {
        error = BadArgument(function, "number", index, Describe(L, index));
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        error = BadArgument(function, "number", index, "NaN");
        return false;
    }
    if (std::isinf(number) || number < -128.0 || number >= 128.0) {
        error = BadArgument(function, "number within the destination type range", index, "out-of-range number");
        return false;
    }
    out = static_cast<int>(static_cast<std::int8_t>(number));
    return true;
}

// CLuaEngineDefs::EngineGetModelVisibleTime / Set (CLuaEngineDefs.cpp:2348-2383).
int EngineGetModelVisibleTime(lua_State* L) {
    std::string text;
    std::string error;
    if (!ReadEngineText(L, 1, "engineGetModelVisibleTime", text, error)) return luaL_error(L, "%s", error.c_str());
    std::uint16_t runtime = 0;
    if (!ResolveModelText(text, runtime)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int on = 0;
    int off = 24;
    const Models::TimeQuery query = Models::ReadLiveVisibleTime(runtime, on, off);
    if (query == Models::TimeQuery::Missing) {
        NoteModelInfoGap();
        lua_pushboolean(L, 0);
        return 1;
    }
    if (query == Models::TimeQuery::Incompatible) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 24);
        return 2;
    }
    lua_pushnumber(L, on);
    lua_pushnumber(L, off);
    return 2;
}

int EngineSetModelVisibleTime(lua_State* L) {
    std::string text;
    std::string error;
    if (!ReadEngineText(L, 1, "engineSetModelVisibleTime", text, error)) return luaL_error(L, "%s", error.c_str());
    int on = 0;
    int off = 0;
    if (!ReadSignedByte(L, 2, "engineSetModelVisibleTime", on, error)) return luaL_error(L, "%s", error.c_str());
    if (!ReadSignedByte(L, 3, "engineSetModelVisibleTime", off, error)) return luaL_error(L, "%s", error.c_str());
    std::uint16_t runtime = 0;
    if (!ResolveModelText(text, runtime) || !Models::HoursInRange(on, off)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!Models::WriteLiveVisibleTime(runtime, on, off)) {
        NoteModelInfoGap();
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int EngineGetModelRuntimeID(lua_State* L) { return EngineModelIdentityMiss(L, "engineGetModelRuntimeID"); }
int EngineGetModelServerID(lua_State* L) { return EngineModelIdentityMiss(L, "engineGetModelServerID"); }
int EngineGetModelParent(lua_State* L) { return EngineModelIdentityMiss(L, "engineGetModelParent"); }
int EngineGetModelType(lua_State* L) { return EngineModelIdentityMiss(L, "engineGetModelType"); }

// CLuaEngineDefs::EngineGetModelLODDistance (CLuaEngineDefs.cpp:1570-1599).
int EngineGetModelLODDistance(lua_State* L) {
    std::string text;
    const char* error = nullptr;
    if (!ReadModelName(L, text, error)) return Fail(L, "engineGetModelLODDistance", error);

    std::uint16_t runtime = 0;
    if (ResolveModelText(text, runtime) && runtime < Models::kStockModelCount) {
        Models::StockModelView view;
        if (LoadModel(runtime, view)) {
            lua_pushnumber(L, static_cast<lua_Number>(view.lod));
            return 1;
        }
    } else {
        const std::string message = "Expected a valid model name or ID in range [0-" +
                                    std::to_string(Models::kStockTxdBase) + "] at argument 1, got \"" + text + "\"";
        return Fail(L, "engineGetModelLODDistance", message.c_str());
    }
    lua_pushboolean(L, 0);
    return 1;
}

int RejectModel(lua_State* L, bool ranged) {
    if (!ranged) return luaL_error(L, "Expected a valid model ID at argument 1");
    return luaL_error(L, "Expected a valid model ID in range [0-%d] at argument 1",
                      static_cast<int>(Models::kStockModelCount - 1));
}

// CLuaEngineDefs::EngineGetModelFlags (CLuaEngineDefs.cpp:3399-3411).
int EngineGetModelFlags(lua_State* L) {
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineGetModelFlags", supplied, error)) return luaL_error(L, "%s", error.c_str());
    std::uint16_t runtime = 0;
    if (!ResolveRuntime(supplied, runtime)) return RejectModel(L, false);
    if (runtime >= Models::kStockModelCount) return RejectModel(L, true);
    Models::StockModelView view;
    if (!LoadModel(runtime, view)) return RejectModel(L, true);
    lua_pushnumber(L, static_cast<lua_Number>(view.flags));
    return 1;
}

// CLuaEngineDefs::EngineGetModelFlag (CLuaEngineDefs.cpp:3414-3426).
int EngineGetModelFlag(lua_State* L) {
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineGetModelFlag", supplied, error)) return luaL_error(L, "%s", error.c_str());
    Models::IdeFlag flag = Models::IdeFlag::IsRoad;
    if (!ReadParserFlag(L, "engineGetModelFlag", flag, error)) return luaL_error(L, "%s", error.c_str());
    std::uint16_t runtime = 0;
    if (!ResolveRuntime(supplied, runtime)) return RejectModel(L, false);
    if (runtime >= Models::kStockModelCount) return RejectModel(L, true);
    Models::StockModelView view;
    if (!LoadModel(runtime, view)) return RejectModel(L, true);
    lua_pushboolean(L, Models::ReadIdeFlag(view, flag) ? 1 : 0);
    return 1;
}

// CScriptArgReader::ReadNumber for a float (CScriptArgReader.h:56-91).
bool ReadLuaFloat(lua_State* L, int index, float& out, const char*& error) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        error = "Expected number at argument 2";
        return false;
    }
    if (!lua_isnumber(L, index)) {
        error = "Expected number, got non-convertible string";
        return false;
    }
    const lua_Number value = lua_tonumber(L, index);
    if (std::isnan(value)) {
        error = "Expected number, got NaN";
        return false;
    }
    out = static_cast<float>(value);
    return true;
}

// CScriptArgReader::ReadBool with a default (CScriptArgReader.h:584-600).
bool ReadLuaBoolDefault(lua_State* L, int index, bool fallback, bool& out, const char*& error) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        out = fallback;
        return true;
    }
    if (type != LUA_TBOOLEAN) {
        error = "Expected boolean at argument 3";
        return false;
    }
    out = lua_toboolean(L, index) != 0;
    return true;
}

// CLuaEngineDefs::EngineSetModelLODDistance (CLuaEngineDefs.cpp:1602-1635).
int EngineSetModelLODDistance(lua_State* L) {
    std::string text;
    const char* error = nullptr;
    if (!ReadModelName(L, text, error)) return Fail(L, "engineSetModelLODDistance", error);
    float distance = 0.f;
    if (!ReadLuaFloat(L, 2, distance, error)) return Fail(L, "engineSetModelLODDistance", error);
    bool extended = false;
    if (!ReadLuaBoolDefault(L, 3, false, extended, error)) return Fail(L, "engineSetModelLODDistance", error);

    std::uint16_t runtime = 0;
    const bool resolved = ResolveModelText(text, runtime) && runtime < Models::kStockModelCount;
    if (!resolved) {
        const std::string message = "Expected a valid model name or ID in range [0-" +
                                    std::to_string(Models::kStockModelCount - 1) + "] at argument 1, got \"" + text +
                                    "\"";
        return Fail(L, "engineSetModelLODDistance", message.c_str());
    }
    if (!(distance > 0.f) || !Models::WriteLiveLodDistance(runtime, distance, extended)) {
        if (!(distance > 0.f)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        NoteModelInfoGap();
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaEngineDefs::EngineResetModelLODDistance (CLuaEngineDefs.cpp:1638-1661).
// A bad argument is a Lua error. An unknown model or a distance that was never
// changed returns false.
int EngineResetModelLODDistance(lua_State* L) {
    std::string text;
    const char* error = nullptr;
    if (!ReadModelName(L, text, error)) {
        const std::string message = BadArgument("engineResetModelLODDistance", "string", 1, Describe(L, 1));
        return luaL_error(L, "%s", message.c_str());
    }
    std::uint16_t runtime = 0;
    if (!ResolveModelText(text, runtime) || runtime >= Models::kStockModelCount ||
        !Models::ResetLiveLodDistance(runtime)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaEngineDefs::EngineSetModelFlags (CLuaEngineDefs.cpp:3429-3448).
int EngineSetModelFlags(lua_State* L) {
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineSetModelFlags", supplied, error)) return luaL_error(L, "%s", error.c_str());
    std::uint32_t flags = 0;
    if (!ReadParserUint(L, 2, "engineSetModelFlags", flags, error)) return luaL_error(L, "%s", error.c_str());
    bool ide = false;
    const int third = lua_type(L, 3);
    if (third != LUA_TNONE && third != LUA_TNIL) {
        if (third != LUA_TBOOLEAN) return luaL_error(L, "%s", BadArgument("engineSetModelFlags", "boolean", 3, Describe(L, 3)).c_str());
        ide = lua_toboolean(L, 3) != 0;
    }
    std::uint16_t runtime = 0;
    if (!ResolveRuntime(supplied, runtime)) return RejectModel(L, false);
    if (runtime >= Models::kStockModelCount) return RejectModel(L, true);
    const bool wrote = ide ? Models::WriteLiveIdeFlags(runtime, flags)
                           : Models::WriteLiveRawFlags(runtime, static_cast<std::uint16_t>(flags));
    if (!wrote) {
        NoteModelInfoGap();
        return RejectModel(L, true);
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaEngineDefs::EngineSetModelFlag (CLuaEngineDefs.cpp:3451-3467).
int EngineSetModelFlag(lua_State* L) {
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineSetModelFlag", supplied, error)) return luaL_error(L, "%s", error.c_str());
    Models::IdeFlag flag = Models::IdeFlag::IsRoad;
    if (!ReadParserFlag(L, "engineSetModelFlag", flag, error)) return luaL_error(L, "%s", error.c_str());
    if (lua_type(L, 3) != LUA_TBOOLEAN)
        return luaL_error(L, "%s", BadArgument("engineSetModelFlag", "boolean", 3, Describe(L, 3)).c_str());
    const bool state = lua_toboolean(L, 3) != 0;
    std::uint16_t runtime = 0;
    if (!ResolveRuntime(supplied, runtime)) return RejectModel(L, false);
    if (runtime >= Models::kStockModelCount) return RejectModel(L, true);
    if (!Models::WriteLiveIdeFlag(runtime, flag, state)) {
        NoteModelInfoGap();
        return RejectModel(L, true);
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaEngineDefs::EngineResetModelFlags (CLuaEngineDefs.cpp:3470-3490).
int EngineResetModelFlags(lua_State* L) {
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineResetModelFlags", supplied, error)) return luaL_error(L, "%s", error.c_str());
    std::uint16_t runtime = 0;
    if (!ResolveRuntime(supplied, runtime)) return RejectModel(L, false);
    if (runtime >= Models::kStockModelCount) return RejectModel(L, true);
    const Models::FlagReset reset = Models::ResetLiveFlags(runtime);
    if (reset == Models::FlagReset::Missing) {
        NoteModelInfoGap();
        return RejectModel(L, true);
    }
    lua_pushboolean(L, reset == Models::FlagReset::Changed ? 1 : 0);
    return 1;
}

// CLuaEngineDefs::EngineGetTXDIDFromName (CLuaEngineDefs.cpp:2144-2156).
int EngineGetTXDIDFromName(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING && lua_type(L, 1) != LUA_TNUMBER)
        return luaL_error(L, "%s", BadArgument("engineGetTXDIDFromName", "string", 1, Describe(L, 1)).c_str());
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text || length == 0 || length > 24)
        return luaL_error(L, "Expected a TXD name between 1 and 24 characters at argument 1");
    const std::string name(text, length);
    const int slot = Models::FindLiveTxdSlot(name.c_str());
    if (slot < 0) {
        if (slot == -2) NoteModelInfoGap();
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(slot));
    return 1;
}

// CLuaEngineDefs::EngineSetModelTXDID (CLuaEngineDefs.cpp:2159-2173).
int EngineSetModelTXDID(lua_State* L) {
    std::uint32_t supplied = 0;
    std::uint32_t txd = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineSetModelTXDID", supplied, error)) return luaL_error(L, "%s", error.c_str());
    if (!ReadParserUint(L, 2, "engineSetModelTXDID", txd, error)) return luaL_error(L, "%s", error.c_str());
    std::uint16_t runtime = 0;
    if (!ResolveRuntime(supplied, runtime) || runtime >= Models::kStockModelCount || txd >= Models::kStockTxdCount)
        return luaL_error(L, "Expected a valid model ID at argument 1");
    if (!Models::WriteLiveTxdId(runtime, static_cast<std::uint16_t>(txd))) {
        NoteModelInfoGap();
        return luaL_error(L, "Expected a valid model ID at argument 1");
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaEngineDefs::EngineResetModelTXDID (CLuaEngineDefs.cpp:2176-2189).
int EngineResetModelTXDID(lua_State* L) {
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineResetModelTXDID", supplied, error)) return luaL_error(L, "%s", error.c_str());
    std::uint16_t runtime = 0;
    if (!ResolveRuntime(supplied, runtime) || runtime >= Models::kStockModelCount) return luaL_error(L, "Expected a valid model ID at argument 1");
    if (!Models::ResetLiveTxdId(runtime)) {
        NoteModelInfoGap();
        return luaL_error(L, "Expected a valid model ID at argument 1");
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaEngineDefs::EngineGetModelTXDID (CLuaEngineDefs.cpp:2129-2141).
int EngineGetModelTXDID(lua_State* L) {
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineGetModelTXDID", supplied, error)) return luaL_error(L, "%s", error.c_str());
    std::uint16_t runtime = 0;
    if (!ResolveRuntime(supplied, runtime)) return RejectModel(L, false);
    if (runtime >= Models::kStockModelCount) return RejectModel(L, true);
    Models::StockModelView view;
    if (!LoadModel(runtime, view)) return RejectModel(L, true);
    lua_pushnumber(L, static_cast<lua_Number>(view.txd));
    return 1;
}

}  // namespace

void InstallEngineLua(lua_State* L) {
    const struct {
        const char* name;
        lua_CFunction fn;
    } binds[] = {
        {"engineGetModelRuntimeID", EngineGetModelRuntimeID},
        {"engineGetModelServerID", EngineGetModelServerID},
        {"engineGetModelParent", EngineGetModelParent},
        {"engineGetModelType", EngineGetModelType},
        {"engineGetModelNameFromID", EngineGetModelNameFromID},
        {"engineGetModelIDFromName", EngineGetModelIDFromName},
        {"engineGetModelLODDistance", EngineGetModelLODDistance},
        {"engineGetModelFlags", EngineGetModelFlags},
        {"engineGetModelFlag", EngineGetModelFlag},
        {"engineGetModelTXDID", EngineGetModelTXDID},
        {"engineGetModelVisibleTime", EngineGetModelVisibleTime},
        {"engineSetModelVisibleTime", EngineSetModelVisibleTime},
        {"engineSetModelLODDistance", EngineSetModelLODDistance},
        {"engineResetModelLODDistance", EngineResetModelLODDistance},
        {"engineSetModelFlags", EngineSetModelFlags},
        {"engineSetModelFlag", EngineSetModelFlag},
        {"engineResetModelFlags", EngineResetModelFlags},
        {"engineGetTXDIDFromName", EngineGetTXDIDFromName},
        {"engineSetModelTXDID", EngineSetModelTXDID},
        {"engineResetModelTXDID", EngineResetModelTXDID},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    BindObjectGroupLua(L);
    BindSurfaceLua(L);
    BindStreamingLua(L);
    BindEngineWorldLua(L);
    BindEngineRendererLua(L);
    BindCullZoneLua(L);
    BindVehicleAudioLua(L);
    BindBreakEffectLua(L);
    BindEngineAssetLua(L);
}

}  // namespace WorldRPG::Script
