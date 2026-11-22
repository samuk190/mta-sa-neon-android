package com.saonline.neonlauncher.game;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.Log;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;

import java.io.IOException;
import java.io.InputStream;

/** Built-in F11 map. The stock image covers -3000 to 3000 on both world axes. */
public final class PlayerMapView extends View {
    private static final float WORLD_SIZE = 6000f;
    private static final float WORLD_HALF = WORLD_SIZE / 2f;

    public interface Host {
        String snapshot();
        void onClose();
    }

    private final Host host;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mapPaint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final RectF mapBounds = new RectF();
    private final ScaleGestureDetector scale;
    private Bitmap mapImage;
    private boolean mapImageLoaded;
    private String snapshot = "";
    private float zoom = 1f;
    private float panX;
    private float panY;
    private float lastX;
    private float lastY;

    public PlayerMapView(android.content.Context context, Host host) {
        super(context);
        this.host = host;
        setBackgroundColor(Color.rgb(111, 137, 170));
        scale = new ScaleGestureDetector(context, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override
            public boolean onScale(ScaleGestureDetector detector) {
                zoom = Math.max(0.5f, Math.min(4f, zoom * detector.getScaleFactor()));
                invalidate();
                return true;
            }
        });
    }

    public void refresh() {
        String next = host.snapshot();
        snapshot = next == null ? "" : next;
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        scale.onTouchEvent(event);
        if (event.getActionMasked() == MotionEvent.ACTION_POINTER_UP && event.getPointerCount() == 2) {
            int remainingPointer = 1 - event.getActionIndex();
            lastX = event.getX(remainingPointer);
            lastY = event.getY(remainingPointer);
            return true;
        }
        if (event.getPointerCount() == 1 && !scale.isInProgress()) {
            if (event.getAction() == MotionEvent.ACTION_DOWN) {
                lastX = event.getX();
                lastY = event.getY();
            } else if (event.getAction() == MotionEvent.ACTION_MOVE) {
                float worldPerPixel = WORLD_SIZE / Math.max(1f, mapSize());
                panX -= (event.getX() - lastX) * worldPerPixel;
                panY += (event.getY() - lastY) * worldPerPixel;
                lastX = event.getX();
                lastY = event.getY();
                invalidate();
            }
        }
        return true;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!mapImageLoaded) {
            mapImageLoaded = true;
            // Same default texture as CPlayerMap::CreateOrUpdateMapTexture on desktop.
            try (InputStream image = getContext().getAssets().open("MTA/cgui/images/map_1024.png")) {
                mapImage = BitmapFactory.decodeStream(image);
            } catch (IOException error) {
                Log.e("PlayerMapView", "Could not read the default map image", error);
            }
        }

        float size = mapSize();
        if (size <= 0f) return;
        float pixelsPerWorldUnit = size / WORLD_SIZE;
        float maxPanX = Math.max(0f, (size - getWidth()) / 2f) / pixelsPerWorldUnit;
        float maxPanY = Math.max(0f, (size - getHeight()) / 2f) / pixelsPerWorldUnit;
        panX = Math.max(-maxPanX, Math.min(maxPanX, panX));
        panY = Math.max(-maxPanY, Math.min(maxPanY, panY));
        float left = (getWidth() - size) / 2f - panX * pixelsPerWorldUnit;
        float top = (getHeight() - size) / 2f + panY * pixelsPerWorldUnit;
        mapBounds.set(left, top, left + size, top + size);
        if (mapImage != null) canvas.drawBitmap(mapImage, null, mapBounds, mapPaint);

        float playerX = 0, playerY = 0, heading = 0;
        String[] lines = snapshot.split("\n");
        for (String line : lines) {
            if (line.startsWith("P ")) {
                String[] p = line.substring(2).split(" ");
                if (p.length >= 3) {
                    playerX = f(p[0]);
                    playerY = f(p[1]);
                    heading = f(p[2]);
                }
            }
        }
        for (String line : lines) {
            if (line.startsWith("A ")) drawArea(canvas, line.substring(2));
            else if (line.startsWith("B ") || line.startsWith("M "))
                drawDot(canvas, line.substring(2), line.charAt(0) == 'B' ? 8f : 14f);
        }
        float[] player = project(playerX, playerY);
        paint.setColor(Color.WHITE);
        canvas.save();
        canvas.rotate((float) Math.toDegrees(heading), player[0], player[1]);
        canvas.drawLine(player[0], player[1] + 10, player[0], player[1] - 16, paint);
        canvas.drawLine(player[0] - 8, player[1], player[0], player[1] - 16, paint);
        canvas.drawLine(player[0] + 8, player[1], player[0], player[1] - 16, paint);
        canvas.restore();
        paint.setTextSize(42f);
        paint.setColor(Color.WHITE);
        canvas.drawText("Map", 24f, 64f, paint);
    }

    private void drawArea(Canvas canvas, String body) {
        String[] p = body.split(" ");
        if (p.length < 8) return;
        float[] a = project(f(p[0]), f(p[1]));
        float[] b = project(f(p[0]) + f(p[2]), f(p[1]) + f(p[3]));
        paint.setColor(Color.argb(i(p[7]), i(p[4]), i(p[5]), i(p[6])));
        canvas.drawRect(Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.max(a[0], b[0]), Math.max(a[1], b[1]), paint);
    }

    private void drawDot(Canvas canvas, String body, float radius) {
        String[] p = body.split(" ");
        if (p.length < 5) return;
        float[] at = project(f(p[0]), f(p[1]));
        if (at[0] < 0 || at[1] < 0 || at[0] > getWidth() || at[1] > getHeight()) return;
        paint.setColor(Color.rgb(i(p[2]), i(p[3]), i(p[4])));
        canvas.drawCircle(at[0], at[1], radius, paint);
    }

    private float mapSize() {
        return Math.min(getWidth(), getHeight()) * zoom;
    }

    private float[] project(float x, float y) {
        float sx = mapBounds.left + (x + WORLD_HALF) / WORLD_SIZE * mapBounds.width();
        float sy = mapBounds.top + (WORLD_HALF - y) / WORLD_SIZE * mapBounds.height();
        return new float[]{sx, sy};
    }

    private static float f(String value) {
        try { return Float.parseFloat(value); } catch (NumberFormatException error) { return 0f; }
    }

    private static int i(String value) {
        try { return Integer.parseInt(value); } catch (NumberFormatException error) { return 255; }
    }
}
