#pragma once
#include "SpatialWorld.h"
struct lua_State;
namespace WorldRPG::Script {
void InstallSpatialLua(lua_State* L);
bool SpatialShapeAlive(uint32_t id);
bool CopySpatialShape(uint32_t id, Spatial::Element& out);
bool SpatialSetArea(uint32_t id, unsigned value, bool dimension);
void SpatialRelease(uint32_t id);
void SpatialDropResource(const std::string& owner,uint16_t netId,uint32_t generation);
void PulseSpatial();
void ResetSpatial();
// CClientMarker::CreateOfType collision. kind is the marker type 0..4.
// The shape cannot be destroyed by script. 0 when no resource can own it.
uint32_t RetainMarkerColShape(uint32_t marker, int kind, float x, float y, float z, float size);
void ReleaseMarkerColShape(uint32_t marker);
// Cached collider list (CClientEntity::CollisionExists), after publishing current positions.
bool ElementInsideShape(uint32_t entity, uint32_t shape);
}
