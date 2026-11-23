#include "LuaDxApi.h"

#include "LuaApiExtra.h"
#include "LuaGuiApi.h"
#include "LuaSvgApi.h"
#include "ModelEffects.h"
#include "LuaDx3DApi.h"
#include "DxShader.h"
#include "LuaVMStub.h"

#include "lua.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace WorldRPG::JniBridge {
// Width of the text at this pixel size, in the overlay font or in the TTF file given, or a
// negative value when the Activity is not available.
float MeasureText(const std::string& utf8, float pixelSize, const std::string& fontFile, bool bold);
// Positive line spacing after loading the font, 0 for an invalid font, -1 without an Activity.
float FontHeight(const std::string& source, float pixelSize, bool bold);
// Bitmap work done by the Activity with Android's codecs. A pixel is 0xAARRGGBB. Every call
// returns false when the Activity is not available (host build) or the image cannot be handled.
// The pixels of an image file, or of an overlay bitmap named "tx:<id>" or "rt:<id>".
bool LoadPixels(const std::string& source, std::vector<uint32_t>& argb, unsigned& width, unsigned& height);
// The pixels of PNG or JPEG bytes.
bool DecodePixels(const char* data, size_t size, std::vector<uint32_t>& argb, unsigned& width, unsigned& height);
// PNG or JPEG bytes of the pixels.
bool EncodePixels(const std::vector<uint32_t>& argb, unsigned width, unsigned height, bool jpeg, int quality,
                  std::string& out);
// Creates or overwrites the overlay bitmap of this name.
bool StorePixels(const std::string& name, const std::vector<uint32_t>& argb, unsigned width, unsigned height);
// Asks the Activity to copy the game picture, scaled, into the overlay bitmap of this name.
// The copy lands a frame or two later. Nothing happens when the Activity is not available.
void CopyScreen(const std::string& name, unsigned width, unsigned height);
}

namespace WorldRPG::Script {
namespace {

// EBlendModeType (CLuaFunctionParseHelpers.cpp:357-361), in enum order.
constexpr const char* kBlendModes[] = {"blend", "add", "modulate_add", "overwrite"};
// eDxTestMode (CLuaFunctionParseHelpers.cpp:173-177).
constexpr const char* kTestModes[] = {"none", "no_mem", "low_mem", "no_shader"};
// D3DPRIMITIVETYPE (CLuaFunctionParseHelpers.cpp:592-598). The index plus one is the D3D value.
constexpr const char* kPrimitiveTypes[] = {"pointlist", "linelist", "linestrip",
                                           "trianglelist", "trianglestrip", "trianglefan"};
constexpr int kTriangleFan = 6;

// VM thread only.
int g_blendMode = 0;
int g_testMode = 0;
uint32_t g_activeTarget = 0;

template <size_t N>
int FindName(const char* const (&table)[N], const char* name) {
    if (!name) return -1;
    for (size_t i = 0; i < N; ++i)
        if (strcasecmp(table[i], name) == 0) return static_cast<int>(i);
    return -1;
}

int Fail(lua_State* L, const char* function, const char* expected, int index) {
    LUA_LOGI("WARNING: Bad argument @ '%s' [Expected %s at argument %d, got %s]", function, expected, index,
             lua_isnone(L, index) ? "none" : luaL_typename(L, index));
    lua_pushboolean(L, 0);
    return 1;
}

// CScriptArgReader::ReadNumber accepts numbers and numeric strings. The Canvas
// bridge also rejects infinity and values that cannot be represented as a float.
bool ReadNumber(lua_State* L, int index, double& out) {
    if (!lua_isnumber(L, index)) return false;
    out = lua_tonumber(L, index);
    return std::isfinite(out) && std::abs(out) <= std::numeric_limits<float>::max();
}

// ReadNumber with a default: none or nil takes the default.
bool ReadNumberOr(lua_State* L, int index, double fallback, double& out) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    return ReadNumber(L, index, out);
}

// ReadBool with a default: a boolean, none or nil.
bool ReadBoolOr(lua_State* L, int index, bool fallback, bool& out) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    if (!lua_isboolean(L, index)) return false;
    out = lua_toboolean(L, index) != 0;
    return true;
}

// CScriptArgReader::ReadColor (CScriptArgReader.h:527-546): the number as a 32-bit ARGB value.
uint32_t ToColor(double value) {
    if (!std::isfinite(value)) return 0;
    // Preserve 32-bit wrapping without an out-of-range floating-to-integer cast.
    double wrapped = std::fmod(std::trunc(value), 4294967296.0);
    if (wrapped < 0) wrapped += 4294967296.0;
    return static_cast<uint32_t>(wrapped);
}

struct Vertex {
    float x = 0, y = 0;
    uint32_t color = 0xFFFFFFFFu;
};

// CGraphics::IsValidPrimitiveSize (CGraphics.cpp). The trianglelist case falls through to the
// three-vertex minimum, as on desktop.
bool ValidPrimitiveSize(size_t count, int type) {
    if (count < 1) return false;
    switch (type) {
        case 3: return count >= 2;                       // linestrip
        case 2: return count % 2 == 0;                   // linelist
        case 4: return count % 3 == 0 && count >= 3;     // trianglelist
        case 5:
        case 6: return count >= 3;                       // trianglestrip, trianglefan
        default: return true;                            // pointlist
    }
}

// "P type count x y color ..." for MtaDxView.
void AppendOrdered(const std::string& line, bool postGui) {
    if (postGui && !g_activeTarget)
        ActiveVmAppendPostGuiDraw("B " + std::to_string(g_blendMode) + "\n" + line);
    else ActiveVmAppendDraw(line); // Render-target draws keep their original order.
}

void AppendPrimitive(int type, const std::vector<Vertex>& vertices, bool postGui = false) {
    std::string line;
    line.reserve(16 + vertices.size() * 28);
    char part[128];
    std::snprintf(part, sizeof(part), "P %d %zu", type, vertices.size());
    line += part;
    for (const Vertex& vertex : vertices) {
        std::snprintf(part, sizeof(part), " %.1f %.1f %u", vertex.x, vertex.y, static_cast<unsigned>(vertex.color));
        line += part;
    }
    line += '\n';
    AppendOrdered(line, postGui);
}

// bool dxDrawLine ( int startX, int startY, int endX, int endY, int color, [float width=1, bool postGUI=false] )
// CLuaDrawingDefs.cpp:159-187. Screen draws honor the pre/post GUI queue.
int DxDrawLine(lua_State* L) {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0, color = 0, width = 1;
    bool postGui = false;
    if (!ReadNumber(L, 1, x1)) return Fail(L, "dxDrawLine", "number", 1);
    if (!ReadNumber(L, 2, y1)) return Fail(L, "dxDrawLine", "number", 2);
    if (!ReadNumber(L, 3, x2)) return Fail(L, "dxDrawLine", "number", 3);
    if (!ReadNumber(L, 4, y2)) return Fail(L, "dxDrawLine", "number", 4);
    if (!ReadNumberOr(L, 5, 4294967295.0, color)) return Fail(L, "dxDrawLine", "number", 5);
    if (!ReadNumberOr(L, 6, 1, width)) return Fail(L, "dxDrawLine", "number", 6);
    if (!ReadBoolOr(L, 7, false, postGui)) return Fail(L, "dxDrawLine", "bool", 7);
    char line[512];
    std::snprintf(line, sizeof(line), "L %.1f %.1f %.1f %.1f %u %.2f\n", x1, y1, x2, y2,
                  static_cast<unsigned>(ToColor(color)), width);
    AppendOrdered(line, postGui);
    lua_pushboolean(L, 1);
    return 1;
}

// bool dxDrawCircle ( float posX, float posY, float radius [, float startAngle = 0, float stopAngle = 360,
//   int color = white, int centerColor = color, int segments = 32, float ratio = 1, bool postGUI = false ] )
// CLuaDrawingDefs.cpp:443-509 and CGraphics::DrawCircleQueued: a triangle fan around the centre.
int DxDrawCircle(lua_State* L) {
    double x = 0, y = 0, radius = 0, start = 0, stop = 360, color = 0, center = 0, segments = 32, ratio = 1;
    bool postGui = false;
    if (!ReadNumber(L, 1, x)) return Fail(L, "dxDrawCircle", "number", 1);
    if (!ReadNumber(L, 2, y)) return Fail(L, "dxDrawCircle", "number", 2);
    if (!ReadNumber(L, 3, radius)) return Fail(L, "dxDrawCircle", "number", 3);
    if (!ReadNumberOr(L, 4, 0, start)) return Fail(L, "dxDrawCircle", "number", 4);
    if (!ReadNumberOr(L, 5, 360, stop)) return Fail(L, "dxDrawCircle", "number", 5);
    if (!ReadNumberOr(L, 6, 4294967295.0, color)) return Fail(L, "dxDrawCircle", "number", 6);
    if (!ReadNumberOr(L, 7, color, center)) return Fail(L, "dxDrawCircle", "number", 7);
    if (!ReadNumberOr(L, 8, 32, segments)) return Fail(L, "dxDrawCircle", "number", 8);
    if (!ReadNumberOr(L, 9, 1, ratio)) return Fail(L, "dxDrawCircle", "number", 9);
    if (!ReadBoolOr(L, 10, false, postGui)) return Fail(L, "dxDrawCircle", "bool", 10);
    // The segment count is a short on desktop.
    const int count = segments >= -32768 && segments <= 32767 ? static_cast<int>(segments) : 0;
    const float fRadius = static_cast<float>(radius);
    float fStart = static_cast<float>(start), fStop = static_cast<float>(stop);
    const float fRatio = static_cast<float>(ratio);
    if (count < 3 || count > 1024 || !(fRatio > 0 && fRatio <= 100) || !(fRadius > 0) || fStart == fStop) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (fStop < fStart) std::swap(fStop, fStart);
    // Never more than 360 degrees.
    if (fStart + 360.0f < fStop) fStop = fStart + 360.0f;
    const float kRadians = 3.14159265358979323846f / 180.0f;
    fStart *= kRadians;
    fStop *= kRadians;
    const float step = (fStop - fStart) / static_cast<float>(count);
    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<size_t>(count) + 2);
    vertices.push_back(Vertex{static_cast<float>(x), static_cast<float>(y), ToColor(center)});
    for (int segment = 0; segment <= count; ++segment) {
        const float angle = fStart + static_cast<float>(segment) * step;
        const float vx = static_cast<float>(x) + fRadius * std::cos(angle) * fRatio;
        const float vy = static_cast<float>(y) + fRadius * std::sin(angle);
        if (!std::isfinite(vx) || !std::isfinite(vy)) { lua_pushboolean(L, 0); return 1; }
        vertices.push_back(Vertex{vx, vy, ToColor(color)});
    }
    AppendPrimitive(kTriangleFan, vertices, postGui);
    lua_pushboolean(L, 1);
    return 1;
}

