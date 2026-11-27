// Native UI. Desktop CLuaNativeUIDefs.cpp and CNativeUISA::Show/Clear.
// Renders all NativeUI widget kinds and timed showNativeText channels into the DX overlay.

#include "LuaNativeUiApi.h"

#include "LuaVMStub.h"

#include "lua.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
    int64_t expiresAtMs = 0;
    int style = 1;
    int number1 = -1;
    int number2 = -1;
};

struct Widget {
    std::string owner;
    Kind kind = Kind::Text;
    Options options;
    int64_t lastTickMs = 0;
    bool finished = false;
};

std::unordered_map<uint32_t, Widget> g_widgets;
uint32_t g_next = 1;
std::unordered_map<std::string, Message> g_messages;
int64_t g_testClockMs = -1;

int64_t NowMs() {
    if (g_testClockMs >= 0) return g_testClockMs;
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint32_t RgbaToArgb(uint32_t rgba) {
    return ((rgba & 0xFFu) << 24) | ((rgba >> 8) & 0x00FFFFFFu);
}

uint32_t HudColorArgb(uint32_t idx) {
    static const uint32_t kHudColors[15] = {
        0xFFE1E1E1u,  // 0: white/default
        0xFFB4191Du,  // 1: red
        0xFF36682Cu,  // 2: green
        0xFF323C7Fu,  // 3: blue
        0xFFE1E1E1u,  // 4: white
        0xFFE2C063u,  // 5: yellow
        0xFF906210u,  // 6: orange
        0xFFACCBF1u,  // 7: light blue
        0xFF969696u,  // 8: grey
        0xFF464646u,  // 9: dark grey
        0xFF000000u,  // 10: black
        0xFFF09696u,  // 11: pink
        0xFF6E3C7Fu,  // 12: purple
        0xFF74B064u,  // 13: light green
        0xFF712224u,  // 14: dark red
    };
    return kHudColors[idx < 15 ? idx : 0];
}

void SanitizeLineText(std::string& s) {
    for (char& ch : s) {
        if (ch == '\r' || ch == '\n') ch = ' ';
    }
}

std::string ResolveTextContent(uint32_t textId, int number1, int number2) {
    auto it = g_widgets.find(textId);
    if (it == g_widgets.end() || it->second.kind != Kind::Text) return {};
    const std::string& raw = it->second.options.content;
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '~') {
            const size_t end = raw.find('~', i + 1);
            if (end != std::string::npos) {
                const std::string tok = raw.substr(i + 1, end - i - 1);
                if (tok == "n") out.push_back(' ');
                else if (tok == "1" && number1 >= 0) out += std::to_string(number1);
                else if (tok == "2" && number2 >= 0) out += std::to_string(number2);
                else if (tok == "k") {
                    const size_t nextEnd = (end + 1 < raw.size() && raw[end + 1] == '~') ? raw.find('~', end + 2) : std::string::npos;
                    if (nextEnd != std::string::npos) {
                        out += "[" + raw.substr(end + 2, nextEnd - (end + 2)) + "]";
                        i = nextEnd;
                        continue;
                    }
                }
                i = end;
                continue;
            }
        }
        out.push_back(raw[i]);
    }
    SanitizeLineText(out);
    return out;
}

void EmitRect(std::string& out, float x, float y, float w, float h, uint32_t argb) {
    if (w <= 0.f || h <= 0.f || (argb >> 24) == 0) return;
    char line[128];
    std::snprintf(line, sizeof(line), "R %.1f %.1f %.1f %.1f %u\n", x, y, w, h, static_cast<unsigned>(argb));
    out += line;
}

void EmitText(std::string& out, float left, float top, float right, float bottom, float scale, uint32_t argb, const std::string& text) {
    if (text.empty() || right <= left || bottom <= top || (argb >> 24) == 0) return;
    char hdr[160];
    std::snprintf(hdr, sizeof(hdr), "T %.1f %.1f %.1f %.1f %.2f %u 0 ",
                  left, top, right, bottom, std::clamp(scale, 0.5f, 6.0f), static_cast<unsigned>(argb));
    out += hdr;
    out += text;
    out.push_back('\n');
}

