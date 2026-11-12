#pragma once

#include <cstdint>
#include <memory>
#include <functional>

#ifdef __ANDROID__
#include <android/log.h>
#define INPUT_LOG_TAG "WorldRPGNeon"
#define INPUT_LOGI(...) __android_log_print(ANDROID_LOG_INFO, INPUT_LOG_TAG, __VA_ARGS__)
#define INPUT_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, INPUT_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define INPUT_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define INPUT_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Game {

/// Virtual keybind IDs mapped from touch input
enum class VirtualKey : uint8_t {
    NONE = 0,
    MOVE_FORWARD,
    MOVE_BACKWARD,
    MOVE_LEFT,
    MOVE_RIGHT,
    SPRINT,
    CROUCH,
    JUMP,
    ATTACK,
    AIM,
    ENTER_VEHICLE,
    WEAPON_NEXT,
    WEAPON_PREV,
    LOOK_UP,
    LOOK_DOWN,
    LOOK_LEFT,
    LOOK_RIGHT,
    HORN,
    BRAKE,
    ACCELERATE,
    STEER_LEFT,
    STEER_RIGHT,
    COUNT
};

/// Touch action types mirroring Android MotionEvent
enum class TouchAction : uint8_t {
    DOWN = 0,
    UP = 1,
    MOVE = 2,
    CANCEL = 3
};

/// Normalized touch event from Java layer
struct TouchEvent {
    float x;           ///< Screen X (0..screenWidth)
    float y;           ///< Screen Y (0..screenHeight)
    float pressure;    ///< Touch pressure (0..1)
    int32_t pointerId; ///< Multi-touch pointer index
    TouchAction action;
};

/// Configuration for virtual analog stick
struct AnalogStickConfig {
    float centerX;     ///< Stick center X (normalized 0..1)
    float centerY;     ///< Stick center Y (normalized 0..1)
    float radius;      ///< Max drag radius (normalized 0..1)
    float deadzone;    ///< Deadzone fraction of radius (0..1)
    float sensitivity; ///< Output multiplier (default 1.0)
};

/// Callback for virtual key state changes
using VirtualKeyCallback = std::function<void(VirtualKey key, bool pressed)>;

/// Intercepts touch input and maps it to virtual keybinds for MTA gameplay.
/// Supports dual analog sticks (left=movement, right=camera) and configurable button zones.
class InputHooks {
public:
    InputHooks();
    ~InputHooks();

    InputHooks(const InputHooks&) = delete;
    InputHooks& operator=(const InputHooks&) = delete;

    /// Initialize with screen dimensions
    bool initialize(int screenWidth, int screenHeight);

    /// Process a raw touch event from GameActivity JNI bridge
    void onTouchEvent(const TouchEvent& event);

    /// Set callback for virtual key state changes
    void setKeyCallback(VirtualKeyCallback callback);

    /// Configure left analog stick (movement)
    void setLeftStickConfig(const AnalogStickConfig& config);

    /// Configure right analog stick (camera look)
    void setRightStickConfig(const AnalogStickConfig& config);

    /// Get current analog stick output values (-1..1)
    void getLeftStick(float& x, float& y) const;
    void getRightStick(float& x, float& y) const;

    /// Check if a virtual key is currently pressed
    bool isKeyPressed(VirtualKey key) const;

    /// Update screen dimensions on rotation/resize
    void setScreenSize(int width, int height);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Game