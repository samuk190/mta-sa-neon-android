#include "LuaCryptApi.h"

// Port of CLuaCryptDefs (mta_neon_source/Shared/mods/deathmatch/logic/luadefs/CLuaCryptDefs.cpp)
// for passwordHash (162-234), passwordVerify (236-316), generateKeyPair (318-397),
// encodeString (399-836) and decodeString (838-1264). The other CLuaCryptDefs functions
// (md5, sha256, hash, tea*, base64*) were already in LuaUtilApi.cpp.
//
// Argument rules follow the desktop readers:
//  - encodeString / decodeString / passwordVerify use CScriptArgReader: strings accept
//    numbers, an options table is a CStringMap (string keys; string, number and boolean
//    values, booleans as "1"/"0"; other values are kept as ""), errors log and return
//    false (nil for passwordVerify).
//  - passwordHash is ArgumentParserWarn<false>: errors log a warning and return false.
//  - generateKeyPair is ArgumentParser: errors are Lua errors.
//
// The callback forms run the work on a worker thread like CLuaShared::GetAsyncTaskScheduler
// and call back on the VM thread (CryptDeliverCompletions, from ResourceLoader::Pump).

#include "LuaVMStub.h"
#include "SharedCrypto.h"

#include "lua.hpp"

#include <zlib.h>

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace WorldRPG::Script {
namespace {

namespace C = WorldRPG::Crypto;

// --- Async task queue -------------------------------------------------------------

// One Lua value of a callback call. Only strings and booleans are ever passed.
struct Value {
    bool isString = false;
    bool boolean = false;
    std::string text;
};

Value Str(std::string text) {
    Value value;
    value.isString = true;
    value.text = std::move(text);
    return value;
}

Value Bool(bool b) {
    Value value;
    value.boolean = b;
    return value;
}

struct Outcome {
    std::vector<Value> values;
    std::string warning;      // m_pScriptDebugging->LogWarning
    std::string customError;  // m_pScriptDebugging->LogCustom
};

struct Task {
    uint64_t id = 0;
    std::function<Outcome()> work;
};

// Worker side, guarded by mutex.
struct Worker {
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable idle;
    std::deque<Task> queue;
    std::vector<std::pair<uint64_t, Outcome>> done;
    bool busy = false;
    bool stop = false;
    std::thread thread;

    ~Worker() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        wake.notify_all();
        if (thread.joinable()) thread.join();
    }

    void Run() {
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stop || !queue.empty(); });
                if (stop) return;
                task = std::move(queue.front());
                queue.pop_front();
                busy = true;
            }
            Outcome outcome;
            try {
                outcome = task.work();
            } catch (const std::exception& ex) {
                outcome = Outcome{};
                outcome.values.push_back(Bool(false));
                outcome.warning = ex.what();
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                done.emplace_back(task.id, std::move(outcome));
                busy = false;
            }
            idle.notify_all();
        }
    }

    void Push(Task task) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!thread.joinable()) thread = std::thread([this] { Run(); });
            queue.push_back(std::move(task));
        }
        wake.notify_one();
    }
};

Worker& GetWorker() {
    static Worker worker;
    return worker;
}

// VM thread only.
struct Pending {
    int ref = LUA_NOREF;
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};
lua_State* g_L = nullptr;
std::unordered_map<uint64_t, Pending> g_pending;
uint64_t g_nextTask = 1;

// Pushes a task whose result is passed to the function at fnIndex.
void PushTask(lua_State* L, int fnIndex, std::function<Outcome()> work) {
    lua_pushvalue(L, fnIndex);
    Pending pending;
    pending.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    pending.owner = ActiveVmOwner();
    pending.netId = ActiveVmOwnerNet();
    pending.generation = ActiveVmOwnerGeneration();
    const uint64_t id = g_nextTask++;
    g_pending.emplace(id, std::move(pending));
    GetWorker().Push(Task{id, std::move(work)});
}

// --- Argument readers -------------------------------------------------------------

enum class Encode { TEA, AES128, RSA, BASE64, BASE32, ZLIB, Invalid };

