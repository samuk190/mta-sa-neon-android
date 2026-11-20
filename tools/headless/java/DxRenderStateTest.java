package com.worldrpg.neonlauncher.game;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.Set;

/** Queue ordering and versioned font cache checks, without Android. */
public final class DxRenderStateTest {
    private static int checks;
    private static void check(boolean value) {
        checks++;
        if (!value) throw new AssertionError("DX state check " + checks);
    }
    private static Set<String> targets(String commands) {
        Set<String> alive = new HashSet<>();
        for (String line : commands.split("\n")) {
            if (line.startsWith("G ")) alive.add(line.split(" ")[1]);
            if (line.startsWith("D ")) alive.remove(line.substring(2));
        }
        return alive;
    }
    public static void main(String[] args) throws Exception {
        DxFrameQueue q = new DxFrameQueue(200000);
        q.offer("B 3\nG 7 16 16 1 0\nR first\nG\nB 0\nR old-screen\n");
        q.offer("R latest-screen\n");
        String commands = q.take();
        check(commands.contains("B 3\nF\nG 7 "));
        check(!commands.contains("old-screen"));
        check(commands.contains("G\nB 0\nF\nR latest-screen"));
        check(q.take().equals("R latest-screen\n")); // Redraw newest screen; do not replay carried commands.

        q.offer("G 8 16 16 1 0\nR target\nG\n");
        q.offer("D 8\n");
        commands = q.take();
        check(commands.indexOf("G 8 ") < commands.indexOf("D 8\n"));
        check(!targets(commands).contains("8"));

        q.offer("G 9 16 16 1 0\nR target\nG\n");
        q.offer("D 9\n");
        q.offer(""); // Even the destruction frame may be replaced before onDraw.
        check(q.hasCommands());
        check(!targets(q.take()).contains("9"));

        q.offer("F 20 0 @font:1:/tmp/font.ttf\nB 3\nG 10 16 16 1 0\nR a\nG 11 16 16 1 0\nR b\nG\n");
        q.offer("B 1\nR screen\n");
        commands = q.take();
        check(commands.contains("B 3\nF 20 0 @font:1:/tmp/font.ttf\nG 10 "));
        check(commands.contains("B 3\nF 20 0 @font:1:/tmp/font.ttf\nG 11 "));
        check(commands.endsWith("G\nB 0\nF\nB 1\nR screen\n"));

        DxFrameQueue tiny = new DxFrameQueue(1);
        tiny.offer("G 12 16 16 1 0\nR oversized\nG\nD 12\n");
        tiny.offer("");
        commands = tiny.take();
        check(!commands.contains("oversized") && commands.contains("D 12\n"));

        Path file = Files.createTempFile("dx-font-version", ".ttf");
        int[] loads = {0};
        DxFontCache<String> fonts = new DxFontCache<>(2, (path, bold) -> {
            loads[0]++;
            try { return new String(Files.readAllBytes(java.nio.file.Paths.get(path)), java.nio.charset.StandardCharsets.UTF_8); }
            catch (java.io.IOException e) { throw new RuntimeException(e); }
        });
        try {
            Files.write(file,"old".getBytes(java.nio.charset.StandardCharsets.UTF_8));
            String first = "@font:1:" + file;
            check(fonts.get(first,false).equals("old"));
            check(fonts.get(first,false).equals("old") && loads[0] == 1);
            Files.write(file,"new".getBytes(java.nio.charset.StandardCharsets.UTF_8));
            check(fonts.get("@font:2:" + file,false).equals("new") && loads[0] == 2);
            check(fonts.get(first,false).equals("old")); // An older queued font still has its own version.
            check(fonts.get("@font:2:" + file,true).equals("new") && loads[0] == 3);
            fonts.get("@font:3:" + file,false);
            check(fonts.get(first,false).equals("new") && loads[0] == 5); // Oldest entry was evicted.
            check(DxFontCache.path("@font:4:/tmp/a:b.ttf").equals("/tmp/a:b.ttf"));
            check(DxFontCache.path(file.toString()).equals(file.toString()));
        } finally { Files.deleteIfExists(file); }
        int[] failedLoads = {0};
        DxFontCache<String> invalid = new DxFontCache<>(2, (path, bold) -> { failedLoads[0]++; return null; });
        check(invalid.get("@font:10:/missing.ttf",false) == null);
        check(invalid.get("@font:10:/missing.ttf",false) == null && failedLoads[0] == 1);
        check(invalid.get("@font:11:/missing.ttf",false) == null && failedLoads[0] == 2);
        check(invalid.pin("@font:11:/missing.ttf",false) == null && failedLoads[0] == 2);

        int[] versions = {1};
        DxFontCache<Integer> liveFonts = new DxFontCache<>(1, (path, bold) -> versions[0]);
        check(liveFonts.pin("@font:1:/face.ttf",false) == 1);
        versions[0] = 2;
        liveFonts.pin("@font:2:/face.ttf",false);
        liveFonts.get("@font:3:/face.ttf",false);
        liveFonts.get("@font:4:/face.ttf",false);
        check(liveFonts.get("@font:1:/face.ttf",false) == 1); // Active faces may exceed the LRU budget.
        liveFonts.releaseInstance("10");
        check(liveFonts.get("@font:1:/face.ttf",false) == 1); // Instance 1 is distinct from 10.
        versions[0] = 3;
        liveFonts.releaseInstance("1");
        liveFonts.get("@font:5:/face.ttf",false);
        check(liveFonts.get("@font:1:/face.ttf",false) == 3);
        check(liveFonts.get("@font:2:/face.ttf",false) == 2); // Releasing another instance preserves this one.
        System.out.println("DxRenderStateTest: " + checks + " checks, 0 failures");
    }
}
