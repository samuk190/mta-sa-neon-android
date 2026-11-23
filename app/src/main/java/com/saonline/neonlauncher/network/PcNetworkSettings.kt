package com.saonline.neonlauncher.network

import android.content.Context

/**
 * "Use PC Network — Compatibility": join through MTA's PC netc.dll running
 * under x86 emulation (cpp/netemu) instead of the mobile transport. A
 * per-server choice, off by default.
 */
object PcNetworkSettings {
    /** Intent extras EngineHostActivity reads; an empty path means off. */
    const val EXTRA_NETC_PATH = "pc_network_netc_path"
    const val EXTRA_NETC_BUILD = "pc_network_netc_build"

    /** The two netc.dll builds the emulator runs. */
    enum class NetcBuild(val id: String) {
        /** The Neon fork's netc, for Neon servers without the mobile transport. */
        NEON("neon"),
        /** Stock MTA netc (bdata/fork-support), for every other server. */
        STOCK("stock"),
    }

    /**
     * Neon servers (listed in the Neon registry, or reporting MTA 1.7) use the
     * Neon build; everything else is a stock MTA server.
     */
    fun buildFor(server: ServerInfo): NetcBuild =
        if (server.registryId.isNotEmpty() || server.version.trim().startsWith("1.7")) NetcBuild.NEON else NetcBuild.STOCK

    @JvmStatic fun isEnabled(context: Context, host: String, port: Int): Boolean =
        prefs(context).getBoolean(key(host, port), false)

    fun setEnabled(context: Context, host: String, port: Int, enabled: Boolean): Boolean =
        prefs(context).edit().putBoolean(key(host, port), enabled).commit()

    private fun prefs(context: Context) = context.getSharedPreferences("neon_pc_network", Context.MODE_PRIVATE)
    private fun key(host: String, port: Int) = "${host.lowercase().trimEnd('.')}:$port"
}