// bool dxDrawPrimitive (string primitiveType, bool postGUI, table vertice1, ...)
// CLuaDrawingDefs.cpp:706-753. A bad argument is a Lua error, as on desktop.
enum class PrimitiveError { None, Number, Size, Material };

// CScriptArgReader::ReadNumberTable (CScriptArgReader.h:1145-1165) reads only
// numeric values. Strings in a vertex table are ignored, including numeric strings.
template <size_t N>
bool ReadVertexNumbers(lua_State* L, int index, double (&values)[N], int& count) {
    count = 0;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (lua_type(L, -1) == LUA_TNUMBER) {
            double value = 0;
            if (!ReadNumber(L, -1, value)) { lua_pop(L, 2); return false; }
            if (count < static_cast<int>(N)) values[count] = value;
            ++count;
        }
        lua_pop(L, 1);
    }
    return true;
}

// Return before luaL_error: Lua's longjmp must not skip vector/string destructors.
PrimitiveError QueuePrimitive(lua_State* L, int type, int& count) {
    std::vector<Vertex> vertices;
    for (int index = 3; lua_istable(L, index); ++index) {
        double values[4] = {};
        if (!ReadVertexNumbers(L, index, values, count)) return PrimitiveError::Number;
        if (count == 2) {
            vertices.push_back(Vertex{static_cast<float>(values[0]), static_cast<float>(values[1]), 0xFFFFFFFFu});
        } else if (count == 3) {
            vertices.push_back(Vertex{static_cast<float>(values[0]), static_cast<float>(values[1]), ToColor(values[2])});
        } else {
            return PrimitiveError::Size;
        }
    }
    if (!ValidPrimitiveSize(vertices.size(), type + 1)) {
        lua_pushboolean(L, 0);
        return PrimitiveError::None;
    }
    AppendPrimitive(type + 1, vertices, lua_toboolean(L, 2) != 0);
    lua_pushboolean(L, 1);
    return PrimitiveError::None;
}

int DxDrawPrimitive(lua_State* L) {
    const int type = lua_type(L, 1) == LUA_TSTRING ? FindName(kPrimitiveTypes, lua_tostring(L, 1)) : -1;
    if (type < 0) {
        return luaL_error(L, "Bad argument @ 'dxDrawPrimitive' [Expected primitive-type at argument 1, got %s]",
                          lua_isnone(L, 1) ? "none" : luaL_typename(L, 1));
    }
    if (!lua_isboolean(L, 2)) {
        return luaL_error(L, "Bad argument @ 'dxDrawPrimitive' [Expected bool at argument 2, got %s]",
                          lua_isnone(L, 2) ? "none" : luaL_typename(L, 2));
    }
    int count = 0;
    const PrimitiveError error = QueuePrimitive(L,type,count);
    if (error == PrimitiveError::Number) return luaL_error(L, "Bad argument @ 'dxDrawPrimitive' [Expected finite vertex number]");
    if (error == PrimitiveError::Size)
        return luaL_error(L, "Bad argument @ 'dxDrawPrimitive' [Expected table with 2 or 3 numbers, got %d numbers]",count);
    return 1;
}

void AppendBlendMode() {
    char line[16];
    std::snprintf(line, sizeof(line), "B %d\n", g_blendMode);
    ActiveVmAppendDraw(line);
}

// bool dxSetBlendMode ( string blendMode ). CLuaDrawingDefs.cpp:2044-2064. No argument is "blend".
int DxSetBlendMode(lua_State* L) {
    int mode = 0;
    if (!lua_isnoneornil(L, 1)) {
        mode = lua_type(L, 1) == LUA_TSTRING ? FindName(kBlendModes, lua_tostring(L, 1)) : -1;
        if (mode < 0) return Fail(L, "dxSetBlendMode", "blend-mode", 1);
    }
    if (mode != g_blendMode) {
        g_blendMode = mode;
        AppendBlendMode();
    }
    lua_pushboolean(L, 1);
    return 1;
}

// string dxGetBlendMode (). CLuaDrawingDefs.cpp:2066-2072.
int DxGetBlendMode(lua_State* L) {
    lua_pushstring(L, kBlendModes[g_blendMode]);
    return 1;
}

// bool dxSetAspectRatioAdjustmentEnabled( bool enabled, float sourceRatio = 4/3 )
// CLuaDrawingDefs.cpp:2074-2099. Desktop allows it only while onClientHUDRender runs
// (g_bAllowAspectRatioAdjustment). The phone does not fire that event, so the call always fails
// with the desktop error.
int DxSetAspectRatioAdjustmentEnabled(lua_State* L) {
    double ratio = 0;
    if (!lua_isboolean(L, 1)) return Fail(L, "dxSetAspectRatioAdjustmentEnabled", "bool", 1);
    if (!ReadNumberOr(L, 2, 4 / 3.0, ratio)) return Fail(L, "dxSetAspectRatioAdjustmentEnabled", "number", 2);
    LUA_LOGI("WARNING: Bad usage @ 'dxSetAspectRatioAdjustmentEnabled' [Function can only be used inside certain events]");
    lua_pushboolean(L, 0);
    return 1;
}

// bool, float dxIsAspectRatioAdjustmentEnabled(). CLuaDrawingDefs.cpp:2101-2109. Never enabled here.
int DxIsAspectRatioAdjustmentEnabled(lua_State* L) {
    lua_pushboolean(L, 0);
    lua_pushnumber(L, 4 / 3.f);
    return 2;
}

// bool dxSetTestMode( string testMode ). CLuaDrawingDefs.cpp:1613-1633. The mode is kept; the
// phone has no render item manager to starve of memory, so nothing else changes.
int DxSetTestMode(lua_State* L) {
    int mode = 0;
    if (!lua_isnoneornil(L, 1)) {
        mode = lua_type(L, 1) == LUA_TSTRING ? FindName(kTestModes, lua_tostring(L, 1)) : -1;
        if (mode < 0) return Fail(L, "dxSetTestMode", "dx-test-mode", 1);
    }
    if (mode != g_testMode) LUA_LOGI("DIAG dxSetTestMode: '%s' stored, CRenderItemManager::SetTestMode not wired", kTestModes[mode]);
    g_testMode = mode;
    lua_pushboolean(L, 1);
    return 1;
}

// ETextureAddress (CLuaFunctionParseHelpers.cpp), in enum order.
constexpr const char* kTextureEdges[] = {"wrap", "mirror", "clamp", "border", "mirror-once"};

struct Texture {
    uint32_t id = 0;
    std::string file;
    unsigned width = 0, height = 0;
    int edge = 0;
    uint32_t borderColor = 0;
    // dxCreateRenderTarget: the overlay owns a bitmap named "rt:<id>" instead of a file.
    bool renderTarget = false;
    // The picture is an overlay bitmap named "tx:<id>" (made from pixels, blank, or written by
    // dxSetTexturePixels), not the file.
    bool memory = false;
    // dxCreateScreenSource: a memory texture that dxUpdateScreenSource fills with the game picture.
    bool screenSource = false;
    bool alpha = false;
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};
std::vector<Texture> g_textures;
// Render targets destroyed since the last frame; the overlay frees their bitmaps.
std::vector<uint32_t> g_deadTargets;
constexpr size_t kMaxRenderTargets = 32;
constexpr double kMaxRenderTargetSide = 4096;

void ForgetTarget(const Texture& texture) {
    if (!texture.renderTarget && !texture.memory) return;
    g_deadTargets.push_back(texture.id);
    if (g_activeTarget == texture.id) {
        g_activeTarget = 0;
        ActiveVmAppendDraw("G\n");
    }
}
uint32_t g_nextTexture = 0x33000001u;

