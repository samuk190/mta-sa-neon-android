#pragma once

namespace WorldRPG::Game {

// Desktop setPlayerHudComponentVisible (CHudSA::SetComponentVisible). The mobile HUD cluster is drawn by
// CWidgetPlayerInfo::Draw (libGTASA 2.10 arm64, 0x37cefc); each component is one or two calls in it, and
// hiding a component replaces those calls with NOP. Showing it again restores the saved instruction.
//
// Supported names: "health", "armour", "breath", "money", "clock", "weapon", "ammo", "wanted", "radar"
// and "all". Returns false for a name this build cannot hide ("area_name", "vehicle_name", "radio",
// "crosshair") or when libGTASA does not match the expected code.
bool SetHudComponentVisible(const char* component, bool visible);
bool IsHudComponentVisible(const char* component);
// Shows every component again (new session).
void ResetHudComponents();

}  // namespace WorldRPG::Game
