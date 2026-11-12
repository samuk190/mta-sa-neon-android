#pragma once

#include "RenderWareHooks.h"
#include "../shared/ElementManager.h"

#include <memory>
#include <string>
#include <array>

#ifdef __ANDROID__
#include <android/log.h>
#define HUD_LOG_TAG "WorldRPGNeon"
#define HUD_LOGI(...) __android_log_print(ANDROID_LOG_INFO, HUD_LOG_TAG, __VA_ARGS__)
#define HUD_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, HUD_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define HUD_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define HUD_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Game {

/// Administrative HUD element positioning parameters (per AGENTS.md directive 10)
struct HUDElementParams {
 float x = 0.0f;       ///< Horizontal position (normalized 0..1 or pixels)
 float y = 0.0f;       ///< Vertical position (normalized 0..1 or pixels)
 float width = 0.0f;   ///< Width / length (tc)
 float height = 0.0f;  ///< Height / thickness (ta)
 bool visible = true;
};

/// Renders multiplayer HUD overlays using GLESv2 on top of GTA SA Mobile's RenderWare output.
/// Supports health/armor bars, nametags, speedometer, chat overlay, and admin-adjustable positioning.
class HUDRenderer {
public:
 HUDRenderer();
 ~HUDRenderer();

 HUDRenderer(const HUDRenderer&) = delete;
 HUDRenderer& operator=(const HUDRenderer&) = delete;

 /// Initialize with rendering backend and screen dimensions
 bool initialize(RenderWareHooks& renderer, int screenWidth, int screenHeight);

 /// Per-frame HUD draw call (invoked from RenderWareHooks::endFrame)
 void draw(const Sync::ElementManager& elements, Sync::ElementID localPlayerId);

 // --- Admin-adjustable HUD element parameters (AGENTS.md directive 10) ---

 /// Set positioning/dimension for a named HUD element
 void setElementParams(const char* elementName, const HUDElementParams& params);

 /// Get current parameters for a named HUD element
 HUDElementParams getElementParams(const char* elementName) const;

 /// List all configurable HUD element names
 void listElements(std::function<void(const char* name, const HUDElementParams& params)> callback) const;

 // --- Chat overlay ---

 /// Add a chat message to the overlay buffer (ASCII-only)
 void addChatMessage(const char* message, uint8_t color = 255);

 /// Set maximum visible chat lines
 void setMaxChatLines(int maxLines);

 // --- Screen resize handling ---

 void onScreenResize(int width, int height);

private:
 struct Impl;
 std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Game