// dxCreateFont element (desktop CClientDxFont / CDxFontItem).
struct Font {
    uint32_t id = 0;
    std::string file;
    // D3DX font height in pixels: floor(size * 1.75) (CRenderItem.DxFont.cpp:89-94).
    unsigned height = 0;
    bool bold = false;
    bool gui = false;
    double guiPixels = 0;
    std::string guiName;
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};
std::vector<Font> g_fonts;
uint32_t g_nextFont = 0x34000001u;

Font* FindFont(uint32_t id) {
    for (Font& font : g_fonts)
        if (font.id == id) return &font;
    return nullptr;
}

bool IsGuiFontArgument(lua_State* L, int index) {
    if (!lua_istable(L, index)) return false;
    const Font* font = FindFont(ElementIdOf(L, index));
    return font && font->gui;
}

std::string FontSource(const Font& font) {
    return "@font:" + std::to_string(font.id) + ":" + font.file;
}

Texture* FindTexture(uint32_t id) {
    for (Texture& texture : g_textures)
        if (texture.id == id) return &texture;
    return nullptr;
}

// Pixel size from the file header: PNG (IHDR), JPEG (the first SOF segment), BMP and GIF.
// False for any other file, which is then "corrupt or unsupported format" as on desktop.
bool ImageSize(const std::string& file, unsigned& width, unsigned& height) {
    std::FILE* in = std::fopen(file.c_str(), "rb");
    if (!in) return false;
    unsigned char head[32] = {};
    const size_t got = std::fread(head, 1, sizeof(head), in);
    bool ok = false;
    auto be32 = [](const unsigned char* p) { return (unsigned(p[0]) << 24) | (unsigned(p[1]) << 16) | (unsigned(p[2]) << 8) | p[3]; };
    if (got >= 24 && std::memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0 && std::memcmp(head + 12, "IHDR", 4) == 0) {
        width = be32(head + 16);
        height = be32(head + 20);
        ok = true;
    } else if (got >= 26 && head[0] == 'B' && head[1] == 'M') {
        width = head[18] | (head[19] << 8) | (head[20] << 16) | (unsigned(head[21]) << 24);
        const int32_t rows = static_cast<int32_t>(head[22] | (head[23] << 8) | (head[24] << 16) | (unsigned(head[25]) << 24));
        height = static_cast<unsigned>(rows < 0 ? -rows : rows);
        ok = true;
    } else if (got >= 10 && std::memcmp(head, "GIF8", 4) == 0) {
        width = head[6] | (head[7] << 8);
        height = head[8] | (head[9] << 8);
        ok = true;
    } else if (got >= 4 && head[0] == 0xFF && head[1] == 0xD8) {
        long at = 2;
        for (int guard = 0; guard < 512 && !ok; ++guard) {
            unsigned char seg[9] = {};
            if (std::fseek(in, at, SEEK_SET) != 0 || std::fread(seg, 1, 9, in) != 9 || seg[0] != 0xFF) break;
            const unsigned marker = seg[1];
            const unsigned length = (unsigned(seg[2]) << 8) | seg[3];
            const bool sof = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
            if (sof) {
                height = (unsigned(seg[5]) << 8) | seg[6];
                width = (unsigned(seg[7]) << 8) | seg[8];
                ok = true;
            } else if (length < 2) {
                break;
            }
            at += 2 + static_cast<long>(length);
        }
    }
    std::fclose(in);
    return ok && width > 0 && height > 0 && width <= 16384 && height <= 16384;
}

// Plain pixels: B, G, R, A per pixel, then the width and the height as two 16-bit values
// (CPixelsManager SetPlainDimensions). On this little-endian CPU a 0xAARRGGBB word has that byte order.
bool ArgbToPlain(const std::vector<uint32_t>& argb, unsigned width, unsigned height, std::string& out) {
    if (width == 0 || height == 0 || width > 0xFFFF || height > 0xFFFF ||
        argb.size() != static_cast<size_t>(width) * height)
        return false;
    out.resize(argb.size() * 4 + 4);
    std::memcpy(&out[0], argb.data(), argb.size() * 4);
    const uint16_t tail[2] = {static_cast<uint16_t>(width), static_cast<uint16_t>(height)};
    std::memcpy(&out[argb.size() * 4], tail, 4);
    return true;
}

// Any pixel string as 0xAARRGGBB words: plain is copied, PNG and JPEG go through the Activity.
// DDS is not handled.
bool PixelsToArgb(const char* data, size_t size, std::vector<uint32_t>& argb, unsigned& width, unsigned& height) {
    const char* format = DxPixelsFormatName(data, size);
    if (!format) return false;
    if (std::strcmp(format, "plain") == 0) {
        uint32_t w = 0, h = 0;
        if (!DxPlainSize(data, size, w, h) || w == 0 || h == 0) return false;
        argb.resize(static_cast<size_t>(w) * h);
        std::memcpy(argb.data(), data, argb.size() * 4);
        width = w;
        height = h;
        return true;
    }
    if (std::strcmp(format, "png") == 0 || std::strcmp(format, "jpeg") == 0)
        return WorldRPG::JniBridge::DecodePixels(data, size, argb, width, height);
    return false;
}

// PNG (IHDR) or JPEG (first SOF segment) size from bytes in memory.
bool EncodedSize(const char* data, size_t size, unsigned& width, unsigned& height) {
    const auto* p = reinterpret_cast<const unsigned char*>(data);
    if (size >= 24 && std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0 && std::memcmp(p + 12, "IHDR", 4) == 0) {
        width = (unsigned(p[16]) << 24) | (unsigned(p[17]) << 16) | (unsigned(p[18]) << 8) | p[19];
        height = (unsigned(p[20]) << 24) | (unsigned(p[21]) << 16) | (unsigned(p[22]) << 8) | p[23];
        return true;
    }
    if (size < 4 || p[0] != 0xFF || p[1] != 0xD8) return false;
    for (size_t at = 2; at + 9 <= size;) {
        if (p[at] != 0xFF) return false;
        const unsigned marker = p[at + 1];
        const unsigned length = (unsigned(p[at + 2]) << 8) | p[at + 3];
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            height = (unsigned(p[at + 5]) << 8) | p[at + 6];
            width = (unsigned(p[at + 7]) << 8) | p[at + 8];
            return true;
        }
        if (length < 2) return false;
        at += 2 + length;
    }
    return false;
}

// A texture whose picture is an overlay bitmap. Pushes the element. False when the overlay
// cannot take the bitmap.
bool CreateMemoryTexture(lua_State* L, const std::vector<uint32_t>& argb, unsigned width, unsigned height) {
    const uint32_t id = g_nextTexture;
    const std::string name = "tx:" + std::to_string(id);
    if (!WorldRPG::JniBridge::StorePixels(name, argb, width, height)) return false;
    Texture texture;
    texture.id = g_nextTexture++;
    texture.file = name;
    texture.width = width;
    texture.height = height;
    texture.memory = true;
    texture.owner = ActiveVmOwner();
    texture.netId = ActiveVmOwnerNet();
    texture.generation = ActiveVmOwnerGeneration();
    g_textures.push_back(texture);
    PushElement(L, texture.id);
    return true;
}

// element dxCreateTexture( string filepath [, string textureFormat = "argb", bool mipmaps = true, string textureEdge = "wrap" ] )
// element dxCreateTexture( string pixels [, ...] ) and element dxCreateTexture( int width, int height [, ...] )
// CLuaDrawingDefs.cpp:980-1117. A file texture stays in its file. The pixels form and the blank
// form make an overlay bitmap; they return nil with the desktop failure line when that fails.
int DxCreateTexture(lua_State* L) {
    if (lua_type(L, 1) == LUA_TNUMBER) {
        // element dxCreateTexture( int width, int height [, ...] ): a blank texture.
        double width = 0, height = 0;
        if (!ReadNumber(L, 2, height)) return Fail(L, "dxCreateTexture", "number", 2);
        width = lua_tonumber(L, 1);
        std::vector<uint32_t> blank;
        const bool sized = width >= 1 && height >= 1 && width <= kMaxRenderTargetSide && height <= kMaxRenderTargetSide;
        if (sized) blank.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0u);
        if (!sized || !CreateMemoryTexture(L, blank, static_cast<unsigned>(width), static_cast<unsigned>(height))) {
            LUA_LOGI("WARNING: [DxCreateTexture:Blank] Failed to create blank texture %dx%d (invalid dimensions or out of memory)",
                     static_cast<int>(width), static_cast<int>(height));
            lua_pushnil(L);
        }
        return 1;
    }
    if (lua_type(L, 1) != LUA_TSTRING) return Fail(L, "dxCreateTexture", "string", 1);
    {
        // element dxCreateTexture( string pixels [, ...] ): the string is pixels, not a path
        // (CPixelsManager::IsPixels).
        size_t size = 0;
        const char* data = lua_tolstring(L, 1, &size);
        if (DxPixelsFormatName(data, size)) {
            std::vector<uint32_t> argb;
            unsigned width = 0, height = 0;
            if (!PixelsToArgb(data, size, argb, width, height) || !CreateMemoryTexture(L, argb, width, height)) {
                LUA_LOGI("WARNING: [DxCreateTexture:Pixels] Failed to create texture from pixel data (invalid format or out of memory)");
                lua_pushnil(L);
            }
            return 1;
        }
    }
    bool mipmaps = true;
    if (!lua_isnoneornil(L, 2) && lua_type(L, 2) != LUA_TSTRING) return Fail(L, "dxCreateTexture", "string", 2);
    if (!ReadBoolOr(L, 3, true, mipmaps)) return Fail(L, "dxCreateTexture", "bool", 3);
    int edge = 0;
    if (!lua_isnoneornil(L, 4)) {
        edge = lua_type(L, 4) == LUA_TSTRING ? FindName(kTextureEdges, lua_tostring(L, 4)) : -1;
        if (edge < 0) return Fail(L, "dxCreateTexture", "texture-edge", 4);
    }
    size_t length = 0;
    const char* raw = lua_tolstring(L, 1, &length);
    const std::string path(raw ? raw : "", length);
    const std::string file = ActiveVmResourceFile(path);
    if (file.empty()) {
        LUA_LOGI("WARNING: Bad argument @ 'dxCreateTexture' [[DxCreateTexture] Bad file path '%.200s']", path.c_str());
        lua_pushboolean(L, 0);
        return 1;
    }
    std::FILE* exists = std::fopen(file.c_str(), "rb");
    if (!exists) {
        LUA_LOGI("WARNING: Bad argument @ 'dxCreateTexture' [[DxCreateTexture] File not found '%.200s']", path.c_str());
        lua_pushboolean(L, 0);
        return 1;
    }
    std::fclose(exists);
    Texture texture;
    if (!ImageSize(file, texture.width, texture.height)) {
        LUA_LOGI("WARNING: [DxCreateTexture] Failed to create texture from file '%.200s' (may be corrupt, unsupported format, or out of memory)",
                 path.c_str());
        lua_pushnil(L);
        return 1;
    }
    texture.id = g_nextTexture++;
    texture.file = file;
    texture.edge = edge;
    texture.owner = ActiveVmOwner();
    texture.netId = ActiveVmOwnerNet();
    texture.generation = ActiveVmOwnerGeneration();
    g_textures.push_back(texture);
    PushElement(L, texture.id);
    return 1;
}

