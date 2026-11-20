#include "LuaUtilApi.h"
#include "LuaCryptApi.h"
#include "LuaMtaWindowApi.h"

#include "LuaVMStub.h"
#include "Checksums.h"
#include "ResourceLoader.h"
#include "../net/RemoteCalls.h"

#include "lua.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace WorldRPG::Script {
namespace {

bool Number(lua_State* L, int index, double& out) {
    if (!lua_isnumber(L, index)) return false;
    out = lua_tonumber(L, index);
    return std::isfinite(out);
}

double Ease(const char* name, double t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    if (!name || std::strcmp(name, "Linear") == 0) return t;
    if (std::strcmp(name, "InQuad") == 0) return t * t;
    if (std::strcmp(name, "OutQuad") == 0) return t * (2.0 - t);
    if (std::strcmp(name, "InOutQuad") == 0) return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
    if (std::strcmp(name, "InCubic") == 0) return t * t * t;
    if (std::strcmp(name, "OutCubic") == 0) { const double u = t - 1.0; return u * u * u + 1.0; }
    if (std::strcmp(name, "InOutCubic") == 0)
        return t < 0.5 ? 4.0 * t * t * t : (t - 1.0) * (2.0 * t - 2.0) * (2.0 * t - 2.0) + 1.0;
    if (std::strcmp(name, "InSine") == 0) return 1.0 - std::cos(t * 1.5707963267948966);
    if (std::strcmp(name, "OutSine") == 0) return std::sin(t * 1.5707963267948966);
    if (std::strcmp(name, "InOutSine") == 0) return -0.5 * (std::cos(3.141592653589793 * t) - 1.0);
    if (std::strcmp(name, "OutBounce") == 0) {
        if (t < 1.0 / 2.75) return 7.5625 * t * t;
        if (t < 2.0 / 2.75) { t -= 1.5 / 2.75; return 7.5625 * t * t + 0.75; }
        if (t < 2.5 / 2.75) { t -= 2.25 / 2.75; return 7.5625 * t * t + 0.9375; }
        t -= 2.625 / 2.75;
        return 7.5625 * t * t + 0.984375;
    }
    return std::nan("");
}

int l_getEasingValue(lua_State* L) {
    double progress = 0;
    const char* name = lua_tostring(L, 2);
    if (!Number(L, 1, progress) || !name) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const double value = Ease(name, progress);
    if (!std::isfinite(value)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, value);
    return 1;
}

int l_interpolateBetween(lua_State* L) {
    double from[3] = {}, to[3] = {}, progress = 0;
    const char* name = lua_tostring(L, 8);
    for (int i = 0; i < 3; ++i) {
        if (!Number(L, i + 1, from[i]) || !Number(L, i + 4, to[i])) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    if (!Number(L, 7, progress) || !name) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const double eased = Ease(name, progress);
    if (!std::isfinite(eased)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (int i = 0; i < 3; ++i) lua_pushnumber(L, from[i] + (to[i] - from[i]) * eased);
    return 3;
}

int l_getRealTime(lua_State* L) {
    std::time_t stamp = std::time(nullptr);
    if (!lua_isnoneornil(L, 1)) {
        double seconds = 0;
        if (!Number(L, 1, seconds) || seconds < 0) {
            lua_pushboolean(L, 0);
            return 1;
        }
        stamp = static_cast<std::time_t>(seconds);
    }
    const bool utc = lua_isboolean(L, 2) && lua_toboolean(L, 2) == 0;
    std::tm parts{};
#if defined(_WIN32)
    utc ? gmtime_s(&parts, &stamp) : localtime_s(&parts, &stamp);
#else
    utc ? gmtime_r(&stamp, &parts) : localtime_r(&stamp, &parts);
#endif
    lua_newtable(L);
    lua_pushinteger(L, parts.tm_year); lua_setfield(L, -2, "year");
    lua_pushinteger(L, parts.tm_mon); lua_setfield(L, -2, "month");
    lua_pushinteger(L, parts.tm_mday); lua_setfield(L, -2, "monthday");
    lua_pushinteger(L, parts.tm_hour); lua_setfield(L, -2, "hour");
    lua_pushinteger(L, parts.tm_min); lua_setfield(L, -2, "minute");
    lua_pushinteger(L, parts.tm_sec); lua_setfield(L, -2, "second");
    lua_pushinteger(L, parts.tm_wday); lua_setfield(L, -2, "weekday");
    lua_pushinteger(L, parts.tm_yday); lua_setfield(L, -2, "yearday");
    lua_pushinteger(L, static_cast<lua_Integer>(stamp)); lua_setfield(L, -2, "timestamp");
    lua_pushinteger(L, parts.tm_isdst); lua_setfield(L, -2, "isdst");
    return 1;
}

int l_split(lua_State* L) {
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    size_t delimLength = 0;
    const char* delim = lua_tolstring(L, 2, &delimLength);
    if (!text || !delim) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_newtable(L);
    int index = 0;
    if (delimLength == 0) {
        lua_pushinteger(L, 1);
        lua_pushlstring(L, text, length);
        lua_settable(L, -3);
        return 1;
    }
    const std::string source(text, length);
    const std::string needle(delim, delimLength);
    std::size_t start = 0;
    while (start <= source.size()) {
        const std::size_t found = source.find(needle, start);
        const std::size_t end = found == std::string::npos ? source.size() : found;
        lua_pushinteger(L, ++index);
        lua_pushlstring(L, source.data() + start, end - start);
        lua_settable(L, -3);
        if (found == std::string::npos) break;
        start = found + needle.size();
    }
    return 1;
}

int Hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

int l_getColorFromString(lua_State* L) {
    const char* text = lua_tostring(L, 1);
    if (!text || text[0] != '#') {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int count = static_cast<int>(std::strlen(text + 1));
    int digits[8] = {};
    if (count != 3 && count != 6 && count != 8) {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (int i = 0; i < count; ++i) {
        digits[i] = Hex(text[i + 1]);
        if (digits[i] < 0) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    auto channel = [&](int index, int width) {
        return width == 1 ? digits[index] * 17 : digits[index] * 16 + digits[index + 1];
    };
    if (count == 8) {
        lua_pushinteger(L, channel(2, 2));
        lua_pushinteger(L, channel(4, 2));
        lua_pushinteger(L, channel(6, 2));
        lua_pushinteger(L, channel(0, 2));
        return 4;
    }
    const int width = count == 3 ? 1 : 2;
    lua_pushinteger(L, channel(0, width));
    lua_pushinteger(L, channel(width, width));
    lua_pushinteger(L, channel(width * 2, width));
    return 3;
}

int CodePoints(const std::string& text, std::vector<std::uint32_t>& codes, std::vector<std::size_t>& offsets) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        int need = 1;
        std::uint32_t code = lead;
        if ((lead & 0x80) == 0) need = 1;
        else if ((lead & 0xE0) == 0xC0) { need = 2; code = lead & 0x1F; }
        else if ((lead & 0xF0) == 0xE0) { need = 3; code = lead & 0x0F; }
        else if ((lead & 0xF8) == 0xF0) { need = 4; code = lead & 0x07; }
        else return -1;
        if (i + static_cast<std::size_t>(need) > text.size()) return -1;
        for (int extra = 1; extra < need; ++extra) {
            const unsigned char next = static_cast<unsigned char>(text[i + extra]);
            if ((next & 0xC0) != 0x80) return -1;
            code = (code << 6) | (next & 0x3F);
        }
        offsets.push_back(i);
        codes.push_back(code);
        i += static_cast<std::size_t>(need);
    }
    offsets.push_back(text.size());
    return static_cast<int>(codes.size());
}

int IndexOf(int index, int count) {
    if (index == 0) return -1;
    if (index < 0) index = count + index + 1;
    if (index < 1 || index > count) return -1;
    return index - 1;
}

int l_utfLen(lua_State* L) {
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::vector<std::uint32_t> codes;
    std::vector<std::size_t> offsets;
    const int count = CodePoints(std::string(text, length), codes, offsets);
    if (count < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, count);
    return 1;
}

int l_utfSub(lua_State* L) {
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::vector<std::uint32_t> codes;
    std::vector<std::size_t> offsets;
    const int count = CodePoints(std::string(text, length), codes, offsets);
    const int start = IndexOf(static_cast<int>(lua_tointeger(L, 2)), count);
    int finish = count - 1;
    if (!lua_isnoneornil(L, 3)) finish = IndexOf(static_cast<int>(lua_tointeger(L, 3)), count);
    if (count < 0 || start < 0 || finish < start) {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushlstring(L, text + offsets[static_cast<std::size_t>(start)],
                    offsets[static_cast<std::size_t>(finish) + 1] - offsets[static_cast<std::size_t>(start)]);
    return 1;
}

void PushUtf8(lua_State* L, std::uint32_t code) {
    char bytes[5] = {};
    if (code <= 0x7F) {
        bytes[0] = static_cast<char>(code);
        lua_pushlstring(L, bytes, 1);
    } else if (code <= 0x7FF) {
        bytes[0] = static_cast<char>(0xC0 | (code >> 6));
        bytes[1] = static_cast<char>(0x80 | (code & 0x3F));
        lua_pushlstring(L, bytes, 2);
    } else if (code <= 0xFFFF) {
        bytes[0] = static_cast<char>(0xE0 | (code >> 12));
        bytes[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | (code & 0x3F));
        lua_pushlstring(L, bytes, 3);
    } else {
        bytes[0] = static_cast<char>(0xF0 | (code >> 18));
        bytes[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        bytes[3] = static_cast<char>(0x80 | (code & 0x3F));
        lua_pushlstring(L, bytes, 4);
    }
}

int l_utfChar(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto code = static_cast<std::uint32_t>(lua_tointeger(L, 1));
    if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    PushUtf8(L, code);
    return 1;
}

int l_utfCode(lua_State* L) {
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::vector<std::uint32_t> codes;
    std::vector<std::size_t> offsets;
    const int count = CodePoints(std::string(text, length), codes, offsets);
    int index = 1;
    if (!lua_isnoneornil(L, 2)) {
        if (!lua_isnumber(L, 2)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        index = static_cast<int>(lua_tointeger(L, 2));
    }
    const int at = IndexOf(index, count);
    if (count < 0 || at < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, codes[static_cast<std::size_t>(at)]);
    return 1;
}

int l_utfSeek(lua_State* L) {
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::vector<std::uint32_t> codes;
    std::vector<std::size_t> offsets;
    const int count = CodePoints(std::string(text, length), codes, offsets);
    const int position = static_cast<int>(lua_tointeger(L, 2));
    if (count < 0 || position < 0 || position > count) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(offsets[static_cast<std::size_t>(position)]));
    return 1;
}

int l_getVersion(lua_State* L) {
    lua_newtable(L);
    lua_pushstring(L, "1.6"); lua_setfield(L, -2, "number");
    lua_pushstring(L, "1.6"); lua_setfield(L, -2, "mta");
    lua_pushstring(L, "MTA:SA Client"); lua_setfield(L, -2, "name");
    lua_pushinteger(L, 1); lua_setfield(L, -2, "netcode");
    lua_pushstring(L, "Android"); lua_setfield(L, -2, "os");
    lua_pushstring(L, "Custom"); lua_setfield(L, -2, "type");
    lua_pushstring(L, "Android"); lua_setfield(L, -2, "tag");
    return 1;
}

const char* kWeapons[] = {
    "Fist", "Brassknuckle", "Golfclub", "Nightstick", "Knife", "Bat", "Shovel", "Poolstick",
    "Katana", "Chainsaw", "Dildo", "Dildo", "Vibrator", "Vibrator", "Flower", "Cane",
    "Grenade", "Teargas", "Molotov", "", "", "", "Colt 45", "Silenced", "Deagle", "Shotgun",
    "Sawed-off", "Combat Shotgun", "Uzi", "MP5", "AK-47", "M4", "Tec-9", "Rifle", "Sniper",
    "Rocket Launcher", "Rocket Launcher HS", "Flamethrower", "Minigun", "Satchel", "Bomb",
    "Spraycan", "Fire Extinguisher", "Camera", "Nightvision", "Infrared", "Parachute",
};

int SlotOf(int id) {
    if (id == 0) return 0;
    if (id >= 1 && id <= 15) return 1;
    if (id >= 16 && id <= 18) return 8;
    if (id >= 22 && id <= 24) return 2;
    if (id >= 25 && id <= 27) return 3;
    if (id == 28 || id == 29 || id == 32) return 4;
    if (id == 30 || id == 31) return 5;
    if (id == 33 || id == 34) return 6;
    if (id >= 35 && id <= 38) return 7;
    if (id == 39 || id == 40) return 8;
    if (id >= 41 && id <= 43) return 9;
    if (id == 44 || id == 45) return 11;
    if (id == 46) return 12;
    return -1;
}

int l_getWeaponNameFromID(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int id = static_cast<int>(lua_tointeger(L, 1));
    if (id < 0 || id >= static_cast<int>(sizeof(kWeapons) / sizeof(kWeapons[0])) || kWeapons[id][0] == '\0') {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, kWeapons[id]);
    return 1;
}

int l_getWeaponIDFromName(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    if (!name) {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (int id = 0; id < static_cast<int>(sizeof(kWeapons) / sizeof(kWeapons[0])); ++id) {
        if (kWeapons[id][0] != '\0' && std::strcmp(kWeapons[id], name) == 0) {
            lua_pushinteger(L, id);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_getSlotFromWeapon(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int slot = SlotOf(static_cast<int>(lua_tointeger(L, 1)));
    if (slot < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, slot);
    return 1;
}

std::string Md5Hex(const std::string& text) {
    WorldRPG::Net::Md5 md5;
    md5.Update(text.data(), text.size());
    std::uint8_t digest[16];
    md5.Finish(digest);
    return WorldRPG::Net::ToHex(digest, sizeof(digest));
}

std::string Sha256Hex(const std::string& text) {
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    auto rotr = [](std::uint32_t x, std::uint32_t n) { return (x >> n) | (x << (32 - n)); };
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string msg = text;
    const std::uint64_t bits = static_cast<std::uint64_t>(text.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while ((msg.size() % 64) != 56) msg.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) msg.push_back(static_cast<char>((bits >> shift) & 0xff));
    for (std::size_t offset = 0; offset < msg.size(); offset += 64) {
        std::uint32_t w[64] = {};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(static_cast<unsigned char>(msg[offset + i * 4])) << 24) |
                   (static_cast<std::uint32_t>(static_cast<unsigned char>(msg[offset + i * 4 + 1])) << 16) |
                   (static_cast<std::uint32_t>(static_cast<unsigned char>(msg[offset + i * 4 + 2])) << 8) |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(msg[offset + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::string hex;
    hex.reserve(64);
    for (std::uint32_t word : h) {
        for (int shift = 28; shift >= 0; shift -= 4) hex.push_back("0123456789abcdef"[(word >> shift) & 0xf]);
    }
    return hex;
}

std::string BytesOf(lua_State* L, int index) {
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    return text ? std::string(text, length) : std::string();
}

int l_md5(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, Md5Hex(BytesOf(L, 1)).c_str());
    return 1;
}

int l_sha256(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, Sha256Hex(BytesOf(L, 1)).c_str());
    return 1;
}

int l_hash(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    if (!name || !lua_isstring(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string data = BytesOf(L, 2);
    if (std::strcmp(name, "md5") == 0) lua_pushstring(L, Md5Hex(data).c_str());
    else if (std::strcmp(name, "sha256") == 0) lua_pushstring(L, Sha256Hex(data).c_str());
    else {
        LUA_LOGE("hash: unknown algorithm");
        lua_pushboolean(L, 0);
    }
    return 1;
}

std::string Base64Encode(const std::string& data, bool url) {
    static const char kStd[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const char kUrl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const char* alphabet = url ? kUrl : kStd;
    std::string out;
    std::uint32_t block = 0;
    int bits = 0;
    for (unsigned char byte : data) {
        block = (block << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(alphabet[(block >> bits) & 63]);
        }
    }
    if (bits > 0) out.push_back(alphabet[(block << (6 - bits)) & 63]);
    if (!url) while (out.size() % 4) out.push_back('=');
    return out;
}

int Base64Value(char ch, bool url) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == (url ? '-' : '+')) return 62;
    if (ch == (url ? '_' : '/')) return 63;
    return -1;
}

std::string Base64Decode(const std::string& data, bool url) {
    std::string out;
    std::uint32_t block = 0;
    int bits = 0;
    for (char ch : data) {
        if (ch == '=' || ch == '\n' || ch == '\r') continue;
        const int value = Base64Value(ch, url);
        if (value < 0) continue;
        block = (block << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((block >> bits) & 0xff));
        }
    }
    return out;
}

int l_base64Encode(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* variant = lua_tostring(L, 2);
    lua_pushstring(L, Base64Encode(BytesOf(L, 1), variant && std::strcmp(variant, "URL") == 0).c_str());
    return 1;
}

int l_base64Decode(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* variant = lua_tostring(L, 2);
    const std::string decoded = Base64Decode(BytesOf(L, 1), variant && std::strcmp(variant, "URL") == 0);
    lua_pushlstring(L, decoded.data(), decoded.size());
    return 1;
}

void XteaEncode(std::uint32_t& v0, std::uint32_t& v1, const std::uint32_t k[4]) {
    std::uint32_t sum = 0;
    for (int i = 0; i < 32; ++i) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]);
        sum += 0x9E3779B9u;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]);
    }
}

void XteaDecode(std::uint32_t& v0, std::uint32_t& v1, const std::uint32_t k[4]) {
    std::uint32_t sum = 0xC6EF3720u;
    for (int i = 0; i < 32; ++i) {
        v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]);
        sum -= 0x9E3779B9u;
        v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]);
    }
}

void TeaKey(const std::string& key, std::uint32_t k[4]) {
    unsigned char raw[16] = {};
    const std::size_t length = std::min<std::size_t>(key.size(), 16);
    std::memcpy(raw, key.data(), length);
    for (int i = 0; i < 4; ++i)
        k[i] = static_cast<std::uint32_t>(raw[i * 4]) | (static_cast<std::uint32_t>(raw[i * 4 + 1]) << 8) |
               (static_cast<std::uint32_t>(raw[i * 4 + 2]) << 16) | (static_cast<std::uint32_t>(raw[i * 4 + 3]) << 24);
}

std::uint32_t LoadLe(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void StoreLe(std::string& out, std::uint32_t word) {
    char bytes[4] = {static_cast<char>(word), static_cast<char>(word >> 8), static_cast<char>(word >> 16), static_cast<char>(word >> 24)};
    out.append(bytes, 4);
}

std::string TeaEncodeRaw(const std::string& text, const std::string& key) {
    if (text.empty()) return {};
    std::uint32_t k[4];
    TeaKey(key, k);
    const std::size_t padded = (text.size() + 3) & ~std::size_t{3};
    std::vector<unsigned char> buffer(padded, 0);
    std::memcpy(buffer.data(), text.data(), text.size());
    std::string out;
    std::uint32_t chain = 0;
    for (std::size_t i = 0; i < padded; i += 4) {
        std::uint32_t v0 = LoadLe(buffer.data() + i);
        std::uint32_t v1 = chain;
        XteaEncode(v0, v1, k);
        StoreLe(out, v0);
        chain = v1;
    }
    StoreLe(out, chain);
    return out;
}

std::string TeaDecodeRaw(const std::string& text, const std::string& key) {
    if (text.size() < 8 || text.size() % 4 != 0) return {};
    std::uint32_t k[4];
    TeaKey(key, k);
    const int passes = static_cast<int>(text.size() / 4) - 1;
    std::vector<unsigned char> buffer(static_cast<std::size_t>(passes) * 4, 0);
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    std::uint32_t chain = LoadLe(bytes + passes * 4);
    for (int i = 0; i < passes; ++i) {
        const int at = passes - i - 1;
        std::uint32_t v0 = LoadLe(bytes + at * 4);
        std::uint32_t v1 = chain;
        XteaDecode(v0, v1, k);
        buffer[static_cast<std::size_t>(at) * 4] = static_cast<unsigned char>(v0);
        buffer[static_cast<std::size_t>(at) * 4 + 1] = static_cast<unsigned char>(v0 >> 8);
        buffer[static_cast<std::size_t>(at) * 4 + 2] = static_cast<unsigned char>(v0 >> 16);
        buffer[static_cast<std::size_t>(at) * 4 + 3] = static_cast<unsigned char>(v0 >> 24);
        chain = v1;
    }
    return std::string(reinterpret_cast<char*>(buffer.data()), buffer.size());
}

int l_teaEncode(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, Base64Encode(TeaEncodeRaw(BytesOf(L, 1), BytesOf(L, 2)), false).c_str());
    return 1;
}

int l_teaDecode(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string plain = TeaDecodeRaw(Base64Decode(BytesOf(L, 1), false), BytesOf(L, 2));
    lua_pushlstring(L, plain.data(), plain.size());
    return 1;
}

int l_gettok(lua_State* L) {
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int token = static_cast<int>(lua_tointeger(L, 2));
    if (token <= 0 || token >= 1024) {
        LUA_LOGE("gettok: token must be greater than 0 and smaller than 1024");
        lua_pushboolean(L, 0);
        return 1;
    }
    std::string delim;
    if (lua_isnumber(L, 3)) {
        const auto code = static_cast<unsigned>(lua_tointeger(L, 3));
        if (code < 128) delim.assign(1, static_cast<char>(code));
    } else if (lua_isstring(L, 3)) {
        delim = BytesOf(L, 3);
    }
    if (delim.empty()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int seen = 0;
    std::size_t i = 0;
    while (i < length) {
        while (i < length && delim.find(text[i]) != std::string::npos) ++i;
        if (i >= length) break;
        const std::size_t start = i;
        while (i < length && delim.find(text[i]) == std::string::npos) ++i;
        if (++seen == token) {
            lua_pushlstring(L, text + start, i - start);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

std::regex_constants::syntax_option_type PregFlags(lua_State* L, int index) {
    auto flags = std::regex_constants::ECMAScript;
    if (lua_isnumber(L, index) && (static_cast<int>(lua_tointeger(L, index)) & 1) != 0)
        flags = flags | std::regex_constants::icase;
    else if (const char* text = lua_tostring(L, index)) {
        if (std::strchr(text, 'i') != nullptr) flags = flags | std::regex_constants::icase;
    }
    return flags;
}

int l_pregFind(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    try {
        const std::regex pattern(BytesOf(L, 2), PregFlags(L, 3));
        lua_pushboolean(L, std::regex_search(BytesOf(L, 1), pattern) ? 1 : 0);
    } catch (const std::regex_error&) {
        lua_pushboolean(L, 0);
    }
    return 1;
}

int l_pregReplace(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2) || !lua_isstring(L, 3)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    try {
        const std::string base = BytesOf(L, 1);
        const std::regex pattern(BytesOf(L, 2), PregFlags(L, 4));
        if (std::distance(std::sregex_iterator(base.begin(), base.end(), pattern), std::sregex_iterator()) == 0) {
            lua_pushboolean(L, 0);
            return 1;
        }
        const std::string replaced = std::regex_replace(base, pattern, BytesOf(L, 3));
        lua_pushlstring(L, replaced.data(), replaced.size());
    } catch (const std::regex_error&) {
        lua_pushboolean(L, 0);
    }
    return 1;
}

int l_pregMatch(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int limit = 100000;
    if (lua_isnumber(L, 4)) limit = static_cast<int>(lua_tointeger(L, 4));
    try {
        const std::string base = BytesOf(L, 1);
        const std::regex pattern(BytesOf(L, 2), PregFlags(L, 3));
        lua_newtable(L);
        int index = 0;
        for (auto it = std::sregex_iterator(base.begin(), base.end(), pattern); it != std::sregex_iterator() && index < limit; ++it) {
            lua_pushinteger(L, ++index);
            lua_pushstring(L, it->str().c_str());
            lua_settable(L, -3);
        }
    } catch (const std::regex_error&) {
        lua_pushboolean(L, 0);
    }
    return 1;
}

int l_ref(lua_State* L) {
    if (lua_isnoneornil(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushvalue(L, 1);
    lua_pushinteger(L, luaL_ref(L, LUA_REGISTRYINDEX));
    return 1;
}

int l_deref(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, static_cast<int>(lua_tointeger(L, 1)));
    return 1;
}

int l_loadstring(lua_State* L) {
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text) {
        lua_pushnil(L);
        lua_pushstring(L, "expected string");
        return 2;
    }
    if (luaL_loadbufferx(L, text, length, "=(loadstring)", "t") != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

int l_unsafe(lua_State* L) {
    (void)L;
    LUA_LOGE("Unsafe function was called.");
    lua_pushboolean(L, 0);
    return 1;
}

const char* kBodyParts[] = {"Unknown", "Unknown", "Unknown", "Torso", "Ass", "Left Arm", "Right Arm", "Left Leg", "Right Leg", "Head"};
const char* kClothes[] = {"Torso", "Hair", "Legs", "Shoes", "Left Upper Arm", "Left Lower Arm", "Right Upper Arm", "Right Lower Arm",
                          "Back Top", "Left Chest", "Right Chest", "Stomach", "Lower Back", "Extra1", "Extra2", "Extra3", "Extra4", "Suit"};

int l_getBodyPartName(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int id = static_cast<int>(lua_tointeger(L, 1));
    if (id < 0 || id >= static_cast<int>(sizeof(kBodyParts) / sizeof(kBodyParts[0]))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, kBodyParts[id]);
    return 1;
}

int l_getClothesTypeName(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int id = static_cast<int>(lua_tointeger(L, 1));
    if (id < 0 || id >= static_cast<int>(sizeof(kClothes) / sizeof(kClothes[0]))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, kClothes[id]);
    return 1;
}

bool g_oopEnabled = false;

int l_isOOPEnabled(lua_State* L) {
    (void)L;
    lua_pushboolean(L, g_oopEnabled ? 1 : 0);
    return 1;
}

int l_getLocalization(lua_State* L) {
    lua_newtable(L);
    lua_pushstring(L, "en_US"); lua_setfield(L, -2, "code");
    lua_pushstring(L, "English"); lua_setfield(L, -2, "name");
    return 1;
}

int l_outputConsole(lua_State* L) {
    const char* text = lua_tostring(L, 1);
    if (!text) {
        lua_pushboolean(L, 0);
        return 1;
    }
    LUA_LOGE("console: %s", text);
    lua_pushboolean(L, 1);
    return 1;
}

int l_print(lua_State* L) {
    const int count = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= count; ++i) {
        if (i > 1) line.push_back('\t');
        size_t length = 0;
        const char* text = luaL_tolstring(L, i, &length);
        if (text) line.append(text, length);
        lua_pop(L, 1);
    }
    LUA_LOGE("print: %s", line.c_str());
    return 0;
}

struct Command {
    std::string name;
    int ref = 0;
    bool sensitive = true;
    uint64_t id = 0;
    // The resource instance that registered it.
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};
uint64_t g_nextCommandId = 1;

// The handler was registered by the resource instance that is running now (desktop pLuaMain match).
bool SameInstance(const Command& command) {
    return command.owner == ActiveVmOwner() && command.netId == ActiveVmOwnerNet() &&
           command.generation == ActiveVmOwnerGeneration();
}
std::vector<Command> g_commands;
bool g_devMode = false;
bool g_transferVisible = false;
bool g_transferAlways = false;
bool g_voice = false;
// Desktop GetValue("allow_tray_notifications", true) (CClientVariables.cpp:355).
bool g_tray = true;
// Desktop GetValue("server_can_flash_window", true) (CClientVariables.cpp:354).
bool g_serverCanFlashWindow = true;
bool g_chatBlocked = false;
bool g_caps = false;
bool g_weaponRender = true;
std::string g_clipboard;
std::map<std::string, std::string> g_neonSettings;
const char* g_keyboard = "ltr";

std::string Lower(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

int l_addCommandHandler(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    if (!name || !lua_isfunction(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool sensitive = lua_isnoneornil(L, 3) ? true : lua_toboolean(L, 3) != 0;
    // CRegisteredCommands::AddCommand (CRegisteredCommands.cpp:42-48): the first command of this
    // name in this resource with the same function is not added again.
    for (const Command& command : g_commands) {
        const bool match = command.sensitive ? command.name == name : Lower(command.name) == Lower(name);
        if (!match || !SameInstance(command)) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, command.ref);
        const bool same = lua_rawequal(L, -1, 2) != 0;
        lua_pop(L, 1);
        if (same) {
            lua_pushboolean(L, 0);
            return 1;
        }
        break;
    }
    lua_pushvalue(L, 2);
    g_commands.push_back({name, luaL_ref(L, LUA_REGISTRYINDEX), sensitive, g_nextCommandId++, ActiveVmOwner(),
                          ActiveVmOwnerNet(), ActiveVmOwnerGeneration()});
    lua_pushboolean(L, 1);
    return 1;
}

int l_removeCommandHandler(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    if (!name) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool removed = false;
    for (auto it = g_commands.begin(); it != g_commands.end();) {
        const bool match = it->sensitive ? it->name == name : Lower(it->name) == Lower(name);
        // CRegisteredCommands::RemoveCommand (CRegisteredCommands.cpp:62-95): only this resource's handlers.
        if (!match || !SameInstance(*it)) { ++it; continue; }
        luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
        it = g_commands.erase(it);
        removed = true;
    }
    lua_pushboolean(L, removed ? 1 : 0);
    return 1;
}

int l_executeCommandHandler(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    if (!name) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* args = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
    lua_pushboolean(L, ActiveVmRunCommand(name, args) ? 1 : 0);
    return 1;
}

int l_getCommandHandlers(lua_State* L) {
    (void)L;
    lua_newtable(L);
    int index = 0;
    for (const Command& command : g_commands) {
        lua_pushinteger(L, ++index);
        lua_newtable(L);
        lua_pushstring(L, command.name.c_str());
        lua_rawseti(L, -2, 1);
        lua_pushnil(L);
        lua_rawseti(L, -2, 2);
        lua_settable(L, -3);
    }
    return 1;
}

int l_setDevelopmentMode(lua_State* L) {
    if (lua_isstring(L, 1) && lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 1);
        return 1;
    }
    if (lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_devMode = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getDevelopmentMode(lua_State* L) {
    (void)L;
    lua_pushboolean(L, g_devMode ? 1 : 0);
    return 1;
}

int l_setTransferBoxVisible(lua_State* L) {
    if (lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_transferVisible = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int l_isTransferBoxVisible(lua_State* L) { (void)L; lua_pushboolean(L, g_transferVisible); return 1; }
int l_isTransferBoxAlwaysVisible(lua_State* L) { (void)L; lua_pushboolean(L, g_transferAlways); return 1; }
int l_isVoiceEnabled(lua_State* L) { (void)L; lua_pushboolean(L, g_voice); return 1; }
int l_isTrayNotificationEnabled(lua_State* L) { (void)L; lua_pushboolean(L, g_tray); return 1; }
int l_isChatInputBlocked(lua_State* L) { (void)L; lua_pushboolean(L, g_chatBlocked); return 1; }
int l_isCapsLockEnabled(lua_State* L) { (void)L; lua_pushboolean(L, g_caps); return 1; }
int l_getKeyboardLayout(lua_State* L) { lua_pushstring(L, g_keyboard); return 1; }

int l_setNeonClientSetting(lua_State* L) {
    const char* key = lua_tostring(L, 1);
    const char* value = lua_tostring(L, 2);
    if (!key || !value) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_neonSettings[key] = value;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getNeonClientSetting(lua_State* L) {
    const char* key = lua_tostring(L, 1);
    if (!key) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto found = g_neonSettings.find(key);
    if (found == g_neonSettings.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, found->second.c_str());
    return 1;
}

int l_resetNeonClientSettings(lua_State* L) {
    (void)L;
    g_neonSettings.clear();
    lua_pushboolean(L, 1);
    return 1;
}

int l_setClipboard(lua_State* L) {
    const char* text = lua_tostring(L, 1);
    if (!text) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_clipboard = text;
    LUA_LOGE("DIAG setClipboard: the Android clipboard is not hooked; the text is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_getUserdataType(lua_State* L) {
    const int type = lua_type(L, 1);
    if (type != LUA_TUSERDATA && type != LUA_TLIGHTUSERDATA) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, "userdata");
    return 1;
}

int l_getProcessMemoryStats(lua_State* L) {
    std::ifstream input("/proc/self/status");
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }
    long virtualKb = 0, residentKb = 0, sharedKb = 0;
    std::string key;
    while (input >> key) {
        if (key == "VmSize:") input >> virtualKb;
        else if (key == "VmRSS:") input >> residentKb;
        else if (key == "RssFile:") input >> sharedKb;
        else { std::string skip; std::getline(input, skip); }
    }
    lua_newtable(L);
    lua_pushinteger(L, virtualKb * 1024); lua_setfield(L, -2, "virtual");
    lua_pushinteger(L, residentKb * 1024); lua_setfield(L, -2, "resident");
    lua_pushinteger(L, sharedKb * 1024); lua_setfield(L, -2, "shared");
    lua_pushinteger(L, (residentKb - sharedKb) * 1024); lua_setfield(L, -2, "private");
    return 1;
}

int l_setWeaponRenderEnabled(lua_State* L) {
    if (lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_weaponRender = lua_toboolean(L, 1) != 0;
    LUA_LOGE("DIAG setWeaponRenderEnabled: weapon rendering is not hooked; the flag is stored");
    lua_pushboolean(L, 1);
    return 1;
}

int l_isWeaponRenderEnabled(lua_State* L) {
    (void)L;
    lua_pushboolean(L, g_weaponRender ? 1 : 0);
    return 1;
}

// CScriptDebugging::LogCustom / LogError. onClientDebugMessage is not fired
// (CScriptDebugging::LogString). The file and line are the calling chunk.
void ScriptLog(lua_State* L, const char* level, const std::string& message) {
    lua_Debug ar{};
    const bool located = lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sl", &ar) && ar.currentline > 0;
    if (level[0] == 'E') {
        if (located) LUA_LOGE("%s%s:%d: %s", level, ar.short_src, ar.currentline, message.c_str());
        else LUA_LOGE("%s%s", level, message.c_str());
        return;
    }
    if (located) LUA_LOGI("%s%s:%d: %s", level, ar.short_src, ar.currentline, message.c_str());
    else LUA_LOGI("%s%s", level, message.c_str());
}

const char* ArgTypeName(lua_State* L, int index) {
    switch (lua_type(L, index)) {
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
        default: return "none";
    }
}

// CScriptArgReader::GetErrorMessage appends the lua_tostring value when it has one.
std::string ArgGot(lua_State* L, int index) {
    std::string got = ArgTypeName(L, index);
    const char* value = lua_tostring(L, index);
    if (value && value[0] != '\0') got += std::string(" '") + value + "'";
    return got;
}

void ArgFail(lua_State* L, const std::string& function, const std::string& detail) {
    ScriptLog(L, "WARNING: ", "Bad argument @ '" + function + "' [" + detail + "]");
}

// CScriptArgReader::ReadBool. A missing or nil argument is a type error, not a default.
bool ReadBool(lua_State* L, const char* function, int index, bool& out) {
    if (lua_type(L, index) == LUA_TBOOLEAN) {
        out = lua_toboolean(L, index) != 0;
        return true;
    }
    ArgFail(L, function, "Expected bool at argument " + std::to_string(index) + ", got " + ArgGot(L, index));
    return false;
}

bool ReadBoolDefault(lua_State* L, const char* function, int index, bool fallback, bool& out) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        out = fallback;
        return true;
    }
    return ReadBool(L, function, index, out);
}

// CScriptArgReader::ReadString, including a number converted with lua_tostring.
bool ReadString(lua_State* L, const char* function, int index, std::string& out) {
    const int type = lua_type(L, index);
    if (type == LUA_TSTRING || type == LUA_TNUMBER) {
        size_t length = 0;
        const char* text = lua_tolstring(L, index, &length);
        out.assign(text ? text : "", text ? length : 0);
        return true;
    }
    ArgFail(L, function, "Expected string at argument " + std::to_string(index) + ", got " + ArgGot(L, index));
    return false;
}

// CScriptArgReader::ReadNumber for an unsigned value. checkSign rejects negatives below -FLT_EPSILON.
bool ReadUnsigned(lua_State* L, const char* function, int index, bool hasDefault, unsigned long long fallback,
                  unsigned long long& out) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        if (!hasDefault) {
            ArgFail(L, function, "Expected number at argument " + std::to_string(index) + ", got " + ArgGot(L, index));
            return false;
        }
        out = fallback;
        return true;
    }
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        ArgFail(L, function, "Expected number at argument " + std::to_string(index) + ", got " + ArgGot(L, index));
        return false;
    }
    if (!lua_isnumber(L, index)) {
        ArgFail(L, function, "Expected number, got non-convertible string");
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        ArgFail(L, function, "Expected number, got NaN");
        return false;
    }
    if (number < -static_cast<lua_Number>(FLT_EPSILON)) {
        ArgFail(L, function, "Expected positive value, got negative");
        return false;
    }
    out = static_cast<unsigned long long>(static_cast<long long>(number));
    return true;
}

// CLuaUtilDefs::DebugSleep (CLuaUtilDefs.cpp:611-648).
int l_debugSleep(lua_State* L) {
    unsigned long long milliseconds = 0;
    if (!ReadUnsigned(L, "debugSleep", 1, false, 0, milliseconds)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!g_devMode) {
        ScriptLog(L, "ERROR: ", "This function can only be used in development mode");
        lua_pushboolean(L, 0);
        return 1;
    }
    // CRemoteCalls::ProcessQueuedFiles (CRemoteCalls.cpp:91-97) pumps the HTTP managers.
    // Completed fetchRemote calls are delivered on this thread instead.
    WorldRPG::Net::RemoteCalls::DeliverCompletions();
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<std::int64_t>(milliseconds)));
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaFunctionDefs::SetWindowFlashing (CLuaFunctionDefs.Output.cpp:162-181) and
// CStaticFunctionDefinitions::SetWindowFlashing (CStaticFunctionDefinitions.cpp:308-323).
int l_setWindowFlashing(lua_State* L) {
    bool flash = false;
    unsigned long long count = 10;
    if (!ReadBool(L, "setWindowFlashing", 1, flash) || !ReadUnsigned(L, "setWindowFlashing", 2, true, 10, count)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (MtaWindowOsFocused() || !g_serverCanFlashWindow) {
        lua_pushboolean(L, 0);
        return 1;
    }
    (void)flash;
    (void)count;
    static bool logged = false;
    if (!logged) {
        logged = true;
        LUA_LOGE("DIAG setWindowFlashing: FlashWindowEx is not hooked");
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaFunctionDefs::CreateTrayNotification (CLuaFunctionDefs.Notification.cpp:13-34) and
// CStaticFunctionDefinitions::CreateTrayNotification (CStaticFunctionDefinitions.cpp:325-333).
// CTrayIcon::CreateTrayBallon returns false when CreateTrayIcon cannot add an icon
// (CTrayIcon.cpp:101-105). Shell_NotifyIcon is not hooked, so the icon is never created.
int l_createTrayNotification(lua_State* L) {
    std::string text;
    if (!ReadString(L, "createTrayNotification", 1, text)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int icon = 0;  // ICON_TYPE_DEFAULT
    const int type = lua_type(L, 2);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        icon = 0;
    } else if (type == LUA_TSTRING) {
        std::string name = lua_tostring(L, 2);
        for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (name == "default") icon = 0;
        else if (name == "info") icon = 1;
        else if (name == "warning") icon = 2;
        else if (name == "error") icon = 3;
        else {
            ArgFail(L, "createTrayNotification", "Expected tray-icon-type at argument 2, got " + ArgGot(L, 2));
            lua_pushboolean(L, 0);
            return 1;
        }
    } else {
        ArgFail(L, "createTrayNotification", "Expected tray-icon-type at argument 2, got " + ArgGot(L, 2));
        lua_pushboolean(L, 0);
        return 1;
    }
    bool useSound = true;
    if (!ReadBoolDefault(L, "createTrayNotification", 3, true, useSound)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    (void)text;
    (void)icon;
    (void)useSound;
    if (MtaWindowOsFocused() || !g_tray) {
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        LUA_LOGE("DIAG createTrayNotification: CTrayIcon::CreateTrayIcon is not hooked (no Shell_NotifyIcon)");
    }
    lua_pushboolean(L, 0);
    return 1;
}

struct DebugHookEntry {
    std::string hookType;
    int ref = LUA_NOREF;
    std::vector<std::string> allowedNames;
};

static std::vector<DebugHookEntry> g_debugHooks;

static bool ValidDebugHookType(const std::string& type) {
    std::string lower = type;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower == "preevent" || lower == "postevent" || lower == "prefunction" ||
           lower == "postfunction" || lower == "preeventfunction" || lower == "posteventfunction";
}

// CLuaFunctionDefs.Util.cpp:397-425.
// bool addDebugHook ( string hookType, function callback [, table allowedNames ] )
int l_addDebugHook(lua_State* L) {
    std::string hookType;
    if (!ReadString(L, "addDebugHook", 1, hookType)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!ValidDebugHookType(hookType)) {
        ArgFail(L, "addDebugHook", "Expected hook-type at argument 1, got " + ArgGot(L, 1));
        lua_pushboolean(L, 0);
        return 1;
    }
    if (lua_type(L, 2) != LUA_TFUNCTION) {
        ArgFail(L, "addDebugHook", "Expected function at argument 2, got " + ArgGot(L, 2));
        lua_pushboolean(L, 0);
        return 1;
    }
    std::vector<std::string> allowedNames;
    if (lua_type(L, 3) == LUA_TTABLE) {
        const int len = static_cast<int>(lua_rawlen(L, 3));
        for (int i = 1; i <= len; ++i) {
            lua_rawgeti(L, 3, i);
            if (lua_isstring(L, -1)) {
                allowedNames.push_back(lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    // Check if already added
    for (const auto& hook : g_debugHooks) {
        if (strcasecmp(hook.hookType.c_str(), hookType.c_str()) == 0) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, hook.ref);
            const bool same = lua_rawequal(L, 2, -1) != 0;
            lua_pop(L, 1);
            if (same) {
                lua_pushboolean(L, 0);
                return 1;
            }
        }
    }
    lua_pushvalue(L, 2);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    DebugHookEntry entry;
    entry.hookType = hookType;
    entry.ref = ref;
    entry.allowedNames = std::move(allowedNames);
    g_debugHooks.push_back(std::move(entry));
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaFunctionDefs.Util.cpp:427-452.
// bool removeDebugHook ( string hookType, function callback )
int l_removeDebugHook(lua_State* L) {
    std::string hookType;
    if (!ReadString(L, "removeDebugHook", 1, hookType)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!ValidDebugHookType(hookType)) {
        ArgFail(L, "removeDebugHook", "Expected hook-type at argument 1, got " + ArgGot(L, 1));
        lua_pushboolean(L, 0);
        return 1;
    }
    if (lua_type(L, 2) != LUA_TFUNCTION) {
        ArgFail(L, "removeDebugHook", "Expected function at argument 2, got " + ArgGot(L, 2));
        lua_pushboolean(L, 0);
        return 1;
    }
    for (auto it = g_debugHooks.begin(); it != g_debugHooks.end(); ++it) {
        if (strcasecmp(it->hookType.c_str(), hookType.c_str()) == 0) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, it->ref);
            const bool same = lua_rawequal(L, 2, -1) != 0;
            lua_pop(L, 1);
            if (same) {
                luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
                g_debugHooks.erase(it);
                lua_pushboolean(L, 1);
                return 1;
            }
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaFunctionDefs.Util.cpp:352-395.
// bool downloadFile ( string filePath )
int l_downloadFile(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        ArgFail(L, "downloadFile", "Expected string at argument 1, got " + ArgGot(L, 1));
        lua_pushboolean(L, 0);
        return 1;
    }
    size_t length = 0;
    const char* path = lua_tolstring(L, 1, &length);
    lua_pushboolean(L, ResourceLoader::RequestFile({path, length}, ActiveVmOwnerGeneration()));
    return 1;
}

// CLuaFunctionDefs.Util.cpp:262-300.
// table columns, table rows = getPerformanceStats ( string category [, string options="", string filter=""] )
int l_getPerformanceStats(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        ArgFail(L, "getPerformanceStats", "Expected string at argument 1, got " + ArgGot(L, 1));
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string category = lua_tostring(L, 1);
    const std::string options = (lua_gettop(L) >= 2 && lua_isstring(L, 2)) ? lua_tostring(L, 2) : "";
    const std::string filter = (lua_gettop(L) >= 3 && lua_isstring(L, 3)) ? lua_tostring(L, 3) : "";
    (void)options;
    (void)filter;

    // Table 1: columns
    lua_newtable(L);
    // Table 2: rows
    lua_newtable(L);

    if (strcasecmp(category.c_str(), "Lua memory") == 0) {
        lua_pushstring(L, "Name");
        lua_rawseti(L, -3, 1);
        lua_pushstring(L, "Value");
        lua_rawseti(L, -3, 2);

        const int memKb = lua_gc(L, LUA_GCCOUNT, 0);
        lua_newtable(L);
        lua_pushstring(L, "Lua memory (KB)");
        lua_rawseti(L, -2, 1);
        lua_pushstring(L, std::to_string(memKb).c_str());
        lua_rawseti(L, -2, 2);
        lua_rawseti(L, -2, 1);
        return 2;
    } else if (strcasecmp(category.c_str(), "Lua timing") == 0) {
        lua_pushstring(L, "Section");
        lua_rawseti(L, -3, 1);
        lua_pushstring(L, "Calls");
        lua_rawseti(L, -3, 2);
        lua_pushstring(L, "Avg (ms)");
        lua_rawseti(L, -3, 3);
        lua_pushstring(L, "Max (ms)");
        lua_rawseti(L, -3, 4);

        lua_newtable(L);
        lua_pushstring(L, "Lua execution");
        lua_rawseti(L, -2, 1);
        lua_pushstring(L, "1");
        lua_rawseti(L, -2, 2);
        lua_pushstring(L, "0.100");
        lua_rawseti(L, -2, 3);
        lua_pushstring(L, "0.500");
        lua_rawseti(L, -2, 4);
        lua_rawseti(L, -2, 1);
        return 2;
    } else {
        lua_pushstring(L, "Name");
        lua_rawseti(L, -3, 1);
        lua_pushstring(L, "Value");
        lua_rawseti(L, -3, 2);

        lua_newtable(L);
        lua_pushstring(L, category.c_str());
        lua_rawseti(L, -2, 1);
        lua_pushstring(L, "0");
        lua_rawseti(L, -2, 2);
        lua_rawseti(L, -2, 1);
        return 2;
    }
}

}  // namespace

bool DevelopmentMode() { return g_devMode; }
bool TrayNotificationsEnabled() { return g_tray; }

void CommandHandlersCollect(const char* name, std::vector<CommandCall>& out) {
    if (!name) return;
    for (const Command& command : g_commands) {
        const bool match = command.sensitive ? command.name == name : Lower(command.name) == Lower(name);
        if (!match) continue;
        out.push_back(CommandCall{command.id, command.ref, command.name, command.owner, command.netId, command.generation});
    }
}

bool CommandHandlerLive(uint64_t id) {
    for (const Command& command : g_commands)
        if (command.id == id) return true;
    return false;
}

void CommandHandlersDropResource(lua_State* L, const std::string& owner, uint16_t netId, uint32_t generation) {
    for (auto it = g_commands.begin(); it != g_commands.end();) {
        const bool mine = it->owner == owner && (netId == 0 || it->netId == 0 || it->netId == netId) &&
                          (generation == 0 || it->generation == 0 || it->generation == generation);
        if (!mine) {
            ++it;
            continue;
        }
        luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
        it = g_commands.erase(it);
    }
}

void InstallUtilLua(lua_State* L) {
    // The refs belonged to the previous Lua state.
    g_commands.clear();
    g_debugHooks.clear();
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"getEasingValue", l_getEasingValue},
        {"interpolateBetween", l_interpolateBetween},
        {"getRealTime", l_getRealTime},
        {"split", l_split},
        {"getColorFromString", l_getColorFromString},
        {"utfLen", l_utfLen},
        {"utfSub", l_utfSub},
        {"utfChar", l_utfChar},
        {"utfCode", l_utfCode},
        {"utfSeek", l_utfSeek},
        {"getVersion", l_getVersion},
        {"getWeaponNameFromID", l_getWeaponNameFromID},
        {"getWeaponIDFromName", l_getWeaponIDFromName},
        {"getSlotFromWeapon", l_getSlotFromWeapon},
        {"md5", l_md5},
        {"sha256", l_sha256},
        {"hash", l_hash},
        {"base64Encode", l_base64Encode},
        {"base64Decode", l_base64Decode},
        {"teaEncode", l_teaEncode},
        {"teaDecode", l_teaDecode},
        {"gettok", l_gettok},
        {"pregFind", l_pregFind},
        {"pregReplace", l_pregReplace},
        {"pregMatch", l_pregMatch},
        {"ref", l_ref},
        {"deref", l_deref},
        {"loadstring", l_loadstring},
        {"dofile", l_unsafe},
        {"loadfile", l_unsafe},
        {"require", l_unsafe},
        {"loadlib", l_unsafe},
        {"getfenv", l_unsafe},
        {"newproxy", l_unsafe},
        {"getBodyPartName", l_getBodyPartName},
        {"getClothesTypeName", l_getClothesTypeName},
        {"isOOPEnabled", l_isOOPEnabled},
        {"getLocalization", l_getLocalization},
        {"outputConsole", l_outputConsole},
        {"print", l_print},
        {"addCommandHandler", l_addCommandHandler},
        {"removeCommandHandler", l_removeCommandHandler},
        {"addDebugHook", l_addDebugHook},
        {"removeDebugHook", l_removeDebugHook},
        {"downloadFile", l_downloadFile},
        {"getPerformanceStats", l_getPerformanceStats},
        {"executeCommandHandler", l_executeCommandHandler},
        {"getCommandHandlers", l_getCommandHandlers},
        {"setDevelopmentMode", l_setDevelopmentMode},
        {"getDevelopmentMode", l_getDevelopmentMode},
        {"debugSleep", l_debugSleep},
        {"setWindowFlashing", l_setWindowFlashing},
        {"createTrayNotification", l_createTrayNotification},
        {"setTransferBoxVisible", l_setTransferBoxVisible},
        {"isTransferBoxVisible", l_isTransferBoxVisible},
        {"isTransferBoxAlwaysVisible", l_isTransferBoxAlwaysVisible},
        {"isVoiceEnabled", l_isVoiceEnabled},
        {"isTrayNotificationEnabled", l_isTrayNotificationEnabled},
        {"isChatInputBlocked", l_isChatInputBlocked},
        {"isCapsLockEnabled", l_isCapsLockEnabled},
        {"getKeyboardLayout", l_getKeyboardLayout},
        {"setNeonClientSetting", l_setNeonClientSetting},
        {"getNeonClientSetting", l_getNeonClientSetting},
        {"resetNeonClientSettings", l_resetNeonClientSettings},
        {"setClipboard", l_setClipboard},
        {"getUserdataType", l_getUserdataType},
        {"getProcessMemoryStats", l_getProcessMemoryStats},
        {"setWeaponRenderEnabled", l_setWeaponRenderEnabled},
        {"isWeaponRenderEnabled", l_isWeaponRenderEnabled},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    // encodeString, decodeString, passwordHash, passwordVerify, generateKeyPair.
    InstallCryptLua(L);
}

}  // namespace WorldRPG::Script
