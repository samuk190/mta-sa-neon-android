// Asset load, image, replace, shader, and texture queries.
// CLuaEngineDefs.cpp:457-1393, 1499-1567, 2073-2126, 2274-2433, 3517-3548.
// A DFF is owned by its resource element. TXDs use a Lua finalizer.
// Applying them requires model-type-aware replacement and restoration through
// the game streamer. Other unsupported load operations report failure to Lua.

#include "EngineAssets.h"
#include "EngineModelLoader.h"
#include "LuaApiExtra.h"
#include "LuaFileApi.h"

#include "LuaVMStub.h"
#include "ModelNames.h"
#include "SampMapParser.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace WorldRPG::Script {
namespace {

constexpr std::uint32_t kServerModelMin = 42341;
constexpr std::uint32_t kTxdBase = 20000;
constexpr std::uint32_t kTxdCount = 5000;
constexpr std::uint32_t kFileIdCount = 26316;
constexpr std::uint32_t kClothesModelFirst = 30000;
constexpr std::uint32_t kClothesModelLast = 30151;
constexpr std::uint32_t kClothesTexFirst = 30152;
constexpr std::uint32_t kClothesTexLast = 30541;

constexpr const char* kTxdMetatable = "mta.engine.txd";

struct DffAsset {
    void* clump;
    uint32_t generation;
};

std::unordered_map<uint32_t, DffAsset> g_dffs;

void** TxdAt(lua_State* L) {
    if (lua_type(L, 1) != LUA_TUSERDATA || lua_rawlen(L, 1) != sizeof(void*)) return nullptr;
    return static_cast<void**>(luaL_testudata(L, 1, kTxdMetatable));
}

int CollectTxd(lua_State* L) {
    if (void** asset = TxdAt(L)) {
        NativeDestroyTXD(*asset);
        *asset = nullptr;
    }
    return 0;
}

void RegisterTxd(lua_State* L) {
    if (luaL_newmetatable(L, kTxdMetatable)) {
        lua_pushcfunction(L, CollectTxd);
        lua_setfield(L, -2, "__gc");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
}

const char* LuaTypeName(int type) {
    switch (type) {
        case LUA_TNONE: return "none";
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TLIGHTUSERDATA: return "lightuserdata";
        case LUA_TNUMBER: return "number";
        case LUA_TSTRING: return "string";
        case LUA_TTABLE: return "table";
        case LUA_TFUNCTION: return "function";
        case LUA_TUSERDATA: return "userdata";
        case LUA_TTHREAD: return "thread";
        default: return "unknown";
    }
}

std::string ParserGot(lua_State* L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TNONE: return "none";
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
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
        case LUA_TTABLE: return "table";
        case LUA_TFUNCTION: return "function";
        case LUA_TTHREAD: return "coroutine";
        case LUA_TUSERDATA: return "userdata";
        default: return lua_typename(L, lua_type(L, index));
    }
}

std::string ParserBad(const char* function, const char* expected, int index, const std::string& got) {
    return std::string("Bad argument @ '") + function + "' [Expected " + expected + " at argument " + std::to_string(index) + ", got " + got + "]";
}

std::string ScriptBad(lua_State* L, const char* function, const char* expected, int index) {
    const int type = lua_type(L, index);
    std::string message = std::string("Expected ") + expected + " at argument " + std::to_string(index) + ", got " + LuaTypeName(type);
    if (type == LUA_TNUMBER || type == LUA_TSTRING) {
        const char* value = lua_tostring(L, index);
        if (value && value[0] != '\0') message += std::string(" '") + value + "'";
    }
    return std::string("Bad argument @ '") + function + "' [" + message + "]";
}

std::string Usage(const char* function, const char* category, const std::string& reason) {
    return category + std::string(" @ '") + function + "' [" + reason + "]";
}

int FailFalse(lua_State* L, const std::string& message, bool log) {
    if (log && !message.empty()) LUA_LOGE("%s", message.c_str());
    lua_pushboolean(L, 0);
    return 1;
}

bool ReadScriptString(lua_State* L, int index, const char* function, std::string& out, std::string& error, bool optional, const char* fallback) {
    const int type = lua_type(L, index);
    if (optional && (type == LUA_TNONE || type == LUA_TNIL)) {
        out = fallback ? fallback : "";
        return true;
    }
    if (type == LUA_TNUMBER) {
        (void)lua_rawlen(L, index);
        out.clear();
        return true;
    }
    if (type != LUA_TSTRING) {
        error = ScriptBad(L, function, "string", index);
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    out.assign(text ? text : "", text ? length : 0);
    return true;
}

bool ReadParserString(lua_State* L, int index, const char* function, std::string& out, std::string& error) {
    const int type = lua_type(L, index);
    if (type != LUA_TSTRING && type != LUA_TNUMBER) {
        error = ParserBad(function, "string", index, ParserGot(L, index));
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    if (!text) {
        error = ParserBad(function, "string", index, ParserGot(L, index));
        return false;
    }
    out.assign(text, length);
    return true;
}

bool ReadParserUint(lua_State* L, int index, const char* function, std::uint32_t& out, std::string& error) {
    if (!lua_isnumber(L, index)) {
        error = ParserBad(function, "number", index, ParserGot(L, index));
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        error = ParserBad(function, "number", index, "NaN");
        return false;
    }
    if (std::isinf(number)) {
        error = ParserBad(function, "number", index, "inf");
        return false;
    }
    const lua_Number upper = std::ldexp(1.0, std::numeric_limits<std::uint32_t>::digits);
    if (number < 0.0 || number >= upper) {
        error = ParserBad(function, "number within the destination type range", index, "out-of-range number");
        return false;
    }
    out = static_cast<std::uint32_t>(number);
    return true;
}

bool ReadBounded(lua_State* L, int index, const char* function, double minimum, double maximum, double& out, std::string& error) {
    const int type = lua_type(L, index);
    if (type == LUA_TNUMBER || type == LUA_TSTRING) {
        if (!lua_isnumber(L, index)) {
            error = Usage(function, "Bad argument", "Expected number, got non-convertible string");
            return false;
        }
        const lua_Number number = lua_tonumber(L, index);
        if (std::isnan(number)) {
            error = Usage(function, "Bad argument", "Expected number, got NaN");
            return false;
        }
        if (!std::isfinite(number) || number < minimum || number > maximum) {
            error = Usage(function, "Bad argument", "Expected number within the supported range");
            return false;
        }
        out = static_cast<double>(number);
        return true;
    }
    error = ScriptBad(L, function, "number", index);
    return false;
}

bool ReadOptionalBool(lua_State* L, int index, const char* function, bool fallback, bool& out, std::string& error) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        out = fallback;
        return true;
    }
    if (type == LUA_TBOOLEAN) {
        out = lua_toboolean(L, index) != 0;
        return true;
    }
    error = ScriptBad(L, function, "bool", index);
    return false;
}

bool ResolveRuntime(std::uint32_t supplied, std::uint32_t& runtime) {
    if (supplied > std::numeric_limits<std::uint16_t>::max()) return false;
    if (supplied >= kServerModelMin) return false;
    runtime = supplied;
    return true;
}

bool ParseDecimal(std::string_view text, std::uint32_t& value) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool BeginsWithTxd(std::string_view text) {
    return text.size() >= 3 && (text[0] == 't' || text[0] == 'T') && (text[1] == 'x' || text[1] == 'X') && (text[2] == 'd' || text[2] == 'D');
}

// CModelNames::ResolveModelID (CModelNames.cpp:29326-29343). A "txdN" name is
// the stock TXD base plus N. Overflowing digit strings do not resolve.
bool ResolveText(const std::string& text, std::uint32_t& runtime) {
    std::uint32_t parsed = 0;
    if (ParseDecimal(text, parsed)) return ResolveRuntime(parsed, runtime);
    if (BeginsWithTxd(text)) {
        std::uint32_t txdId = 0;
        if (ParseDecimal(std::string_view(text).substr(3), txdId) && txdId != 0 && txdId < kTxdCount)
            return ResolveRuntime(kTxdBase + txdId, runtime);
    }
    return ResolveRuntime(WorldRPG::Models::ModelIdFromName(text), runtime);
}

bool LooksCol(const std::string& data) {
    return data.size() >= 32 && std::memcmp(data.data(), "COL", 3) == 0 && (data[3] == 'L' || data[3] == '2' || data[3] == '3' || data[3] == '4');
}

bool LooksIfp(const std::string& data) { return data.size() > 32 && std::memcmp(data.data(), "ANP", 3) == 0; }

int LoadAsset(lua_State* L, const char* function, const char* kind, bool (*raw)(const std::string&)) {
    std::string input;
    std::string error;
    if (!ReadScriptString(L, 1, function, input, error, false, "")) return FailFalse(L, error, true);
    if (std::strcmp(function, "engineLoadTXD") == 0 && lua_type(L, 2) == LUA_TBOOLEAN) {
        // The filtering flag is accepted. It does not change a missing file.
    }
    if (ActiveVmOwnerNet() == 0) return FailFalse(L, "", false);
    const bool isRaw = raw && raw(input);
    if (!isRaw && input.size() > 32767)
        return FailFalse(L, Usage(function, (std::string("Error loading ") + kind).c_str(), std::string("Corrupt ") + kind + " file data or file path too long"), true);
    if (isRaw) return FailFalse(L, Usage(function, (std::string("Error loading ") + kind).c_str(), "raw data"), true);
    LUA_LOGI("DIAG %s: resource file loading is not hooked", function);
    return FailFalse(L, Usage(function, "Bad file path", input), true);
}

int LoadCOL(lua_State* L) { return LoadAsset(L, "engineLoadCOL", "COL", LooksCol); }

int LoadRenderWareAsset(lua_State* L, bool dff) {
    const char* function = dff ? "engineLoadDFF" : "engineLoadTXD";
    std::string input;
    std::string error;
    if (!ReadScriptString(L, 1, function, input, error, false, "")) return FailFalse(L, error, true);
    if (ActiveVmOwnerNet() == 0) return FailFalse(L, "", false);

    std::filesystem::path path;
    if (input.size() > 32767 || !(dff ? ResolveModelPath(input, path) : ResolveFilePath(input, path)))
        return FailFalse(L, Usage(function, "Bad file path", input), true);

    const std::string filename = path.string();
    if (dff) {
        void* clump = NativeLoadDFF(filename.c_str());
        if (!clump) return FailFalse(L, Usage(function, "Error reading RenderWare file", input), true);
        const uint32_t id = CreateResourceElement("dff");
        if (!id) {
            NativeDestroyClump(clump);
            return FailFalse(L, Usage(function, "Error creating DFF element", input), true);
        }
        g_dffs.emplace(id, DffAsset{clump, ActiveVmOwnerGeneration()});
        PushElement(L, id);
        return 1;
    }

    auto** asset = static_cast<void**>(lua_newuserdata(L, sizeof(void*)));
    *asset = nullptr;
    luaL_setmetatable(L, kTxdMetatable);
    *asset = NativeLoadTXD(filename.c_str());
    if (*asset) return 1;

    lua_pop(L, 1);
    return FailFalse(L, Usage(function, "Error reading RenderWare file", input), true);
}

int LoadDFF(lua_State* L) {
    return LoadRenderWareAsset(L, true);
}

int LoadTXD(lua_State* L) {
    std::string error;
    bool filtering = true;
    if (!ReadOptionalBool(L, 2, "engineLoadTXD", true, filtering, error)) return FailFalse(L, error, true);
    return LoadRenderWareAsset(L, false);
}

int LoadIFP(lua_State* L) {
    std::string input;
    std::string block;
    std::string error;
    if (!ReadScriptString(L, 1, "engineLoadIFP", input, error, false, "")) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 2, "engineLoadIFP", block, error, false, "")) return FailFalse(L, error, true);
    (void)block;
    if (ActiveVmOwnerNet() == 0) return FailFalse(L, "", false);
    if (!LooksIfp(input) && input.size() > 32767)
        return FailFalse(L, Usage("engineLoadIFP", "Error loading IFP", "Corrupt IFP file data or file path too long"), true);
    if (LooksIfp(input)) return FailFalse(L, Usage("engineLoadIFP", "Error loading IFP", "raw data"), true);
    LUA_LOGI("DIAG engineLoadIFP: the IFP engine is not hooked");
    return FailFalse(L, Usage("engineLoadIFP", "Bad file path", input), true);
}

