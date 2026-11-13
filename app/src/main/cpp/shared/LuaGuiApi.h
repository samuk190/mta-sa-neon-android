#pragma once

// GUI elements and DX font metrics for client scripts. The GUI side follows desktop
// CLuaGUIDefs.cpp and CStaticFunctionDefinitions::GUICreate*/GUISet*. Elements are
// tables {id = <id>, element = true} like every other element (see LuaApiExtra.h).
// Visible elements are drawn once per frame into the DX command list; nothing is kept
// between frames, so a window that is hidden or destroyed stops being drawn.

#include <cstdint>
#include <string>

struct lua_State;

namespace WorldRPG::Script {

// Installs guiCreate*, guiSet*/guiGet*, showCursor, destroyElement. Call before the
// default stubs of LuaApiExtra so these bindings are not replaced by defaults.
void InstallGuiBindings(lua_State* L);

// True when id names a GUI element created by guiCreate*.
bool GuiIsElement(uint32_t id);
// Desktop getElementType name ("gui-window", "gui-label", "gui-button"), or nullptr.
const char* GuiElementType(uint32_t id);
// Destroys a GUI element and its children. False when id is not a GUI element.
bool GuiDestroy(uint32_t id);
// Destroys the GUI elements a resource created (desktop: the resource GUI entity on stop).
void GuiDropResource(const std::string& owner);
// Drops every GUI element (new VM or session).
void GuiReset();
// Appends the R/T draw lines of the visible elements (Android MtaDxView format).
void GuiAppendDraws(std::string& out);

// Desktop font height in pixels at scale 1, relative to the default 15 px font, for
// dxDrawText and dxGetFontHeight. Unknown or non-string names are the default font.
double DxFontScale(const char* font);

} // namespace WorldRPG::Script
