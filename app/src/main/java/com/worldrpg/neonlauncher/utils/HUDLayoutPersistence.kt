package com.worldrpg.neonlauncher.utils

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import android.util.Log

/**
 * Persists HUD element layout parameters to SQLite `hud_layout` table
 * per AGENTS.md directive 10. Survives app restarts and allows admin
 * calibration to persist across sessions.
 *
 * Table schema:
 *   hud_layout (
 *     element_name TEXT PRIMARY KEY,
 *     x REAL NOT NULL,
 *     y REAL NOT NULL,
 *     width REAL NOT NULL,
 *     height REAL NOT NULL,
 *     visible INTEGER NOT NULL DEFAULT 1,
 *     updated_at INTEGER NOT NULL
 *   )
 */
class HUDLayoutPersistence(context: Context) :
    SQLiteOpenHelper(context, DATABASE_NAME, null, DATABASE_VERSION) {

    companion object {
        private const val TAG = "HUDLayoutDB"
        private const val DATABASE_NAME = "worldrpg_neon.db"
        private const val DATABASE_VERSION = 1
        private const val TABLE_HUD_LAYOUT = "hud_layout"

        private const val COL_ELEMENT = "element_name"
        private const val COL_X = "x"
        private const val COL_Y = "y"
        private const val COL_WIDTH = "width"
        private const val COL_HEIGHT = "height"
        private const val COL_VISIBLE = "visible"
        private const val COL_UPDATED = "updated_at"

        private const val CREATE_TABLE = """
            CREATE TABLE IF NOT EXISTS $TABLE_HUD_LAYOUT (
                $COL_ELEMENT TEXT PRIMARY KEY,
                $COL_X REAL NOT NULL,
                $COL_Y REAL NOT NULL,
                $COL_WIDTH REAL NOT NULL,
                $COL_HEIGHT REAL NOT NULL,
                $COL_VISIBLE INTEGER NOT NULL DEFAULT 1,
                $COL_UPDATED INTEGER NOT NULL
            )
        """
    }

    data class HUDElementLayout(
        val elementName: String,
        val x: Float,
        val y: Float,
        val width: Float,
        val height: Float,
        val visible: Boolean
    )

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL(CREATE_TABLE)
        Log.i(TAG, "Created table $TABLE_HUD_LAYOUT")
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
        // Future migrations go here; for v1->v2 add ALTER TABLE statements
        Log.w(TAG, "Upgrading from v$oldVersion to v$newVersion")
    }

    /** Save or update a HUD element's layout parameters */
    fun saveLayout(layout: HUDElementLayout): Boolean {
        val values = ContentValues().apply {
            put(COL_ELEMENT, layout.elementName)
            put(COL_X, layout.x)
            put(COL_Y, layout.y)
            put(COL_WIDTH, layout.width)
            put(COL_HEIGHT, layout.height)
            put(COL_VISIBLE, if (layout.visible) 1 else 0)
            put(COL_UPDATED, System.currentTimeMillis())
        }
        return try {
            writableDatabase.insertWithOnConflict(
                TABLE_HUD_LAYOUT, null, values, SQLiteDatabase.CONFLICT_REPLACE
            ) != -1L
        } catch (e: Exception) {
            Log.e(TAG, "saveLayout failed for ${layout.elementName}: ${e.message}")
            false
        }
    }

    /** Load a specific HUD element's saved layout, or null if not persisted */
    fun loadLayout(elementName: String): HUDElementLayout? {
        val cursor = readableDatabase.query(
            TABLE_HUD_LAYOUT, null,
            "$COL_ELEMENT = ?", arrayOf(elementName),
            null, null, null
        )
        return cursor.use {
            if (it.moveToFirst()) {
                HUDElementLayout(
                    elementName = it.getString(it.getColumnIndexOrThrow(COL_ELEMENT)),
                    x = it.getFloat(it.getColumnIndexOrThrow(COL_X)),
                    y = it.getFloat(it.getColumnIndexOrThrow(COL_Y)),
                    width = it.getFloat(it.getColumnIndexOrThrow(COL_WIDTH)),
                    height = it.getFloat(it.getColumnIndexOrThrow(COL_HEIGHT)),
                    visible = it.getInt(it.getColumnIndexOrThrow(COL_VISIBLE)) != 0
                )
            } else null
        }
    }

    /** Load all persisted HUD layouts */
    fun loadAllLayouts(): List<HUDElementLayout> {
        val results = mutableListOf<HUDElementLayout>()
        val cursor = readableDatabase.query(
            TABLE_HUD_LAYOUT, null, null, null, null, null, COL_ELEMENT
        )
        cursor.use {
            while (it.moveToNext()) {
                results.add(HUDElementLayout(
                    elementName = it.getString(it.getColumnIndexOrThrow(COL_ELEMENT)),
                    x = it.getFloat(it.getColumnIndexOrThrow(COL_X)),
                    y = it.getFloat(it.getColumnIndexOrThrow(COL_Y)),
                    width = it.getFloat(it.getColumnIndexOrThrow(COL_WIDTH)),
                    height = it.getFloat(it.getColumnIndexOrThrow(COL_HEIGHT)),
                    visible = it.getInt(it.getColumnIndexOrThrow(COL_VISIBLE)) != 0
                ))
            }
        }
        return results
    }

    /** Delete a specific element's saved layout (revert to defaults) */
    fun deleteLayout(elementName: String): Boolean {
        return try {
            writableDatabase.delete(TABLE_HUD_LAYOUT, "$COL_ELEMENT = ?", arrayOf(elementName)) > 0
        } catch (e: Exception) {
            Log.e(TAG, "deleteLayout failed for $elementName: ${e.message}")
            false
        }
    }

    /** Clear all saved HUD layouts */
    fun clearAll(): Int {
        return try {
            writableDatabase.delete(TABLE_HUD_LAYOUT, null, null)
        } catch (e: Exception) {
            Log.e(TAG, "clearAll failed: ${e.message}")
            0
        }
    }
}