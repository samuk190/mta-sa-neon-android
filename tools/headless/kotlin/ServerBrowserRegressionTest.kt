package com.worldrpg.neonlauncher.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.util.concurrent.atomic.AtomicInteger

private fun server(id: String = "test-server", endpoint: String = "192.0.2.1:22003"): JSONObject = JSONObject()
    .put("id", id).put("name", "Official name").put("tagline", "Test").put("description", "Description")
    .put("countries", JSONArray()).put("languages", JSONArray()).put("links", JSONArray())
    .put("endpoints", JSONArray().put(endpoint))

private fun catalogue(vararg servers: JSONObject): ByteArray = JSONObject().put("schema_version", 1)
    .put("servers", JSONArray(servers.toList())).toString().toByteArray()

private fun invalid(bytes: ByteArray) {
    check(runCatching { NeonServerRegistry.parse(bytes) }.isFailure) { "Accepted malformed registry" }
}

private fun List<ServerBrowserService.BrowserState>.lastServers() = filterIsInstance<ServerBrowserService.BrowserState.Success>().last().servers

private suspend fun checkLiveQuery() = coroutineScope {
    // Some development hosts forward 127.0.0.1 to another network namespace.
    val local = InetAddress.getByName(System.getenv("NEON_TEST_UDP_ADDRESS") ?: "127.0.0.1")
    DatagramSocket(0, local).use { server ->
        server.soTimeout = 3000
        val gamePort = server.localPort - ServerQuery.QUERY_PORT_OFFSET
        val responder = async(Dispatchers.IO) {
            val request = DatagramPacket(ByteArray(8), 8)
            server.receive(request)
            check(request.length == 1 && request.data[0] == 'r'.code.toByte())
            val reply = ByteArrayOutputStream().apply {
                write("EYE2".toByteArray())
                for (field in listOf("mta", gamePort.toString(), "Loopback", "Race", "Map", "1.7")) {
                    val encoded = field.toByteArray()
                    write(encoded.size + 1)
                    write(encoded)
                }
                write(byteArrayOf(0, 0, 1, 8))
            }.toByteArray()
            server.send(DatagramPacket(reply, reply.size, request.socketAddress))
        }
        check(withTimeout(4000) { ServerQuery.query(local.hostAddress, gamePort) }?.serverName == "Loopback")
        responder.await()

        val pending = async { ServerQuery.query(local.hostAddress, gamePort) }
        withContext(Dispatchers.IO) {
            server.receive(DatagramPacket(ByteArray(8), 8))
        }
        // No reply. Cancellation should close the receive socket before its timeout.
        withTimeout(1000) { pending.cancelAndJoin() }
    }
}

