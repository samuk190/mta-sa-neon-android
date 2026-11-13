package com.worldrpg.neonlauncher.utils

import android.content.Context
import android.net.Uri
import android.util.Log
import com.worldrpg.neonlauncher.BuildConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.sync.Mutex
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.Locale
import java.util.UUID

/** Installs and verifies GTA data from a local folder or configured CDN before making it available to the engine. */
class NativeAssetBridge(private val context: Context) {
    private val storage = File(context.filesDir, "game_assets")
    private val generations = File(storage, "generations")
    private val active = File(storage, "active")

    sealed interface DownloadState {
        data object FetchingManifest : DownloadState
        data class Verifying(val current: Int, val total: Int) : DownloadState
        data class Downloading(val fileName: String, val percent: Int) : DownloadState
        data class Complete(val fileCount: Int) : DownloadState
        data class Error(val message: String) : DownloadState
    }

    /** Search for a local GTA San Andreas Mobile 2.10 assets folder on device or external storage. */
    fun findLocalAssetsFolder(): File? {
        val candidates = listOfNotNull(
            BuildConfig.DEVICE_ASSETS_PATH.takeIf { it.isNotBlank() }?.let { File(it) },
            context.getExternalFilesDir(null)?.let { File(it, "game_assets") },
            context.getExternalFilesDir(null)?.let { File(it, "gta_assets") },
            File(storage, "local_source"),
            File("/sdcard/Android/data/${context.packageName}/files/game_assets"),
            File("/sdcard/Download/WorldRPG_backup"),
            File("/sdcard/worldrpg_gamefiles"),
            File("/sdcard/mta_assets"),
            File("/sdcard/GTASA")
        )
        return candidates.firstOrNull { dir ->
            try {
                dir.isDirectory && REQUIRED_FILES.all { File(dir, it).isFile }
            } catch (_: SecurityException) {
                false
            }
        }
    }

    /** Fast startup gate. Returns installed generation path or direct local assets path. */
    fun getReadyStoragePath(): String? {
        return try {
            val generation = activeGeneration()
            if (generation != null) {
                val entries = installedEntries(generation)
                if (entries != null && entries.filter { it.path in REQUIRED_FILES }.all { entry ->
                        val file = assetFile(generation, entry.path)
                        file.isFile && file.length() == entry.size
                    }) return generation.absolutePath
            }
            val localFolder = findLocalAssetsFolder()
            if (localFolder != null && REQUIRED_FILES.all { File(localFolder, it).isFile }) {
                return localFolder.absolutePath
            }
            null
        } catch (error: Exception) {
            Log.w(TAG, "Cannot read installed game assets", error)
            null
        }
    }

