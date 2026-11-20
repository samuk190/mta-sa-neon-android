#pragma once

// Vector graphics: desktop CLuaVectorGraphicDefs.cpp and CClientVectorGraphic. An svg element
// is a texture whose picture the overlay renders from its SVG document with the AndroidSVG
// library, into the bitmap "tx:<id>".

#include <cstdint>
#include <string>
#include <vector>

struct lua_State;

namespace WorldRPG::Script {

// Installs svgCreate, svgGetDocumentXML, svgSetDocumentXML, svgGetSize, svgSetSize and
// svgSetUpdateCallback, and drops the elements of the previous VM.
void InstallSvgLua(lua_State* L);
// Release callback refs while their state is alive; nullptr after it is gone.
void SvgReset(lua_State* L);

// The overlay bitmap name and the size of a live svg element. False when id is not one.
bool SvgInfo(uint32_t id, std::string& bitmap, unsigned& width, unsigned& height);
// destroyElement. False when id is not an svg element.
bool SvgRelease(lua_State* L, uint32_t id);
// A stopped resource loses its svg elements. netId or generation 0 matches any.
void SvgDropResource(lua_State* L, const std::string& owner, uint16_t netId, uint32_t generation);

// One update callback to call with its svg element (desktop CClientVectorGraphic::OnUpdate).
struct SvgUpdate {
    int ref = 0;
    uint32_t id = 0;
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};
// The svg elements whose picture changed since the last call and that have a callback.
void SvgTakeUpdates(std::vector<SvgUpdate>& out);
// False when the element or its callback is gone since SvgTakeUpdates.
bool SvgCallbackLive(uint32_t id, int ref);

}  // namespace WorldRPG::Script
