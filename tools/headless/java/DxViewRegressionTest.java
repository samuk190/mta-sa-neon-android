package com.saonline.neonlauncher.game;

import android.graphics.Canvas;
import android.graphics.Typeface;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;

/** Executes the real MtaDxView methods against small Android test doubles. */
public final class DxViewRegressionTest {
    private static int checks;
    private static void check(boolean value) {
        checks++;
        if (!value) throw new AssertionError("DX view check " + checks);
    }
    public static void main(String[] args) throws Exception {
        MtaDxView view = new MtaDxView(null);
        view.setFrame("B 3\nG 7 16 16 1 0\nR 0 0 8 8 4294967295\nG\nB 0\n");
        view.setFrame("R 0 0 1 1 4294967295\n");
        view.onDraw(new Canvas());
        check(Canvas.draws.get(0).equals("RT:SRC"));
        check(Canvas.draws.get(1).equals("screen:SRC_OVER"));

        Field field = MtaDxView.class.getDeclaredField("MEMORY");
        field.setAccessible(true);
        Map<?,?> memory = (Map<?,?>)field.get(null);
        view.setFrame("G 8 16 16 1 0\nR 0 0 8 8 4294967295\nG\n");
        view.setFrame("D 8\n");
        view.onDraw(new Canvas());
        check(!memory.containsKey("rt:8"));
        view.setFrame("G 9 16 16 1 0\nR 0 0 8 8 4294967295\nG\n");
        view.setFrame("D 9\n");
        view.setFrame("");
        check(view.getVisibility() == android.view.View.VISIBLE);
        view.onDraw(new Canvas());
        check(!memory.containsKey("rt:9"));

        Canvas color = new Canvas();
        view.setFrame("B 0\nR 0 0 8 8 4278190335\nB 1\nR 0 0 8 8 4294901760\n");
        view.onDraw(color);
        check(color.pixel == 0xffff0000); // Opaque red replaces blue; saturated ADD would yield magenta.

        Path font = Files.createTempFile("dx-view-font", ".ttf");
        Method lookup = MtaDxView.class.getDeclaredMethod("typefaceFor",String.class,boolean.class);
        lookup.setAccessible(true);
        try {
            Files.write(font,"version-one".getBytes(StandardCharsets.UTF_8));
            Typeface old = (Typeface)lookup.invoke(null,"@font:1:" + font,false);
            Files.write(font,"version-two".getBytes(StandardCharsets.UTF_8));
            Typeface fresh = (Typeface)lookup.invoke(null,"@font:2:" + font,false);
            check(old.version.equals("version-one"));
            check(fresh.version.equals("version-two"));
            check(old != fresh);
        } finally { Files.deleteIfExists(font); }

        Canvas bottom = new Canvas();
        view.setFrame("X 0 0 100 300 1 1 4294967295 1 0 2 0 0 0 0 0 0 100 x\\ny\n");
        view.onDraw(bottom);
        check(bottom.textY.size() == 2);
        check(bottom.textY.get(0) == 100f && bottom.textY.get(1) == 200f);
        Canvas center = new Canvas();
        view.setFrame("X 0 0 100 300 1 1 4294967295 1 0 1 0 0 0 0 0 0 100 x\n");
        view.onDraw(center);
        check(center.textY.size() == 1 && center.textY.get(0) == 100f);

        check(MtaDxView.fontHeight("@gui:default-normal",12,false) > 0f);
        check(MtaDxView.fontHeight("@gui:default-normal",24,false) == 2f * MtaDxView.fontHeight("@gui:default-normal",12,false));
        check(MtaDxView.fontHeight("@font:invalid:/does-not-exist.ttf",12,false) == 0f);
        check(MtaDxView.fontHeight("@gui:default-normal",Float.NaN,false) == 0f);

        Canvas label = new Canvas();
        view.setFrame("F 12 0 @gui:default-normal\nY 10 20 110 120 4294967295 2 2 0 abcd\nF\nX 0 0 100 30 1 1 4294967295 0 0 0 0 0 0 0 0 0 0 DX\n");
        view.onDraw(label);
        check(label.texts.size() == 2);
        Canvas.TextDraw gui = label.texts.get(0), dx = label.texts.get(1);
        check(gui.size == 12f && gui.face == Typeface.DEFAULT);
        check(gui.alignment == android.text.Layout.Alignment.ALIGN_OPPOSITE && gui.width == 24);
        check(gui.x == 86f && Math.abs(gui.y - 105.6f) < 0.001f);
        check(label.clips.size() == 1); // Labels always clip, independently of the later DX call.
        check(dx.size == 15f && dx.face == Typeface.DEFAULT_BOLD && dx.x == 0 && dx.y == 0);

        Canvas wrapping = new Canvas();
        view.setFrame("F 12 1 @gui:default-bold-small\nY 10 20 34 120 4294967295 1 0 1 abcde\nF\n");
        view.onDraw(wrapping);
        gui = wrapping.texts.get(0);
        check(gui.face == Typeface.DEFAULT_BOLD && gui.size == 12f);
        check(gui.width == 24 && gui.lines == 2 && gui.x == 10f && gui.y == 20f);
        check(gui.alignment == android.text.Layout.Alignment.ALIGN_CENTER);

        Canvas literal = new Canvas();
        view.setFrame("F 12 0 @gui:default-normal\nY 0 0 200 200 4294967295 0 1 0 #ff0000\\n2\\n3\\n4\\n5\\\\n\nF\n");
        view.onDraw(literal);
        gui = literal.texts.get(0);
        check(gui.lines == 5 && gui.value.equals("#ff0000\n2\n3\n4\n5\\n"));
        check(gui.y == 64f); // 5 logical lines at 14.4 px have a 72 px block, centered in 200.
        check(gui.alignment == android.text.Layout.Alignment.ALIGN_NORMAL);
        Canvas oversized = new Canvas();
        view.setFrame("F 12 0 @gui:default-normal\nY 0 0 100 10 4294967295 0 1 0 x\nF\n");
        view.onDraw(oversized);
        check(oversized.texts.get(0).y == -1f); // CEGUI rounds by truncating offset + 0.5, even below zero.

        Canvas fieldCanvas = new Canvas();
        view.setFrame("F 200 0 @gui:default-normal\nT 10 20 110 40 1.00 4294967295 0 large-font\nF\nX 0 0 100 30 1 1 4294967295 0 0 0 0 0 0 0 0 0 0 next\n");
        view.onDraw(fieldCanvas);
        check(fieldCanvas.clips.size() == 1 && fieldCanvas.clips.get(0).left == 10f && fieldCanvas.clips.get(0).bottom == 40f);
        check(fieldCanvas.texts.size() == 2 && fieldCanvas.texts.get(1).x == 0f && fieldCanvas.texts.get(1).y == 0f);

        Path pinnedFile = Files.createTempFile("gui-font-live", ".ttf");
        String liveSource = "@font:9000:" + pinnedFile;
        try {
            Files.write(pinnedFile,"pinned-one".getBytes(StandardCharsets.UTF_8));
            check(MtaDxView.fontHeight(liveSource,12f,false) > 0f);
            Typeface live = (Typeface)lookup.invoke(null,liveSource,false);
            Files.write(pinnedFile,"pinned-two".getBytes(StandardCharsets.UTF_8));
            for (int i = 0; i < 80; i++) lookup.invoke(null,"@font:" + (9100 + i) + ":" + pinnedFile,false);
            check(lookup.invoke(null,liveSource,false) == live && live.version.equals("pinned-one"));
            view.setFrame("D 9000\n");
            view.setFrame(""); // A replaced deletion frame still releases its GUI font.
            view.onDraw(new Canvas());
            for (int i = 0; i < 80; i++) lookup.invoke(null,"@font:" + (9200 + i) + ":" + pinnedFile,false);
            check(lookup.invoke(null,liveSource,false) != live);
        } finally { Files.deleteIfExists(pinnedFile); }
        check(InterfaceScale.logicalSize(3120,3.75f,100)==832);
        check(InterfaceScale.logicalSize(1440,3.75f,100)==384);
        check(InterfaceScale.clampPercent(99)==100 && InterfaceScale.clampPercent(-1)==50);
        check(InterfaceScale.clampPercent(900)==150);
        check(InterfaceScale.logicalSize(1920,Float.NaN,100)==1920);
        for(int percent=50;percent<=150;percent+=5) {
            int logical=InterfaceScale.logicalSize(3120,3.75f,percent);
            float cursor=InterfaceScale.toLogical(1560,3120,logical);
            check(Math.abs(cursor/logical-0.5f)<0.00001f);
        }
        MtaDxView scaled=new MtaDxView(null);
        Canvas scaledScreen=new Canvas();
        Canvas.rectangles.clear();
        scaled.setFrame("C\nV 832 384\nG 700 64 64 1 0\nR 0 0 8 8 4294967295\nG\nR 100 100 20 20 4294967295\nF 12 0 @gui:default-normal\nY 100 100 200 140 4294967295 0 0 0 gui\nF\nX 100 150 300 180 1 1 4294967295 0 0 0 0 0 0 0 0 0 0 dx\n");
        scaled.onDraw(scaledScreen);
        check(Canvas.rectangles.size()==2);
        check(Canvas.rectangles.get(0).target && Canvas.rectangles.get(0).right==8f);
        check(!Canvas.rectangles.get(1).target && Canvas.rectangles.get(1).right==450f);
        check(scaledScreen.texts.get(0).size==45f && scaledScreen.texts.get(1).size==56.25f);
        float[] touched=new float[3];
        scaled.setCursorListener((action,x,y)->{touched[0]=action;touched[1]=x;touched[2]=y;});
        android.view.MotionEvent touch=android.view.MotionEvent.obtain(0,0,0,1560,720,0);
        check(scaled.onTouchEvent(touch));touch.recycle();
        check(touched[0]==0 && touched[1]==416f && touched[2]==192f);
        // Retained old offscreen work must not carry an old viewport into the latest frame.
        scaled.setFrame("V 832 384\nG 701 64 64 1 0\nR 0 0 8 8 4294967295\nG\n");
        scaled.setFrame("C\nV 1664 768\nX 0 0 100 30 1 1 4294967295 0 0 0 0 0 0 0 0 0 0 new\n");
        Canvas latest=new Canvas();scaled.onDraw(latest);
        check(latest.texts.get(0).size==28.125f);
        touch=android.view.MotionEvent.obtain(0,0,1,1560,720,0);
        scaled.onTouchEvent(touch);touch.recycle();
        check(touched[0]==1 && touched[1]==832f && touched[2]==384f);

        check(HudLayout.left(HudLayout.TOP_LEFT, 800, 200, 12) == 12);
        check(HudLayout.left(HudLayout.BOTTOM_RIGHT, 800, 200, 12) == 588);
        check(HudLayout.top(HudLayout.BOTTOM_RIGHT, 400, 100, 12) == 288);
        check(HudLayout.besideChat(12, 40, 12, 140, HudLayout.TOP_LEFT, 400, 12, 8) == 160);
        check(HudLayout.besideChat(348, 40, 288, 100, HudLayout.BOTTOM_RIGHT, 400, 12, 8) == 240);
        scaled.setKeyboardScale(HudLayout.keyboardScale(1440, 720));
        Canvas.rectangles.clear();
        scaled.setFrame("C\nV 1560 720\nR 500 440 300 200 4294967295\n");
        scaled.onDraw(new Canvas());
        Canvas.RectDraw form = Canvas.rectangles.get(0);
        check(form.left == 1279f && form.bottom == 640f);
        touch = android.view.MotionEvent.obtain(0, 0, 0, 1300, 500, 0);
        check(scaled.onTouchEvent(touch)); touch.recycle();
        check(touched[0] == 0 && touched[1] == 520f && touched[2] == 500f);
        check(HudLayout.keyboardScale(1440, 0) == 1f);

        System.out.println("DxViewRegressionTest: " + checks + " checks, 0 failures");
    }
}