fun main(args: Array<String>) = runBlocking {
    check(ServerAddress.normalizeHost("  EXAMPLE.org. ") == "example.org")
    check(ServerAddress.normalizeHost("192.168.001.010") == "192.168.1.10")
    for (host in listOf("", "256.1.2.3", "1.2.3", "http://example.org", "example.org/path", "example.org:22003", "bad host", "-bad.org", "bad..org", "[::1]")) {
        check(ServerAddress.normalizeHost(host) == null) { "Accepted host $host" }
    }
    for (port in listOf("", "0", "65536", "-1", "+22003", "22x03")) check(ServerAddress.parsePort(port) == null)
    check(ServerAddress.parsePort("65535") == 65535)
    check(ServerAddress.fromKey("EXAMPLE.org:22003") == ServerAddress("example.org", 22003))
    check(ServerAddress.fromKey("example.org:invalid") == null)
    check(ServerInfo.fromFavoriteKey("EXAMPLE.org.:02203")?.getKey() == "example.org:2203")
    check(ServerInfo.fromFavoriteKey("192.000.002.005:22003")?.ip == "192.0.2.5")
    check(ServerInfo.fromFavoriteKey("example.org:invalid") == null)

    val entries = NeonServerRegistry.parse(catalogue(server().put("endpoints", JSONArray().put("192.0.2.1:22003").put("192.0.2.1:22004"))))
    check(entries.size == 2 && entries.all { it.registryId == "test-server" && it.name == "Official name" })
    check(NeonServerRegistry.parse(catalogue()).isEmpty())
    for (endpoint in listOf("example.org:22003", "192.0.2.1:0", "192.0.2.1:65536", "999.0.2.1:22003", "192.000.2.1:22003")) {
        invalid(catalogue(server(endpoint = endpoint)))
    }
    invalid(catalogue(server(), server()))
    invalid(catalogue(server(), server(id = "other")))
    invalid(catalogue(server().put("name", 123)))
    invalid(catalogue(server().put("countries", JSONArray().put("BRA"))))
    invalid(catalogue(server().put("languages", JSONArray().put("English").put("English"))))
    invalid(catalogue(server().put("public_host", "127.0.0.1")))
    invalid(catalogue(server().put("accent", "red")))
    invalid(catalogue(server().put("logo_url", "http://example.org/image.png")))
    invalid("{\"schema_version\":2,\"servers\":[]}".toByteArray())
    invalid("{\"schema_version\":\"1\",\"servers\":[]}".toByteArray())
    invalid(catalogue(server()) + " extra".toByteArray())
    invalid(ByteArray(256 * 1024 + 1) { ' '.code.toByte() })
    invalid(byteArrayOf(0xC0.toByte(), 0xAF.toByte()))
    invalid(catalogue(*(0..256).map { server("server-$it", "192.0.2.1:${22003 + it}") }.toTypedArray()))
    val safeLogo = "https://identity.mta-neon.com/v1/server-registry/assets/" + "a".repeat(64)
    check(NeonServerRegistry.parse(catalogue(server().put("logo_url", safeLogo))).single().logoUrl == safeLogo)
    check(NeonServerRegistry.parse(catalogue(server().put("logo_url", "https://example.org/image.png"))).single().logoUrl.isEmpty())

    val queried = ServerQuery.Result("mta", "Untrusted ASE name", "Race", "Map", "1.7", false, false, 3, 64, 1, 1, 22005, emptyList(), 42)
    val service = ServerBrowserService(fetchRegistry = { entries }, queryServer = { _, _ -> delay(2); queried })
    val states = service.fetchServerList(setOf(entries.first().getKey())).toList()
    val snapshots = states.filterIsInstance<ServerBrowserService.BrowserState.Success>()
    check(snapshots.first().servers.all { it.status == ServerInfo.Status.QUERYING })
    check(snapshots.last().servers.all { it.status == ServerInfo.Status.ONLINE && it.players == 3 && it.name == "Official name" })
    check(snapshots.last().servers.first().isFavorite)

    var mode = 0
    val cached = ServerBrowserService(fetchRegistry = {
        when (mode) { 0 -> entries; 1 -> emptyList(); else -> throw IOException("offline") }
    }, queryServer = { _, _ -> null })
    cached.fetchServerList().toList()
    mode = 2
    val fallback = cached.fetchServerList().toList()
    check(fallback.any { it is ServerBrowserService.BrowserState.Error })
    check(fallback.lastServers().map { it.getKey() } == entries.map { it.getKey() })
    mode = 1
    check(cached.fetchServerList().toList().lastServers().isEmpty())
    mode = 2
    check(cached.fetchServerList().toList().lastServers().isEmpty()) { "Empty catalogue resurrected expired servers" }
    val unavailable = ServerBrowserService(fetchRegistry = { throw IOException("offline") }, queryServer = { _, _ -> null })
    val favoriteFallback = unavailable.fetchServerList(
        setOf("192.0.2.5:22003", "192.000.002.005:22003", "corrupt")
    ).toList().lastServers()
    check(favoriteFallback.size == 2)
    check(favoriteFallback.single { it.isFavorite }.getKey() == "192.0.2.5:22003")
    checkLiveQuery()

    val active = AtomicInteger()
    val peak = AtomicInteger()
    val many = (1..40).map { ServerInfo("Server $it", "192.0.2.$it", 22003) }
    val bounded = ServerBrowserService(fetchRegistry = { many }, queryServer = { _, _ ->
        val count = active.incrementAndGet()
        peak.updateAndGet { old -> maxOf(old, count) }
        try { delay(5); null } finally { active.decrementAndGet() }
    })
    check(bounded.fetchServerList().toList().lastServers().size == 40)
    check(peak.get() in 2..12 && active.get() == 0)
    val cancelled = mutableListOf<ServerBrowserService.BrowserState>()
    val waiting = ServerBrowserService(fetchRegistry = { delay(1000); entries })
    check(withTimeoutOrNull(20) { waiting.fetchServerList().toList(cancelled) } == null)
    check(cancelled.none { it is ServerBrowserService.BrowserState.Error })

    if (args.isNotEmpty()) {
        val live = NeonServerRegistry.parse(File(args[0]).readBytes())
        check(live.isNotEmpty())
        println("PASS: actual Neon catalogue contains ${live.size} endpoints")
    }
    println("PASS: endpoint validation, registry validation, progressive queries, favourites, cache, empty refresh, cancellation and query limits")
}
