// Native UI. Desktop CLuaNativeUIDefs.cpp and the handle checks in CNativeUISA::Show/Clear.
// Widgets are stored for the owning resource. Nothing is drawn and input is not captured.

#include "LuaNativeUiApi.h"

#include "LuaVMStub.h"

#include "lua.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldRPG::Script {
namespace {

enum class Kind { Text, Clock, Counter, Menu, Grid, DrawText, Window, Rectangle, Card };

struct Options {
    uint32_t text = 0;
    std::string name;
    std::string content;
    bool gxt = false;
    float x = 40, y = 100, width = 260, height = 80;
    float scaleX = 0.48f, scaleY = 1.12f;
    uint32_t color = 0xFFFFFFFFu, background = 0x000000BEu, dropColor = 0x000000FFu;
    int font = 1, alignment = 0, shadow = 2, outline = 0, style = 0, selected = 0;
    double value = 0, maximum = 100;
    bool visible = true, beforeFade = true, paused = false, countdown = true, flash = false, proportional = true;
    unsigned columns = 1, card = 53, beepSeconds = 0;
    int number1 = -1, number2 = -1;
    std::vector<uint32_t> cells;
    std::vector<uint32_t> headers;
    std::vector<char> enabled;
    std::vector<float> widths;
    std::vector<int> alignments;
    std::string encoded;
};

struct Message {
    std::string owner;
    uint32_t text = 0;
};

struct Widget {
    std::string owner;
    Kind kind = Kind::Text;
    Options options;
};

std::unordered_map<uint32_t, Widget> g_widgets;
uint32_t g_next = 1;
bool g_diag = false;
std::unordered_map<std::string, Message> g_messages;

const char* KindName(Kind kind) {
    const char* names[] = {"text", "clock", "counter", "menu", "grid", "drawText", "window", "rectangle", "card"};
    return names[static_cast<int>(kind)];
}

bool KindFromName(const std::string& name, Kind& kind) {
    for (int i = 0; i < 9; ++i) {
        if (name == KindName(static_cast<Kind>(i))) {
            kind = static_cast<Kind>(i);
            return true;
        }
    }
    return false;
}

std::string OwnerKey(const std::string& name, uint16_t netId, uint32_t generation) {
    return name + ":" + std::to_string(netId) + ":" + std::to_string(generation);
}

std::string Owner() { return OwnerKey(ActiveVmOwner(), ActiveVmOwnerNet(), ActiveVmOwnerGeneration()); }

void DropOwner(const std::string& owner) {
    std::erase_if(g_widgets, [&](const auto& entry) { return entry.second.owner == owner; });
    std::erase_if(g_messages, [&](const auto& entry) { return entry.second.owner == owner; });
}

int Failure(lua_State* L, const std::string& error) {
    lua_pushboolean(L, 0);
    lua_pushlstring(L, error.data(), error.size());
    return 2;
}

bool StringAt(lua_State* L, int index, std::string& out) {
    if (lua_type(L, index) != LUA_TSTRING) return false;
    size_t size = 0;
    const char* value = lua_tolstring(L, index, &size);
    if (!value || size > 640 || std::memchr(value, 0, size)) return false;
    out.assign(value, size);
    return true;
}

bool NumberAt(lua_State* L, int index, double& out, bool integer = false) {
    if (lua_type(L, index) != LUA_TNUMBER) return false;
    out = lua_tonumber(L, index);
    return std::isfinite(out) && (!integer || std::floor(out) == out);
}

bool HandleAt(lua_State* L, int index, uint32_t& id) {
    double number = 0;
    if (!NumberAt(L, index, number, true) || number < 1 || number > 0xFFFFFF) return false;
    id = static_cast<uint32_t>(number);
    return true;
}

bool OptionsOf(lua_State* L, int index, Options& options, Kind kind, std::string& error) {
    if (!lua_istable(L, index)) {
        error = "options-table-required";
        return false;
    }
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        std::string key;
        bool valid = StringAt(L, -2, key);
        double number = 0;
        auto numeric = [&](auto& value, double low, double high, bool integer = false) {
            if (!NumberAt(L, -1, number, integer) || number < low || number > high) return false;
            value = static_cast<std::decay_t<decltype(value)>>(number);
            return true;
        };
        auto boolean = [&](bool& value) {
            if (lua_type(L, -1) != LUA_TBOOLEAN) return false;
            value = lua_toboolean(L, -1) != 0;
            return true;
        };
        if (!valid) {
        } else if (key == "name") valid = StringAt(L, -1, options.name);
        else if (key == "content") valid = StringAt(L, -1, options.content);
        else if (key == "gxt") valid = boolean(options.gxt);
        else if (key == "text") valid = numeric(options.text, 0, 0xFFFFFF, true);
        else if (key == "x") valid = numeric(options.x, 0, 640);
        else if (key == "y") valid = numeric(options.y, 0, 448);
        else if (key == "width") valid = numeric(options.width, 0.01, 640);
        else if (key == "height") valid = numeric(options.height, 0.01, 448);
        else if (key == "scaleX") valid = numeric(options.scaleX, 0.01, 4);
        else if (key == "scaleY") valid = numeric(options.scaleY, 0.01, 4);
        else if (key == "color") valid = numeric(options.color, 0, 4294967295.0, true);
        else if (key == "background") valid = numeric(options.background, 0, 4294967295.0, true);
        else if (key == "dropColor") valid = numeric(options.dropColor, 0, 4294967295.0, true);
        else if (key == "font") valid = numeric(options.font, 0, 2, true);
        else if (key == "alignment") valid = numeric(options.alignment, 0, 2, true);
        else if (key == "shadow") valid = numeric(options.shadow, 0, 4, true);
        else if (key == "outline") valid = numeric(options.outline, 0, 4, true);
        else if (key == "style") valid = numeric(options.style, 0, 2, true);
        else if (key == "selected") {
            valid = numeric(options.selected, 1, 64, true);
            if (valid) --options.selected;
        } else if (key == "value") valid = numeric(options.value, 0, 2147483647);
        else if (key == "maximum") valid = numeric(options.maximum, 0.01, 2147483647);
        else if (key == "beepSeconds") valid = numeric(options.beepSeconds, 0, 60, true);
        else if (key == "card") valid = numeric(options.card, 1, 53, true);
        else if (key == "columns") valid = numeric(options.columns, 1, 8, true);
        else if (key == "visible") valid = boolean(options.visible);
        else if (key == "beforeFade") valid = boolean(options.beforeFade);
        else if (key == "paused") valid = boolean(options.paused);
        else if (key == "countdown") valid = boolean(options.countdown);
        else if (key == "flash") valid = boolean(options.flash);
        else if (key == "proportional") valid = boolean(options.proportional);
        else if (key == "number1") valid = numeric(options.number1, -2147483647, 2147483647, true);
        else if (key == "number2") valid = numeric(options.number2, -2147483647, 2147483647, true);
        else if (key == "cells" || key == "headers" || key == "enabled" || key == "widths" || key == "alignments") {
            valid = lua_istable(L, -1);
            if (valid) {
                const int count = static_cast<int>(lua_rawlen(L, -1));
                valid = count <= 48;
                int seen = 0;
                lua_pushnil(L);
                while (lua_next(L, -2) != 0) {
                    double arrayIndex = 0;
                    if (!NumberAt(L, -2, arrayIndex, true) || arrayIndex < 1 || arrayIndex > count) valid = false;
                    ++seen;
                    lua_pop(L, 1);
                }
                if (seen != count) valid = false;
                if (key == "cells") options.cells.clear();
                if (key == "headers") options.headers.clear();
                if (key == "enabled") options.enabled.clear();
                if (key == "widths") options.widths.clear();
                if (key == "alignments") options.alignments.clear();
                for (int i = 0; valid && i < count; ++i) {
                    lua_rawgeti(L, -1, i + 1);
                    if (key == "enabled") {
                        valid = lua_type(L, -1) == LUA_TBOOLEAN;
                        if (valid) options.enabled.push_back(static_cast<char>(lua_toboolean(L, -1) != 0));
                    } else if (key == "cells" || key == "headers") {
                        uint32_t handle = 0;
                        valid = HandleAt(L, -1, handle);
                        if (valid) (key == "cells" ? options.cells : options.headers).push_back(handle);
                    } else {
                        valid = NumberAt(L, -1, number, key == "alignments") && number >= 0 && number <= (key == "alignments" ? 2 : 640);
                        if (valid) {
                            if (key == "widths") options.widths.push_back(static_cast<float>(number));
                            else options.alignments.push_back(static_cast<int>(number));
                        }
                    }
                    lua_pop(L, 1);
                }
            }
        } else valid = false;
        lua_pop(L, 1);
        if (!valid) {
            lua_pop(L, 1);
            error = "invalid-option:" + key;
            return false;
        }
    }
    if (kind == Kind::Menu && options.enabled.empty() && options.columns && options.cells.size() % options.columns == 0)
        options.enabled.assign(options.cells.size() / options.columns, 1);
    return true;
}

