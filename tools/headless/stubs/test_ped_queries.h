#pragma once
#include "shared/PedQueries.h"
namespace WorldRPG::Headless {
// Opt-in native snapshot substitute. Normal headless clients have no GTA body.
void SetPedQueryNative(uint32_t id, const PedQueries::Snapshot& snapshot);
void ClearPedQueryNative();
}
