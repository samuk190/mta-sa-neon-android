// Asset load, image, replace, shader, and texture queries.
// CLuaEngineDefs.cpp:457-1393, 1499-1567, 2073-2126, 2274-2433, 3517-3548.
// DFF and TXD elements are owned by their resources. GTA applies their
// replacements when it streams the target model.

#include "DesktopLuaString.h"
#include "DxShader.h"
#include "WorldShaderMatches.h"
#include "WorldShaderRuntime.h"
#include "EngineAssets.h"
#include "EngineModelLoader.h"
#include "EngineRenderer.h"
#include "LuaApiExtra.h"
#include "LuaFileApi.h"

#include "LuaVMStub.h"
#include "ModelNames.h"
#include "SampMapParser.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
constexpr std::uint32_t kClothesTexFirst = 30152;
constexpr std::uint32_t kClothesTexLast = 30541;

struct ModelAsset {
    std::uint64_t serial;
    uint32_t generation;
    DffSource dff;
    void* txd;
    std::vector<std::uint8_t> txdBytes;
    bool txdFiltering = true;
};

struct ColAsset {
    uint32_t generation = 0;
    std::vector<std::uint8_t> bytes;
};

struct IfpAsset {
    uint32_t generation = 0;
    std::string blockName;
    std::string blockKey;
    std::vector<std::uint8_t> bytes;
    std::vector<std::string> animNames;
};

struct AnimOverride {
    uint32_t pedId = 0;
    uint32_t ifpId = 0;
    std::string internalBlock;
    std::string internalAnim;
    std::string customBlock;
    std::string customAnim;
};

struct ImgEntry {
    std::uint32_t offsetBlocks = 0;
    std::uint16_t sizeBlocks = 0;
    std::string name;
};

struct ImgAsset {
    uint32_t generation = 0;
    std::filesystem::path filePath;
    std::vector<ImgEntry> entries;
    bool streamed = false;
};

struct ImgDffLink {
    uint32_t imgId = 0;
    std::uint64_t serial = 0;
};

struct ImgTxdLink {
    uint32_t imgId = 0;
    std::uint64_t serial = 0;
    void* txd = nullptr;
};

std::unordered_map<uint32_t, ModelAsset> g_assets;
std::unordered_map<uint32_t, ColAsset> g_colAssets;
std::unordered_map<std::uint32_t, uint32_t> g_colModelLinks;
std::unordered_map<uint32_t, IfpAsset> g_ifpAssets;
std::unordered_map<std::string, uint32_t> g_ifpByBlock;
std::vector<AnimOverride> g_animOverrides;
std::unordered_map<uint32_t, ImgAsset> g_imgAssets;
std::unordered_map<std::uint32_t, ImgDffLink> g_imgDffLinks;
std::unordered_map<std::uint32_t, ImgTxdLink> g_imgTxdLinks;
std::uint64_t g_nextAssetSerial = 1;
bool g_diagColHookLogged = false;
bool g_diagIfpHookLogged = false;

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool ReadBinaryFile(const std::filesystem::path& path, std::vector<std::uint8_t>& out, size_t maxBytes = 64u * 1024u * 1024u) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > maxBytes) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.resize(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    return in.good() || in.gcount() == static_cast<std::streamsize>(size);
}

std::uint32_t ReadLeU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) | (static_cast<std::uint32_t>(p[3]) << 24u);
}

std::uint16_t ReadLeU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) | static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8u);
}

