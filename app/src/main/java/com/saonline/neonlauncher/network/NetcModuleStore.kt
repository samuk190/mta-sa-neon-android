package com.saonline.neonlauncher.network

import android.content.Context
import android.net.Uri
import com.saonline.neonlauncher.BuildConfig
import com.saonline.neonlauncher.network.PcNetworkSettings.NetcBuild
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL

/**
 * Where the PC netc.dll for each build lives on the phone:
 * files/netc/<build>/netc.dll. It arrives one of three ways: copied from the APK
 * assets when the build bundles it (NETC_BUNDLE_IN_ASSETS), imported from a file
 * the player picks, or downloaded from MTA. Every file is
 * checked to be a 32-bit x86 DLL exporting InitNetInterface before it is kept.
 */
object NetcModuleStore {
    private const val MAX_BYTES = 64L * 1024 * 1024

    /** Writable data folder for that build's emulated drive, registry and identity. */
    fun dataDir(context: Context, build: NetcBuild): File = dataDirFor(context, build.id)

    /** [dataDir] by build id, for Java callers (EngineHostActivity). */
    @JvmStatic fun dataDirFor(context: Context, buildId: String): File = File(context.filesDir, "netemu/$buildId")

    fun isBundled(build: NetcBuild): Boolean = when (build) {
        NetcBuild.OFFICIAL -> BuildConfig.NETC_BUNDLED_OFFICIAL
        NetcBuild.FORK -> BuildConfig.NETC_BUNDLED_FORK
    }

    /** Where MTA publishes each build (.env NETC_OFFICIAL/FORK_DOWNLOAD_URL). */
    private fun downloadUrl(build: NetcBuild): String = when (build) {
        NetcBuild.OFFICIAL -> BuildConfig.NETC_OFFICIAL_DOWNLOAD_URL
        NetcBuild.FORK -> BuildConfig.NETC_FORK_DOWNLOAD_URL
    }

    fun canDownload(build: NetcBuild): Boolean = downloadUrl(build).isNotEmpty()

    /** The installed module, without doing I/O beyond a stat. */
    fun installed(context: Context, build: NetcBuild): File? = target(context, build).takeIf { it.isFile && it.length() > 0 }

    /**
     * The module to run, installing the bundled copy on first use. Null when
     * the player still has to import or download it. Call off the main thread.
     */
    fun resolve(context: Context, build: NetcBuild): File? {
        installed(context, build)?.let { return it }
        if (!isBundled(build)) return null
        return try {
            // Another resolve may have installed it while this one waited.
            synchronized(installLocks.getValue(build)) { installed(context, build) }?.let { return it }
            context.assets.open("netc/${build.id}/netc.dll").use { install(context, build, it) }
        } catch (e: IOException) {
            null
        }
    }

    /** Imports a file the player picked. Throws IOException with a reason. */
    fun importFrom(context: Context, build: NetcBuild, uri: Uri): File {
        val input = context.contentResolver.openInputStream(uri) ?: throw IOException("cannot open the file")
        return input.use { install(context, build, it) }
    }

    /** Downloads the module from MTA. Throws IOException with a reason. */
    fun download(context: Context, build: NetcBuild): File {
        if (!canDownload(build)) throw IOException("no download for this build")
        val conn = URL(downloadUrl(build)).openConnection() as HttpURLConnection
        conn.connectTimeout = 15_000
        conn.readTimeout = 30_000
        try {
            if (conn.responseCode != HttpURLConnection.HTTP_OK) throw IOException("HTTP ${conn.responseCode}")
            return conn.inputStream.use { install(context, build, it) }
        } finally {
            conn.disconnect()
        }
    }

    private fun target(context: Context, build: NetcBuild) = File(context.filesDir, "netc/${build.id}/netc.dll")

    /** One install per build at a time (a bundled copy, a pick and a download can overlap). */
    private val installLocks = NetcBuild.values().associateWith { Any() }

    private fun install(context: Context, build: NetcBuild, input: InputStream): File = synchronized(installLocks.getValue(build)) {
        val dest = target(context, build)
        dest.parentFile?.mkdirs()
        // Unique per install, so a concurrent one never deletes this file.
        val tmp = File.createTempFile("netc", ".part", dest.parentFile)
        try {
            tmp.outputStream().use { out ->
                val buf = ByteArray(64 * 1024)
                var total = 0L
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    total += n
                    if (total > MAX_BYTES) throw IOException("file is too large to be netc.dll")
                    out.write(buf, 0, n)
                }
            }
            validate(tmp)
            if (!tmp.renameTo(dest)) throw IOException("cannot save netc.dll")
            dest
        } finally {
            tmp.delete()
        }
    }

    /** A PE32 i386 DLL whose export table names InitNetInterface. */
    private fun validate(file: File) {
        RandomAccessFile(file, "r").use { f ->
            fun u16(at: Long): Int { f.seek(at); return f.read() or (f.read() shl 8) }
            fun u32(at: Long): Long { f.seek(at); return (f.read().toLong() or (f.read().toLong() shl 8) or (f.read().toLong() shl 16) or (f.read().toLong() shl 24)) }
            if (f.length() < 0x400 || u16(0) != 0x5A4D) throw IOException("not a Windows DLL")
            val pe = u32(0x3C)
            if (pe <= 0 || pe + 248 > f.length() || u32(pe) != 0x4550L) throw IOException("not a Windows DLL")
            if (u16(pe + 4) != 0x14C) throw IOException("not the 32-bit PC netc.dll")
            if ((u16(pe + 22) and 0x2000) == 0) throw IOException("not a DLL")
            // Find "InitNetInterface" among the export names.
            val opt = pe + 24
            val exportRva = u32(opt + 96)
            val sections = u16(pe + 6)
            val table = opt + u16(pe + 20)
            fun fileOffset(rva: Long): Long? {
                for (i in 0 until sections) {
                    val s = table + i * 40L
                    val va = u32(s + 12)
                    val size = maxOf(u32(s + 8), u32(s + 16))
                    if (rva >= va && rva < va + size) return u32(s + 20) + (rva - va)
                }
                return null
            }
            val exports = fileOffset(exportRva) ?: throw IOException("netc.dll has no exports")
            val count = u32(exports + 24)
            val names = fileOffset(u32(exports + 32)) ?: throw IOException("netc.dll has no exports")
            val wanted = "InitNetInterface".toByteArray()
            for (i in 0 until minOf(count, 4096)) {
                val at = fileOffset(u32(names + i * 4)) ?: continue
                if (at + wanted.size + 1 > f.length()) continue
                f.seek(at)
                val name = ByteArray(wanted.size + 1)
                f.readFully(name)
                if (name.copyOf(wanted.size).contentEquals(wanted) && name[wanted.size] == 0.toByte()) return
            }
            throw IOException("this DLL is not MTA's netc.dll")
        }
    }
}
