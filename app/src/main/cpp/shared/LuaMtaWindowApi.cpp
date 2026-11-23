// Client window state calls, ported from desktop Client/mods/deathmatch/logic/luadefs/CLuaGUIDefs.cpp.
// Each function names the desktop lines it follows.

#include "LuaMtaWindowApi.h"

#include "LuaVMStub.h"
#include "../net/HttpDownloader.h"

#include "lua.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <strings.h>

namespace WorldRPG::JniBridge { void PostGameLine(int kind, const char* text); }

namespace WorldRPG::Script {
namespace {

// Desktop CChat.h:308-309.
constexpr int kDefaultCharacterLimit = 96;
constexpr int kMaxCharacterLimit = 255;

// Written by the UI thread, read by the VM thread.
std::atomic<bool> g_chatInputActive{false};
// VM thread only.
int g_characterLimit = kDefaultCharacterLimit;
bool g_debugViewVisible = false;
// CClientGame::m_bFocused. The UI thread writes g_osFocused; the VM thread copies it.
std::mutex g_focusMutex;
bool g_osFocused = false;
bool g_appliedFocus = false;
bool g_focusDirty = false;

// The phone has no console window and no main menu over the game, so both desktop
// queries (CConsole::IsVisible, CMainMenu::IsVisible) are always false here.
constexpr bool kConsoleVisible = false;
constexpr bool kMenuVisible = false;

// Desktop CTransferBox::IsVisible (CTransferBox.h:33): the TB_VISIBILITY_MTA bit, set by Show()
// while files download and cleared by Hide(). It ignores setTransferBoxVisible. The phone shows
// its box under the same condition (JNI_OnLoad.cpp nativePulse).
bool TransferBoxVisible() {
    const auto transfer = WorldRPG::Net::HttpDownloader::GetTransfer();
    return transfer.active && transfer.total > 0;
}

// Game line 17: the chat input length limit for the Activity (EngineHostActivity.onNativeGameLine).
void PostCharacterLimit() {
    char line[16];
    std::snprintf(line, sizeof(line), "%d", g_characterLimit);
    WorldRPG::JniBridge::PostGameLine(17, line);
}

// Desktop CChat::SetCharacterLimit (CChat.cpp:1154-1162).
void SetCharacterLimit(int limit) {
    if (limit < 0) limit = 0;
    else if (limit > kMaxCharacterLimit) limit = kMaxCharacterLimit;
    if (limit == g_characterLimit) return;
    g_characterLimit = limit;
    PostCharacterLimit();
}

// CLuaGUIDefs.cpp:642-646.
int IsChatBoxInputActive(lua_State* L) {
    lua_pushboolean(L, g_chatInputActive.load() ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:648-652.
int IsConsoleActive(lua_State* L) {
    lua_pushboolean(L, kConsoleVisible ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:654-658.
int IsDebugViewActive(lua_State* L) {
    lua_pushboolean(L, g_debugViewVisible ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:660-679. ReadBool takes a boolean only; anything else logs and returns false.
// The flag is kept, but nothing is drawn: the debug echo (CScriptDebugging::UpdateLogOutput and
// the debugscript level) is not ported, so the view would have no lines.
int SetDebugViewActive(lua_State* L) {
    if (!lua_isboolean(L, 1)) {
        LUA_LOGI("WARNING: Bad argument @ 'setDebugViewActive' [Expected bool at argument 1, got %s]",
                 lua_isnone(L, 1) ? "none" : luaL_typename(L, 1));
        lua_pushboolean(L, 0);
        return 1;
    }
    g_debugViewVisible = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:681-685.
int IsMainMenuActive(lua_State* L) {
    lua_pushboolean(L, kMenuVisible ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:687-694.
int IsMTAWindowActive(lua_State* L) {
    const bool active = g_chatInputActive.load() || kMenuVisible || kConsoleVisible || TransferBoxVisible();
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

// CLuaClientDefs.cpp:72-76. ArgumentParser has no arguments, so extra values are ignored.
// CCore::DebugClear (CCore.cpp:1737-1743) clears the debug view when one exists. The phone
// has no CDebugView, which is the desktop null-view path: nothing is cleared, and the call
// still returns true.
int ClearDebugBox(lua_State* L) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        LUA_LOGI("DIAG clearDebugBox: CDebugView::Clear is not hooked; there is no debug view");
    }
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaClientDefs.cpp:78-81. Reads CClientGame::m_bFocused, not the live foreground check.
int IsMTAWindowFocused(lua_State* L) {
    bool focused = false;
    {
        std::lock_guard<std::mutex> lock(g_focusMutex);
        focused = g_appliedFocus;
    }
    lua_pushboolean(L, focused ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:696-700.
int IsTransferBoxActive(lua_State* L) {
    lua_pushboolean(L, TransferBoxVisible() ? 1 : 0);
    return 1;
}

// CLuaGUIDefs.cpp:4066-4081. ArgumentParser: a bad argument or a limit out of range is a Lua error.
// A number or a numeric string is accepted (CLuaFunctionParser.h:236-237).
int SetChatboxCharacterLimit(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        return luaL_error(L, "Bad argument @ 'setChatboxCharacterLimit' [Expected number at argument 1, got %s]",
                          lua_isnone(L, 1) ? "none" : luaL_typename(L, 1));
    }
    const lua_Number raw = lua_tonumber(L, 1);
    if (raw != raw) {
        return luaL_error(L, "Bad argument @ 'setChatboxCharacterLimit' [Expected number at argument 1, got NaN]");
    }
    // Clamp before the cast so a huge number is still reported as out of range.
    const int limit = raw < -2.0 ? -2 : (raw > 1000000.0 ? 1000000 : static_cast<int>(raw));
    if (limit == -1) {
        // CCore::ResetChatboxCharacterLimit (CCore.cpp:1896-1904).
        SetCharacterLimit(kDefaultCharacterLimit);
        lua_pushboolean(L, 1);
        return 1;
    }
    if (limit < 0 || limit > kMaxCharacterLimit) {
        return luaL_error(L, "Character limit must be %s than, or equal to %d (got: %d)",
                          limit < 0 ? "greater" : "less", limit < 0 ? 0 : kMaxCharacterLimit, limit);
    }
    SetCharacterLimit(limit);
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaGUIDefs.cpp:4083-4086.
int GetChatboxCharacterLimit(lua_State* L) {
    lua_pushinteger(L, g_characterLimit);
    return 1;
}

// One chatbox layout value. Desktop reads these from the client cvars; the phone has no
// settings file, so each value is what the phone chat (MtaChatView) really uses, or the
// desktop default (CClientVariables.cpp:332-356) where the phone has no such option.
struct LayoutValue {
    enum class Kind { Number, Color, Scale, Boolean } kind;
    const char* name;
    double a = 0, b = 0, c = 0, d = 0;
};

// Order of desktop g_chatboxLayoutCVars (CLuaGUIDefs.cpp:15-35).
constexpr LayoutValue kChatboxLayout[] = {
    {LayoutValue::Kind::Number, "chat_font", 2},                           // Chat::Font::BOLD
    {LayoutValue::Kind::Number, "chat_lines", 10},                         // MtaChatView.VISIBLE_MAX_LINES
    {LayoutValue::Kind::Color, "chat_color", 12, 12, 16, 160},             // MtaChatView bar colour
    {LayoutValue::Kind::Color, "chat_text_color", 172, 213, 254, 255},
    {LayoutValue::Kind::Color, "chat_input_color", 0, 0, 0, 0},
    {LayoutValue::Kind::Color, "chat_input_prefix_color", 172, 213, 254, 255},
    {LayoutValue::Kind::Color, "chat_input_text_color", 172, 213, 254, 255},
    {LayoutValue::Kind::Scale, "chat_scale", 1, 1},
    {LayoutValue::Kind::Number, "chat_position_offset_x", 0.0125},
    {LayoutValue::Kind::Number, "chat_position_offset_y", 0.015},
    {LayoutValue::Kind::Number, "chat_position_horizontal", 0},            // Chat::Position::Horizontal::LEFT
    {LayoutValue::Kind::Number, "chat_position_vertical", 0},              // Chat::Position::Vertical::TOP
    {LayoutValue::Kind::Number, "chat_text_alignment", 0},                 // Chat::Text::Align::LEFT
    {LayoutValue::Kind::Number, "chat_width", 1.5},
    {LayoutValue::Kind::Number, "chat_css_style_text", 0},
    {LayoutValue::Kind::Number, "chat_css_style_background", 0},
    {LayoutValue::Kind::Number, "chat_line_life", 14000},                  // MtaChatView.LIFE_MS
    {LayoutValue::Kind::Number, "chat_line_fade_out", 2000},               // MtaChatView.FADE_MS
    {LayoutValue::Kind::Boolean, "chat_use_cegui", 0},
    {LayoutValue::Kind::Number, "text_scale", 1},
    {LayoutValue::Kind::Number, "chat_text_outline", 0},
};

void PushLayoutValue(lua_State* L, const LayoutValue& value) {
    switch (value.kind) {
        case LayoutValue::Kind::Color:
        case LayoutValue::Kind::Scale: {
            const double parts[4] = {value.a, value.b, value.c, value.d};
            const int count = value.kind == LayoutValue::Kind::Color ? 4 : 2;
            lua_createtable(L, count, 0);
            for (int i = 0; i < count; ++i) {
                lua_pushnumber(L, parts[i]);
                lua_rawseti(L, -2, i + 1);
            }
            break;
        }
        case LayoutValue::Kind::Boolean:
            lua_pushboolean(L, value.a != 0 ? 1 : 0);
            break;
        case LayoutValue::Kind::Number:
            lua_pushnumber(L, value.a);
            break;
    }
}

// CLuaGUIDefs.cpp:3631-3748. No argument returns the table of all values; a name returns that
// value; an unknown name or a non-string (nil included) returns false. Only chat_use_cegui is a boolean.
int GetChatboxLayout(lua_State* L) {
    if (lua_isnone(L, 1)) {
        lua_createtable(L, 0, static_cast<int>(sizeof(kChatboxLayout) / sizeof(kChatboxLayout[0])));
        for (const LayoutValue& value : kChatboxLayout) {
            PushLayoutValue(L, value);
            lua_setfield(L, -2, value.name);
        }
        return 1;
    }
    // CScriptArgReader::ReadString accepts a string or a number.
    if (lua_type(L, 1) == LUA_TSTRING || lua_type(L, 1) == LUA_TNUMBER) {
        const char* name = lua_tostring(L, 1);
        for (const LayoutValue& value : kChatboxLayout) {
            if (strcasecmp(value.name, name) == 0) {
                PushLayoutValue(L, value);
                return 1;
            }
        }
    } else {
        LUA_LOGI("WARNING: Bad argument @ 'getChatboxLayout' [Expected string at argument 1, got %s]",
                 luaL_typename(L, 1));
    }
    lua_pushboolean(L, 0);
    return 1;
}

}  // namespace

void MtaWindowSetChatInputActive(bool active) { g_chatInputActive.store(active); }
bool MtaWindowChatInputActive() { return g_chatInputActive.load(); }

void MtaWindowSetFocused(bool focused) {
    std::lock_guard<std::mutex> lock(g_focusMutex);
    g_osFocused = focused;
    g_focusDirty = focused != g_appliedFocus;
}

bool MtaWindowOsFocused() {
    std::lock_guard<std::mutex> lock(g_focusMutex);
    return g_osFocused;
}

bool MtaWindowFocused() {
    std::lock_guard<std::mutex> lock(g_focusMutex);
    return g_appliedFocus;
}

void MtaWindowAdoptFocus() {
    std::lock_guard<std::mutex> lock(g_focusMutex);
    g_appliedFocus = g_osFocused;
    g_focusDirty = false;
}

bool MtaWindowTakeFocusChange(bool& state) {
    std::lock_guard<std::mutex> lock(g_focusMutex);
    if (!g_focusDirty) return false;
    g_appliedFocus = g_osFocused;
    g_focusDirty = false;
    state = g_appliedFocus;
    return true;
}

void MtaWindowResetSession() { SetCharacterLimit(kDefaultCharacterLimit); }

void InstallMtaWindowLua(lua_State* L) {
    static const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"isChatBoxInputActive", IsChatBoxInputActive},
        {"isConsoleActive", IsConsoleActive},
        {"isDebugViewActive", IsDebugViewActive},
        {"setDebugViewActive", SetDebugViewActive},
        {"isMainMenuActive", IsMainMenuActive},
        {"isMTAWindowActive", IsMTAWindowActive},
        {"isMTAWindowFocused", IsMTAWindowFocused},
        {"clearDebugBox", ClearDebugBox},
        {"isTransferBoxActive", IsTransferBoxActive},
        {"setChatboxCharacterLimit", SetChatboxCharacterLimit},
        {"getChatboxCharacterLimit", GetChatboxCharacterLimit},
        {"getChatboxLayout", GetChatboxLayout},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
