#include "LuaSvgApi.h"

#include "LuaApiExtra.h"
#include "LuaDxApi.h"
#include "LuaVMStub.h"
#include "LuaXmlApi.h"

#include "lua.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace WorldRPG::JniBridge {
// Renders the SVG document into the overlay bitmap of this name at this size.
// 1 drawn, 0 the document could not be read as SVG, -1 the Activity is not available.
int RenderSvg(const std::string& name, const std::string& xml, unsigned width, unsigned height);
}

namespace WorldRPG::Script {
namespace {

struct Svg {
    uint32_t id = 0;
    unsigned width = 0, height = 0;
    std::string xml;
    int callback = LUA_NOREF;
    // The picture changed and the callback has not run yet.
    bool updated = false;
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};

// VM thread only.
std::vector<Svg> g_svgs;
uint32_t g_nextSvg = 0x35000001u;
// A document larger than this is refused (desktop reads up to 50 MB; a phone frame cannot).
constexpr size_t kMaxDocument = 4u * 1024u * 1024u;

Svg* FindSvg(uint32_t id) {
    for (Svg& svg : g_svgs)
        if (svg.id == id) return &svg;
    return nullptr;
}

Svg* SvgArg(lua_State* L, int index) {
    return lua_istable(L, index) ? FindSvg(ElementIdOf(L, index)) : nullptr;
}

std::string BitmapName(uint32_t id) { return "tx:" + std::to_string(id); }

// The text after the XML declaration, comments and DOCTYPE that may precede the root element.
std::string RootElementText(const std::string& text) {
    size_t at = 0;
    for (;;) {
        at = text.find_first_not_of(" \t\r\n\f\v", at);
        if (at == std::string::npos) return {};
        if (text.compare(at, 2, "<?") == 0) {
            const size_t end = text.find("?>", at);
            if (end == std::string::npos) return {};
            at = end + 2;
        } else if (text.compare(at, 4, "<!--") == 0) {
            const size_t end = text.find("-->", at);
            if (end == std::string::npos) return {};
            at = end + 3;
        } else if (text.compare(at, 2, "<!") == 0) {
            const size_t end = text.find('>', at);
            if (end == std::string::npos) return {};
            at = end + 1;
        } else {
            return text.substr(at);
        }
    }
}

// CClientVectorGraphic::LoadFromString: the text must parse as XML. The overlay then draws it;
// a document it cannot read as SVG is a failed load as well. With no Activity (host build) the
// document is kept and nothing is drawn.
bool LoadDocument(Svg& svg, const std::string& xml) {
    if (xml.empty() || xml.size() > kMaxDocument) return false;
    if (!XmlValidateString(RootElementText(xml))) return false;
    const int drawn = WorldRPG::JniBridge::RenderSvg(BitmapName(svg.id), xml, svg.width, svg.height);
    if (drawn == 0) return false;
    if (drawn < 0) LUA_LOGI("DIAG svg %u: no overlay to draw the document, kept without a picture", static_cast<unsigned>(svg.id));
    svg.xml = xml;
    svg.updated = true;
    return true;
}

// The size checks shared by svgCreate and svgSetSize. They raise a Lua error, as on desktop.
void CheckSize(lua_State* L, double width, double height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1)
        luaL_error(L, "A vector graphic must be atleast 1x1 in size.");
    if (width > 4096 || height > 4096) luaL_error(L, "A vector graphic cannot exceed 4096x4096 in size.");
}

void SetCallback(lua_State* L, Svg& svg, int index) {
    if (!lua_isfunction(L, index)) return;
    if (svg.callback != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, svg.callback);
    lua_pushvalue(L, index);
    svg.callback = luaL_ref(L, LUA_REGISTRYINDEX);
}

void Destroy(lua_State* L, const Svg& svg) {
    if (L && svg.callback != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, svg.callback);
    DxFreeOverlayBitmap(svg.id);
}

bool ReadFile(const std::string& file, std::string& out) {
    std::FILE* in = std::fopen(file.c_str(), "rb");
    if (!in) return false;
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), in)) > 0) {
        out.append(buffer, got);
        if (out.size() > kMaxDocument) break;
    }
    std::fclose(in);
    return true;
}

