package com.worldrpg.neonlauncher.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * The connection loading screen of the Neon desktop client, drawn the same way:
 * CGraphics::DrawConnectionLoadingScreen (Client/core/Graphics/CGraphics.cpp:2559-2648).
 *
 * Artwork: one of MTA/cgui/images/loadscreens/loadsc1..14.jpg per connection
 * (BeginConnectionLoading picks 1 + tick % 14), aspect-filled. Status font:
 * "BankGothic Md BT" 30 px from MTA/cgui/sabankgothic.ttf (CGraphics.cpp:1469, 1480).
 * Synced artwork overrides the fourteen images bundled in the APK, using the
 * same relative paths as CalcMTASAPath on desktop. The font is read from the
 * game folder, with a bold system font as fallback.
 */
public final class ConnectionLoadingView extends View {
    private static final String TAG = "ConnectionLoading";
    private static final int ARTWORK_COUNT = 14;
    private static final float FONT_PX = 30f;           // bankgothic md bt, 30 (CGraphics.cpp:1469)
    private static final long INDETERMINATE_CYCLE_MS = 1600L;

    private final String gameDirectory;
    private final Paint fill = new Paint();
    private final Paint image = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect src = new Rect();
    private final RectF dst = new RectF();

    private Bitmap artwork;
    private String status = "";
    private float barFill = 0f;       // whole-load bar, 0..1
    private float segStart = -1f;     // moving segment range, < 0 when the phase has a counter
    private float segEnd = -1f;
    private long animStart;
    private boolean active;

