package com.worldrpg.neonlauncher.utils

import org.json.JSONArray
import org.json.JSONObject
import org.json.JSONTokener
import java.io.IOException
import java.security.MessageDigest
import java.util.Locale

internal object GameAssetManifest {
    const val FILE_NAME = "manifest_android_mta.json"
    const val MAX_BYTES = 1024 * 1024
    const val MAX_FILES = 4096
    val requiredFiles = setOf("data/default.dat", "data/gta.dat", "data/handling.cfg", "texdb/gta3.img", "texdb/menu/menu.pvr.dat")
    private const val MAX_FILE_BYTES = 4L * 1024 * 1024 * 1024
    private val pathPart = Regex("[A-Za-z0-9_ .()\\-]{1,255}")
    private val sha256 = Regex("[0-9a-f]{64}")
    private val revision = Regex("[0-9a-f]{40}")

    data class Entry(val path: String, val size: Long, val sha256: String)
    data class Remote(val sourceRevision: String, val generationId: String, val json: String, val entries: List<Entry>)

    fun parseRemote(commitHeader: String?, json: String): Remote {
        val pinned = commitHeader?.trim()?.lowercase(Locale.ROOT)?.takeIf(revision::matches)
        // CDNs that omit the revision header serve main. Use the manifest's identity
        // for staging and verify every streamed file before activating the generation.
        val generationId = pinned ?: MessageDigest.getInstance("SHA-256")
            .apply { update(json.toByteArray(Charsets.UTF_8)) }.hexDigest().take(40)
        return Remote(pinned ?: "main", generationId, json, parse(json, local = false))
    }

    fun parse(json: String, local: Boolean): List<Entry> {
        val decoder = JSONTokener(json)
        val document = decoder.nextValue() as? JSONObject ?: throw IOException("Invalid game asset manifest.")
        if (decoder.nextClean() != '\u0000') throw IOException("Unexpected data after the game asset manifest.")
        val files = document.getJSONArray("files")
        if (files.length() !in 1..MAX_FILES || document.getInt("total_files") != files.length()) {
            throw IOException("Invalid game asset file count.")
        }
        val entries = List(files.length()) { index ->
            val item = files.getJSONObject(index)
            Entry(item.getString("path"), item.getLong("size"), item.optString("sha256", "").lowercase(Locale.ROOT))
        }
        validate(entries, local)
        if (entries.sumOf { it.size } != document.getLong("total_size")) throw IOException("Invalid game asset total size.")
        return entries
    }

    fun validate(entries: List<Entry>, local: Boolean) {
        if (entries.size !in 1..MAX_FILES) throw IOException("Invalid game asset file count.")
        val filesByPath = mutableSetOf<String>()
        val directories = mutableMapOf<String, String>()
        for (entry in entries) {
            val path = entry.path
            val parts = path.split('/')
            val normalized = path.lowercase(Locale.ROOT)
            if (path.length > 1024 || parts.any { !pathPart.matches(it) || it == "." || it == ".." } ||
                normalized == FILE_NAME || normalized.startsWith("$FILE_NAME/") ||
                normalized in directories || !filesByPath.add(normalized)
            ) throw IOException("Invalid or duplicate game asset path.")
            for (depth in 1 until parts.size) {
                val parent = parts.take(depth).joinToString("/")
                val folded = parent.lowercase(Locale.ROOT)
                if (folded in filesByPath || (directories.putIfAbsent(folded, parent) ?: parent) != parent) {
                    throw IOException("Conflicting game asset directory.")
                }
            }
            val minimum = if (local && path !in requiredFiles) 0L else 1L
            if (entry.size !in minimum..MAX_FILE_BYTES ||
                (!sha256.matches(entry.sha256) && !(local && entry.sha256.isEmpty()))
            ) throw IOException("Invalid game asset integrity metadata for $path.")
        }
        if (!entries.map { it.path }.containsAll(requiredFiles)) throw IOException("Incomplete game asset manifest.")
    }

    fun encode(entries: List<Entry>): String {
        val files = JSONArray()
        for (entry in entries) {
            files.put(JSONObject().put("path", entry.path).put("size", entry.size).put("sha256", entry.sha256))
        }
        val json = JSONObject().put("files", files).put("total_files", entries.size)
            .put("total_size", entries.sumOf { it.size }).toString()
        if (json.toByteArray(Charsets.UTF_8).size > MAX_BYTES) throw IOException("Game asset manifest is too large.")
        return json
    }
}

internal fun MessageDigest.hexDigest(): String {
    val bytes = digest()
    val hex = "0123456789abcdef"
    return buildString(bytes.size * 2) {
        for (byte in bytes) {
            val value = byte.toInt() and 0xff
            append(hex[value ushr 4])
            append(hex[value and 0xf])
        }
    }
}