void AdvanceWidget(Widget& widget, int64_t nowMs) {
    if (widget.kind != Kind::Clock) return;
    if (widget.lastTickMs <= 0) {
        widget.lastTickMs = nowMs;
        return;
    }
    const int64_t delta = std::max<int64_t>(0, nowMs - widget.lastTickMs);
    widget.lastTickMs = nowMs;
    if (widget.options.paused || delta == 0) return;
    if (widget.options.countdown) {
        widget.options.value = std::max(0.0, widget.options.value - static_cast<double>(delta));
        if (widget.options.value <= 0.0) widget.finished = true;
    } else {
        widget.options.value = std::min(5999999.0, widget.options.value + static_cast<double>(delta));
    }
}

void ExpireMessages(int64_t nowMs) {
    for (auto it = g_messages.begin(); it != g_messages.end();) {
        if (it->second.expiresAtMs > 0 && nowMs >= it->second.expiresAtMs) it = g_messages.erase(it);
        else ++it;
    }
}

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
    g_widgets[id] = Widget{owner, kind, options, NowMs(), false};
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
    widget->lastTickMs = NowMs();
    if (widget->kind == Kind::Clock && widget->options.value > 0.0) widget->finished = false;
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
    Widget& widget = *Owned(id);
    AdvanceWidget(widget, NowMs());
    lua_newtable(L);
    lua_pushstring(L, KindName(widget.kind));
    lua_setfield(L, -2, "kind");
    lua_pushnumber(L, widget.options.value);
    lua_setfield(L, -2, "value");
    lua_pushboolean(L, widget.options.visible ? 1 : 0);
    lua_setfield(L, -2, "visible");
    lua_pushboolean(L, widget.options.paused ? 1 : 0);
    lua_setfield(L, -2, "paused");
    lua_pushboolean(L, widget.finished ? 1 : 0);
    lua_setfield(L, -2, "finished");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "available");
    const int sel = (widget.kind == Kind::Menu || widget.kind == Kind::Grid) ? (widget.options.selected + 1) : 0;
    lua_pushnumber(L, sel);
    lua_setfield(L, -2, "selected");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "accepted");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "cancelled");
    lua_pushnumber(L, widget.kind == Kind::Counter ? static_cast<lua_Number>(widget.options.color) : -1);
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
    int number1 = -1;
    int number2 = -1;
    for (int i = 4; i <= top; ++i) {
        double number = 0;
        if (!NumberAt(L, i, number, true) || number < -2147483647 || number > 2147483647) return Failure(L, "invalid-message-number");
        if (i == 4) style = static_cast<int>(number);
        else if (i == 5) number1 = static_cast<int>(number);
        else if (i == 6) number2 = static_cast<int>(number);
    }
    Widget* widget = Owned(id);
    if (!widget || widget->kind != Kind::Text) return Failure(L, "invalid-text-handle");
    if (channel != "objective" && channel != "dialogue" && channel != "help" && channel != "big") return Failure(L, "invalid-channel");
    if (style < 1 || style > 7) return Failure(L, "invalid-message-options");
    const int64_t now = NowMs();
    ExpireMessages(now);
    for (const auto& [name, message] : g_messages) {
        (void)name;
        if (message.owner != Owner()) return Failure(L, "channel-busy");
    }
    g_messages[channel] = Message{Owner(), id, now + static_cast<int64_t>(duration), style, number1, number2};
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
    g_testClockMs = -1;
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

void NativeUiSetTestClockMs(int64_t nowMs) { g_testClockMs = nowMs; }

