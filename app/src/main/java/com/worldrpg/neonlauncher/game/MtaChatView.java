package com.worldrpg.neonlauncher.game;

import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.text.style.ForegroundColorSpan;
import android.view.View;
import java.util.ArrayDeque;

/**
 * Same chat the World RPG client draws on desktop and MobileSA:
 * top-left rows, a dark bar per line, bold text with a shadow, and
 * #RRGGBB color codes. Lines stay for 14 seconds and fade for 2.
 */
public final class MtaChatView extends View {
    private static final int LIFE_MS = 14000;
    private static final int FADE_MS = 2000;
    private static final int MAX_LINES = 10;

    private static final class Line {
        final CharSequence text;
        final long born = System.currentTimeMillis();
        Line(CharSequence text) { this.text = text; }
    }

    private final ArrayDeque<Line> lines = new ArrayDeque<>();
    private final TextPaint paint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bar = new Paint(Paint.ANTI_ALIAS_FLAG);

    public MtaChatView(android.content.Context context) {
        super(context);
        paint.setTypeface(Typeface.DEFAULT_BOLD);
        paint.setShadowLayer(2f, 1f, 1f, 0xCC000000);
        bar.setColor(Color.argb(160, 12, 12, 16));
        setClickable(false);
    }

    public void clearLines() {
        lines.clear();
        invalidate();
    }

    public void push(String raw) {
        if (raw == null || raw.isEmpty()) return;
        lines.addLast(new Line(colorize(raw)));
        while (lines.size() > 40) lines.removeFirst();
        setVisibility(VISIBLE);
        invalidate();
        removeCallbacks(tick);
        postDelayed(tick, 400);
    }

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (visibleCount() > 0) {
                invalidate();
                postDelayed(this, 400);
            }
        }
    };

    private int visibleCount() {
        long now = System.currentTimeMillis();
        int count = 0;
        for (Line line : lines) {
            if (now - line.born < LIFE_MS + FADE_MS) count++;
        }
        return count;
    }

    static CharSequence colorize(String raw) {
        int color = Color.rgb(172, 213, 254);
        SpannableStringBuilder out = new SpannableStringBuilder();
        int i = 0;
        while (i < raw.length()) {
            if (raw.charAt(i) == '#' && i + 6 < raw.length() && isHex(raw, i + 1)) {
                color = Color.parseColor("#" + raw.substring(i + 1, i + 7));
                i += 7;
                continue;
            }
            int start = out.length();
            out.append(raw.charAt(i));
            out.setSpan(new ForegroundColorSpan(color), start, start + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            i++;
        }
        return out;
    }

    private static boolean isHex(String raw, int at) {
        for (int i = 0; i < 6; i++) {
            char ch = raw.charAt(at + i);
            boolean ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
            if (!ok) return false;
        }
        return true;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int width = getWidth();
        if (width <= 0) return;
        float lineHeight = Math.max(22f, getResources().getDisplayMetrics().heightPixels / 720f * 22f);
        paint.setTextSize(lineHeight * 0.72f);
        long now = System.currentTimeMillis();
        Line[] recent = new Line[MAX_LINES];
        int n = 0;
        Line[] all = lines.toArray(new Line[0]);
        for (int i = all.length - 1; i >= 0 && n < MAX_LINES; i--) {
            if (now - all[i].born < LIFE_MS + FADE_MS) recent[n++] = all[i];
        }
        // Desktop CChat (Client/core/CChat.cpp): one background box behind the
        // whole stack, and each line steps down by exactly the font height.
        int shown = 0;
        for (int i = n - 1; i >= 0; i--) {
            if (fadeAlpha(recent[i], now) >= 8) shown++;
        }
        if (shown > 0) {
            bar.setAlpha(160);
            canvas.drawRect(0, 0, width, shown * lineHeight, bar);
        }
        float y = 0f;
        for (int i = n - 1; i >= 0; i--) {
            Line line = recent[i];
            int alpha = fadeAlpha(line, now);
            if (alpha < 8) continue;
            paint.setAlpha(alpha);
            StaticLayout layout = StaticLayout.Builder.obtain(line.text, 0, line.text.length(), paint,
                    Math.max(1, width - dp(16))).setMaxLines(1).build();
            canvas.save();
            canvas.translate(dp(8), y + (lineHeight - layout.getHeight()) / 2f);
            layout.draw(canvas);
            canvas.restore();
            y += lineHeight;
        }
    }

    private int fadeAlpha(Line line, long now) {
        long age = now - line.born;
        if (age <= LIFE_MS) return 255;
        return (int) (255 * (1f - (age - LIFE_MS) / (float) FADE_MS));
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
