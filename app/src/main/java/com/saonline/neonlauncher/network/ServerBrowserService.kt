package com.saonline.neonlauncher.network

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
        data class Success(val servers: List<ServerInfo>, val catalogueKeys: Set<String>) : BrowserState()
        data class Error(val message: String) : BrowserState()
    }

    /**
     * Emits the list right away with every server QUERYING, then again each time
     * a query answers or times out.
     */
    fun fetchServerList(favoriteKeys: Set<String> = emptySet(), recentKeys: List<String> = emptyList()): Flow<BrowserState> = channelFlow {
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
        // A manually saved address remains available in its own tab even when absent from the registry.
        val catalogueKeys = catalogue.mapTo(hashSetOf()) { it.getKey() }
        val savedServers = (favorites + recentKeys).mapNotNull(ServerInfo::fromFavoriteKey)
        val servers = (catalogue + savedServers).distinctBy { it.getKey() }.map {
            it.copy(isFavorite = it.getKey() in favorites)
        }.toMutableList()
        send(BrowserState.Success(servers.toList(), catalogueKeys))

        val lock = Mutex()
        val permits = Semaphore(12)
        coroutineScope {
            servers.indices.filter { servers[it].status != ServerInfo.Status.DIRECT }.forEach { index ->
                launch {
                    permits.withPermit {
                        val s = servers[index]
                        val result = queryServer(s.ip, s.port)
                        lock.withLock {
                            servers[index] = servers[index].withQuery(result)
                            send(BrowserState.Success(servers.toList(), catalogueKeys))
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