// svg svgCreate ( int width, int height [, string pathOrRawData, function callback ] )
// CLuaVectorGraphicDefs.cpp:108-185. Text that starts with '<' is the document; anything else
// is a file path of the resource. A failed load returns false.
int SvgCreate(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        return luaL_error(L, "Bad argument @ 'svgCreate' [Expected number at argument %d, got %s]", lua_isnumber(L, 1) ? 2 : 1,
                          lua_isnone(L, lua_isnumber(L, 1) ? 2 : 1) ? "none" : luaL_typename(L, lua_isnumber(L, 1) ? 2 : 1));
    }
    const double width = lua_tonumber(L, 1), height = lua_tonumber(L, 2);
    CheckSize(L, width, height);
    if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TSTRING) {
        return luaL_error(L, "Bad argument @ 'svgCreate' [Expected string at argument 3, got %s]", luaL_typename(L, 3));
    }
    Svg svg;
    svg.id = g_nextSvg++;
    svg.width = static_cast<unsigned>(width);
    svg.height = static_cast<unsigned>(height);
    svg.owner = ActiveVmOwner();
    svg.netId = ActiveVmOwnerNet();
    svg.generation = ActiveVmOwnerGeneration();
    if (lua_type(L, 3) == LUA_TSTRING) {
        size_t length = 0;
        const char* raw = lua_tolstring(L, 3, &length);
        const std::string data(raw, length);
        const size_t first = data.find_first_not_of(" \t\n\r\f\v");
        bool loaded = false;
        if (first != std::string::npos && data[first] == '<') {
            loaded = LoadDocument(svg, data);
            if (!loaded) LUA_LOGI("WARNING: Unable to load SVG data (check for XML syntax errors)");
        } else {
            const std::string file = ActiveVmResourceFile(data);
            std::string text;
            if (file.empty()) {
                LUA_LOGI("WARNING: Unable to load SVG (invalid file path) [%.200s]", data.c_str());
            } else if (!ReadFile(file, text)) {
                LUA_LOGI("WARNING: Unable to load SVG (file doesn't exist) [%.200s]", data.c_str());
            } else {
                loaded = LoadDocument(svg, text);
                if (!loaded) LUA_LOGI("WARNING: Unable to load SVG file (check for XML syntax errors)");
            }
        }
        if (!loaded) {
            DxFreeOverlayBitmap(svg.id);
            lua_pushboolean(L, 0);
            return 1;
        }
        SetCallback(L, svg, 4);
    }
    g_svgs.push_back(svg);
    PushElement(L, svg.id);
    return 1;
}

// xmlnode svgGetDocumentXML ( svg svgElement ). CLuaVectorGraphicDefs.cpp:187-190. The node is
// parsed from the stored document on each call; it is not a live view of it, so a change made
// through the node takes effect with svgSetDocumentXML.
int SvgGetDocumentXML(lua_State* L) {
    const Svg* svg = SvgArg(L, 1);
    if (!svg) return luaL_error(L, "Bad argument @ 'svgGetDocumentXML' [Expected svg at argument 1, got %s]",
                                lua_isnone(L, 1) ? "none" : luaL_typename(L, 1));
    const int node = svg->xml.empty() ? 0 : XmlParseString(RootElementText(svg->xml));
    if (node == 0) lua_pushboolean(L, 0);
    else lua_pushinteger(L, node);
    return 1;
}

// bool svgSetDocumentXML ( svg svgElement, xmlnode xmlDocument [, function callback ] )
// CLuaVectorGraphicDefs.cpp:192-204.
int SvgSetDocumentXML(lua_State* L) {
    Svg* svg = SvgArg(L, 1);
    if (!svg) return luaL_error(L, "Bad argument @ 'svgSetDocumentXML' [Expected svg at argument 1, got %s]",
                                lua_isnone(L, 1) ? "none" : luaL_typename(L, 1));
    std::string xml;
    if (!XmlNodeString(L, 2, xml)) {
        return luaL_error(L, "Bad argument @ 'svgSetDocumentXML' [Expected xml-node at argument 2, got %s]",
                          lua_isnone(L, 2) ? "none" : luaL_typename(L, 2));
    }
    const bool loaded = LoadDocument(*svg, xml);
    SetCallback(L, *svg, 3);
    lua_pushboolean(L, loaded ? 1 : 0);
    return 1;
}

// int, int svgGetSize ( svg svgElement ). CLuaVectorGraphicDefs.cpp:206-209.
int SvgGetSize(lua_State* L) {
    const Svg* svg = SvgArg(L, 1);
    if (!svg) return luaL_error(L, "Bad argument @ 'svgGetSize' [Expected svg at argument 1, got %s]",
                                lua_isnone(L, 1) ? "none" : luaL_typename(L, 1));
    lua_pushinteger(L, svg->width);
    lua_pushinteger(L, svg->height);
    return 2;
}

