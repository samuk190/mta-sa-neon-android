#include "InputHooks.h"

#include <cmath>
#include <array>
#include <algorithm>

namespace WorldRPG::Game {

struct InputHooks::Impl {
    bool initialized = false;
    int screenWidth = 0;
    int screenHeight = 0;

    // Virtual key state
    std::array<bool, static_cast<size_t>(VirtualKey::COUNT)> keyState{};
    VirtualKeyCallback keyCallback;

    // Analog stick configs
    AnalogStickConfig leftStick{0.15f, 0.75f, 0.12f, 0.1f, 1.0f};
    AnalogStickConfig rightStick{0.85f, 0.75f, 0.12f, 0.1f, 1.0f};

    // Current stick outputs (-1..1)
    float leftStickX = 0.0f;
    float leftStickY = 0.0f;
    float rightStickX = 0.0f;
    float rightStickY = 0.0f;

    // Active pointer tracking for sticks
    int32_t leftStickPointer = -1;
    int32_t rightStickPointer = -1;
    float leftStickOriginX = 0.0f;
    float leftStickOriginY = 0.0f;
    float rightStickOriginX = 0.0f;
    float rightStickOriginY = 0.0f;

    void setKey(VirtualKey key, bool pressed) {
        auto idx = static_cast<size_t>(key);
        if (idx >= keyState.size()) return;
        if (keyState[idx] == pressed) return;
        keyState[idx] = pressed;
        if (keyCallback) keyCallback(key, pressed);
    }

    float clamp(float v, float lo, float hi) const {
        return std::max(lo, std::min(hi, v));
    }

    void updateStickOutput(const AnalogStickConfig& cfg,
                           float originX, float originY,
                           float currentX, float currentY,
                           float& outX, float& outY)
    {
        float dx = currentX - originX;
        float dy = currentY - originY;
        float dist = std::sqrt(dx * dx + dy * dy);
        float maxDist = cfg.radius * static_cast<float>(screenWidth);

        if (dist < cfg.deadzone * maxDist || maxDist <= 0.0f) {
            outX = 0.0f;
            outY = 0.0f;
            return;
        }

        float normDist = std::min(dist / maxDist, 1.0f);
        float angle = std::atan2(dy, dx);
        outX = clamp(std::cos(angle) * normDist * cfg.sensitivity, -1.0f, 1.0f);
        outY = clamp(std::sin(angle) * normDist * cfg.sensitivity, -1.0f, 1.0f);
    }

