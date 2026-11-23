#include "LuaGuiApi.h"

#include "LuaApiExtra.h"
#include "LuaDxApi.h"
#include "LuaVMStub.h"
#include "LuaAudioApi.h"
#include "LuaStoredElementApi.h"
#include "../gtasa/GameEngine.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace WorldRPG::Script {

namespace {

// Server element ids are 17 bits, so GUI ids start above them and never collide.
constexpr uint32_t kFirstGuiId = 0x40000000u;
lua_State* g_guiVm = nullptr;

// Desktop "default" font (tahoma, D3DX height 15) is the scale-1 reference size.
constexpr double kDefaultFontHeight = 15.0;

// Colors as ARGB, the format the R and T draw lines use.
constexpr uint32_t kWindowBackground = 0xE0101820u;
constexpr uint32_t kButtonBackground = 0xE0304050u;
constexpr uint32_t kFieldBackground = 0xE0181828u;
constexpr uint32_t kCheckBackground = 0xE0E0E0E0u;
constexpr uint32_t kCheckMark = 0xE040C060u;
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
    uint16_t netId = 0;             // which start of that resource, 0 when unknown
    uint32_t generation = 0;
    uint32_t parent = 0;            // 0 = top level, drawn on the screen
    double x = 0, y = 0, w = 0, h = 0;
    bool relPos = false;
    bool relSize = false;
    std::string text;               // window title, label or button text
    bool visible = true;
    bool enabled = true;
    double alpha = 1.0;
    int z = 0;                      // sibling draw order; higher is in front
    bool selected = false;          // checkbox (desktop GetSelected, not the CEGUI invert)
    bool masked = false;            // gui-edit, guiEditSetMasked (CGUIEdit::SetMasked)
    bool dropOpen = false;          // combobox drop list
    bool alwaysOnTop = false;       // CEGUI AlwaysOnTop, default False; guiSetProperty is not ported
    int selectedItem = -1;          // combobox, -1 is none (CGUIComboBox_Impl::GetSelectedItemIndex)
    std::string caption;            // combobox storedCaption, restored when the selection clears
    std::vector<std::string> items; // combobox rows, index 0 is the first item
    int caret = 0;
    // CEGUI String::max_size(), exposed as unsigned int by CGUIEdit_Impl::GetMaxLength.
    uint32_t maxLength = static_cast<uint32_t>(std::numeric_limits<size_t>::max() / sizeof(uint32_t));
    bool readOnly = false;
    double scroll = 0;              // progress bar, scrollbar, and memo/pane vertical, 0-100
    double scrollX = 0;             // scroll pane horizontal, 0-100
    bool horizontal = false;        // scrollbar
    bool barH = true, barV = true;  // scroll pane bars
    bool movable = true, sizable = true;
    int red = 255, green = 255, blue = 255;
    std::string alignH = "left", alignV = "top";
    std::string font = "default-normal";
    uint32_t fontId = 0;            // only an explicit gui-font element is returned by guiGetFont
    bool labelWrap = false;
    uint32_t tab = 0;               // selected tab of a tab panel
    std::map<std::string, std::string> props;
    struct GridColumn { std::string title; double width = 0.3; };
    struct GridCell {
        std::string text;
        bool number = false;
        bool section = false;
        bool selected = false;
        int r = 255, g = 255, b = 255, a = 255;
        int dataRef = LUA_NOREF;
    };
    std::vector<GridColumn> columns;
    std::vector<std::vector<GridCell>> gridRows;
    int gridMode = 0;
    bool gridSort = true;
};

// Ordered by id. Draw order among siblings is z, not id (guiBringToFront).
std::map<uint32_t, GuiElement> g_elements;
uint32_t g_nextId = kFirstGuiId;
uint32_t g_nextZ = 0;
int g_backZ = 0;
uint32_t g_focused = 0;

// Desktop CGUI_Impl starts in INPUTMODE_NO_BINDS_ON_EDIT (CGUI_Impl.cpp:76, 343-387).
// guiGetInputEnabled is then false until an edit has focus, or guiSetInputEnabled(true)
// selects INPUTMODE_NO_BINDS. Bind blocking is not wired: this mode is only what
// guiGetInputEnabled reads. Nothing on the phone drops key binds from it.
enum class InputMode { AllowBinds, NoBinds, NoBindsOnEdit };
InputMode g_inputMode = InputMode::NoBindsOnEdit;
// onClientGUIFocus / onClientGUIBlur. Set by the VM when it can fire Lua events.
GuiFocusHook g_focusHook = nullptr;

// Desktop CResource::ShowCursor (CResource.cpp:1707-1741) keeps one flag per resource
// and a shared count. The phone has a single Lua state, so the key is ActiveVmOwner():
// the resource name already stored on elements as owner. An empty name is the caller
// outside a resource. One showCursor(false) clears only that key.
std::map<std::string, bool> g_cursorHold;
int g_cursorHolds = 0;

GuiElement* Find(uint32_t id) {
    auto found = g_elements.find(id);
    return found == g_elements.end() ? nullptr : &found->second;
}

// CGUI_Impl.cpp:130-139. These names are distinct from the DX font names.
// Android uses its system face for built-ins; resource fonts load the actual file.
bool BuiltinGuiFont(const std::string& name, DxFont& font) {
    struct Entry { const char* name; unsigned points; bool bold; bool autoScale; };
    static constexpr Entry entries[] = {
        {"default-normal", 9, false, false}, {"default-small", 7, false, false},
        {"default-bold-small", 8, true, false}, {"clear-normal", 9, false, false},
        {"sa-header", 26, false, true}, {"sa-gothic", 47, false, true},
        {"sans", 9, false, false}, {"unifont", 9, false, false},
    };
    for (const Entry& entry : entries) {
        if (name != entry.name) continue;
        font.pixels = entry.points * (96.0 / 72.0);
        if (entry.autoScale) {
            int width = 0, height = 0;
            ActiveVmScreenSize(width, height);
            font.pixels *= height / 768.0;
        }
        font.bold = entry.bold;
        font.file = "@gui:" + name;
        return true;
    }
    return false;
}

DxFont ElementFont(const GuiElement& element) {
    DxFont font;
    std::string name;
    const uint32_t id = element.fontId ? element.fontId : DxGuiFontByName(element.font);
    if (id && DxGuiFontInfo(id, font, name)) return font;
    if (!BuiltinGuiFont(element.font, font)) BuiltinGuiFont("default-normal", font);
    return font;
}

void AppendGuiFont(std::string& out, const GuiElement& element) {
    const DxFont font = ElementFont(element);
    char head[80];
    std::snprintf(head, sizeof(head), "F %.3f %d ", font.pixels, font.bold ? 1 : 0);
    out += head;
    out += font.file;
    out += '\n';
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

// Desktop CGUIElement::IsVisible is CEGUI isVisible() with localOnly false: a child
// reports visible only when it and every ancestor are visible.
bool EffectivelyVisible(const GuiElement& element) {
    const GuiElement* current = &element;
    for (int depth = 0; current && depth < 64; ++depth) {
        if (!current->visible) return false;
        current = current->parent ? Find(current->parent) : nullptr;
    }
    return true;
}

// Desktop CGUIElement::IsEnabled is !isDisabled() with localOnly false: a disabled
// ancestor makes the child report disabled. Visibility is a separate walk.
bool EffectivelyEnabled(const GuiElement& element) {
    const GuiElement* current = &element;
    for (int depth = 0; current && depth < 64; ++depth) {
        if (!current->enabled) return false;
        current = current->parent ? Find(current->parent) : nullptr;
    }
    return true;
}

void SetCursorHold(const std::string& owner, bool show) {
    bool& held = g_cursorHold[owner];
    if (held == show) return;
    held = show;
    g_cursorHolds += show ? 1 : -1;
}

void ReleaseCursorHold(const std::string& owner) {
    const auto found = g_cursorHold.find(owner);
    if (found == g_cursorHold.end() || !found->second) return;
    found->second = false;
    --g_cursorHolds;
}

bool AnyCursorHold() { return g_cursorHolds > 0; }

// INPUTMODE_NO_BINDS_ON_EDIT: true only for a visible gui-edit that has focus.
// Read-only edits and memos are not tracked. Bind blocking is not wired.
bool InputEnabledNow() {
    if (g_inputMode == InputMode::AllowBinds) return false;
    if (g_inputMode == InputMode::NoBinds) return true;
    const GuiElement* focused = Find(g_focused);
    return focused && focused->type == "gui-edit" && EffectivelyVisible(*focused);
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

// One '*' per Unicode character. The draw frame keeps only ASCII, so the bullet
// used on the desktop password field cannot travel in an R/T line.
std::string Masked(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        size_t n = 1;
        if ((lead & 0xE0) == 0xC0) n = 2;
        else if ((lead & 0xF0) == 0xE0) n = 3;
        else if ((lead & 0xF8) == 0xF0) n = 4;
        if (i + n > text.size()) n = 1;
        out.push_back('*');
        i += n;
    }
    return out;
}

// Text in one line: the draw format cannot carry a line break.
int TextChars(const std::string& text) {
    int count = 0;
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        size_t step = 1;
        if ((lead & 0xE0) == 0xC0) step = 2;
        else if ((lead & 0xF0) == 0xE0) step = 3;
        else if ((lead & 0xF8) == 0xF0) step = 4;
        if (i + step > text.size()) step = 1;
        i += step;
        ++count;
    }
    return count;
}

size_t ByteAtChar(const std::string& text, size_t index) {
    size_t offset = 0;
    for (size_t count = 0; count < index && offset < text.size(); ++count) {
        ++offset;
        while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) ++offset;
    }
    return offset;
}

int CaretLimit(const GuiElement& element) {
    // CEGUI MultiLineEditbox keeps a final newline and places the caret before it.
    return std::max(0, TextChars(element.text) - (element.type == "gui-memo" ? 1 : 0));
}

void EnsureMemoNewline(GuiElement& element) {
    if (element.type == "gui-memo" && (element.text.empty() || element.text.back() != '\n')) element.text += '\n';
}

bool ParentSegment(const std::string& raw) {
    size_t start = 0;
    while (start <= raw.size()) {
        const size_t slash = raw.find_first_of("/\\", start);
        const size_t count = slash == std::string::npos ? std::string::npos : slash - start;
        if (raw.compare(start, count, "..") == 0) return true;
        if (slash == std::string::npos) return false;
        start = slash + 1;
    }
    return false;
}

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

void AppendLabel(std::string& out, const Box& box, const GuiElement& element, uint32_t color, double alpha) {
    if (element.text.empty()) return;
    char head[192];
    const int alignX = element.alignH == "center" ? 1 : element.alignH == "right" ? 2 : 0;
    const int alignY = element.alignV == "center" ? 1 : element.alignV == "bottom" ? 2 : 0;
    std::snprintf(head, sizeof(head), "Y %.1f %.1f %.1f %.1f %u %d %d %d ", box.x, box.y, box.x + box.w,
                  box.y + box.h, static_cast<unsigned>(WithAlpha(color, alpha)), alignX, alignY, element.labelWrap ? 1 : 0);
    out += head;
    for (char ch : element.text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += static_cast<unsigned char>(ch) < 32 ? ' ' : ch; break;
        }
    }
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

// Puts the element in the map. z is the sibling order (later creates are in front).
uint32_t Emplace(GuiElement element) {
    EnsureMemoNewline(element);
    element.owner = ActiveVmOwner();
    element.netId = ActiveVmOwnerNet();
    element.generation = ActiveVmOwnerGeneration();
    element.z = ++g_nextZ;
    const uint32_t id = g_nextId++;
    g_elements.emplace(id, std::move(element));
    return id;
}

// guiGetPosition(element, relative). The matching setter is below, with the other
// argument-checked bindings.
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

// guiGetSize(element, relative).
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

} // namespace

// Defined below, after Args. InstallGuiBindings runs before that point.
static int GuiCreateWindow(lua_State* L);
static int GuiCreateLabel(lua_State* L);
static int GuiCreateButton(lua_State* L);
static int GuiSetVisible(lua_State* L);
static int GuiGetVisible(lua_State* L);
static int GuiSetText(lua_State* L);
static int GuiGetText(lua_State* L);
static int GuiSetPosition(lua_State* L);
static int GuiSetSize(lua_State* L);
static int GuiSetAlpha(lua_State* L);
static int GuiGetAlpha(lua_State* L);
static int GuiSetEnabled(lua_State* L);
static int GuiGetEnabled(lua_State* L);
static int ShowCursor(lua_State* L);
static int IsCursorShowing(lua_State* L);
static int GuiSetInputEnabled(lua_State* L);
static int GuiGetInputEnabled(lua_State* L);
static int DestroyElement(lua_State* L);

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
    InstallGuiLua(L);
}

bool GuiIsElement(uint32_t id) {
    return Find(id) != nullptr;
}

static void ReleaseGridData(GuiElement& element);

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
    for (uint32_t gone : doomed) {
        if (g_focused == gone) GuiSetFocused(0);
    }
    for (uint32_t gone : doomed) {
        if (GuiElement* element = Find(gone)) ReleaseGridData(*element);
        g_elements.erase(gone);
    }
    return true;
}

uint32_t GuiEnsureResourceRoot(const std::string& owner, uint16_t netId, uint32_t generation) {
    if (owner.empty()) return 0;
    for (const auto& [id, element] : g_elements) {
        if (element.type != "guiroot" || element.owner != owner) continue;
        if (generation != 0 && element.generation != generation) continue;
        if (netId != 0 && element.netId != netId) continue;
        return id;
    }
    // Full-screen relative box so a child uses the screen, the same as no parent.
    // Visible so children stay visible. PickBranch does not treat this box as a hit.
    GuiElement element;
    element.type = "guiroot";
    element.owner = owner;
    element.relPos = true;
    element.relSize = true;
    element.w = 1.0;
    element.h = 1.0;
    const uint32_t id = Emplace(std::move(element));
    if (GuiElement* created = Find(id)) {
        created->owner = owner;
        created->netId = netId;
        created->generation = generation;
    }
    return id;
}

std::string CursorKey(const std::string& owner, uint32_t generation) {
    if (owner.empty() || generation == 0) return owner;
    return owner + "#" + std::to_string(generation);
}

