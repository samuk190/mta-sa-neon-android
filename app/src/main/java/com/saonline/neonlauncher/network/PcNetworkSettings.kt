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
    const val EXTRA_PRODUCT_VERSION = "pc_network_product_version"

    /** The netc.dll builds the emulator can run. */
    enum class NetcBuild(val id: String) {
        /** MTA's official module (mirror-cdn bdata/netc.dll), also what the Neon PC client ships. */
        OFFICIAL("official"),
        /** MTA's module for fork builds (bdata/fork-support/netc.dll). Official servers ignore it. */
        FORK("fork"),
    }

    /**
     * Official MTA servers answer only the official module (checked against an
     * MTA 1.6 server: the fork-support build gets no reply at the RakNet step),
     * and the Neon PC client ships the official one too, so every server uses it.
     */
    @Suppress("UNUSED_PARAMETER")
    fun buildFor(server: ServerInfo): NetcBuild = NetcBuild.OFFICIAL

    /**
     * The product version core.dll passes to InitNetRev
     * (SharedUtil::GetProductVersion): Neon servers (Neon registry, or MTA 1.7)
     * expect the Neon 1.7.0 custom client, others an MTA 1.6 release client.
     */
    fun productVersionFor(server: ServerInfo): String =
        if (server.registryId.isNotEmpty() || server.version.trim().startsWith("1.7")) "1.7.0-1.00000" else "1.6.0-9.23081"

    @JvmStatic fun isEnabled(context: Context, host: String, port: Int): Boolean =
        prefs(context).getBoolean(key(host, port), false)

    fun setEnabled(context: Context, host: String, port: Int, enabled: Boolean): Boolean =
        prefs(context).edit().putBoolean(key(host, port), enabled).commit()

    private fun prefs(context: Context) = context.getSharedPreferences("neon_pc_network", Context.MODE_PRIVATE)
    private fun key(host: String, port: Int) = "${host.lowercase().trimEnd('.')}:$port"
}
