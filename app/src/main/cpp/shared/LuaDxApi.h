#pragma once

// 2D drawing calls of desktop CLuaDrawingDefs.cpp that the Android Canvas overlay can draw:
// lines, circles, primitives, the blend mode, and text size. Each call appends one line to the
// DX frame that MtaDxView draws (see LuaVMStub::pumpDx).

#include <cstdint>
#include <string>

struct lua_State;

namespace WorldRPG::Script {

// Installs the dx* calls of this file (lines, circles, primitives, blend mode, text size,
// textures, fonts, dxGetStatus). A new VM starts in blend mode "blend".
void InstallDxLua(lua_State* L);
// Release all DX state at VM shutdown as well as session replacement.
void DxReset();

// Start of a DX frame: writes the blend mode that is still active from the last frame
// (desktop CGraphics keeps m_ActiveBlendMode until a script changes it).
void DxBeginFrame(std::string& frame);

// Queue a complete draw command (including any font selection/reset). Screen
// calls capture blend state and honor postGUI; active render targets draw in order.
void DxAppendDraw(const std::string& commands, bool postGUI);
// Current viewport, including a selected render target.
void DxDestinationSize(int& width,int& height);
bool DxIsOverwriteBlend();

// Elements made by dxCreateTexture ("texture", desktop CClientTexture) and dxCreateFont
// ("dx-font", CClientDxFont). A texture's pixels stay in its file; the overlay decodes it when a
// draw call names the texture. A font is loaded by the overlay from its TTF file.
// The element type name, or nullptr when id is neither.
const char* DxElementType(uint32_t id);
// destroyElement. False when id is neither.
bool DxElementRelease(uint32_t id);
// The cached image file of a live texture. False when id is not a texture.
bool DxTextureFile(uint32_t id, std::string& file);
// Resolve a texture/SVG or resource image path for the 3D material queue.
bool DxResolveMaterial(lua_State*,int,std::string& file,unsigned& width,unsigned& height);
// A stopped resource loses its textures and fonts. netId or generation 0 matches any.
void DxDropResource(const std::string& owner, uint16_t netId, uint32_t generation);

// The font argument of a text call at this scale: a dx-font element, a standard font name, or
// the default font. pixels is the text size; file is empty for a standard font,
// otherwise an opaque @font:<instance>:<path> source understood by the overlay.
struct DxFont {
    double pixels = 15;
    std::string file;
    bool bold = false;
};
DxFont DxResolveFont(lua_State* L, int index, double scale);

// CClientGuiFont render elements use the same font source/cache bridge, but are
// distinct from dx-font elements. Creation returns 0 for an invalid resource file.
uint32_t DxCreateGuiFont(const std::string& path, uint32_t pointSize);
bool DxGuiFontInfo(uint32_t id, DxFont& renderFont, std::string& name);
uint32_t DxGuiFontByName(const std::string& name);
// Font line spacing in overlay pixels; host builds estimate it from the pixel size.
double DxMeasureFontHeight(const DxFont& font);

// Queues an ordered D command: frees "rt:<id>"/"tx:<id>" or unpins a GUI font
// after any older retained draws. Also used for GUI font destruction/session reset.
void DxFreeOverlayBitmap(uint32_t id);

// End of the script render events: draws go back to the screen if a script left a render target
// selected (desktop restores the default target), so the GUI layer is not drawn into it.
void DxEndRender(std::string& frame);

// Width in pixels of one line of text in this font, from the phone's own text renderer. Falls
// back to 7 px per byte at 15 px when the Activity cannot be asked (host build).
double DxMeasureLine(const std::string& text, const DxFont& font);

}  // namespace WorldRPG::Script
