package com.worldrpg.neonlauncher.network

import kotlinx.coroutines.runBlocking
import org.json.JSONObject

/** Exercises the real HTTP contract with an in-memory service, without browser/account side effects. */
fun main() = runBlocking {
    var count = 0
    var calls = 0
    val api = NeonIdentityApi { path, body, token ->
        calls++
        when (path) {
            "/v1/auth/discord/start" -> {
                check(token == null && body.length() == 0)
                NeonIdentityApi.Response(200, JSONObject().put("flow_id", "flow").put("poll_token", "poll-secret")
                    .put("authorization_url", "${NeonIdentityApi.BASE}/v1/auth/discord/authorize#flow"))
            }
            "/v1/auth/discord/poll" -> {
                check(token == null && body.getString("flow_id") == "flow" && body.getString("poll_token") == "poll-secret")
                NeonIdentityApi.Response(200, if (count++ == 0) JSONObject().put("status", "pending") else JSONObject().put("session_token", "session-secret"))
            }
            "/v1/tickets" -> {
                check(token == "session-secret")
                check(body.getString("server_id") == "server-id" && body.getString("server_endpoint") == "1.2.3.4:22003")
                NeonIdentityApi.Response(200, JSONObject().put("ticket", "T".repeat(1024)))
            }
            else -> error("unexpected request")
        }
    }
    val flow = api.start()
    check(api.poll(flow) == null)
    val session = api.poll(flow)!!
    check(api.ticket(session, "server-id", "1.2.3.4:22003") == "T".repeat(1024))
    check(calls == 4)

    suspend fun rejected(block: suspend () -> Unit) {
        var failed = false
        try { block() } catch (_: java.io.IOException) { failed = true }
        check(failed)
    }
    for (url in listOf("http://identity.mta-neon.com/v1/auth/discord/authorize#x",
        "https://identity.mta-neon.com.attacker.test/v1/auth/discord/authorize#x", "https://attacker.test/")) {
        val wrongUrl = NeonIdentityApi { _, _, _ -> NeonIdentityApi.Response(200, JSONObject()
            .put("flow_id", "x").put("poll_token", "y").put("authorization_url", url)) }
        rejected { wrongUrl.start() }
    }
    val expired = NeonIdentityApi { _, _, _ -> NeonIdentityApi.Response(401, JSONObject()) }
    var needsLogin = false
    try { expired.ticket(session, "server-id", "1.2.3.4:22003") } catch (_: NeonIdentityApi.SessionExpired) { needsLogin = true }
    check(needsLogin)
    val oversized = NeonIdentityApi { _, _, _ -> NeonIdentityApi.Response(200, JSONObject().put("ticket", "T".repeat(4097))) }
    rejected { oversized.ticket(session, "server-id", "1.2.3.4:22003") }
    for (malformed in listOf("has spaces", "non-ascii-é", "bad\u0000token")) {
        val wrongToken = NeonIdentityApi { _, _, _ -> NeonIdentityApi.Response(200,
            JSONObject().put("ticket", malformed).put("session_token", malformed)) }
        rejected { wrongToken.poll(flow) }
        rejected { wrongToken.ticket(session, "server-id", "1.2.3.4:22003") }
    }
    val challenge = "A".repeat(43)
    var proofRequests = 0
    val protected = NeonIdentityApi(createProof = {
        check(it == challenge)
        JSONObject().put("challenge", it)
    }) { path, body, token ->
        check(path == "/v1/tickets" && token == session)
        check(body.getString("server_id") == "server-id" && body.getString("server_endpoint") == "1.2.3.4:22003")
        if (++proofRequests == 1) {
            check(!body.has("android_proof"))
            NeonIdentityApi.Response(428, JSONObject().put("error", "android_proof_required").put("challenge", challenge))
        } else {
            check(body.getJSONObject("android_proof").getString("challenge") == challenge)
            NeonIdentityApi.Response(200, JSONObject().put("ticket", "signed-ticket"))
        }
    }
    check(protected.ticket(session, "server-id", "1.2.3.4:22003") == "signed-ticket" && proofRequests == 2)
    val invalid = NeonIdentityApi { _, _, _ -> error("invalid endpoint must not reach the service") }
    rejected { invalid.ticket(session, "server-id", "https://attacker.test") }
    for (endpoint in listOf("999.1.2.3:22003", "1.2.3.4:0", "1.2.3.4:65536", "01.2.3.4:22003", "1.2.3.4:02203"))
        rejected { invalid.ticket(session, "server-id", endpoint) }
    println("ALL NEON IDENTITY CONTRACT TESTS PASSED")
}
