#include "LuaGuiApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace WorldRPG::Script {

namespace {

// Server element ids are 17 bits, so GUI ids start above them and never collide.
constexpr uint32_t kFirstGuiId = 0x40000000u;

// Desktop "default" font (tahoma, D3DX height 15) is the scale-1 reference size.
constexpr double kDefaultFontHeight = 15.0;

// Colors as ARGB, the format the R and T draw lines use.
constexpr uint32_t kWindowBackground = 0xE0101820u;
constexpr uint32_t kButtonBackground = 0xE0304050u;
constexpr uint32_t kTextWhite = 0xFFFFFFFFu;
constexpr uint32_t kTextDisabled = 0x80FFFFFFu;

struct Box {
    double x = 0, y = 0, w = 0, h = 0;
};

// One GUI element. Position and size are stored as given: pixels, or a fraction of the
// parent box when the matching relative flag is set (desktop SetPosition/SetSize).
struct GuiElement {
    std::string type;
    std::string owner;              // resource that created it
    uint32_t parent = 0;            // 0 = top level, drawn on the screen
    double x = 0, y = 0, w = 0, h = 0;
    bool relPos = false;
    bool relSize = false;
    std::string text;               // window title, label or button text
    bool visible = true;
    bool enabled = true;
    double alpha = 1.0;
};

// Ordered by id, so creation order is the draw order: later elements are drawn on top.
std::map<uint32_t, GuiElement> g_elements;
uint32_t g_nextId = kFirstGuiId;
bool g_cursorShown = false;
bool g_inputEnabled = true;

GuiElement* Find(uint32_t id) {
    auto found = g_elements.find(id);
    return found == g_elements.end() ? nullptr : &found->second;
}

Box ScreenBox(uint32_t id);

// The box an element is placed in: its parent's screen box, or the whole screen.
Box ParentBox(const GuiElement& element) {
    if (element.parent && Find(element.parent)) return ScreenBox(element.parent);
    int width = 0, height = 0;
    ActiveVmScreenSize(width, height);
    return Box{0.0, 0.0, static_cast<double>(width), static_cast<double>(height)};
}

// Screen-space box of an element, with its parents' offsets applied.
Box ScreenBox(uint32_t id) {
    const GuiElement* element = Find(id);
    if (!element) return Box{};
    const Box parent = ParentBox(*element);
    Box box;
    box.x = parent.x + (element->relPos ? element->x * parent.w : element->x);
    box.y = parent.y + (element->relPos ? element->y * parent.h : element->y);
    box.w = element->relSize ? element->w * parent.w : element->w;
    box.h = element->relSize ? element->h * parent.h : element->h;
    return box;
}

// Desktop CGUIElement::IsVisible includes the parents: a child shows only when every ancestor does.
bool EffectivelyVisible(const GuiElement& element) {
    const GuiElement* current = &element;
    for (int depth = 0; current && depth < 64; ++depth) {
        if (!current->visible) return false;
        current = current->parent ? Find(current->parent) : nullptr;
    }
    return true;
}

double EffectiveAlpha(const GuiElement& element) {
    double alpha = 1.0;
    const GuiElement* current = &element;
    for (int depth = 0; current && depth < 64; ++depth) {
        alpha *= current->alpha;
        current = current->parent ? Find(current->parent) : nullptr;
    }
    return alpha;
}

// Multiplies the alpha byte of an ARGB color by alpha (0..1).
uint32_t WithAlpha(uint32_t argb, double alpha) {
    const double scaled = static_cast<double>(argb >> 24) * std::clamp(alpha, 0.0, 1.0);
    const uint32_t a = static_cast<uint32_t>(std::clamp(scaled, 0.0, 255.0));
    return (a << 24) | (argb & 0x00FFFFFFu);
}

// Text in one line: the draw format cannot carry a line break.
std::string OneLine(const char* text, size_t length) {
    std::string out(text ? text : "", length);
    for (char& ch : out) {
        if (ch == '\n' || ch == '\r') ch = ' ';
    }
    return out;
}

void AppendRect(std::string& out, const Box& box, uint32_t argb, double alpha) {
    char line[128];
    std::snprintf(line, sizeof(line), "R %.1f %.1f %.1f %.1f %u\n", box.x, box.y, box.w, box.h,
                  static_cast<unsigned>(WithAlpha(argb, alpha)));
    out += line;
}

void AppendText(std::string& out, double left, double top, double right, double bottom, uint32_t argb,
                double alpha, const std::string& text) {
    if (text.empty()) return;
    char head[160];
    std::snprintf(head, sizeof(head), "T %.1f %.1f %.1f %.1f %.2f %u %d ", left, top, right, bottom, 1.0,
                  static_cast<unsigned>(WithAlpha(argb, alpha)), 0);
    out += head;
    out += text;
    out += '\n';
}

// Lua element argument to its GUI element, or nullptr for any other value.
GuiElement* ElementArg(lua_State* L, int index) {
    return Find(ElementIdOf(L, index));
}

// Position or size as the script asked for it: a fraction of the parent when relative,
// otherwise pixels. An element placed in pixels converts to a fraction of its parent.
double Requested(double pixels, double parentSize, bool relative) {
    if (!relative) return pixels;
    return parentSize > 0.0 ? pixels / parentSize : 0.0;
}

// guiCreateWindow(x, y, w, h, title, relative), guiCreateLabel / guiCreateButton(x, y, w, h,
// text, relative, parent). Elements start visible and enabled, as on desktop.
int CreateElement(lua_State* L, const char* type, bool hasParent) {
    GuiElement element;
    element.type = type;
    element.owner = ActiveVmOwner();
    element.x = luaL_optnumber(L, 1, 0.0);
    element.y = luaL_optnumber(L, 2, 0.0);
    element.w = luaL_optnumber(L, 3, 200.0);
    element.h = luaL_optnumber(L, 4, 120.0);
    size_t textLength = 0;
    const char* text = luaL_optlstring(L, 5, "", &textLength);
    element.text = OneLine(text, textLength);
    element.relPos = lua_toboolean(L, 6) != 0;
    element.relSize = element.relPos;
    if (hasParent) {
        const uint32_t parent = ElementIdOf(L, 7);
        if (Find(parent)) element.parent = parent;
    }
    const uint32_t id = g_nextId++;
    g_elements.emplace(id, std::move(element));
    PushElement(L, id);
    return 1;
}

int GuiCreateWindow(lua_State* L) { return CreateElement(L, "gui-window", false); }
int GuiCreateLabel(lua_State* L) { return CreateElement(L, "gui-label", true); }
int GuiCreateButton(lua_State* L) { return CreateElement(L, "gui-button", true); }

// guiSetVisible / guiGetVisible: the element's own flag (desktop IsVisible).
int GuiSetVisible(lua_State* L) {
    GuiElement* element = ElementArg(L, 1);
    if (element) element->visible = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, element ? 1 : 0);
    return 1;
}