// StringEncodeFunction (Enums.cpp:66-73).
Encode EncodeOf(const char* name) {
    if (!name) return Encode::Invalid;
    if (!std::strcmp(name, "tea")) return Encode::TEA;
    if (!std::strcmp(name, "aes128")) return Encode::AES128;
    if (!std::strcmp(name, "rsa")) return Encode::RSA;
    if (!std::strcmp(name, "base64")) return Encode::BASE64;
    if (!std::strcmp(name, "base32")) return Encode::BASE32;
    if (!std::strcmp(name, "zlib")) return Encode::ZLIB;
    return Encode::Invalid;
}

// CScriptArgReader::ReadString: string or number.
bool ReadString(lua_State* L, int index, std::string& out) {
    const int type = lua_type(L, index);
    if (type != LUA_TSTRING && type != LUA_TNUMBER) return false;
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    out.assign(text, length);
    return true;
}

using StringMap = std::map<std::string, std::string>;

// CScriptArgReader::InternalReadStringMap (CScriptArgReader.h:1253-1281), without sub-maps:
// no function here reads one, so a nested table is kept as an empty value.
void ReadStringMap(lua_State* L, int index, StringMap& out) {
    out.clear();
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            std::string value;
            const int type = lua_type(L, -1);
            if (type == LUA_TSTRING || type == LUA_TNUMBER) {
                size_t length = 0;
                const char* text = lua_tolstring(L, -1, &length);  // converts the value slot only
                value.assign(text, length);
            } else if (type == LUA_TBOOLEAN) {
                value = lua_toboolean(L, -1) ? "1" : "0";
            }
            out.emplace(lua_tostring(L, -2), std::move(value));
        }
        lua_pop(L, 1);
    }
}

// CStringMap::ReadBool -> ReadNumber: stringstream >> bool, default when it does not parse.
bool MapBool(const StringMap& map, const char* key, bool fallback) {
    auto found = map.find(key);
    if (found == map.end()) return fallback;
    std::stringstream ss(found->second);
    bool value = false;
    if (ss >> value) return value;
    return fallback;
}

std::string Upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

// ZLibFormat names (Enums.cpp:88-92), then SharedUtil::StringToZLibFormat.
bool ReadZLibFormat(const std::string& text, int& format) {
    if (text == "raw") format = static_cast<int>(C::ZLibFormat::ZRAW);
    else if (text == "zlib") format = static_cast<int>(C::ZLibFormat::ZLIB);
    else if (text == "gzip") format = static_cast<int>(C::ZLibFormat::GZIP);
    else return C::StringToZLibFormat(text, format);
    return true;
}

// ZLibStrategy names (Enums.cpp:94-100).
bool ReadZLibStrategy(const std::string& text, C::ZLibStrategy& strategy) {
    if (text == "default") strategy = C::ZLibStrategy::DEFAULT;
    else if (text == "filtered") strategy = C::ZLibStrategy::FILTERED;
    else if (text == "huffman") strategy = C::ZLibStrategy::HUFFMAN_ONLY;
    else if (text == "rle") strategy = C::ZLibStrategy::RLE;
    else if (text == "fixed") strategy = C::ZLibStrategy::FIXED;
    else return false;
    return true;
}

int PushBool(lua_State* L, bool value) {
    lua_pushboolean(L, value ? 1 : 0);
    return 1;
}

void PushString(lua_State* L, const std::string& text) { lua_pushlstring(L, text.data(), text.size()); }

int CustomFalse(lua_State* L, const char* message) {
    LUA_LOGE("%s", message);
    return PushBool(L, false);
}

int WarningFalse(lua_State* L, const std::string& message) {
    LUA_LOGE("WARNING: %s", message.c_str());
    return PushBool(L, false);
}

