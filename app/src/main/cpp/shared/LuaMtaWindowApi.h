#pragma once

// Client window state calls of desktop CLuaGUIDefs.cpp: chat input, console, debug view,
// main menu, transfer box, chatbox character limit and chatbox layout.

struct lua_State;

namespace WorldRPG::Script {

// Installs isChatBoxInputActive, isConsoleActive, isDebugViewActive, setDebugViewActive,
// isMainMenuActive, isMTAWindowActive, isTransferBoxActive, setChatboxCharacterLimit,
// getChatboxCharacterLimit and getChatboxLayout. Call after the default stubs.
void InstallMtaWindowLua(lua_State* L);

// The Activity reports the chat input bar here (UI thread). Desktop CLocalGUI::IsChatBoxInputEnabled.
void MtaWindowSetChatInputActive(bool active);
bool MtaWindowChatInputActive();

// Desktop CCore::IsFocused (GetForegroundWindow() == GetHookedWindow()). Any thread.
// The game thread copies this into m_bFocused in OnWindowFocusChange.
void MtaWindowSetFocused(bool focused);
bool MtaWindowOsFocused();
// CClientGame::m_bFocused. VM thread. Updated by MtaWindowTakeFocusChange.
bool MtaWindowFocused();
// CClientGame constructor samples IsFocused() and does not fire onClientMTAFocusChange.
void MtaWindowAdoptFocus();
// OnWindowFocusChange: one change since the last call. VM thread. Coalesced to the latest
// value when several reports arrive before the next pump.
bool MtaWindowTakeFocusChange(bool& state);

// Desktop CCore::OnModUnload (CCore.cpp:2964-2965): a new session starts with the default limit.
void MtaWindowResetSession();

}  // namespace WorldRPG::Script
