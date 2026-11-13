package com.worldrpg.neonlauncher.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.text.TextPaint;
import android.view.View;

import java.io.InputStream;
import java.util.Locale;

/**
 * The MTA transfer box (desktop Client/mods/deathmatch/logic/CTransferBox.cpp), drawn with the Default
 * skin colours (skins/Default/CGUI.png).
 * Scaled 2.5x for mobile high-DPI displays.
 * Window height 58 * 2.5 = 145, progress bar from y 50 with height 67.5, icon 50,
 * centred horizontally with its middle at 85% of the screen height, alpha 0.7.
 */
public final class MtaTransferBox extends View {
    private static final float SCALE = 2.5f;
    private static final float HEIGHT = 58f * SCALE;
    private static final float ICON = 20f * SCALE;
    private static final float Y_START = 20f * SCALE;
    private static final float SPACER = 11f * SCALE;
    private static final float PADDING = 80f * SCALE;
    private static final float ALPHA = 0.7f;
    private static final int ICON_COUNT = 10;
    private static final long ICON_MILLIS = 50L;
    private static final String PREFIX = "Download Progress:";
    private static final String INFO = "Disconnect to cancel download";

    // Default skin: TitleBarBackground (8,8,19), TitleBarTop highlight (43,43,65), TitleBarBorder (33,33,53),
    // ProgressBarLitSegment (235,235,240). ProgressBarDimSegment is transparent.
    private static final int COLOR_WINDOW = 0xFF080813;
    private static final int COLOR_HIGHLIGHT = 0xFF2B2B41;
    private static final int COLOR_BORDER = 0xFF212135;
    private static final int COLOR_LIT = 0xFFEBEBF0;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint title = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint info = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final Bitmap[] icons = new Bitmap[ICON_COUNT];
    private final RectF rect = new RectF();
    private final float boxWidth;
    private long done;
    private long total;
    private int iconIndex;
    private long iconTime;

    private final Runnable autoHideRunnable = () -> {
        setVisibility(GONE);
    };

    public MtaTransferBox(Context context) {
        super(context);
        setClickable(false);
        // "default-bold-small" for the title and "default-normal" for the label (Tahoma 8 pt bold / 9 pt).
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextSize(11f * SCALE);
        title.setColor(0xFFFFFFFF);
        info.setTypeface(Typeface.DEFAULT);
        info.setTextSize(12f * SCALE);
        info.setColor(0xFF000000);
        String sample = PREFIX + " " + "999.99 kB of 999.99 kB";
        boxWidth = Math.max(title.measureText(sample), info.measureText(INFO)) + PADDING;
        for (int i = 0; i < ICON_COUNT; ++i) {
            try (InputStream in = context.getAssets().open("cgui/transferset/" + (i + 1) + ".png")) {
                icons[i] = BitmapFactory.decodeStream(in);
            } catch (Exception ignored) {
                icons[i] = null;
            }
        }
        setVisibility(GONE);
    }

    /** Desktop SetDownloadProgress. A total of 0 hides the box (desktop Hide). */
    public void setProgress(long downloaded, long totalBytes) {
        removeCallbacks(autoHideRunnable);
        if (totalBytes <= 0) {
            setVisibility(GONE);
            return;
        }
        done = Math.max(0, Math.min(downloaded, totalBytes));
        total = totalBytes;
        setVisibility(VISIBLE);
        invalidate();

        if (done >= total && total > 0) {
            // When download reaches 100%, hide the transfer box after a brief pause (desktop Hide)
            postDelayed(autoHideRunnable, 800L);
        }
    }

    /** Desktop GetDataUnit (Shared/mods/deathmatch/logic/Utils.cpp). */
    static String dataUnit(long bytes) {
        float value = (float) bytes;
        if (value < 1024f) return bytes + " B";
        value /= 1024f;
        if (value < 1024f) return String.format(Locale.US, "%.2f kB", value);
        value /= 1024f;
        if (value < 1024f) return String.format(Locale.US, "%.2f MB", value);
        value /= 1024f;
        return String.format(Locale.US, "%.2f GB", value);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (total <= 0) return;
        float left = getWidth() * 0.5f - boxWidth * 0.5f;
        float top = getHeight() * 0.85f - HEIGHT * 0.5f;
        int alpha = Math.round(255 * ALPHA);

        // Window: body, the lighter top edge of the title bar, and the border.
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(COLOR_WINDOW);
        fill.setAlpha(alpha);
        canvas.drawRect(left, top, left + boxWidth, top + HEIGHT, fill);
        fill.setColor(COLOR_HIGHLIGHT);
        fill.setAlpha(alpha);
        canvas.drawRect(left, top, left + boxWidth, top + (1.5f * SCALE), fill);
        fill.setStyle(Paint.Style.STROKE);
        fill.setStrokeWidth(1.5f * SCALE);
        fill.setColor(COLOR_BORDER);
        fill.setAlpha(alpha);
        canvas.drawRect(left, top, left + boxWidth, top + HEIGHT, fill);
        fill.setStyle(Paint.Style.FILL);

        // Title: "Download Progress: X of Y", centred in the title bar.
        String text = PREFIX + " " + dataUnit(done) + " of " + dataUnit(total);
        title.setAlpha(alpha);
        float titleY = top + (Y_START - (title.descent() + title.ascent())) * 0.5f;
        canvas.drawText(text, left + (boxWidth - title.measureText(text)) * 0.5f, titleY, title);

        // Progress bar: lit part only (the dim part is transparent in the skin).
        float barTop = top + Y_START;
        float barHeight = HEIGHT - Y_START - SPACER;
        float progress = (float) ((double) done / (double) total);
        fill.setColor(COLOR_LIT);
        fill.setAlpha(alpha);
        rect.set(left, barTop, left + boxWidth * progress, barTop + barHeight);
        canvas.drawRect(rect, fill);

        // Icon and label, centred together inside the bar.
        float infoWidth = info.measureText(INFO);
        float iconTextGap = 6f * SCALE;
        float start = left + (boxWidth - infoWidth - ICON - iconTextGap) * 0.5f;
        long now = android.os.SystemClock.uptimeMillis();
        if (now - iconTime >= ICON_MILLIS) {
            iconTime = now;
            iconIndex = (iconIndex + 1) % ICON_COUNT;
        }
        Bitmap icon = icons[iconIndex];
        if (icon != null) {
            float iconTop = barTop + (barHeight - ICON) * 0.5f;
            rect.set(start, iconTop, start + ICON, iconTop + ICON);
            fill.setAlpha(alpha);
            canvas.drawBitmap(icon, null, rect, fill);
        }
        info.setAlpha(alpha);
        float infoY = barTop + (barHeight - (info.descent() + info.ascent())) * 0.5f;
        canvas.drawText(INFO, start + ICON + iconTextGap, infoY, info);

        // Keep the icon moving while the box is up (desktop DoPulse, every 50 ms).
        postInvalidateDelayed(ICON_MILLIS);
    }
}
