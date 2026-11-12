package com.worldrpg.neonlauncher.network

import com.worldrpg.neonlauncher.BuildConfig
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
     * Uses default server configured via .env / BuildConfig.
     */
    fun fetchServerList(): Flow<BrowserState> = flow {
        emit(BrowserState.Loading)

        try {
            // Simulate network delay
            delay(800)

            // Placeholder server list using configured defaults from .env
            val servers = listOf(
                ServerInfo(
                    name = BuildConfig.DEFAULT_SERVER_NAME,
                    ip = BuildConfig.DEFAULT_SERVER_IP,
                    port = BuildConfig.DEFAULT_SERVER_PORT,
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