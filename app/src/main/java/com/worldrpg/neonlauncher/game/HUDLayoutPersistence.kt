package com.worldrpg.neonlauncher.game

import android.content.Context
import android.content.SharedPreferences

/**
 * Persists HUD element layout positions and visibility to SharedPreferences.
 * Allows admin commands to save/restore custom HUD layouts across app restarts.
 */
class HUDLayoutPersistence(context: Context) {

    private val prefs: SharedPreferences = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    /**
     * Saves a HUD element's layout parameters.
     */
    fun saveElementParams(elementName: String, x: Float, y: Float, width: Float, height: Float, visible: Boolean) {
        prefs.edit()
            .putFloat("${elementName}_x", x)
            .putFloat("${elementName}_y", y)
            .putFloat("${elementName}_width", width)
            .putFloat("${elementName}_height", height)
            .putBoolean("${elementName}_visible", visible)
            .apply()
    }

    /**
     * Loads saved HUD element parameters, returning defaults if not previously saved.
     */
    fun loadElementParams(elementName: String): ElementLayout {
        return ElementLayout(
            x = prefs.getFloat("${elementName}_x", 0f),
            y = prefs.getFloat("${elementName}_y", 0f),
            width = prefs.getFloat("${elementName}_width", 1f),
            height = prefs.getFloat("${elementName}_height", 1f),
            visible = prefs.getBoolean("${elementName}_visible", true)
        )
    }

    /**
     * Checks if a specific element has saved layout data.
     */
    fun hasSavedLayout(elementName: String): Boolean {
        return prefs.contains("${elementName}_x")
    }

    /**
     * Loads all saved HUD element layouts as a map of element name to layout.
     */
    fun loadAllLayouts(): Map<String, ElementLayout> {
        val layouts = mutableMapOf<String, ElementLayout>()
        val keys = prefs.all.keys
        val elementNames = keys
            .filter { it.endsWith("_x") }
            .map { it.removeSuffix("_x") }
        for (name in elementNames) {
            if (hasSavedLayout(name)) {
                layouts[name] = loadElementParams(name)
            }
        }
        return layouts
    }

    /**
     * Clears all saved HUD layout data.
     */
    fun clearAll() {
        prefs.edit().clear().apply()
    }

    data class ElementLayout(
        val x: Float,
        val y: Float,
        val width: Float,
        val height: Float,
        val visible: Boolean
    )

    companion object {
        private const val PREFS_NAME = "hud_layout_prefs"
    }
}