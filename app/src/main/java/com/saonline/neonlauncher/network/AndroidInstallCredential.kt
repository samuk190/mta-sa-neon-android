package com.saonline.neonlauncher.network

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.AtomicFile
import android.util.Base64
import com.google.android.gms.tasks.Tasks
import com.google.android.play.core.integrity.IntegrityManagerFactory
import com.google.android.play.core.integrity.StandardIntegrityManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.MessageDigest
import java.security.Signature
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.security.spec.ECGenParameterSpec
import java.util.concurrent.TimeUnit

/** The app obtains a certificate for its own private key. No external component supplies the key or the bytes sent for a Play verdict. */
internal object AndroidInstallCredential {
    private const val alias = "neon-android-quic-install-v1"
    private const val clientAuth = "1.3.6.1.5.5.7.3.2"
    private val lock = Mutex()

    suspend fun enroll(context: Context): X509Certificate? = withContext(Dispatchers.IO) {
        lock.withLock {
            val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
            val file = AtomicFile(File(context.noBackupFilesDir, "neon-install-certificate.der"))
            val key = if (store.containsAlias(alias)) store.getCertificate(alias).publicKey else {
                val generator = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, "AndroidKeyStore")
                generator.initialize(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_SIGN)
                    .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
                    .setDigests(KeyProperties.DIGEST_SHA256, KeyProperties.DIGEST_NONE)
                    .build())
                generator.generateKeyPair().public
            }
            val factory = CertificateFactory.getInstance("X.509")
            val cached = try {
                file.openRead().use { factory.generateCertificate(it) as X509Certificate }
            } catch (_: FileNotFoundException) { null }
              catch (_: java.security.cert.CertificateException) { null }
            if (cached != null && cached.publicKey.encoded.contentEquals(key.encoded) &&
                cached.notBefore.time <= System.currentTimeMillis() && cached.notAfter.time > System.currentTimeMillis() + 60_000) {
                return@withLock cached
            }

            val body = JSONObject().put("public_key", Base64.encodeToString(key.encoded, Base64.NO_WRAP))
            val start = NeonIdentityApi.post("/v1/android/installs/challenge", body, null)
            if (start.status == 404) return@withLock null
            if (start.status != 200) throw IOException("Android install enrollment is unavailable.")
            val challenge = start.body.optString("challenge")
            val project = start.body.optString("play_project_number").toLongOrNull()
            if (!challenge.matches(Regex("[A-Za-z0-9_-]{43}")) || project == null || project <= 0)
                throw IOException("Invalid Android install challenge.")
            val fingerprint = digest(key.encoded)
            val message = listOf("neon-android-install-v1", NeonIdentityApi.BASE, challenge, fingerprint)
                .joinToString("\u0000").toByteArray(Charsets.UTF_8)
            val possession = Signature.getInstance("SHA256withECDSA").run {
                initSign(store.getKey(alias, null) as java.security.PrivateKey)
                update(message)
                sign()
            }
            val manager = IntegrityManagerFactory.createStandard(context.applicationContext)
            val prepare = StandardIntegrityManager.PrepareIntegrityTokenRequest.builder().setCloudProjectNumber(project).build()
            val provider = Tasks.await(manager.prepareIntegrityToken(prepare), 30, TimeUnit.SECONDS)
            val request = StandardIntegrityManager.StandardIntegrityTokenRequest.builder().setRequestHash(digest(message)).build()
            val verdict = Tasks.await(provider.request(request), 20, TimeUnit.SECONDS)
            body.put("challenge", challenge)
                .put("signature", Base64.encodeToString(possession, Base64.NO_WRAP))
                .put("integrity_token", verdict.token())
            val issued = NeonIdentityApi.post("/v1/android/installs/enroll", body, null)
            if (issued.status != 201) throw IOException("Neon Identity could not verify this Android install.")
            val certificate = factory.generateCertificate(issued.body.getString("certificate_pem").byteInputStream()) as X509Certificate
            certificate.checkValidity()
            if (!certificate.publicKey.encoded.contentEquals(key.encoded) || certificate.basicConstraints >= 0 ||
                certificate.extendedKeyUsage != listOf(clientAuth)) throw IOException("Invalid Android transport certificate.")
            val output = file.startWrite()
            try {
                output.write(certificate.encoded)
                file.finishWrite(output)
            } catch (error: IOException) {
                file.failWrite(output)
                throw error
            }
            certificate
        }
    }

    private fun digest(data: ByteArray): String = Base64.encodeToString(MessageDigest.getInstance("SHA-256").digest(data),
        Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)
}
