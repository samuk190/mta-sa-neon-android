#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallMarkerLua(lua_State* L);
void MarkerRelease(uint32_t id);
// Colshape owned by this marker, creating it the way CClientMarker::CreateOfType does.
// 0 when id is not a marker or no resource can own the shape.
uint32_t MarkerColShape(uint32_t id);
}
