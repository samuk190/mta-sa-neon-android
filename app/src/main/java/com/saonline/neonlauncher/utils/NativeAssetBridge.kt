package com.saonline.neonlauncher.utils

import android.content.Context
import android.net.Uri
import android.util.Log
import com.saonline.neonlauncher.BuildConfig
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.utils.GameAssetManifest.Entry
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONException
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.UncheckedIOException
import java.net.HttpURLConnection
import java.net.URL
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

data class GameFileStatus(val path: String, val size: Long, val verified: Boolean)
data class GameFileInventory(val files: List<GameFileStatus>, val localReference: Boolean)

internal data class LocalGameFile(val entry: Entry, val open: () -> InputStream)
internal class InvalidGameFolder(message: String) : IOException(message)

/** Installs and verifies GTA data from a local folder or configured CDN before making it available to the engine. */
class NativeAssetBridge(private val context: Context) {
    private val storage = File(context.filesDir, "game_assets")
    private val generations = File(storage, "generations")
    private val active = File(storage, "active")
    private val cdnRoot = BuildConfig.CDN_ROOT_URL.trim().trimEnd('/').ifBlank { DEFAULT_CDN_ROOT }

    fun usesCdn(): Boolean = cdnRoot.isNotBlank()

    sealed interface DownloadState {
        data object FetchingManifest : DownloadState
        data class Verifying(val current: Int, val total: Int) : DownloadState
        data class Syncing(val current: Int, val total: Int, val percent: Int) : DownloadState
        data class Complete(val fileCount: Int) : DownloadState
        data class Error(val reason: SyncFailure, val detail: String? = null) : DownloadState
    }

    enum class SyncFailure {
        ALREADY_RUNNING, LOCAL_FILES_MISSING, INSUFFICIENT_SPACE, STORAGE_UNAVAILABLE,
        INVALID_MANIFEST, INVALID_LOCAL_FOLDER, FILE_INVALID, HTTP_ERROR, IO_ERROR
    }

