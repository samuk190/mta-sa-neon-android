package com.worldrpg.neonlauncher.network

import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow

/**
 * Service for fetching MTA:SA server list.
 * Currently a placeholder - actual implementation will query master server or cached list.
 */
class ServerBrowserService {

    sealed class BrowserState {
        object Loading : BrowserState()
        data class Success(val servers: List<ServerInfo>) : BrowserState()
        data class Error(val message: String) : BrowserState()
    }

    /**
     * Fetches the server list as a Flow of states.
     * Placeholder implementation returns mock data.
     */
    fun fetchServerList(): Flow<BrowserState> = flow {
        emit(BrowserState.Loading)

        try {
            // Simulate network delay
            delay(800)

            // Placeholder server list - replace with actual master server query
            val servers = listOf(
                ServerInfo(
                    name = "WorldRPG Brasil Oficial",
                    ip = "worldrpgbroficial.duckdns.org",
                    port = 22003,
                    players = 42,
                    maxPlayers = 100,
                    ping = 85
                )
            )

            emit(BrowserState.Success(servers))
        } catch (e: Exception) {
            emit(BrowserState.Error(e.message ?: "Unknown error"))
        }
    }

    /**
     * Pings a single server to get current player count and latency.
     * Placeholder - actual implementation uses native RakNet ping.
     */
    suspend fun pingServer(ip: String, port: Int): ServerInfo? {
        delay(200)
        return ServerInfo(
            name = "Server",
            ip = ip,
            port = port,
            players = 0,
            maxPlayers = 100,
            ping = 50
        )
    }
}