    fun syncAssets(): Flow<DownloadState> = flow {
        if (!installationLock.tryLock()) {
            emit(DownloadState.Error("Game assets are already syncing."))
            return@flow
        }

        try {
            // Priority 1: Check for local assets folder on device
            val localSource = findLocalAssetsFolder()
            if (localSource != null) {
                emit(DownloadState.FetchingManifest)
                syncFromLocalFolder(localSource) { emit(it) }
                return@flow
            }

            // Priority 2: Check for configured CDN / Local HTTP Server URL
            val cdnRoot = BuildConfig.CDN_ROOT_URL.ifBlank { DEFAULT_CDN_ROOT }
            if (cdnRoot.isBlank()) {
                val expectedPath = context.getExternalFilesDir(null)?.let { File(it, "game_assets").absolutePath }
                    ?: BuildConfig.DEVICE_ASSETS_PATH
                emit(DownloadState.Error(
                    "Local game assets not found!\n" +
                    "Please place original GTA Mobile 2.10 files in:\n" +
                    "$expectedPath\n" +
                    "or configure GAME_ASSETS_DIR in .env"
                ))
                return@flow
            }

            emit(DownloadState.FetchingManifest)
            val manifest = fetchManifest(cdnRoot)
            val previous = activeGeneration()
            val unchanged = previous != null && installedEntries(previous) == manifest.entries &&
                manifest.entries.withIndex().all { (index, entry) ->
                    emit(DownloadState.Verifying(index + 1, manifest.entries.size))
                    matches(assetFile(previous, entry.path), entry)
                }
            if (unchanged) {
                emit(DownloadState.Complete(manifest.entries.size))
                return@flow
            }

            ensureDirectory(generations)
            val staging = assetFile(storage, "staging-${manifest.revision}")
            ensureDirectory(staging)
            val partial = assetFile(storage, "asset-download.part")
            try {
                manifest.entries.forEachIndexed { index, entry ->
                    emit(DownloadState.Verifying(index + 1, manifest.entries.size))
                    val target = assetFile(staging, entry.path)
                    if (matches(target, entry)) return@forEachIndexed
                    ensureDirectory(target.parentFile ?: staging)
                    if (staging.usableSpace < entry.size) {
                        throw IOException("Not enough free space for game assets.")
                    }
                    val old = previous?.let { assetFile(it, entry.path) }
                    if (old != null && matches(old, entry)) {
                        old.inputStream().buffered().use { input ->
                            copyVerified(input, partial, entry) {}
                        }
                    } else {
                        val path = entry.path.split('/').joinToString("/") { Uri.encode(it) }
                        val url = URL("$cdnRoot/${manifest.revision}/$CONTENT_DIR/$path")
                        val connection = open(url)
                        try {
                            emit(DownloadState.Downloading(entry.path, 0))
                            connection.inputStream.buffered().use { input ->
                                copyVerified(input, partial, entry) { percent ->
                                    emit(DownloadState.Downloading(entry.path, percent))
                                }
                            }
                        } finally {
                            connection.disconnect()
                        }
                    }
                    atomicMove(partial, target)
                }

                currentCoroutineContext().ensureActive()
                val installed = File(generations, "${manifest.revision}-${UUID.randomUUID()}")
                writeSynced(File(staging, MANIFEST_NAME), manifest.json)
                atomicMove(staging, installed)
                writeSynced(partial, installed.name)
                atomicMove(partial, active)
            } finally {
                partial.delete()
            }
            emit(DownloadState.Complete(manifest.entries.size))
        } catch (error: IOException) {
            Log.w(TAG, "Game asset sync failed", error)
            emit(DownloadState.Error(error.message ?: "Game asset sync failed."))
        } catch (error: JSONException) {
            Log.w(TAG, "Invalid game asset manifest", error)
            emit(DownloadState.Error("Invalid game asset manifest."))
        } finally {
            installationLock.unlock()
        }
    }.flowOn(Dispatchers.IO)

    private suspend fun syncFromLocalFolder(
        sourceDir: File,
        emitState: suspend (DownloadState) -> Unit
    ) {
        val files = mutableListOf<File>()
        sourceDir.walkTopDown().filter { it.isFile && it.name != MANIFEST_NAME }.forEach { files.add(it) }

        val entries = mutableListOf<Entry>()
        val total = files.size
        ensureDirectory(generations)
        val staging = assetFile(storage, "staging-local")
        ensureDirectory(staging)

        files.forEachIndexed { index, file ->
            val relativePath = file.relativeTo(sourceDir).path.replace('\\', '/')
            emitState(DownloadState.Verifying(index + 1, total))

            val target = assetFile(staging, relativePath)
            ensureDirectory(target.parentFile ?: staging)
            if (!target.isFile || target.length() != file.length()) {
                file.copyTo(target, overwrite = true)
            }
            entries.add(Entry(relativePath, file.length(), ""))
        }

        val jsonArray = JSONArray()
        entries.forEach { entry ->
            val obj = JSONObject()
            obj.put("path", entry.path)
            obj.put("size", entry.size)
            obj.put("sha256", "")
            jsonArray.put(obj)
        }
        val manifestObj = JSONObject().apply {
            put("files", jsonArray)
            put("total_files", entries.size)
            put("total_size", entries.sumOf { it.size })
        }

        val installed = File(generations, "local-${UUID.randomUUID()}")
        writeSynced(File(staging, MANIFEST_NAME), manifestObj.toString())
        atomicMove(staging, installed)
        val partial = assetFile(storage, "asset-download.part")
        writeSynced(partial, installed.name)
        atomicMove(partial, active)

        emitState(DownloadState.Complete(entries.size))
    }

