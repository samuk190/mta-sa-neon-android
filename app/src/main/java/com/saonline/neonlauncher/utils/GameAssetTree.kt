package com.saonline.neonlauncher.utils

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import java.io.IOException

/** Reads the selected document tree through its provider, including folders with no filesystem path. */
internal suspend fun readGameAssetTree(resolver: ContentResolver, tree: Uri): List<LocalGameFile> {
    val files = mutableListOf<LocalGameFile>()
    val directories = ArrayDeque<Pair<String, String>>()
    val seen = mutableSetOf<String>()
    directories.add(DocumentsContract.getTreeDocumentId(tree) to "")
    val columns = arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE)
    while (directories.isNotEmpty()) {
        currentCoroutineContext().ensureActive()
        val (parent, prefix) = directories.removeFirst()
        if (!seen.add(parent)) throw InvalidGameFolder("Repeated directory in game folder.")
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent)
        val cursor = resolver.query(children, columns, null, null, null) ?: throw IOException("Cannot list game folder.")
        cursor.use {
            while (it.moveToNext()) {
                currentCoroutineContext().ensureActive()
                val id = it.getString(0) ?: throw InvalidGameFolder("Missing document ID.")
                val name = it.getString(1) ?: throw InvalidGameFolder("Missing document name.")
                if (name.isBlank() || name == "." || name == ".." || '/' in name || '\\' in name) {
                    throw InvalidGameFolder("Invalid document name.")
                }
                val path = prefix + name
                if (path.length > 1024) throw InvalidGameFolder("Game asset path is too long.")
                if (it.getString(2) == Document.MIME_TYPE_DIR) {
                    if (seen.size + directories.size >= GameAssetManifest.MAX_FILES) throw InvalidGameFolder("Too many directories.")
                    directories.add(id to "$path/")
                } else if (path != GameAssetManifest.FILE_NAME) {
                    if (files.size >= GameAssetManifest.MAX_FILES) throw InvalidGameFolder("Too many game files.")
                    val uri = DocumentsContract.buildDocumentUriUsingTree(tree, id)
                    val open = { resolver.openInputStream(uri) ?: throw IOException("Cannot read $path.") }
                    val size = if (it.isNull(3) || it.getLong(3) < 0) open().use { input ->
                        val buffer = ByteArray(64 * 1024)
                        var total = 0L
                        while (true) {
                            currentCoroutineContext().ensureActive()
                            val count = input.read(buffer)
                            if (count < 0) break
                            total += count
                            if (total > GameAssetManifest.MAX_FILE_BYTES) throw InvalidGameFolder("Game file is too large.")
                        }
                        total
                    } else it.getLong(3)
                    files += LocalGameFile(GameAssetManifest.Entry(path, size, ""), open)
                }
            }
        }
    }
    return files
}
