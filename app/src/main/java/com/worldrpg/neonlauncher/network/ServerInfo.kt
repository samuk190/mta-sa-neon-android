package com.worldrpg.neonlauncher.network

/**
 * Data class representing an MTA:SA server entry.
 * All strings are ASCII-only per project rules.
 */
data class ServerInfo(
    val name: String,
    val ip: String,
    val port: Int,
    val players: Int,
    val maxPlayers: Int,
    val ping: Int,
    val isFavorite: Boolean = false
) {
    /**
     * Returns a unique key for favorite persistence (ip:port).
     */
    fun getKey(): String = "$ip:$port"

    /**
     * Returns formatted player count string.
     */
    fun getPlayerCountText(): String = "$players/$maxPlayers"

    /**
     * Returns formatted ping string.
     */
    fun getPingText(): String = "${ping}ms"

    companion object {
        /**
         * Creates a ServerInfo from a persisted favorite key.
         */
        fun fromFavoriteKey(key: String, name: String = "Unknown Server"): ServerInfo {
            val parts = key.split(":")
            return if (parts.size == 2) {
                ServerInfo(
                    name = name,
                    ip = parts[0],
                    port = parts[1].toIntOrNull() ?: 22003,
                    players = 0,
                    maxPlayers = 0,
                    ping = 0,
                    isFavorite = true
                )
            } else {
                ServerInfo(name = name, ip = key, port = 22003, players = 0, maxPlayers = 0, ping = 0, isFavorite = true)
            }
        }
    }
}