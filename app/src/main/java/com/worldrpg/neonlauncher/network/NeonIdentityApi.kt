package com.worldrpg.neonlauncher.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import org.mtamobile.identity.AndroidTicketProof
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** Wire contract ported from desktop CNeonIdentityManager (start/poll/tickets).
 * The identity service is trusted configuration, never a URL supplied by a game server.
 */
class NeonIdentityApi(
    private val createProof: (String) -> JSONObject = AndroidTicketProof::create,
    private val request: suspend (String, JSONObject, String?) -> Response = ::post
) {
    data class Response(val status: Int, val body: JSONObject)
    data class Flow(val id: String, val pollToken: String, val authorizationUrl: String)
    class SessionExpired : IOException("Your SA:O session expired. Sign in again.")

    suspend fun start(): Flow {
        val response = request("/v1/auth/discord/start", JSONObject(), null)
        requireSuccess(response)
        val url = field(response.body, "authorization_url", 4096)
        if (!url.startsWith("$BASE/v1/auth/discord/authorize#"))
            throw IOException("The account service returned an invalid authorization address.")
        return Flow(field(response.body, "flow_id", 128), field(response.body, "poll_token", 256), url)
    }

    /** null means pending; the browser flow expires independently of the game connection. */
    suspend fun poll(flow: Flow): String? {
        val response = request("/v1/auth/discord/poll", JSONObject().put("flow_id", flow.id).put("poll_token", flow.pollToken), null)
        requireSuccess(response)
        if (response.body.optString("status") == "pending") return null
        return tokenField(response.body, "session_token", 65536)
    }

    suspend fun ticket(session: String, serverId: String, endpoint: String): String {
        val address = ServerAddress.fromKey(endpoint)
        if (serverId.isBlank() || serverId.length > 128 || address == null || address.key() != endpoint ||
            address.host.any { it !in '0'..'9' && it != '.' })
            throw IOException("The server returned invalid SA:O connection details.")
        val payload = JSONObject().put("server_id", serverId).put("server_endpoint", endpoint)
        var response = request("/v1/tickets", payload, session)
        if (response.status == 428 && response.body.optString("error") == "android_proof_required") {
            val challenge = field(response.body, "challenge", 43)
            val proof = withContext(Dispatchers.IO) { createProof(challenge) }
            response = request("/v1/tickets", payload.put("android_proof", proof), session)
        }
        if (response.status == 401) throw SessionExpired()
        requireSuccess(response)
        return tokenField(response.body, "ticket", 4096)
    }

    private fun tokenField(body: JSONObject, fieldName: String, maximum: Int): String {
        val token = field(body, fieldName, maximum)
        if (token.any { it.code !in 33..126 }) throw IOException("The SA:O account service returned an invalid token.")
        return token
    }

    private fun requireSuccess(response: Response) {
        if (response.status !in 200..299) throw IOException("The SA:O account service refused the request. Please try again.")
    }

    private fun field(body: JSONObject, key: String, maximum: Int): String {
        val value = body.opt(key) as? String
        if (value.isNullOrEmpty() || value.toByteArray(Charsets.UTF_8).size > maximum || value.any { it.code < 32 || it.code == 127 })
            throw IOException("The SA:O account service returned an invalid response.")
        return value
    }

    companion object {
        const val BASE = "https://identity.mta-neon.com"
        private suspend fun post(path: String, body: JSONObject, token: String?): Response = withContext(Dispatchers.IO) {
            val connection = URL(BASE + path).openConnection() as HttpURLConnection
            try {
                connection.connectTimeout = 8_000
                connection.readTimeout = 8_000
                connection.instanceFollowRedirects = false
                connection.requestMethod = "POST"
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json")
                connection.setRequestProperty("Accept", "application/json")
                if (token != null) connection.setRequestProperty("Authorization", "Bearer $token")
                val bytes = body.toString().toByteArray(Charsets.UTF_8)
                connection.setFixedLengthStreamingMode(bytes.size)
                connection.outputStream.use { it.write(bytes) }
                val status = connection.responseCode
                if (status !in 200..299 && status != 428) return@withContext Response(status, JSONObject())
                if (connection.contentLengthLong > 131072) throw IOException("Invalid SA:O response size.")
                val stream = if (status == 428) connection.errorStream else connection.inputStream
                if (stream == null) return@withContext Response(status, JSONObject())
                val bytesRead = stream.use { input ->
                    val output = java.io.ByteArrayOutputStream()
                    val buffer = ByteArray(8192)
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        if (output.size() + count > 131072) throw IOException("Invalid SA:O response size.")
                        output.write(buffer, 0, count)
                    }
                    output.toByteArray()
                }
                if (bytesRead.size > 131072) throw IOException("Invalid SA:O response size.")
                val text = Charsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(bytesRead)).toString()
                Response(status, JSONObject(text))
            } finally {
                connection.disconnect()
            }
        }
    }
}