    private data class Entry(val path: String, val size: Long, val sha256: String)
    private data class Manifest(val revision: String, val json: String, val entries: List<Entry>)

    private suspend fun fetchManifest(cdnRoot: String): Manifest {
        val connection = open(URL("$cdnRoot/main/$MANIFEST_NAME"))
        try {
            val revision = connection.getHeaderField("X-Repo-Commit") ?: "head"
            val json = connection.inputStream.use { input ->
                val output = ByteArrayOutputStream()
                val buffer = ByteArray(BUFFER_SIZE)
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (output.size() + count > MAX_MANIFEST_BYTES) {
                        throw IOException("Game asset manifest is too large.")
                    }
                    output.write(buffer, 0, count)
                }
                output.toString(Charsets.UTF_8.name())
            }
            return Manifest(revision, json, parseManifest(json))
        } finally {
            connection.disconnect()
        }
    }

    private fun parseManifest(json: String): List<Entry> {
        val document = JSONObject(json)
        val files = document.getJSONArray("files")
        if (files.length() !in 1..4096 || document.getInt("total_files") != files.length()) {
            throw IOException("Invalid game asset file count.")
        }
        val filesByPath = mutableSetOf<String>()
        val directories = mutableMapOf<String, String>()
        val entries = List(files.length()) { index ->
            val item = files.getJSONObject(index)
            val path = item.getString("path")
            val parts = path.split('/')
            val normalized = path.lowercase(Locale.ROOT)
            if (path.length > 1024 || parts.any { !PATH_PART.matches(it) || it == "." || it == ".." } ||
                normalized == MANIFEST_NAME || normalized.startsWith("$MANIFEST_NAME/") ||
                normalized in directories || !filesByPath.add(normalized)
            ) throw IOException("Invalid or duplicate game asset path.")
            for (depth in 1 until parts.size) {
                val parent = parts.take(depth).joinToString("/")
                val folded = parent.lowercase(Locale.ROOT)
                if (folded in filesByPath || (directories.putIfAbsent(folded, parent) ?: parent) != parent) {
                    throw IOException("Conflicting game asset directory.")
                }
            }
            val size = item.getLong("size")
            val sha = item.optString("sha256", "").lowercase(Locale.ROOT)
            if (size !in 1..MAX_ASSET_BYTES || (sha.isNotEmpty() && !SHA256.matches(sha))) {
                throw IOException("Invalid game asset integrity metadata.")
            }
            Entry(path, size, sha)
        }
        if (!entries.map { it.path }.containsAll(REQUIRED_FILES) ||
            entries.sumOf { it.size } != document.getLong("total_size")
        ) throw IOException("Incomplete game asset manifest.")
        return entries
    }

    private fun installedEntries(directory: File): List<Entry>? {
        return try {
            val file = assetFile(directory, MANIFEST_NAME)
            if (file.length() !in 1..MAX_MANIFEST_BYTES.toLong()) return null
            parseManifest(file.readText())
        } catch (_: IOException) {
            null
        } catch (_: JSONException) {
            null
        }
    }

    private fun activeGeneration(): File? {
        if (!active.isFile || active.length() !in 1..128) return null
        val name = active.readText()
        if (!GENERATION.matches(name)) return null
        val directory = assetFile(generations, name)
        return directory.takeIf { it.isDirectory && File(it, MANIFEST_NAME).isFile }
    }

    private fun assetFile(root: File, path: String): File {
        val file = File(root.canonicalFile, path)
        if (file.canonicalFile != file) throw IOException("Unsafe game asset path.")
        return file
    }

    private fun ensureDirectory(directory: File) {
        if (!directory.isDirectory && !directory.mkdirs()) {
            throw IOException("Cannot create game asset storage.")
        }
    }

    private fun writeSynced(file: File, contents: String) {
        FileOutputStream(file).use { output ->
            output.write(contents.toByteArray(Charsets.UTF_8))
            output.fd.sync()
        }
    }

    private fun atomicMove(from: File, to: File) {
        Files.move(from.toPath(), to.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
    }

    private suspend fun matches(file: File, entry: Entry): Boolean {
        if (!file.isFile || file.length() != entry.size) return false
        if (entry.sha256.isEmpty()) return true
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER_SIZE)
        file.inputStream().buffered().use { input ->
            while (true) {
                currentCoroutineContext().ensureActive()
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.hex() == entry.sha256
    }

    private suspend fun copyVerified(
        input: InputStream,
        partial: File,
        entry: Entry,
        progress: suspend (Int) -> Unit
    ) {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER_SIZE)
        var total = 0L
        var previousPercent = 0
        FileOutputStream(partial).use { output ->
            while (true) {
                currentCoroutineContext().ensureActive()
                val count = input.read(buffer)
                if (count < 0) break
                total += count
                if (total > entry.size) throw IOException("Unexpected size for ${entry.path}.")
                digest.update(buffer, 0, count)
                output.write(buffer, 0, count)
                val percent = (100L * total / entry.size).toInt().coerceAtMost(99)
                if (percent > previousPercent) {
                    progress(percent)
                    previousPercent = percent
                }
            }
            if (total != entry.size || (entry.sha256.isNotEmpty() && digest.hex() != entry.sha256)) {
                throw IOException("Integrity check failed for ${entry.path}.")
            }
            output.fd.sync()
        }
    }

    private fun MessageDigest.hex(): String = digest().joinToString("") { "%02x".format(it) }

    private fun open(url: URL): HttpURLConnection {
        var endpoint = url
        repeat(6) {
            if (endpoint.protocol != "https" && endpoint.protocol != "http") {
                throw IOException("Game asset URLs must use HTTP or HTTPS.")
            }
            val connection = (endpoint.openConnection() as HttpURLConnection).apply {
                instanceFollowRedirects = false
                connectTimeout = 15_000
                readTimeout = 30_000
            }
            var keepOpen = false
            try {
                val status = connection.responseCode
                if (status == HttpURLConnection.HTTP_OK) {
                    keepOpen = true
                    return connection
                }
                if (status !in REDIRECTS) throw IOException("Game assets CDN returned HTTP $status.")
                val location = connection.getHeaderField("Location")
                    ?: throw IOException("Missing game asset redirect.")
                endpoint = URL(endpoint, location)
            } finally {
                if (!keepOpen) connection.disconnect()
            }
        }
        throw IOException("Too many game asset redirects.")
    }

    private companion object {
        const val TAG = "NativeAssetBridge"
        const val DEFAULT_CDN_ROOT = ""
        const val CONTENT_DIR = "game_files_android_mta"
        const val MANIFEST_NAME = "manifest_android_mta.json"
        const val BUFFER_SIZE = 64 * 1024
        const val MAX_MANIFEST_BYTES = 1024 * 1024
        const val MAX_ASSET_BYTES = 4L * 1024 * 1024 * 1024
        val REVISION = Regex("([0-9a-f]{40}|head)")
        val SHA256 = Regex("[0-9a-f]{64}")
        val GENERATION = Regex("([0-9a-f]{40}|local)-[0-9a-f-]{36}")
        val PATH_PART = Regex("[A-Za-z0-9_ .()\\-]{1,255}")
        val REQUIRED_FILES = setOf("data/gta.dat", "data/handling.cfg", "texdb/gta3.img", "texdb/menu/menu.pvr.dat")
        val REDIRECTS = setOf(301, 302, 303, 307, 308)
        val installationLock = Mutex()
    }
}
