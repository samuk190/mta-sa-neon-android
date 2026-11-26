#pragma once

namespace WorldRPG::Game {

// Desktop setPlayerHudComponentVisible (CHudSA::SetComponentVisible). The mobile HUD cluster is drawn by
// CWidgetPlayerInfo::Draw (libGTASA 2.10 arm64, 0x37cefc); each component is one or two calls in it, and
// hiding a component replaces those calls with NOP. Showing it again restores the saved instruction.
//
// Supported names: "health", "armour", "breath", "money", "clock", "weapon", "ammo", "wanted", "radar", "crosshair"
// and "all". Returns false for unsupported components or if the libGTASA code does not match.
bool SetHudComponentVisible(const char* component, bool visible);
bool IsHudComponentVisible(const char* component);
bool GetMobileCrosshairPosition(float& x, float& y);
// Shows every component again (new session).
void ResetHudComponents();

}  // namespace WorldRPG::Game