// The reader part shared by encodeString and decodeString (399-419, 838-858).
// Returns false after logging; fnIndex is 0 without a callback.
bool ReadCodecArgs(lua_State* L, const char* function, Encode& algorithm, std::string& data, StringMap& options, int& fnIndex) {
    algorithm = lua_type(L, 1) == LUA_TSTRING ? EncodeOf(lua_tostring(L, 1)) : Encode::Invalid;
    if (algorithm == Encode::Invalid) {
        LUA_LOGE("Bad argument @ '%s' [Expected string-encode-function at argument 1]", function);
        return false;
    }
    if (!ReadString(L, 2, data)) {
        LUA_LOGE("Bad argument @ '%s' [Expected string at argument 2]", function);
        return false;
    }
    int index = 3;
    const bool tableOptional = algorithm == Encode::BASE64 || algorithm == Encode::BASE32 || algorithm == Encode::ZLIB;
    if (!tableOptional || lua_istable(L, index)) {
        if (!lua_istable(L, index)) {
            LUA_LOGE("Bad argument @ '%s' [Expected table at argument %d]", function, index);
            return false;
        }
        ReadStringMap(L, index, options);
        ++index;
    }
    // ReadFunction(luaFunctionRef, LUA_REFNIL): a function, or nil / none.
    const int type = lua_type(L, index);
    if (type == LUA_TFUNCTION) {
        fnIndex = index;
    } else if (type == LUA_TNONE || type == LUA_TNIL) {
        fnIndex = 0;
    } else {
        LUA_LOGE("Bad argument @ '%s' [Expected function at argument %d]", function, index);
        return false;
    }
    return true;
}

// Callback outcome for the "string or warning + false" tasks.
Outcome StringOrWarning(std::function<std::string()> body) {
    Outcome outcome;
    try {
        outcome.values.push_back(Str(body()));
    } catch (const C::CryptoError& ex) {
        outcome.values.push_back(Bool(false));
        outcome.warning = ex.what();
    }
    return outcome;
}

// --- encodeString / decodeString --------------------------------------------------

int EncodeString(lua_State* L) {
    Encode algorithm;
    std::string data;
    StringMap options;
    int fn = 0;
    if (!ReadCodecArgs(L, "encodeString", algorithm, data, options, fn)) return PushBool(L, false);

    switch (algorithm) {
        case Encode::TEA: {
            const std::string key = options["key"];
            if (key.empty()) return CustomFalse(L, "Invalid value for field 'key'");
            if (fn) {
                PushTask(L, fn, [data, key] {
                    std::string result;
                    C::TeaEncode(data, key, &result);
                    return Outcome{{Str(result)}, {}, {}};
                });
                return PushBool(L, true);
            }
            std::string result;
            C::TeaEncode(data, key, &result);
            PushString(L, result);
            return 1;
        }
        case Encode::AES128: {
            const std::string key = options["key"];
            if (key.size() != 16) return CustomFalse(L, "Invalid key length (must be 16 characters long)");
            if (fn) {
                PushTask(L, fn, [data, key] {
                    const auto result = C::Aes128encode(data, key);
                    if (result.first.empty()) return Outcome{{Bool(false)}, {}, {}};
                    return Outcome{{Str(result.first), Str(result.second)}, {}, {}};
                });
                // Desktop returns 2 here with only `true` pushed, so its second value is
                // whatever was below on the stack; the phone returns only `true`.
                return PushBool(L, true);
            }
            const auto result = C::Aes128encode(data, key);
            // Desktop pushes the pair even when Aes128encode returned empty strings.
            PushString(L, result.first);
            PushString(L, result.second);
            return 2;
        }
        case Encode::RSA: {
            const std::string key = options["key"];
            if (key.empty()) return CustomFalse(L, "Invalid value for field 'key'");
            if (fn) {
                PushTask(L, fn, [data, key] { return StringOrWarning([&] { return C::RsaEncode(data, key); }); });
                return PushBool(L, true);
            }
            std::string result;
            try {
                result = C::RsaEncode(data, key);
            } catch (const C::CryptoError& ex) {
                return WarningFalse(L, ex.what());
            }
            PushString(L, result);
            return 1;
        }
        case Encode::BASE64:
        case Encode::BASE32: {
            const bool b64 = algorithm == Encode::BASE64;
            const std::string variant = Upper(options["variant"]);
            if (!variant.empty() && variant != (b64 ? "URL" : "HEX")) return CustomFalse(L, "Invalid value for field 'variant'");
            auto body = [data, variant, b64] { return b64 ? C::Base64encode(data, variant) : C::Base32encode(data, variant); };
            if (fn) {
                PushTask(L, fn, [body] { return StringOrWarning(body); });
                return PushBool(L, true);
            }
            std::string result;
            try {
                result = body();
            } catch (const C::CryptoError& ex) {
                return WarningFalse(L, ex.what());
            }
            PushString(L, result);
            return 1;
        }
        case Encode::ZLIB: {
            int compression = 9;
            int format = static_cast<int>(C::ZLibFormat::GZIP);
            C::ZLibStrategy strategy = C::ZLibStrategy::DEFAULT;
            if (!options["format"].empty() && !ReadZLibFormat(options["format"], format))
                return CustomFalse(L, "Invalid value for field 'format'");
            if (!options["strategy"].empty() && !ReadZLibStrategy(options["strategy"], strategy))
                return CustomFalse(L, "Invalid value for field 'strategy'");
            if (!options["compression"].empty()) {
                compression = std::atoi(options["compression"].c_str());
                if (compression < 0 || compression > 9)
                    return CustomFalse(L, "Value for field 'compression' is out of range (0-9)");
            }
            if (fn) {
                PushTask(L, fn, [data, format, compression, strategy] {
                    std::string output;
                    const int result = C::ZLibCompress(data, output, format, compression, strategy);
                    if (result == Z_STREAM_END) return Outcome{{Str(output)}, {}, {}};
                    return Outcome{{Bool(false)}, "zlib error: " + std::to_string(result), {}};
                });
                return PushBool(L, true);
            }
            std::string output;
            const int result = C::ZLibCompress(data, output, format, compression, strategy);
            if (result != Z_STREAM_END) return WarningFalse(L, "zlib error: " + std::to_string(result));
            PushString(L, output);
            return 1;
        }
        default:
            return CustomFalse(L, "Unknown encryption algorithm");
    }
}

