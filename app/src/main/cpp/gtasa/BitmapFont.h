#pragma once

#include "RenderWareHooks.h"

#include <cstdint>
#include <memory>
#include <string>

#ifdef __ANDROID__
#include <android/log.h>
#define FONT_LOG_TAG "WorldRPGNeon"
#define FONT_LOGI(...) __android_log_print(ANDROID_LOG_INFO, FONT_LOG_TAG, __VA_ARGS__)
#define FONT_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, FONT_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define FONT_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define FONT_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Game {

/// Self-contained bitmap font renderer using GLESv2.
/// Embeds a minimal 8x12 monospace ASCII font atlas (chars 32-127) as compile-time data.
/// No external asset files required.
class BitmapFont {
public:
    BitmapFont();
    ~BitmapFont();

    BitmapFont(const BitmapFont&) = delete;
    BitmapFont& operator=(const BitmapFont&) = delete;

    /// Create GLESv2 texture from embedded atlas and compile textured quad shader.
    bool initialize();

    /// Check if font renderer is ready
    bool isReady() const;

    /// Render text at screen position using the embedded bitmap font.
    /// @param text ASCII-only string to render
    /// @param x Screen X in normalized coords (0..1)
    /// @param y Screen Y in normalized coords (0..1)
    /// @param size Font size multiplier (1.0 = native 8x12 pixel size scaled to screen)
    /// @param color RGBA color for text
    /// @param screenWidth Current screen width in pixels
    /// @param screenHeight Current screen height in pixels
    void drawText(const char* text, float x, float y, float size,
                  RenderColor color, int screenWidth, int screenHeight);

    /// Measure text width in normalized screen coordinates
    float measureText(const char* text, float size, int screenWidth) const;

    /// Get native glyph dimensions
    static constexpr int glyphWidth() { return 8; }
    static constexpr int glyphHeight() { return 12; }
    static constexpr int atlasCols() { return 16; }
    static constexpr int atlasRows() { return 6; } // 96 chars / 16 cols

    /// Clean up GL resources
    void destroy();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Game