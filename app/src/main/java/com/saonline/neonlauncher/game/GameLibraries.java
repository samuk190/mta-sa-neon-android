package com.saonline.neonlauncher.game;

import android.content.Context;
import android.util.Log;

import com.saonline.neonlauncher.R;

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
 * The CDN installs the libraries under {@code lib/} in the active game folder. A local
 * installation can supply the same files, and developer builds can package them. The loader
 * stages external libraries in private storage and loads libGTASA.so after its dependencies.
 */
final class GameLibraries {
    private static final String TAG = "GameLibraries";
    // Load order: dependencies first.
    static final String[] FILES = {"libOpenAL64.so", "libSCAnd.so", "libGTASA.so"};

    private GameLibraries() {}

    static boolean areAvailable(Context context, File gameDirectory, File localAssetsFolder) {
        File packaged = new File(context.getApplicationInfo().nativeLibraryDir, "libGTASA.so");
        return packaged.isFile() || findSource(gameDirectory, localAssetsFolder) != null;
    }

    /** Loads libGTASA (and its two dependencies). Throws with a player-readable message when missing. */
    static void load(Context context, File gameDirectory, File localAssetsFolder, boolean usesCdn) throws IOException {
        try {
            System.loadLibrary("GTASA");
            Log.i(TAG, "libGTASA.so loaded from the APK");
            return;
        } catch (UnsatisfiedLinkError notPackaged) {
            Log.i(TAG, "libGTASA.so is not in the APK; loading it from the game folder");
        }

        File source = findSource(gameDirectory, localAssetsFolder);
        if (source == null) {
            if (usesCdn) {
                throw new IOException(context.getString(R.string.engine_libs_missing_cdn));
            }
            String where = (localAssetsFolder != null ? localAssetsFolder : gameDirectory) + File.separator + "lib";
            throw new IOException(context.getString(R.string.engine_libs_missing_local, where));
        }

        File target = new File(context.getFilesDir(), "engine_libs");
        if (!target.isDirectory() && !target.mkdirs()) {
            throw new IOException(context.getString(R.string.engine_file_write_failed, target));
        }
        for (String name : FILES) {
            File from = new File(source, name);
            File to = new File(target, name);
            if (!isElf(from)) throw new IOException(context.getString(R.string.engine_invalid_lib, from));
            if (!to.isFile() || to.length() != from.length() || to.lastModified() < from.lastModified()) {
                copy(context, from, to);
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

    private static void copy(Context context, File from, File to) throws IOException {
        File partial = new File(to.getPath() + ".part");
        try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(partial)) {
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
        }
        if (!partial.renameTo(to)) throw new IOException(context.getString(R.string.engine_file_write_failed, to));
        to.setReadable(true, true);
        to.setLastModified(from.lastModified());
    }
}