int LoadIMG(lua_State* L) {
    std::string path;
    std::string error;
    if (!ReadParserString(L, 1, "engineLoadIMG", path, error)) return luaL_error(L, "%s", error.c_str());
    (void)path;
    if (ActiveVmOwnerNet() == 0) {
        lua_pushnil(L);
        return 1;
    }
    LUA_LOGI("DIAG engineLoadIMG: IMG archives are not loaded");
    return luaL_error(L, "Bad file path");
}

int NeedImg(lua_State* L, const char* function) {
    return luaL_error(L, "%s", ParserBad(function, "img", 1, ParserGot(L, 1)).c_str());
}

int AddImage(lua_State* L) { return NeedImg(L, "engineAddImage"); }
int RemoveImage(lua_State* L) { return NeedImg(L, "engineRemoveImage"); }
int ImageCount(lua_State* L) { return NeedImg(L, "engineImageGetFilesCount"); }
int ImageFiles(lua_State* L) { return NeedImg(L, "engineImageGetFiles"); }
int ImageFile(lua_State* L) { return NeedImg(L, "engineImageGetFile"); }
int LinkDff(lua_State* L) { return NeedImg(L, "engineImageLinkDFF"); }
int LinkTxd(lua_State* L) { return NeedImg(L, "engineImageLinkTXD"); }

