package com.worldrpg.neonlauncher.game;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.text.style.ForegroundColorSpan;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import java.util.ArrayList;
import java.util.List;

/**
 * Native chat matching Multi Theft Auto desktop (Client/core/CChat.cpp):
 * - Circular buffer of up to 100 lines (CHAT_MAX_LINES).
 * - Up to 10 visible lines at a time (CHAT_NUM_LINES).
 * - #RRGGBB color-coded spans.
 * - 14-second life with 2-second fade-out when anchored to bottom.
 * - Touch-drag scrolling into history (ScrollUp / ScrollDown) with no fade-out while reading history.
 * - Auto-reset to bottom after 8 seconds idle.
 */
public final class MtaChatView extends View {
    private static final int LIFE_MS = 14000;
    private static final int FADE_MS = 2000;
    private static final int VISIBLE_MAX_LINES = 10;
    private static final int BUFFER_MAX_LINES = 100;
    private static final int SCROLL_RESET_DELAY_MS = 8000;

    private static final class Line {
        final CharSequence text;
        final long born = System.currentTimeMillis();
        Line(CharSequence text) { this.text = text; }
    }

    private final List<Line> lines = new ArrayList<>();
    private final TextPaint paint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bar = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final int touchSlop;

    private int scrollOffset = 0; // 0 = anchored to bottom/latest lines; >0 = scrolled up in history
    private float touchStartY = 0f;
    private float accumulatedDrag = 0f;
    private boolean isDragging = false;
    private boolean chatVisible = true;

    public MtaChatView(Context context) {
        super(context);
        paint.setTypeface(Typeface.DEFAULT_BOLD);
        paint.setShadowLayer(2f, 1f, 1f, 0xCC000000);
        bar.setColor(Color.argb(160, 12, 12, 16));
        touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        setClickable(true);
        setFocusable(false);
    }

    public void setChatVisible(boolean visible) {
        this.chatVisible = visible;
        setVisibility(visible ? VISIBLE : GONE);
        invalidate();
    }

    public boolean isChatVisible() {
        return chatVisible;
    }

    public void clearLines() {
        synchronized (lines) {
            lines.clear();
            scrollOffset = 0;
        }
        removeCallbacks(scrollResetRunnable);
        postInvalidate();
    }