void NativeUiAppendDraws(std::string& out, int screenW, int screenH) {
    if (screenW <= 0 || screenH <= 0) return;
    const int64_t now = NowMs();
    ExpireMessages(now);
    const float sx = static_cast<float>(screenW) / 640.f;
    const float sy = static_cast<float>(screenH) / 448.f;

    std::vector<uint32_t> ids;
    ids.reserve(g_widgets.size());
    for (const auto& kv : g_widgets) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());

    for (uint32_t id : ids) {
        auto it = g_widgets.find(id);
        if (it == g_widgets.end()) continue;
        Widget& w = it->second;
        AdvanceWidget(w, now);
        if (w.kind == Kind::Text || !w.options.visible) continue;
        const Options& o = w.options;
        const float px = o.x * sx;
        const float py = o.y * sy;
        const float pw = o.width * sx;
        const float ph = o.height * sy;
        const float textScale = std::max(0.75f, o.scaleY * sy * 0.65f);
        const uint32_t fg = RgbaToArgb(o.color);
        const uint32_t bg = RgbaToArgb(o.background);
        const uint32_t drop = RgbaToArgb(o.dropColor);

        switch (w.kind) {
            case Kind::Rectangle:
                EmitRect(out, px, py, pw, ph, fg);
                break;
            case Kind::Window: {
                EmitRect(out, px, py, pw, ph, bg);
                const std::string body = ResolveTextContent(o.text, o.number1, o.number2);
                const float pad = 6.f * sx;
                EmitText(out, px + pad, py + pad, px + pw - pad, py + ph - pad, textScale, fg, body);
                break;
            }
            case Kind::DrawText: {
                const std::string text = ResolveTextContent(o.text, o.number1, o.number2);
                const float right = std::min(static_cast<float>(screenW), px + std::max(pw, 180.f * sx));
                const float bottom = std::min(static_cast<float>(screenH), py + std::max(ph, 32.f * sy));
                if (o.shadow > 0) {
                    const float d = static_cast<float>(o.shadow);
                    EmitText(out, px + d, py + d, right + d, bottom + d, textScale, drop, text);
                }
                EmitText(out, px, py, right, bottom, textScale, fg, text);
                break;
            }
            case Kind::Clock: {
                const int totalSec = static_cast<int>(std::max(0.0, o.value) / 1000.0);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%02d:%02d", totalSec / 60, totalSec % 60);
                const std::string label = o.text ? (ResolveTextContent(o.text, o.number1, o.number2) + " " + buf) : std::string(buf);
                EmitRect(out, px, py, std::max(pw, 120.f * sx), 26.f * sy, bg);
                EmitText(out, px + 6.f * sx, py + 4.f * sy, px + std::max(pw, 120.f * sx), py + 26.f * sy, textScale, fg, label);
                break;
            }
            case Kind::Counter: {
                const uint32_t barArgb = HudColorArgb(o.color);
                const std::string label = o.text ? ResolveTextContent(o.text, o.number1, o.number2) : std::string();
                const float boxW = std::max(pw, 140.f * sx);
                const float boxH = 24.f * sy;
                EmitRect(out, px, py, boxW, boxH, bg);
                if (o.style == 1) {
                    const float ratio = static_cast<float>(std::clamp(o.value / std::max(0.01, o.maximum), 0.0, 1.0));
                    EmitRect(out, px + 4.f * sx, py + 6.f * sy, (boxW - 8.f * sx) * ratio, boxH - 12.f * sy, barArgb);
                    if (!label.empty()) EmitText(out, px + 4.f * sx, py - 18.f * sy, px + boxW, py, textScale * 0.9f, 0xFFFFFFFFu, label);
                } else {
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "%.0f", o.value);
                    const std::string text = label.empty() ? std::string(buf) : (label + ": " + buf);
                    EmitText(out, px + 6.f * sx, py + 3.f * sy, px + boxW, py + boxH, textScale, barArgb, text);
                }
                break;
            }
            case Kind::Menu:
            case Kind::Grid: {
                const unsigned cols = std::max(1u, o.columns);
                const unsigned rows = w.kind == Kind::Menu ? static_cast<unsigned>(o.cells.size() / cols) : cols;
                const float rowH = 22.f * sy;
                const float headerH = (o.text || !o.headers.empty()) ? 24.f * sy : 0.f;
                const float totalH = std::max(ph, headerH + static_cast<float>(std::max(1u, rows)) * rowH + 8.f * sy);
                EmitRect(out, px, py, pw, totalH, bg);
                if (o.text) {
                    EmitText(out, px + 6.f * sx, py + 3.f * sy, px + pw - 6.f * sx, py + headerH, textScale, fg,
                             ResolveTextContent(o.text, o.number1, o.number2));
                }
                const float colW = pw / static_cast<float>(cols);
                for (unsigned r = 0; r < rows; ++r) {
                    const float ry = py + headerH + static_cast<float>(r) * rowH;
                    if (w.kind == Kind::Menu && static_cast<int>(r) == o.selected) {
                        EmitRect(out, px + 2.f * sx, ry, pw - 4.f * sx, rowH - 2.f * sy, 0x6644AAFFu);
                    }
                    for (unsigned c = 0; c < cols; ++c) {
                        const size_t idx = static_cast<size_t>(r) * cols + c;
                        if (w.kind == Kind::Grid && static_cast<int>(idx) == o.selected) {
                            EmitRect(out, px + static_cast<float>(c) * colW + 2.f * sx, ry, colW - 4.f * sx, rowH - 2.f * sy, 0x6644AAFFu);
                        }
                        if (idx < o.cells.size()) {
                            const std::string cellText = ResolveTextContent(o.cells[idx], o.number1, o.number2);
                            EmitText(out, px + static_cast<float>(c) * colW + 4.f * sx, ry + 2.f * sy,
                                     px + static_cast<float>(c + 1) * colW - 4.f * sx, ry + rowH, textScale * 0.9f, fg, cellText);
                        }
                    }
                }
                break;
            }
            case Kind::Card: {
                static const char* kRanks[13] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
                static const char* kSuits[4] = {"C", "D", "H", "S"};
                EmitRect(out, px, py, pw, ph, o.card == 53 ? 0xFF1F3B73u : 0xFFF7F7F7u);
                if (o.card >= 1 && o.card <= 52) {
                    const unsigned zero = o.card - 1;
                    const std::string label = std::string(kRanks[zero % 13]) + kSuits[zero / 13];
                    const uint32_t suitColor = (zero / 13 == 1 || zero / 13 == 2) ? 0xFFC02020u : 0xFF101010u;
                    EmitText(out, px + 6.f * sx, py + 6.f * sy, px + pw - 4.f * sx, py + ph - 4.f * sy, textScale * 1.1f, suitColor, label);
                }
                break;
            }
            case Kind::Text:
                break;
        }
    }

    for (const auto& [channel, msg] : g_messages) {
        const std::string text = ResolveTextContent(msg.text, msg.number1, msg.number2);
        if (text.empty()) continue;
        if (channel == "help") {
            EmitRect(out, 24.f * sx, 24.f * sy, 260.f * sx, 56.f * sy, 0xCC000000u);
            EmitText(out, 32.f * sx, 30.f * sy, 276.f * sx, 74.f * sy, 1.0f, 0xFFFFFFFFu, text);
        } else if (channel == "big") {
            EmitText(out, 80.f * sx, 160.f * sy, 560.f * sx, 240.f * sy, 2.2f, 0xFFE2C063u, text);
        } else if (channel == "dialogue") {
            EmitRect(out, 60.f * sx, 372.f * sy, 520.f * sx, 44.f * sy, 0xA8000000u);
            EmitText(out, 72.f * sx, 378.f * sy, 568.f * sx, 412.f * sy, 1.1f, 0xFFFFFFFFu, text);
        } else {  // objective
            EmitRect(out, 80.f * sx, 336.f * sy, 480.f * sx, 34.f * sy, 0x99000000u);
            EmitText(out, 90.f * sx, 342.f * sy, 550.f * sx, 368.f * sy, 1.1f, 0xFFE2C063u, text);
        }
    }
}

void NativeUiDropResource(const std::string& name, uint16_t netId, uint32_t generation) {
    DropOwner(OwnerKey(name, netId, generation));
}

void NativeUiNoteEvent(uint32_t resourceRoot, uint32_t elementId, const char* eventType, int itemIndex) {
    lua_State* L = ActiveVmState();
    if (!L) return;
    const uint32_t src = resourceRoot ? resourceRoot : ClientRootElement();
    lua_pushstring(L, eventType ? eventType : "click");
    lua_pushinteger(L, static_cast<lua_Integer>(elementId));
    lua_pushinteger(L, itemIndex);
    ActiveVmFireElementEvent(L, "onClientNativeUI", src, 3);
}

void InstallNativeUiLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
