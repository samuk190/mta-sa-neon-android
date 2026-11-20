package com.saonline.neonlauncher.game;

final class HudLayout {
    static final int TOP_LEFT = 0;
    static final int BOTTOM_LEFT = 2;
    static final int BOTTOM_RIGHT = 3;
    static final int FOLLOW_CHAT = 4;
    static final int MIN_CHAT_PERCENT = 60;
    static final int MAX_CHAT_PERCENT = 150;
    static final int DEFAULT_CHAT_PERCENT = 100;

    private HudLayout() {}

    static int clamp(int value, int minimum, int maximum) {
        return Math.max(minimum, Math.min(value, maximum));
    }

    static int left(int anchor, int viewport, int width, int margin) {
        return (anchor & 1) == 0 ? margin : Math.max(margin, viewport - margin - width);
    }

    static int top(int anchor, int viewport, int height, int margin) {
        return anchor < BOTTOM_LEFT ? margin : Math.max(margin, viewport - margin - height);
    }

    static int besideChat(int preferred, int height, int chatTop, int chatHeight, int anchor,
                          int viewport, int margin, int gap) {
        if (preferred + height <= chatTop || preferred >= chatTop + chatHeight) return preferred;
        int above = chatTop - gap - height;
        int below = chatTop + chatHeight + gap;
        boolean fitsAbove = above >= margin;
        boolean fitsBelow = below + height <= viewport - margin;
        if (fitsBelow && (anchor < BOTTOM_LEFT || !fitsAbove)) return below;
        if (fitsAbove) return above;
        return preferred;
    }

    static float keyboardScale(int height, int inset) {
        return height <= 0 ? 1f : clamp(height - inset, 1, height) / (float) height;
    }
}
