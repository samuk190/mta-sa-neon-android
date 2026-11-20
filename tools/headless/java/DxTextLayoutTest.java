package com.worldrpg.neonlauncher.game;

/** Run with javac/java; Android rendering itself still requires a device. */
public final class DxTextLayoutTest {
    private static int checks;
    private static void check(boolean value) {
        checks++;
        if (!value) throw new AssertionError("DX layout check " + checks);
    }
    public static void main(String[] args) {
        // A 40px label in a 100px box, and an overflowing 160px label.
        check(DxTextLayout.offset(100,40,0) == 0);
        check(DxTextLayout.offset(100,40,1) == 30);
        check(DxTextLayout.offset(100,40,2) == 60);
        check(DxTextLayout.offset(100,160,1) == -30);
        check(DxTextLayout.offset(100,160,2) == -60);
        // Independent X/Y scale: 40px measured at Y size stretches to 80px.
        check(DxTextLayout.offset(100,40 * (2f / 1f),1) == 10);
        check(DxTextLayout.width(0) == 1);
        check(DxTextLayout.width(31.2f) == 32);
        check(DxTextLayout.width(1000000) == 32768);
        check(DxTextLayout.width(Float.POSITIVE_INFINITY) == 32768);
        check(DxTextLayout.decode("a\\nb").equals("a\nb"));
        check(DxTextLayout.decode("a\\r\\nb").equals("a\r\nb"));
        check(DxTextLayout.decode("a\\tb").equals("a\tb"));
        check(DxTextLayout.decode("a\\\\nb").equals("a\\nb"));
        check(DxTextLayout.decode("é🙂\\nΩ").equals("é🙂\nΩ"));
        check(DxTextLayout.decode("literal\\q").equals("literal\\q"));
        check(DxTextLayout.decode("end\\").equals("end\\"));
        check(DxTextLayout.decode("").isEmpty());
        // The last logical line occupies lineHeight, even for one short glyph.
        check(DxTextLayout.blockHeight("x",100,15) == 100);
        check(DxTextLayout.blockHeight("x\ny",100,30) == 200);
        check(DxTextLayout.blockHeight("x\n",100,30) == 200);
        check(DxTextLayout.blockHeight("x",0,15) == 15);
        check(DxTextLayout.offset(200,DxTextLayout.blockHeight("x",100,15),2) == 100);
        check(DxTextLayout.offset(200,DxTextLayout.blockHeight("x",100,15),1) == 50);
        System.out.println("DxTextLayoutTest: " + checks + " checks, 0 failures");
    }
}
