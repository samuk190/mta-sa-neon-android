package com.worldrpg.neonlauncher.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import org.json.JSONTokener
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URI

/** CServerBrowserWeb.cpp: CNeonServerRegistry::RefreshRemote / ParseManifest (536-788). */
object NeonServerRegistry {
    const val URL = "https://identity.mta-neon.com/.well-known/neon-server-registry"
    private const val MAX_BYTES = 256 * 1024
    private const val MAX_SERVERS = 256
    private val serverId = Regex("[A-Za-z0-9][A-Za-z0-9._:-]{0,127}")
    private val assetPath = Regex("/v1/server-registry/assets/[0-9a-f]{64}")
    private val linkKinds = setOf("website", "discord", "instagram", "x", "facebook", "vk", "youtube", "tiktok")

    suspend fun fetch(): List<ServerInfo> = withContext(Dispatchers.IO) {
        val connection = java.net.URL(URL).openConnection() as HttpURLConnection
        try {
            connection.connectTimeout = 10_000
            connection.readTimeout = 10_000
            connection.instanceFollowRedirects = false
            connection.setRequestProperty("Accept", "application/json")
            if (connection.responseCode !in 200..299 || connection.contentLengthLong > MAX_BYTES) {
                throw IOException("Neon registry request failed")
            }
            val bytes = ByteArrayOutputStream()
            connection.inputStream.use { input ->
                val buffer = ByteArray(8192)
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (bytes.size() + count > MAX_BYTES) throw IOException("Neon registry response is too large")
                    bytes.write(buffer, 0, count)
                }
            }
            parse(bytes.toByteArray())
        } finally {
            connection.disconnect()
        }
    }

    internal fun parse(bytes: ByteArray): List<ServerInfo> {
        require(bytes.isNotEmpty() && bytes.size <= MAX_BYTES) { "Invalid Neon registry size" }
        val payload = Charsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(bytes)).toString()
        val tokener = JSONTokener(payload)
        val document = tokener.nextValue() as? JSONObject ?: error("Invalid Neon registry document")
        require(tokener.nextClean() == '\u0000') { "Trailing Neon registry data" }
        val version = document.opt("schema_version")
        require(version is Int && version == 1) { "Unsupported Neon registry schema" }
        val entries = document.getJSONArray("servers")
        require(entries.length() <= MAX_SERVERS) { "Too many Neon servers" }
        val ids = HashSet<String>()
        val endpoints = HashSet<String>()
        val result = ArrayList<ServerInfo>()
        for (i in 0 until entries.length()) {
            val server = entries.getJSONObject(i)
            val id = string(server, "id", 128)
            require(serverId.matches(id) && ids.add(id)) { "Invalid or duplicate Neon server ID" }
            val name = string(server, "name", 128)
            string(server, "tagline", 256)
            string(server, "description", 2048)
            strings(server.getJSONArray("countries"), 16, 2).forEach { country ->
                require(country.length == 2 && country.all { it in 'a'..'z' || it in 'A'..'Z' })
            }
            strings(server.getJSONArray("languages"), 16, 64)
            if (server.has("public_host")) {
                val host = string(server, "public_host", 253)
                require(ServerAddress.normalizeHost(host) == host && '.' in host && host.substringAfterLast('.').first() in 'a'..'z')
            }
            if (server.has("accent")) require(Regex("#[0-9A-Fa-f]{6}").matches(string(server, "accent", 7)))
            for (field in listOf("logo_url", "banner_url")) {
                if (server.has(field)) require(string(server, field, 2048).startsWith("https://"))
            }
            val links = server.getJSONArray("links")
            require(links.length() <= 12)
            for (j in 0 until links.length()) {
                val link = links.getJSONObject(j)
                require(string(link, "kind", 16) in linkKinds)
                string(link, "label", 96)
                require(string(link, "url", 2048).startsWith("https://"))
            }
            val addresses = strings(server.getJSONArray("endpoints"), 16, 64)
            require(addresses.isNotEmpty())
            for (endpoint in addresses) {
                val address = ServerAddress.fromKey(endpoint) ?: error("Invalid Neon endpoint")
                require(address.key() == endpoint && address.host.all { it in '0'..'9' || it == '.' }
                    && endpoints.add(endpoint)) { "Invalid or duplicate Neon endpoint" }
                result += ServerInfo(name = name, ip = address.host, port = address.port,
                    registryId = id, logoUrl = artwork(server.opt("logo_url") as? String))
            }
        }
        // A valid empty catalogue replaces previous entries, just as the desktop does.
        return result
    }

    private fun string(objectValue: JSONObject, key: String, maxBytes: Int): String {
        val value = objectValue.get(key) as? String ?: error("Invalid Neon registry field: $key")
        require(value.isNotEmpty() && value.toByteArray(Charsets.UTF_8).size <= maxBytes)
        return value
    }

    private fun strings(array: JSONArray, maxItems: Int, maxBytes: Int): List<String> {
        require(array.length() <= maxItems)
        val values = (0 until array.length()).map { index ->
            val value = array.get(index) as? String ?: error("Invalid Neon registry array")
            require(value.isNotEmpty() && value.toByteArray(Charsets.UTF_8).size <= maxBytes)
            value
        }
        require(values.size == values.toSet().size)
        return values
    }

    private fun artwork(value: String?): String {
        val uri = runCatching { URI(value ?: return "") }.getOrNull() ?: return ""
        return if (uri.scheme == "https" && uri.host == "identity.mta-neon.com" && uri.port in listOf(-1, 443)
            && uri.userInfo == null && uri.rawQuery == null && uri.rawFragment == null && assetPath.matches(uri.path.orEmpty())) value.orEmpty()
        else ""
    }
}
