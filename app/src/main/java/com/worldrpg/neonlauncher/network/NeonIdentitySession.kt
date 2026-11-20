package com.worldrpg.neonlauncher.network

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.AtomicFile
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Keystore-bound AES-GCM, outside backups; file locks also cover the :engine process. */
class NeonIdentitySession(context: Context) {
    private val directory = context.applicationContext.noBackupFilesDir
    private val file = AtomicFile(File(directory, "neon-identity.session"))
    private val lockFile = File(directory, "neon-identity.lock")

    private fun <T> locked(block: () -> T): T = synchronized(LOCK) {
        FileOutputStream(lockFile, true).channel.use { channel -> channel.lock().use { block() } }
    }

    fun load(): String? = try {
        locked { readToken() }
    } catch (_: IOException) {
        null
    }

    private fun readToken(): String? =
        try {
            // openRead restores AtomicFile's previous complete write first.
            val bytes = file.openRead().use { input ->
                check(input.channel.size() in 29..131072)
                input.readBytes()
            }
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, key(false), GCMParameterSpec(128, bytes.copyOfRange(0, 12)))
            val token = cipher.doFinal(bytes, 12, bytes.size - 12).toString(Charsets.UTF_8)
            check(token.isNotEmpty() && token.length <= 65536 && token.all { it.code in 33..126 })
            token
        } catch (_: Exception) {
            null
        }

    fun save(token: String) = locked {
        require(token.isNotEmpty() && token.length <= 65536 && token.all { it.code in 33..126 })
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key(true))
        val bytes = cipher.iv + cipher.doFinal(token.toByteArray(Charsets.UTF_8))
        val output = file.startWrite()
        try {
            output.write(bytes)
            file.finishWrite(output)
        } catch (error: Exception) {
            file.failWrite(output)
            throw error
        }
    }

    fun clear() = locked { file.delete() }

    fun clearIfCurrent(token: String) = locked {
        if (readToken() == token) file.delete()
    }

    private fun key(create: Boolean): SecretKey {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (store.getKey(ALIAS, null) as? SecretKey)?.let { return it }
        check(create)
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
            init(KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
        }.generateKey()
    }

    private companion object {
        val LOCK = Any()
        const val ALIAS = "worldrpg.neon.identity.v1"
    }
}