int GuiGetVisible(lua_State* L) {
    const GuiElement* element = ElementArg(L, 1);
    lua_pushboolean(L, element && element->visible ? 1 : 0);
    return 1;
}

int GuiSetText(lua_State* L) {
    GuiElement* element = ElementArg(L, 1);
    size_t length = 0;
    const char* text = luaL_optlstring(L, 2, "", &length);
    if (element) element->text = OneLine(text, length);
    lua_pushboolean(L, element ? 1 : 0);
    return 1;
}

int GuiGetText(lua_State* L) {
    const GuiElement* element = ElementArg(L, 1);
    lua_pushstring(L, element ? element->text.c_str() : "");
    return 1;
}

// guiSetPosition(element, x, y, relative) and guiGetPosition(element, relative).
int GuiSetPosition(lua_State* L) {
    GuiElement* element = ElementArg(L, 1);
    if (element) {
        element->x = luaL_optnumber(L, 2, 0.0);
        element->y = luaL_optnumber(L, 3, 0.0);
        element->relPos = lua_toboolean(L, 4) != 0;
    }
    lua_pushboolean(L, element ? 1 : 0);
    return 1;
}

int GuiGetPosition(lua_State* L) {
    const GuiElement* element = ElementArg(L, 1);
    if (!element) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool relative = lua_toboolean(L, 2) != 0;
    const Box parent = ParentBox(*element);
    const double x = element->relPos ? element->x * parent.w : element->x;
    const double y = element->relPos ? element->y * parent.h : element->y;
    lua_pushnumber(L, Requested(x, parent.w, relative));
    lua_pushnumber(L, Requested(y, parent.h, relative));
    return 2;
}

// guiSetSize(element, w, h, relative) and guiGetSize(element, relative).
int GuiSetSize(lua_State* L) {
    GuiElement* element = ElementArg(L, 1);
    if (element) {
        element->w = luaL_optnumber(L, 2, 0.0);
        element->h = luaL_optnumber(L, 3, 0.0);
        element->relSize = lua_toboolean(L, 4) != 0;
    }
    lua_pushboolean(L, element ? 1 : 0);
    return 1;
}

int GuiGetSize(lua_State* L) {
    const GuiElement* element = ElementArg(L, 1);
    if (!element) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool relative = lua_toboolean(L, 2) != 0;
    const Box parent = ParentBox(*element);
    const double w = element->relSize ? element->w * parent.w : element->w;
    const double h = element->relSize ? element->h * parent.h : element->h;
    lua_pushnumber(L, Requested(w, parent.w, relative));
    lua_pushnumber(L, Requested(h, parent.h, relative));
    return 2;
}

int GuiSetAlpha(lua_State* L) {
    GuiElement* element = ElementArg(L, 1);
    if (element) element->alpha = std::clamp(luaL_optnumber(L, 2, 1.0), 0.0, 1.0);
    lua_pushboolean(L, element ? 1 : 0);
    return 1;
}

int GuiGetAlpha(lua_State* L) {
    const GuiElement* element = ElementArg(L, 1);
    lua_pushnumber(L, element ? element->alpha : 0.0);
    return 1;
}

int GuiSetEnabled(lua_State* L) {
    GuiElement* element = ElementArg(L, 1);
    if (element) element->enabled = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, element ? 1 : 0);
    return 1;
}

