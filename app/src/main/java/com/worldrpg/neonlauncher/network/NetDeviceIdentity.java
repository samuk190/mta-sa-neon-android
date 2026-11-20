package com.worldrpg.neonlauncher.network;

import android.content.Context;
import android.provider.Settings;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.UUID;

/** Stable app-scoped network identifier, independent of the player's IP and SA:O session. */
public final class NetDeviceIdentity {
    private NetDeviceIdentity() {}

    public static String get(Context context) {
        String seed = Settings.Secure.getString(context.getContentResolver(), Settings.Secure.ANDROID_ID);
        if (seed == null || seed.isEmpty()) {
            android.content.SharedPreferences prefs = context.getSharedPreferences("neon_network_identity", Context.MODE_PRIVATE);
            seed = prefs.getString("seed", null);
            if (seed == null) {
                seed = UUID.randomUUID().toString();
                if (!prefs.edit().putString("seed", seed).commit()) throw new IllegalStateException("Unable to save network identity");
            }
        }
        try {
            byte[] digest = MessageDigest.getInstance("MD5").digest(("neon-android-v1:" + context.getPackageName() + ":" + seed).getBytes(StandardCharsets.UTF_8));
            char[] hex = "0123456789ABCDEF".toCharArray();
            char[] result = new char[32];
            for (int i=0; i<digest.length; ++i) {
                result[i*2] = hex[(digest[i] & 255) >>> 4];
                result[i*2+1] = hex[digest[i] & 15];
            }
            return new String(result);
        } catch (java.security.NoSuchAlgorithmException impossible) {
            throw new IllegalStateException("Network identity is unavailable", impossible);
        }
    }
}
