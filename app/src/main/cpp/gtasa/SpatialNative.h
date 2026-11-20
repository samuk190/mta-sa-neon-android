#pragma once
#include "../shared/SpatialWorld.h"
namespace WorldRPG::SpatialNative {
// Game thread only. Copies model-space CBoundingBox offset/radius, with ABI guards.
void ModelBounds(std::vector<Spatial::Element>& elements);
}
