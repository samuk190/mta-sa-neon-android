package com.worldrpg.neonlauncher.game

import android.util.Log

import com.worldrpg.neonlauncher.BuildConfig

/**
 * Gates admin command execution behind server-side ACL verification
 * or client-side admin token validation. Prevents unauthorized players
 * from modifying HUD layout or executing privileged commands.
 *
 * Security model:
 * - Server assigns admin level on join (0 = none, 1 = moderator, 2 = admin, 3 = owner)
 * - Client caches admin level from server sync packet
 * - Commands check minimum required level before dispatching to native layer
 * - Token-based fallback ONLY available in debug builds (Phase 12)
 */
class AdminPermissionGate {

    companion object {
        private const val TAG = "AdminPermGate"

        /** Admin levels matching MTA ACL hierarchy */
        const val LEVEL_NONE = 0
        const val LEVEL_MODERATOR = 1
        const val LEVEL_ADMIN = 2
        const val LEVEL_OWNER = 3
    }

    /** Current player's admin level (set by server sync or local override) */
    var currentLevel: Int = LEVEL_NONE
        private set

    /** Optional client-side admin token for offline/testing use */
    private var localAdminToken: String? = null

    /** Command-to-minimum-level mapping */
    private val commandPermissions = mapOf(
        "hud" to LEVEL_ADMIN,
        "kick" to LEVEL_MODERATOR,
        "ban" to LEVEL_ADMIN,
        "shutdown" to LEVEL_OWNER,
        "restart" to LEVEL_OWNER,
        "setlevel" to LEVEL_OWNER
    )

    /**
     * Update admin level from server sync packet.
     * Called when ELEMENT_CREATE or PLAYER_SYNC includes admin metadata.
     */
    fun setServerAdminLevel(level: Int) {
        currentLevel = level.coerceIn(LEVEL_NONE, LEVEL_OWNER)
        Log.i(TAG, "Server admin level set: $currentLevel")
    }

    /**
     * Set a local admin token for offline/testing bypass.
     * Token is validated against a hardcoded test value — NOT for production security.
     */
    fun setLocalTestToken(token: String?) {
        localAdminToken = token
        Log.i(TAG, "Local test token ${if (token != null) "set" else "cleared"}")
    }

    /**
     * Check if the current player has permission to execute a given admin module.
     * @param module The admin module name (e.g., "hud", "kick", "ban")
     * @return true if authorized, false otherwise
     */
    fun hasPermission(module: String): Boolean {
        val normalizedModule = module.lowercase()

        // Check local test token first — ONLY in debug builds (Phase 12 security gate)
        if (BuildConfig.DEBUG && localAdminToken == "worldrpg_admin_test_2026") {
            Log.d(TAG, "Permission granted via local test token for '$normalizedModule' (DEBUG only)")
            return true
        }

        // Look up required level for this module
        val requiredLevel = commandPermissions[normalizedModule] ?: run {
            Log.w(TAG, "Unknown admin module '$normalizedModule' — defaulting to OWNER level")
            LEVEL_OWNER
        }

        val authorized = currentLevel >= requiredLevel
        if (!authorized) {
            Log.w(TAG, "Permission DENIED for '$normalizedModule': have=$currentLevel need=$requiredLevel")
        } else {
            Log.d(TAG, "Permission granted for '$normalizedModule': level=$currentLevel >= $requiredLevel")
        }
        return authorized
    }

    /**
     * Get human-readable description of current permission state.
     * ASCII-only output per project rules.
     */
    fun getStatusString(): String {
        val levelName = when (currentLevel) {
            LEVEL_NONE -> "None"
            LEVEL_MODERATOR -> "Moderator"
            LEVEL_ADMIN -> "Admin"
            LEVEL_OWNER -> "Owner"
            else -> "Unknown($currentLevel)"
        }
        val hasTestToken = localAdminToken != null
        return "Level: $levelName | TestToken: ${if (hasTestToken) "Active" else "Inactive"}"
    }

    /** Reset all permission state (on disconnect) */
    fun reset() {
        currentLevel = LEVEL_NONE
        localAdminToken = null
        Log.i(TAG, "Admin permissions reset")
    }
}