package com.worldrpg.neonlauncher.game;

import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;

/** Full map used by the desktop client on F11. World box is 6000 units. */
public final class PlayerMapView extends View {
    public interface Host {
        String snapshot();
        void onClose();
    }

    private final Host host;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ScaleGestureDetector scale;
    private String snapshot = "";
    private float zoom = 1f;
    private float panX;
    private float panY;
    private float lastX;
    private float lastY;
    private boolean attached = true;

    public PlayerMapView(android.content.Context context, Host host) {
        super(context);
        this.host = host;
        setBackgroundColor(Color.rgb(111, 137, 170));
        scale = new ScaleGestureDetector(context, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override
            public boolean onScale(ScaleGestureDetector detector) {
                zoom = Math.max(0.5f, Math.min(4f, zoom * detector.getScaleFactor()));
                attached = false;
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
        if (event.getPointerCount() == 1 && !scale.isInProgress()) {
            if (event.getAction() == MotionEvent.ACTION_DOWN) {
                lastX = event.getX();
                lastY = event.getY();
            } else if (event.getAction() == MotionEvent.ACTION_MOVE) {
                float world = 6000f / zoom;
                panX -= (event.getX() - lastX) / Math.max(1f, getWidth()) * world;
                panY += (event.getY() - lastY) / Math.max(1f, getHeight()) * world;
                lastX = event.getX();
                lastY = event.getY();
                attached = false;
                invalidate();
            }
        }
        return true;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
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
        float centerX = attached ? playerX : playerX + panX;
        float centerY = attached ? playerY : playerY + panY;
        float world = 6000f / zoom;
        for (String line : lines) {
            if (line.startsWith("A ")) drawArea(canvas, line.substring(2), centerX, centerY, world);
            else if (line.startsWith("B ") || line.startsWith("M "))
                drawDot(canvas, line.substring(2), centerX, centerY, world, line.charAt(0) == 'B' ? 8f : 14f);
        }
        float[] player = project(playerX, playerY, centerX, centerY, world);
        paint.setColor(Color.WHITE);
        canvas.save();
        canvas.rotate((float) Math.toDegrees(heading), player[0], player[1]);
        canvas.drawLine(player[0], player[1] + 10, player[0], player[1] - 16, paint);
        canvas.drawLine(player[0] - 8, player[1], player[0], player[1] - 16, paint);
        canvas.drawLine(player[0] + 8, player[1], player[0], player[1] - 16, paint);
        canvas.restore();
        paint.setTextSize(42f);
        paint.setColor(Color.WHITE);
        canvas.drawText(attached ? "Map" : "Map  drag", 24f, 64f, paint);
    }

    private void drawArea(Canvas canvas, String body, float centerX, float centerY, float world) {
        String[] p = body.split(" ");
        if (p.length < 8) return;
        float[] a = project(f(p[0]), f(p[1]), centerX, centerY, world);
        float[] b = project(f(p[0]) + f(p[2]), f(p[1]) + f(p[3]), centerX, centerY, world);
        paint.setColor(Color.argb(i(p[7]), i(p[4]), i(p[5]), i(p[6])));
        canvas.drawRect(Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.max(a[0], b[0]), Math.max(a[1], b[1]), paint);
    }

    private void drawDot(Canvas canvas, String body, float centerX, float centerY, float world, float radius) {
        String[] p = body.split(" ");
        if (p.length < 5) return;
        float[] at = project(f(p[0]), f(p[1]), centerX, centerY, world);
        if (at[0] < 0 || at[1] < 0 || at[0] > getWidth() || at[1] > getHeight()) return;
        paint.setColor(Color.rgb(i(p[2]), i(p[3]), i(p[4])));
        canvas.drawCircle(at[0], at[1], radius, paint);
    }

    private float[] project(float x, float y, float centerX, float centerY, float world) {
        float sx = (x - (centerX - world * 0.5f)) / world * getWidth();
        float sy = ((centerY + world * 0.5f) - y) / world * getHeight();
        return new float[]{sx, sy};
    }

    private static float f(String value) {
        try { return Float.parseFloat(value); } catch (NumberFormatException error) { return 0f; }
    }

    private static int i(String value) {
        try { return Integer.parseInt(value); } catch (NumberFormatException error) { return 255; }
    }
}
