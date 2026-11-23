#pragma once

namespace WorldRPG::Game {

// GTA SA mobile's own options table (MobileSettings::settings, libGTASA 2.10 arm64). 37 entries of
// 0x28 bytes: +0x10 current value, +0x18 min, +0x1c max. The order is setupValues in the library:
//   0 MOB_VS visual quality 0-3      1 MOB_RES resolution 0-100     2 MOB_DD draw distance 0-100
//   5 MOB_SH shadows 0-2             7 CAR_REF car reflections 0-3  11 FEA_SFX sfx volume 0-100
//   12 FEA_MUS music volume 0-100    16 FED_BRI brightness 0-100     19 MOB_HUD hud 0-1
//   24 MOB_SUB subtitles 0-1         29 MOB_VIB vibration 0-1       30 FEM_FRM frame limiter 0-1
// The game reads these values where it uses them, so a write takes effect the next time that code runs.
constexpr int kMobileSettingCount = 37;

// Clamped to the entry's min/max. False when the table is not found or the index is out of range.
bool SetMobileSetting(int index, int value);
// The current value, with the entry's range. False when the table is not found.
bool GetMobileSetting(int index, int& value, int& min, int& max);

}  // namespace WorldRPG::Game
