package com.worldrpg.neonlauncher.game

import android.util.Log
import com.worldrpg.neonlauncher.utils.HUDLayoutPersistence

/**
 * Parses and dispatches admin commands from in-game chat input.
 * Bridges to native JNI HUD calibration methods per AGENTS.md directive 10.
 * Enforces permission checks via AdminPermissionGate before execution.
 * Persists HUD layout changes to SQLite via HUDLayoutPersistence.
 *
 * Supported commands:
 *   /admin module hud <element> <x> <y> <width> <height> [visible]
 *   /admin hud list
 *   /admin hud get <element>
 *   /admin hud reset <element>
 */
class AdminCommandHandler(private val gameActivity: GameActivity) {

    private val permissionGate = AdminPermissionGate()
    private val hudPersistence = HUDLayoutPersistence(gameActivity)

    companion object {
        private const val TAG = "AdminCommand"
        private const val CMD_PREFIX = "/admin"
    }

    /**
     * Attempt to parse and execute an admin command.
     * @return true if the message was recognized as an admin command (even if invalid args)
     */
    fun tryHandleCommand(message: String): Boolean {
        val trimmed = message.trim()
        if (!trimmed.startsWith(CMD_PREFIX, ignoreCase = true)) return false

        val parts = trimmed.split("\\s+".toRegex())
        if (parts.size < 3) {
            logUsage()
            return true
        }

        val module = parts[1].lowercase()
        when (module) {
            "module", "hud" -> handleHudCommand(parts)
            else -> Log.w(TAG, "Unknown admin module: $module")
        }
        return true
    }

    private fun handleHudCommand(parts: List<String>) {
        // Permission gate: require ADMIN level for all HUD commands
        if (!permissionGate.hasPermission("hud")) {
            val status = permissionGate.getStatusString()
            gameActivity.nativeAddChatMessage("[ADMIN] Permission denied. $status", 0xFF.toByte().toInt())
            Log.w(TAG, "HUD command denied: $status")
            return
        }

        // /admin module hud <element> <x> <y> <w> <h> [vis]
        // or /admin hud list
        // or /admin hud get <element>
        // or /admin hud reset <element>
        val subCmd = if (parts.size > 2) parts[2].lowercase() else ""

        when {
            subCmd == "list" -> {
                val result = gameActivity.nativeListHUDElements()
                Log.i(TAG, "HUD elements:\n$result")
                gameActivity.nativeAddChatMessage("[ADMIN] HUD elements listed in logcat", 0xFF.toByte().toInt())
            }

            subCmd == "get" && parts.size > 3 -> {
                val element = parts[3]
                val result = gameActivity.nativeGetHUDElementParams(element)
                Log.i(TAG, "HUD '$element': $result")
                gameActivity.nativeAddChatMessage("[ADMIN] $element: $result", 0xFF.toByte().toInt())
            }

            subCmd == "reset" && parts.size > 3 -> {
                val element = parts[3]
                gameActivity.nativeSetHUDElementParams(element, 0f, 0f, 0f, 0f, true)
                // Remove persisted layout so defaults apply on next startup
                hudPersistence.deleteLayout(element)
                Log.i(TAG, "HUD '$element' reset and persistence cleared")
                gameActivity.nativeAddChatMessage("[ADMIN] $element reset (defaults restored, persistence cleared)", 0xFF.toByte().toInt())
            }

            parts.size >= 7 -> {
                // /admin module hud <element> <x> <y> <w> <h> [vis]
                val elementIdx = if (parts[1].equals("module", true)) 3 else 2
                if (elementIdx + 4 >= parts.size) {
                    logUsage()
                    return
                }
                val element = parts[elementIdx]
                val x = parts[elementIdx + 1].toFloatOrNull() ?: run { logUsage(); return }
                val y = parts[elementIdx + 2].toFloatOrNull() ?: run { logUsage(); return }
                val w = parts[elementIdx + 3].toFloatOrNull() ?: run { logUsage(); return }
                val h = parts[elementIdx + 4].toFloatOrNull() ?: run { logUsage(); return }
                val visible = if (elementIdx + 5 < parts.size) {
                    parts[elementIdx + 5].toIntOrNull()?.let { it != 0 } ?: true
                } else true

                gameActivity.nativeSetHUDElementParams(element, x, y, w, h, visible)

                // Persist to SQLite so layout survives app restart (AGENTS.md directive 10)
                val saved = hudPersistence.saveLayout(
                    HUDLayoutPersistence.HUDElementLayout(element, x, y, w, h, visible)
                )
                val msg = "[ADMIN] HUD '$element' set: x=$x y=$y w=$w h=$h vis=$visible saved=$saved"
                Log.i(TAG, msg)
                gameActivity.nativeAddChatMessage(msg, 0xFF.toByte().toInt())
            }

            else -> logUsage()
        }
    }

    private fun logUsage() {
        val usage = """
            Usage:
              /admin hud list
              /admin hud get <element>
              /admin hud reset <element>
              /admin module hud <element> <x> <y> <width> <height> [visible]
        """.trimIndent()
        Log.i(TAG, usage)
    }
}