    bool isInStickZone(float nx, float ny, const AnalogStickConfig& cfg) const {
        float dx = nx - cfg.centerX;
        float dy = ny - cfg.centerY;
        return (dx * dx + dy * dy) <= (cfg.radius * cfg.radius * 4.0f);
    }
};

InputHooks::InputHooks() : pImpl_(std::make_unique<Impl>()) {}
InputHooks::~InputHooks() = default;

bool InputHooks::initialize(int screenWidth, int screenHeight) {
    if (screenWidth <= 0 || screenHeight <= 0) return false;
    pImpl_->screenWidth = screenWidth;
    pImpl_->screenHeight = screenHeight;
    pImpl_->keyState.fill(false);
    pImpl_->initialized = true;
    INPUT_LOGI("InputHooks: initialized %dx%d", screenWidth, screenHeight);
    return true;
}

void InputHooks::onTouchEvent(const TouchEvent& event) {
    if (!pImpl_->initialized) return;

    float nx = event.x / static_cast<float>(pImpl_->screenWidth);
    float ny = event.y / static_cast<float>(pImpl_->screenHeight);

    switch (event.action) {
        case TouchAction::DOWN: {
            // Check if touch is in a stick zone
            if (pImpl_->leftStickPointer < 0 &&
                pImpl_->isInStickZone(nx, ny, pImpl_->leftStick)) {
                pImpl_->leftStickPointer = event.pointerId;
                pImpl_->leftStickOriginX = event.x;
                pImpl_->leftStickOriginY = event.y;
                pImpl_->leftStickX = 0.0f;
                pImpl_->leftStickY = 0.0f;
            } else if (pImpl_->rightStickPointer < 0 &&
                       pImpl_->isInStickZone(nx, ny, pImpl_->rightStick)) {
                pImpl_->rightStickPointer = event.pointerId;
                pImpl_->rightStickOriginX = event.x;
                pImpl_->rightStickOriginY = event.y;
                pImpl_->rightStickX = 0.0f;
                pImpl_->rightStickY = 0.0f;
            } else {
                // Button zone detection (simplified layout)
                // Right side buttons: jump (top-right), sprint (bottom-right)
                // Left side non-stick: crouch, enter vehicle
                if (nx > 0.7f && ny < 0.4f) {
                    pImpl_->setKey(VirtualKey::JUMP, true);
                } else if (nx > 0.7f && ny > 0.6f) {
                    pImpl_->setKey(VirtualKey::SPRINT, true);
                } else if (nx < 0.3f && ny < 0.4f) {
                    pImpl_->setKey(VirtualKey::ENTER_VEHICLE, true);
                } else if (nx < 0.3f && ny > 0.6f) {
                    pImpl_->setKey(VirtualKey::CROUCH, true);
                } else if (nx > 0.4f && nx < 0.6f && ny > 0.8f) {
                    pImpl_->setKey(VirtualKey::ATTACK, true);
                }
            }
            break;
        }

        case TouchAction::MOVE: {
            if (event.pointerId == pImpl_->leftStickPointer) {
                pImpl_->updateStickOutput(pImpl_->leftStick,
                    pImpl_->leftStickOriginX, pImpl_->leftStickOriginY,
                    event.x, event.y,
                    pImpl_->leftStickX, pImpl_->leftStickY);

                // Map stick to virtual movement keys
                pImpl_->setKey(VirtualKey::MOVE_FORWARD, pImpl_->leftStickY < -0.3f);
                pImpl_->setKey(VirtualKey::MOVE_BACKWARD, pImpl_->leftStickY > 0.3f);
                pImpl_->setKey(VirtualKey::MOVE_LEFT, pImpl_->leftStickX < -0.3f);
                pImpl_->setKey(VirtualKey::MOVE_RIGHT, pImpl_->leftStickX > 0.3f);
            } else if (event.pointerId == pImpl_->rightStickPointer) {
                pImpl_->updateStickOutput(pImpl_->rightStick,
                    pImpl_->rightStickOriginX, pImpl_->rightStickOriginY,
                    event.x, event.y,
                    pImpl_->rightStickX, pImpl_->rightStickY);

                pImpl_->setKey(VirtualKey::LOOK_UP, pImpl_->rightStickY < -0.3f);
                pImpl_->setKey(VirtualKey::LOOK_DOWN, pImpl_->rightStickY > 0.3f);
                pImpl_->setKey(VirtualKey::LOOK_LEFT, pImpl_->rightStickX < -0.3f);
                pImpl_->setKey(VirtualKey::LOOK_RIGHT, pImpl_->rightStickX > 0.3f);
            }
            break;
        }

        case TouchAction::UP:
        case TouchAction::CANCEL: {
            if (event.pointerId == pImpl_->leftStickPointer) {
                pImpl_->leftStickPointer = -1;
                pImpl_->leftStickX = 0.0f;
                pImpl_->leftStickY = 0.0f;
                pImpl_->setKey(VirtualKey::MOVE_FORWARD, false);
                pImpl_->setKey(VirtualKey::MOVE_BACKWARD, false);
                pImpl_->setKey(VirtualKey::MOVE_LEFT, false);
                pImpl_->setKey(VirtualKey::MOVE_RIGHT, false);
            } else if (event.pointerId == pImpl_->rightStickPointer) {
                pImpl_->rightStickPointer = -1;
                pImpl_->rightStickX = 0.0f;
                pImpl_->rightStickY = 0.0f;
                pImpl_->setKey(VirtualKey::LOOK_UP, false);
                pImpl_->setKey(VirtualKey::LOOK_DOWN, false);
                pImpl_->setKey(VirtualKey::LOOK_LEFT, false);
                pImpl_->setKey(VirtualKey::LOOK_RIGHT, false);
            } else {
                // Release all button keys on any non-stick touch up
                // (simplified — production should track per-pointer button assignments)
                pImpl_->setKey(VirtualKey::JUMP, false);
                pImpl_->setKey(VirtualKey::SPRINT, false);
                pImpl_->setKey(VirtualKey::ENTER_VEHICLE, false);
                pImpl_->setKey(VirtualKey::CROUCH, false);
                pImpl_->setKey(VirtualKey::ATTACK, false);
            }
            break;
        }
    }
}

void InputHooks::setKeyCallback(VirtualKeyCallback callback) {
    pImpl_->keyCallback = std::move(callback);
}

void InputHooks::setLeftStickConfig(const AnalogStickConfig& config) {
    pImpl_->leftStick = config;
}

void InputHooks::setRightStickConfig(const AnalogStickConfig& config) {
    pImpl_->rightStick = config;
}

void InputHooks::getLeftStick(float& x, float& y) const {
    x = pImpl_->leftStickX;
    y = pImpl_->leftStickY;
}

void InputHooks::getRightStick(float& x, float& y) const {
    x = pImpl_->rightStickX;
    y = pImpl_->rightStickY;
}

bool InputHooks::isKeyPressed(VirtualKey key) const {
    auto idx = static_cast<size_t>(key);
    if (idx >= pImpl_->keyState.size()) return false;
    return pImpl_->keyState[idx];
}

void InputHooks::setScreenSize(int width, int height) {
    pImpl_->screenWidth = width;
    pImpl_->screenHeight = height;
    INPUT_LOGI("InputHooks: screen resized to %dx%d", width, height);
}

} // namespace WorldRPG::Game