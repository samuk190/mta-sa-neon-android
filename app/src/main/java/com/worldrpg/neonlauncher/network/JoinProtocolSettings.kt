package com.worldrpg.neonlauncher.network

import android.content.Context

/** The legacy handshake is an explicit per-server preference. */
object JoinProtocolSettings {
    const val EXTRA_JOIN_PROFILE = "join_profile"

    @JvmStatic fun read(context: Context, host: String, port: Int): Int =
        context.getSharedPreferences("neon_join_protocol", Context.MODE_PRIVATE)
            .getInt(key(host, port), 0).takeIf { it in 0..2 } ?: 0

    fun save(context: Context, host: String, port: Int, mode: Int): Boolean {
        require(mode in 0..2)
        return context.getSharedPreferences("neon_join_protocol", Context.MODE_PRIVATE).edit().putInt(key(host, port), mode).commit()
    }

    private fun key(host: String, port: Int) = "${host.lowercase().trimEnd('.')}:$port"
}
