package com.worldrpg.neonlauncher.game;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * Loads the GTA San Andreas Mobile 2.10 native libraries.
 *
 * A developer build may package them (app/src/main/jniLibs/arm64-v8a, git-ignored). The public
 * release APK does not ship them: they are the game's own code, so each player copies them from
 * their own copy of the game into the game folder, under {@code lib/}. Android will not load code
 * from shared storage, so the files are copied into the app's private storage first and loaded
 * from there. libGTASA.so needs libSCAnd.so and libOpenAL64.so (DT_NEEDED), so those load first;
 * the linker then matches them by soname.
 */
final class GameLibraries {
    private static final String TAG = "GameLibraries";
    // Load order: dependencies first.
    static final String[] FILES = {"libOpenAL64.so", "libSCAnd.so", "libGTASA.so"};

    private GameLibraries() {}

    /** Loads libGTASA (and its two dependencies). Throws with a player-readable message when missing. */
    static void load(Context context, File gameDirectory, File localAssetsFolder) throws IOException {
        try {
            System.loadLibrary("GTASA");
            Log.i(TAG, "libGTASA.so loaded from the APK");
            return;
        } catch (UnsatisfiedLinkError notPackaged) {
            Log.i(TAG, "libGTASA.so is not in the APK; loading it from the game folder");
        }

        File source = findSource(gameDirectory, localAssetsFolder);
        if (source == null) {
            String where = (localAssetsFolder != null ? localAssetsFolder : gameDirectory) + File.separator + "lib";
            throw new IOException("Game libraries missing. Copy libGTASA.so, libSCAnd.so and libOpenAL64.so from "
                    + "lib/arm64-v8a/ of your GTA San Andreas 2.10 APK to " + where);
        }

        File target = new File(context.getFilesDir(), "engine_libs");
        if (!target.isDirectory() && !target.mkdirs()) throw new IOException("Cannot create " + target);
        for (String name : FILES) {
            File from = new File(source, name);
            File to = new File(target, name);
            if (!isElf(from)) throw new IOException(from + " is not a valid native library.");
            if (!to.isFile() || to.length() != from.length() || to.lastModified() < from.lastModified()) {
                copy(from, to);
            }
            System.load(to.getAbsolutePath());
            Log.i(TAG, "Loaded " + name + " from " + source);
        }
    }

    private static File findSource(File gameDirectory, File localAssetsFolder) {
        List<File> candidates = new ArrayList<>();
        for (File root : new File[] {gameDirectory, localAssetsFolder}) {
            if (root == null) continue;
            candidates.add(new File(root, "lib"));
            candidates.add(new File(root, "lib/arm64-v8a"));
        }
        for (File dir : candidates) {
            boolean all = true;
            for (String name : FILES) all &= new File(dir, name).isFile();
            if (all) return dir;
        }
        return null;
    }

    private static boolean isElf(File file) {
        byte[] head = new byte[5];
        try (InputStream in = new FileInputStream(file)) {
            // 0x7F 'E' 'L' 'F', class 2 = 64-bit.
            return in.read(head) == 5 && head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F'
                    && head[4] == 2;
        } catch (IOException e) {
            return false;
        }
    }

    private static void copy(File from, File to) throws IOException {
        File partial = new File(to.getPath() + ".part");
        try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(partial)) {
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
        }
        if (!partial.renameTo(to)) throw new IOException("Cannot write " + to);
        to.setReadable(true, true);
        to.setLastModified(from.lastModified());
    }
}