void GuiDropResource(const std::string& owner, uint16_t netId, uint32_t generation) {
    if (owner.empty()) return;
    // CResource's destructor calls ShowCursor(false) (CResource.cpp:220) before it
    // deletes the resource's GUI elements. The hold is per start, so another start
    // of the same name keeps the cursor if it still asked for it.
    ReleaseCursorHold(CursorKey(owner, generation));
    std::vector<uint32_t> mine;
    for (const auto& [id, element] : g_elements) {
        if (element.owner != owner) continue;
        if (netId != 0 && element.netId != 0 && element.netId != netId) continue;
        if (generation != 0 && element.generation != 0 && element.generation != generation) continue;
        mine.push_back(id);
    }
    for (uint32_t id : mine) GuiDestroy(id);
}

static void ReleaseGridData(GuiElement& element) {
    if (!g_guiVm) return;
    for (auto& row : element.gridRows) {
        for (auto& cell : row) {
            if (cell.dataRef == LUA_NOREF) continue;
            luaL_unref(g_guiVm, LUA_REGISTRYINDEX, cell.dataRef);
            cell.dataRef = LUA_NOREF;
        }
    }
}

void GuiReset() {
    g_cursorHold.clear();
    g_cursorHolds = 0;
    for (auto& entry : g_elements) ReleaseGridData(entry.second);
    g_elements.clear();
    // Direct clear: session teardown must not fire focus events.
    g_focused = 0;
    g_inputMode = InputMode::NoBindsOnEdit;
}

void GuiDetachFont(uint32_t id) {
    DxFont font;
    std::string name;
    if (!DxGuiFontInfo(id, font, name)) return;
    for (auto& [elementId, element] : g_elements) {
        if (element.fontId != id && element.font != name) continue;
        element.fontId = 0;
        element.font = "default-normal";
    }
}

bool GuiCursorShown() { return AnyCursorHold(); }

static void DrawElement(std::string& out, uint32_t id, const GuiElement& element) {
    if (element.type == "guiroot") return;
    const Box box = ScreenBox(id);
    const double alpha = EffectiveAlpha(element);
    const uint32_t textColor = element.enabled ? kTextWhite : kTextDisabled;
    const std::string line = OneLine(element.text.c_str(), element.text.size());
    AppendGuiFont(out, element);
    if (element.type == "gui-window") {
        // Background, then the title in the top bar (desktop title bar height is about 24 px).
        AppendRect(out, box, kWindowBackground, alpha);
        AppendText(out, box.x + 8, box.y + 4, box.x + box.w - 8, box.y + 28, kTextWhite, alpha, line);
    } else if (element.type == "gui-button" || element.type == "gui-combobox") {
        AppendRect(out, box, kButtonBackground, alpha);
        AppendText(out, box.x, box.y, box.x + box.w, box.y + box.h, textColor, alpha, line);
        // Open combobox: the drop list under the box (CGUIComboBox_Impl::ShowDropList).
        if (element.type == "gui-combobox" && element.dropOpen) {
            for (size_t i = 0; i < element.items.size(); ++i) {
                Box row = box;
                row.y = box.y + box.h + static_cast<double>(i) * 18.0;
                row.h = 18.0;
                AppendRect(out, row, kButtonBackground, alpha);
                const std::string item = OneLine(element.items[i].c_str(), element.items[i].size());
                AppendText(out, row.x + 4, row.y, row.x + row.w, row.y + row.h, textColor, alpha, item);
            }
        }
    } else if (element.type == "gui-edit") {
        AppendRect(out, box, kFieldBackground, alpha);
        // Cursor indices count Unicode characters, not UTF-8 bytes.
        std::string shown = element.masked ? Masked(line) : line;
        if (g_focused == id) shown.insert(ByteAtChar(shown, element.caret), "|");
        AppendText(out, box.x, box.y, box.x + box.w, box.y + box.h, textColor, alpha, shown);
    } else if (element.type == "gui-tabpanel") {
        AppendRect(out, box, kWindowBackground, alpha);
        double x = box.x;
        for (const auto& [childId, child] : g_elements) {
            if (child.parent != id || child.type != "gui-tab") continue;
            const std::string title = OneLine(child.text.c_str(), child.text.size());
            const double w = 12.0 + static_cast<double>(title.size()) * 8.0;
            AppendRect(out, Box{x, box.y, w, 18.0}, childId == element.tab ? kButtonBackground : kFieldBackground, alpha);
            AppendGuiFont(out, child);
            AppendText(out, x + 4, box.y, x + w, box.y + 18.0, textColor, alpha, title);
            x += w;
        }
    } else if (element.type == "gui-memo") {
        AppendRect(out, box, kFieldBackground, alpha);
        std::string shown = element.text;
        if (g_focused == id) shown.insert(ByteAtChar(shown, element.caret), "|");
        // Draw each logical line without embedding control characters in the DX protocol.
        // Wrapping and automatic caret scrolling remain separate renderer work.
        size_t start = 0;
        double y = box.y;
        const double lineHeight = DxMeasureFontHeight(ElementFont(element));
        while (start < shown.size() && y + lineHeight <= box.y + box.h) {
            const size_t end = shown.find('\n', start);
            const size_t size = end == std::string::npos ? shown.size() - start : end - start;
            AppendText(out, box.x, y, box.x + box.w, y + lineHeight, textColor, alpha, OneLine(shown.data() + start, size));
            if (end == std::string::npos) break;
            start = end + 1;
            y += lineHeight;
        }
    } else if (element.type == "gui-progressbar") {
        AppendRect(out, box, kFieldBackground, alpha);
        const double fill = box.w * std::clamp(element.scroll, 0.0, 100.0) / 100.0;
        if (fill > 0.0) AppendRect(out, Box{box.x, box.y, fill, box.h}, kCheckMark, alpha);
    } else if (element.type == "gui-scrollbar") {
        AppendRect(out, box, kFieldBackground, alpha);
        const double t = std::clamp(element.scroll, 0.0, 100.0) / 100.0;
        if (element.horizontal) {
            const double w = std::max(8.0, box.w * 0.2);
            AppendRect(out, Box{box.x + (box.w - w) * t, box.y, w, box.h}, kButtonBackground, alpha);
        } else {
            const double h = std::max(8.0, box.h * 0.2);
            AppendRect(out, Box{box.x, box.y + (box.h - h) * t, box.w, h}, kButtonBackground, alpha);
        }
    } else if (element.type == "gui-radiobutton") {
        Box mark{box.x, box.y, std::min(16.0, std::max(0.0, box.w)), std::min(16.0, std::max(0.0, box.h))};
        AppendRect(out, mark, kCheckBackground, alpha);
        if (element.selected && mark.w > 6.0 && mark.h > 6.0)
            AppendRect(out, Box{mark.x + 4, mark.y + 4, mark.w - 8, mark.h - 8}, kCheckMark, alpha);
        AppendText(out, box.x + 20, box.y, box.x + box.w, box.y + box.h, textColor, alpha, line);
    } else if (element.type == "gui-checkbox") {
        Box mark{box.x, box.y, std::min(16.0, std::max(0.0, box.w)), std::min(16.0, std::max(0.0, box.h))};
        AppendRect(out, mark, kCheckBackground, alpha);
        if (element.selected && mark.w > 6.0 && mark.h > 6.0) {
            AppendRect(out, Box{mark.x + 3, mark.y + 3, mark.w - 6, mark.h - 6}, kCheckMark, alpha);
        }
        AppendText(out, box.x + 20, box.y, box.x + box.w, box.y + box.h, textColor, alpha, line);
    } else if (element.type == "gui-gridlist") {
        AppendRect(out, box, kFieldBackground, alpha);
        double x = box.x;
        const double header = 16.0;
        for (const GuiElement::GridColumn& column : element.columns) {
            const double w = std::max(8.0, box.w * column.width);
            AppendRect(out, Box{x, box.y, w, header}, kButtonBackground, alpha);
            AppendText(out, x + 2, box.y, x + w, box.y + header, textColor, alpha, OneLine(column.title.c_str(), column.title.size()));
            x += w;
        }
        for (size_t row = 0; row < element.gridRows.size() && row < 8; ++row) {
            x = box.x;
            const double y = box.y + header + static_cast<double>(row) * 16.0;
            for (size_t col = 0; col < element.columns.size() && col < element.gridRows[row].size(); ++col) {
                const double w = std::max(8.0, box.w * element.columns[col].width);
                const auto& cell = element.gridRows[row][col];
                if (cell.selected) AppendRect(out, Box{x, y, w, 16.0}, kCheckMark, alpha * 0.45);
                const uint32_t cellColor = 0xFF000000u | (static_cast<uint32_t>(cell.r) << 16) |
                    (static_cast<uint32_t>(cell.g) << 8) | static_cast<uint32_t>(cell.b);
                AppendText(out, x + 2, y, x + w, y + 16.0, cellColor, alpha * (cell.a / 255.0),
                    OneLine(cell.text.c_str(), cell.text.size()));
                x += w;
            }
        }
    } else if (element.type == "gui-label") {
        const uint32_t color = element.enabled
            ? (0xFF000000u | (static_cast<uint32_t>(element.red) << 16) | (static_cast<uint32_t>(element.green) << 8) | static_cast<uint32_t>(element.blue))
            : kTextDisabled;
        AppendLabel(out, box, element, color, alpha);
    } else {
        AppendText(out, box.x, box.y, box.x + box.w, box.y + box.h, textColor, alpha, line);
    }
    out += "F\n";
}

// Child ids in creation order (the map is ordered by id).
static std::vector<uint32_t> ChildrenOf(uint32_t parent) {
    std::vector<uint32_t> kids;
    for (const auto& [id, element] : g_elements) {
        if (element.parent == parent) kids.push_back(id);
    }
    return kids;
}

// Desktop RUN_CHILDREN, then the element itself (CStaticFunctionDefinitions.cpp:66-73).
template <typename Fn>
static void PostOrder(uint32_t id, Fn& fn) {
    for (uint32_t child : ChildrenOf(id)) PostOrder(child, fn);
    if (GuiElement* element = Find(id)) fn(*element);
}

static void DrawBranch(std::string& out, uint32_t parent) {
    std::vector<uint32_t> kids = ChildrenOf(parent);
    std::sort(kids.begin(), kids.end(), [](uint32_t a, uint32_t b) {
        const GuiElement* left = Find(a);
        const GuiElement* right = Find(b);
        if (left->z != right->z) return left->z < right->z;
        return a < b;
    });
    for (uint32_t id : kids) {
        const GuiElement* element = Find(id);
        if (!element || !EffectivelyVisible(*element)) continue;
        if (element->type != "gui-tab") DrawElement(out, id, *element);
        if (element->type == "gui-tab") {
            const GuiElement* panel = element->parent ? Find(element->parent) : nullptr;
            if (!panel || panel->type != "gui-tabpanel" || panel->tab != id) continue;
        }
        DrawBranch(out, id);
    }
}

void GuiAppendDraws(std::string& out) {
    const size_t start = out.size();
    DrawBranch(out, 0);
    // CEGUI renders independently of the last DX blend mode. Do not let add or
    // overwrite from a script change the GUI; post-GUI draws restore their own mode.
    if (out.size() != start) out.insert(start, "B 0\n");
}

namespace {

bool BoxContains(const Box& box, double x, double y) {
    return box.w > 0.0 && box.h > 0.0 && x >= box.x && y >= box.y && x < box.x + box.w && y < box.y + box.h;
}

// Same order DrawBranch paints in, so the last drawn element under the point wins.
uint32_t PickBranch(uint32_t parent, double x, double y) {
    uint32_t hit = 0;
    std::vector<uint32_t> kids = ChildrenOf(parent);
    std::sort(kids.begin(), kids.end(), [](uint32_t a, uint32_t b) {
        const GuiElement* left = Find(a);
        const GuiElement* right = Find(b);
        if (left->z != right->z) return left->z < right->z;
        return a < b;
    });
    for (uint32_t id : kids) {
        const GuiElement* element = Find(id);
        if (!element || !EffectivelyVisible(*element) || !EffectivelyEnabled(*element)) continue;
        // The resource guiroot covers the screen so children can be placed in it.
        // It is not itself a widget (CClientDummy "guiroot").
        if (element->type != "guiroot" && BoxContains(ScreenBox(id), x, y)) hit = id;
        if (element->type == "gui-tab") {
            const GuiElement* panel = element->parent ? Find(element->parent) : nullptr;
            if (panel && panel->type == "gui-tabpanel" && panel->tab != id) continue;
        }
        if (const uint32_t child = PickBranch(id, x, y)) hit = child;
    }
    return hit;
}

bool EditType(uint32_t id) {
    const GuiElement* element = Find(id);
    return element && (element->type == "gui-edit" || element->type == "gui-memo");
}

} // namespace

uint32_t GuiPick(double x, double y) { return PickBranch(0, x, y); }

bool GuiIsEdit(uint32_t id) {
    const GuiElement* element = Find(id);
    return EditType(id) && EffectivelyVisible(*element) && EffectivelyEnabled(*element);
}

bool GuiFocusedMemo() {
    const GuiElement* element = Find(GuiFocusedEdit());
    return element && element->type == "gui-memo";
}

uint32_t GuiFocusedEdit() {
    return GuiIsEdit(g_focused) ? g_focused : 0;
}

void GuiSetFocusHook(GuiFocusHook hook) { g_focusHook = hook; }

void GuiSetFocused(uint32_t id) {
    if (id && !Find(id)) id = 0;
    if (g_focused == id) return;
    const uint32_t previous = g_focused;
    g_focused = id;
    if (!g_focusHook) return;
    if (EditType(previous)) g_focusHook(previous, false);
    if (EditType(id)) g_focusHook(id, true);
}

bool GuiEditAppend(const char* bytes, size_t length) {
    GuiElement* element = Find(GuiFocusedEdit());
    if (!element || !bytes || length == 0 || element->readOnly) return false;
    const int added = TextChars(std::string(bytes, length));
    if (static_cast<uint64_t>(CaretLimit(*element)) + added > element->maxLength)
        return false;
    if (element->text.size() + length > 4096) return false;
    element->caret = std::clamp(element->caret, 0, CaretLimit(*element));
    element->text.insert(ByteAtChar(element->text, element->caret), bytes, length);
    element->caret += added;
    return true;
}

