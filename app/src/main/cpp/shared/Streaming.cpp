#include "Streaming.h"

#include "LuaVMStub.h"
#include "../gtasa/GameTasks.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace WorldRPG::Models {
namespace {

std::uint64_t g_customMemory = 0;
CacheLimits g_cache;

}  // namespace

void SetStreamingMemory(std::uint64_t bytes) { g_customMemory = bytes; }

void RestoreStreamingMemory() { g_customMemory = 0; }

std::uint64_t StreamingMemory() { return g_customMemory != 0 ? g_customMemory : kDefaultStreamingMemory; }

void SetCacheLimits(std::optional<std::uint64_t> vehicles, std::optional<std::uint64_t> peds) {
    g_cache.pedsSet = peds.has_value();
    if (peds) g_cache.peds = *peds;
    g_cache.vehiclesSet = vehicles.has_value();
    if (vehicles) g_cache.vehicles = *vehicles;
}

CacheLimits GetCacheLimits() { return g_cache; }

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
namespace {

constexpr double kServerModelIdMax = 65534.0;
constexpr std::uint32_t kServerModelMin = 42341;
constexpr std::uint32_t kColBase = 25000;
constexpr std::uint32_t kFileIdCount = 26316;
constexpr std::uint32_t kModelInfoCount = 20000;
constexpr std::uint32_t kLargestImgBlocks = 1260u * 2u;

std::string Bad(const char* function, const char* expected, int index, const std::string& got) {
    return std::string("Bad argument @ '") + function + "' [Expected " + expected + " at argument " +
           std::to_string(index) + ", got " + got + "]";
}

std::string Got(lua_State* L, int index) {
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
        default: return lua_typename(L, lua_type(L, index));
    }
}

bool ReadWide(lua_State* L, int index, const char* function, int bits, std::uint64_t& out, std::string& error) {
    if (!lua_isnumber(L, index)) {
        error = Bad(function, "number", index, Got(L, index));
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        error = Bad(function, "number", index, "NaN");
        return false;
    }
    if (std::isinf(number)) {
        error = Bad(function, "number", index, "inf");
        return false;
    }
    const lua_Number upper = std::ldexp(1.0, bits);
    if (number < 0.0 || number >= upper) {
        error = Bad(function, "number within the destination type range", index, "out-of-range number");
        return false;
    }
    out = static_cast<std::uint64_t>(number);
    return true;
}

bool ReadOptionalWide(lua_State* L, int index, const char* function, int bits, std::optional<std::uint64_t>& out, std::string& error) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        out.reset();
        return true;
    }
    std::uint64_t value = 0;
    if (!ReadWide(L, index, function, bits, value, error)) return false;
    out = value;
    return true;
}

bool ReadOptionalBool(lua_State* L, int index, const char* function, std::optional<bool>& out, std::string& error) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        out.reset();
        return true;
    }
    if (type != LUA_TBOOLEAN) {
        error = Bad(function, "boolean", index, Got(L, index));
        return false;
    }
    out = lua_toboolean(L, index) != 0;
    return true;
}

bool ResolveStreamingId(std::uint64_t supplied, std::uint32_t& runtime) {
    if (supplied > kServerModelIdMax || supplied >= kServerModelMin) return false;
    runtime = static_cast<std::uint32_t>(supplied);
    return true;
}

void NoteStreaming(const char* what) {
    static std::once_flag once;
    std::call_once(once, [what] { LUA_LOGI("DIAG engine streaming: %s is not hooked", what); });
}

#if defined(__ANDROID__)
template <typename T>
T* StreamingSymbol(const char* name) {
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return nullptr;
    auto* symbol = static_cast<T*>(dlsym(lib, name));
    dlclose(lib);
    return symbol;
}
#endif

bool ReadUsedMemory(std::uint32_t& used) {
#if !defined(__ANDROID__)
    (void)used;
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        auto* value = StreamingSymbol<std::uint32_t>("_ZN10CStreaming13ms_memoryUsedE");
        if (!value) return;
        used = *value;
        ok = true;
    });
    return ran && ok;