int RestoreDffImage(lua_State* L) {
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineRestoreDFFImage", supplied, error)) return luaL_error(L, "%s", error.c_str());
    std::uint32_t id = 0;
    if (!ResolveRuntime(supplied, id)) return luaL_error(L, "Expected a valid model ID at argument 1");
    if (id >= kTxdBase) return luaL_error(L, "Expected model ID in range [0-19999] at argument 1");
    lua_pushboolean(L, 0);
    return 1;
}

int RestoreTxdImage(lua_State* L) {
    std::uint32_t id = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineRestoreTXDImage", id, error)) return luaL_error(L, "%s", error.c_str());
    if (id >= kTxdCount) return luaL_error(L, "Expected TXD ID in range [0-4999] at argument 1");
    (void)kFileIdCount;
    lua_pushboolean(L, 0);
    return 1;
}

int ReplaceCol(lua_State* L) {
    if (lua_type(L, 1) != LUA_TUSERDATA) return FailFalse(L, ScriptBad(L, "engineReplaceCOL", "col-model", 1), true);
    double model = 0;
    std::string error;
    if (!ReadBounded(L, 2, "engineReplaceCOL", 0, 65534, model, error)) return FailFalse(L, error, true);
    (void)model;
    LUA_LOGE("Bad 'number' pointer @ 'engineReplaceCOL'(2)");
    lua_pushboolean(L, 0);
    return 1;
}