std::string ReadFixedCString(const std::uint8_t* p, size_t cap) {
    size_t len = 0;
    while (len < cap && p[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

void ParseIfpAnimNames(const std::vector<std::uint8_t>& data, std::vector<std::string>& outNames) {
    outNames.clear();
    if (data.size() < 36) return;
    if (std::memcmp(data.data(), "ANP3", 4) == 0) {
        // GTA:SA IFP v3 (ANP3): [4B magic][4B size][24B internalName][4B numAnims]
        // Each animation starts with [24B animName][4B numObjects][4B frameDataSize][4B unknown].
        const std::uint32_t numAnims = ReadLeU32(data.data() + 32);
        if (numAnims == 0 || numAnims > 4096) return;
        size_t pos = 36;
        for (std::uint32_t a = 0; a < numAnims && pos + 36 <= data.size(); ++a) {
            std::string name = ReadFixedCString(data.data() + pos, 24);
            const std::uint32_t numObjects = ReadLeU32(data.data() + pos + 24);
            const std::uint32_t frameDataSize = ReadLeU32(data.data() + pos + 28);
            if (!name.empty()) outNames.push_back(std::move(name));
            pos += 36;
            // Walk objects to reach the next animation header when frame data is present.
            size_t objPos = pos;
            bool ok = true;
            for (std::uint32_t o = 0; o < numObjects; ++o) {
                if (objPos + 44 > data.size()) { ok = false; break; }
                const std::uint32_t frameType = ReadLeU32(data.data() + objPos + 24);
                const std::uint32_t numFrames = ReadLeU32(data.data() + objPos + 28);
                objPos += 44;
                const size_t stride = (frameType == 3) ? 10u : (frameType == 4 ? 16u : 20u);
                const size_t bytes = static_cast<size_t>(numFrames) * stride;
                if (bytes > data.size() - objPos) { ok = false; break; }
                objPos += bytes;
            }
            if (ok) {
                pos = objPos;
            } else if (frameDataSize <= data.size() - pos) {
                pos += frameDataSize;
            } else {
                break;
            }
        }
    } else if (std::memcmp(data.data(), "ANPK", 4) == 0) {
        // GTA3/VC IFP v1 (ANPK): scan chunk headers for NAME chunks.
        size_t pos = 8;
        while (pos + 8 <= data.size()) {
            const std::uint32_t chunkSize = ReadLeU32(data.data() + pos + 4);
            const size_t aligned = (static_cast<size_t>(chunkSize) + 3u) & ~size_t{3u};
            if (aligned > data.size() - (pos + 8)) break;
            if (std::memcmp(data.data() + pos, "NAME", 4) == 0 && chunkSize > 0) {
                std::string name = ReadFixedCString(data.data() + pos + 8, chunkSize);
                if (!name.empty()) outNames.push_back(std::move(name));
            }
            pos += 8 + aligned;
        }
    }
}

void UnlinkImageModels(uint32_t imgId) {
    for (auto it = g_imgDffLinks.begin(); it != g_imgDffLinks.end();) {
        if (it->second.imgId == imgId) {
            NativeRestoreDFF(static_cast<std::uint16_t>(it->first));
            it = g_imgDffLinks.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = g_imgTxdLinks.begin(); it != g_imgTxdLinks.end();) {
        if (it->second.imgId == imgId) {
            if (it->second.txd) NativeReleaseModelAsset(it->second.serial, it->second.txd);
            it = g_imgTxdLinks.erase(it);
        } else {
            ++it;
        }
    }
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
        out = DesktopNumberText(L, index);
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

bool LooksColBytes(std::span<const std::uint8_t> data) {
    return data.size() >= 32 && std::memcmp(data.data(), "COL", 3) == 0 &&
           (data[3] == 'L' || data[3] == '2' || data[3] == '3' || data[3] == '4');
}

bool LooksCol(const std::string& data) {
    return LooksColBytes(std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

bool LooksIfpBytes(std::span<const std::uint8_t> data) {
    return data.size() >= 32 && std::memcmp(data.data(), "ANP", 3) == 0 && (data[3] == 'K' || data[3] == '3');
}

bool LooksIfp(const std::string& data) {
    return LooksIfpBytes(std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

// CLuaEngineDefs::EngineLoadCOL (CLuaEngineDefs.cpp:457-535).
int LoadCOL(lua_State* L) {
    std::string input;
    std::string error;
    if (!ReadScriptString(L, 1, "engineLoadCOL", input, error, false, "")) return FailFalse(L, error, true);
    if (ActiveVmOwnerNet() == 0) return FailFalse(L, "", false);

    std::vector<std::uint8_t> bytes;
    const bool isRaw = LooksCol(input);
    if (isRaw) {
        bytes.assign(reinterpret_cast<const std::uint8_t*>(input.data()),
                     reinterpret_cast<const std::uint8_t*>(input.data()) + input.size());
    } else {
        if (input.size() > 32767)
            return FailFalse(L, Usage("engineLoadCOL", "Error loading COL", "Corrupt COL file data or file path too long"), true);
        std::filesystem::path path;
        if (input.find('\0') != std::string::npos || !ResolveFilePath(input, path))
            return FailFalse(L, Usage("engineLoadCOL", "Bad file path", input), true);
        if (!ReadBinaryFile(path, bytes) || !LooksColBytes(bytes))
            return FailFalse(L, Usage("engineLoadCOL", "Error loading COL", input), true);
    }

    const uint32_t id = CreateResourceElement("col");
    if (!id) return FailFalse(L, Usage("engineLoadCOL", "Error loading COL", "resource element unavailable"), true);
    g_colAssets.emplace(id, ColAsset{ActiveVmOwnerGeneration(), std::move(bytes)});
    PushElement(L, id);
    return 1;
}

int LoadRenderWareAsset(lua_State* L, bool dff, bool filtering = true) {
    const char* function = dff ? "engineLoadDFF" : "engineLoadTXD";
    std::string input;
    std::string error;
    if (!ReadScriptString(L, 1, function, input, error, false, "")) return FailFalse(L, error, true);
    if (ActiveVmOwnerNet() == 0) return FailFalse(L, "", false);

    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
    auto data = dff ? FindDffClump(bytes) : FindTxdDictionary(bytes);
    std::string filename;
    std::vector<std::uint8_t> file;
    if (data.empty()) {
        std::filesystem::path path;
        if (input.find('\0') != std::string::npos || input.size() > 32767 ||
            !(dff ? ResolveModelPath(input, path) : ResolveFilePath(input, path)))
            return FailFalse(L, Usage(function, "Bad file path or invalid RenderWare data",
                                     input.find('\0') == std::string::npos && input.size() <= 32767 ? input : "raw data"), true);
        filename = path.string();
        file = dff ? ReadDffClump(filename.c_str()) : ReadTxdDictionary(filename.c_str());
        if (file.empty()) return FailFalse(L, Usage(function, "Error reading RenderWare file", input), true);
        data = file;
    }

    ModelAsset asset{g_nextAssetSerial++, ActiveVmOwnerGeneration(), {}, nullptr};
    if (dff) {
        // Defer decoding until the streamer knows the target model and its TXDs.
        if (filename.empty()) asset.dff.bytes.assign(data.begin(), data.end());
        else asset.dff.path = std::move(filename);
    } else {
        asset.txd = NativeLoadTXD(data, filtering);
        if (!asset.txd) return FailFalse(L, Usage(function, "Error reading RenderWare texture dictionary",
                                                filename.empty() ? "raw data" : input), true);
        asset.txdBytes.assign(data.begin(), data.end());
        asset.txdFiltering = filtering;
    }
    const uint32_t id = CreateResourceElement(dff ? "dff" : "txd");
    if (!id) {
        if (asset.txd) NativeReleaseModelAsset(asset.serial, asset.txd);
        return FailFalse(L, Usage(function, "Error creating asset element", "resource element unavailable"), true);
    }
    g_assets.emplace(id, std::move(asset));
    PushElement(L, id);
    return 1;
}

int LoadDFF(lua_State* L) {
    return LoadRenderWareAsset(L, true);
}

int LoadTXD(lua_State* L) {
    std::string error;
    bool filtering = true;
    if (!ReadOptionalBool(L, 2, "engineLoadTXD", true, filtering, error)) return FailFalse(L, error, true);
    return LoadRenderWareAsset(L, false, filtering);
}

// CLuaEngineDefs::EngineLoadIFP (CLuaEngineDefs.cpp:950-1048).
int LoadIFP(lua_State* L) {
    std::string input;
    std::string block;
    std::string error;
    if (!ReadScriptString(L, 1, "engineLoadIFP", input, error, false, "")) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 2, "engineLoadIFP", block, error, false, "")) return FailFalse(L, error, true);
    if (block.empty()) return FailFalse(L, Usage("engineLoadIFP", "Error loading IFP", "Empty custom block name"), true);
    if (ActiveVmOwnerNet() == 0) return FailFalse(L, "", false);

    std::vector<std::uint8_t> bytes;
    const bool isRaw = LooksIfp(input);
    if (isRaw) {
        bytes.assign(reinterpret_cast<const std::uint8_t*>(input.data()),
                     reinterpret_cast<const std::uint8_t*>(input.data()) + input.size());
    } else {
        if (input.size() > 32767)
            return FailFalse(L, Usage("engineLoadIFP", "Error loading IFP", "Corrupt IFP file data or file path too long"), true);
        std::filesystem::path path;
        if (input.find('\0') != std::string::npos || !ResolveFilePath(input, path))
            return FailFalse(L, Usage("engineLoadIFP", "Bad file path", input), true);
        if (!ReadBinaryFile(path, bytes) || !LooksIfpBytes(bytes))
            return FailFalse(L, Usage("engineLoadIFP", "Error loading IFP", input), true);
    }

    const std::string blockKey = LowerAscii(block);
    const uint32_t id = CreateResourceElement("ifp");
    if (!id) return FailFalse(L, Usage("engineLoadIFP", "Error loading IFP", "resource element unavailable"), true);
    IfpAsset asset;
    asset.generation = ActiveVmOwnerGeneration();
    asset.blockName = block;
    asset.blockKey = blockKey;
    ParseIfpAnimNames(bytes, asset.animNames);
    asset.bytes = std::move(bytes);
    g_ifpAssets.emplace(id, std::move(asset));
    g_ifpByBlock[blockKey] = id;
    PushElement(L, id);
    return 1;
}

bool ParseImgArchive(const std::filesystem::path& path, std::vector<ImgEntry>& outEntries) {
    // Desktop CClientIMG::Load (CClientIMG.cpp:54-92): GTA:SA IMG v2 ("VER2").
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize < 8) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::uint8_t header[8] = {};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!in || std::memcmp(header, "VER2", 4) != 0) return false;

    const std::uint32_t count = ReadLeU32(header + 4);
    if (count > 65536u) return false;
    const std::uint64_t dirBytes = 8ull + static_cast<std::uint64_t>(count) * 32ull;
    if (dirBytes > fileSize) return false;

    outEntries.clear();
    outEntries.reserve(count);
    std::uint8_t rec[32] = {};
    for (std::uint32_t i = 0; i < count; ++i) {
        in.read(reinterpret_cast<char*>(rec), sizeof(rec));
        if (!in) return false;
        ImgEntry entry;
        entry.offsetBlocks = ReadLeU32(rec);
        const std::uint16_t streamingSize = ReadLeU16(rec + 4);
        const std::uint16_t archiveSize = ReadLeU16(rec + 6);
        entry.sizeBlocks = streamingSize != 0 ? streamingSize : archiveSize;
        entry.name = ReadFixedCString(rec + 8, 24);
        outEntries.push_back(std::move(entry));
    }
    return true;
}

bool ReadImgEntryBytes(const ImgAsset& img, size_t fileId, std::vector<std::uint8_t>& out) {
    if (fileId >= img.entries.size()) return false;
    const ImgEntry& entry = img.entries[fileId];
    const std::uint64_t offsetBytes = static_cast<std::uint64_t>(entry.offsetBlocks) * 2048ull;
    const std::uint64_t readBytes = static_cast<std::uint64_t>(entry.sizeBlocks) * 2048ull;
    if (readBytes == 0 || readBytes > 64ull * 1024ull * 1024ull) return false;
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(img.filePath, ec);
    if (ec || offsetBytes > fileSize || readBytes > fileSize - offsetBytes) return false;
    std::ifstream in(img.filePath, std::ios::binary);
    if (!in) return false;
    in.seekg(static_cast<std::streamoff>(offsetBytes), std::ios::beg);
    out.resize(static_cast<size_t>(readBytes));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(readBytes));
    return !in.fail() && !in.eof();
}

ImgAsset* RequireImgArg(lua_State* L, const char* function, uint32_t& outId) {
    outId = ElementIdOf(L, 1);
    auto it = g_imgAssets.find(outId);
    if (it == g_imgAssets.end()) {
        luaL_error(L, "%s", ParserBad(function, "img", 1, ParserGot(L, 1)).c_str());
        return nullptr;
    }
    return &it->second;
}

size_t ResolveImgFileIdArg(lua_State* L, const ImgAsset& img, int index, const char* function) {
    const int t = lua_type(L, index);
    if (t == LUA_TNUMBER) {
        std::uint32_t idx = 0;
        std::string error;
        if (!ReadParserUint(L, index, function, idx, error)) luaL_error(L, "%s", error.c_str());
        return static_cast<size_t>(idx);
    }
    if (t == LUA_TSTRING) {
        size_t len = 0;
        const char* raw = lua_tolstring(L, index, &len);
        const std::string_view name(raw ? raw : "", len);
        for (size_t i = 0; i < img.entries.size(); ++i) {
            if (img.entries[i].name == name) return i;
        }
        const std::string lower = LowerAscii(name);
        for (size_t i = 0; i < img.entries.size(); ++i) {
            if (LowerAscii(img.entries[i].name) == lower) return i;
        }
        luaL_error(L, "Invalid file name specified (%s)", std::string(name).c_str());
        return 0;
    }
    luaL_error(L, "%s", ParserBad(function, "string or number", index, ParserGot(L, index)).c_str());
    return 0;
}

// CLuaEngineDefs::EngineLoadIMG (CLuaEngineDefs.cpp:1051-1088).
int LoadIMG(lua_State* L) {
    std::string relPath;
    std::string error;
    if (!ReadParserString(L, 1, "engineLoadIMG", relPath, error)) return luaL_error(L, "%s", error.c_str());
    if (ActiveVmOwnerNet() == 0) {
        lua_pushnil(L);
        return 1;
    }
    std::filesystem::path fullPath;
    if (relPath.find('\0') != std::string::npos || !ResolveFilePath(relPath, fullPath))
        return luaL_error(L, "Bad file path");

    ImgAsset img;
    img.generation = ActiveVmOwnerGeneration();
    img.filePath = std::move(fullPath);
    if (!ParseImgArchive(img.filePath, img.entries))
        return luaL_error(L, "Error loading IMG");

    const uint32_t id = CreateResourceElement("img");
    if (!id) return luaL_error(L, "Error loading IMG");
    g_imgAssets.emplace(id, std::move(img));
    PushElement(L, id);
    return 1;
}

int AddImage(lua_State* L) {
    uint32_t imgId = 0;
    ImgAsset* img = RequireImgArg(L, "engineAddImage", imgId);
    if (!img || img->entries.empty() || img->streamed) {
        lua_pushboolean(L, 0);
        return 1;
    }
    img->streamed = true;
    lua_pushboolean(L, 1);
    return 1;
}

int RemoveImage(lua_State* L) {
    uint32_t imgId = 0;
    ImgAsset* img = RequireImgArg(L, "engineRemoveImage", imgId);
    if (!img || !img->streamed) {
        lua_pushboolean(L, 0);
        return 1;
    }
    UnlinkImageModels(imgId);
    img->streamed = false;
    lua_pushboolean(L, 1);
    return 1;
}

int ImageCount(lua_State* L) {
    uint32_t imgId = 0;
    ImgAsset* img = RequireImgArg(L, "engineImageGetFilesCount", imgId);
    lua_pushinteger(L, static_cast<lua_Integer>(img->entries.size()));
    return 1;
}

int ImageFiles(lua_State* L) {
    uint32_t imgId = 0;
    ImgAsset* img = RequireImgArg(L, "engineImageGetFiles", imgId);
    lua_createtable(L, static_cast<int>(img->entries.size()), 0);
    for (size_t i = 0; i < img->entries.size(); ++i) {
        lua_pushlstring(L, img->entries[i].name.data(), img->entries[i].name.size());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

int ImageFile(lua_State* L) {
    uint32_t imgId = 0;
    ImgAsset* img = RequireImgArg(L, "engineImageGetFile", imgId);
    const size_t fileId = ResolveImgFileIdArg(L, *img, 2, "engineImageGetFile");
    std::vector<std::uint8_t> bytes;
    if (!ReadImgEntryBytes(*img, fileId, bytes))
        return luaL_error(L, "Failed to read file. Probably EOF reached, make sure the archieve isn't corrupted.");
    lua_pushlstring(L, reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return 1;
}

int LinkDff(lua_State* L) {
    uint32_t imgId = 0;
    ImgAsset* img = RequireImgArg(L, "engineImageLinkDFF", imgId);
    const size_t fileId = ResolveImgFileIdArg(L, *img, 2, "engineImageLinkDFF");
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadParserUint(L, 3, "engineImageLinkDFF", supplied, error)) return luaL_error(L, "%s", error.c_str());
    std::uint32_t modelId = 0;
    if (!ResolveRuntime(supplied, modelId))
        return luaL_error(L, "Expected a valid model ID, got %u", supplied);
    if (modelId >= kTxdBase)
        return luaL_error(L, "Expected modelid in range 0 - %u, got %u", kTxdBase - 1, modelId);

    std::vector<std::uint8_t> bytes;
    if (!ReadImgEntryBytes(*img, fileId, bytes))
        return luaL_error(L, "Failed to read file. Probably EOF reached, make sure the archieve isn't corrupted.");
    const auto clump = FindDffClump(bytes);
    if (clump.empty())
        return luaL_error(L, "Failed to link file. Make sure the archieve isn't corrupted.");
    if (!img->streamed) {
        lua_pushboolean(L, 0);
        return 1;
    }

    DffSource src;
    src.bytes.assign(clump.begin(), clump.end());
    const std::uint64_t serial = g_nextAssetSerial++;
    (void)NativeReplaceDFF(static_cast<std::uint16_t>(modelId), serial, src, false);
    g_imgDffLinks[modelId] = ImgDffLink{imgId, serial};
    lua_pushboolean(L, 1);
    return 1;
}

int LinkTxd(lua_State* L) {
    uint32_t imgId = 0;
    ImgAsset* img = RequireImgArg(L, "engineImageLinkTXD", imgId);
    const size_t fileId = ResolveImgFileIdArg(L, *img, 2, "engineImageLinkTXD");
    std::uint32_t txdId = 0;
    std::string error;
    if (!ReadParserUint(L, 3, "engineImageLinkTXD", txdId, error)) return luaL_error(L, "%s", error.c_str());
    if (txdId >= kTxdCount)
        return luaL_error(L, "Expected txdid in range 0 - %u, got %u", kTxdCount - 1, txdId);

    std::vector<std::uint8_t> bytes;
    if (!ReadImgEntryBytes(*img, fileId, bytes))
        return luaL_error(L, "Failed to read file. Probably EOF reached, make sure the archieve isn't corrupted.");
    const auto dict = FindTxdDictionary(bytes);
    if (dict.empty())
        return luaL_error(L, "Failed to link file. Make sure the archieve isn't corrupted.");
    if (!img->streamed) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto prev = g_imgTxdLinks.find(txdId);
    if (prev != g_imgTxdLinks.end() && prev->second.txd) {
        NativeReleaseModelAsset(prev->second.serial, prev->second.txd);
    }
    const std::uint64_t serial = g_nextAssetSerial++;
    void* nativeTxd = NativeLoadTXD(dict, true);
    g_imgTxdLinks[txdId] = ImgTxdLink{imgId, serial, nativeTxd};
    lua_pushboolean(L, 1);
    return 1;
}

int RestoreDffImage(lua_State* L) {
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineRestoreDFFImage", supplied, error)) return luaL_error(L, "%s", error.c_str());
    std::uint32_t id = 0;
    if (!ResolveRuntime(supplied, id)) return luaL_error(L, "Expected a valid model ID at argument 1");
    if (id >= kTxdBase) return luaL_error(L, "Expected model ID in range [0-19999] at argument 1");
    auto it = g_imgDffLinks.find(id);
    if (it == g_imgDffLinks.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    NativeRestoreDFF(static_cast<std::uint16_t>(id));
    g_imgDffLinks.erase(it);
    lua_pushboolean(L, 1);
    return 1;
}

int RestoreTxdImage(lua_State* L) {
    std::uint32_t id = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineRestoreTXDImage", id, error)) return luaL_error(L, "%s", error.c_str());
    if (id >= kTxdCount) return luaL_error(L, "Expected TXD ID in range [0-4999] at argument 1");
    (void)kFileIdCount;
    auto it = g_imgTxdLinks.find(id);
    if (it == g_imgTxdLinks.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (it->second.txd) NativeReleaseModelAsset(it->second.serial, it->second.txd);
    g_imgTxdLinks.erase(it);
    lua_pushboolean(L, 1);
    return 1;
}

int ReplaceCol(lua_State* L) {
    const uint32_t colId = ElementIdOf(L, 1);
    if (g_colAssets.find(colId) == g_colAssets.end())
        return FailFalse(L, ScriptBad(L, "engineReplaceCOL", "col-model", 1), true);
    std::string modelText;
    std::string error;
    if (!ReadParserString(L, 2, "engineReplaceCOL", modelText, error)) return FailFalse(L, error, true);
    std::uint32_t id = 0;
    if (!ResolveText(modelText, id) || id >= kTxdBase)
        return FailFalse(L, Usage("engineReplaceCOL", "Could not replace COL for model", modelText), true);
    if (!g_diagColHookLogged) {
        g_diagColHookLogged = true;
        LUA_LOGI("DIAG engineReplaceCOL: CColModel GTA replacement hook pending (stored in asset registry)");
    }
    g_colModelLinks[id] = colId;
    lua_pushboolean(L, 1);
    return 1;
}

int RestoreCol(lua_State* L) {
    std::string text;
    std::string error;
    if (!ReadScriptString(L, 1, "engineRestoreCOL", text, error, false, "")) return FailFalse(L, error, true);
    std::uint32_t id = 0;
    if (!ResolveText(text, id) || id >= kTxdBase) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, g_colModelLinks.erase(id) > 0 ? 1 : 0);
    return 1;
}

int ImportTxd(lua_State* L) {
    constexpr const char* function = "engineImportTXD";
    const auto it = g_assets.find(ElementIdOf(L, 1));
    if (it == g_assets.end() || !it->second.txd) return FailFalse(L, ScriptBad(L, function, "txd", 1), true);
    std::string model, error;
    if (!ReadParserString(L, 2, function, model, error)) return FailFalse(L, error, true);
    std::uint32_t id = 0;
    if (!ResolveText(model, id) || id >= kTxdBase || !NativeImportTXD(id, it->second.serial, it->second.txd))
        return FailFalse(L, Usage(function, "Could not import TXD for model", model), true);
    lua_pushboolean(L, 1);
    return 1;
}

int ReplaceModel(lua_State* L) {
    constexpr const char* function = "engineReplaceModel";
    const auto it = g_assets.find(ElementIdOf(L, 1));
    if (it == g_assets.end() || it->second.txd) return FailFalse(L, ScriptBad(L, function, "dff", 1), true);
    std::string model, error;
    bool alpha = false;
    if (!ReadParserString(L, 2, function, model, error) || !ReadOptionalBool(L, 3, function, false, alpha, error))
        return FailFalse(L, error, true);
    std::uint32_t id = 0;
    if (!ResolveText(model, id) || id >= kTxdBase || !NativeReplaceDFF(id, it->second.serial, it->second.dff, alpha))
        return FailFalse(L, Usage(function, "Could not replace model", model), true);
    lua_pushboolean(L, 1);
    return 1;
}

int RestoreModel(lua_State* L) {
    constexpr const char* function = "engineRestoreModel";
    std::string model, error;
    if (!ReadParserString(L, 1, function, model, error)) return FailFalse(L, error, true);
    std::uint32_t id = 0;
    lua_pushboolean(L, ResolveText(model, id) && id < kTxdBase && NativeRestoreDFF(id));
    return 1;
}

// engineRequestModel ids, by owning resource start. A model still used by an entity
// when its resource stops is freed on a later call (pendingFree).
struct CustomModelEntry {
    uint32_t generation = 0;
    bool nativeAllocated = false;
    std::string kind;
    int parent = 1337;
};
std::unordered_map<int, CustomModelEntry> g_customModels;
std::vector<int> g_pendingFree;

struct CustomTxdEntry {
    uint32_t generation = 0;
    std::string name;
};
std::unordered_map<uint32_t, CustomTxdEntry> g_customTxds;

void RetryPendingFrees() {
    std::erase_if(g_pendingFree, [](int id) { return NativeFreeModel(id); });
}

int AllocateVirtualModelId() {
    // Free custom model ID range below kTxdBase (20000), matching desktop model ID bounds.
    constexpr int kVirtualModelStart = 18000;
    constexpr int kVirtualModelEnd = static_cast<int>(kTxdBase);
    for (int id = kVirtualModelStart; id < kVirtualModelEnd; ++id) {
        if (g_customModels.find(id) == g_customModels.end()) return id;
    }
    return -1;
}

int DefaultParentForModelKind(const std::string& kind) {
    // CLuaEngineDefs::EngineRequestModel (CLuaEngineDefs.cpp:1316-1338)
    if (kind == "ped") return 7;                // male01
    if (kind == "timed-object") return 4715;    // LTSLAsky1_LAn2
    if (kind == "clump") return 3425;           // nt_windmill
    if (kind == "object-damageable") return 994;// lhouse_barrier2
    if (kind == "vehicle") return 400;          // VT_LANDSTAL
    return 1337;                                // BinNt07_LA (object)
}

// CLuaEngineDefs::EngineRequestModel (CLuaEngineDefs.cpp:1289-1361): allocate a free model id
// copied from the requested parent (or desktop default parent for the type).
int RequestModel(lua_State* L) {
    const char* text = lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : nullptr;
    const std::string kind = text ? text : "";
    if (kind != "ped" && kind != "object" && kind != "object-damageable" && kind != "vehicle" && kind != "timed-object" && kind != "clump")
        return FailFalse(L, ScriptBad(L, "engineRequestModel", "client-model-type", 1), true);
    double parent = static_cast<double>(DefaultParentForModelKind(kind));
    std::string error;
    if (!lua_isnoneornil(L, 2) && !ReadBounded(L, 2, "engineRequestModel", 0, static_cast<double>(kTxdBase - 1), parent, error))
        return FailFalse(L, error, true);
    RetryPendingFrees();
    int id = -1;
    bool nativeAllocated = false;
    if (kind == "object" || kind == "object-damageable" || kind == "timed-object" || kind == "clump") {
        id = NativeAllocateObjectModel(static_cast<int>(parent));
        if (id >= 0) nativeAllocated = true;
    }
    if (id < 0) {
        id = AllocateVirtualModelId();
        if (id < 0) {
            lua_pushboolean(L, 0);
            return 1;
        }
        LUA_LOGI("DIAG engineRequestModel: allocated tracked %s model id=%d (parent=%d; native CModelInfo::Make* pending)",
                 kind.c_str(), id, static_cast<int>(parent));
    }
    g_customModels[id] = CustomModelEntry{ActiveVmOwnerGeneration(), nativeAllocated, kind, static_cast<int>(parent)};
    lua_pushinteger(L, id);
    return 1;
}

int FreeModel(lua_State* L) {
    double model = 0;
    std::string error;
    if (!ReadBounded(L, 1, "engineFreeModel", 0, static_cast<double>(kTxdBase - 1), model, error)) return FailFalse(L, error, true);
    const int id = static_cast<int>(model);
    const auto found = g_customModels.find(id);
    if (found == g_customModels.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool nativeAllocated = found->second.nativeAllocated;
    g_customModels.erase(found);
    if (nativeAllocated && !NativeFreeModel(id)) g_pendingFree.push_back(id);
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaEngineDefs::EngineRequestTXD (CLuaEngineDefs.cpp:3517-3533)
int RequestTxd(lua_State* L) {
    std::string name;
    std::string error;
    if (!ReadParserString(L, 1, "engineRequestTXD", name, error)) return luaL_error(L, "%s", error.c_str());
    if (name.size() > 24) return luaL_error(L, "TXD name length shoudn't be more than 24 characters");
    // Allocate from high TXD slots [4000..4999] within desktop [0..4999] range.
    constexpr uint32_t kCustomTxdStart = 4000;
    for (uint32_t slot = kCustomTxdStart; slot < kTxdCount; ++slot) {
        if (g_customTxds.find(slot) == g_customTxds.end()) {
            g_customTxds[slot] = CustomTxdEntry{ActiveVmOwnerGeneration(), name};
            lua_pushnumber(L, static_cast<double>(slot));
            return 1;
        }
    }
    LUA_LOGI("DIAG engineRequestTXD: no free TXD slot available");
    lua_pushnumber(L, 0);
    return 1;
}

// CLuaEngineDefs::EngineFreeTXD (CLuaEngineDefs.cpp:3535-3549)
int FreeTxd(lua_State* L) {
    std::uint32_t id = 0;
    std::string error;
    if (!ReadParserUint(L, 1, "engineFreeTXD", id, error)) return luaL_error(L, "%s", error.c_str());
    if (id >= kTxdCount) return luaL_error(L, "Expected TXD ID in range [0-4999] at argument 1");
    const auto found = g_customTxds.find(id);
    if (found == g_customTxds.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_customTxds.erase(found);
    lua_pushboolean(L, 1);
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
    const uint32_t pedId = ElementIdOf(L, 1);
    if (pedId == 0) return FailFalse(L, ScriptBad(L, "engineReplaceAnimation", "element", 1), true);
    std::string internalBlock;
    std::string internalAnim;
    std::string customBlock;
    std::string customAnim;
    std::string error;
    if (!ReadScriptString(L, 2, "engineReplaceAnimation", internalBlock, error, false, "")) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 3, "engineReplaceAnimation", internalAnim, error, false, "")) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 4, "engineReplaceAnimation", customBlock, error, false, "")) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 5, "engineReplaceAnimation", customAnim, error, false, "")) return FailFalse(L, error, true);

    const auto blockIt = g_ifpByBlock.find(LowerAscii(customBlock));
    if (blockIt == g_ifpByBlock.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto ifpIt = g_ifpAssets.find(blockIt->second);
    if (ifpIt == g_ifpAssets.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!ifpIt->second.animNames.empty()) {
        const std::string wantAnim = LowerAscii(customAnim);
        const bool found = std::any_of(ifpIt->second.animNames.begin(), ifpIt->second.animNames.end(),
                                       [&](const std::string& n) { return LowerAscii(n) == wantAnim; });
        if (!found) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    if (!g_diagIfpHookLogged) {
        g_diagIfpHookLogged = true;
        LUA_LOGI("DIAG engineReplaceAnimation: CAnimManager GTA override hook pending (stored in IFP registry)");
    }
    const std::string ibKey = LowerAscii(internalBlock);
    const std::string iaKey = LowerAscii(internalAnim);
    for (auto& existing : g_animOverrides) {
        if (existing.pedId == pedId && existing.internalBlock == ibKey && existing.internalAnim == iaKey) {
            existing.ifpId = ifpIt->first;
            existing.customBlock = ifpIt->second.blockKey;
            existing.customAnim = LowerAscii(customAnim);
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    g_animOverrides.push_back(AnimOverride{pedId, ifpIt->first, ibKey, iaKey, ifpIt->second.blockKey, LowerAscii(customAnim)});
    lua_pushboolean(L, 1);
    return 1;
}

int RestoreAnim(lua_State* L) {
    const uint32_t pedId = ElementIdOf(L, 1);
    if (pedId == 0) return FailFalse(L, ScriptBad(L, "engineRestoreAnimation", "element", 1), true);
    std::string internalBlock;
    std::string internalAnim;
    std::string error;
    const int second = lua_type(L, 2);
    if (second != LUA_TNONE && second != LUA_TNIL) {
        if (!ReadScriptString(L, 2, "engineRestoreAnimation", internalBlock, error, false, "")) return FailFalse(L, error, true);
        const int third = lua_type(L, 3);
        if (third != LUA_TNONE && third != LUA_TNIL) {
            if (!ReadScriptString(L, 3, "engineRestoreAnimation", internalAnim, error, false, "")) return FailFalse(L, error, true);
        }
    }
    const std::string ibKey = LowerAscii(internalBlock);
    const std::string iaKey = LowerAscii(internalAnim);
    const size_t before = g_animOverrides.size();
    std::erase_if(g_animOverrides, [&](const AnimOverride& ov) {
        if (ov.pedId != pedId) return false;
        if (!ibKey.empty() && ov.internalBlock != ibKey) return false;
        if (!iaKey.empty() && ov.internalAnim != iaKey) return false;
        return true;
    });
    lua_pushboolean(L, g_animOverrides.size() < before ? 1 : 0);
    return 1;
}

// ReadUserData(CClientShader*): a dxCreateShader element.
bool ReadShaderElement(lua_State* L, const char* function, WorldShaders::ShaderInfo& info, std::string& error) {
    if (lua_istable(L, 1) && DxShader::WorldInfo(ElementIdOf(L, 1), info)) return true;
    error = ScriptBad(L, function, "shader", 1);
    return false;
}

// ReadUserData(CClientEntity*, NULL): nil or none is every entity.
bool ReadOptionalEntity(lua_State* L, const char* function, uint32_t& entity, std::string& error) {
    entity = 0;
    const int type = lua_type(L, 3);
    if (type == LUA_TNONE || type == LUA_TNIL) return true;
    if (type == LUA_TTABLE) {
        entity = ElementIdOf(L, 3);
        if (entity && KnownClientElement(entity)) return true;
    }
    error = ScriptBad(L, function, "element", 3);
    return false;
}

// Desktop drops an entity's matches when it is destroyed (RemoveClientEntityRefs).
// The phone checks the entities that have matches whenever matches change.
void DropDestroyedMatchEntities() {
    for (uint32_t entity : WorldShaders::KnownEntities())
        if (!KnownClientElement(entity)) WorldShaders::RemoveEntityRefs(entity);
}

bool g_diagWorldShaderDraw = false;

// CLuaEngineDefs::EngineApplyShaderToWorldTexture (CLuaEngineDefs.cpp) +
// CRenderItemManager::ApplyShaderItemToWorldTexture (CRenderItemManager.TextureReplace.cpp:95-102):
// records an additive match and returns true.
int ApplyShader(lua_State* L) {
    WorldShaders::ShaderInfo info;
    std::string texture;
    std::string error;
    uint32_t entity = 0;
    bool append = true;
    if (!ReadShaderElement(L, "engineApplyShaderToWorldTexture", info, error)) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 2, "engineApplyShaderToWorldTexture", texture, error, false, "")) return FailFalse(L, error, true);
    if (!ReadOptionalEntity(L, "engineApplyShaderToWorldTexture", entity, error)) return FailFalse(L, error, true);
    if (!ReadOptionalBool(L, 4, "engineApplyShaderToWorldTexture", true, append, error)) return FailFalse(L, error, true);
    DropDestroyedMatchEntities();
    WorldShaders::AppendAdditive(info, entity, texture, append);
    if (!g_diagWorldShaderDraw && !WorldShaderRuntime::IsNativeDrawHookInstalled()) {
        g_diagWorldShaderDraw = true;
        LUA_LOGI("DIAG engineApplyShaderToWorldTexture: matches and FxWorld GLES2 executor ready; GTA inline draw hook pending");
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaEngineDefs::EngineRemoveShaderFromWorldTexture +
// CRenderItemManager::RemoveShaderItemFromWorldTexture (111-116): a subtractive match, true.
int RemoveShader(lua_State* L) {
    WorldShaders::ShaderInfo info;
    std::string texture;
    std::string error;
    uint32_t entity = 0;
    if (!ReadShaderElement(L, "engineRemoveShaderFromWorldTexture", info, error)) return FailFalse(L, error, true);
    if (!ReadScriptString(L, 2, "engineRemoveShaderFromWorldTexture", texture, error, false, "")) return FailFalse(L, error, true);
    if (!ReadOptionalEntity(L, "engineRemoveShaderFromWorldTexture", entity, error)) return FailFalse(L, error, true);
    DropDestroyedMatchEntities();
    WorldShaders::AppendSubtractive(info.shader, entity, texture);
    lua_pushboolean(L, 1);
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
    (void)id;
    const auto names = WorldShaderRuntime::VisibleTextureNames(match);
    lua_createtable(L, static_cast<int>(names.size()), 0);
    for (size_t i = 0; i < names.size(); ++i) {
        lua_pushlstring(L, names[i].data(), names[i].size());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
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

bool CopyTxdDataForRadar(uint32_t id, uint32_t expectedGeneration, std::vector<uint8_t>& outBytes, bool& outFiltering) {
    const auto it = g_assets.find(id);
    if (it == g_assets.end() || !it->second.txd || it->second.txdBytes.empty()) return false;
    if (expectedGeneration == 0 || it->second.generation != expectedGeneration) return false;
    outBytes = it->second.txdBytes;
    outFiltering = it->second.txdFiltering;
    return true;
}

void ReleaseModelAsset(uint32_t id) {
    RemoveRadarMapTilesForSource(id);
    if (const auto it = g_assets.find(id); it != g_assets.end()) {
        NativeReleaseModelAsset(it->second.serial, it->second.txd);
        g_assets.erase(it);
    }
    if (g_colAssets.erase(id) > 0) {
        for (auto it = g_colModelLinks.begin(); it != g_colModelLinks.end();)
            it = (it->second == id) ? g_colModelLinks.erase(it) : std::next(it);
    }
    if (const auto it = g_ifpAssets.find(id); it != g_ifpAssets.end()) {
        auto byBlock = g_ifpByBlock.find(it->second.blockKey);
        if (byBlock != g_ifpByBlock.end() && byBlock->second == id) g_ifpByBlock.erase(byBlock);
        std::erase_if(g_animOverrides, [id](const AnimOverride& ov) { return ov.ifpId == id; });
        g_ifpAssets.erase(it);
    }
    if (g_imgAssets.erase(id) > 0) {
        UnlinkImageModels(id);
    }
}

void DropResourceModelAssets(uint32_t generation) {
    RemoveRadarMapTilesForOwner(generation);
    std::vector<uint32_t> owned;
    for (const auto& [id, asset] : g_assets)
        if (asset.generation == generation) owned.push_back(id);
    for (const auto& [id, asset] : g_colAssets)
        if (asset.generation == generation) owned.push_back(id);
    for (const auto& [id, asset] : g_ifpAssets)
        if (asset.generation == generation) owned.push_back(id);
    for (const auto& [id, asset] : g_imgAssets)
        if (asset.generation == generation) owned.push_back(id);
    for (uint32_t id : owned)
        if (!DestroyClientElementForced(id)) ReleaseModelAsset(id);
    // Its requested models and TXD slots go too; one an entity still uses is retried later.
    for (auto it = g_customModels.begin(); it != g_customModels.end();) {
        if (it->second.generation != generation) {
            ++it;
            continue;
        }
        if (it->second.nativeAllocated && !NativeFreeModel(it->first)) g_pendingFree.push_back(it->first);
        it = g_customModels.erase(it);
    }
    for (auto it = g_customTxds.begin(); it != g_customTxds.end();) {
        if (it->second.generation == generation) it = g_customTxds.erase(it);
        else ++it;
    }
}

bool CustomModelAllocated(uint32_t model) { return g_customModels.count(static_cast<int>(model)) > 0; }

void ResetModelAssets() {
    ResetRadarMapTiles();
    for (const auto& [id, asset] : g_assets) NativeReleaseModelAsset(asset.serial, asset.txd);
    g_assets.clear();
    g_colAssets.clear();
    g_colModelLinks.clear();
    g_ifpAssets.clear();
    g_ifpByBlock.clear();
    g_animOverrides.clear();
    for (const auto& [modelId, link] : g_imgDffLinks) {
        (void)link;
        NativeRestoreDFF(static_cast<std::uint16_t>(modelId));
    }
    g_imgDffLinks.clear();
    for (const auto& [txdId, link] : g_imgTxdLinks) {
        (void)txdId;
        if (link.txd) NativeReleaseModelAsset(link.serial, link.txd);
    }
    g_imgTxdLinks.clear();
    g_imgAssets.clear();
    for (const auto& [id, entry] : g_customModels)
        if (entry.nativeAllocated && !NativeFreeModel(id)) g_pendingFree.push_back(id);
    g_customModels.clear();
    g_customTxds.clear();
}

void BindEngineAssetLua(lua_State* L) {
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