    public void push(String raw) {
        if (raw == null || raw.isEmpty()) return;
        synchronized (lines) {
            lines.add(new Line(colorize(raw)));
            while (lines.size() > BUFFER_MAX_LINES) {
                lines.remove(0);
            }
            if (scrollOffset > 0) {
                scrollOffset = Math.min(scrollOffset + 1, Math.max(0, lines.size() - VISIBLE_MAX_LINES));
            }
        }
        if (chatVisible) {
            setVisibility(VISIBLE);
        }
        postInvalidate();
        removeCallbacks(tick);
        postDelayed(tick, 400);
    }

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (visibleCount() > 0 || scrollOffset > 0) {
                invalidate();
                postDelayed(this, 400);
            }
        }
    };

    private final Runnable scrollResetRunnable = new Runnable() {
        @Override
        public void run() {
            if (scrollOffset > 0) {
                scrollOffset = 0;
                invalidate();
            }
        }
    };

    private int visibleCount() {
        long now = System.currentTimeMillis();
        int count = 0;
        synchronized (lines) {
            for (Line line : lines) {
                if (now - line.born < LIFE_MS + FADE_MS) count++;
            }
        }
        return count;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!chatVisible) return super.onTouchEvent(event);

        float lineHeight = Math.max(22f, getResources().getDisplayMetrics().heightPixels / 720f * 22f);
        int total;
        synchronized (lines) {
            total = lines.size();
        }
        int maxScroll = Math.max(0, total - VISIBLE_MAX_LINES);

        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                touchStartY = event.getY();
                accumulatedDrag = 0f;
                isDragging = false;
                removeCallbacks(scrollResetRunnable);
                if (getParent() != null) {
                    getParent().requestDisallowInterceptTouchEvent(true);
                }
                return true;

            case MotionEvent.ACTION_MOVE:
                float dy = event.getY() - touchStartY;
                touchStartY = event.getY();
                accumulatedDrag += dy;

                if (!isDragging && Math.abs(accumulatedDrag) > touchSlop) {
                    isDragging = true;
                }

                if (isDragging && maxScroll > 0) {
                    // Dragging down (positive dy) -> scroll UP into history (increase offset)
                    // Dragging up (negative dy) -> scroll DOWN toward newest (decrease offset)
                    float step = lineHeight * 0.75f;
                    int lineDelta = (int) (accumulatedDrag / step);
                    if (lineDelta != 0) {
                        accumulatedDrag -= lineDelta * step;
                        int nextOffset = Math.max(0, Math.min(maxScroll, scrollOffset + lineDelta));
                        if (nextOffset != scrollOffset) {
                            scrollOffset = nextOffset;
                            invalidate();
                        }
                    }
                }
                return true;

            case MotionEvent.ACTION_UP:
                if (getParent() != null) {
                    getParent().requestDisallowInterceptTouchEvent(false);
                }
                if (isDragging) {
                    isDragging = false;
                    if (scrollOffset > 0) {
                        removeCallbacks(scrollResetRunnable);
                        postDelayed(scrollResetRunnable, SCROLL_RESET_DELAY_MS);
                    }
                } else {
                    performClick();
                }
                return true;

            case MotionEvent.ACTION_CANCEL:
                if (getParent() != null) {
                    getParent().requestDisallowInterceptTouchEvent(false);
                }
                isDragging = false;
                if (scrollOffset > 0) {
                    removeCallbacks(scrollResetRunnable);
                    postDelayed(scrollResetRunnable, SCROLL_RESET_DELAY_MS);
                }
                return true;
        }
        return super.onTouchEvent(event);
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }

    static CharSequence colorize(String raw) {
        int color = Color.rgb(172, 213, 254);
        SpannableStringBuilder out = new SpannableStringBuilder();
        int i = 0;
        while (i < raw.length()) {
            if (raw.charAt(i) == '#' && i + 6 < raw.length() && isHex(raw, i + 1)) {
                try {
                    color = Color.parseColor("#" + raw.substring(i + 1, i + 7));
                    i += 7;
                    continue;
                } catch (Exception ignored) {}
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

        List<Line> drawList = new ArrayList<>();
        int currentOffset;
        synchronized (lines) {
            if (lines.isEmpty()) return;
            currentOffset = scrollOffset;
            int total = lines.size();
            int endIdx = total - 1 - currentOffset;
            int startIdx = Math.max(0, endIdx - VISIBLE_MAX_LINES + 1);
            for (int i = startIdx; i <= endIdx && i < total; i++) {
                drawList.add(lines.get(i));
            }
        }

        if (drawList.isEmpty()) return;

        int shown = 0;
        if (currentOffset > 0) {
            shown = drawList.size();
        } else {
            for (Line line : drawList) {
                if (fadeAlpha(line, now) >= 8) shown++;
            }
        }

        if (shown > 0) {
            bar.setAlpha(160);
            canvas.drawRect(0, 0, width, shown * lineHeight, bar);
        }

        float y = 0f;
        for (Line line : drawList) {
            int alpha;
            if (currentOffset > 0) {
                alpha = 255;
            } else {
                alpha = fadeAlpha(line, now);
                if (alpha < 8) continue;
            }
            paint.setAlpha(alpha);
            StaticLayout layout = StaticLayout.Builder.obtain(line.text, 0, line.text.length(), paint,
                    Math.max(1, width - dp(16))).setMaxLines(1).build();
            canvas.save();
            canvas.translate(dp(8), y + (lineHeight - layout.getHeight()) / 2f);
            layout.draw(canvas);
            canvas.restore();
            y += lineHeight;
        }

        if (currentOffset > 0) {
            paint.setAlpha(220);
            paint.setColor(Color.argb(220, 255, 200, 80));
            String indicator = "▲ +" + currentOffset;
            float textWidth = paint.measureText(indicator);
            canvas.drawText(indicator, width - textWidth - dp(12), lineHeight * 0.8f, paint);
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
