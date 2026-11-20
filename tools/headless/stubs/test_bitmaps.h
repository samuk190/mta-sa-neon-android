#pragma once
#include <cstddef>
namespace WorldRPG::JniBridge {
// Explicit opt-in for Lua-to-bitmap integration tests. Normal host executables
// still report that the Android bitmap bridge is unavailable.
void EnableTestBitmaps(bool);
size_t TestBitmapCount();
}