    public ConnectionLoadingView(Context context, String gameDirectory) {
        super(context);
        this.gameDirectory = gameDirectory;
        setClickable(true);   // the screen covers the game; touches stay here
        // Above every overlay added to the content frame (chat button, DX overlay, transfer box).
        setTranslationZ(10000f);
        setVisibility(GONE);
        text.setColor(0xFFE4D29F);
        Typeface face = null;
        File font = gameFile("MTA/cgui/sabankgothic.ttf");
        if (font != null) {
            try {
                face = Typeface.createFromFile(font);
            } catch (RuntimeException e) {
                Log.w(TAG, "sabankgothic.ttf did not load: " + e.getMessage());
            }
        }
        text.setTypeface(face != null ? face : Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
    }

    /** BeginConnectionLoading: new artwork, status and bar. */
    public void begin(String newStatus, float newFill, float newSegStart, float newSegEnd) {
        active = true;
        setState(newStatus, newFill, newSegStart, newSegEnd);
        animStart = SystemClock.uptimeMillis();
        loadArtwork(1 + (int) (SystemClock.uptimeMillis() % ARTWORK_COUNT));
        setVisibility(VISIBLE);
        bringToFront();
        invalidate();
    }

    /** UpdateConnectionLoading. Opens the screen if it was not open (for the pre-join "Connecting" phase). */
    public void update(String newStatus, float newFill, float newSegStart, float newSegEnd) {
        if (!active) {
            begin(newStatus, newFill, newSegStart, newSegEnd);
            return;
        }
        setState(newStatus, newFill, newSegStart, newSegEnd);
        invalidate();
    }

    private void setState(String newStatus, float newFill, float newSegStart, float newSegEnd) {
        status = newStatus == null ? "" : newStatus;
        barFill = Math.max(0f, Math.min(newFill, 1f));
        segStart = newSegStart;
        segEnd = newSegEnd;
    }

    /** EndConnectionLoading: hide and free the artwork. */
    public void end() {
        active = false;
        status = "";
        barFill = 0f;
        segStart = segEnd = -1f;
        setVisibility(GONE);
        if (artwork != null) {
            artwork.recycle();
            artwork = null;
        }
    }

    public boolean isActive() {
        return active;
    }

    private File gameFile(String relative) {
        if (gameDirectory == null) return null;
        File f = new File(gameDirectory, relative);
        return f.isFile() ? f : null;
    }

    // Only the chosen image is loaded (the desktop comment: loading all fourteen wastes video memory).
    private void loadArtwork(int index) {
        if (artwork != null) {
            artwork.recycle();
            artwork = null;
        }
        String relative = "MTA/cgui/images/loadscreens/loadsc" + index + ".jpg";
        File file = gameFile(relative);
        if (file != null) {
            try {
                artwork = decodeArtwork(relative, file);
            } catch (IOException | SecurityException e) {
                Log.w(TAG, "Cannot read synced loading artwork; using bundled image", e);
            }
        }
        if (artwork != null) return;
        try {
            artwork = decodeArtwork(relative, null);
        } catch (IOException e) {
            Log.w(TAG, "Cannot read bundled loading artwork: " + relative, e);
        }
    }

    private InputStream openArtwork(String relative, File file) throws IOException {
        return file != null ? new FileInputStream(file) : getContext().getAssets().open(relative);
    }

    private Bitmap decodeArtwork(String relative, File file) throws IOException {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        try (InputStream input = openArtwork(relative, file)) {
            BitmapFactory.decodeStream(input, null, bounds);
        }
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null;
        int target = Math.max(1, Math.max(getResources().getDisplayMetrics().widthPixels,
                getResources().getDisplayMetrics().heightPixels));
        int sample = 1;
        while (bounds.outWidth / 2 / sample >= target) sample *= 2;
        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inSampleSize = sample;
        try (InputStream input = openArtwork(relative, file)) {
            return BitmapFactory.decodeStream(input, null, opts);
        }
    }

    private void rect(Canvas canvas, float x, float y, float w, float h, int argb) {
        fill.setColor(argb);
        canvas.drawRect(x, y, x + w, y + h, fill);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (!active) return;
        final float vw = getWidth();
        final float vh = getHeight();
        if (vw <= 0 || vh <= 0) return;

        rect(canvas, 0, 0, vw, vh, 0xFF020303);

        if (artwork != null && artwork.getWidth() > 0 && artwork.getHeight() > 0) {
            // Aspect-fill without stretching (CGraphics.cpp:2582-2598).
            float tw = artwork.getWidth();
            float th = artwork.getHeight();
            float textureAspect = tw / th;
            float viewportAspect = vw / vh;
            float u = 0f, v = 0f, sizeU = 1f, sizeV = 1f;
            if (textureAspect > viewportAspect) {
                sizeU = viewportAspect / textureAspect;
                u = (1f - sizeU) * 0.5f;
            } else if (textureAspect < viewportAspect) {
                sizeV = textureAspect / viewportAspect;
                v = (1f - sizeV) * 0.5f;
            }
            src.set((int) (u * tw), (int) (v * th), (int) ((u + sizeU) * tw), (int) ((v + sizeV) * th));
            dst.set(0, 0, vw, vh);
            canvas.drawBitmap(artwork, src, dst, image);
        }

        // Readability layers (CGraphics.cpp:2609-2610).
        rect(canvas, 0, 0, vw, vh, 0x30000000);
        rect(canvas, 0, vh * 0.78f, vw, vh * 0.22f, 0x78000000);

        final float uiScale = Math.max(0.75f, Math.min(Math.min(vw / 1920f, vh / 1080f), 2.0f));
        final float barWidth = Math.min(vw * 0.42f, 720f * uiScale);
        final float barHeight = Math.max(7f, 10f * uiScale);
        final float barX = (vw - barWidth) * 0.5f;
        final float barY = vh - 62f * uiScale;
        final float textScale = 0.68f * uiScale;

        if (!status.isEmpty()) {
            // DT_LEFT at the top of the rect (barY - 34 * uiScale).
            text.setTextSize(FONT_PX * textScale);
            Paint.FontMetrics fm = text.getFontMetrics();
            canvas.drawText(status, barX, barY - 34f * uiScale - fm.ascent, text);
        }

        rect(canvas, barX - 2f, barY - 2f, barWidth + 4f, barHeight + 4f, 0xD8000000);
        rect(canvas, barX, barY, barWidth, barHeight, 0xB04A5A6B);

        // Whole-load fill (phone addition; the desktop bar restarts per phase).
        if (barFill > 0f) rect(canvas, barX, barY, barWidth * barFill, barHeight, 0xFFACCBF1);
        if (segStart >= 0f && segEnd > segStart) {
            // The desktop moving segment (CGraphics.cpp:2628-2644: 22% wide, 1600 ms sweep), kept inside the
            // part of the bar the current phase still owns, at half strength over the empty track.
            float regionX = barX + barWidth * segStart;
            float regionW = barWidth * (Math.min(segEnd, 1f) - segStart);
            float segmentWidth = Math.min(regionW, barWidth * 0.22f);
            float cycle = ((SystemClock.uptimeMillis() - animStart) % INDETERMINATE_CYCLE_MS)
                    / (float) INDETERMINATE_CYCLE_MS;
            float segmentX = regionX - segmentWidth + (regionW + segmentWidth) * cycle;
            float start = Math.max(regionX, segmentX);
            float endX = Math.min(regionX + regionW, segmentX + segmentWidth);
            if (endX > start) rect(canvas, start, barY, endX - start, barHeight, 0x80ACCBF1);
            postInvalidateOnAnimation();
        }
    }
}
