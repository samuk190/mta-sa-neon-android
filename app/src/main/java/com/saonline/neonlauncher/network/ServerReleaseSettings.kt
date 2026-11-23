package com.saonline.neonlauncher.network

import android.content.Context

/**
 * Which server release a join speaks, per server: the current Neon protocol
 * (netcode 0x41E7) or release 189 (0x41E6, older Neon deployments). Automatic
 * tries the release that worked last and switches once when the server refuses
 * it ("requires the current ... network protocol").
 */
object ServerReleaseSettings {
    const val AUTOMATIC = 0
    const val CURRENT = 1
    const val RELEASE_189 = 2

    const val EXTRA_RELEASE = "server_release"

    @JvmStatic fun read(context: Context, host: String, port: Int): Int =
        prefs(context).getInt(key(host, port), AUTOMATIC).takeIf { it in AUTOMATIC..RELEASE_189 } ?: AUTOMATIC

    fun save(context: Context, host: String, port: Int, release: Int): Boolean {
        require(release in AUTOMATIC..RELEASE_189)
        return prefs(context).edit().putInt(key(host, port), release).commit()
    }

    /**
     * What Automatic found last time (CURRENT or RELEASE_189). A release 189
     * finding expires after a week, so a server that has upgraded (current
     * servers still accept 189) is probed with Current again.
     */
    @JvmStatic fun detected(context: Context, host: String, port: Int): Int {
        val p = prefs(context)
        val release = p.getInt("detected:" + key(host, port), CURRENT)
        val at = p.getLong("detectedAt:" + key(host, port), 0L)
        val fresh = System.currentTimeMillis() - at < DETECTED_TTL_MS
        return if (release == RELEASE_189 && fresh) RELEASE_189 else CURRENT
    }

    /** Called after a join that used Automatic; forced choices are not remembered. */
    @JvmStatic fun rememberDetected(context: Context, host: String, port: Int, requested: Int, release: Int) {
        if (requested != AUTOMATIC || (release != CURRENT && release != RELEASE_189)) return
        prefs(context).edit()
            .putInt("detected:" + key(host, port), release)
            .putLong("detectedAt:" + key(host, port), System.currentTimeMillis())
            .apply()
    }

    private const val DETECTED_TTL_MS = 7L * 24 * 60 * 60 * 1000

    private fun prefs(context: Context) = context.getSharedPreferences("neon_server_release", Context.MODE_PRIVATE)
    private fun key(host: String, port: Int) = "${host.lowercase().trimEnd('.')}:$port"
}
