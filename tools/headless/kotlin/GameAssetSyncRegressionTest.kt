package com.saonline.neonlauncher.utils

import android.content.Context
import com.saonline.neonlauncher.BuildConfig
import kotlinx.coroutines.flow.last
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLConnection
import java.net.URLStreamHandler
import java.nio.file.Files
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference

private fun manifest(files: Map<String, ByteArray>): String = GameAssetManifest.encode(files.map { (path, bytes) ->
    val sha = MessageDigest.getInstance("SHA-256").apply { update(bytes) }.hexDigest()
    GameAssetManifest.Entry(path, bytes.size.toLong(), sha)
})

private fun write(root: File, path: String, text: String) {
    File(root, path).apply { parentFile.mkdirs(); writeText(text) }
}

private fun installed(bridge: NativeAssetBridge): File = File(checkNotNull(bridge.getReadyStoragePath()))

private suspend fun verifyLocalImport(root: File) {
    val source = File(root, "source")
    BuildConfig.CDN_ROOT_URL = ""
    BuildConfig.DEVICE_ASSETS_PATH = source.path
    val context = Context(File(root, "local"))
    val bridge = NativeAssetBridge(context)
    val empty = bridge.gameFileInventory()
    check(empty.localReference && empty.files.isNotEmpty() && empty.files.none { it.verified })
    val name = "scache_small.txt"
    val original = File(System.getProperty("test.mainSource"), "assets/$name").readBytes()
    source.mkdirs()
    File(source, name).writeBytes(original)
    check(bridge.gameFileInventory().files.filter { it.verified }.single().path == name)
    original[0] = original[0].inc()
    File(source, name).writeBytes(original)
    for (path in GameAssetManifest.requiredFiles) write(source, path, "new")
    write(source, "data/tuning.dat", "")
    write(context.filesDir, "game_assets/staging-local/data/gta.dat", "old")
    write(context.filesDir, "game_assets/staging-local/data/removed.cfg", "stale")

    expectComplete(bridge)
    val generation = installed(bridge)
    expectComplete(bridge)
    check(installed(bridge) == generation) { "Repeating a local transfer created another copy." }
    check(File(generation, "data/gta.dat").readText() == "new")
    check(!File(generation, "data/removed.cfg").exists())
    check(File(generation, "data/tuning.dat").isFile)
    // The document picker may return an empty tree or a different GTA release.
    // Rejected imports must leave the active generation available.
    val emptyPick = bridge.syncAssets { emptyList() }.last()
    check(emptyPick == NativeAssetBridge.DownloadState.Error(NativeAssetBridge.SyncFailure.INVALID_LOCAL_FOLDER))
    val invalidPick = bridge.syncAssets {
        GameAssetManifest.requiredFiles.map { path ->
            LocalGameFile(GameAssetManifest.Entry(path, 3, "")) { "bad".byteInputStream() }
        }
    }.last()
    check(invalidPick is NativeAssetBridge.DownloadState.Error && invalidPick.reason == NativeAssetBridge.SyncFailure.FILE_INVALID)
    check(installed(bridge) == generation)
    val entries = GameAssetManifest.parse(File(generation, GameAssetManifest.FILE_NAME).readText(), local = true)
    check(entries.all { it.sha256.length == 64 })
    // The imported file has the expected length and matches the locally generated hash.
    // Its bytes do not match the trusted reference.
    check(!bridge.gameFileInventory().files.single { it.path == name }.verified)
    val legacy = GameAssetManifest.encode(entries.map { it.copy(sha256 = "") })
    write(generation, GameAssetManifest.FILE_NAME, legacy)
    check(installed(bridge) == generation)
    write(source, "data/gta.dat", "")
    check(bridge.syncAssets().last() is NativeAssetBridge.DownloadState.Error)
    check(installed(bridge) == generation)
}

private data class Update(
    val files: Map<String, ByteArray>,
    val json: String = manifest(files),
    val corrupt: String? = null
)