int RestoreCol(lua_State* L) {
    std::string text;
    std::string error;
    if (!ReadScriptString(L, 1, "engineRestoreCOL", text, error, false, "")) return FailFalse(L, error, true);
    std::uint32_t id = 0;
    (void)ResolveText(text, id);
    lua_pushboolean(L, 0);
    return 1;
}

int ImportTxd(lua_State* L) {
    if (!TxdAt(L)) return FailFalse(L, ScriptBad(L, "engineImportTXD", "txd", 1), true);
    return FailFalse(L, Usage("engineImportTXD", "Unavailable", "Model texture replacement needs streamer support"), true);
}

int ReplaceModel(lua_State* L) {
    if (!g_dffs.contains(ElementIdOf(L, 1))) return FailFalse(L, ScriptBad(L, "engineReplaceModel", "dff", 1), true);
    return FailFalse(L, Usage("engineReplaceModel", "Unavailable", "Model replacement needs streamer support"), true);
}

int RestoreModel(lua_State* L) {
    const char* printed = nullptr;
    if (lua_type(L, 1) == LUA_TSTRING || lua_type(L, 1) == LUA_TNUMBER) printed = lua_tostring(L, 1);
    const std::string text = printed ? printed : "";
    std::uint32_t id = 0;
    const bool clothes = ResolveText(text, id) && id >= kClothesModelFirst && id <= kClothesModelLast;
    if (!clothes) LUA_LOGE("Bad argument @ 'engineRestoreModel'");
    lua_pushboolean(L, 0);
    return 1;
}