bool GuiEditErase() {
    GuiElement* element = Find(GuiFocusedEdit());
    if (!element || element->readOnly || element->text.empty()) return false;
    element->caret = std::clamp(element->caret, 0, CaretLimit(*element));
    if (element->caret == 0) return false;
    const size_t end = ByteAtChar(element->text, element->caret);
    const size_t start = ByteAtChar(element->text, element->caret - 1);
    element->text.erase(start, end - start);
    --element->caret;
    return true;
}

bool GuiKeyCapture() { return g_inputMode == InputMode::NoBinds; }

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

// Desktop CScriptArgReader checks used by the functions below: numbers (and numeric
// strings), real booleans, strings, and a GUI element table. A bad argument returns
// false or nil, the same failure value as CLuaGUIDefs / CLuaDrawingDefs, and does not
// raise a Lua error.
struct Args {
    lua_State* L = nullptr;
    int index = 1;
    bool bad = false;
    const char* why = "bad argument";

    void fail(const char* reason) {
        if (!bad) {
            bad = true;
            why = reason;
        }
    }

    bool num(double& out) {
        if (bad) return false;
        if (!lua_isnumber(L, index)) {
            fail("number");
            return false;
        }
        out = lua_tonumber(L, index++);
        if (std::isnan(out)) {
            fail("NaN");
            return false;
        }
        return true;
    }

    bool vec2(double& x, double& y) {
        if (bad) return false;
        if (!lua_isnumber(L, index)) {
            fail("vector2");
            return false;
        }
        return num(x) && num(y);
    }

    bool integer(int& out) {
        double value = 0;
        if (!num(value)) return false;
        const double highest = static_cast<double>(std::numeric_limits<int>::max()) + 1.0;
        if (value >= highest || value < static_cast<double>(std::numeric_limits<int>::min())) {
            fail("number");
            return false;
        }
        out = static_cast<int>(value);
        return true;
    }

    // Unsigned color byte. Negative values are rejected (ReadNumber checkSign).
    // A missing optional byte uses fallback (alpha defaults to 255).
    bool byte(uint8_t& out, bool optional, uint8_t fallback) {
        if (bad) return false;
        const int kind = lua_type(L, index);
        if (optional && (kind == LUA_TNONE || kind == LUA_TNIL)) {
            out = fallback;
            ++index;
            return true;
        }
        double value = 0;
        if (!num(value)) return false;
        if (value < -static_cast<double>(std::numeric_limits<float>::epsilon())) {
            fail("positive");
            return false;
        }
        if (value >= 1.0e15) {
            fail("number");
            return false;
        }
        out = static_cast<uint8_t>(static_cast<int64_t>(value));
        return true;
    }

    bool flag(bool& out, bool optional, bool fallback) {
        if (bad) return false;
        const int kind = lua_type(L, index);
        if (kind == LUA_TBOOLEAN) {
            out = lua_toboolean(L, index++) != 0;
            return true;
        }
        if (optional && (kind == LUA_TNONE || kind == LUA_TNIL)) {
            out = fallback;
            ++index;
            return true;
        }
        fail("bool");
        return false;
    }

    bool text(std::string& out) {
        if (bad) return false;
        const int kind = lua_type(L, index);
        if (kind != LUA_TSTRING && kind != LUA_TNUMBER) {
            fail("string");
            return false;
        }
        size_t length = 0;
        const char* value = lua_tolstring(L, index++, &length);
        out.assign(value ? value : "", length);
        return true;
    }

    // ReadCharStringRef: a real string, not a number.
    bool bytes(const char*& data, size_t& length) {
        if (bad) return false;
        if (lua_type(L, index) != LUA_TSTRING) {
            fail("string");
            return false;
        }
        data = lua_tolstring(L, index++, &length);
        if (!data) {
            data = "";
            length = 0;
        }
        return true;
    }

    // typeName null accepts any GUI element. Desktop ReadUserData of a widget class
    // rejects every other element type.
    bool gui(uint32_t& id, const char* typeName) {
        if (bad) return false;
        if (lua_type(L, index) != LUA_TTABLE) {
            fail("element");
            return false;
        }
        id = ElementIdOf(L, index);
        const GuiElement* element = Find(id);
        if (!element || (typeName && element->type != typeName)) {
            fail(typeName ? typeName : "element");
            ++index;
            return false;
        }
        ++index;
        return true;
    }

    // Optional parent. nil, none, and false mean no parent (ReadUserData NULL).
    bool parent(uint32_t& id) {
        if (bad) return false;
        const int kind = lua_type(L, index);
        if (kind == LUA_TNONE || kind == LUA_TNIL) {
            id = 0;
            ++index;
            return true;
        }
        if (kind == LUA_TBOOLEAN && lua_toboolean(L, index) == 0) {
            id = 0;
            ++index;
            return true;
        }
        if (kind != LUA_TTABLE) {
            fail("element");
            return false;
        }
        const uint32_t found = ElementIdOf(L, index);
        if (!Find(found)) {
            fail("element");
            ++index;
            return false;
        }
        id = found;
        ++index;
        return true;
    }
};

static int Reject(lua_State* L, const char* name, const Args& args, bool nilOnError) {
    LUA_LOGE("%s: bad argument (%s)", name, args.why);
    if (nilOnError) lua_pushnil(L);
    else lua_pushboolean(L, 0);
    return 1;
}

// CPixelsManager.cpp:727-748. Plain tail is two little-endian uint16s (width, height).
// The byte size must be width * height * 4 + 4. Multiply wraps as a uint32, same as desktop.
static bool PlainSize(const char* data, size_t size, uint32_t& width, uint32_t& height) {
    if (!data || size < 8 || size > 0xFFFFFFFFu) return false;
    uint16_t tail[2] = {};
    std::memcpy(tail, data + size - 4, 4);
    width = tail[0];
    height = tail[1];
    const uint32_t expect = width * height * 4u + 4u;
    return static_cast<uint32_t>(size) == expect;
}

