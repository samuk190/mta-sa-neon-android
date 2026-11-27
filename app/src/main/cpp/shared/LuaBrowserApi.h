#pragma once

#include <cstdint>
#include <string>

struct lua_State;

namespace WorldRPG::Script {

void InstallBrowserLua(lua_State* L);

// DX texture / element-tree integration (desktop CClientBrowser inherits CClientTexture).
bool BrowserInfo(uint32_t id, std::string& bitmap, unsigned& width, unsigned& height);
bool BrowserAlive(uint32_t id);
bool BrowserDestroy(uint32_t id);
void BrowserDropResource(lua_State* L, const std::string& owner, uint16_t netId, uint32_t generation);
void BrowserReset(lua_State* L);
void BrowserPumpEvents(lua_State* L);
void BrowserInjectGuiClick(uint32_t browserId, int x, int y);

// Android WebView / host event callbacks (thread-safe; drained in BrowserPumpEvents).
void BrowserNoteCreated(uint32_t id);
void BrowserNoteLoadingStart(uint32_t id, const char* url, bool isMainFrame);
void BrowserNoteDocumentReady(uint32_t id, const char* url, const char* title, bool canBack, bool canForward);
void BrowserNoteLoadingFailed(uint32_t id, const char* url, int errorCode, const char* errorDescription);
void BrowserNoteNavigate(uint32_t id, const char* targetUrl, bool isBlocked, bool isMainFrame);
void BrowserNotePopup(uint32_t id, const char* targetUrl, const char* openerUrl);
void BrowserNoteSource(uint32_t id, const char* html);
void BrowserNoteInputFocus(uint32_t id, bool gainedFocus);
void BrowserNoteResourceBlocked(uint32_t id, const char* url, const char* domain, int reason);
void BrowserNoteConsoleMessage(uint32_t id, const char* message);
void BrowserNoteCursorChange(uint32_t id, int cursorId);
void BrowserNoteTooltip(uint32_t id, const char* tooltip);

}  // namespace WorldRPG::Script
