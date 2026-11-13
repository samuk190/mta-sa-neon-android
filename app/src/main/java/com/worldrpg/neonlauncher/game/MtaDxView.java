package com.worldrpg.neonlauncher.game;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.view.View;

import java.util.HashMap;

/** Draws the rectangles, text, and images produced by client resource scripts. */
public final class MtaDxView extends View {
    private String frame = "";
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint image = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final HashMap<String, Bitmap> images = new HashMap<>();
    private final Rect source = new Rect();
    private final RectF destination = new RectF();

    public MtaDxView(android.content.Context context) {
        super(context);
        setClickable(false);
        text.setTypeface(Typeface.DEFAULT_BOLD);
        text.setShadowLayer(2f, 1f, 1f, 0xCC000000);
    }

    /** Receives touches while a script has the cursor shown. action: 0 down, 1 up, 2 move. */
    public interface CursorListener {
        void onCursor(int action, float x, float y);
    }

    private CursorListener cursorListener;
    private boolean cursorShown;

    public void setCursorListener(CursorListener listener) {
        cursorListener = listener;
    }

    // Desktop: with showCursor(true) the mouse drives the interface and the game gets no input.
    // Here the overlay takes the touch and reports it at the coordinates the frame is drawn in.
    @Override
    public boolean onTouchEvent(android.view.MotionEvent event) {
        if (!cursorShown || cursorListener == null) return false;
        switch (event.getActionMasked()) {
            case android.view.MotionEvent.ACTION_DOWN:
                cursorListener.onCursor(0, event.getX(), event.getY());
                break;
            case android.view.MotionEvent.ACTION_MOVE:
                cursorListener.onCursor(2, event.getX(), event.getY());
                break;
            case android.view.MotionEvent.ACTION_UP:
            case android.view.MotionEvent.ACTION_CANCEL:
                cursorListener.onCursor(1, event.getX(), event.getY());
                break;
            default:
                break;
        }
        return true;
    }

    public void setFrame(String commands) {
        frame = commands == null ? "" : commands;
        // A first line "C" means the cursor is showing (LuaVMStub::pumpDx).
        cursorShown = frame.startsWith("C\n") || frame.equals("C");
        setVisibility(frame.isEmpty() ? GONE : VISIBLE);
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (frame.isEmpty()) return;
        String[] lines = frame.split("\n");
        for (String line : lines) {
            if (line.startsWith("R ")) drawRect(canvas, line.substring(2));
            else if (line.startsWith("T ")) drawText(canvas, line.substring(2));
            else if (line.startsWith("I ")) drawImage(canvas, line.substring(2));
        }
    }

    // "I dx dy dw dh sx sy sw sh color rot ox oy path" from LuaVMStub.
    // sw or sh below zero means the whole PNG (dxDrawImage). Otherwise the
    // source rectangle is in PNG pixels (dxDrawImageSection).
    private void drawImage(Canvas canvas, String body) {
        String[] p = body.split(" ", 13);
        if (p.length < 13 || p[12].isEmpty()) return;
        Bitmap bitmap = bitmapFor(p[12]);
        if (bitmap == null) return;
        float x = f(p[0]), y = f(p[1]), w = f(p[2]), h = f(p[3]);
        float sx = f(p[4]), sy = f(p[5]), sw = f(p[6]), sh = f(p[7]);
        if (sw < 0f || sh < 0f) source.set(0, 0, bitmap.getWidth(), bitmap.getHeight());
        else {
            source.set(Math.round(sx), Math.round(sy), Math.round(sx + sw), Math.round(sy + sh));
            if (!source.intersect(0, 0, bitmap.getWidth(), bitmap.getHeight())) return;
        }
        if (source.isEmpty()) return;
        int color = (int) l(p[8]);
        image.setColorFilter(color == 0xFFFFFFFF
                ? null
                : new PorterDuffColorFilter(color, PorterDuff.Mode.MULTIPLY));
        destination.set(x, y, x + w, y + h);
        float rotation = f(p[9]);
        canvas.save();
        if (rotation != 0f) canvas.rotate(rotation, x + w * 0.5f + f(p[10]), y + h * 0.5f + f(p[11]));
        canvas.drawBitmap(bitmap, source, destination, image);
        canvas.restore();
    }

    private Bitmap bitmapFor(String path) {
        Bitmap cached = images.get(path);
        if (cached != null) return cached.isRecycled() ? null : cached;
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inScaled = false;
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        Bitmap decoded = BitmapFactory.decodeFile(path, options);
        if (decoded != null) images.put(path, decoded);
        return decoded;
    }

    private void drawRect(Canvas canvas, String body) {
        String[] p = body.split(" ");
        if (p.length < 5) return;
        float x = f(p[0]), y = f(p[1]), w = f(p[2]), h = f(p[3]);
        fill.setColor((int) l(p[4]));
        canvas.drawRect(x, y, x + w, y + h, fill);
    }

    private void drawText(Canvas canvas, String body) {
        String[] p = body.split(" ", 8);
        if (p.length < 8) return;
        float x = f(p[0]), y = f(p[1]), right = f(p[2]), bottom = f(p[3]);
        float scale = f(p[4]);
        text.setColor((int) l(p[5]));
        // The draw line is in the pixels that guiGetScreenSize reports (no density factor).
        // Desktop draws the default font at 15 px times the scale (CGraphics fontInfos).
        text.setTextSize(15f * scale);
        CharSequence shown = "1".equals(p[6]) ? MtaChatView.colorize(p[7]) : p[7];
        int width = Math.max(1, (int) (right - x));
        StaticLayout layout = StaticLayout.Builder.obtain(shown, 0, shown.length(), text, width)
                .setMaxLines(3).build();
        canvas.save();
        canvas.translate(x, y + Math.max(0, (bottom - y - layout.getHeight()) / 2f));
        layout.draw(canvas);
        canvas.restore();
    }

    private static float f(String value) {
        try { return Float.parseFloat(value); } catch (NumberFormatException error) { return 0f; }
    }

    private static long l(String value) {
        try { return Long.parseLong(value); } catch (NumberFormatException error) { return Color.WHITE; }
    }
}