bool EncodeText(const std::string& input, std::string& output) {
    static const uint16_t upper[] = {0xC0, 0xC1, 0xC2, 0xC4, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD2, 0xD3,
                                      0xD4, 0xD6, 0xD9, 0xDA, 0xDB, 0xDC, 0xDF, 0xE0, 0xE1, 0xE2, 0xE4, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
                                      0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF6, 0xF9, 0xFA, 0xFB, 0xFC, 0xD1, 0xF1, 0xBF};
    output.clear();
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned cp = static_cast<unsigned char>(input[i]);
        if (cp >= 0xC2 && cp <= 0xDF) {
            if (++i >= input.size()) return false;
            const auto tail = static_cast<unsigned char>(input[i]);
            if ((tail & 0xC0) != 0x80) return false;
            cp = ((cp & 31) << 6) | (tail & 63);
        } else if (cp >= 128) return false;
        if (cp == 10) output += "~n~";
        else if (cp >= 32 && cp < 127) {
            if (cp == '[' || cp == ']' || cp == '^') return false;
            output += static_cast<char>(cp == '<' ? '[' : cp == '>' ? ']' : cp);
        } else {
            const auto found = std::find(std::begin(upper), std::end(upper), static_cast<uint16_t>(cp));
            if (found == std::end(upper)) return false;
            output += static_cast<char>(0x80 + (found - std::begin(upper)));
        }
        if (output.size() > 160) return false;
    }
    return !output.empty();
}