int GuiGetEnabled(lua_State* L) {
    const GuiElement* element = ElementArg(L, 1);
    lua_pushboolean(L, element && element->enabled ? 1 : 0);
    return 1;
}

// The phone has no mouse cursor, so the cursor and input flags are only remembered.
int ShowCursor(lua_State* L) {
    g_cursorShown = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int IsCursorShowing(lua_State* L) {
    lua_pushboolean(L, g_cursorShown ? 1 : 0);
    return 1;
}

int GuiSetInputEnabled(lua_State* L) {
    g_inputEnabled = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int GuiGetInputEnabled(lua_State* L) {
    lua_pushboolean(L, g_inputEnabled ? 1 : 0);
    return 1;
}

// destroyElement(element): GUI elements are destroyed with their children. Any other
// element keeps the no-op result the phone gave before (no client-side element creation).
int DestroyElement(lua_State* L) {
    GuiDestroy(ElementIdOf(L, 1));
    lua_pushboolean(L, 1);
    return 1;
}

} // namespace

void InstallGuiBindings(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"guiCreateWindow", GuiCreateWindow},
        {"guiCreateLabel", GuiCreateLabel},
        {"guiCreateButton", GuiCreateButton},
        {"guiSetVisible", GuiSetVisible},
        {"guiGetVisible", GuiGetVisible},
        {"guiSetText", GuiSetText},
        {"guiGetText", GuiGetText},
        {"guiSetPosition", GuiSetPosition},
        {"guiGetPosition", GuiGetPosition},
        {"guiSetSize", GuiSetSize},
        {"guiGetSize", GuiGetSize},
        {"guiSetAlpha", GuiSetAlpha},
        {"guiGetAlpha", GuiGetAlpha},
        {"guiSetEnabled", GuiSetEnabled},
        {"guiGetEnabled", GuiGetEnabled},
        {"guiSetInputEnabled", GuiSetInputEnabled},
        {"guiGetInputEnabled", GuiGetInputEnabled},
        {"showCursor", ShowCursor},
        {"isCursorShowing", IsCursorShowing},
        {"destroyElement", DestroyElement},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

bool GuiIsElement(uint32_t id) {
    return Find(id) != nullptr;
}

const char* GuiElementType(uint32_t id) {
    const GuiElement* element = Find(id);
    return element ? element->type.c_str() : nullptr;
}

bool GuiDestroy(uint32_t id) {
    if (!Find(id)) return false;
    // The element and every element below it (children of children, in creation order).
    std::vector<uint32_t> doomed{id};
    for (size_t i = 0; i < doomed.size(); ++i) {
        for (const auto& [childId, child] : g_elements) {
            if (child.parent == doomed[i]) doomed.push_back(childId);
        }
    }
    for (uint32_t gone : doomed) g_elements.erase(gone);
    return true;
}

void GuiDropResource(const std::string& owner) {
    if (owner.empty()) return;
    std::vector<uint32_t> mine;
    for (const auto& [id, element] : g_elements) {
        if (element.owner == owner) mine.push_back(id);
    }
    for (uint32_t id : mine) GuiDestroy(id);
}

void GuiReset() {
    g_cursorShown = false;
    g_elements.clear();
    g_cursorShown = false;
    g_inputEnabled = true;
}

bool GuiCursorShown() { return g_cursorShown; }

void GuiAppendDraws(std::string& out) {
    for (const auto& [id, element] : g_elements) {
        if (!EffectivelyVisible(element)) continue;
        const Box box = ScreenBox(id);
        const double alpha = EffectiveAlpha(element);
        const uint32_t textColor = element.enabled ? kTextWhite : kTextDisabled;
        if (element.type == "gui-window") {
            // Background, then the title in the top bar (desktop title bar height is about 24 px).
            AppendRect(out, box, kWindowBackground, alpha);
            AppendText(out, box.x + 8, box.y + 4, box.x + box.w - 8, box.y + 28, kTextWhite, alpha, element.text);
        } else if (element.type == "gui-button") {
            AppendRect(out, box, kButtonBackground, alpha);
            AppendText(out, box.x, box.y, box.x + box.w, box.y + box.h, textColor, alpha, element.text);
        } else {
            AppendText(out, box.x, box.y, box.x + box.w, box.y + box.h, textColor, alpha, element.text);
        }
    }
}

double DxFontScale(const char* font) {
    struct FontHeight {
        const char* name;
        double height;  // desktop CGraphics fontInfos height in px
    };
    static const FontHeight kFonts[] = {
        {"default", 15.0}, {"default-bold", 15.0}, {"clear", 15.0}, {"arial", 15.0}, {"sans", 15.0},
        {"pricedown", 30.0}, {"bankgothic", 30.0}, {"diploma", 30.0}, {"beckett", 30.0},
        {"unifont", 14.0},
    };
    if (font) {
        for (const FontHeight& entry : kFonts) {
            if (std::strcmp(font, entry.name) == 0) return entry.height / kDefaultFontHeight;
        }
    }
    return 1.0;
}

} // namespace WorldRPG::Script