int DecodeString(lua_State* L) {
    Encode algorithm;
    std::string data;
    StringMap options;
    int fn = 0;
    if (!ReadCodecArgs(L, "decodeString", algorithm, data, options, fn)) return PushBool(L, false);

    switch (algorithm) {
        case Encode::TEA: {
            const std::string key = options["key"];
            if (key.empty()) return CustomFalse(L, "Invalid value for field 'key'");
            if (fn) {
                PushTask(L, fn, [data, key] {
                    std::string result;
                    C::TeaDecode(data, key, &result);
                    return Outcome{{Str(result)}, {}, {}};
                });
                return PushBool(L, true);
            }
            std::string result;
            C::TeaDecode(data, key, &result);
            PushString(L, result);
            return 1;
        }
        case Encode::AES128: {
            const std::string key = options["key"];
            const std::string iv = options["iv"];
            if (key.size() != 16) return CustomFalse(L, "Invalid key length (must be 16 characters long)");
            if (iv.size() != 16) return CustomFalse(L, "Invalid iv length (must be 16 characters long)");
            if (fn) {
                PushTask(L, fn, [data, key, iv] {
                    const std::string result = C::Aes128decode(data, key, iv);
                    if (result.empty()) return Outcome{{Bool(false)}, {}, {}};
                    return Outcome{{Str(result)}, {}, {}};
                });
                return PushBool(L, true);
            }
            PushString(L, C::Aes128decode(data, key, iv));
            return 1;
        }
        case Encode::RSA: {
            const std::string key = options["key"];
            if (key.empty()) return CustomFalse(L, "Invalid value for field 'key'");
            if (fn) {
                PushTask(L, fn, [data, key] { return StringOrWarning([&] { return C::RsaDecode(data, key); }); });
                return PushBool(L, true);
            }
            std::string result;
            try {
                result = C::RsaDecode(data, key);
            } catch (const C::CryptoError& ex) {
                return WarningFalse(L, ex.what());
            }
            PushString(L, result);
            return 1;
        }
        case Encode::BASE64:
        case Encode::BASE32: {
            const bool b64 = algorithm == Encode::BASE64;
            const std::string variant = Upper(options["variant"]);
            if (!variant.empty() && variant != (b64 ? "URL" : "HEX")) return CustomFalse(L, "Invalid value for field 'variant'");
            auto body = [data, variant, b64] { return b64 ? C::Base64decode(data, variant) : C::Base32decode(data, variant); };
            if (fn) {
                PushTask(L, fn, [body] { return StringOrWarning(body); });
                return PushBool(L, true);
            }
            std::string result;
            try {
                result = body();
            } catch (const C::CryptoError& ex) {
                return WarningFalse(L, ex.what());
            }
            PushString(L, result);
            return 1;
        }
        case Encode::ZLIB: {
            int format = 0;
            if (!options["format"].empty() && !ReadZLibFormat(options["format"], format))
                return CustomFalse(L, "Not supported value for field 'format'");
            if (fn) {
                PushTask(L, fn, [data, format] {
                    std::string output;
                    const int result = C::ZLibUncompress(data, output, format);
                    if (result == Z_STREAM_END) return Outcome{{Str(output)}, {}, {}};
                    return Outcome{{Bool(false)}, "zlib error: " + std::to_string(result), {}};
                });
                return PushBool(L, true);
            }
            std::string output;
            const int result = C::ZLibUncompress(data, output, format);
            if (result != Z_STREAM_END) return WarningFalse(L, "zlib error: " + std::to_string(result));
            PushString(L, output);
            return 1;
        }
        default:
            return CustomFalse(L, "Unknown encryption algorithm");
    }
}