bool TokensOk(const std::string& text) {
    unsigned keys = 0, numbers = 0;
    for (size_t p = 0; (p = text.find('~', p)) != std::string::npos;) {
        const auto end = text.find('~', p + 1);
        if (end == std::string::npos) return false;
        const auto token = text.substr(p + 1, end - p - 1);
        p = end + 1;
        if ((token == "1" || token == "2") && ++numbers > 6) return false;
        if (token == "k") {
            if (++keys > 2 || p >= text.size() || text[p] != '~') return false;
            const auto last = text.find('~', p + 1);
            if (last == std::string::npos) return false;
            const auto action = text.substr(p + 1, last - p - 1);
            const char* allowed[] = {"PED_FIREWEAPON", "PED_SPRINT", "PED_JUMPING", "VEHICLE_ENTER_EXIT",
                                     "VEHICLE_ACCELERATE", "VEHICLE_BRAKE", "PED_DUCK", "PED_LOCK_TARGET"};
            if (std::none_of(std::begin(allowed), std::end(allowed), [&](const char* name) { return action == name; })) return false;
            p = last + 1;
        } else if (token.size() != 1 || std::string("rgbwypnshz12").find(token[0]) == std::string::npos) return false;
    }
    return true;
}

bool IsText(const std::string& owner, uint32_t id) {
    auto found = g_widgets.find(id);
    return found != g_widgets.end() && found->second.owner == owner && found->second.kind == Kind::Text;
}