// element dxCreateRenderTarget( int sizeX, int sizeY [, bool withAlphaChannel = false ] )
// CLuaDrawingDefs.cpp:1227-1270. The element is a texture, as on desktop (CClientRenderTarget is a
// CClientTexture). The overlay keeps one bitmap per target. The surface-format form is not
// ported (Canvas has one bitmap format), and a target larger than 4096 px or a 33rd target fails
// the way desktop does when video memory runs out.
int DxCreateRenderTarget(lua_State* L) {
    double width = 0, height = 0;
    bool alpha = false;
    if (!ReadNumber(L, 1, width)) return Fail(L, "dxCreateRenderTarget", "number", 1);
    if (!ReadNumber(L, 2, height)) return Fail(L, "dxCreateRenderTarget", "number", 2);
    if (lua_type(L, 3) == LUA_TSTRING) {
        LUA_LOGI("DIAG dxCreateRenderTarget: surface format '%.32s' not ported, no target created", lua_tostring(L, 3));
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!ReadBoolOr(L, 3, false, alpha)) return Fail(L, "dxCreateRenderTarget", "bool", 3);
    size_t targets = 0;
    for (const Texture& texture : g_textures)
        if (texture.renderTarget) ++targets;
    if (!(width >= 1 && height >= 1) || width > kMaxRenderTargetSide || height > kMaxRenderTargetSide ||
        targets >= kMaxRenderTargets) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Texture texture;
    texture.id = g_nextTexture++;
    texture.width = static_cast<unsigned>(width);
    texture.height = static_cast<unsigned>(height);
    texture.renderTarget = true;
    texture.alpha = alpha;
    texture.file = "rt:" + std::to_string(texture.id);
    texture.owner = ActiveVmOwner();
    texture.netId = ActiveVmOwnerNet();
    texture.generation = ActiveVmOwnerGeneration();
    g_textures.push_back(texture);
    PushElement(L, texture.id);
    return 1;
}

// element dxCreateScreenSource( int sizeX, int sizeY ). CLuaDrawingDefs.cpp:1272-1302. A texture
// that holds a copy of the game picture. It is empty until dxUpdateScreenSource runs.
int DxCreateScreenSource(lua_State* L) {
    double width = 0, height = 0;
    if (!ReadNumber(L, 1, width)) return Fail(L, "dxCreateScreenSource", "number", 1);
    if (!ReadNumber(L, 2, height)) return Fail(L, "dxCreateScreenSource", "number", 2);
    size_t sources = 0;
    for (const Texture& texture : g_textures)
        if (texture.screenSource) ++sources;
    if (!(width >= 1 && height >= 1) || width > kMaxRenderTargetSide || height > kMaxRenderTargetSide ||
        sources >= kMaxRenderTargets) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Texture texture;
    texture.id = g_nextTexture++;
    texture.width = static_cast<unsigned>(width);
    texture.height = static_cast<unsigned>(height);
    texture.memory = true;
    texture.screenSource = true;
    texture.file = "tx:" + std::to_string(texture.id);
    texture.owner = ActiveVmOwner();
    texture.netId = ActiveVmOwnerNet();
    texture.generation = ActiveVmOwnerGeneration();
    g_textures.push_back(texture);
    PushElement(L, texture.id);
    return 1;
}

// bool dxUpdateScreenSource( element screenSource [, bool resampleNow = false ] )
// CLuaDrawingDefs.cpp:1535-1557. The Activity copies the game surface into the source's bitmap
// (PixelCopy), without the DX overlay. The copy is asynchronous, so the picture is one or two
// frames old; resampleNow is read and makes no difference.
int DxUpdateScreenSource(lua_State* L) {
    const Texture* texture = lua_istable(L, 1) ? FindTexture(ElementIdOf(L, 1)) : nullptr;
    if (!texture || !texture->screenSource) return Fail(L, "dxUpdateScreenSource", "screen-source", 1);
    bool resample = false;
    if (!ReadBoolOr(L, 2, false, resample)) return Fail(L, "dxUpdateScreenSource", "bool", 2);
    WorldRPG::JniBridge::CopyScreen(texture->file, texture->width, texture->height);
    lua_pushboolean(L, 1);
    return 1;
}

// bool dxSetRenderTarget( [ element renderTarget, bool clear = false ] )
// CLuaDrawingDefs.cpp:1502-1533. With a target, the draw lines that follow go to its bitmap
// ("G id width height alpha clear"); with none they go back to the screen ("G"). The target
// keeps its picture between frames.
int DxSetRenderTarget(lua_State* L) {
    bool clear = false;
    if (lua_isnoneornil(L, 1)) {
        if (!ReadBoolOr(L, 2, false, clear)) return Fail(L, "dxSetRenderTarget", "bool", 2);
        if (g_activeTarget != 0) ActiveVmAppendDraw("G\n");
        g_activeTarget = 0;
        lua_pushboolean(L, 1);
        return 1;
    }
    const Texture* texture = lua_istable(L, 1) ? FindTexture(ElementIdOf(L, 1)) : nullptr;
    if (!texture || !texture->renderTarget) return Fail(L, "dxSetRenderTarget", "render-target", 1);
    if (!ReadBoolOr(L, 2, false, clear)) return Fail(L, "dxSetRenderTarget", "bool", 2);
    char line[96];
    std::snprintf(line, sizeof(line), "G %u %u %u %d %d\n", static_cast<unsigned>(texture->id), texture->width,
                  texture->height, texture->alpha ? 1 : 0, clear ? 1 : 0);
    ActiveVmAppendDraw(line);
    g_activeTarget = texture->id;
    lua_pushboolean(L, 1);
    return 1;
}

Texture* TextureArg(lua_State* L, int index) {
    if (!lua_istable(L, index)) return nullptr;
    return FindTexture(ElementIdOf(L, index));
}

// int, int dxGetMaterialSize( element material ). CLuaDrawingDefs.cpp:1304-1333. Textures are the
// only material here (no shader, render target or volume texture).
int DxGetMaterialSize(lua_State* L) {
    const Texture* texture = TextureArg(L, 1);
    if (!texture) {
        std::string bitmap;
        unsigned width = 0, height = 0;
        if (!lua_istable(L, 1) || (!SvgInfo(ElementIdOf(L, 1), bitmap, width, height) && !DxShader::Size(ElementIdOf(L,1),width,height)))
            return Fail(L, "dxGetMaterialSize", "material", 1);
        lua_pushinteger(L, width);
        lua_pushinteger(L, height);
        return 2;
    }
    lua_pushinteger(L, texture->width);
    lua_pushinteger(L, texture->height);
    return 2;
}

// bool dxSetTextureEdge ( texture theTexture, string textureEdge [, int border-color ] )
// CLuaDrawingDefs.cpp:2111-2135. The values are kept. dxDrawImage draws the whole image or a
// section inside it, so no draw samples outside the texture.
int DxSetTextureEdge(lua_State* L) {
    Texture* texture = TextureArg(L, 1);
    if (!texture) return Fail(L, "dxSetTextureEdge", "texture", 1);
    const int edge = lua_type(L, 2) == LUA_TSTRING ? FindName(kTextureEdges, lua_tostring(L, 2)) : -1;
    if (edge < 0) return Fail(L, "dxSetTextureEdge", "texture-edge", 2);
    double border = 0;
    if (!ReadNumberOr(L, 3, 0, border)) return Fail(L, "dxSetTextureEdge", "number", 3);
    texture->edge = edge;
    texture->borderColor = ToColor(border);
    lua_pushboolean(L, 1);
    return 1;
}

