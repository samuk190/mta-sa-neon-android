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
// netId 0 drops every element of that resource name. A non-zero netId drops only
// the elements created by that instance, so a restarted resource keeps its own.
void GuiDropResource(const std::string& owner, uint16_t netId, uint32_t generation);
// The resource GUI parent (CResource.cpp 87-88). One full-screen guiroot per
// resource instance, accepted by guiCreate* as a parent. The same id is returned
// every call for that instance.
uint32_t GuiEnsureResourceRoot(const std::string& owner, uint16_t netId, uint32_t generation);
// Drops every GUI element (new VM or session).
void GuiReset();
// CClientGuiFont::Unlink restores default-normal on every widget using that font.
void GuiDetachFont(uint32_t id);
// True while any resource still holds the cursor (showCursor). One resource's
// showCursor(false) leaves it showing if another resource still holds it.
// The phone then routes touches to onClientClick.
bool GuiCursorShown();
// Appends the R/T draw lines of the visible elements (Android MtaDxView format).
void GuiAppendDraws(std::string& out);

// Pixels format of a pixel string as dxGetPixelsFormat names it ("plain", "png", "jpeg", "dds"),
// or nullptr (desktop CPixelsManager::GetPixelsFormat).
const char* DxPixelsFormatName(const char* data, size_t size);
// Width and height of plain pixels, from the 4-byte tail. False when the size does not match.
bool DxPlainSize(const char* data, size_t size, uint32_t& width, uint32_t& height);

// Topmost visible, enabled GUI element under this screen point, or 0.
uint32_t GuiPick(double x, double y);
// True when id is a visible, enabled gui-edit or gui-memo (accepts keyboard input).
bool GuiIsEdit(uint32_t id);
// The focused edit/memo when it is still visible and enabled, otherwise 0.
uint32_t GuiFocusedEdit();
// The focused input accepts line breaks (gui-memo).
bool GuiFocusedMemo();
// Activates id, or clears the focus when id is 0. A change of gui-edit focus
// notifies the hook (onClientGUIFocus / onClientGUIBlur).
void GuiSetFocused(uint32_t id);
// Inserts UTF-8 text at the focused edit's Unicode caret. False when no edit has focus
// or the text is already at the 4096-byte cap.
bool GuiEditAppend(const char* bytes, size_t length);
// Deletes the Unicode character before the caret; read-only edits are unchanged.
bool GuiEditErase();
// True in INPUTMODE_NO_BINDS (guiSetInputEnabled(true) or guiSetInputMode("no_binds")).
// The phone then shows the soft keyboard and delivers keystrokes as onClientCharacter
// and onClientKey, which is how a DX edit (the login boxes) receives text.
bool GuiKeyCapture();
// onClientGUIFocus / onClientGUIBlur. focused is true for focus, false for blur.
using GuiFocusHook = void (*)(uint32_t id, bool focused);
void GuiSetFocusHook(GuiFocusHook hook);

// Desktop font height in pixels at scale 1, relative to the default 15 px font, for
// dxDrawText and dxGetFontHeight. Unknown or non-string names are the default font.
double DxFontScale(const char* font);

} // namespace WorldRPG::Script

// Registers the GUI and plain-pixel bindings added after InstallGuiBindings.
// InstallGuiBindings calls this. A later translation unit may also call it (weak
// reference); both paths install the same functions.
extern "C" void InstallGuiLua(lua_State* L);