int RequestModel(lua_State* L) {
    const char* text = lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : nullptr;
    const std::string kind = text ? text : "";
    if (kind != "ped" && kind != "object" && kind != "object-damageable" && kind != "vehicle" && kind != "timed-object" && kind != "clump")
        return FailFalse(L, ScriptBad(L, "engineRequestModel", "client-model-type", 1), true);
    if (ActiveVmOwnerNet() != 0) LUA_LOGI("DIAG engineRequestModel: custom model allocation is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int FreeModel(lua_State* L) {
    double model = 0;
    std::string error;
    if (!ReadBounded(L, 1, "engineFreeModel", 0, static_cast<double>(kTxdBase - 1), model, error)) return FailFalse(L, error, true);
    (void)model;
    lua_pushboolean(L, 0);
    return 1;
}

int RequestTxd(lua_State* L) {
    std::string name;
    std::string error;
    if (!ReadParserString(L, 1, "engineRequestTXD", name, error)) return luaL_error(L, "%s", error.c_str());
    if (name.size() > 24) return luaL_error(L, "TXD name length shoudn't be more than 24 characters");
    // The desktop function returns uint. `return false` becomes 0, and Lua
    // treats 0 as true, so this must not be a boolean.
    LUA_LOGI("DIAG engineRequestTXD: no free TXD slot is allocated");
    lua_pushnumber(L, 0);
    return 1;
}

int FreeTxd(lua_State* L) {
    std::uint32_t id = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineFreeTXD", id, error)) return luaL_error(L, "%s", error.c_str());
    if (id >= kTxdCount) return luaL_error(L, "Expected TXD ID in range [0-4999] at argument 1");
    lua_pushboolean(L, 0);
    return 1;
}

int AddClothesTxd(lua_State* L) { return luaL_error(L, "%s", ParserBad("engineAddClothingTXD", "txd", 1, ParserGot(L, 1)).c_str()); }

int AddClothesModel(lua_State* L) { return luaL_error(L, "%s", ParserBad("engineAddClothingModel", "dff", 1, ParserGot(L, 1)).c_str()); }

bool CanonicalNeonPath(const std::string& path) {
    if (path.empty() || path.size() > std::numeric_limits<std::uint16_t>::max() || path.front() == '/' || path.back() == '/' ||
        path.find('\\') != std::string::npos || path.find_first_of(":*?\"<>|") != std::string::npos || path.size() < 10 ||
        path.compare(path.size() - 10, 10, ".neonasset") != 0)
        return false;
    const std::filesystem::path filesystemPath(path);
    if (filesystemPath.has_root_path() || filesystemPath.lexically_normal().generic_string() != path) return false;
    for (const std::filesystem::path& part : filesystemPath) {
        if (part.empty() || part == "." || part == "..") return false;
    }
    return true;
}

int ReplaceEncrypted(lua_State* L) {
    std::string path;
    std::string model;
    std::string error;
    bool alpha = false;
    bool filtering = true;
    if (!ReadScriptString(L, 1, "engineReplaceEncryptedModel", path, error, false, "")) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 2, "engineReplaceEncryptedModel", model, error, false, "")) return FailFalse(L, error, true);
    if (!ReadOptionalBool(L, 3, "engineReplaceEncryptedModel", false, alpha, error)) return FailFalse(L, error, true);
    if (!ReadOptionalBool(L, 4, "engineReplaceEncryptedModel", true, filtering, error)) return FailFalse(L, error, true);
    (void)alpha;
    (void)filtering;
    std::uint32_t id = 0;
    if (ActiveVmOwnerNet() != 0 && CanonicalNeonPath(path) && ResolveText(model, id)) {
        LUA_LOGI("DIAG engineReplaceEncryptedModel: the resource file is not an auto-download neon asset");
        return FailFalse(L, Usage("engineReplaceEncryptedModel", "Encrypted model is not a declared auto-download file", path), true);
    }
    (void)id;
    return FailFalse(L, Usage("engineReplaceEncryptedModel", "Bad usage", "Expected an own-resource .neonasset path and a valid non-clothes model ID"), true);
}