// EPixelsFormatType names accepted as a target format (CLuaFunctionParseHelpers.cpp:350-354).
constexpr const char* kPixelFormats[] = {"plain", "jpeg", "png", "dds"};

// int x,y dxGetPixelsSize( string pixels ). CLuaDrawingDefs.cpp:1902-1927 and
// CPixelsManager::GetPixelsSize: plain, PNG and JPEG; anything else is false.
int DxGetPixelsSize(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return Fail(L, "dxGetPixelsSize", "string", 1);
    size_t size = 0;
    const char* data = lua_tolstring(L, 1, &size);
    const char* format = DxPixelsFormatName(data, size);
    unsigned width = 0, height = 0;
    bool ok = false;
    if (format && std::strcmp(format, "plain") == 0) {
        uint32_t w = 0, h = 0;
        ok = DxPlainSize(data, size, w, h);
        width = w;
        height = h;
    } else if (format && (std::strcmp(format, "png") == 0 || std::strcmp(format, "jpeg") == 0)) {
        ok = EncodedSize(data, size, width, height);
    }
    if (!ok) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    return 2;
}

// Pushes the pixels in the format asked for. False when the format is DDS or the codec fails.
bool PushPixels(lua_State* L, const std::vector<uint32_t>& argb, unsigned width, unsigned height, int format, int quality) {
    std::string out;
    bool ok = false;
    if (format == 0) ok = ArgbToPlain(argb, width, height, out);
    else if (format == 1 || format == 2) ok = WorldRPG::JniBridge::EncodePixels(argb, width, height, format == 1, quality, out);
    if (!ok) return false;
    lua_pushlstring(L, out.data(), out.size());
    return true;
}

// string dxConvertPixels( string pixels, string pixelFormat [, int quality = 80] )
// CLuaDrawingDefs.cpp:1954-1981 and CPixelsManager::ChangePixelsFormat. The same format returns
// the same string. PNG and JPEG are coded by the Activity. DDS, as source or target, is false.
int DxConvertPixels(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return Fail(L, "dxConvertPixels", "string", 1);
    const int target = lua_type(L, 2) == LUA_TSTRING ? FindName(kPixelFormats, lua_tostring(L, 2)) : -1;
    if (target < 0) return Fail(L, "dxConvertPixels", "pixel-format", 2);
    double quality = 80;
    if (!ReadNumberOr(L, 3, 80, quality)) return Fail(L, "dxConvertPixels", "number", 3);
    size_t size = 0;
    const char* data = lua_tolstring(L, 1, &size);
    const char* source = DxPixelsFormatName(data, size);
    if (!source) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (std::strcmp(source, kPixelFormats[target]) == 0) {
        lua_pushvalue(L, 1);
        return 1;
    }
    std::vector<uint32_t> argb;
    unsigned width = 0, height = 0;
    const int clamped = quality < 0 ? 0 : (quality > 100 ? 100 : static_cast<int>(quality));
    if (!PixelsToArgb(data, size, argb, width, height) || !PushPixels(L, argb, width, height, target, clamped))
        lua_pushboolean(L, 0);
    return 1;
}

// The optional "x, y, width, height" of the texture pixel calls. Desktop uses the rectangle
// only when the height is not 0. False when it leaves the texture.
bool ReadRegion(lua_State* L, int index, unsigned textureWidth, unsigned textureHeight, bool& whole, unsigned& x,
                unsigned& y, unsigned& width, unsigned& height) {
    double values[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i)
        if (!ReadNumberOr(L, index + i, 0, values[i])) return false;
    whole = static_cast<int>(values[3]) == 0;
    if (whole) {
        x = y = 0;
        width = textureWidth;
        height = textureHeight;
        return true;
    }
    if (values[0] < 0 || values[1] < 0 || values[2] < 1 || values[3] < 1 || values[0] + values[2] > textureWidth ||
        values[1] + values[3] > textureHeight)
        return false;
    x = static_cast<unsigned>(values[0]);
    y = static_cast<unsigned>(values[1]);
    width = static_cast<unsigned>(values[2]);
    height = static_cast<unsigned>(values[3]);
    return true;
}

// string dxGetTexturePixels( [ int surfaceIndex, ] element texture [, string pixelsFormat = "plain"
//   [, string textureFormat = "argb"] [, bool mipmaps = true]] [, int x, int y, int width, int height ] )
// CLuaDrawingDefs.cpp:1816-1864. The Activity decodes the file or copies the overlay bitmap.
// Only surface 0 exists (no cube or volume texture).
int DxGetTexturePixels(lua_State* L) {
    int index = 1;
    double surface = 0;
    if (lua_type(L, index) == LUA_TNUMBER) surface = lua_tonumber(L, index++);
    // A texture, a render target, a screen source or an svg (all CClientTexture on desktop).
    Texture svgTexture;
    const Texture* texture = lua_istable(L, index) ? FindTexture(ElementIdOf(L, index)) : nullptr;
    if (!texture && lua_istable(L, index) &&
        SvgInfo(ElementIdOf(L, index), svgTexture.file, svgTexture.width, svgTexture.height))
        texture = &svgTexture;
    if (!texture) return Fail(L, "dxGetTexturePixels", "texture", index);
    ++index;
    int format = 0;
    if (lua_type(L, index) == LUA_TSTRING && FindName(kPixelFormats, lua_tostring(L, index)) >= 0) {
        format = FindName(kPixelFormats, lua_tostring(L, index++));
        if (lua_type(L, index) == LUA_TSTRING) ++index;   // textureFormat, not used
        if (lua_isboolean(L, index)) ++index;             // mipmaps, not used
    }
    bool whole = true;
    unsigned x = 0, y = 0, width = 0, height = 0;
    if (!ReadRegion(L, index, texture->width, texture->height, whole, x, y, width, height))
        return Fail(L, "dxGetTexturePixels", "number", index);
    std::vector<uint32_t> argb;
    unsigned fullWidth = 0, fullHeight = 0;
    if (surface != 0 || !WorldRPG::JniBridge::LoadPixels(texture->file, argb, fullWidth, fullHeight) ||
        x + width > fullWidth || y + height > fullHeight) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!whole) {
        std::vector<uint32_t> part(static_cast<size_t>(width) * height);
        for (unsigned row = 0; row < height; ++row)
            std::memcpy(&part[static_cast<size_t>(row) * width], &argb[static_cast<size_t>(y + row) * fullWidth + x], width * 4u);
        argb.swap(part);
    } else {
        width = fullWidth;
        height = fullHeight;
    }
    if (!PushPixels(L, argb, width, height, format, 80)) lua_pushboolean(L, 0);
    return 1;
}

