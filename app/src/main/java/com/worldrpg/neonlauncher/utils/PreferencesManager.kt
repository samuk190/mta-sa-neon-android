package com.worldrpg.neonlauncher.utils

import android.content.Context
import android.content.SharedPreferences

/**
 * Typed SharedPreferences wrapper for launcher settings.
 * All keys and values are ASCII-safe per project rules.
 */
class PreferencesManager(context: Context) {

    private val prefs: SharedPreferences = context.getSharedPreferences(
        PREFS_NAME,
        Context.MODE_PRIVATE
    )

    // --- Username ---
    var username: String
        get() = prefs.getString(KEY_USERNAME, DEFAULT_USERNAME) ?: DEFAULT_USERNAME
        set(value) = prefs.edit().putString(KEY_USERNAME, value).apply()

    // --- Graphics Quality (0=Low, 1=Medium, 2=High) ---
    var graphicsQuality: Int
        get() = prefs.getInt(KEY_GRAPHICS_QUALITY, DEFAULT_GRAPHICS_QUALITY)
        set(value) = prefs.edit().putInt(KEY_GRAPHICS_QUALITY, value.coerceIn(0, 2)).apply()

    // --- Audio Volume (0-100) ---
    var audioVolume: Int
        get() = prefs.getInt(KEY_AUDIO_VOLUME, DEFAULT_AUDIO_VOLUME)
        set(value) = prefs.edit().putInt(KEY_AUDIO_VOLUME, value.coerceIn(0, 100)).apply()

    // --- Touch Sensitivity (1-10) ---
    var touchSensitivity: Int
        get() = prefs.getInt(KEY_TOUCH_SENSITIVITY, DEFAULT_TOUCH_SENSITIVITY)
        set(value) = prefs.edit().putInt(KEY_TOUCH_SENSITIVITY, value.coerceIn(1, 10)).apply()

    // --- Asset Storage Path (set by NativeAssetBridge after successful sync) ---
    var assetStoragePath: String
        get() = prefs.getString(KEY_ASSET_STORAGE_PATH, "") ?: ""
        set(value) = prefs.edit().putString(KEY_ASSET_STORAGE_PATH, value).apply()

    // --- Favorites (stored as comma-separated ip:port keys) ---
    fun getFavorites(): Set<String> {
        return prefs.getStringSet(KEY_FAVORITES, emptySet()) ?: emptySet()
    }

    fun addFavorite(key: String) {
        val current = getFavorites().toMutableSet()
        current.add(key)
        prefs.edit().putStringSet(KEY_FAVORITES, current).apply()
    }

    fun removeFavorite(key: String) {
        val current = getFavorites().toMutableSet()
        current.remove(key)
        prefs.edit().putStringSet(KEY_FAVORITES, current).apply()
    }

    fun isFavorite(key: String): Boolean {
        return getFavorites().contains(key)
    }

    companion object {
        private const val PREFS_NAME = "worldrpg_neon_prefs"

        private const val KEY_USERNAME = "username"
        private const val KEY_GRAPHICS_QUALITY = "graphics_quality"
        private const val KEY_AUDIO_VOLUME = "audio_volume"
        private const val KEY_TOUCH_SENSITIVITY = "touch_sensitivity"
        private const val KEY_FAVORITES = "favorites"
        private const val KEY_ASSET_STORAGE_PATH = "asset_storage_path"

        private const val DEFAULT_USERNAME = "Player"
        private const val DEFAULT_GRAPHICS_QUALITY = 1 // Medium
        private const val DEFAULT_AUDIO_VOLUME = 80
        private const val DEFAULT_TOUCH_SENSITIVITY = 5
    }
}