// bool svgSetSize ( svg svgElement, int width, int height [, function callback ] )
// CLuaVectorGraphicDefs.cpp:211-226: the picture is drawn again at the new size.
int SvgSetSize(lua_State* L) {
    Svg* svg = SvgArg(L, 1);
    if (!svg) return luaL_error(L, "Bad argument @ 'svgSetSize' [Expected svg at argument 1, got %s]",
                                lua_isnone(L, 1) ? "none" : luaL_typename(L, 1));
    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Bad argument @ 'svgSetSize' [Expected number at argument %d, got %s]", lua_isnumber(L, 2) ? 3 : 2,
                          lua_isnone(L, lua_isnumber(L, 2) ? 3 : 2) ? "none" : luaL_typename(L, lua_isnumber(L, 2) ? 3 : 2));
    }
    const double width = lua_tonumber(L, 2), height = lua_tonumber(L, 3);
    CheckSize(L, width, height);
    svg->width = static_cast<unsigned>(width);
    svg->height = static_cast<unsigned>(height);
    if (!svg->xml.empty()) {
        WorldRPG::JniBridge::RenderSvg(BitmapName(svg->id), svg->xml, svg->width, svg->height);
        svg->updated = true;
    }
    SetCallback(L, *svg, 4);
    lua_pushboolean(L, 1);
    return 1;
}

// bool svgSetUpdateCallback ( svg svgElement, function callback | false )
// CLuaVectorGraphicDefs.cpp:228-243.
int SvgSetUpdateCallback(lua_State* L) {
    Svg* svg = SvgArg(L, 1);
    if (!svg) return luaL_error(L, "Bad argument @ 'svgSetUpdateCallback' [Expected svg at argument 1, got %s]",
                                lua_isnone(L, 1) ? "none" : luaL_typename(L, 1));
    if (lua_isfunction(L, 2)) {
        SetCallback(L, *svg, 2);
        lua_pushboolean(L, 1);
        return 1;
    }
    if (lua_isboolean(L, 2) && lua_toboolean(L, 2) == 0) {
        if (svg->callback != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, svg->callback);
        svg->callback = LUA_NOREF;
        lua_pushboolean(L, 1);
        return 1;
    }
    if (lua_isboolean(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    return luaL_error(L, "Bad argument @ 'svgSetUpdateCallback' [Expected function at argument 2, got %s]",
                      lua_isnone(L, 2) ? "none" : luaL_typename(L, 2));
}

}  // namespace

bool SvgInfo(uint32_t id, std::string& bitmap, unsigned& width, unsigned& height) {
    const Svg* svg = FindSvg(id);
    if (!svg) return false;
    bitmap = BitmapName(id);
    width = svg->width;
    height = svg->height;
    return true;
}

bool SvgRelease(lua_State* L, uint32_t id) {
    for (auto it = g_svgs.begin(); it != g_svgs.end(); ++it) {
        if (it->id != id) continue;
        Destroy(L, *it);
        g_svgs.erase(it);
        return true;
    }
    return false;
}

void SvgDropResource(lua_State* L, const std::string& owner, uint16_t netId, uint32_t generation) {
    for (auto it = g_svgs.begin(); it != g_svgs.end();) {
        const bool mine = it->owner == owner && (netId == 0 || it->netId == 0 || it->netId == netId) &&
                          (generation == 0 || it->generation == 0 || it->generation == generation);
        if (!mine) {
            ++it;
            continue;
        }
        Destroy(L, *it);
        it = g_svgs.erase(it);
    }
}

void SvgTakeUpdates(std::vector<SvgUpdate>& out) {
    for (Svg& svg : g_svgs) {
        if (!svg.updated) continue;
        svg.updated = false;
        if (svg.callback == LUA_NOREF) continue;
        out.push_back(SvgUpdate{svg.callback, svg.id, svg.owner, svg.netId, svg.generation});
    }
}

bool SvgCallbackLive(uint32_t id, int ref) {
    const Svg* svg = FindSvg(id);
    return svg && svg->callback == ref;
}

void SvgReset(lua_State* L) {
    for (const Svg& svg : g_svgs) Destroy(L,svg);
    g_svgs.clear();
}

void InstallSvgLua(lua_State* L) {
    // Normal shutdown already releases refs through DxReset. A standalone
    // installation may instead follow destruction of the old registry.
    SvgReset(nullptr);
    static const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"svgCreate", SvgCreate},
        {"svgGetDocumentXML", SvgGetDocumentXML},
        {"svgSetDocumentXML", SvgSetDocumentXML},
        {"svgGetSize", SvgGetSize},
        {"svgSetSize", SvgSetSize},
        {"svgSetUpdateCallback", SvgSetUpdateCallback},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