int ReplaceAnim(lua_State* L) {
    if (lua_type(L, 1) != LUA_TUSERDATA) return FailFalse(L, ScriptBad(L, "engineReplaceAnimation", "element", 1), true);
    std::string internalBlock;
    std::string internalAnim;
    std::string customBlock;
    std::string customAnim;
    std::string error;
    if (!ReadScriptString(L, 2, "engineReplaceAnimation", internalBlock, error, false, "")) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 3, "engineReplaceAnimation", internalAnim, error, false, "")) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 4, "engineReplaceAnimation", customBlock, error, false, "")) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 5, "engineReplaceAnimation", customAnim, error, false, "")) return FailFalse(L, error, true);
    LUA_LOGI("DIAG engineReplaceAnimation: the IFP engine is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int RestoreAnim(lua_State* L) {
    if (lua_type(L, 1) != LUA_TUSERDATA) return FailFalse(L, ScriptBad(L, "engineRestoreAnimation", "element", 1), true);
    const int second = lua_type(L, 2);
    if (second != LUA_TNONE && second != LUA_TNIL && second != LUA_TSTRING && second != LUA_TNUMBER)
        return FailFalse(L, ScriptBad(L, "engineRestoreAnimation", "string", 2), true);
    if (second == LUA_TSTRING || second == LUA_TNUMBER) {
        const int third = lua_type(L, 3);
        if (third != LUA_TNONE && third != LUA_TNIL && third != LUA_TSTRING && third != LUA_TNUMBER)
            return FailFalse(L, ScriptBad(L, "engineRestoreAnimation", "string", 3), true);
    }
    LUA_LOGI("DIAG engineRestoreAnimation: the IFP engine is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int ApplyShader(lua_State* L) {
    if (lua_type(L, 1) != LUA_TUSERDATA) return FailFalse(L, ScriptBad(L, "engineApplyShaderToWorldTexture", "material", 1), true);
    std::string texture;
    std::string error;
    bool append = true;
    if (!ReadScriptString(L, 2, "engineApplyShaderToWorldTexture", texture, error, false, "")) return FailFalse(L, error, true);
    if (lua_type(L, 3) != LUA_TNONE && lua_type(L, 3) != LUA_TNIL && lua_type(L, 3) != LUA_TUSERDATA)
        return FailFalse(L, ScriptBad(L, "engineApplyShaderToWorldTexture", "element", 3), true);
    if (!ReadOptionalBool(L, 4, "engineApplyShaderToWorldTexture", true, append, error)) return FailFalse(L, error, true);
    (void)texture;
    (void)append;
    LUA_LOGI("DIAG engineApplyShaderToWorldTexture: the shader pipeline is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int RemoveShader(lua_State* L) {
    if (lua_type(L, 1) != LUA_TUSERDATA) return FailFalse(L, ScriptBad(L, "engineRemoveShaderFromWorldTexture", "material", 1), true);
    std::string texture;
    std::string error;
    if (!ReadScriptString(L, 2, "engineRemoveShaderFromWorldTexture", texture, error, false, "")) return FailFalse(L, error, true);
    if (lua_type(L, 3) != LUA_TNONE && lua_type(L, 3) != LUA_TNIL && lua_type(L, 3) != LUA_TUSERDATA)
        return FailFalse(L, ScriptBad(L, "engineRemoveShaderFromWorldTexture", "element", 3), true);
    (void)texture;
    LUA_LOGI("DIAG engineRemoveShaderFromWorldTexture: the shader pipeline is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int TextureNames(lua_State* L) {
    std::string name;
    std::string error;
    if (!ReadScriptString(L, 1, "engineGetModelTextureNames", name, error, true, "")) return FailFalse(L, error, true);
    std::uint32_t id = 0;
    if (!ResolveText(name, id)) return FailFalse(L, Usage("engineGetModelTextureNames", "Bad usage", "Expected valid model ID or name at argument 1"), true);
    LUA_LOGI("DIAG engineGetModelTextureNames: model texture names are not read");
    lua_pushboolean(L, 0);
    return 1;
}

int VisibleTextures(lua_State* L) {
    std::string match;
    std::string model;
    std::string error;
    if (!ReadScriptString(L, 1, "engineGetVisibleTextureNames", match, error, true, "*")) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 2, "engineGetVisibleTextureNames", model, error, true, "")) return FailFalse(L, error, true);
    std::uint32_t id = 0;
    if (!model.empty() && !ResolveText(model, id))
        return FailFalse(L, Usage("engineGetVisibleTextureNames", "Bad usage", "Expected valid model ID or name at argument 1"), true);
    (void)match;
    (void)id;
    static bool noted = false;
    if (!noted) {
        noted = true;
        LUA_LOGI("DIAG engineGetVisibleTextureNames: the visible texture list is not read");
    }
    lua_newtable(L);
    return 1;
}

void SetNumber(lua_State* L, const char* name, double value) {
    lua_pushnumber(L, value);
    lua_setfield(L, -2, name);
}

void SetString(lua_State* L, const char* name, const std::string& value) {
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, name);
}

