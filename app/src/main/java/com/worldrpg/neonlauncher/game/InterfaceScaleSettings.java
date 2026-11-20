package com.worldrpg.neonlauncher.game;

import android.content.Context;
import android.util.AtomicFile;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** One atomic preference shared by the launcher and :engine, without stale process-local caches. */
public final class InterfaceScaleSettings {
    private InterfaceScaleSettings() {}

    private static AtomicFile file(Context context) {
        return new AtomicFile(new File(context.getFilesDir(), "interface-scale"));
    }

    public static int read(Context context) {
        try (FileInputStream input = file(context).openRead()) {
            byte[] value = new byte[16];
            int length = input.read(value);
            if (length <= 0 || input.read() != -1) return InterfaceScale.DEFAULT_PERCENT;
            return InterfaceScale.clampPercent(Integer.parseInt(
                    new String(value, 0, length, StandardCharsets.US_ASCII).trim()));
        } catch (IOException | NumberFormatException error) {
            return InterfaceScale.DEFAULT_PERCENT;
        }
    }

    public static boolean save(Context context, int percent) {
        AtomicFile file = file(context);
        FileOutputStream output = null;
        try {
            output = file.startWrite();
            output.write(Integer.toString(InterfaceScale.clampPercent(percent)).getBytes(StandardCharsets.US_ASCII));
            file.finishWrite(output);
            return true;
        } catch (IOException error) {
            file.failWrite(output);
            Log.w("InterfaceScale", "Cannot save interface scale", error);
            return false;
        }
    }
}
