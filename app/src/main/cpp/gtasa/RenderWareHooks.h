#pragma once

#include <cstdint>
#include <memory>
#include <string>

#ifdef __ANDROID__
#include <android/log.h>
#define RW_LOG_TAG "WorldRPGNeon"
#define RW_LOGI(...) __android_log_print(ANDROID_LOG_INFO, RW_LOG_TAG, __VA_ARGS__)
#define RW_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, RW_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define RW_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define RW_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Game {

/// RGBA color for rendering primitives
struct RenderColor {
    uint8_t r, g, b, a;
    static constexpr RenderColor white() { return {255, 255, 255, 255}; }
    static constexpr RenderColor black() { return {0, 0, 0, 255}; }
    static constexpr RenderColor red() { return {255, 0, 0, 255}; }
    static constexpr RenderColor green() { return {0, 255, 0, 255}; }
    static constexpr RenderColor transparent() { return {0, 0, 0, 0}; }
};

/// Hooks into GTA SA Mobile's RenderWare pipeline for custom HUD/overlay rendering.
/// Uses GLESv2 shaders instead of D3D9 since mobile RenderWare uses OpenGL ES.
class RenderWareHooks {
public:
    RenderWareHooks();
    ~RenderWareHooks();

    RenderWareHooks(const RenderWareHooks&) = delete;
    RenderWareHooks& operator=(const RenderWareHooks&) = delete;

    /// Initialize GLESv2 overlay rendering context.
    /// Must be called after EGL surface is created by GameActivity.
    bool initialize();

    /// Check if rendering system is ready
    bool isReady() const;

    /// Called at the start of each frame (hooks RwCameraBeginScene)
    void beginFrame();

    /// Called at the end of each frame (hooks RwCameraEndScene)
    /// This is where custom overlays are drawn on top of the game.
    void endFrame();

    // --- Drawing primitives (GLESv2-based) ---

    /// Draw a filled rectangle in screen coordinates (0..1 normalized)
    void drawRect(float x, float y, float width, float height, RenderColor color);

    /// Draw text at screen position using bitmap font
    /// Text must be ASCII-only per project rules
    void drawText(const char* text, float x, float y, float size, RenderColor color);

    /// Draw a textured quad (for HUD elements, icons, etc.)
    void drawTexturedRect(float x, float y, float w, float h, uint32_t textureId);

    // --- Texture management ---

    /// Load a texture from GTA SA mobile texdb format
    /// Returns texture ID or 0 on failure
    uint32_t loadTextureFromTexDb(const char* txdName, const char* textureName);

    /// Delete a loaded texture
    void deleteTexture(uint32_t textureId);

    // --- Screen metrics ---

    /// Set the current screen dimensions (called on surface change)
    void setScreenSize(int width, int height);

    int getScreenWidth() const;
    int getScreenHeight() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Game