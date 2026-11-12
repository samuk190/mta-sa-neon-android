package com.worldrpg.engine;

import android.content.Context;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

/** Reads the engine's own data directory, then packaged assets, without legacy shared-storage assumptions. */
public final class EngineAssetLoader {
    private static final int MAX_RAW_BYTES = 64 * 1024 * 1024;

    private EngineAssetLoader() {}

    public static File resolveFile(String baseDirectory, String name) throws IOException {
        if (baseDirectory == null || baseDirectory.length() == 0 || name == null
                || name.length() == 0 || name.indexOf('\0') >= 0) {
            throw new IOException("Empty or invalid engine file path");
        }
        File base = new File(baseDirectory).getCanonicalFile();
        File file = new File(name.replace('\\', '/'));
        if (!file.isAbsolute()) file = new File(base, file.getPath());
        file = file.getCanonicalFile();
        if (!file.getPath().startsWith(base.getPath() + File.separator)) {
            throw new IOException("Engine file is outside its data directory");
        }
        return file;
    }

    public static InputStream open(Context context, String baseDirectory, String name) throws IOException {
        if (name == null || name.length() == 0 || name.indexOf('\0') >= 0) {
            throw new IOException("Empty or invalid engine asset name");
        }
        String normalized = name.replace('\\', '/');
        File base = new File(baseDirectory == null || baseDirectory.length() == 0
                ? context.getFilesDir().getAbsolutePath() : baseDirectory).getCanonicalFile();
        File file = new File(normalized);
        if (!file.isAbsolute()) file = new File(base, normalized);
        file = file.getCanonicalFile();
        // Native filenames are not a grant to read outside this game's data tree.
        if (file.getPath().startsWith(base.getPath() + File.separator) && file.isFile()) {
            return new FileInputStream(file);
        }
        if (new File(normalized).isAbsolute() || normalized.equals("..")
                || normalized.startsWith("../") || normalized.contains("/../")) {
            throw new IOException("Engine asset is outside its data directory: " + name);
        }
        return context.getAssets().open(normalized);
    }

    public static byte[] read(Context context, String baseDirectory, String name) throws IOException {
        try (InputStream input = open(context, baseDirectory, name);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] chunk = new byte[16384];
            int count;
            while ((count = input.read(chunk)) != -1) {
                if (count == 0) continue;
                if (output.size() > MAX_RAW_BYTES - count) {
                    throw new IOException("Engine raw asset exceeds 64 MiB: " + name);
                }
                output.write(chunk, 0, count);
            }
            return output.toByteArray();
        }
    }
}
