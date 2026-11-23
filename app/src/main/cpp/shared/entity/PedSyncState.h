#pragma once

#include <cstdint>

namespace WorldRPG::Sync::PedSyncState {

// CPedSync::AddPed / RemovePed (CPedSync.cpp:167-204).
void BeginHosted(uint32_t id);
void EndHosted(uint32_t id);
bool IsHosted(uint32_t id);

// CPedSync.cpp:408-421 infers the spatial cadence, clamped to [50, 400] ms.
uint64_t SpatialSyncRateMs(uint32_t id);
void Forget(uint32_t id);

} // namespace WorldRPG::Sync::PedSyncState
