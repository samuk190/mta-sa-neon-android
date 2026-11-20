package com.worldrpg.neonlauncher.ui

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.ColorFilter
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.Typeface
import android.graphics.drawable.Drawable
import android.util.LruCache
import android.widget.ImageView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL

/**
 * Shows a server logo in an ImageView.
 *
 * The initials of the server name are drawn first, so a server without a
 * logo URL, or one whose download fails, still gets a distinct tile. A logo
 * URL (http or https) is downloaded once, decoded at the view's pixel size
 * (so a 512 px image is not kept in memory for a 44 dp row) and cached.
 */
object ServerLogo {

    private const val MAX_BYTES = 2 * 1024 * 1024
    private const val TIMEOUT_MS = 6000

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // Cache size in KB: an eighth of the app heap, as the Android docs suggest.
    private val cache = object : LruCache<String, Bitmap>(
        (Runtime.getRuntime().maxMemory() / 1024 / 8).toInt()
    ) {
        override fun sizeOf(key: String, value: Bitmap): Int = value.byteCount / 1024
    }

    private val failed = mutableSetOf<String>()

    fun bind(view: ImageView, name: String, logoUrl: String) {
        val url = logoUrl.trim()
        view.tag = url
        val sizePx = maxOf(view.layoutParams?.width ?: 0, view.width, 1)
        val key = "$url@$sizePx"

        cache.get(key)?.let {
            view.setImageBitmap(it)
            return
        }
        view.setImageDrawable(MonogramDrawable(name))

        if (!(url.startsWith("http://") || url.startsWith("https://"))) return
        synchronized(failed) { if (url in failed) return }

        scope.launch {
            val bitmap = runCatching { download(url, sizePx) }.getOrNull()
            if (bitmap == null) {
                synchronized(failed) { failed += url }
                return@launch
            }
            cache.put(key, bitmap)
            withContext(Dispatchers.Main) {
                // The view may have been recycled for another server meanwhile.
                if (view.tag == url) view.setImageBitmap(bitmap)
            }
        }
    }

    private fun download(url: String, targetPx: Int): Bitmap? {
        val conn = URL(url).openConnection() as HttpURLConnection
        try {
            conn.connectTimeout = TIMEOUT_MS
            conn.readTimeout = TIMEOUT_MS
            conn.instanceFollowRedirects = true
            if (conn.responseCode !in 200..299) return null
            if (conn.contentLengthLong > MAX_BYTES) return null

            val out = ByteArrayOutputStream()
            conn.inputStream.use { input ->
                val buf = ByteArray(16 * 1024)
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    out.write(buf, 0, n)
                    if (out.size() > MAX_BYTES) return null
                }
            }
            return decodeScaled(out.toByteArray(), targetPx)
        } finally {
            conn.disconnect()
        }
    }

    private fun decodeScaled(bytes: ByteArray, targetPx: Int): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null

        var sample = 1
        val smallest = minOf(bounds.outWidth, bounds.outHeight)
        while (smallest / (sample * 2) >= targetPx) sample *= 2

        val opts = BitmapFactory.Options().apply { inSampleSize = sample }
        return BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts)
    }
}

/**
 * Initials on a tonal tile. The color comes from the name, so each server
 * keeps the same color. Everything is sized from the bounds, so it is sharp
 * at any density.
 */
class MonogramDrawable(name: String) : Drawable() {

    private val initials: String = name.split(' ', '-', '_', '.', ':')
        .filter { it.isNotBlank() && it[0].isLetterOrDigit() }
        .take(2)
        .joinToString("") { it.substring(0, 1).uppercase() }
        .ifEmpty { "?" }

    private val palette = arrayOf(
        intArrayOf(0xFF005313.toInt(), 0xFF98F994.toInt()),
        intArrayOf(0xFF6A3C00.toInt(), 0xFFFFDCBE.toInt()),
        intArrayOf(0xFF1F4D52.toInt(), 0xFFBCEBF0.toInt()),
        intArrayOf(0xFF3B4B00.toInt(), 0xFFD5F08A.toInt()),
        intArrayOf(0xFF4A2B5C.toInt(), 0xFFF2DAFF.toInt()),
    )
    private val colors = palette[Math.floorMod(name.hashCode(), palette.size)]

    private val bgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = colors[0] }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = colors[1]
        textAlign = Paint.Align.CENTER
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
    }

    override fun draw(canvas: Canvas) {
        val b = bounds
        canvas.drawRect(b, bgPaint) // the ShapeableImageView clips the corners
        textPaint.textSize = b.height() * if (initials.length > 1) 0.38f else 0.46f
        val y = b.exactCenterY() - (textPaint.descent() + textPaint.ascent()) / 2f
        canvas.drawText(initials, b.exactCenterX(), y, textPaint)
    }

    override fun setAlpha(alpha: Int) {
        bgPaint.alpha = alpha
        textPaint.alpha = alpha
    }

    override fun setColorFilter(colorFilter: ColorFilter?) {
        bgPaint.colorFilter = colorFilter
        textPaint.colorFilter = colorFilter
    }

    @Deprecated("Deprecated in Java")
    override fun getOpacity(): Int = PixelFormat.OPAQUE
}