size_t CountKind(Kind kind) {
    return static_cast<size_t>(std::count_if(g_widgets.begin(), g_widgets.end(), [&](const auto& item) { return item.second.kind == kind; }));
}

bool Referenced(const std::string& owner, uint32_t text) {
    for (const auto& [id, widget] : g_widgets) {
        (void)id;
        if (widget.owner != owner || widget.kind == Kind::Text) continue;
        if (widget.options.text == text) return true;
        if (std::find(widget.options.cells.begin(), widget.options.cells.end(), text) != widget.options.cells.end()) return true;
        if (std::find(widget.options.headers.begin(), widget.options.headers.end(), text) != widget.options.headers.end()) return true;
    }
    return false;
}

bool Validate(const std::string& owner, Kind kind, Options& options, std::string& error) {
    auto fail = [&](const char* why) {
        error = why;
        return false;
    };
    if (owner.empty() || owner.front() == ':') return fail("invalid-owner");
    if (kind == Kind::Text) {
        if (options.name.empty() || options.name.size() > 64 || options.content.empty() || options.content.size() > 640) return fail("invalid-text");
        if (!options.gxt && (!EncodeText(options.content, options.encoded) || !TokensOk(options.encoded))) return fail("unsupported-text-or-token");
        if (options.gxt && (options.content.size() > 7 || options.content.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos))
            return fail("invalid-gxt-key");
        return true;
    }
    if (kind == Kind::Card && (options.card < 1 || options.card > 53)) return fail("invalid-card");
    if (options.text && !IsText(owner, options.text)) return fail("invalid-text-handle");
    const float finite[] = {options.x, options.y, options.width, options.height, options.scaleX, options.scaleY};
    for (float value : finite)
        if (!std::isfinite(value)) return fail("non-finite");
    if (options.x < 0 || options.x > 640 || options.y < 0 || options.y > 448 || options.width <= 0 || options.width > 640 || options.height <= 0 ||
        options.height > 448 || options.scaleX <= 0 || options.scaleX > 4 || options.scaleY <= 0 || options.scaleY > 4)
        return fail("invalid-layout");
    if (options.font < 0 || options.font > 2 || options.alignment < 0 || options.alignment > 2 || options.shadow < 0 || options.shadow > 4 ||
        options.outline < 0 || options.outline > 4)
        return fail("invalid-font");
    if (!std::isfinite(options.value) || !std::isfinite(options.maximum) || options.value < 0 || options.maximum <= 0 || options.value > 2147483647 ||
        options.maximum > 2147483647)
        return fail("invalid-value");
    if (kind == Kind::Clock && (options.value > 5999999 || options.beepSeconds > 60)) return fail("invalid-clock");
    if (kind == Kind::Counter && (options.style < 0 || options.style > 2 || options.color > 14 || (options.style == 1 && options.value > options.maximum)))
        return fail("invalid-counter");
    if ((kind == Kind::DrawText || kind == Kind::Window) && !options.text) return fail("text-required");
    if (kind == Kind::Menu || kind == Kind::Grid) {
        if (options.columns < 1 || options.columns > (kind == Kind::Grid ? 8u : 4u)) return fail("invalid-columns");
        if (kind == Kind::Menu) {
            if (options.cells.empty() || options.cells.size() % options.columns || options.cells.size() / options.columns > 12) return fail("invalid-rows");
            for (auto text : options.cells)
                if (!IsText(owner, text)) return fail("invalid-cell");
            for (auto text : options.headers)
                if (text && !IsText(owner, text)) return fail("invalid-header");
            if (options.headers.size() > options.columns || options.enabled.size() != options.cells.size() / options.columns ||
                std::none_of(options.enabled.begin(), options.enabled.end(), [](char bit) { return bit != 0; }))
                return fail("invalid-enabled-rows");
            if (options.selected < 0 || static_cast<size_t>(options.selected) >= options.enabled.size() || !options.enabled[static_cast<size_t>(options.selected)])
                return fail("invalid-selection");
            if (options.widths.size() > options.columns || options.alignments.size() > options.columns) return fail("invalid-columns");
            float total = 0;
            for (unsigned i = 0; i < options.columns; ++i) {
                const float width = i < options.widths.size() ? options.widths[i] : options.width;
                if (!std::isfinite(width) || width <= 0 || width > 640) return fail("invalid-column-width");
                total += width;
            }
            if (total > 640) return fail("menu-too-wide");
            for (int alignment : options.alignments)
                if (alignment < 0 || alignment > 2) return fail("invalid-alignment");
        } else if (options.selected < 0 || options.selected >= static_cast<int>(options.columns * options.columns)) return fail("invalid-selection");
    }
    return true;
}