#endif
}

bool ReadLoadState(std::uint32_t modelId, unsigned char& state) {
    if (modelId >= kFileIdCount) return false;
#if !defined(__ANDROID__)
    (void)state;
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        auto* info = StreamingSymbol<unsigned char>("_ZN10CStreaming16ms_aInfoForModelE");
        if (!info) return;
        state = info[static_cast<std::size_t>(modelId) * 0x14 + 0x10];
        ok = true;
    });
    return ran && ok;
#endif
}

bool ModelInfoPresent(std::uint32_t modelId) {
    if (modelId >= kModelInfoCount) return false;
#if !defined(__ANDROID__)
    return false;
#else
    bool present = false;
    const bool ran = GameTasks::Run([&] {
        auto** models = StreamingSymbol<void*>("_ZN10CModelInfo16ms_modelInfoPtrsE");
        if (!models) return;
        present = models[modelId] != nullptr;
    });
    return ran && present;
#endif
}

bool MakeSpace(std::uint32_t bytes) {
#if !defined(__ANDROID__)
    (void)bytes;
    return false;
#else
    bool ok = false;
    const bool ran = GameTasks::Run([&] {
        void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
        if (!lib) return;
        auto* fn = reinterpret_cast<void (*)(int)>(dlsym(lib, "_ZN10CStreaming12MakeSpaceForEi"));
        dlclose(lib);
        if (!fn) return;
        fn(static_cast<int>(bytes));
        ok = true;
    });
    return ran && ok;
#endif
}

const char* LoadStateName(unsigned char state) {
    switch (state) {
        case 0: return "unloaded";
        case 1: return "loaded";
        case 2: return "requested";
        case 3: return "reading";
        case 4: return "finishing";
        default: return "";
    }
}

int SetMemory(lua_State* L) {
    std::uint64_t bytes = 0;
    std::string error;
    if (!ReadWide(L, 1, "engineStreamingSetMemorySize", std::numeric_limits<std::uint32_t>::digits, bytes, error))
        return luaL_error(L, "%s", error.c_str());
    if (bytes == 0) return luaL_error(L, "Memory size must be > 0");
    Models::SetStreamingMemory(bytes);
    return 0;
}

int RestoreMemory(lua_State* L) {
    (void)L;
    Models::RestoreStreamingMemory();
    return 0;
}

int GetMemory(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(Models::StreamingMemory()));
    return 1;
}

int FreeMemory(lua_State* L) {
    std::uint64_t bytes = 0;
    std::string error;
    if (!ReadWide(L, 1, "engineStreamingFreeUpMemory", std::numeric_limits<std::uint32_t>::digits, bytes, error))
        return luaL_error(L, "%s", error.c_str());
    if (!MakeSpace(static_cast<std::uint32_t>(bytes))) NoteStreaming("CStreaming::MakeSpaceFor");
    return 0;
}

int GetUsed(lua_State* L) {
    std::uint32_t used = 0;
    if (!ReadUsedMemory(used)) {
        NoteStreaming("CStreaming::ms_memoryUsed");
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, used);
    return 1;
}

int SetBuffer(lua_State* L) {
    std::uint64_t bytes = 0;
    std::string error;
    if (!ReadWide(L, 1, "engineStreamingSetBufferSize", std::numeric_limits<std::uint32_t>::digits, bytes, error))
        return luaL_error(L, "%s", error.c_str());
    const std::uint64_t blocks = bytes / 2048ull;
    if (blocks > kLargestImgBlocks) NoteStreaming("CStreaming::SetStreamingBufferSize");
    lua_pushboolean(L, 0);
    return 1;
}

int RestoreBuffer(lua_State* L) {
    (void)L;
    NoteStreaming("CStreaming::SetStreamingBufferSize");
    return 0;
}

int GetBuffer(lua_State* L) {
    NoteStreaming("CStreaming::GetStreamingBufferSize");
    lua_pushboolean(L, 0);
    return 1;
}

