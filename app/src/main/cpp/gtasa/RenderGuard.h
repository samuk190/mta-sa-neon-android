#pragma once

namespace WorldRPG::Game {

// Run before GTA constructs the vehicle-model pool and RenderWare command queue.
void InstallStreamingCapacities();

// Hooks CRenderer::RenderEverythingBarRoads once. Before each render it removes visible LOD entities whose
// geometry has instance data but no vertex array (the sortLODs SIGSEGV) and logs their model index.
bool InstallRenderGuard();

}  // namespace WorldRPG::Game
