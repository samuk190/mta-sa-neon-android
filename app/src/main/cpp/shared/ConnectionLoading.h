#pragma once

// Connection loading screen state.
//
// Phases, status texts and the end rule follow the Neon desktop client:
//   CClientGame::BeginConnectionLoading / UpdateConnectionLoading / EndConnectionLoading /
//   PulseConnectionLoading (Client/mods/deathmatch/logic/CClientGame.cpp:3630-3672)
//   CGraphics::BeginConnectionLoading / UpdateConnectionLoading / UpdateConnectionLoadingProgress
//   (Client/core/Graphics/CGraphics.cpp:2522-2557)
//
// Phone addition (the desktop bar restarts per phase): one bar for the whole load. Each part
// has a weight and only moves on its own real counter:
//   engine   0.30  GTA's CLoadingScreen::m_PercentLoaded until the session leaves the frontend
//   join     0.05  done at SERVER_JOINEDGAME
//   world    0.10  entity index / count of ENTITY_ADD
//   download 0.45  HTTP bytes done / total (done when no files are left)
//   process  0.10  no counter; done when the screen ends
// The bar never moves back. While the current phase has no counter, a moving segment runs
// over the part of the bar that phase still owns, as the desktop does for its whole bar.
//
// Callers on any thread change the state. Flush(), from nativePulse, sends the change to the
// Activity as one game line (kind 19), so no JNI call runs under a packet or world lock.
namespace WorldRPG::ConnectionLoading {

constexpr int kLineKind = 19;

enum class Phase : int { Join = 0, World = 1, Download = 2, Process = 3 };

// Opens the screen (or, when it is already open, only changes the status: the phone opens it
// at connect, before desktop Packet_ServerConnected would).
void Begin(const char* status);
// Status for a phase. Moving to a later phase completes the earlier server phases.
// progress < 0: this phase has no counter right now.
void Update(Phase phase, const char* status, float progress = -1.0f);
// Counter of the current phase (CCore::UpdateDummyProgress with type "%").
void UpdateProgress(float progress);
// GTA load, 0..1. Called from nativePulse with ReadEngineLoad().
void SetEngine(float fraction);
void End();
bool Active();

// GTA's own loading counter: CLoadingScreen::m_PercentLoaded / 100, or -1 when the symbol is
// missing. libGTASA 2.10 arm64: float at 0xc20f04, written by CLoadingScreen::NewChunkLoaded
// (0x520304: 99.999 * m_numChunksLoaded / m_nChunksToLoad, stored through GOT 0x84df70).
float ReadEngineLoad();

// CClientGame::PulseConnectionLoading: once joined, with the game loaded and no initial files
// left, the screen ends after 1500 ms without loading activity.
void Pulse(bool joined, bool gameLoaded, bool transferringInitialFiles);

// Sends the state to the Activity when it changed since the last flush.
// Line: "B|fill|segStart|segEnd|status", "U|..." or "E". fill and seg are 0..1 of the bar;
// segStart < 0 means no moving segment.
void Flush();

} // namespace WorldRPG::ConnectionLoading