int SetCache(lua_State* L) {
    std::optional<std::uint64_t> vehicles;
    std::optional<std::uint64_t> peds;
    std::string error;
    const int bits = std::numeric_limits<std::uint32_t>::digits;
    if (!ReadOptionalWide(L, 1, "engineStreamingSetModelCacheLimits", bits, vehicles, error))
        return luaL_error(L, "%s", error.c_str());
    if (!ReadOptionalWide(L, 2, "engineStreamingSetModelCacheLimits", bits, peds, error))
        return luaL_error(L, "%s", error.c_str());
    Models::SetCacheLimits(vehicles, peds);
    return 0;
}

int RequestModel(lua_State* L) {
    std::uint64_t supplied = 0;
    std::optional<bool> addReference;
    std::optional<bool> blocking;
    std::string error;
    if (!ReadWide(L, 1, "engineStreamingRequestModel", std::numeric_limits<std::uint32_t>::digits, supplied, error))
        return luaL_error(L, "%s", error.c_str());
    if (!ReadOptionalBool(L, 2, "engineStreamingRequestModel", addReference, error)) return luaL_error(L, "%s", error.c_str());
    if (!ReadOptionalBool(L, 3, "engineStreamingRequestModel", blocking, error)) return luaL_error(L, "%s", error.c_str());
    std::uint32_t runtime = 0;
    if (!ResolveStreamingId(supplied, runtime) || runtime >= kColBase || !ModelInfoPresent(runtime))
        return luaL_error(L, "Expected a valid model ID at argument 1");
    (void)addReference;
    (void)blocking;
    NoteStreaming("CResourceModelStreamer::RequestModel");
    lua_pushboolean(L, 0);
    return 1;
}

int ReleaseModel(lua_State* L) {
    std::uint64_t supplied = 0;
    std::optional<bool> removeReference;
    std::string error;
    if (!ReadWide(L, 1, "engineStreamingReleaseModel", std::numeric_limits<std::uint32_t>::digits, supplied, error))
        return luaL_error(L, "%s", error.c_str());
    if (!ReadOptionalBool(L, 2, "engineStreamingReleaseModel", removeReference, error))
        return luaL_error(L, "%s", error.c_str());
    std::uint32_t runtime = 0;
    if (!ResolveStreamingId(supplied, runtime) || runtime >= kColBase || !ModelInfoPresent(runtime))
        return luaL_error(L, "Expected a valid model ID at argument 1");
    (void)removeReference;
    NoteStreaming("CResourceModelStreamer::ReleaseModel");
    lua_pushboolean(L, 0);
    return 1;
}

int GetLoadState(lua_State* L) {
    std::uint64_t supplied = 0;
    std::string error;
    if (!ReadWide(L, 1, "engineStreamingGetModelLoadState", std::numeric_limits<std::uint32_t>::digits, supplied, error))
        return luaL_error(L, "%s", error.c_str());
    std::uint32_t runtime = 0;
    if (!ResolveStreamingId(supplied, runtime) || runtime >= kFileIdCount)
        return luaL_error(L, "Expected a valid model ID at argument 1");
    unsigned char state = 0;
    if (!ReadLoadState(runtime, state)) {
        NoteStreaming("CStreaming::ms_aInfoForModel");
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, LoadStateName(state));
    return 1;
}

}  // namespace

void BindStreamingLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"engineStreamingSetMemorySize", SetMemory},
        {"engineStreamingRestoreMemorySize", RestoreMemory},
        {"engineStreamingGetMemorySize", GetMemory},
        {"engineStreamingFreeUpMemory", FreeMemory},
        {"engineStreamingGetUsedMemory", GetUsed},
        {"engineStreamingSetBufferSize", SetBuffer},
        {"engineStreamingRestoreBufferSize", RestoreBuffer},
        {"engineStreamingGetBufferSize", GetBuffer},
        {"engineStreamingSetModelCacheLimits", SetCache},
        {"engineStreamingRequestModel", RequestModel},
        {"engineStreamingReleaseModel", ReleaseModel},
        {"engineStreamingGetModelLoadState", GetLoadState},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
