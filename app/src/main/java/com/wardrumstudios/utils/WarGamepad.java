package com.wardrumstudios.utils;

import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/**
 * Android hardware gamepad bridge for libGTASA.so (AND_GamepadUpdate / WarGamepad_*).
 * Maps up to 4 connected SOURCE_GAMEPAD / SOURCE_JOYSTICK InputDevices to the 16-bit
 * button mask and 6 analog axes (LX, LY, RX, RY, LT, RT) polled by libGTASA.so every frame.
 */
public class WarGamepad extends com.wardrumstudios.utils.WarBilling {
    private static final int MAX_GAMEPADS = 4;
    private static final int NUM_AXES = 6;
    private static final int GAMEPAD_TYPE_STANDARD = 1;
    private static final float STICK_DEADZONE = 0.12f;
    private static final float TRIGGER_DEADZONE = 0.05f;

    private static final int BIT_A = 1 << 0;
    private static final int BIT_B = 1 << 1;
    private static final int BIT_X = 1 << 2;
    private static final int BIT_Y = 1 << 3;
    private static final int BIT_START = 1 << 4;
    private static final int BIT_SELECT = 1 << 5;
    private static final int BIT_L1 = 1 << 6;
    private static final int BIT_R1 = 1 << 7;
    private static final int BIT_DPAD_UP = 1 << 8;
    private static final int BIT_DPAD_DOWN = 1 << 9;
    private static final int BIT_DPAD_LEFT = 1 << 10;
    private static final int BIT_DPAD_RIGHT = 1 << 11;
    private static final int BIT_L3 = 1 << 12;
    private static final int BIT_R3 = 1 << 13;

    private final Object gamepadLock = new Object();
    private final int[] gamepadDeviceIds = new int[] { -1, -1, -1, -1 };
    private final int[] gamepadButtons = new int[MAX_GAMEPADS];
    private final float[][] gamepadAxes = new float[MAX_GAMEPADS][NUM_AXES];
    private long lastDeviceScanMs = 0L;

