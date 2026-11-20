package com.worldrpg.neonlauncher.game;

/** Logical pixels shared by server layouts, the DX canvas and cursor hit tests. */
public final class InterfaceScale {
    public static final int DEFAULT_PERCENT = 100;
    public static final int MIN_PERCENT = 50;
    public static final int MAX_PERCENT = 150;
    public static final int STEP_PERCENT = 5;

    private InterfaceScale() {}

    public static int clampPercent(int percent) {
        int clamped = Math.max(MIN_PERCENT, Math.min(MAX_PERCENT, percent));
        return MIN_PERCENT + Math.round((clamped - MIN_PERCENT) / (float) STEP_PERCENT) * STEP_PERCENT;
    }

    public static int logicalSize(int physicalPixels, float density, int percent) {
        if (!(density > 0f) || !Float.isFinite(density)) density = 1f;
        float factor = density * clampPercent(percent) / 100f;
        return Math.max(1, Math.round(Math.max(1, physicalPixels) / factor));
    }

    public static float toLogical(float coordinate, int physicalSize, int logicalSize) {
        return physicalSize > 0 && logicalSize > 0 ? coordinate * logicalSize / physicalSize : coordinate;
    }
}