// CFileFormatPng.cpp:21-34. Header and IEND tail only; no libpng.
static bool IsPng(const void* data, size_t size) {
    static const unsigned char header[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    static const unsigned char tail[] = {0xAE, 0x42, 0x60, 0x82};
    if (!data || size < sizeof(header)) return false;
    const auto* bytes = static_cast<const unsigned char*>(data);
    return std::memcmp(bytes, header, sizeof(header)) == 0 &&
           std::memcmp(bytes + size - sizeof(tail), tail, sizeof(tail)) == 0;
}

// CFileFormatJpeg.cpp:30-56. Header, tail, and the second marker. No libjpeg.
static bool IsJpeg(const void* data, size_t size) {
    if (!data || size == 0) return false;
    static const unsigned char header[] = {0xFF, 0xD8, 0xFF};
    static const unsigned char tail[] = {0xFF, 0xD9};
    if (size < 6) return false;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (std::memcmp(bytes, header, 3) != 0 || std::memcmp(bytes + size - 2, tail, 2) != 0) return false;
    const unsigned seg1 = static_cast<unsigned>(bytes[4]) * 256u + bytes[5];
    if (static_cast<size_t>(seg1) + 5 >= size) return false;
    return bytes[4 + seg1] == 0xFF;
}

static bool IsDds(const void* data, size_t size) {
    static const unsigned char header[] = {0x44, 0x44, 0x53, 0x20};
    if (!data || size < sizeof(header)) return false;
    return std::memcmp(data, header, sizeof(header)) == 0;
}

// CPixelsManager.cpp:656-682. PNG, then JPEG, then DDS, then plain.
static const char* PixelsFormatName(const char* data, size_t size) {
    if (IsPng(data, size)) return "png";
    if (IsJpeg(data, size)) return "jpeg";
    if (IsDds(data, size)) return "dds";
    uint32_t width = 0, height = 0;
    if (size >= 8 && PlainSize(data, size, width, height)) return "plain";
    return nullptr;
}

const char* DxPixelsFormatName(const char* data, size_t size) { return PixelsFormatName(data, size); }
bool DxPlainSize(const char* data, size_t size, uint32_t& width, uint32_t& height) {
    return PlainSize(data, size, width, height);
}

// (int) position, then the uint parameter of GetPixelColor / SetPixelColor.
static bool PixelIndex(double value, uint32_t& out) {
    const double highest = static_cast<double>(std::numeric_limits<int>::max()) + 1.0;
    if (value >= highest || value < static_cast<double>(std::numeric_limits<int>::min())) return false;
    out = static_cast<uint32_t>(static_cast<int>(value));
    return true;
}

struct Bgra {
    uint8_t b, g, r, a;
};

static bool PixelOffset(const char* data, size_t size, uint32_t x, uint32_t y, size_t& offset) {
    uint32_t width = 0, height = 0;
    if (!PlainSize(data, size, width, height)) return false;
    if (static_cast<uint64_t>(x) >= width || static_cast<uint64_t>(y) >= height) return false;
    const uint64_t index = static_cast<uint64_t>(x) + static_cast<uint64_t>(y) * width;
    if ((index + 1ull) * 4ull + 4ull > size) return false;
    offset = static_cast<size_t>(index);
    return true;
}

// CLuaDrawingDefs.cpp:1983-2010, CPixelsManager.cpp:836-851.
// Only plain pixels. Returns r, g, b, a. Failure is false.
static int DxGetPixelColor(lua_State* L) {
    Args args{L};
    const char* data = nullptr;
    size_t length = 0;
    double px = 0, py = 0;
    if (!args.bytes(data, length) || !args.vec2(px, py)) return Reject(L, "dxGetPixelColor", args, false);
    uint32_t x = 0, y = 0;
    size_t offset = 0;
    if (!PixelIndex(px, x) || !PixelIndex(py, y) || !PixelOffset(data, length, x, y, offset)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto* pixel = reinterpret_cast<const Bgra*>(data) + offset;
    lua_pushnumber(L, pixel->r);
    lua_pushnumber(L, pixel->g);
    lua_pushnumber(L, pixel->b);
    lua_pushnumber(L, pixel->a);
    return 4;
}

// CLuaDrawingDefs.cpp:1929-1951, CPixelsManager.cpp:656-682.
static int DxGetPixelsFormat(lua_State* L) {
    Args args{L};
    const char* data = nullptr;
    size_t length = 0;
    if (!args.bytes(data, length)) return Reject(L, "dxGetPixelsFormat", args, false);
    const char* name = PixelsFormatName(data, length);
    if (!name) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, name);
    return 1;
}

// CLuaDrawingDefs.cpp:2013-2041, CPixelsManager.cpp:860-874.
// Writes B,G,R,A into the Lua string, which is what ReadCharStringRef points at.
static int DxSetPixelColor(lua_State* L) {
    Args args{L};
    const char* data = nullptr;
    size_t length = 0;
    double px = 0, py = 0;
    uint8_t red = 0, green = 0, blue = 0, alpha = 255;
    if (!args.bytes(data, length) || !args.vec2(px, py) || !args.byte(red, false, 0) || !args.byte(green, false, 0) ||
        !args.byte(blue, false, 0) || !args.byte(alpha, true, 255)) {
        return Reject(L, "dxSetPixelColor", args, false);
    }
    uint32_t x = 0, y = 0;
    size_t offset = 0;
    if (!PixelIndex(px, x) || !PixelIndex(py, y) || !PixelOffset(data, length, x, y, offset)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    auto* pixel = reinterpret_cast<Bgra*>(const_cast<char*>(data)) + offset;
    pixel->b = blue;
    pixel->g = green;
    pixel->r = red;
    pixel->a = alpha;
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:1913-1930, CStaticFunctionDefinitions.cpp:6153-6162.
// Deactivate. Returns true for any GUI element. Clears the focus when this element is the
// active one, which fires onClientGUIBlur for a gui-edit.
static int GuiBlur(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "guiBlur", args, false);
    if (g_focused == id) GuiSetFocused(0);
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:1933-1948, CStaticFunctionDefinitions.cpp:6165-6175.
// Activate. A gui-edit that gains focus is what the phone's soft keyboard types into.
static int GuiFocus(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "guiFocus", args, false);
    GuiSetFocused(id);
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:614-633. "allow_binds", "no_binds", "no_binds_when_editing".
// "no_binds" is INPUTMODE_NO_BINDS: the phone opens the soft keyboard for DX edits.
static int GuiSetInputMode(lua_State* L) {
    Args args{L};
    std::string mode;
    if (!args.text(mode)) return Reject(L, "guiSetInputMode", args, false);
    if (mode == "allow_binds") g_inputMode = InputMode::AllowBinds;
    else if (mode == "no_binds") g_inputMode = InputMode::NoBinds;
    else if (mode == "no_binds_when_editing") g_inputMode = InputMode::NoBindsOnEdit;
    else {
        args.fail("input-mode");
        return Reject(L, "guiSetInputMode", args, false);
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:635-639.
static int GuiGetInputMode(lua_State* L) {
    const char* mode = "no_binds_when_editing";
    if (g_inputMode == InputMode::AllowBinds) mode = "allow_binds";
    else if (g_inputMode == InputMode::NoBinds) mode = "no_binds";
    lua_pushstring(L, mode);
    return 1;
}

// CLuaGUIDefs.cpp:3163-3184, CStaticFunctionDefinitions.cpp:6340-6356.
// A non-edit fails the argument check (ReadUserData of CGUIEdit).
static int GuiEditSetMasked(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool masked = false;
    if (!args.gui(id, "gui-edit") || !args.flag(masked, false, false))
        return Reject(L, "guiEditSetMasked", args, false);
    if (GuiElement* element = Find(id)) element->masked = masked;
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:3187-3206. A bad element is nil, not false.
static int GuiEditIsMasked(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-edit")) return Reject(L, "guiEditIsMasked", args, true);
    const GuiElement* element = Find(id);
    lua_pushboolean(L, element && element->masked ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:1386-1407, CStaticFunctionDefinitions.cpp:6111-6132.
// Moves the element in front of its siblings. AlwaysOnTop stays false (guiSetProperty is
// not ported). Console input is not restored: there is no console, and isConsoleActive stays false.
static int GuiBringToFront(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "guiBringToFront", args, false);
    GuiElement* element = Find(id);
    if (!element || element->alwaysOnTop) {
        lua_pushboolean(L, 0);
        return 1;
    }
    element->z = ++g_nextZ;
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:1502-1521. The stored flag is the script value. CGUICheckBox_Impl inverts
// it only for the CEGUI widget, and GetSelected inverts it back.
static int GuiCheckBoxGetSelected(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-checkbox")) return Reject(L, "guiCheckBoxGetSelected", args, false);
    const GuiElement* element = Find(id);
    lua_pushboolean(L, element && element->selected ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:1456-1477, CStaticFunctionDefinitions.cpp:6177-6192.
// Also sets descendant checkboxes (RUN_CHILDREN, propagation on by default).
static int GuiCheckBoxSetSelected(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool state = false;
    if (!args.gui(id, "gui-checkbox") || !args.flag(state, false, false))
        return Reject(L, "guiCheckBoxSetSelected", args, false);
    auto visit = [&](GuiElement& element) {
        if (element.type == "gui-checkbox") element.selected = state;
    };
    PostOrder(id, visit);
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:3785-3806, CStaticFunctionDefinitions.cpp:5768-5787,
// CGUIComboBox_Impl.cpp:63-69 and 123-148. The new row is appended. The result is its
// 0-based index (the "+ 1" comment in the static function is not what GetItemIndex returns).
static int GuiComboBoxAddItem(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    std::string value;
    if (!args.gui(id, "gui-combobox") || !args.text(value)) return Reject(L, "guiComboBoxAddItem", args, false);
    int added = 0;
    auto visit = [&](GuiElement& element) {
        if (element.type != "gui-combobox") return;
        element.items.push_back(value);
        added = static_cast<int>(element.items.size()) - 1;
    };
    PostOrder(id, visit);
    lua_pushnumber(L, added);
    return 1;
}

// CLuaGUIDefs.cpp:3833-3851, CStaticFunctionDefinitions.cpp:5810-5828,
// CGUIComboBox_Impl.cpp:230-242. Clears the rows and restores the caption.
static int GuiComboBoxClear(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-combobox")) return Reject(L, "guiComboBoxClear", args, false);
    bool ok = false;
    auto visit = [&](GuiElement& element) {
        if (element.type != "gui-combobox") return;
        element.items.clear();
        element.selectedItem = -1;
        element.text = element.caption;
        ok = true;
    };
    PostOrder(id, visit);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:3946-3964, CStaticFunctionDefinitions.cpp:5911-5926.
// No RUN_CHILDREN. The count is this combobox only.
static int GuiComboBoxGetItemCount(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-combobox")) return Reject(L, "guiComboBoxGetItemCount", args, false);
    const GuiElement* element = Find(id);
    lua_pushnumber(L, element ? static_cast<lua_Number>(element->items.size()) : 0);
    return 1;
}

// CLuaGUIDefs.cpp:3898-3918, CStaticFunctionDefinitions.cpp:5871-5888,
// CGUIComboBox_Impl.cpp:151-172. Index -1 is the text in the box. A bad index is "".
static int GuiComboBoxGetItemText(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int index = 0;
    if (!args.gui(id, "gui-combobox") || !args.integer(index)) return Reject(L, "guiComboBoxGetItemText", args, false);
    const GuiElement* element = Find(id);
    std::string value;
    if (element) {
        if (index == -1) value = element->text;
        else if (index >= 0 && static_cast<size_t>(index) < element->items.size())
            value = element->items[static_cast<size_t>(index)];
    }
    lua_pushlstring(L, value.data(), value.size());
    return 1;
}

// CLuaGUIDefs.cpp:3854-3872, CStaticFunctionDefinitions.cpp:5831-5848,
// CGUIComboBox_Impl.cpp:105-120. -1 when nothing is selected. A bad argument is nil.
static int GuiComboBoxGetSelected(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-combobox")) return Reject(L, "guiComboBoxGetSelected", args, true);
    const GuiElement* element = Find(id);
    lua_pushnumber(L, element ? element->selectedItem : -1);
    return 1;
}

// CLuaGUIDefs.cpp:3989-4006, CStaticFunctionDefinitions.cpp:5956-5970.
// A bad argument is nil, not false.
static int GuiComboBoxIsOpen(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-combobox")) return Reject(L, "guiComboBoxIsOpen", args, true);
    const GuiElement* element = Find(id);
    lua_pushboolean(L, element && element->dropOpen ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:3809-3830, CStaticFunctionDefinitions.cpp:5790-5807,
// CGUIComboBox_Impl.cpp:81-97. Out of range is false. Removing the selected row
// restores the caption. Later rows keep their identity, so the selected index shifts.
static int GuiComboBoxRemoveItem(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int index = 0;
    if (!args.gui(id, "gui-combobox") || !args.integer(index)) return Reject(L, "guiComboBoxRemoveItem", args, false);
    bool removed = false;
    auto visit = [&](GuiElement& element) {
        if (element.type != "gui-combobox") return;
        removed = false;
        if (index < 0 || static_cast<size_t>(index) >= element.items.size()) return;
        const bool wasSelected = element.selectedItem == index;
        element.items.erase(element.items.begin() + static_cast<size_t>(index));
        if (wasSelected) {
            element.selectedItem = -1;
            element.text = element.caption;
        } else if (element.selectedItem > index) {
            --element.selectedItem;
        }
        removed = true;
    };
    PostOrder(id, visit);
    lua_pushboolean(L, removed ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:3921-3943, CStaticFunctionDefinitions.cpp:5891-5908,
// CGUIComboBox_Impl.cpp:175-191. A selected row also updates the text in the box.
static int GuiComboBoxSetItemText(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int index = 0;
    std::string value;
    if (!args.gui(id, "gui-combobox") || !args.integer(index) || !args.text(value))
        return Reject(L, "guiComboBoxSetItemText", args, false);
    bool ok = false;
    auto visit = [&](GuiElement& element) {
        if (element.type != "gui-combobox") return;
        if (index < 0 || static_cast<size_t>(index) >= element.items.size()) {
            ok = false;
            return;
        }
        element.items[static_cast<size_t>(index)] = value;
        if (element.selectedItem == index) element.text = value;
        ok = true;
    };
    PostOrder(id, visit);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:3967-3986, CStaticFunctionDefinitions.cpp:5929-5953.
static int GuiComboBoxSetOpen(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool open = false;
    if (!args.gui(id, "gui-combobox") || !args.flag(open, false, false))
        return Reject(L, "guiComboBoxSetOpen", args, false);
    bool ok = false;
    auto visit = [&](GuiElement& element) {
        if (element.type != "gui-combobox") return;
        element.dropOpen = open;
        ok = true;
    };
    PostOrder(id, visit);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:3875-3895, CStaticFunctionDefinitions.cpp:5851-5868,
// CGUIComboBox_Impl.cpp:201-227. -1 clears the selection and restores the caption.
static int GuiComboBoxSetSelected(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int index = 0;
    if (!args.gui(id, "gui-combobox") || !args.integer(index)) return Reject(L, "guiComboBoxSetSelected", args, false);
    bool ok = false;
    auto visit = [&](GuiElement& element) {
        if (element.type != "gui-combobox") return;
        if (index == -1) {
            element.selectedItem = -1;
            element.text = element.caption;
            ok = true;
            return;
        }
        if (index < 0 || static_cast<size_t>(index) >= element.items.size()) {
            ok = false;
            return;
        }
        element.selectedItem = index;
        element.text = element.items[static_cast<size_t>(index)];
        ok = true;
    };
    PostOrder(id, visit);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:890-924, CStaticFunctionDefinitions.cpp:5561-5576.
// onClientGUIStateChanged is not fired: there is no widget input hook.
static int GuiCreateCheckBox(lua_State* L) {
    Args args{L};
    GuiElement element;
    bool selected = false;
    bool relative = false;
    uint32_t parent = 0;
    if (!args.vec2(element.x, element.y) || !args.vec2(element.w, element.h) || !args.text(element.text) ||
        !args.flag(selected, false, false) || !args.flag(relative, true, false) || !args.parent(parent)) {
        return Reject(L, "guiCreateCheckBox", args, false);
    }
    element.type = "gui-checkbox";
    element.selected = selected;
    element.relPos = relative;
    element.relSize = relative;
    element.parent = parent;
    PushElement(L, Emplace(std::move(element)));
    return 1;
}

// CLuaGUIDefs.cpp:3751-3782, CStaticFunctionDefinitions.cpp:5746-5765.
// The caption is kept separately from the text shown in the box. Read-only, as on desktop.
// onClientGUIComboBoxAccepted is not fired: there is no widget input hook.
static int GuiCreateComboBox(lua_State* L) {
    Args args{L};
    GuiElement element;
    bool relative = false;
    uint32_t parent = 0;
    if (!args.vec2(element.x, element.y) || !args.vec2(element.w, element.h) || !args.text(element.text) ||
        !args.flag(relative, true, false) || !args.parent(parent)) {
        return Reject(L, "guiCreateComboBox", args, false);
    }
    element.type = "gui-combobox";
    element.caption = element.text;
    element.relPos = relative;
    element.relSize = relative;
    element.parent = parent;
    element.selectedItem = -1;
    PushElement(L, Emplace(std::move(element)));
    return 1;
}

// CLuaGUIDefs.cpp:962-994, CStaticFunctionDefinitions.cpp:5597-5614.
// A tap focuses the edit (GuiPick) and the soft keyboard writes the text, which fires
// onClientGUIChanged. Enter fires onClientGUIAccepted. Typing respects the caret index.
static int GuiCreateEdit(lua_State* L) {
    Args args{L};
    GuiElement element;
    bool relative = false;
    uint32_t parent = 0;
    if (!args.vec2(element.x, element.y) || !args.vec2(element.w, element.h) || !args.text(element.text) ||
        !args.flag(relative, true, false) || !args.parent(parent)) {
        return Reject(L, "guiCreateEdit", args, false);
    }
    element.type = "gui-edit";
    element.relPos = relative;
    element.relSize = relative;
    element.parent = parent;
    PushElement(L, Emplace(std::move(element)));
    return 1;
}

// CLuaGUIDefs.cpp GUICreateWindow 702-732, GUICreateLabel 735-767, GUICreateButton 822-854.
// A bad type or a short argument list returns false and creates nothing. relative is a
// real boolean (omitted or nil is false). A parent that is not a GUI element fails.
// The text is stored with its line breaks. The draw line flattens them (OneLine).
static int CreateSized(lua_State* L, const char* name, const char* type, bool hasParent) {
    Args args{L};
    GuiElement element;
    bool relative = false;
    uint32_t parent = 0;
    if (!args.vec2(element.x, element.y) || !args.vec2(element.w, element.h) || !args.text(element.text) ||
        !args.flag(relative, true, false) || (hasParent && !args.parent(parent))) {
        return Reject(L, name, args, false);
    }
    element.type = type;
    element.relPos = relative;
    element.relSize = relative;
    element.parent = parent;
    PushElement(L, Emplace(std::move(element)));
    return 1;
}

static int GuiCreateWindow(lua_State* L) { return CreateSized(L, "guiCreateWindow", "gui-window", false); }
static int GuiCreateLabel(lua_State* L) { return CreateSized(L, "guiCreateLabel", "gui-label", true); }
static int GuiCreateButton(lua_State* L) { return CreateSized(L, "guiCreateButton", "gui-button", true); }

// CLuaGUIDefs.cpp:1867-1887. A bad element or a non-boolean leaves the widget unchanged.
// guiSetVisible(element, 0) does not store visible: 0 is not a boolean.
static int GuiSetVisible(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool state = false;
    if (!args.gui(id, nullptr) || !args.flag(state, false, false)) return Reject(L, "guiSetVisible", args, false);
    if (GuiElement* element = Find(id)) element->visible = state;
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:1978-1996. IsVisible walks ancestors (CEGUI localOnly defaults false).
static int GuiGetVisible(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "guiGetVisible", args, false);
    const GuiElement* element = Find(id);
    lua_pushboolean(L, element && EffectivelyVisible(*element) ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:1335-1356. Stored text keeps line breaks.
static int GuiSetText(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    std::string text;
    if (!args.gui(id, nullptr) || !args.text(text)) return Reject(L, "guiSetText", args, false);
    if (GuiElement* element = Find(id)) {
        if (TextChars(text) > element->maxLength) text.resize(ByteAtChar(text, element->maxLength));
        element->text = std::move(text);
        EnsureMemoNewline(*element);
        element->caret = std::min(element->caret, CaretLimit(*element));
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:1708-1727. A bad element returns false, not an empty string.
static int GuiGetText(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "guiGetText", args, false);
    const GuiElement* element = Find(id);
    if (!element) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushlstring(L, element->text.data(), element->text.size());
    return 1;
}

// CLuaGUIDefs.cpp:2108-2133. relative omitted or nil is false. A number is not a boolean.
static int GuiSetPosition(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    double x = 0;
    double y = 0;
    bool relative = false;
    if (!args.gui(id, nullptr) || !args.num(x) || !args.num(y) || !args.flag(relative, true, false))
        return Reject(L, "guiSetPosition", args, false);
    if (GuiElement* element = Find(id)) {
        element->x = x;
        element->y = y;
        element->relPos = relative;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:2080-2105.
static int GuiSetSize(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    double width = 0;
    double height = 0;
    bool relative = false;
    if (!args.gui(id, nullptr) || !args.num(width) || !args.num(height) || !args.flag(relative, true, false))
        return Reject(L, "guiSetSize", args, false);
    if (GuiElement* element = Find(id)) {
        element->w = width;
        element->h = height;
        element->relSize = relative;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:1821-1841. The number is clamped to 0..1. A bad argument changes nothing.
static int GuiSetAlpha(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    double alpha = 0;
    if (!args.gui(id, nullptr) || !args.num(alpha)) return Reject(L, "guiSetAlpha", args, false);
    if (GuiElement* element = Find(id)) element->alpha = std::clamp(alpha, 0.0, 1.0);
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:1844-1864. effectiveAlpha defaults false. A bad element is false, not 0.
static int GuiGetAlpha(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool effective = false;
    if (!args.gui(id, nullptr) || !args.flag(effective, true, false)) return Reject(L, "guiGetAlpha", args, false);
    const GuiElement* element = Find(id);
    if (!element) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, effective ? EffectiveAlpha(*element) : element->alpha);
    return 1;
}

// CLuaGUIDefs.cpp:1890-1910.
static int GuiSetEnabled(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool enabled = false;
    if (!args.gui(id, nullptr) || !args.flag(enabled, false, false)) return Reject(L, "guiSetEnabled", args, false);
    if (GuiElement* element = Find(id)) element->enabled = enabled;
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:1999-2017. IsEnabled walks ancestors (CEGUI localOnly defaults false).
static int GuiGetEnabled(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "guiGetEnabled", args, false);
    const GuiElement* element = Find(id);
    lua_pushboolean(L, element && EffectivelyEnabled(*element) ? 1 : 0);
    return 1;
}

// CLuaFunctionDefs.Input.cpp:116-145 and CResource::ShowCursor (CResource.cpp:1707-1741).
// Holds are keyed by ActiveVmOwner(), the resource name. toggleControls is checked
// so a bad type fails, and is not applied: player-control toggling is not wired.
static int ShowCursor(lua_State* L) {
    Args args{L};
    bool show = false;
    bool toggleControls = true;
    if (!args.flag(show, false, false) || !args.flag(toggleControls, true, true))
        return Reject(L, "showCursor", args, false);
    (void)toggleControls;
    SetCursorHold(CursorKey(ActiveVmOwner(), ActiveVmOwnerGeneration()), show);
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaFunctionDefs.Input.cpp:67-76. True while any resource's hold is still set.
static int IsCursorShowing(lua_State* L) {
    lua_pushboolean(L, AnyCursorHold() ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:593-611. true selects INPUTMODE_NO_BINDS, false selects INPUTMODE_ALLOW_BINDS.
// Bind blocking is not wired. On the phone, true also opens the soft keyboard so a DX
// edit that captures keys (the login boxes) receives onClientCharacter / onClientKey.
static int GuiSetInputEnabled(lua_State* L) {
    Args args{L};
    bool enabled = false;
    if (!args.flag(enabled, false, false)) return Reject(L, "guiSetInputEnabled", args, false);
    g_inputMode = enabled ? InputMode::NoBinds : InputMode::AllowBinds;
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:587-590. Starts false (INPUTMODE_NO_BINDS_ON_EDIT, no focused edit).
static int GuiGetInputEnabled(lua_State* L) {
    lua_pushboolean(L, InputEnabledNow() ? 1 : 0);
    return 1;
}

// CLuaElementDefs.cpp:1717-1738. False unless this GUI element is actually deleted.
// Non-GUI elements are not deleted here.
static int DestroyElement(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "destroyElement", args, false);
    lua_pushboolean(L, GuiDestroy(id) ? 1 : 0);
    return 1;
}

// InstallElementLua (when that file is linked) replaces destroyElement after
// InstallGuiBindings. This runs from InstallGuiLua, which is called again after
// that, and keeps the GUI result: a GUI id returns whether GuiDestroy deleted it.
// Any other id is handed to the binding that was already installed (catalog
// elements). A value that is not an element table deletes nothing and returns false.
static int DestroyElementDispatch(lua_State* L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t id = ElementIdOf(L, 1);
    if (WorldRPG::Game::ReleaseScriptPed(id)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    if (ReleaseStoredElement(id) || AudioStopSound(id)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    if (id == 0 || id >= kFirstGuiId) {
        lua_pushboolean(L, (id >= kFirstGuiId && GuiDestroy(id)) ? 1 : 0);
        return 1;
    }
    const int argc = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, argc, 1);
    return 1;
}

static void InstallDestroyElement(lua_State* L) {
    lua_getglobal(L, "destroyElement");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        lua_pushcfunction(L, DestroyElement);
        lua_setglobal(L, "destroyElement");
        return;
    }
    lua_pushcclosure(L, DestroyElementDispatch, 1);
    lua_setglobal(L, "destroyElement");
}

static bool ClampPercent(double value, double& out) {
    if (std::isnan(value)) return false;
    if (value < 0.0) value = 0.0;
    if (value > 100.0) value = 100.0;
    out = value;
    return true;
}

static int CreateBox(lua_State* L, const char* name, const char* type) {
    Args args{L};
    GuiElement element;
    bool relative = false;
    uint32_t parent = 0;
    if (!args.vec2(element.x, element.y) || !args.vec2(element.w, element.h) ||
        !args.flag(relative, true, false) || !args.parent(parent)) {
        return Reject(L, name, args, false);
    }
    element.type = type;
    element.relPos = relative;
    element.relSize = relative;
    element.parent = parent;
    PushElement(L, Emplace(std::move(element)));
    return 1;
}

// CLuaGUIDefs.cpp 857-887.
static int GuiCreateProgressBar(lua_State* L) { return CreateBox(L, "guiCreateProgressBar", "gui-progressbar"); }
// CLuaGUIDefs.cpp 1065-1095.
static int GuiCreateScrollPane(lua_State* L) { return CreateBox(L, "guiCreateScrollPane", "gui-scrollpane"); }
// CLuaGUIDefs.cpp 1133-1163.
static int GuiCreateTabPanel(lua_State* L) { return CreateBox(L, "guiCreateTabPanel", "gui-tabpanel"); }

// CLuaGUIDefs.cpp 997-1029.
static int GuiCreateMemo(lua_State* L) {
    Args args{L};
    GuiElement element;
    bool relative = false;
    uint32_t parent = 0;
    if (!args.vec2(element.x, element.y) || !args.vec2(element.w, element.h) || !args.text(element.text) ||
        !args.flag(relative, true, false) || !args.parent(parent)) {
        return Reject(L, "guiCreateMemo", args, false);
    }
    element.type = "gui-memo";
    element.relPos = relative;
    element.relSize = relative;
    element.parent = parent;
    PushElement(L, Emplace(std::move(element)));
    return 1;
}

// CLuaGUIDefs.cpp 927-964.
static int GuiCreateRadioButton(lua_State* L) {
    Args args{L};
    GuiElement element;
    bool relative = false;
    uint32_t parent = 0;
    if (!args.vec2(element.x, element.y) || !args.vec2(element.w, element.h) || !args.text(element.text) ||
        !args.flag(relative, true, false) || !args.parent(parent)) {
        return Reject(L, "guiCreateRadioButton", args, false);
    }
    element.type = "gui-radiobutton";
    element.relPos = relative;
    element.relSize = relative;
    element.parent = parent;
    PushElement(L, Emplace(std::move(element)));
    return 1;
}

// CLuaGUIDefs.cpp 1098-1130. horizontal is required.
static int GuiCreateScrollBar(lua_State* L) {
    Args args{L};
    GuiElement element;
    bool relative = false;
    uint32_t parent = 0;
    if (!args.vec2(element.x, element.y) || !args.vec2(element.w, element.h) || !args.flag(element.horizontal, false, false) ||
        !args.flag(relative, true, false) || !args.parent(parent)) {
        return Reject(L, "guiCreateScrollBar", args, false);
    }
    element.type = "gui-scrollbar";
    element.relPos = relative;
    element.relSize = relative;
    element.parent = parent;
    PushElement(L, Emplace(std::move(element)));
    return 1;
}

static bool g_diagImage = false;

// CLuaGUIDefs.cpp 770-819. The file is not decoded; the path is stored.
static int GuiCreateStaticImage(lua_State* L) {
    Args args{L};
    GuiElement element;
    bool relative = false;
    uint32_t parent = 0;
    if (!args.vec2(element.x, element.y) || !args.vec2(element.w, element.h) || !args.text(element.text) ||
        element.text.empty() || ParentSegment(element.text) ||
        !args.flag(relative, true, false) || !args.parent(parent)) {
        return Reject(L, "guiCreateStaticImage", args, false);
    }
    if (!g_diagImage) {
        g_diagImage = true;
        LUA_LOGE("DIAG guiCreateStaticImage: the bitmap is not decoded; the path is stored");
    }
    element.type = "gui-staticimage";
    element.relPos = relative;
    element.relSize = relative;
    element.parent = parent;
    PushElement(L, Emplace(std::move(element)));
    return 1;
}

// CLuaGUIDefs.cpp 1231-1256. The parent is a tab panel. The first tab is selected.
static int GuiCreateTab(lua_State* L) {
    Args args{L};
    std::string text;
    uint32_t parent = 0;
    if (!args.text(text) || !args.gui(parent, "gui-tabpanel")) return Reject(L, "guiCreateTab", args, false);
    GuiElement element;
    element.type = "gui-tab";
    element.text = std::move(text);
    element.parent = parent;
    element.relPos = true;
    element.relSize = true;
    element.w = 1.0;
    element.h = 1.0;
    const uint32_t id = Emplace(std::move(element));
    if (GuiElement* panel = Find(parent))
        if (panel->tab == 0) panel->tab = id;
    PushElement(L, id);
    return 1;
}

// CLuaGUIDefs.cpp 1410-1429. A lower z is behind its siblings.
static int GuiMoveToBack(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "guiMoveToBack", args, false);
    GuiElement* element = Find(id);
    if (!element) {
        lua_pushboolean(L, 0);
        return 1;
    }
    element->z = --g_backZ;
    lua_pushboolean(L, 1);
    return 1;
}

static int SetCaret(lua_State* L, const char* name, const char* type) {
    Args args{L};
    uint32_t id = 0;
    int index = 0;
    if (!args.gui(id, type) || !args.integer(index)) return Reject(L, name, args, false);
    GuiElement* element = Find(id);
    if (!element) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t limit = static_cast<uint32_t>(CaretLimit(*element));
    element->caret = static_cast<int>(std::min(static_cast<uint32_t>(index), limit));
    lua_pushboolean(L, 1);
    return 1;
}

static int GetCaret(lua_State* L, const char* name, const char* type) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, type)) return Reject(L, name, args, false);
    const GuiElement* element = Find(id);
    lua_pushinteger(L, element ? element->caret : 0);
    return 1;
}

// CLuaGUIDefs.cpp 3254-3288 and 3071-3104.
static int GuiEditSetCaretIndex(lua_State* L) { return SetCaret(L, "guiEditSetCaretIndex", "gui-edit"); }
static int GuiEditGetCaretIndex(lua_State* L) { return GetCaret(L, "guiEditGetCaretIndex", "gui-edit"); }
static int GuiMemoSetCaretIndex(lua_State* L) { return SetCaret(L, "guiMemoSetCaretIndex", "gui-memo"); }
static int GuiMemoGetCaretIndex(lua_State* L) { return GetCaret(L, "guiMemoGetCaretIndex", "gui-memo"); }

static int SetReadOnly(lua_State* L, const char* name, const char* type) {
    Args args{L};
    uint32_t id = 0;
    bool state = false;
    if (!args.gui(id, type) || !args.flag(state, false, false)) return Reject(L, name, args, false);
    if (GuiElement* element = Find(id)) element->readOnly = state;
    lua_pushboolean(L, 1);
    return 1;
}

static int IsReadOnly(lua_State* L, const char* name, const char* type) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, type)) return Reject(L, name, args, false);
    const GuiElement* element = Find(id);
    lua_pushboolean(L, element && element->readOnly ? 1 : 0);
    return 1;
}

static int GuiEditSetReadOnly(lua_State* L) { return SetReadOnly(L, "guiEditSetReadOnly", "gui-edit"); }
static int GuiEditIsReadOnly(lua_State* L) { return IsReadOnly(L, "guiEditIsReadOnly", "gui-edit"); }
static int GuiMemoSetReadOnly(lua_State* L) { return SetReadOnly(L, "guiMemoSetReadOnly", "gui-memo"); }
static int GuiMemoIsReadOnly(lua_State* L) { return IsReadOnly(L, "guiMemoIsReadOnly", "gui-memo"); }

// CLuaGUIDefs.cpp 3209-3240.
static int GuiEditSetMaxLength(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int length = 0;
    if (!args.gui(id, "gui-edit") || !args.integer(length)) return Reject(L, "guiEditSetMaxLength", args, false);
    if (GuiElement* element = Find(id)) {
        element->maxLength = static_cast<uint32_t>(length); // Desktop passes the signed Lua value to unsigned SetMaxLength.
        if (static_cast<uint32_t>(TextChars(element->text)) > element->maxLength) {
            element->text.resize(ByteAtChar(element->text, element->maxLength));
            element->caret = std::min(element->caret, TextChars(element->text));
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiEditGetMaxLength(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-edit")) return Reject(L, "guiEditGetMaxLength", args, false);
    const GuiElement* element = Find(id);
    lua_pushinteger(L, element ? element->maxLength : 0);
    return 1;
}

// CLuaGUIDefs.cpp 1524-1545. The value is 0-100 (CGUIProgressBar::SetProgress divides by 100).
static int GuiProgressBarSetProgress(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    double value = 0;
    if (!args.gui(id, "gui-progressbar") || !args.num(value)) return Reject(L, "guiProgressBarSetProgress", args, false);
    // Desktop casts to int before dividing by 100 (CStaticFunctionDefinitions.cpp 6213-6226).
    const int whole = static_cast<int>(value);
    double clamped = 0;
    if (!ClampPercent(static_cast<double>(whole), clamped)) return Reject(L, "guiProgressBarSetProgress", args, false);
    if (GuiElement* element = Find(id)) element->scroll = clamped;
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiProgressBarGetProgress(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-progressbar")) return Reject(L, "guiProgressBarGetProgress", args, false);
    const GuiElement* element = Find(id);
    lua_pushnumber(L, element ? element->scroll : 0);
    return 1;
}

// CLuaGUIDefs.cpp 1432-1453. Selecting one clears the other radios with the same parent.
static int GuiRadioButtonSetSelected(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool state = false;
    if (!args.gui(id, "gui-radiobutton") || !args.flag(state, false, false))
        return Reject(L, "guiRadioButtonSetSelected", args, false);
    GuiElement* element = Find(id);
    if (!element) {
        lua_pushboolean(L, 0);
        return 1;
    }
    element->selected = state;
    if (state) {
        for (auto& [otherId, other] : g_elements) {
            if (otherId != id && other.type == "gui-radiobutton" && other.parent == element->parent)
                other.selected = false;
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiRadioButtonGetSelected(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-radiobutton")) return Reject(L, "guiRadioButtonGetSelected", args, false);
    const GuiElement* element = Find(id);
    lua_pushboolean(L, element && element->selected ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp 1548-1569.
static int GuiScrollBarSetScrollPosition(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    double value = 0;
    if (!args.gui(id, "gui-scrollbar") || !args.num(value)) return Reject(L, "guiScrollBarSetScrollPosition", args, false);
    // Desktop casts to int (CStaticFunctionDefinitions.cpp 6231-6244).
    const int whole = static_cast<int>(value);
    double clamped = 0;
    if (!ClampPercent(static_cast<double>(whole), clamped)) return Reject(L, "guiScrollBarSetScrollPosition", args, false);
    if (GuiElement* element = Find(id)) element->scroll = clamped;
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiScrollBarGetScrollPosition(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-scrollbar")) return Reject(L, "guiScrollBarGetScrollPosition", args, false);
    const GuiElement* element = Find(id);
    lua_pushnumber(L, element ? element->scroll : 0);
    return 1;
}

static int SetPaneScroll(lua_State* L, const char* name, bool horizontal) {
    Args args{L};
    uint32_t id = 0;
    double value = 0;
    if (!args.gui(id, "gui-scrollpane") || !args.num(value)) return Reject(L, name, args, false);
    double clamped = 0;
    if (!ClampPercent(value, clamped)) return Reject(L, name, args, false);
    if (GuiElement* element = Find(id)) {
        if (horizontal) element->scrollX = clamped;
        else element->scroll = clamped;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int GetPaneScroll(lua_State* L, const char* name, bool horizontal) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-scrollpane")) return Reject(L, name, args, false);
    const GuiElement* element = Find(id);
    lua_pushnumber(L, element ? (horizontal ? element->scrollX : element->scroll) : 0);
    return 1;
}

static int GuiScrollPaneSetHorizontalScrollPosition(lua_State* L) {
    return SetPaneScroll(L, "guiScrollPaneSetHorizontalScrollPosition", true);
}
static int GuiScrollPaneGetHorizontalScrollPosition(lua_State* L) {
    return GetPaneScroll(L, "guiScrollPaneGetHorizontalScrollPosition", true);
}
static int GuiScrollPaneSetVerticalScrollPosition(lua_State* L) {
    return SetPaneScroll(L, "guiScrollPaneSetVerticalScrollPosition", false);
}
static int GuiScrollPaneGetVerticalScrollPosition(lua_State* L) {
    return GetPaneScroll(L, "guiScrollPaneGetVerticalScrollPosition", false);
}

static int GuiScrollPaneSetScrollBars(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool horizontal = false;
    bool vertical = false;
    if (!args.gui(id, "gui-scrollpane") || !args.flag(horizontal, false, false) || !args.flag(vertical, false, false))
        return Reject(L, "guiScrollPaneSetScrollBars", args, false);
    if (GuiElement* element = Find(id)) {
        element->barH = horizontal;
        element->barV = vertical;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int SetMemoScroll(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    double value = 0;
    if (!args.gui(id, "gui-memo") || !args.num(value)) return Reject(L, "guiMemoSetVerticalScrollPosition", args, false);
    double clamped = 0;
    if (!ClampPercent(value, clamped)) return Reject(L, "guiMemoSetVerticalScrollPosition", args, false);
    if (GuiElement* element = Find(id)) element->scroll = clamped;
    lua_pushboolean(L, 1);
    return 1;
}

static int GetMemoScroll(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-memo")) return Reject(L, "guiMemoGetVerticalScrollPosition", args, false);
    const GuiElement* element = Find(id);
    lua_pushnumber(L, element ? element->scroll : 0);
    return 1;
}

static int GuiWindowSetMovable(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool state = false;
    if (!args.gui(id, "gui-window") || !args.flag(state, false, false)) return Reject(L, "guiWindowSetMovable", args, false);
    if (GuiElement* element = Find(id)) element->movable = state;
    lua_pushboolean(L, 1);
    return 1;
}
static int GuiWindowIsMovable(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-window")) return Reject(L, "guiWindowIsMovable", args, false);
    const GuiElement* element = Find(id);
    lua_pushboolean(L, element && element->movable ? 1 : 0);
    return 1;
}
static int GuiWindowSetSizable(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool state = false;
    if (!args.gui(id, "gui-window") || !args.flag(state, false, false)) return Reject(L, "guiWindowSetSizable", args, false);
    if (GuiElement* element = Find(id)) element->sizable = state;
    lua_pushboolean(L, 1);
    return 1;
}
static int GuiWindowIsSizable(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-window")) return Reject(L, "guiWindowIsSizable", args, false);
    const GuiElement* element = Find(id);
    lua_pushboolean(L, element && element->sizable ? 1 : 0);
    return 1;
}

static int ColorByte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

// CLuaGUIDefs.cpp 3526-3560.
static int GuiLabelSetColor(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int red = 0, green = 0, blue = 0;
    if (!args.gui(id, "gui-label") || !args.integer(red) || !args.integer(green) || !args.integer(blue))
        return Reject(L, "guiLabelSetColor", args, false);
    if (GuiElement* element = Find(id)) {
        element->red = ColorByte(red);
        element->green = ColorByte(green);
        element->blue = ColorByte(blue);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiLabelGetColor(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-label")) return Reject(L, "guiLabelGetColor", args, false);
    const GuiElement* element = Find(id);
    lua_pushinteger(L, element ? element->red : 255);
    lua_pushinteger(L, element ? element->green : 255);
    lua_pushinteger(L, element ? element->blue : 255);
    return 3;
}

static bool AlignName(const std::string& value, const char* a, const char* b, const char* c) {
    return value == a || value == b || value == c;
}

static int GuiLabelSetHorizontalAlign(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    std::string align;
    bool wordWrap = false;
    if (!args.gui(id, "gui-label") || !args.text(align) || !AlignName(align, "left", "center", "right") ||
        !args.flag(wordWrap, true, false))
        return Reject(L, "guiLabelSetHorizontalAlign", args, false);
    if (GuiElement* element = Find(id)) {
        element->alignH = align;
        element->labelWrap = wordWrap;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiLabelSetVerticalAlign(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    std::string align;
    if (!args.gui(id, "gui-label") || !args.text(align) || !AlignName(align, "top", "center", "bottom"))
        return Reject(L, "guiLabelSetVerticalAlign", args, false);
    if (GuiElement* element = Find(id)) element->alignV = align;
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiLabelGetFontHeight(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-label")) return Reject(L, "guiLabelGetFontHeight", args, false);
    const GuiElement* element = Find(id);
    lua_pushnumber(L, DxMeasureFontHeight(ElementFont(*element)));
    return 1;
}

static int GuiLabelGetTextExtent(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-label")) return Reject(L, "guiLabelGetTextExtent", args, false);
    const GuiElement* element = Find(id);
    // CGUILabel_Impl.cpp:140-157 measures the longest logical line in the assigned font.
    const DxFont font = ElementFont(*element);
    double extent = 0;
    size_t start = 0;
    do {
        const size_t end = element->text.find('\n', start);
        std::string line = element->text.substr(start, end == std::string::npos ? end : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        extent = std::max(extent, DxMeasureLine(line, font));
        if (end == std::string::npos) break;
        start = end + 1;
    } while (start < element->text.size());
    lua_pushnumber(L, extent);
    return 1;
}

static int GuiSetFont(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "guiSetFont", args, false);
    std::string font = "default-normal";
    uint32_t fontId = 0;
    DxFont renderFont;
    if (lua_istable(L, 2)) {
        fontId = ElementIdOf(L, 2);
        if (!DxGuiFontInfo(fontId, renderFont, font)) { lua_pushboolean(L, 0); return 1; }
    } else if (!lua_isnone(L, 2)) {
        if (lua_type(L, 2) != LUA_TSTRING || !args.text(font)) return Reject(L, "guiSetFont", args, false);
        if (font.empty()) font = "default-normal";
        if (!BuiltinGuiFont(font, renderFont) && !DxGuiFontByName(font)) { lua_pushboolean(L, 0); return 1; }
    }
    if (GuiElement* element = Find(id)) {
        element->font = std::move(font);
        element->fontId = fontId;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiGetFont(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "guiGetFont", args, false);
    const GuiElement* element = Find(id);
    if (!element) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushlstring(L, element->font.data(), element->font.size());
    if (element->fontId) PushElement(L, element->fontId);
    else lua_pushnil(L);
    return 2;
}

static int GuiGetSelectedTab(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-tabpanel")) return Reject(L, "guiGetSelectedTab", args, false);
    const GuiElement* panel = Find(id);
    if (!panel || panel->tab == 0 || !Find(panel->tab)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    PushElement(L, panel->tab);
    return 1;
}

static int GuiSetSelectedTab(lua_State* L) {
    Args args{L};
    uint32_t panelId = 0;
    uint32_t tabId = 0;
    if (!args.gui(panelId, "gui-tabpanel") || !args.gui(tabId, "gui-tab"))
        return Reject(L, "guiSetSelectedTab", args, false);
    GuiElement* panel = Find(panelId);
    GuiElement* tab = Find(tabId);
    if (!panel || !tab || tab->parent != panelId) {
        lua_pushboolean(L, 0);
        return 1;
    }
    panel->tab = tabId;
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiDeleteTab(lua_State* L) {
    Args args{L};
    uint32_t tabId = 0;
    uint32_t panelId = 0;
    if (!args.gui(tabId, "gui-tab") || !args.gui(panelId, "gui-tabpanel"))
        return Reject(L, "guiDeleteTab", args, false);
    GuiElement* tab = Find(tabId);
    GuiElement* panel = Find(panelId);
    if (!tab || !panel || tab->parent != panelId) {
        lua_pushboolean(L, 0);
        return 1;
    }
    GuiDestroy(tabId);
    panel = Find(panelId);
    if (panel && panel->tab == tabId) {
        panel->tab = 0;
        for (const auto& [id, element] : g_elements) {
            if (element.parent == panelId && element.type == "gui-tab") {
                panel->tab = id;
                break;
            }
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiStaticImageLoadImage(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    std::string path;
    if (!args.gui(id, "gui-staticimage") || !args.text(path) || path.empty() || ParentSegment(path))
        return Reject(L, "guiStaticImageLoadImage", args, false);
    if (GuiElement* element = Find(id)) element->text = std::move(path);
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiStaticImageGetNativeSize(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-staticimage")) return Reject(L, "guiStaticImageGetNativeSize", args, false);
    lua_pushboolean(L, 0);
    return 1;
}

static int GuiSetProperty(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    std::string key;
    std::string value;
    if (!args.gui(id, nullptr) || !args.text(key) || !args.text(value)) return Reject(L, "guiSetProperty", args, false);
    GuiElement* element = Find(id);
    if (!element) {
        lua_pushboolean(L, 0);
        return 1;
    }
    element->props[key] = value;
    if (key == "AlwaysOnTop") element->alwaysOnTop = value == "True" || value == "true";
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiGetProperty(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    std::string key;
    if (!args.gui(id, nullptr) || !args.text(key)) return Reject(L, "guiGetProperty", args, false);
    const GuiElement* element = Find(id);
    if (!element) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto found = element->props.find(key);
    if (found == element->props.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushlstring(L, found->second.data(), found->second.size());
    return 1;
}

static int GuiGetProperties(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, nullptr)) return Reject(L, "guiGetProperties", args, false);
    const GuiElement* element = Find(id);
    lua_newtable(L);
    if (!element) return 1;
    for (const auto& [key, value] : element->props) {
        lua_pushlstring(L, value.data(), value.size());
        lua_setfield(L, -2, key.c_str());
    }
    return 1;
}

static GuiElement::GridCell* GridCellAt(GuiElement& grid, int row, int column, bool create) {
    const int index = column - 1;
    if (row < 0 || index < 0 || index >= static_cast<int>(grid.columns.size())) return nullptr;
    if (row >= static_cast<int>(grid.gridRows.size())) return nullptr;
    auto& cells = grid.gridRows[static_cast<size_t>(row)];
    if (static_cast<int>(cells.size()) < static_cast<int>(grid.columns.size())) cells.resize(grid.columns.size());
    (void)create;
    return &cells[static_cast<size_t>(index)];
}

static void ClearGridSelection(GuiElement& grid) {
    for (auto& row : grid.gridRows)
        for (auto& cell : row) cell.selected = false;
}

static int CountGridSelection(const GuiElement& grid) {
    int count = 0;
    for (const auto& row : grid.gridRows)
        for (const auto& cell : row)
            if (cell.selected) ++count;
    return count;
}

static void ReleaseCell(lua_State* L, GuiElement::GridCell& cell) {
    if (cell.dataRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, cell.dataRef);
        cell.dataRef = LUA_NOREF;
    }
}

// CLuaGUIDefs.cpp 1032-1062. CGUIGridList_Impl.cpp 78 hides the horizontal bar.
// Selection starts at RowSingle (CGUIGridList.h 23, constructor line 79).
static int GuiCreateGridList(lua_State* L) {
    const int result = CreateBox(L, "guiCreateGridList", "gui-gridlist");
    if (result == 1 && lua_istable(L, -1)) {
        if (GuiElement* grid = Find(ElementIdOf(L, -1))) grid->barH = false;
    }
    return result;
}

// CLuaGUIDefs.cpp 2180-2203. The returned id is 1-based (CGUIGridList_Impl.cpp 178).
static int GuiGridListAddColumn(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    std::string title;
    double width = 0;
    if (!args.gui(id, "gui-gridlist") || !args.text(title) || !args.num(width))
        return Reject(L, "guiGridListAddColumn", args, false);
    GuiElement* grid = Find(id);
    if (!grid) {
        lua_pushboolean(L, 0);
        return 1;
    }
    grid->columns.push_back(GuiElement::GridColumn{std::move(title), width});
    for (auto& row : grid->gridRows) row.resize(grid->columns.size());
    lua_pushinteger(L, static_cast<lua_Integer>(grid->columns.size()));
    return 1;
}

static int GuiGridListRemoveColumn(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int column = 0;
    if (!args.gui(id, "gui-gridlist") || !args.integer(column)) return Reject(L, "guiGridListRemoveColumn", args, false);
    GuiElement* grid = Find(id);
    const int index = column - 1;
    if (grid && index >= 0 && index < static_cast<int>(grid->columns.size())) {
        grid->columns.erase(grid->columns.begin() + index);
        for (auto& row : grid->gridRows) {
            if (index < static_cast<int>(row.size())) {
                ReleaseCell(L, row[static_cast<size_t>(index)]);
                row.erase(row.begin() + index);
            }
        }
    }
    lua_pushboolean(L, grid ? 1 : 0);
    return 1;
}

static int GuiGridListSetColumnWidth(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int column = 0;
    double width = 0;
    bool relative = false;
    if (!args.gui(id, "gui-gridlist") || !args.integer(column) || !args.num(width) || !args.flag(relative, true, false))
        return Reject(L, "guiGridListSetColumnWidth", args, false);
    GuiElement* grid = Find(id);
    const int index = column - 1;
    // CLuaGUIDefs.cpp 2245-2248 returns true even when the column is missing.
    if (grid && index >= 0 && index < static_cast<int>(grid->columns.size())) {
        if (!relative) {
            const double box = std::max(1.0, ScreenBox(id).w);
            width = width / box;
        }
        grid->columns[static_cast<size_t>(index)].width = width;
    }
    lua_pushboolean(L, grid ? 1 : 0);
    return 1;
}

static int GuiGridListGetColumnWidth(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int column = 0;
    bool relative = false;
    if (!args.gui(id, "gui-gridlist") || !args.integer(column) || !args.flag(relative, true, false))
        return Reject(L, "guiGridListGetColumnWidth", args, false);
    const GuiElement* grid = Find(id);
    const int index = column - 1;
    if (!grid || index < 0 || index >= static_cast<int>(grid->columns.size())) {
        lua_pushboolean(L, 0);
        return 1;
    }
    double width = grid->columns[static_cast<size_t>(index)].width;
    if (!relative) width *= ScreenBox(id).w;
    lua_pushnumber(L, width);
    return 1;
}

static int GuiGridListSetColumnTitle(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int column = 0;
    std::string title;
    if (!args.gui(id, "gui-gridlist") || !args.integer(column) || !args.text(title))
        return Reject(L, "guiGridListSetColumnTitle", args, false);
    GuiElement* grid = Find(id);
    const int index = column - 1;
    if (grid && index >= 0 && index < static_cast<int>(grid->columns.size()))
        grid->columns[static_cast<size_t>(index)].title = std::move(title);
    lua_pushboolean(L, grid ? 1 : 0);
    return 1;
}

static int GuiGridListGetColumnTitle(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int column = 0;
    if (!args.gui(id, "gui-gridlist") || !args.integer(column)) return Reject(L, "guiGridListGetColumnTitle", args, false);
    const GuiElement* grid = Find(id);
    const int index = column - 1;
    if (!grid || index < 0 || index >= static_cast<int>(grid->columns.size())) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string& title = grid->columns[static_cast<size_t>(index)].title;
    lua_pushlstring(L, title.data(), title.size());
    return 1;
}

static int GuiGridListGetColumnCount(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-gridlist")) return Reject(L, "guiGridListGetColumnCount", args, false);
    const GuiElement* grid = Find(id);
    lua_pushinteger(L, grid ? static_cast<lua_Integer>(grid->columns.size()) : 0);
    return 1;
}

static int GuiGridListGetRowCount(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-gridlist")) return Reject(L, "guiGridListGetRowCount", args, false);
    const GuiElement* grid = Find(id);
    lua_pushinteger(L, grid ? static_cast<lua_Integer>(grid->gridRows.size()) : 0);
    return 1;
}

// CLuaGUIDefs.cpp 2363-2386. A value that is not a string or a number is skipped and
// does not take a column.
static void FillRowText(lua_State* L, GuiElement& grid, int row, int firstArg) {
    if (row < 0 || row >= static_cast<int>(grid.gridRows.size())) return;
    auto& cells = grid.gridRows[static_cast<size_t>(row)];
    cells.resize(grid.columns.size());
    int column = 1;
    for (int arg = firstArg; arg <= lua_gettop(L) && column <= static_cast<int>(grid.columns.size()); ++arg) {
        const int type = lua_type(L, arg);
        if (type != LUA_TNUMBER && type != LUA_TSTRING) continue;
        auto& cell = cells[static_cast<size_t>(column - 1)];
        size_t length = 0;
        const char* text = lua_tolstring(L, arg, &length);
        cell.text.assign(text ? text : "", length);
        cell.number = type == LUA_TNUMBER;
        ++column;
    }
}

// CLuaGUIDefs.cpp 2344-2403. The row index is 0-based.
static int GuiGridListAddRow(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-gridlist")) return Reject(L, "guiGridListAddRow", args, false);
    GuiElement* grid = Find(id);
    if (!grid) {
        lua_pushboolean(L, 0);
        return 1;
    }
    grid->gridRows.emplace_back();
    const int row = static_cast<int>(grid->gridRows.size()) - 1;
    FillRowText(L, *grid, row, 2);
    lua_pushinteger(L, row);
    return 1;
}

static int GuiGridListInsertRowAfter(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int after = 0;
    if (!args.gui(id, "gui-gridlist") || !args.integer(after)) return Reject(L, "guiGridListInsertRowAfter", args, false);
    GuiElement* grid = Find(id);
    if (!grid || after < -1 || after >= static_cast<int>(grid->gridRows.size())) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int row = after + 1;
    // An empty brace here is an empty initializer list and inserts nothing.
    grid->gridRows.emplace(grid->gridRows.begin() + row);
    FillRowText(L, *grid, row, 3);
    lua_pushinteger(L, row);
    return 1;
}

static int GuiGridListRemoveRow(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int row = 0;
    if (!args.gui(id, "gui-gridlist") || !args.integer(row)) return Reject(L, "guiGridListRemoveRow", args, false);
    GuiElement* grid = Find(id);
    // CLuaGUIDefs.cpp 2692 returns true. A missing row is left unchanged.
    if (grid && row >= 0 && row < static_cast<int>(grid->gridRows.size())) {
        for (auto& cell : grid->gridRows[static_cast<size_t>(row)]) ReleaseCell(L, cell);
        grid->gridRows.erase(grid->gridRows.begin() + row);
    }
    lua_pushboolean(L, grid ? 1 : 0);
    return 1;
}

static int GuiGridListClear(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-gridlist")) return Reject(L, "guiGridListClear", args, false);
    GuiElement* grid = Find(id);
    if (grid) {
        for (auto& row : grid->gridRows)
            for (auto& cell : row) ReleaseCell(L, cell);
        grid->gridRows.clear();
    }
    lua_pushboolean(L, grid ? 1 : 0);
    return 1;
}

static int GuiGridListSetItemText(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int row = 0, column = 0;
    std::string text;
    bool section = false, number = false;
    if (!args.gui(id, "gui-gridlist") || !args.integer(row) || !args.integer(column) || !args.text(text) ||
        !args.flag(section, false, false) || !args.flag(number, false, false))
        return Reject(L, "guiGridListSetItemText", args, false);
    GuiElement* grid = Find(id);
    // CLuaGUIDefs.cpp 2853-2856 returns true; a missing cell is left unchanged.
    if (GuiElement::GridCell* cell = grid ? GridCellAt(*grid, row, column, true) : nullptr) {
        cell->text = std::move(text);
        cell->section = section;
        cell->number = number;
    }
    lua_pushboolean(L, grid ? 1 : 0);
    return 1;
}

static int GuiGridListGetItemText(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int row = 0, column = 0;
    if (!args.gui(id, "gui-gridlist") || !args.integer(row) || !args.integer(column))
        return Reject(L, "guiGridListGetItemText", args, false);
    GuiElement* grid = Find(id);
    GuiElement::GridCell* cell = grid ? GridCellAt(*grid, row, column, false) : nullptr;
    const char* text = cell ? cell->text.c_str() : "";
    lua_pushstring(L, text);
    return 1;
}

static int GuiGridListSetItemData(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int row = 0, column = 0;
    if (!args.gui(id, "gui-gridlist") || !args.integer(row) || !args.integer(column) || lua_isnone(L, args.index))
        return Reject(L, "guiGridListSetItemData", args, false);
    GuiElement* grid = Find(id);
    if (GuiElement::GridCell* cell = grid ? GridCellAt(*grid, row, column, true) : nullptr) {
        ReleaseCell(L, *cell);
        lua_pushvalue(L, 4);
        cell->dataRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_pushboolean(L, grid ? 1 : 0);
    return 1;
}

static int GuiGridListGetItemData(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int row = 0, column = 0;
    if (!args.gui(id, "gui-gridlist") || !args.integer(row) || !args.integer(column))
        return Reject(L, "guiGridListGetItemData", args, false);
    GuiElement* grid = Find(id);
    GuiElement::GridCell* cell = grid ? GridCellAt(*grid, row, column, false) : nullptr;
    if (!cell || cell->dataRef == LUA_NOREF) {
        lua_pushnil(L);
        return 1;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, cell->dataRef);
    return 1;
}

static int GuiGridListSetItemColor(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int row = 0, column = 0, red = 0, green = 0, blue = 0, alpha = 255;
    if (!args.gui(id, "gui-gridlist") || !args.integer(row) || !args.integer(column) || !args.integer(red) ||
        !args.integer(green) || !args.integer(blue))
        return Reject(L, "guiGridListSetItemColor", args, false);
    if (lua_type(L, args.index) != LUA_TNONE && lua_type(L, args.index) != LUA_TNIL && !args.integer(alpha))
        return Reject(L, "guiGridListSetItemColor", args, false);
    GuiElement* grid = Find(id);
    if (GuiElement::GridCell* cell = grid ? GridCellAt(*grid, row, column, true) : nullptr) {
        cell->r = ColorByte(red);
        cell->g = ColorByte(green);
        cell->b = ColorByte(blue);
        cell->a = ColorByte(alpha);
    }
    lua_pushboolean(L, grid ? 1 : 0);
    return 1;
}

static int GuiGridListGetItemColor(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int row = 0, column = 0;
    if (!args.gui(id, "gui-gridlist") || !args.integer(row) || !args.integer(column))
        return Reject(L, "guiGridListGetItemColor", args, false);
    GuiElement* grid = Find(id);
    GuiElement::GridCell* cell = grid ? GridCellAt(*grid, row, column, false) : nullptr;
    if (!cell) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, cell->r);
    lua_pushinteger(L, cell->g);
    lua_pushinteger(L, cell->b);
    lua_pushinteger(L, cell->a);
    return 4;
}

// CGUIGridList.h 23-32. Single modes replace the previous selection inside
// CEGUI setItemSelectState, which SetSelectedItem calls after the optional clear.
static bool SingleGridMode(int mode) {
    return mode == 0 || mode == 2 || mode == 4 || mode == 6 || mode == 8;
}

static void MarkGridSelection(GuiElement& grid, int row, int column) {
    if (!GridCellAt(grid, row, column, false)) return;
    const int mode = grid.gridMode;
    if (mode == 0 || mode == 1 || mode == 8 || mode == 9) {
        for (auto& cell : grid.gridRows[static_cast<size_t>(row)]) cell.selected = true;
        return;
    }
    if (mode == 6 || mode == 7) {
        const int index = column - 1;
        for (auto& cells : grid.gridRows) {
            if (static_cast<int>(cells.size()) < static_cast<int>(grid.columns.size())) cells.resize(grid.columns.size());
            if (index >= 0 && index < static_cast<int>(cells.size())) cells[static_cast<size_t>(index)].selected = true;
        }
        return;
    }
    grid.gridRows[static_cast<size_t>(row)][static_cast<size_t>(column - 1)].selected = true;
}

static int GuiGridListSetSelectedItem(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int row = 0, column = 0;
    bool reset = true;
    if (!args.gui(id, "gui-gridlist") || !args.integer(row) || !args.integer(column) || !args.flag(reset, true, true))
        return Reject(L, "guiGridListSetSelectedItem", args, false);
    GuiElement* grid = Find(id);
    if (!grid) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // CGUIGridList_Impl.cpp 761-764 clears when bReset is true. A single mode
    // also replaces the previous selection.
    if (reset || SingleGridMode(grid->gridMode)) ClearGridSelection(*grid);
    if (row >= 0) MarkGridSelection(*grid, row, column);
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiGridListGetSelectedItem(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-gridlist")) return Reject(L, "guiGridListGetSelectedItem", args, false);
    const GuiElement* grid = Find(id);
    int row = -1, column = 0;
    if (grid) {
        for (int r = 0; r < static_cast<int>(grid->gridRows.size()) && row < 0; ++r) {
            for (int c = 0; c < static_cast<int>(grid->gridRows[static_cast<size_t>(r)].size()); ++c) {
                if (!grid->gridRows[static_cast<size_t>(r)][static_cast<size_t>(c)].selected) continue;
                row = r;
                column = c + 1;
                break;
            }
        }
    }
    lua_pushinteger(L, row);
    lua_pushinteger(L, column);
    return 2;
}

static int GuiGridListGetSelectedCount(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-gridlist")) return Reject(L, "guiGridListGetSelectedCount", args, false);
    const GuiElement* grid = Find(id);
    lua_pushinteger(L, grid ? CountGridSelection(*grid) : 0);
    return 1;
}

static int GuiGridListGetSelectedItems(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-gridlist")) return Reject(L, "guiGridListGetSelectedItems", args, false);
    const GuiElement* grid = Find(id);
    lua_newtable(L);
    if (!grid) return 1;
    int index = 0;
    for (int r = 0; r < static_cast<int>(grid->gridRows.size()); ++r) {
        for (int c = 0; c < static_cast<int>(grid->gridRows[static_cast<size_t>(r)].size()); ++c) {
            if (!grid->gridRows[static_cast<size_t>(r)][static_cast<size_t>(c)].selected) continue;
            lua_pushinteger(L, ++index);
            lua_newtable(L);
            lua_pushinteger(L, c);
            lua_setfield(L, -2, "column");
            lua_pushinteger(L, r);
            lua_setfield(L, -2, "row");
            lua_settable(L, -3);
        }
    }
    return 1;
}

static int GuiGridListSetSelectionMode(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int mode = 0;
    if (!args.gui(id, "gui-gridlist") || !args.integer(mode)) return Reject(L, "guiGridListSetSelectionMode", args, false);
    if (GuiElement* grid = Find(id)) grid->gridMode = mode;
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiGridListGetSelectionMode(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-gridlist")) return Reject(L, "guiGridListGetSelectionMode", args, true);
    const GuiElement* grid = Find(id);
    lua_pushinteger(L, grid ? grid->gridMode : 0);
    return 1;
}

static int GuiGridListSetSortingEnabled(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool enabled = false;
    if (!args.gui(id, "gui-gridlist") || !args.flag(enabled, false, false))
        return Reject(L, "guiGridListSetSortingEnabled", args, false);
    // The flag is stored. Rows stay in insertion order: CEGUI's sort would change the
    // index addRow returned (CGUIGridList_Impl::Sort, 780-797), and that reorder is not hooked.
    if (GuiElement* grid = Find(id)) grid->gridSort = enabled;
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiGridListIsSortingEnabled(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    if (!args.gui(id, "gui-gridlist")) return Reject(L, "guiGridListIsSortingEnabled", args, true);
    const GuiElement* grid = Find(id);
    lua_pushboolean(L, grid && grid->gridSort ? 1 : 0);
    return 1;
}

static int GuiGridListSetScrollBars(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    bool horizontal = false, vertical = false;
    if (!args.gui(id, "gui-gridlist") || !args.flag(horizontal, false, false) || !args.flag(vertical, false, false))
        return Reject(L, "guiGridListSetScrollBars", args, false);
    if (GuiElement* grid = Find(id)) {
        grid->barH = horizontal;
        grid->barV = vertical;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiGridListAutoSizeColumn(lua_State* L) {
    Args args{L};
    uint32_t id = 0;
    int column = 0;
    if (!args.gui(id, "gui-gridlist") || !args.integer(column)) return Reject(L, "guiGridListAutoSizeColumn", args, false);
    GuiElement* grid = Find(id);
    const int index = column - 1;
    if (!grid || index < 0 || index >= static_cast<int>(grid->columns.size())) {
        lua_pushboolean(L, grid ? 1 : 0);
        return 1;
    }
    int chars = TextChars(grid->columns[static_cast<size_t>(index)].title);
    for (const auto& row : grid->gridRows) {
        if (index < static_cast<int>(row.size())) chars = std::max(chars, TextChars(row[static_cast<size_t>(index)].text));
    }
    const double box = std::max(1.0, ScreenBox(id).w);
    grid->columns[static_cast<size_t>(index)].width = std::min(1.0, (8.0 * chars + 12.0) / box);
    lua_pushboolean(L, 1);
    return 1;
}

static int GridScroll(lua_State* L, const char* name, bool set, bool horizontal) {
    Args args{L};
    uint32_t id = 0;
    double value = 0;
    if (!args.gui(id, "gui-gridlist") || (set && !args.num(value))) return Reject(L, name, args, false);
    GuiElement* grid = Find(id);
    if (!grid) {
        lua_pushboolean(L, 0);
        return 1;
    }
    double& slot = horizontal ? grid->scrollX : grid->scroll;
    if (!set) {
        lua_pushnumber(L, slot);
        return 1;
    }
    double clamped = 0;
    if (!ClampPercent(value, clamped)) return Reject(L, name, args, false);
    slot = clamped;
    lua_pushboolean(L, 1);
    return 1;
}

static int GuiGridListSetHorizontalScrollPosition(lua_State* L) {
    return GridScroll(L, "guiGridListSetHorizontalScrollPosition", true, true);
}
static int GuiGridListGetHorizontalScrollPosition(lua_State* L) {
    return GridScroll(L, "guiGridListGetHorizontalScrollPosition", false, true);
}
static int GuiGridListSetVerticalScrollPosition(lua_State* L) {
    return GridScroll(L, "guiGridListSetVerticalScrollPosition", true, false);
}
static int GuiGridListGetVerticalScrollPosition(lua_State* L) {
    return GridScroll(L, "guiGridListGetVerticalScrollPosition", false, false);
}

// CLuaGUIDefs.cpp:4009-4055. Font lifetime belongs to the calling resource,
// including when its file comes from another resource.
static int GuiCreateFont(lua_State* L) {
    Args args{L};
    std::string path;
    int size = 9;
    if (!args.text(path) || path.empty() || ParentSegment(path)) return Reject(L, "guiCreateFont", args, false);
    if (lua_type(L, args.index) != LUA_TNONE && lua_type(L, args.index) != LUA_TNIL && !args.integer(size))
        return Reject(L, "guiCreateFont", args, false);
    const uint32_t id = DxCreateGuiFont(path, static_cast<uint32_t>(size));
    if (id) PushElement(L, id);
    else lua_pushboolean(L, 0);
    return 1;
}

// CLuaGUIDefs.cpp 4058-4063 and CLuaFunctionParseHelpers.cpp 573. GetCursorType is not
// hooked, so the result stays CURSORTYPE_DEFAULT ("arrow").
static int GuiGetCursorType(lua_State* L) {
    lua_pushstring(L, "arrow");
    return 1;
}

static void RegisterGuiExtensions(lua_State* L) {
    g_guiVm = L;
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"dxGetPixelColor", DxGetPixelColor},
        {"dxGetPixelsFormat", DxGetPixelsFormat},
        {"dxSetPixelColor", DxSetPixelColor},
        {"guiBlur", GuiBlur},
        {"guiEditIsMasked", GuiEditIsMasked},
        {"guiEditSetMasked", GuiEditSetMasked},
        {"guiFocus", GuiFocus},
        {"guiGetInputMode", GuiGetInputMode},
        {"guiSetInputMode", GuiSetInputMode},
        {"guiBringToFront", GuiBringToFront},
        {"guiCheckBoxGetSelected", GuiCheckBoxGetSelected},
        {"guiCheckBoxSetSelected", GuiCheckBoxSetSelected},
        {"guiComboBoxAddItem", GuiComboBoxAddItem},
        {"guiComboBoxClear", GuiComboBoxClear},
        {"guiComboBoxGetItemCount", GuiComboBoxGetItemCount},
        {"guiComboBoxGetItemText", GuiComboBoxGetItemText},
        {"guiComboBoxGetSelected", GuiComboBoxGetSelected},
        {"guiComboBoxIsOpen", GuiComboBoxIsOpen},
        {"guiComboBoxRemoveItem", GuiComboBoxRemoveItem},
        {"guiComboBoxSetItemText", GuiComboBoxSetItemText},
        {"guiComboBoxSetOpen", GuiComboBoxSetOpen},
        {"guiComboBoxSetSelected", GuiComboBoxSetSelected},
        {"guiCreateCheckBox", GuiCreateCheckBox},
        {"guiCreateComboBox", GuiCreateComboBox},
        {"guiCreateEdit", GuiCreateEdit},
        {"guiCreateFont", GuiCreateFont},
        {"guiCreateGridList", GuiCreateGridList},
        {"guiCreateMemo", GuiCreateMemo},
        {"guiCreateProgressBar", GuiCreateProgressBar},
        {"guiCreateRadioButton", GuiCreateRadioButton},
        {"guiCreateScrollBar", GuiCreateScrollBar},
        {"guiCreateScrollPane", GuiCreateScrollPane},
        {"guiCreateStaticImage", GuiCreateStaticImage},
        {"guiCreateTab", GuiCreateTab},
        {"guiCreateTabPanel", GuiCreateTabPanel},
        {"guiDeleteTab", GuiDeleteTab},
        {"guiEditGetCaretIndex", GuiEditGetCaretIndex},
        {"guiEditGetMaxLength", GuiEditGetMaxLength},
        {"guiEditIsReadOnly", GuiEditIsReadOnly},
        {"guiEditSetCaretIndex", GuiEditSetCaretIndex},
        // CLuaCompatibilityDefs.cpp:68. The old spelling calls the same caret setter.
        {"guiEditSetCaratIndex", GuiEditSetCaretIndex},
        {"guiEditSetMaxLength", GuiEditSetMaxLength},
        {"guiEditSetReadOnly", GuiEditSetReadOnly},
        {"guiGetCursorType", GuiGetCursorType},
        {"guiGetFont", GuiGetFont},
        {"guiGridListAddColumn", GuiGridListAddColumn},
        {"guiGridListAddRow", GuiGridListAddRow},
        {"guiGridListAutoSizeColumn", GuiGridListAutoSizeColumn},
        {"guiGridListClear", GuiGridListClear},
        {"guiGridListGetColumnCount", GuiGridListGetColumnCount},
        {"guiGridListGetColumnTitle", GuiGridListGetColumnTitle},
        {"guiGridListGetColumnWidth", GuiGridListGetColumnWidth},
        {"guiGridListGetHorizontalScrollPosition", GuiGridListGetHorizontalScrollPosition},
        {"guiGridListGetItemColor", GuiGridListGetItemColor},
        {"guiGridListGetItemData", GuiGridListGetItemData},
        {"guiGridListGetItemText", GuiGridListGetItemText},
        {"guiGridListGetRowCount", GuiGridListGetRowCount},
        {"guiGridListGetSelectedCount", GuiGridListGetSelectedCount},
        {"guiGridListGetSelectedItem", GuiGridListGetSelectedItem},
        {"guiGridListGetSelectedItems", GuiGridListGetSelectedItems},
        {"guiGridListGetSelectionMode", GuiGridListGetSelectionMode},
        {"guiGridListGetVerticalScrollPosition", GuiGridListGetVerticalScrollPosition},
        {"guiGridListInsertRowAfter", GuiGridListInsertRowAfter},
        {"guiGridListIsSortingEnabled", GuiGridListIsSortingEnabled},
        {"guiGridListRemoveColumn", GuiGridListRemoveColumn},
        {"guiGridListRemoveRow", GuiGridListRemoveRow},
        {"guiGridListSetColumnTitle", GuiGridListSetColumnTitle},
        {"guiGridListSetColumnWidth", GuiGridListSetColumnWidth},
        {"guiGridListSetHorizontalScrollPosition", GuiGridListSetHorizontalScrollPosition},
        {"guiGridListSetItemColor", GuiGridListSetItemColor},
        {"guiGridListSetItemData", GuiGridListSetItemData},
        {"guiGridListSetItemText", GuiGridListSetItemText},
        {"guiGridListSetScrollBars", GuiGridListSetScrollBars},
        {"guiGridListSetSelectedItem", GuiGridListSetSelectedItem},
        {"guiGridListSetSelectionMode", GuiGridListSetSelectionMode},
        {"guiGridListSetSortingEnabled", GuiGridListSetSortingEnabled},
        {"guiGridListSetVerticalScrollPosition", GuiGridListSetVerticalScrollPosition},
        {"guiGetProperties", GuiGetProperties},
        {"guiGetProperty", GuiGetProperty},
        {"guiGetSelectedTab", GuiGetSelectedTab},
        {"guiLabelGetColor", GuiLabelGetColor},
        {"guiLabelGetFontHeight", GuiLabelGetFontHeight},
        {"guiLabelGetTextExtent", GuiLabelGetTextExtent},
        {"guiLabelSetColor", GuiLabelSetColor},
        {"guiLabelSetHorizontalAlign", GuiLabelSetHorizontalAlign},
        {"guiLabelSetVerticalAlign", GuiLabelSetVerticalAlign},
        {"guiMemoGetCaretIndex", GuiMemoGetCaretIndex},
        {"guiMemoGetVerticalScrollPosition", GetMemoScroll},
        {"guiMemoIsReadOnly", GuiMemoIsReadOnly},
        {"guiMemoSetCaretIndex", GuiMemoSetCaretIndex},
        // CLuaCompatibilityDefs.cpp:69.
        {"guiMemoSetCaratIndex", GuiMemoSetCaretIndex},
        {"guiMemoSetReadOnly", GuiMemoSetReadOnly},
        {"guiMemoSetVerticalScrollPosition", SetMemoScroll},
        {"guiMoveToBack", GuiMoveToBack},
        {"guiProgressBarGetProgress", GuiProgressBarGetProgress},
        {"guiProgressBarSetProgress", GuiProgressBarSetProgress},
        {"guiRadioButtonGetSelected", GuiRadioButtonGetSelected},
        {"guiRadioButtonSetSelected", GuiRadioButtonSetSelected},
        {"guiScrollBarGetScrollPosition", GuiScrollBarGetScrollPosition},
        {"guiScrollBarSetScrollPosition", GuiScrollBarSetScrollPosition},
        {"guiScrollPaneGetHorizontalScrollPosition", GuiScrollPaneGetHorizontalScrollPosition},
        {"guiScrollPaneGetVerticalScrollPosition", GuiScrollPaneGetVerticalScrollPosition},
        {"guiScrollPaneSetHorizontalScrollPosition", GuiScrollPaneSetHorizontalScrollPosition},
        {"guiScrollPaneSetScrollBars", GuiScrollPaneSetScrollBars},
        {"guiScrollPaneSetVerticalScrollPosition", GuiScrollPaneSetVerticalScrollPosition},
        {"guiSetFont", GuiSetFont},
        {"guiSetProperty", GuiSetProperty},
        {"guiSetSelectedTab", GuiSetSelectedTab},
        {"guiStaticImageGetNativeSize", GuiStaticImageGetNativeSize},
        {"guiStaticImageLoadImage", GuiStaticImageLoadImage},
        {"guiWindowIsMovable", GuiWindowIsMovable},
        {"guiWindowIsSizable", GuiWindowIsSizable},
        {"guiWindowSetMovable", GuiWindowSetMovable},
        {"guiWindowSetSizable", GuiWindowSetSizable},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    InstallDestroyElement(L);
}

} // namespace WorldRPG::Script

extern "C" void InstallGuiLua(lua_State* L) {
    WorldRPG::Script::RegisterGuiExtensions(L);
}