    private static boolean isGamepadSource(int source) {
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private static boolean isGamepadDevice(InputDevice device) {
        if (device == null || device.isVirtual()) return false;
        return isGamepadSource(device.getSources());
    }

    private static float applyDeadzone(float value, float deadzone) {
        if (Math.abs(value) < deadzone) return 0.0f;
        return Math.max(-1.0f, Math.min(1.0f, value));
    }

    private int findOrRegisterSlotLocked(int deviceId) {
        if (deviceId <= 0) return -1;
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (gamepadDeviceIds[i] == deviceId) return i;
        }
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (gamepadDeviceIds[i] == -1 || InputDevice.getDevice(gamepadDeviceIds[i]) == null) {
                gamepadDeviceIds[i] = deviceId;
                gamepadButtons[i] = 0;
                for (int a = 0; a < NUM_AXES; a++) gamepadAxes[i][a] = 0.0f;
                return i;
            }
        }
        return -1;
    }

    private void scanConnectedGamepadsLocked() {
        long now = android.os.SystemClock.uptimeMillis();
        if (now - lastDeviceScanMs < 1000L) return;
        lastDeviceScanMs = now;
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (gamepadDeviceIds[i] != -1 && InputDevice.getDevice(gamepadDeviceIds[i]) == null) {
                gamepadDeviceIds[i] = -1;
                gamepadButtons[i] = 0;
                for (int a = 0; a < NUM_AXES; a++) gamepadAxes[i][a] = 0.0f;
            }
        }
        int[] ids = InputDevice.getDeviceIds();
        if (ids == null) return;
        for (int id : ids) {
            InputDevice dev = InputDevice.getDevice(id);
            if (isGamepadDevice(dev)) {
                findOrRegisterSlotLocked(id);
            }
        }
    }

    protected boolean handleGamepadKeyEvent(KeyEvent event) {
        if (event == null || !isGamepadSource(event.getSource())) return false;
        int action = event.getAction();
        if (action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP) return false;
        boolean down = action == KeyEvent.ACTION_DOWN;
        int code = event.getKeyCode();

        int bit = 0;
        int triggerAxis = -1;
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A: bit = BIT_A; break;
            case KeyEvent.KEYCODE_BUTTON_B: bit = BIT_B; break;
            case KeyEvent.KEYCODE_BUTTON_X: bit = BIT_X; break;
            case KeyEvent.KEYCODE_BUTTON_Y: bit = BIT_Y; break;
            case KeyEvent.KEYCODE_BUTTON_START: bit = BIT_START; break;
            case KeyEvent.KEYCODE_BUTTON_SELECT: bit = BIT_SELECT; break;
            case KeyEvent.KEYCODE_BUTTON_L1: bit = BIT_L1; break;
            case KeyEvent.KEYCODE_BUTTON_R1: bit = BIT_R1; break;
            case KeyEvent.KEYCODE_DPAD_UP: bit = BIT_DPAD_UP; break;
            case KeyEvent.KEYCODE_DPAD_DOWN: bit = BIT_DPAD_DOWN; break;
            case KeyEvent.KEYCODE_DPAD_LEFT: bit = BIT_DPAD_LEFT; break;
            case KeyEvent.KEYCODE_DPAD_RIGHT: bit = BIT_DPAD_RIGHT; break;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: bit = BIT_L3; break;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: bit = BIT_R3; break;
            case KeyEvent.KEYCODE_BUTTON_L2: triggerAxis = 4; break;
            case KeyEvent.KEYCODE_BUTTON_R2: triggerAxis = 5; break;
            default:
                return false;
        }
        synchronized (gamepadLock) {
            int slot = findOrRegisterSlotLocked(event.getDeviceId());
            if (slot < 0) return false;
            if (bit != 0) {
                if (down) gamepadButtons[slot] |= bit;
                else gamepadButtons[slot] &= ~bit;
            } else if (triggerAxis >= 0) {
                gamepadAxes[slot][triggerAxis] = down ? 1.0f : 0.0f;
            }
        }
        return true;
    }

    protected boolean handleGamepadMotionEvent(MotionEvent event) {
        if (event == null || !event.isFromSource(InputDevice.SOURCE_JOYSTICK)) return false;
        if (event.getActionMasked() != MotionEvent.ACTION_MOVE) return false;
        synchronized (gamepadLock) {
            int slot = findOrRegisterSlotLocked(event.getDeviceId());
            if (slot < 0) return false;
            gamepadAxes[slot][0] = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_X), STICK_DEADZONE);
            gamepadAxes[slot][1] = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_Y), STICK_DEADZONE);
            gamepadAxes[slot][2] = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_Z), STICK_DEADZONE);
            gamepadAxes[slot][3] = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_RZ), STICK_DEADZONE);

            float lt = Math.max(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE));
            float rt = Math.max(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS));
            gamepadAxes[slot][4] = lt >= TRIGGER_DEADZONE ? Math.min(1.0f, lt) : 0.0f;
            gamepadAxes[slot][5] = rt >= TRIGGER_DEADZONE ? Math.min(1.0f, rt) : 0.0f;

            float hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X);
            float hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
            int dpadMask = BIT_DPAD_UP | BIT_DPAD_DOWN | BIT_DPAD_LEFT | BIT_DPAD_RIGHT;
            int hatBits = 0;
            if (hatX < -0.5f) hatBits |= BIT_DPAD_LEFT;
            else if (hatX > 0.5f) hatBits |= BIT_DPAD_RIGHT;
            if (hatY < -0.5f) hatBits |= BIT_DPAD_UP;
            else if (hatY > 0.5f) hatBits |= BIT_DPAD_DOWN;
            gamepadButtons[slot] = (gamepadButtons[slot] & ~dpadMask) | hatBits;
        }
        return true;
    }

    public float GetGamepadAxis(int axis) {
        return GetGamepadAxis(0, axis);
    }

    public int GetGamepadButtons() {
        return GetGamepadButtons(0);
    }

    public int GetGamepadTrack(int i, int i2, int i3) {
        return 0;
    }

    public int GetGamepadType() {
        return GetGamepadType(0);
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity
    public native boolean processTouchpadAsPointer(android.view.ViewParent viewParent, boolean z);

    public float GetGamepadAxis(int index, int axis) {
        if (index < 0 || index >= MAX_GAMEPADS || axis < 0 || axis >= NUM_AXES) return 0.0f;
        synchronized (gamepadLock) {
            if (gamepadDeviceIds[index] == -1) return 0.0f;
            return gamepadAxes[index][axis];
        }
    }

    public int GetGamepadButtons(int index) {
        if (index < 0 || index >= MAX_GAMEPADS) return 0;
        synchronized (gamepadLock) {
            if (gamepadDeviceIds[index] == -1) return 0;
            return gamepadButtons[index];
        }
    }

    public int GetGamepadType(int index) {
        if (index < 0 || index >= MAX_GAMEPADS) return -1;
        synchronized (gamepadLock) {
            if (index == 0) scanConnectedGamepadsLocked();
            return gamepadDeviceIds[index] != -1 ? GAMEPAD_TYPE_STANDARD : -1;
        }
    }

    public int GetGamepadTrack(int i, int i2) {
        return 0;
    }
}