int ParseSampMap(lua_State* L) {
    std::string source;
    std::string error;
    if (!ReadScriptString(L, 1, "engineParseSAMPMap", source, error, false, "")) return luaL_error(L, "%s", error.c_str());
    const SampMap::SParseResult result = SampMap::Parse(std::string_view(source.data(), source.size()));
    lua_createtable(L, 0, 7);
    lua_pushboolean(L, result.Succeeded() ? 1 : 0);
    lua_setfield(L, -2, "success");
    SetNumber(L, "errorCount", static_cast<double>(result.errorCount));
    SetNumber(L, "objectCount", static_cast<double>(result.objects.size()));
    SetNumber(L, "removedBuildingCount", static_cast<double>(result.removedBuildings.size()));

    lua_createtable(L, static_cast<int>(result.objects.size()), 0);
    for (std::size_t objectIndex = 0; objectIndex < result.objects.size(); ++objectIndex) {
        const SampMap::SObject& object = result.objects[objectIndex];
        lua_createtable(L, 0, 20);
        const char* creationType = object.creationKind == SampMap::EObjectCreationKind::Object          ? "object"
                                   : object.creationKind == SampMap::EObjectCreationKind::DynamicObject ? "dynamicObject"
                                                                                                        : "dynamicObjectEx";
        lua_pushstring(L, creationType);
        lua_setfield(L, -2, "creationType");
        SetString(L, "handle", object.sourceHandle);
        SetNumber(L, "model", object.model);
        SetNumber(L, "x", object.positionX);
        SetNumber(L, "y", object.positionY);
        SetNumber(L, "z", object.positionZ);
        SetNumber(L, "rx", object.rotationX);
        SetNumber(L, "ry", object.rotationY);
        SetNumber(L, "rz", object.rotationZ);
        SetNumber(L, "world", object.virtualWorld);
        SetNumber(L, "interior", object.interior);
        SetNumber(L, "player", object.player);
        SetNumber(L, "area", object.area);
        SetNumber(L, "priority", object.priority);
        SetNumber(L, "streamDistance", object.streamDistance);
        SetNumber(L, "drawDistance", object.drawDistance);
        SetNumber(L, "line", static_cast<double>(object.location.line));
        SetNumber(L, "column", static_cast<double>(object.location.column));
        lua_createtable(L, static_cast<int>(object.materials.size()), 0);
        for (std::size_t materialIndex = 0; materialIndex < object.materials.size(); ++materialIndex) {
            const SampMap::SMaterial& material = object.materials[materialIndex];
            lua_createtable(L, 0, 8);
            SetNumber(L, "slot", material.slot);
            SetNumber(L, "sourceModel", material.sourceModel);
            SetString(L, "txd", material.txdName);
            SetString(L, "texture", material.textureName);
            SetNumber(L, "color", material.color);
            SetNumber(L, "line", static_cast<double>(material.location.line));
            SetNumber(L, "column", static_cast<double>(material.location.column));
            lua_rawseti(L, -2, static_cast<int>(materialIndex + 1));
        }
        lua_setfield(L, -2, "materials");
        lua_rawseti(L, -2, static_cast<int>(objectIndex + 1));
    }
    lua_setfield(L, -2, "objects");

    lua_createtable(L, static_cast<int>(result.removedBuildings.size()), 0);
    for (std::size_t removalIndex = 0; removalIndex < result.removedBuildings.size(); ++removalIndex) {
        const SampMap::SRemovedBuilding& removal = result.removedBuildings[removalIndex];
        lua_createtable(L, 0, 7);
        SetNumber(L, "model", removal.model);
        SetNumber(L, "x", removal.positionX);
        SetNumber(L, "y", removal.positionY);
        SetNumber(L, "z", removal.positionZ);
        SetNumber(L, "radius", removal.radius);
        SetNumber(L, "line", static_cast<double>(removal.location.line));
        SetNumber(L, "column", static_cast<double>(removal.location.column));
        lua_rawseti(L, -2, static_cast<int>(removalIndex + 1));
    }
    lua_setfield(L, -2, "removedBuildings");

    lua_createtable(L, static_cast<int>(result.diagnostics.size()), 0);
    for (std::size_t diagnosticIndex = 0; diagnosticIndex < result.diagnostics.size(); ++diagnosticIndex) {
        const SampMap::SDiagnostic& diagnostic = result.diagnostics[diagnosticIndex];
        lua_createtable(L, 0, 4);
        lua_pushstring(L, diagnostic.severity == SampMap::EDiagnosticSeverity::Error ? "error" : "warning");
        lua_setfield(L, -2, "severity");
        SetNumber(L, "line", static_cast<double>(diagnostic.location.line));
        SetNumber(L, "column", static_cast<double>(diagnostic.location.column));
        SetString(L, "message", diagnostic.message);
        lua_rawseti(L, -2, static_cast<int>(diagnosticIndex + 1));
    }
    lua_setfield(L, -2, "diagnostics");
    return 1;
}

