package com.worldrpg.neonlauncher.game;

/** Text payload and placement shared by the Canvas renderer and host checks. */
final class DxTextLayout {
    private DxTextLayout() {}

    // Alignment: 0 left/top, 1 center, 2 right/bottom. Oversized text may start
    // outside the box; clipping is a separate option, as in the desktop renderer.
    static float offset(float box, float content, int alignment) {
        return alignment == 1 ? (box - content) * 0.5f : alignment == 2 ? box - content : 0f;
    }

    static int width(float pixels) {
        if (!Float.isFinite(pixels) || pixels >= 32768f) return 32768;
        return Math.max(1, (int) Math.ceil(pixels));
    }

    // CGraphics.cpp:1310-1333 includes lineHeight for the last coded line too.
    // StaticLayout's spacingAdd changes only the distance between baselines.
    static float blockHeight(CharSequence text, float lineHeight, float naturalHeight) {
        if (!(lineHeight > 0f)) return naturalHeight;
        int lines = 1;
        for (int i = 0; i < text.length(); i++) if (text.charAt(i) == '\n') lines++;
        return lines * lineHeight;
    }

    // A frame remains one command per line, including when Lua text contains
    // line breaks or backslashes. Unknown escapes remain literal.
    static String decode(String encoded) {
        StringBuilder out = new StringBuilder(encoded.length());
        for (int i = 0; i < encoded.length(); i++) {
            char ch = encoded.charAt(i);
            if (ch == '\\' && i + 1 < encoded.length()) {
                char next = encoded.charAt(++i);
                if (next == 'n') ch = '\n';
                else if (next == 'r') ch = '\r';
                else if (next == 't') ch = '\t';
                else if (next == '\\') ch = '\\';
                else { out.append('\\'); ch = next; }
            }
            out.append(ch);
        }
        return out.toString();
    }
}
