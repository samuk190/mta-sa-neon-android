#pragma once

namespace WorldRPG::Game {

// Hooks CRenderer::RenderEverythingBarRoads once. Before each render it removes visible LOD entities whose
// geometry has instance data but no vertex array (the sortLODs SIGSEGV) and logs their model index.
bool InstallRenderGuard();

}  // namespace WorldRPG::Game