// bool dxSetTexturePixels( [ int sufaceIndex, ] element texture, string pixels [, int x, int y, int width, int height ] )
// CLuaDrawingDefs.cpp:1866-1900. The pixels must be plain and as large as the texture, or as the
// rectangle. The texture's picture becomes an overlay bitmap from then on.
int DxSetTexturePixels(lua_State* L) {
    int index = 1;
    double surface = 0;
    if (lua_type(L, index) == LUA_TNUMBER) surface = lua_tonumber(L, index++);
    Texture* texture = lua_istable(L, index) ? FindTexture(ElementIdOf(L, index)) : nullptr;
    if (!texture) return Fail(L, "dxSetTexturePixels", "texture", index);
    ++index;
    if (lua_type(L, index) != LUA_TSTRING) return Fail(L, "dxSetTexturePixels", "string", index);
    size_t size = 0;
    const char* data = lua_tolstring(L, index++, &size);
    bool whole = true;
    unsigned x = 0, y = 0, width = 0, height = 0;
    if (!ReadRegion(L, index, texture->width, texture->height, whole, x, y, width, height))
        return Fail(L, "dxSetTexturePixels", "number", index);
    uint32_t pixelsWidth = 0, pixelsHeight = 0;
    std::vector<uint32_t> argb;
    unsigned fullWidth = 0, fullHeight = 0;
    if (surface != 0 || !DxPlainSize(data, size, pixelsWidth, pixelsHeight) || pixelsWidth != width || pixelsHeight != height) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (whole) {
        argb.resize(static_cast<size_t>(width) * height);
        std::memcpy(argb.data(), data, argb.size() * 4);
        fullWidth = width;
        fullHeight = height;
    } else {
        if (!WorldRPG::JniBridge::LoadPixels(texture->file, argb, fullWidth, fullHeight) || x + width > fullWidth ||
            y + height > fullHeight) {
            lua_pushboolean(L, 0);
            return 1;
        }
        for (unsigned row = 0; row < height; ++row)
            std::memcpy(&argb[static_cast<size_t>(y + row) * fullWidth + x], data + static_cast<size_t>(row) * width * 4u, width * 4u);
    }
    // A render target keeps its "rt:" bitmap; any other texture moves to a "tx:" bitmap.
    const std::string name = texture->renderTarget ? texture->file : "tx:" + std::to_string(texture->id);
    if (!WorldRPG::JniBridge::StorePixels(name, argb, fullWidth, fullHeight)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!texture->renderTarget) {
        texture->file = name;
        texture->memory = true;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// eFontQuality (CLuaFunctionParseHelpers.cpp:491-497).
constexpr const char* kFontQualities[] = {"default", "draft", "proof", "nonantialiased",
                                          "antialiased", "cleartype", "cleartype_natural"};

// element dxCreateFont( string filepath [, int size=9, bool bold=false, string quality="proof" ] )
// CLuaDrawingDefs.cpp:1559-1611. The overlay loads the TTF when text is first drawn with it.
// The quality is checked and not used: the overlay always draws anti-aliased text.
int DxCreateFont(lua_State* L) {
    const int pathType = lua_type(L, 1);
    if (pathType != LUA_TSTRING && pathType != LUA_TNUMBER) return Fail(L, "dxCreateFont", "string", 1);
    double size = 9;
    bool bold = false;
    if (!ReadNumberOr(L, 2, 9, size)) return Fail(L, "dxCreateFont", "number", 2);
    if (!ReadBoolOr(L, 3, false, bold)) return Fail(L, "dxCreateFont", "bool", 3);
    if (!lua_isnoneornil(L, 4) && (lua_type(L, 4) != LUA_TSTRING || FindName(kFontQualities, lua_tostring(L, 4)) < 0))
        return Fail(L, "dxCreateFont", "font-quality", 4);
    size_t length = 0;
    const char* raw = lua_tolstring(L, 1, &length);
    const std::string path(raw ? raw : "", length);
    const std::string file = ActiveVmResourceFile(path);
    if (file.empty()) {
        LUA_LOGI("WARNING: Bad argument @ 'dxCreateFont' [Bad file path '%.200s']", path.c_str());
        lua_pushboolean(L, 0);
        return 1;
    }
    std::FILE* in = std::fopen(file.c_str(), "rb");
    unsigned char magic[4] = {};
    const size_t got = in ? std::fread(magic, 1, sizeof(magic), in) : 0;
    if (in) std::fclose(in);
    if (!in) {
        LUA_LOGI("WARNING: Bad argument @ 'dxCreateFont' [File not found '%.200s']", path.c_str());
        lua_pushboolean(L, 0);
        return 1;
    }
    // TrueType (00 01 00 00 or "true"), OpenType ("OTTO") or a collection ("ttcf").
    const bool ttf = got == 4 && (std::memcmp(magic, "\x00\x01\x00\x00", 4) == 0 || std::memcmp(magic, "true", 4) == 0 ||
                                  std::memcmp(magic, "OTTO", 4) == 0 || std::memcmp(magic, "ttcf", 4) == 0);
    if (!ttf) {
        LUA_LOGI("WARNING: Bad argument @ 'dxCreateFont' [Error creating font '%.200s']", path.c_str());
        lua_pushboolean(L, 0);
        return 1;
    }
    Font font;
    font.id = g_nextFont++;
    font.file = file;
    // CDxFontItem::CreateUnderlyingData: the size is clamped to 5-150, the height is size * 1.75.
    const unsigned clamped = size < 5 ? 5u : (size > 150 ? 150u : static_cast<unsigned>(size));
    font.height = static_cast<unsigned>(std::floor(clamped * 1.75f));
    font.bold = bold;
    font.owner = ActiveVmOwner();
    font.netId = ActiveVmOwnerNet();
    font.generation = ActiveVmOwnerGeneration();
    g_fonts.push_back(font);
    PushElement(L, font.id);
    return 1;
}

// int dxGetFontHeight ( [float scale=1, mixed font="default"] ). CLuaDrawingDefs.cpp:925-950 and
// CGraphics::GetDXFontHeight: the font's height times the scale.
int DxGetFontHeight(lua_State* L) {
    double scale = 1;
    if (!ReadNumberOr(L, 1, 1, scale)) return Fail(L, "dxGetFontHeight", "number", 1);
    if (IsGuiFontArgument(L, 2)) return Fail(L, "dxGetFontHeight", "dx-font or font name", 2);
    lua_pushnumber(L, DxResolveFont(L, 2, scale).pixels);
    return 1;
}

// One line of /proc/meminfo in MB, or 0.
double MemInfoMb(const char* key) {
    std::FILE* in = std::fopen("/proc/meminfo", "r");
    if (!in) return 0;
    char line[160];
    double value = 0;
    const size_t length = std::strlen(key);
    while (std::fgets(line, sizeof(line), in)) {
        if (std::strncmp(line, key, length) != 0 || line[length] != ':') continue;
        value = std::strtod(line + length + 1, nullptr) / 1024.0;
        break;
    }
    std::fclose(in);
    return value;
}

// table dxGetStatus(). CLuaDrawingDefs.cpp:1635-1768, the same keys. The phone has no Direct3D
// device or MTA video settings, so only the test mode and the physical memory are measured. The
// card name is "Android", the memory and capability numbers are 0, and the settings are the
// values of a client that has every optional effect off.
int DxGetStatus(lua_State* L) {
    lua_createtable(L, 0, 34);
    auto text = [&](const char* key, const char* value) { lua_pushstring(L, value); lua_setfield(L, -2, key); };
    auto number = [&](const char* key, double value) { lua_pushnumber(L, value); lua_setfield(L, -2, key); };
    auto flag = [&](const char* key, bool value) { lua_pushboolean(L, value ? 1 : 0); lua_setfield(L, -2, key); };
    text("TestMode", kTestModes[g_testMode]);
    text("VideoCardName", "Android");
    number("VideoCardRAM", 0);
    text("VideoCardPSVersion", "0");
    number("VideoCardMaxAnisotropy", 0);
    number("VideoCardNumRenderTargets", 0);
    number("VideoMemoryFreeForMTA", 0);
    number("VideoMemoryUsedByFonts", 0);
    number("VideoMemoryUsedByTextures", 0);
    number("VideoMemoryUsedByRenderTargets", 0);
    flag("SettingWindowed", false);
    number("SettingFullScreenStyle", 0);
    number("SettingFXQuality", 0);
    number("SettingDrawDistance", 0);
    flag("SettingVolumetricShadows", false);
    number("SettingStreamingVideoMemoryForGTA", 0);
    flag("AllowScreenUpload", false);
    text("DepthBufferFormat", "unknown");
    flag("UsingDepthBuffer", false);
    flag("Setting32BitColor", true);
    flag("SettingGrassEffect", false);
    flag("SettingHeatHaze", false);
    number("SettingAnisotropicFiltering", 0);
    number("SettingAntiAliasing", 0);
    text("SettingAspectRatio", "auto");
    flag("SettingHUDMatchAspectRatio", false);
    number("SettingFOV", 70);
    flag("SettingHighDetailVehicles", false);
    flag("SettingHighDetailPeds", false);
    flag("SettingBlur", false);
    flag("SettingCoronaReflections", false);
    flag("SettingDynamicPedShadows", false);
    number("TotalPhysicalMemory", MemInfoMb("MemTotal"));
    text("SettingDebugMode", "Default");
    return 1;
}

// A texture element, or a file path of the running resource (desktop MixedReadMaterialString).
bool MaterialFile(lua_State* L, int index, std::string& file) {
    if (lua_istable(L, index)) return DxTextureFile(ElementIdOf(L, index), file);
    if (lua_type(L, index) != LUA_TSTRING) return false;
    size_t length = 0;
    const char* raw = lua_tolstring(L, index, &length);
    file = ActiveVmResourceFile(std::string(raw ? raw : "", length));
    return !file.empty();
}

// bool dxDrawMaterialPrimitive (string primitiveType, dxMaterial material, bool postGUI, table vertice1, ...)
// CLuaDrawingDefs.cpp:755-807. A vertex is {x, y, u, v} or {x, y, color, u, v}; u and v run 0-1
// across the image. Frame line: "M type count x y color u v ... file".
PrimitiveError QueueMaterialPrimitive(lua_State* L, int type, int& count) {
    std::string file;
    if (!MaterialFile(L, 2, file)) return PrimitiveError::Material;
    std::string line;
    size_t vertices = 0;
    char part[256];
    for (int index = 4; lua_istable(L, index); ++index) {
        double values[5] = {};
        if (!ReadVertexNumbers(L, index, values, count)) return PrimitiveError::Number;
        if (count == 4) {
            std::snprintf(part, sizeof(part), " %.1f %.1f %u %.4f %.4f", values[0], values[1], 0xFFFFFFFFu, values[2], values[3]);
        } else if (count == 5) {
            std::snprintf(part, sizeof(part), " %.1f %.1f %u %.4f %.4f", values[0], values[1],
                          static_cast<unsigned>(ToColor(values[2])), values[3], values[4]);
        } else {
            return PrimitiveError::Size;
        }
        line += part;
        ++vertices;
    }
    if (!ValidPrimitiveSize(vertices, type + 1)) {
        lua_pushboolean(L, 0);
        return PrimitiveError::None;
    }
    std::snprintf(part, sizeof(part), "M %d %zu", type + 1, vertices);
    AppendOrdered(part + line + " " + file + "\n", lua_toboolean(L, 3) != 0);
    lua_pushboolean(L, 1);
    return PrimitiveError::None;
}

int DxDrawMaterialPrimitive(lua_State* L) {
    const int type = lua_type(L, 1) == LUA_TSTRING ? FindName(kPrimitiveTypes, lua_tostring(L, 1)) : -1;
    if (type < 0) {
        return luaL_error(L, "Bad argument @ 'dxDrawMaterialPrimitive' [Expected primitive-type at argument 1, got %s]",
                          lua_isnone(L, 1) ? "none" : luaL_typename(L, 1));
    }
    if (!lua_isboolean(L, 3)) {
        return luaL_error(L, "Bad argument @ 'dxDrawMaterialPrimitive' [Expected bool at argument 3, got %s]",
                          lua_isnone(L, 3) ? "none" : luaL_typename(L, 3));
    }
    int count = 0;
    const PrimitiveError error = QueueMaterialPrimitive(L,type,count);
    if (error == PrimitiveError::Material)
        return luaL_error(L, "Bad argument @ 'dxDrawMaterialPrimitive' [Expected material at argument 2, got %s]",
                          lua_isnone(L, 2) ? "none" : luaL_typename(L, 2));
    if (error == PrimitiveError::Number) return luaL_error(L, "Bad argument @ 'dxDrawMaterialPrimitive' [Expected finite vertex number]");
    if (error == PrimitiveError::Size)
        return luaL_error(L, "Bad argument @ 'dxDrawMaterialPrimitive' [Expected table with 4 or 5 numbers, got %d numbers]",count);
    return 1;
}

// RemoveColorCodes: every "#RRGGBB" is dropped.
std::string StripColorCodes(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        bool code = text[i] == '#' && i + 7 <= text.size();
        for (size_t k = 1; code && k <= 6; ++k) code = std::isxdigit(static_cast<unsigned char>(text[i + k])) != 0;
        if (code) {
            i += 7;
            continue;
        }
        out.push_back(text[i++]);
    }
    return out;
}

// float dxGetTextWidth ( string text, [float scale=1, mixed font="default", bool colorCoded=false] )
// CLuaDrawingDefs.cpp:800-848: the longest line.
int DxGetTextWidth(lua_State* L) {
    const int textType = lua_type(L, 1);
    if (textType != LUA_TSTRING && textType != LUA_TNUMBER) return Fail(L, "dxGetTextWidth", "string", 1);
    double scale = 1;
    bool colorCoded = false;
    if (!ReadNumberOr(L, 2, 1, scale)) return Fail(L, "dxGetTextWidth", "number", 2);
    if (IsGuiFontArgument(L, 3)) return Fail(L, "dxGetTextWidth", "dx-font or font name", 3);
    if (!ReadBoolOr(L, 4, false, colorCoded)) return Fail(L, "dxGetTextWidth", "bool", 4);
    size_t length = 0;
    const char* raw = lua_tolstring(L, 1, &length);
    const std::string text(raw ? raw : "", length);
    const DxFont font = DxResolveFont(L, 3, scale);
    double width = 0;
    for (size_t at = 0; at <= text.size();) {
        const size_t end = text.find('\n', at);
        const size_t stop = end == std::string::npos ? text.size() : end;
        std::string line = text.substr(at, stop - at);
        if (colorCoded) line = StripColorCodes(line);
        const double extent = DxMeasureLine(line, font);
        if (extent > width) width = extent;
        if (end == std::string::npos) break;
        at = end + 1;
    }
    lua_pushnumber(L, width);
    return 1;
}

// float, float dxGetTextSize ( string text, [float width=0, float scaleXY=1.0, float=scaleY=1.0,
//   mixed font="default", bool wordBreak=false, bool colorCoded=false] )
// CLuaDrawingDefs.cpp:894-923 and CGraphics::GetDXTextSize: the widest line and the line count
// times the font height. With wordBreak and a width, a line breaks at the last space that fits.
int DxGetTextSize(lua_State* L) {
    const int textType = lua_type(L, 1);
    if (textType != LUA_TSTRING && textType != LUA_TNUMBER) {
        return luaL_error(L, "Bad argument @ 'dxGetTextSize' [Expected string at argument 1, got %s]",
                          lua_isnone(L, 1) ? "none" : luaL_typename(L, 1));
    }
    size_t length = 0;
    const char* raw = lua_tolstring(L, 1, &length);
    int index = 2;
    double limit = 0, scaleX = 1, scaleY = 1;
    if (lua_type(L, index) == LUA_TNUMBER) limit = lua_tonumber(L, index++);
    else if (lua_isnil(L, index)) ++index;
    if (lua_type(L, index) == LUA_TNUMBER) {
        scaleX = scaleY = lua_tonumber(L, index++);
        if (lua_type(L, index) == LUA_TNUMBER) scaleY = lua_tonumber(L, index++);
    } else if (lua_isnil(L, index)) {
        ++index;
    }
    const int fontIndex = index;
    // Desktop's typed variant rejects gui-font before entering GetDXTextSize.
    // Raise the Lua error before constructing any C++ strings (longjmp).
    if (IsGuiFontArgument(L, fontIndex))
        return luaL_error(L, "Bad argument @ 'dxGetTextSize' [Expected dx-font or font name at argument %d, got gui-font]", fontIndex);
    std::string text(raw ? raw : "", length);
    const bool wordBreak = lua_isboolean(L, index + 1) && lua_toboolean(L, index + 1) != 0;
    const bool colorCoded = lua_isboolean(L, index + 2) && lua_toboolean(L, index + 2) != 0;

    // CGraphics::GetDXTextSize returns without touching the size for an empty text.
    if (text.empty() || scaleX == 0) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        return 2;
    }
    if (colorCoded) text = StripColorCodes(text);
    const DxFont fontX = DxResolveFont(L, fontIndex, scaleX);
    const double lineHeight = DxResolveFont(L, fontIndex, scaleY).pixels;
    const double wrap = wordBreak && limit > 0 ? limit : 0;
    double width = 0;
    int lines = 0;
    for (size_t at = 0; at <= text.size();) {
        const size_t end = text.find('\n', at);
        const size_t stop = end == std::string::npos ? text.size() : end;
        std::string rest = text.substr(at, stop - at);
        do {
            std::string line = rest;
            rest.clear();
            if (wrap > 0 && DxMeasureLine(line, fontX) > wrap) {
                // The longest run of whole words that fits; one word alone stays on its line.
                size_t cut = std::string::npos;
                for (size_t space = line.find(' '); space != std::string::npos; space = line.find(' ', space + 1)) {
                    if (DxMeasureLine(line.substr(0, space), fontX) > wrap) break;
                    cut = space;
                }
                if (cut == std::string::npos) cut = line.find(' ');
                if (cut != std::string::npos && cut > 0) {
                    rest = line.substr(cut + 1);
                    line.resize(cut);
                }
            }
            const double extent = DxMeasureLine(line, fontX);
            if (extent > width) width = extent;
            ++lines;
        } while (!rest.empty());
        if (end == std::string::npos) break;
        at = end + 1;
    }
    lua_pushnumber(L, width);
    lua_pushnumber(L, lines * lineHeight);
    return 2;
}

}  // namespace