private suspend fun verifyCdn(root: File) {
    val first = Update((GameAssetManifest.requiredFiles + "data/addon.dat").associateWith { "old".toByteArray() })
    val second = Update(first.files + mapOf("data/default.dat" to "new".toByteArray(), "data/addon.dat" to "new".toByteArray()))
    val update = AtomicReference(first)
    val offline = AtomicBoolean(true)
    val requests = ConcurrentHashMap<String, Int>()
    val firstTransfers = CountDownLatch(4)
    val activeTransfers = AtomicInteger()
    val peakTransfers = AtomicInteger()
    val fileToKeepOnRetry = AtomicReference<File>()
    URL.setURLStreamHandlerFactory { scheme ->
        if (scheme != "http") null else object : URLStreamHandler() {
            override fun openConnection(url: URL): URLConnection {
                check(url.host == "assets.test")
                if (offline.get()) throw java.net.UnknownHostException(url.host)
                val current = update.get()
                val prefix = "/main/game_files_android_mta/"
                val body = when {
                    url.path == "/main/${GameAssetManifest.FILE_NAME}" -> current.json.toByteArray()
                    url.path.startsWith(prefix) -> {
                        val asset = url.path.removePrefix(prefix)
                        requests.merge(asset, 1, Int::plus)
                        if (asset == current.corrupt) "bad".toByteArray() else current.files[asset]
                    }
                    else -> null
                }
                return object : HttpURLConnection(url) {
                    override fun connect() {}
                    override fun disconnect() {}
                    override fun usingProxy() = false
                    override fun getResponseCode() = if (body == null) 404 else 200
                    override fun getInputStream(): InputStream {
                        val path = url.path
                        if (current === first && path.startsWith(prefix)) {
                            val active = activeTransfers.incrementAndGet()
                            peakTransfers.accumulateAndGet(active, ::maxOf)
                            try {
                                firstTransfers.countDown()
                                check(firstTransfers.await(5, TimeUnit.SECONDS)) { "CDN transfers did not overlap" }
                            } finally {
                                activeTransfers.decrementAndGet()
                            }
                        }
                        if (path == prefix + current.corrupt) {
                            val verified = checkNotNull(fileToKeepOnRetry.get())
                            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
                            while (!verified.isFile && System.nanoTime() < deadline) Thread.sleep(5)
                            check(verified.isFile) { "The other worker did not finish staging its file" }
                        }
                        return checkNotNull(body).inputStream()
                    }
                }
            }
        }
    }
    BuildConfig.DEVICE_ASSETS_PATH = ""
    BuildConfig.CDN_ROOT_URL = "http://assets.test"
    val context = Context(File(root, "cdn"))
    val bridge = NativeAssetBridge(context)
    val bundled = bridge.gameFileInventory()
    check(bundled.localReference && bundled.files.map { it.path } == bridge.referenceEntries().map { it.path })
    check(bundled.files.none { it.verified })
    offline.set(false)
    val online = bridge.gameFileInventory()
    check(!online.localReference && online.files.none { it.verified })
    val states = bridge.syncAssets().toList()
    check(states.last() is NativeAssetBridge.DownloadState.Complete)
    val downloaded = bridge.gameFileInventory()
    check(!downloaded.localReference && downloaded.files.all { it.verified })
    offline.set(true)
    val cached = bridge.gameFileInventory()
    check(cached.localReference && cached.files == downloaded.files)
    check(bridge.syncAssets().last() is NativeAssetBridge.DownloadState.Error)
    offline.set(false)
    check(peakTransfers.get() == 4)
    val progress = states.filterIsInstance<NativeAssetBridge.DownloadState.Syncing>()
    check(progress.zipWithNext().all { (a, b) -> a.current <= b.current && a.percent <= b.percent })
    check(progress.last().let { it.percent == 100 && it.current == it.total })
    val before = installed(bridge)
    check(File(before, "data/addon.dat").readText() == "old")
    val marker = File(context.filesDir, "game_assets/active")
    val previousMarker = marker.readText()

    val generationId = GameAssetManifest.parseRemote(null, second.json).generationId
    fileToKeepOnRetry.set(File(context.filesDir, "game_assets/staging-$generationId/data/default.dat"))
    update.set(second.copy(corrupt = "data/addon.dat"))
    check(bridge.syncAssets().last() is NativeAssetBridge.DownloadState.Error)
    check(installed(bridge) == before)
    check(File(context.filesDir, "game_assets").listFiles().orEmpty().none { it.name.endsWith(".part") })
    val pending = bridge.gameFileInventory().files.filterNot { it.verified }
    check(pending.single().path == "data/addon.dat") { "Only the corrupt file should still need transfer" }
    val copied = requests["data/default.dat"]

    update.set(second)
    expectComplete(bridge)
    val after = installed(bridge)
    check(after != before && File(after, "data/addon.dat").readText() == "new")
    check(requests["data/default.dat"] == copied) { "Retry redownloaded a verified file" }

    marker.writeText(previousMarker)
    val beforeRecovery = requests.toMap()
    expectComplete(bridge)
    check(installed(bridge) == after && requests == beforeRecovery)

    val missingHashes = second.json.replace(Regex("\"sha256\":\"[0-9a-f]{64}\""), "\"sha256\":\"\"")
    update.set(second.copy(json = missingHashes))
    check(runCatching { bridge.gameFileInventory() }.isFailure) { "An invalid CDN manifest was hidden by the offline fallback" }
    check(bridge.syncAssets().last() is NativeAssetBridge.DownloadState.Error)
    check(installed(bridge) == after)
    update.set(second)
    val beforeRepair = requests.toMap()
    write(after, "data/addon.dat", "bad")
    check(!bridge.gameFileInventory().files.single { it.path == "data/addon.dat" }.verified)
    expectComplete(bridge)
    val repaired = installed(bridge)
    check(repaired != after && File(repaired, "data/addon.dat").readText() == "new")
    check(requests["data/addon.dat"] == checkNotNull(beforeRepair["data/addon.dat"]) + 1)
    check(requests.filterKeys { it != "data/addon.dat" } == beforeRepair.filterKeys { it != "data/addon.dat" })

    val libraries = listOf("libGTASA.so", "libSCAnd.so", "libOpenAL64.so")
        .associate { name -> "lib/$name" to name.toByteArray() }
    val beforeEngineSync = requests.toMap()
    update.set(Update(second.files + libraries))
    expectComplete(bridge)
    val withEngine = installed(bridge)
    check(withEngine != repaired)
    for ((path, content) in libraries) {
        check(File(withEngine, path).readBytes().contentEquals(content))
        check(requests[path] == 1)
    }
    check(requests.filterKeys { it !in libraries } == beforeEngineSync)
    check(File(withEngine, "lib/libSCAnd.so").delete())
    check(bridge.getReadyStoragePath() == null)
}

private suspend fun expectComplete(bridge: NativeAssetBridge) {
    val result = bridge.syncAssets().last()
    check(result is NativeAssetBridge.DownloadState.Complete) { "Expected a completed import, got $result" }
}

fun main() = runBlocking {
    val root = Files.createTempDirectory("neon-assets-test").toFile()
    try {
        verifyLocalImport(root)
        verifyCdn(root)
        val valid = manifest(GameAssetManifest.requiredFiles.associateWith { byteArrayOf(1) })
        val pinned = GameAssetManifest.parseRemote("a".repeat(40), valid)
        check(pinned.sourceRevision == "a".repeat(40))
        val fallback = GameAssetManifest.parseRemote(null, valid)
        check(fallback.sourceRevision == "main" && fallback.generationId.matches(Regex("[a-f0-9]{40}")))
        check(runCatching { GameAssetManifest.parseRemote(null, valid + "{}") }.exceptionOrNull() is IOException)
    } finally {
        root.deleteRecursively()
    }
    println("PASS: parallel downloads, progress, SHA-256, retry, atomic activation, recovery and local asset import")
}
