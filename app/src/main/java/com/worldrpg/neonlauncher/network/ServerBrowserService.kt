package com.worldrpg.neonlauncher.network

import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit

/**
 * The same official catalogue as CServerBrowserWeb::CNeonServerRegistry, followed
 * by live ASE queries. A failed refresh retains the last successful catalogue.
 */
class ServerBrowserService(
    private val fetchRegistry: suspend () -> List<ServerInfo> = { NeonServerRegistry.fetch() },
    private val queryServer: suspend (String, Int) -> ServerQuery.Result? = { host, port -> ServerQuery.query(host, port) }
) {
    private var cachedRegistry: List<ServerInfo>? = null

    sealed class BrowserState {
        object Loading : BrowserState()
        data class Success(val servers: List<ServerInfo>) : BrowserState()
        data class Error(val message: String) : BrowserState()
    }

    /**
     * Emits the list right away with every server QUERYING, then again each time
     * a query answers or times out.
     */
    fun fetchServerList(favoriteKeys: Set<String> = emptySet()): Flow<BrowserState> = channelFlow {
        send(BrowserState.Loading)
        val favorites = favoriteKeys.mapNotNullTo(hashSetOf()) { ServerAddress.fromKey(it)?.key() }
        val catalogue = try {
            fetchRegistry().also { cachedRegistry = it }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: Exception) {
            send(BrowserState.Error("Unable to refresh the Neon server list. Showing available servers."))
            cachedRegistry ?: buildList {
                add(ServerInfo.configured())
                addAll(favorites.sorted().mapNotNull(ServerInfo::fromFavoriteKey))
            }
        }
        val servers = catalogue.distinctBy { it.getKey() }.map {
            it.copy(status = ServerInfo.Status.QUERYING, isFavorite = it.getKey() in favorites)
        }.toMutableList()
        send(BrowserState.Success(servers.toList()))

        val lock = Mutex()
        val permits = Semaphore(12)
        coroutineScope {
            servers.indices.forEach { index ->
                launch {
                    permits.withPermit {
                        val s = servers[index]
                        val result = queryServer(s.ip, s.port)
                        lock.withLock {
                            servers[index] = servers[index].withQuery(result)
                            send(BrowserState.Success(servers.toList()))
                        }
                    }
                }
            }
        }
    }

    /** Queries one server. */
    suspend fun refresh(server: ServerInfo): ServerInfo =
        server.withQuery(queryServer(server.ip, server.port))
}