const char* DxElementType(uint32_t id) {
    if (DxShader::Exists(id)) return "shader";
    if (FindTexture(id)) return "texture";
    if (const Font* font = FindFont(id)) return font->gui ? "gui-font" : "dx-font";
    std::string bitmap;
    unsigned width = 0, height = 0;
    if (SvgInfo(id, bitmap, width, height)) return "svg";
    return nullptr;
}

// An svg element is a texture too (desktop CClientVectorGraphic is a CClientTexture).
bool DxTextureFile(uint32_t id, std::string& file) {
    if (const Texture* texture = FindTexture(id)) {
        file = texture->file;
        return true;
    }
    unsigned width = 0, height = 0;
    return SvgInfo(id, file, width, height);
}

bool DxResolveMaterial(lua_State* L,int index,std::string& file,unsigned& width,unsigned& height) {
    if (lua_istable(L,index)) {
        const uint32_t id=ElementIdOf(L,index);
        if (const Texture* texture=FindTexture(id)) {
            file=texture->file; width=texture->width; height=texture->height; return true;
        }
        return SvgInfo(id,file,width,height);
    }
    if (lua_type(L,index)!=LUA_TSTRING) return false;
    size_t length=0; const char* path=lua_tolstring(L,index,&length);
    file=ActiveVmResourceFile(std::string(path,length));
    return !file.empty() && ImageSize(file,width,height);
}

void DxFreeOverlayBitmap(uint32_t id) { g_deadTargets.push_back(id); }

bool DxElementRelease(uint32_t id) {
    if (DxShader::Release(id)) return true;
    for (auto it = g_textures.begin(); it != g_textures.end(); ++it) {
        if (it->id != id) continue;
        ForgetTarget(*it);
        g_textures.erase(it);
        return true;
    }
    for (auto it = g_fonts.begin(); it != g_fonts.end(); ++it) {
        if (it->id != id) continue;
        if (it->gui) {
            GuiDetachFont(id);
            DxFreeOverlayBitmap(id); // D also releases the pinned GUI Typeface after older draws.
        }
        g_fonts.erase(it);
        return true;
    }
    return SvgRelease(ActiveVmState(), id);
}

void DxDropResource(const std::string& owner, uint16_t netId, uint32_t generation) {
    DxShader::DropResource(owner,netId,generation);
    Dx3D::DropResource(owner,netId,generation);
    ModelEffects::DropResource(owner,netId,generation);
    auto mine = [&](const std::string& itemOwner, uint16_t itemNet, uint32_t itemGeneration) {
        return itemOwner == owner && (netId == 0 || itemNet == 0 || itemNet == netId) &&
               (generation == 0 || itemGeneration == 0 || itemGeneration == generation);
    };
    for (auto it = g_textures.begin(); it != g_textures.end();) {
        if (!mine(it->owner, it->netId, it->generation)) {
            ++it;
            continue;
        }
        ForgetTarget(*it);
        it = g_textures.erase(it);
    }
    for (auto it = g_fonts.begin(); it != g_fonts.end();) {
        if (!mine(it->owner, it->netId, it->generation)) { ++it; continue; }
        if (it->gui) {
            GuiDetachFont(it->id);
            DxFreeOverlayBitmap(it->id);
        }
        it = g_fonts.erase(it);
    }
    SvgDropResource(ActiveVmState(), owner, netId, generation);
}