    private fun localFolderCandidates() = listOfNotNull(
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

    /** Local developer installs are available only when no CDN is configured. */
    fun findLocalAssetsFolder(): File? {
        // A leftover backup must never override the CDN, including library loading at boot.
        if (cdnRoot.isNotBlank()) return null
        return localFolderCandidates().firstOrNull { dir ->
            try {
                dir.isDirectory && GameAssetManifest.requiredFiles.all { path ->
                    File(dir, path).let { it.isFile && it.length() > 0 }
                }
            } catch (_: SecurityException) {
                false
            }
        }
    }

    // Metadata only, from the Android 2.10 reference revision linked in game_assets/README.md.
    internal fun referenceEntries(): List<Entry> =
        context.resources.openRawResource(R.raw.game_asset_reference).bufferedReader().use {
            GameAssetManifest.parse(it.readText(), local = false)
        }

    /** Reports files whose bytes match the expected version, including reusable CDN staging files. */
    suspend fun gameFileInventory(): GameFileInventory = withContext(Dispatchers.IO) {
        val activeDir = activeGeneration()
        val cdn = usesCdn()
        val current = activeDir?.takeIf { cdn && !it.name.startsWith("local-") }
        val manifest = try {
            if (cdn) fetchManifest(cdnRoot) else null
        } catch (error: IOException) {
            if (error is SyncException && error.reason == SyncFailure.INVALID_MANIFEST) throw error
            currentCoroutineContext().ensureActive()
            null
        }
        val installed = current?.let(::installedEntries)
        val entries = manifest?.entries ?: installed ?: referenceEntries()
        val candidates = when {
            manifest != null -> {
                val previous = matchingGeneration(current, installed, entries) ?: current
                val staging = assetFile(storage, "staging-${manifest.generationId}").takeIf { it.isDirectory }
                listOfNotNull(staging, previous)
            }
            cdn -> listOfNotNull(current)
            else -> {
                // A pre-existing local installation can be used directly by the engine.
                val local = activeDir ?: findLocalAssetsFolder() ?: localFolderCandidates().firstOrNull { it.isDirectory }
                listOfNotNull(local)
            }
        }
        val verified = BooleanArray(entries.size)
        parallelFiles(entries.size, DISK_WORKERS) { _, index ->
            val entry = entries[index]
            verified[index] = candidates.any { directory ->
                try {
                    matches(assetFile(directory, entry.path), entry)
                } catch (_: IOException) {
                    false
                } catch (_: SecurityException) {
                    false
                }
            }
        }
        val files = entries.mapIndexed { index, entry -> GameFileStatus(entry.path, entry.size, verified[index]) }
        GameFileInventory(files, localReference = manifest == null)
    }

    private fun matchingGeneration(current: File?, currentEntries: List<Entry>?, expected: List<Entry>): File? {
        if (currentEntries == expected) return current
        return generations.listFiles().orEmpty()
            .asSequence()
            .filter { it != current && it.isDirectory && GENERATION.matches(it.name) && !it.name.startsWith("local-") }
            .sortedByDescending { it.lastModified() }
            .firstOrNull { installedEntries(it) == expected }
    }

    /** Fast startup gate. CDN builds never fall back to an external folder or local import. */
    fun getReadyStoragePath(): String? {
        return try {
            val generation = activeGeneration()
            if (generation != null && (cdnRoot.isBlank() || !generation.name.startsWith("local-"))) {
                val entries = installedEntries(generation)
                if (entries != null && entries.all { entry ->
                        val file = assetFile(generation, entry.path)
                        file.isFile && file.length() == entry.size
                    }) return generation.absolutePath
            }
            findLocalAssetsFolder()?.absolutePath
        } catch (error: Exception) {
            Log.w(TAG, "Cannot read installed game assets", error)
            null
        }
    }

    fun syncAssets(): Flow<DownloadState> = syncAssets(null)

    internal fun syncAssets(pickedFolder: (suspend () -> List<LocalGameFile>)?): Flow<DownloadState> = channelFlow {
        if (!installationLock.tryLock()) {
            send(DownloadState.Error(SyncFailure.ALREADY_RUNNING))
            return@channelFlow
        }

        try {
            if (!usesCdn() && pickedFolder != null) {
                send(DownloadState.FetchingManifest)
                val reference = referenceEntries().associateBy(Entry::path)
                val files = pickedFolder().map { file ->
                    val expected = reference[file.entry.path]
                    if (expected != null && expected.size != file.entry.size) {
                        throw AssetContentsException(file.entry.path, "Source size differs from the GTA 2.10 reference.")
                    }
                    if (expected == null) file else file.copy(entry = expected)
                }
                syncFromLocalFiles(files) { send(it) }
                return@channelFlow
            }
            // findLocalAssetsFolder is disabled when a CDN is configured.
            val localSource = findLocalAssetsFolder()
            if (localSource != null) {
                send(DownloadState.FetchingManifest)
                val root = localSource.canonicalFile
                val files = localEntries(root).map { entry ->
                    LocalGameFile(entry) { assetFile(root, entry.path).inputStream() }
                }
                syncFromLocalFiles(files) { send(it) }
                return@channelFlow
            }

            // Sync the configured CDN into private storage before engine startup.
            if (cdnRoot.isBlank()) {
                val expectedPath = context.getExternalFilesDir(null)?.let { File(it, "game_assets").absolutePath }
                    ?: BuildConfig.DEVICE_ASSETS_PATH
                send(DownloadState.Error(SyncFailure.LOCAL_FILES_MISSING, expectedPath))
                return@channelFlow
            }

            send(DownloadState.FetchingManifest)
            val manifest = fetchManifest(cdnRoot)
            // Reuse a fully installed generation if a previous sync stopped before switching the pointer.
            val current = activeGeneration()?.takeUnless { it.name.startsWith("local-") }
            val currentEntries = current?.let(::installedEntries)
            val matching = matchingGeneration(current, currentEntries, manifest.entries)
            val previous = matching ?: current
            if (matching != null && verifyGeneration(matching, manifest.entries) { send(it) }) {
                if (matching != current) activateGeneration(matching)
                send(DownloadState.Complete(manifest.entries.size))
                return@channelFlow
            }

            ensureDirectory(generations)
            val staging = assetFile(storage, "staging-${manifest.generationId}")
            ensureDirectory(staging)
            val partials = List(DOWNLOAD_WORKERS) { assetFile(storage, "asset-download-$it.part") }
            // A previous process may have stopped while a worker was writing.
            assetFile(storage, "asset-download.part").delete()
            partials.forEach { it.delete() }
            val progress = SyncProgress(manifest.entries) { send(it) }
            progress.start()
            val previousByPath = currentEntries?.takeIf { previous == current }?.associateBy(Entry::path)
            // Start the large archives early so they can overlap other transfers.
            val pending = manifest.entries.sortedByDescending(Entry::size)
            try {
                parallelFiles(pending.size, DOWNLOAD_WORKERS) { worker, index ->
                    val entry = pending[index]
                    val target = assetFile(staging, entry.path)
                    if (matches(target, entry)) {
                        progress.advance(entry.size, finished = true)
                        return@parallelFiles
                    }
                    ensureDirectory(target.parentFile ?: staging)
                    if (staging.usableSpace < entry.size) {
                        throw SyncException(SyncFailure.INSUFFICIENT_SPACE, "Not enough free space for game assets.")
                    }
                    val partial = partials[worker]
                    val old = previous?.takeIf { previousByPath == null || previousByPath[entry.path] == entry }
                        ?.let { assetFile(it, entry.path) }
                    var reported = 0L
                    if (!copyMatchingAsset(old, partial, entry)) {
                        val path = entry.path.split('/').joinToString("/") { Uri.encode(it) }
                        val url = URL("$cdnRoot/${manifest.sourceRevision}/$CONTENT_DIR/$path")
                        val connection = open(url)
                        try {
                            connection.inputStream.use { input ->
                                copyVerified(input, partial, entry) { total ->
                                    progress.advance(total - reported)
                                    reported = total
                                }
                            }
                        } finally {
                            connection.disconnect()
                        }
                    }
                    atomicMove(partial, target)
                    progress.advance(entry.size - reported, finished = true)
                }

                currentCoroutineContext().ensureActive()
                val installed = File(generations, "${manifest.generationId}-${UUID.randomUUID()}")
                writeSynced(File(staging, GameAssetManifest.FILE_NAME), manifest.json)
                currentCoroutineContext().ensureActive()
                atomicMove(staging, installed)
                activateGeneration(installed)
            } finally {
                partials.forEach { it.delete() }
            }
            send(DownloadState.Complete(manifest.entries.size))
        } catch (error: IOException) {
            Log.w(TAG, "Game asset sync failed", error)
            send(when (error) {
                is SyncException -> DownloadState.Error(error.reason, error.detail)
                is InvalidGameFolder -> DownloadState.Error(SyncFailure.INVALID_LOCAL_FOLDER)
                is AssetContentsException -> DownloadState.Error(SyncFailure.FILE_INVALID, error.path)
                else -> DownloadState.Error(SyncFailure.IO_ERROR)
            })
        } catch (error: SecurityException) {
            Log.w(TAG, "Cannot access game asset storage", error)
            send(DownloadState.Error(SyncFailure.STORAGE_UNAVAILABLE))
        } catch (error: JSONException) {
            Log.w(TAG, "Invalid game asset manifest", error)
            send(DownloadState.Error(SyncFailure.INVALID_MANIFEST))
        } finally {
            installationLock.unlock()
        }
    }.flowOn(Dispatchers.IO)

    private suspend fun verifyGeneration(
        directory: File,
        entries: List<Entry>,
        emitState: suspend (DownloadState) -> Unit
    ): Boolean {
        val mismatch = AtomicBoolean()
        val progressLock = Mutex()
        var checked = 0
        emitState(DownloadState.Verifying(0, entries.size))
        parallelFiles(entries.size, DISK_WORKERS) { _, index ->
            if (mismatch.get()) return@parallelFiles
            val entry = entries[index]
            if (!matches(assetFile(directory, entry.path), entry)) mismatch.set(true)
            progressLock.withLock { emitState(DownloadState.Verifying(++checked, entries.size)) }
        }
        return !mismatch.get()
    }

    private suspend fun copyMatchingAsset(source: File?, partial: File, entry: Entry): Boolean {
        if (source == null || !source.isFile || source.length() != entry.size) return false
        return try {
            source.inputStream().use { copyVerified(it, partial, entry) }
            true
        } catch (_: AssetContentsException) {
            partial.delete()
            false
        }
    }

    private suspend fun syncFromLocalFiles(
        sources: List<LocalGameFile>,
        emitState: suspend (DownloadState) -> Unit
    ) {
        val files = sources.map(LocalGameFile::entry)
        try {
            GameAssetManifest.validate(files, local = true)
        } catch (error: IOException) {
            throw InvalidGameFolder(error.message.orEmpty())
        }
        val current = activeGeneration()
        val previous = current?.let(::installedEntries)
        if (current != null && previous != null && sameLocalFiles(sources, previous) &&
            verifyGeneration(current, previous, emitState)) {
            emitState(DownloadState.Complete(files.size))
            return
        }

        ensureDirectory(generations)
        val installed = File(generations, "local-${UUID.randomUUID()}")
        val staging = assetFile(storage, "staging-local-import")
        // A process killed mid-import leaves this private staging folder behind.
        if (staging.exists() && !staging.deleteRecursively()) {
            throw SyncException(SyncFailure.STORAGE_UNAVAILABLE, "Cannot clear an unfinished game import.")
        }
        if (generations.usableSpace < files.sumOf(Entry::size)) {
            throw SyncException(SyncFailure.INSUFFICIENT_SPACE, "Not enough free space for game assets.")
        }
        ensureDirectory(staging)
        try {
            val progress = SyncProgress(files, emitState)
            val verified = arrayOfNulls<Entry>(files.size)
            progress.start()
            parallelFiles(files.size, DISK_WORKERS) { _, index ->
                val entry = files[index]
                val target = assetFile(staging, entry.path)
                ensureDirectory(target.parentFile ?: staging)
                var reported = 0L
                val sha = sources[index].open().use { input ->
                    copyVerified(input, target, entry) { total ->
                        progress.advance(total - reported)
                        reported = total
                    }
                }
                verified[index] = entry.copy(sha256 = sha)
                progress.advance(entry.size - reported, finished = true)
            }
            writeSynced(File(staging, GameAssetManifest.FILE_NAME), GameAssetManifest.encode(verified.map { checkNotNull(it) }))
            currentCoroutineContext().ensureActive()
            atomicMove(staging, installed)
            activateGeneration(installed)
        } finally {
            staging.deleteRecursively()
        }
        emitState(DownloadState.Complete(files.size))
    }

    private suspend fun sameLocalFiles(sources: List<LocalGameFile>, installed: List<Entry>): Boolean {
        if (sources.size != installed.size) return false
        val byPath = installed.associateBy(Entry::path)
        return sources.all { source ->
            val expected = source.entry
            val old = byPath[expected.path]
            old != null && old.sha256.isNotEmpty() && old.size == expected.size &&
                (expected.sha256.ifEmpty { source.open().use { sha256(it) } } == old.sha256)
        }
    }

    private suspend fun localEntries(sourceDir: File): List<Entry> {
        val root = sourceDir.toPath()
        val files = mutableListOf<Entry>()
        try {
            // Files.walk does not follow symbolic links unless FOLLOW_LINKS is requested.
            Files.walk(root).use { paths ->
                val iterator = paths.iterator()
                while (iterator.hasNext()) {
                    currentCoroutineContext().ensureActive()
                    val path = iterator.next()
                    if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)) continue
                    val relative = root.relativize(path).toString()
                    if (relative == GameAssetManifest.FILE_NAME) continue
                    if (files.size == GameAssetManifest.MAX_FILES) throw IOException("Too many local game asset files.")
                    files.add(Entry(relative, Files.size(path), ""))
                }
            }
        } catch (error: UncheckedIOException) {
            throw IOException("Cannot read local game assets.", error.cause)
        }
        return files
    }

    private suspend fun fetchManifest(cdnRoot: String): GameAssetManifest.Remote {
        val connection = open(URL("$cdnRoot/main/${GameAssetManifest.FILE_NAME}"))
        try {
            val json = connection.inputStream.use { input ->
                val output = ByteArrayOutputStream()
                val buffer = ByteArray(BUFFER_SIZE)
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (output.size() + count > GameAssetManifest.MAX_BYTES) {
                        throw SyncException(SyncFailure.INVALID_MANIFEST, "Game asset manifest is too large.")
                    }
                    output.write(buffer, 0, count)
                }
                output.toString(Charsets.UTF_8.name())
            }
            return try {
                GameAssetManifest.parseRemote(connection.getHeaderField("X-Repo-Commit"), json)
            } catch (error: IOException) {
                throw SyncException(SyncFailure.INVALID_MANIFEST, "Invalid game asset manifest.", cause = error)
            }
        } finally {
            connection.disconnect()
        }
    }

    private fun installedEntries(directory: File): List<Entry>? {
        return try {
            val file = assetFile(directory, GameAssetManifest.FILE_NAME)
            if (file.length() !in 1..GameAssetManifest.MAX_BYTES.toLong()) return null
            GameAssetManifest.parse(file.readText(), local = directory.name.startsWith("local-"))
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
        return directory.takeIf { it.isDirectory && File(it, GameAssetManifest.FILE_NAME).isFile }
    }

    private fun assetFile(root: File, path: String): File {
        val file = File(root.canonicalFile, path)
        if (file.canonicalFile != file) throw IOException("Unsafe game asset path.")
        return file
    }

    private fun ensureDirectory(directory: File) {
        if (!directory.isDirectory && !directory.mkdirs() && !directory.isDirectory) {
            throw SyncException(SyncFailure.STORAGE_UNAVAILABLE, "Cannot create game asset storage.")
        }
    }

    private fun writeSynced(file: File, contents: String) {
        FileOutputStream(file).use { output ->
            output.write(contents.toByteArray(Charsets.UTF_8))
            output.fd.sync()
        }
    }

    private fun activateGeneration(directory: File) {
        val partial = assetFile(storage, "asset-activation.part")
        try {
            writeSynced(partial, directory.name)
            atomicMove(partial, active)
        } finally {
            partial.delete()
        }
    }

    private fun atomicMove(from: File, to: File) {
        Files.move(from.toPath(), to.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
    }

    private suspend fun matches(file: File, entry: Entry): Boolean {
        if (!file.isFile || file.length() != entry.size) return false
        if (entry.sha256.isEmpty()) return true
        return file.inputStream().use { sha256(it) } == entry.sha256
    }

    private suspend fun sha256(input: InputStream): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER_SIZE)
        while (true) {
            currentCoroutineContext().ensureActive()
            val count = input.read(buffer)
            if (count < 0) break
            digest.update(buffer, 0, count)
        }
        return digest.hexDigest()
    }

    private class SyncException(
        val reason: SyncFailure,
        message: String,
        val detail: String? = null,
        cause: Throwable? = null
    ) : IOException(message, cause)

    private class AssetContentsException(val path: String, message: String) : IOException(message)

    private suspend fun copyVerified(
        input: InputStream,
        partial: File,
        entry: Entry,
        progress: (suspend (Long) -> Unit)? = null
    ): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER_SIZE)
        var total = 0L
        var reported = 0L
        val reportEvery = maxOf(1L, entry.size / 100)
        return FileOutputStream(partial).use { output ->
            while (true) {
                currentCoroutineContext().ensureActive()
                val count = input.read(buffer)
                if (count < 0) break
                total += count
                if (total > entry.size) throw AssetContentsException(entry.path, "Unexpected size for ${entry.path}.")
                digest.update(buffer, 0, count)
                output.write(buffer, 0, count)
                if (progress != null && total - reported >= reportEvery) {
                    progress(total)
                    reported = total
                }
            }
            val sha = digest.hexDigest()
            if (total != entry.size || (entry.sha256.isNotEmpty() && sha != entry.sha256)) {
                throw AssetContentsException(entry.path, "Integrity check failed for ${entry.path}.")
            }
            output.fd.sync()
            sha
        }
    }

    private suspend fun parallelFiles(
        count: Int,
        workers: Int,
        process: suspend (worker: Int, index: Int) -> Unit
    ) = coroutineScope {
        val next = AtomicInteger()
        repeat(minOf(count, workers)) { worker ->
            launch {
                while (true) {
                    ensureActive()
                    val index = next.getAndIncrement()
                    if (index >= count) break
                    process(worker, index)
                }
            }
        }
    }

    private class SyncProgress(entries: List<Entry>, private val emitState: suspend (DownloadState) -> Unit) {
        private val totalBytes = entries.sumOf(Entry::size)
        private val totalFiles = entries.size
        private val mutex = Mutex()
        private var bytes = 0L
        private var files = 0
        private var percent = 0

        suspend fun start() = emitState(DownloadState.Syncing(0, totalFiles, 0))

        suspend fun advance(delta: Long, finished: Boolean = false) = mutex.withLock {
            bytes += delta
            if (finished) ++files
            val nextPercent = if (files == totalFiles) 100 else (100L * bytes / totalBytes).toInt().coerceAtMost(99)
            if (nextPercent != percent || finished) {
                percent = nextPercent
                emitState(DownloadState.Syncing(files, totalFiles, percent))
            }
        }
    }

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
                if (status !in REDIRECTS) {
                    throw SyncException(SyncFailure.HTTP_ERROR, "Game assets CDN returned HTTP $status.", status.toString())
                }
                val location = connection.getHeaderField("Location")
                    ?: throw IOException("Missing game asset redirect.")
                val next = URL(endpoint, location)
                if (endpoint.protocol == "https" && next.protocol != "https") {
                    throw IOException("Game assets CDN redirected HTTPS to an insecure URL.")
                }
                endpoint = next
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
        const val BUFFER_SIZE = 64 * 1024
        const val DOWNLOAD_WORKERS = 4
        const val DISK_WORKERS = 2
        val GENERATION = Regex("([0-9a-f]{40}|local)-[0-9a-f-]{36}")
        val REDIRECTS = setOf(301, 302, 303, 307, 308)
        val installationLock = Mutex()
    }
}