// --- passwordHash / passwordVerify -------------------------------------------------

// CLuaFunctionParser reads std::unordered_map<std::string, std::string>: string or number
// keys and values. Anything else is an argument error.
bool ReadParserMap(lua_State* L, int index, StringMap& out) {
    if (!lua_istable(L, index)) return false;
    out.clear();
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        const int keyType = lua_type(L, -2), valueType = lua_type(L, -1);
        if ((keyType != LUA_TSTRING && keyType != LUA_TNUMBER) || (valueType != LUA_TSTRING && valueType != LUA_TNUMBER)) {
            lua_pop(L, 2);
            return false;
        }
        lua_pushvalue(L, -2);  // convert a copy of the key, so lua_next keeps working
        std::string key = lua_tostring(L, -1);
        lua_pop(L, 1);
        size_t length = 0;
        const char* value = lua_tolstring(L, -1, &length);
        out[key] = std::string(value, length);
        lua_pop(L, 1);
    }
    return true;
}

// Optional callback of the ArgumentParser functions: none / nil, or a function.
bool ReadOptionalFunction(lua_State* L, int index, int& fnIndex) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        fnIndex = 0;
        return true;
    }
    if (type != LUA_TFUNCTION) return false;
    fnIndex = index;
    return true;
}

// CLuaCryptDefs::PasswordHash (162-234), ArgumentParserWarn<false>.
int PasswordHash(lua_State* L) {
    std::string password;
    StringMap options;
    int fn = 0;
    if (!ReadString(L, 1, password)) return WarningFalse(L, "Bad argument @ 'passwordHash' [Expected string at argument 1]");
    if (lua_type(L, 2) != LUA_TSTRING || std::strcmp(lua_tostring(L, 2), "bcrypt") != 0)
        return WarningFalse(L, "Bad argument @ 'passwordHash' [Expected password-hash-function at argument 2]");
    if (!ReadParserMap(L, 3, options)) return WarningFalse(L, "Bad argument @ 'passwordHash' [Expected table at argument 3]");
    if (!ReadOptionalFunction(L, 4, fn)) return WarningFalse(L, "Bad argument @ 'passwordHash' [Expected function at argument 4]");

    std::size_t cost = 10;
    if (auto it = options.find("cost"); it != options.end()) {
        auto [ptr, err] = std::from_chars(it->second.data(), it->second.data() + it->second.length(), cost);
        (void)ptr;
        if (err != std::errc{}) return WarningFalse(L, "Invalid value for field 'cost'");
    }
    if (options.find("salt") != options.end())
        LUA_LOGE("WARNING: Custom generated salts are deprecated and will be removed in the future.");
    const std::string salt = options["salt"];

    if (fn) {
        PushTask(L, fn, [password, salt, cost] {
            const std::string hash = C::BcryptHash(password, salt, cost);
            if (hash.empty()) return Outcome{{Bool(false)}, {}, "Invalid value for field 'salt'"};
            return Outcome{{Str(hash)}, {}, {}};
        });
        return PushBool(L, true);
    }
    const std::string hash = C::BcryptHash(password, salt, cost);
    if (hash.empty()) return WarningFalse(L, "Invalid value for field 'salt'");
    PushString(L, hash);
    return 1;
}

