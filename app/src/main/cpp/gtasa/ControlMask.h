#pragma once

namespace WorldRPG::Game {

// Applies toggleControl / toggleAllControls to the local player's input. Binds the CPad getter slots in
// libGTASA's GOT (the HardwareMouse mechanism, all or nothing) so every caller that reaches a getter through the
// PLT sees shared/ControlToggles masked values. Touch widgets, a hardware keyboard, a gamepad and the hardware
// mouse all end in those getters. Returns false, and masks nothing, when a slot does not match GTA 2.10.
bool InstallControlMask();
bool ControlMaskReady();

}  // namespace WorldRPG::Game