bool Capacity(Kind kind, std::string& error) {
    if (g_widgets.size() >= 256 || g_next > 0xFFFFFF) {
        error = "capacity";
        return false;
    }
    if ((kind == Kind::Clock && CountKind(kind) >= 1) || (kind == Kind::Counter && CountKind(kind) >= 4) ||
        ((kind == Kind::Menu || kind == Kind::Grid) && CountKind(Kind::Menu) + CountKind(Kind::Grid) >= 2) ||
        ((kind == Kind::DrawText || kind == Kind::Window || kind == Kind::Rectangle || kind == Kind::Card) &&
         CountKind(Kind::DrawText) + CountKind(Kind::Window) + CountKind(Kind::Rectangle) + CountKind(Kind::Card) >= 96)) {
        error = "capacity";
        return false;
    }
    return true;
}

void ClearMessages(const std::string& owner, uint32_t text) {
    for (auto it = g_messages.begin(); it != g_messages.end();) {
        if (it->second.owner == owner && it->second.text == text) it = g_messages.erase(it);
        else ++it;
    }
}

Widget* Owned(uint32_t id) {
    auto found = g_widgets.find(id);
    if (found == g_widgets.end() || found->second.owner != Owner()) return nullptr;
    return &found->second;
}

int CreateNativeUI(lua_State* L) {
    std::string name, error;
    Kind kind;
    if (lua_gettop(L) != 2 || !StringAt(L, 1, name) || !KindFromName(name, kind)) return Failure(L, "invalid-kind-or-arguments");
    Options options;
    options.alignment = 1;
    if (kind == Kind::Counter) options.color = 0;
    if (!OptionsOf(L, 2, options, kind, error)) return Failure(L, error);
    const std::string owner = Owner();
    if (!Validate(owner, kind, options, error) || !Capacity(kind, error)) return Failure(L, error);
    if (kind == Kind::Text) {
        for (const auto& item : g_widgets)
            if (item.second.owner == owner && item.second.kind == Kind::Text && item.second.options.name == options.name)
                return Failure(L, "duplicate-name");
    }
    const uint32_t id = g_next++;
    g_widgets[id] = Widget{owner, kind, options};
    if (!g_diag) {
        g_diag = true;
        LUA_LOGE("DIAG createNativeUI: the native widget is stored and not drawn");
    }
    lua_pushnumber(L, id);
    return 1;
}

int UpdateNativeUI(lua_State* L) {
    uint32_t id = 0;
    std::string error;
    if (lua_gettop(L) != 2 || !HandleAt(L, 1, id) || !Owned(id)) return Failure(L, "invalid-handle-or-arguments");
    Widget* widget = Owned(id);
    Options options = widget->options;
    const std::string previousName = options.name;
    if (!OptionsOf(L, 2, options, widget->kind, error) || !Validate(widget->owner, widget->kind, options, error)) return Failure(L, error);
    if (widget->kind == Kind::Text && options.name != previousName) return Failure(L, "immutable-name");
    if (widget->kind == Kind::Text) ClearMessages(widget->owner, id);
    widget->options = options;
    lua_pushboolean(L, 1);
    return 1;
}

int DestroyNativeUI(lua_State* L) {
    uint32_t id = 0;
    if (lua_gettop(L) != 1 || !HandleAt(L, 1, id)) return Failure(L, "invalid-handle-or-arguments");
    Widget* widget = Owned(id);
    if (!widget) return Failure(L, "invalid-handle");
    if (widget->kind == Kind::Text && Referenced(widget->owner, id)) return Failure(L, "text-in-use");
    ClearMessages(widget->owner, id);
    g_widgets.erase(id);
    lua_pushboolean(L, 1);
    return 1;
}