// CLuaCryptDefs::PasswordVerify (236-316), CScriptArgReader: errors return nil.
int PasswordVerify(lua_State* L) {
    std::string password, hash;
    StringMap options;
    if (!ReadString(L, 1, password) || !ReadString(L, 2, hash)) {
        LUA_LOGE("Bad argument @ 'passwordVerify' [Expected string at argument %d]", lua_type(L, 1) == LUA_TSTRING || lua_type(L, 1) == LUA_TNUMBER ? 2 : 1);
        lua_pushnil(L);
        return 1;
    }
    int index = 3;
    if (lua_istable(L, index)) ReadStringMap(L, index++, options);
    // NextIsFunction: anything else at this position is ignored, not an error.
    const int fn = lua_type(L, index) == LUA_TFUNCTION ? index : 0;

    if (hash.rfind("$2y$", 0) == 0 || hash.rfind("$2a$", 0) == 0) {
        const bool insecureBcrypt = MapBool(options, "insecureBcrypt", false);
        if (hash.rfind("$2a$", 0) == 0 && !insecureBcrypt) {
            LUA_LOGE("WARNING: Bcrypt 2a prefix is only supported if the 'insecureBcrypt' option is enabled. Consider upgrading to 2y");
            lua_pushnil(L);
            return 1;
        }
        if (!fn) return PushBool(L, C::BcryptVerify(password, hash));
        PushTask(L, fn, [password, hash] { return Outcome{{Bool(C::BcryptVerify(password, hash))}, {}, {}}; });
        return PushBool(L, true);
    }
    LUA_LOGE("WARNING: Passed unknown hash");
    lua_pushnil(L);
    return 1;
}

// --- generateKeyPair ---------------------------------------------------------------

// CLuaCryptDefs::GenerateKeyPair (318-397), ArgumentParser: bad arguments raise a Lua error.
int GenerateKeyPair(lua_State* L) {
    bool raise = false;
    int results = 0;
    {
        StringMap options;
        int fn = 0;
        std::string error;
        unsigned int size = 0;
        if (lua_type(L, 1) != LUA_TSTRING || std::strcmp(lua_tostring(L, 1), "rsa") != 0)
            error = "Bad argument @ 'generateKeyPair' [Expected key-pair-algorithm at argument 1]";
        else if (!ReadParserMap(L, 2, options))
            error = "Bad argument @ 'generateKeyPair' [Expected table at argument 2]";
        else if (!ReadOptionalFunction(L, 3, fn))
            error = "Bad argument @ 'generateKeyPair' [Expected function at argument 3]";
        else {
            // Desktop leaves `size` uninitialised when the field is missing; the phone
            // treats that like an unparsable value.
            auto it = options.find("size");
            uint32_t parsed = 0;
            if (it == options.end() ||
                std::from_chars(it->second.data(), it->second.data() + it->second.length(), parsed).ec != std::errc{})
                error = "Invalid value for field 'size'";
            size = parsed;
        }
        if (!error.empty()) {
            lua_pushlstring(L, error.data(), error.size());
            raise = true;
        } else if (fn) {
            PushTask(L, fn, [size] {
                try {
                    const C::KeyPair pair = C::GenerateRsaKeyPair(size);
                    return Outcome{{Str(pair.privateKey), Str(pair.publicKey)}, {}, {}};
                } catch (const C::CryptoError& ex) {
                    return Outcome{{Bool(false)}, ex.what(), {}};
                }
            });
            results = PushBool(L, true);
        } else {
            try {
                const C::KeyPair pair = C::GenerateRsaKeyPair(size);
                PushString(L, pair.privateKey);
                PushString(L, pair.publicKey);
                results = 2;
            } catch (const C::CryptoError& ex) {
                results = WarningFalse(L, ex.what());
            }
        }
    }
    // Raised after the C++ locals are gone: Lua is built as C and lua_error longjmps.
    if (raise) return lua_error(L);
    return results;
}

}  // namespace

