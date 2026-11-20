package com.worldrpg.neonlauncher.network

import com.worldrpg.neonlauncher.BuildConfig

/**
 * One MTA:SA server entry. The live fields (players, ping, game mode...) come
 * from the ASE query (see [ServerQuery]); until it answers they are unknown and
 * [status] says why, so the UI never shows made-up numbers.
 */
data class ServerInfo(
    val name: String,
    val ip: String,
    val port: Int,
    val isFavorite: Boolean = false,
    /** Nonempty for entries from the official Neon catalogue. Its display name is authoritative. */
    val registryId: String = "",
    /** http(s) URL of the server logo; empty shows the name's initials. */
    val logoUrl: String = "",
    val status: Status = Status.QUERYING,
    val players: Int = 0,
    val maxPlayers: Int = 0,
    val ping: Int = 0,
    val gameMode: String = "",
    val mapName: String = "",
    val version: String = "",
    val passworded: Boolean = false,
    val playerNames: List<String> = emptyList()
) {
    enum class Status { QUERYING, ONLINE, OFFLINE }

    /** Unique key for favorite persistence (ip:port). */
    fun getKey(): String = "${ServerAddress.normalizeHost(ip) ?: ip}:$port"

    fun withQuery(result: ServerQuery.Result?): ServerInfo =
        if (result == null) copy(status = Status.OFFLINE)
        else copy(
            // Desktop CServerListItem::ParseQuery takes the queried name when no master list restricts it.
            name = if (registryId.isNotEmpty()) name else result.serverName.ifBlank { name },
            status = Status.ONLINE,
            players = result.players,
            maxPlayers = result.maxPlayers,
            ping = result.pingMs,
            gameMode = result.gameType,
            mapName = result.mapName,
            version = result.version,
            passworded = result.passworded,
            playerNames = result.playerNames
        )

    companion object {
        /** The server configured in .env. */
        fun configured(): ServerInfo = ServerInfo(
            name = BuildConfig.DEFAULT_SERVER_NAME,
            ip = ServerAddress.normalizeHost(BuildConfig.DEFAULT_SERVER_IP) ?: BuildConfig.DEFAULT_SERVER_IP,
            port = BuildConfig.DEFAULT_SERVER_PORT,
            logoUrl = BuildConfig.DEFAULT_SERVER_LOGO_URL
        )

        /** Creates a ServerInfo from a persisted favorite key (ip:port). */
        fun fromFavoriteKey(key: String): ServerInfo? {
            val address = ServerAddress.fromKey(key) ?: return null
            val def = configured()
            if (address.key() == def.getKey()) {
                return def.copy(ip = address.host, isFavorite = true)
            }
            return ServerInfo(name = address.key(), ip = address.host, port = address.port, isFavorite = true)
        }
    }
}