// MixedReadDxFontString (CLuaFunctionParseHelpers.cpp:1119-1134): a dx-font element, else a
// standard font name. An unknown name or a destroyed font is the default font.
DxFont DxResolveFont(lua_State* L, int index, double scale) {
    DxFont out;
    if (lua_istable(L, index)) {
        if (const Font* font = FindFont(ElementIdOf(L, index)); font && !font->gui) {
            out.pixels = font->height * scale;
            // Version the Java Typeface cache and native width cache by font
            // instance. Resource reloads may replace a file at the same path.
            out.file = FontSource(*font);
            out.bold = font->bold;
            return out;
        }
    }
    const char* name = lua_type(L, index) == LUA_TSTRING ? lua_tostring(L, index) : "default";
    out.pixels = 15.0 * scale * DxFontScale(name);
    return out;
}

// CLuaGUIDefs.cpp:4009-4055, CRenderItem.GuiFont.cpp:83-107. GUI sizes are
// points at CEGUI's fixed 96 DPI, not the DX font's floor(size * 1.75) pixels.
uint32_t DxCreateGuiFont(const std::string& path, uint32_t pointSize) {
    const std::string file = ActiveVmResourceFile(path);
    if (file.empty()) return 0;
    std::FILE* in = std::fopen(file.c_str(), "rb");
    if (!in) return 0;
    unsigned char magic[4] = {};
    const size_t got = std::fread(magic, 1, sizeof(magic), in);
    std::fclose(in);
    if (got != 4 || (std::memcmp(magic, "\x00\x01\x00\x00", 4) != 0 && std::memcmp(magic, "true", 4) != 0 &&
                     std::memcmp(magic, "OTTO", 4) != 0 && std::memcmp(magic, "ttcf", 4) != 0)) return 0;
    Font font;
    font.id = g_nextFont++;
    font.file = file;
    font.gui = true;
    const uint32_t size = std::clamp(pointSize, 5u, 150u);
    font.guiPixels = size * (96.0 / 72.0);
    font.owner = ActiveVmOwner();
    font.netId = ActiveVmOwnerNet();
    font.generation = ActiveVmOwnerGeneration();
    // Android loads and validates the real font now; queued GUI draws use this
    // creation identity. The host only checks the header and estimates metrics.
    const float height = WorldRPG::JniBridge::FontHeight(FontSource(font), static_cast<float>(font.guiPixels), false);
#ifdef __ANDROID__
    if (!(height > 0)) return 0;
#else
    if (height == 0) return 0;
#endif
    std::string meta = path;
    for (char& ch : meta) if (ch == '\\') ch = '/';
    std::string fileOwner = font.owner;
    if (!meta.empty() && meta.front() == ':') {
        const size_t slash = meta.find('/');
        fileOwner = meta.substr(1, slash - 1);
        meta.erase(0, slash + 1);
    }
    const std::string base = font.owner + "*" + fileOwner + "*" + meta + "*" + std::to_string(size) + "*";
    unsigned counter = 0;
    do { font.guiName = base + std::to_string(counter++); } while (DxGuiFontByName(font.guiName));
    g_fonts.push_back(font);
    return font.id;
}

bool DxGuiFontInfo(uint32_t id, DxFont& renderFont, std::string& name) {
    const Font* font = FindFont(id);
    if (!font || !font->gui) return false;
    renderFont.pixels = font->guiPixels;
    renderFont.file = FontSource(*font);
    renderFont.bold = false;
    name = font->guiName;
    return true;
}

uint32_t DxGuiFontByName(const std::string& name) {
    for (const Font& font : g_fonts) if (font.gui && font.guiName == name) return font.id;
    return 0;
}

double DxMeasureFontHeight(const DxFont& font) {
    static std::unordered_map<std::string, float> cache;
    const std::string key = font.file + "|" + std::to_string(font.pixels) + (font.bold ? "|b" : "|n");
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    const float height = WorldRPG::JniBridge::FontHeight(font.file, static_cast<float>(font.pixels), font.bold);
    if (!(height > 0)) return font.pixels * 1.2;
    if (cache.size() >= 4096) cache.clear();
    cache.emplace(key, height);
    return height;
}

double DxMeasureLine(const std::string& text, const DxFont& font) {
    const double pixelSize = font.pixels;
    if (text.empty() || !(pixelSize > 0)) return 0;
    // One JNI call per new string and size; the same labels are measured every frame.
    static std::unordered_map<std::string, float> cache;
    char key[32];
    std::snprintf(key, sizeof(key), "%.2f|%d|", pixelSize, font.bold ? 1 : 0);
    const std::string cacheKey = key + font.file + "|" + text;
    const auto found = cache.find(cacheKey);
    if (found != cache.end()) return found->second;
    const float measured = WorldRPG::JniBridge::MeasureText(text, static_cast<float>(pixelSize), font.file, font.bold);
    // The estimate is not cached: the Activity may be able to answer on a later call.
    if (measured < 0) return static_cast<double>(text.size()) * 7.0 * pixelSize / 15.0;
    const float width = measured;
    if (cache.size() >= 2048) cache.clear();
    cache.emplace(cacheKey, width);
    return width;
}

void DxBeginFrame(std::string& frame) {
    Dx3D::BeginFrame();
    // Every frame starts on the screen (desktop restores the default target after the render events).
    g_activeTarget = 0;
    for (uint32_t id : g_deadTargets) {
        char line[24];
        // "D id": the overlay frees the bitmaps of this id.
        std::snprintf(line, sizeof(line), "D %u\n", static_cast<unsigned>(id));
        frame += line;
    }
    g_deadTargets.clear();
    if (g_blendMode == 0) return;
    char line[16];
    std::snprintf(line, sizeof(line), "B %d\n", g_blendMode);
    frame += line;
}

void DxAppendDraw(const std::string& commands, bool postGUI) { AppendOrdered(commands, postGUI); }
bool DxIsOverwriteBlend() { return g_blendMode==3; }
void DxDestinationSize(int& width,int& height) {
    if(const Texture* texture=FindTexture(g_activeTarget);texture && texture->renderTarget) {
        width=texture->width;height=texture->height;
    } else ActiveVmScreenSize(width,height);
}

void DxEndRender(std::string& frame) {
    Dx3D::EndFrame();
    if (g_activeTarget == 0) return;
    g_activeTarget = 0;
    frame += "G\n";
}

void DxReset() {
    DxShader::Reset();
    Dx3D::Reset();
    ModelEffects::Reset();
    SvgReset(ActiveVmState());
    g_blendMode = 0;
    g_testMode = 0;
    for (const Texture& texture : g_textures)
        if (texture.renderTarget || texture.memory) g_deadTargets.push_back(texture.id);
    g_textures.clear();
    for (const Font& font : g_fonts) {
        if (!font.gui) continue;
        GuiDetachFont(font.id);
        DxFreeOverlayBitmap(font.id);
    }
    g_fonts.clear();
    g_activeTarget = 0;
}

void InstallDxLua(lua_State* L) {
    DxReset();
    DxShader::InstallShaderLua(L);
    Dx3D::InstallDx3DLua(L);
    ModelEffects::InstallModelEffectsLua(L);
    static const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"dxDrawLine", DxDrawLine},
        {"dxDrawCircle", DxDrawCircle},
        {"dxDrawPrimitive", DxDrawPrimitive},
        {"dxSetBlendMode", DxSetBlendMode},
        {"dxGetBlendMode", DxGetBlendMode},
        {"dxSetAspectRatioAdjustmentEnabled", DxSetAspectRatioAdjustmentEnabled},
        {"dxIsAspectRatioAdjustmentEnabled", DxIsAspectRatioAdjustmentEnabled},
        {"dxSetTestMode", DxSetTestMode},
        {"dxGetTextWidth", DxGetTextWidth},
        {"dxGetTextSize", DxGetTextSize},
        {"dxCreateTexture", DxCreateTexture},
        {"dxGetMaterialSize", DxGetMaterialSize},
        {"dxSetTextureEdge", DxSetTextureEdge},
        {"dxGetPixelsSize", DxGetPixelsSize},
        {"dxConvertPixels", DxConvertPixels},
        {"dxGetTexturePixels", DxGetTexturePixels},
        {"dxSetTexturePixels", DxSetTexturePixels},
        {"dxCreateRenderTarget", DxCreateRenderTarget},
        {"dxSetRenderTarget", DxSetRenderTarget},
        {"dxCreateScreenSource", DxCreateScreenSource},
        {"dxUpdateScreenSource", DxUpdateScreenSource},
        {"dxCreateFont", DxCreateFont},
        {"dxGetFontHeight", DxGetFontHeight},
        {"dxGetStatus", DxGetStatus},
        {"dxDrawMaterialPrimitive", DxDrawMaterialPrimitive},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
