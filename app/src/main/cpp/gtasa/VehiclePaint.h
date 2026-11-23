#pragma once
#include "../shared/VehicleColors.h"

namespace WorldRPG::Game::VehiclePaint {
void Install(void* handle);
void Track(void* clump, uint32_t id, const VehicleColors& colors);
void Forget(void* clump);
}