void InstallCryptLua(lua_State* L) {
    // Install runs on a fresh state (as InstallUtilLua assumes), possibly at the old
    // address: references of the previous state are gone with it. Completions of their
    // tasks no longer match g_pending and are ignored.
    g_pending.clear();
    g_L = L;
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"encodeString", EncodeString},
        {"decodeString", DecodeString},
        {"passwordHash", PasswordHash},
        {"passwordVerify", PasswordVerify},
        {"generateKeyPair", GenerateKeyPair},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

void CryptDeliverCompletions() {
    if (!g_L) return;
    std::vector<std::pair<uint64_t, Outcome>> batch;
    {
        Worker& worker = GetWorker();
        std::lock_guard<std::mutex> lock(worker.mutex);
        batch.swap(worker.done);
    }
    lua_State* L = g_L;
    for (auto& item : batch) {
        // A plain reference, not a structured binding: clang 14 (NDK 25) cannot capture one.
        Outcome& outcome = item.second;
        auto found = g_pending.find(item.first);
        if (found == g_pending.end()) continue;  // dropped with its resource
        const Pending pending = std::move(found->second);
        const int ref = pending.ref;
        g_pending.erase(found);
        if (!outcome.warning.empty()) LUA_LOGE("WARNING: %s", outcome.warning.c_str());
        if (!outcome.customError.empty()) LUA_LOGE("%s", outcome.customError.c_str());
        const int base = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        if (!lua_isfunction(L, -1)) {
            lua_settop(L, base);
            continue;
        }
        for (const Value& value : outcome.values) {
            if (value.isString) PushString(L, value.text);
            else lua_pushboolean(L, value.boolean ? 1 : 0);
        }
        // Desktop calls back in the resource's own CLuaMain; here the call runs as that
        // resource, so getThisResource and resource-owned elements work in the callback.
        ActiveVmRunAsOwner(pending.owner, pending.netId, pending.generation, [&] {
            if (lua_pcall(L, static_cast<int>(outcome.values.size()), 0, 0) != LUA_OK) {
                const char* message = lua_tostring(L, -1);
                LUA_LOGE("crypt callback failed: %s", message ? message : "");
            }
        });
        lua_settop(L, base);
    }
}

void CryptDropOwner(const std::string& owner, uint16_t netId, uint32_t generation) {
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        if (it->second.owner == owner && it->second.netId == netId && it->second.generation == generation) {
            if (g_L) luaL_unref(g_L, LUA_REGISTRYINDEX, it->second.ref);
            it = g_pending.erase(it);
        } else {
            ++it;
        }
    }
}

void CryptAbandonAll() {
    if (g_L)
        for (auto& [id, pending] : g_pending) luaL_unref(g_L, LUA_REGISTRYINDEX, pending.ref);
    g_pending.clear();
}

void CryptWaitIdleForTests() {
    Worker& worker = GetWorker();
    std::unique_lock<std::mutex> lock(worker.mutex);
    worker.idle.wait(lock, [&] { return worker.queue.empty() && !worker.busy; });
}

}  // namespace WorldRPG::Script
