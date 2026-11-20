package org.mtamobile.identity

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.Signature
import java.security.spec.ECGenParameterSpec
import java.util.UUID

/** Call on a worker thread after the trusted Neon Identity issuer returns HTTP 428. */
object AndroidTicketProof {
    fun create(challenge: String): JSONObject {
        require(challenge.matches(Regex("[A-Za-z0-9_-]{43}")))
        val nonce = Base64.decode(challenge, Base64.URL_SAFE or Base64.NO_PADDING or Base64.NO_WRAP)
        val alias = "neon-ticket-" + UUID.randomUUID()
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        try {
            val spec = KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_SIGN)
                .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
                .setDigests(KeyProperties.DIGEST_SHA256)
                .setAttestationChallenge(nonce)
                .build()
            val generator = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, "AndroidKeyStore")
            generator.initialize(spec)
            val key = generator.generateKeyPair()
            val signature = Signature.getInstance("SHA256withECDSA").run {
                initSign(key.private)
                update("neon-android-ticket-v1\u0000".toByteArray(Charsets.US_ASCII))
                update(nonce)
                sign()
            }
            val chain = JSONArray()
            store.getCertificateChain(alias).forEach { chain.put(Base64.encodeToString(it.encoded, Base64.NO_WRAP)) }
            return JSONObject().put("challenge", challenge).put("chain", chain)
                .put("signature", Base64.encodeToString(signature, Base64.NO_WRAP))
        } finally {
            store.deleteEntry(alias)
        }
    }
}
