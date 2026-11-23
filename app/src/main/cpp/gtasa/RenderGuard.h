#pragma once

namespace WorldRPG::Game {

// Must run before the game thread constructs the RenderWare command queue.
void InstallRenderQueueCapacity();

// Hooks CRenderer::RenderEverythingBarRoads once. Before each render it removes visible LOD entities whose
// geometry has instance data but no vertex array (the sortLODs SIGSEGV) and logs their model index.
bool InstallRenderGuard();

}  // namespace WorldRPG::Game
