#pragma once

// Per-ped sync state for PED_STARTSYNC / PED_STOPSYNC / PED_SYNC (stage S4).
//
// Mirrors the reference client's ownership and observer timing:
//   CPedSync::Packet_PedStartSync / Packet_PedStopSync (CPedSync.cpp:211-287)
//   CPedSync::AddPed / RemovePed (CPedSync.cpp:167-204) -> hosted flag
//   spatial receive-interval inference (CPedSync.cpp:408-421)
//   observer target interpolation via PedInterpolator.h
// Engine calls stay in GameEngine.cpp; this module only calls the hooks
// declared in GameEngine.h.

#include <cstdint>

#include "../../gtasa/PedInterpolator.h"

namespace WorldRPG::Sync::PedSyncState {

namespace Interp = WorldRPG::Game::Interp;
using Vec3 = WorldRPG::Game::Interp::Vec3;

// PED_STARTSYNC: this phone becomes the syncer. The pose is set immediately
// (reference Packet_PedStartSync calls SetPosition, not an interpolation).
void BeginHosted(uint32_t id, const Vec3& pos, float heading, const Vec3& velocity);

// PED_STOPSYNC: release the ped and clear the wander task (RemovePed + SetSyncing(false)).
void EndHosted(uint32_t id);

bool IsHosted(uint32_t id);

// PED_SYNC position for an observed ped: start a time-based interpolation to the
// target over delayMs (reference SetTargetPosition). Snaps when no pose is known yet.
void SetObserverPosition(uint32_t id, const Vec3& target, const Vec3& velocity, uint64_t delayMs);

// PED_SYNC heading: start a rotation interpolation over delayMs (reference SetTargetRotation).
void SetObserverRotation(uint32_t id, float heading, uint64_t delayMs);

// Reference CPedSync spatialSyncRate: uses the gap since the last spatial packet
// for this ped, clamped to [50, 400] ms; 400 ms (iPedSync) when unknown.
uint64_t SpatialSyncRateMs(uint32_t id, bool spatialPacket);

// Advances the interpolators to now and, if a pose changed, pushes it with
// Game::ApplyPedTarget. Intended for the per-frame remote-ped loop.
bool SamplePose(uint32_t id);

// Drop all state for a ped that left the world (ForgetRemote / ENTITY_REMOVE).
void Forget(uint32_t id);

// Monotonic milliseconds (reference CClientTime::GetTime).
uint64_t NowMs();

} // namespace WorldRPG::Sync::PedSyncState