int ModelTextures(lua_State* L) {
    std::string name;
    std::string error;
    if (!ReadScriptString(L, 1, "engineGetModelTextures", name, error, false, "")) return luaL_error(L, "%s", error.c_str());
    (void)name;
    if (lua_type(L, 2) == LUA_TSTRING || lua_type(L, 2) == LUA_TNUMBER || lua_istable(L, 2) || lua_type(L, 2) == LUA_TNONE) {
        return luaL_error(L, "%s", Usage("engineGetModelTextures", "Bad usage", "Invalid model ID").c_str());
    }
    return luaL_error(L, "%s", Usage("engineGetModelTextures", "Bad usage", "Invalid model ID").c_str());
}

}  // namespace

void ReleaseDffElement(uint32_t id) {
    const auto it = g_dffs.find(id);
    if (it == g_dffs.end()) return;
    NativeDestroyClump(it->second.clump);
    g_dffs.erase(it);
}

void DropResourceDffs(uint32_t generation) {
    std::vector<uint32_t> owned;
    for (const auto& [id, asset] : g_dffs)
        if (asset.generation == generation) owned.push_back(id);
    for (uint32_t id : owned)
        if (!DestroyClientElementForced(id)) ReleaseDffElement(id);
}

void ResetDffElements() {
    for (const auto& [id, asset] : g_dffs) NativeDestroyClump(asset.clump);
    g_dffs.clear();
}

void BindEngineAssetLua(lua_State* L) {
    RegisterTxd(L);
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"engineLoadCOL", LoadCOL},
        {"engineLoadDFF", LoadDFF},
        {"engineLoadTXD", LoadTXD},
        {"engineLoadIFP", LoadIFP},
        {"engineLoadIMG", LoadIMG},
        {"engineAddImage", AddImage},
        {"engineRemoveImage", RemoveImage},
        {"engineImageGetFilesCount", ImageCount},
        {"engineImageGetFiles", ImageFiles},
        {"engineImageGetFile", ImageFile},
        {"engineImageLinkDFF", LinkDff},
        {"engineImageLinkTXD", LinkTxd},
        {"engineRestoreDFFImage", RestoreDffImage},
        {"engineRestoreTXDImage", RestoreTxdImage},
        {"engineReplaceCOL", ReplaceCol},
        {"engineRestoreCOL", RestoreCol},
        {"engineImportTXD", ImportTxd},
        {"engineReplaceModel", ReplaceModel},
        {"engineRestoreModel", RestoreModel},
        {"engineRequestModel", RequestModel},
        {"engineFreeModel", FreeModel},
        {"engineRequestTXD", RequestTxd},
        {"engineFreeTXD", FreeTxd},
        {"engineAddClothingTXD", AddClothesTxd},
        {"engineAddClothingModel", AddClothesModel},
        {"engineReplaceEncryptedModel", ReplaceEncrypted},
        {"engineReplaceAnimation", ReplaceAnim},
        {"engineRestoreAnimation", RestoreAnim},
        {"engineApplyShaderToWorldTexture", ApplyShader},
        {"engineRemoveShaderFromWorldTexture", RemoveShader},
        {"engineGetModelTextureNames", TextureNames},
        {"engineGetVisibleTextureNames", VisibleTextures},
        {"engineGetModelTextures", ModelTextures},
        {"engineParseSAMPMap", ParseSampMap},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