int GetNativeUIState(lua_State* L) {
    uint32_t id = 0;
    if (lua_gettop(L) != 1 || !HandleAt(L, 1, id) || !Owned(id)) return Failure(L, "invalid-handle-or-arguments");
    const Widget& widget = *Owned(id);
    lua_newtable(L);
    lua_pushstring(L, KindName(widget.kind));
    lua_setfield(L, -2, "kind");
    lua_pushnumber(L, widget.options.value);
    lua_setfield(L, -2, "value");
    lua_pushboolean(L, widget.options.visible ? 1 : 0);
    lua_setfield(L, -2, "visible");
    lua_pushboolean(L, widget.options.paused ? 1 : 0);
    lua_setfield(L, -2, "paused");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "finished");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "available");
    // No native menu is built, so the selection stays unset. Desktop adds one to that -1.
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "selected");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "accepted");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "cancelled");
    lua_pushnumber(L, -1);
    lua_setfield(L, -2, "color");
    return 1;
}

int ReleaseNativeUI(lua_State* L) {
    if (lua_gettop(L) != 0) return Failure(L, "unexpected-arguments");
    DropOwner(Owner());
    lua_pushboolean(L, 1);
    return 1;
}

int ShowNativeText(lua_State* L) {
    uint32_t id = 0;
    std::string channel;
    double duration = 0;
    const int top = lua_gettop(L);
    if (top < 3 || top > 6 || !HandleAt(L, 1, id) || !StringAt(L, 2, channel) || !NumberAt(L, 3, duration, true) || duration < 1 || duration > 600000)
        return Failure(L, "invalid-message-arguments");
    int style = 1;
    for (int i = 4; i <= top; ++i) {
        double number = 0;
        if (!NumberAt(L, i, number, true) || number < -2147483647 || number > 2147483647) return Failure(L, "invalid-message-number");
        if (i == 4) style = static_cast<int>(number);
    }
    Widget* widget = Owned(id);
    if (!widget || widget->kind != Kind::Text) return Failure(L, "invalid-text-handle");
    if (channel != "objective" && channel != "dialogue" && channel != "help" && channel != "big") return Failure(L, "invalid-channel");
    if (style < 1 || style > 7) return Failure(L, "invalid-message-options");
    for (const auto& [name, message] : g_messages) {
        (void)name;
        if (message.owner != Owner()) return Failure(L, "channel-busy");
    }
    g_messages[channel] = Message{Owner(), id};
    lua_pushboolean(L, 1);
    return 1;
}

int ClearNativeText(lua_State* L) {
    std::string channel;
    if (lua_gettop(L) != 1 || !StringAt(L, 1, channel)) return Failure(L, "invalid-channel");
    if (channel != "all" && channel != "objective" && channel != "dialogue" && channel != "help" && channel != "big")
        return Failure(L, "invalid-channel");
    const std::string owner = Owner();
    if (channel == "all") {
        for (auto it = g_messages.begin(); it != g_messages.end();) {
            if (it->second.owner == owner) it = g_messages.erase(it);
            else ++it;
        }
    } else {
        auto found = g_messages.find(channel);
        if (found != g_messages.end() && found->second.owner == owner) g_messages.erase(found);
    }
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    g_widgets.clear();
    g_messages.clear();
    g_next = 1;
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createNativeUI", CreateNativeUI},
        {"updateNativeUI", UpdateNativeUI},
        {"destroyNativeUI", DestroyNativeUI},
        {"getNativeUIState", GetNativeUIState},
        {"releaseNativeUI", ReleaseNativeUI},
        {"showNativeText", ShowNativeText},
        {"clearNativeText", ClearNativeText},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace

void NativeUiDropResource(const std::string& name, uint16_t netId, uint32_t generation) {
    DropOwner(OwnerKey(name, netId, generation));
}

void InstallNativeUiLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
