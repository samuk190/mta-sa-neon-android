package com.wardrumstudios.utils;

/* compiled from: r8-map-id-eb2038ccfe378d12a5577dbd7f9bb9474169988104cef0231e3c6c55764b3b76 */
/* loaded from: classes.dex */
public class WarGamepad extends com.wardrumstudios.utils.WarBilling {
    public float GetGamepadAxis(int i) {
        return 0.0f;
    }

    public int GetGamepadButtons() {
        return 0;
    }

    public int GetGamepadTrack(int i, int i2, int i3) {
        java.lang.System.out.println("**** GetGamepadTrack()");
        return 0;
    }

    public int GetGamepadType() {
        return -1;
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity
    public native boolean processTouchpadAsPointer(android.view.ViewParent viewParent, boolean z);

    public float GetGamepadAxis(int i, int i2) {
        return 0.0f;
    }

    public int GetGamepadButtons(int i) {
        return 0;
    }

    public int GetGamepadType(int i) {
        return 0;
    }

    public int GetGamepadTrack(int i, int i2) {
        java.lang.System.out.println("**** GetGamepadTrack()");
        return 0;
    }
}
