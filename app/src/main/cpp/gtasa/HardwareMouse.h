#pragma once

namespace WorldRPG::Game {

bool InstallHardwareMouse();
bool HardwareMouseReady();
// X and Y are relative surface pixels. Button bits 0..2 are primary,
// secondary and middle. Wheel is in Android vertical scroll units.
void QueueHardwareMouse(float x, float y, unsigned buttons, float wheel, bool captured);

}  // namespace WorldRPG::Game
