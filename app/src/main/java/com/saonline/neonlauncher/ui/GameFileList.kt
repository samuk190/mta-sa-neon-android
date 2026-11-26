package com.saonline.neonlauncher.ui

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.databinding.ItemGameFileBinding
import com.saonline.neonlauncher.utils.GameFileStatus

internal data class GameFileRow(val path: String, val folder: Boolean, val pending: Int, val label: String = path)

internal fun gameFileRows(files: List<GameFileStatus>): List<GameFileRow> {
    val folders = mutableMapOf<String, Int>()
    val rows = files.map { file ->
        val pending = if (file.verified) 0 else 1
        var parent = file.path.substringBeforeLast('/', "")
        while (parent.isNotEmpty()) {
            folders[parent] = (folders[parent] ?: 0) + pending
            parent = parent.substringBeforeLast('/', "")
        }
        GameFileRow(file.path, false, pending)
    }
    return (folders.map { (path, count) -> GameFileRow(path, true, count) } + rows)
        .sortedWith(compareByDescending<GameFileRow> { it.folder }.thenBy(String.CASE_INSENSITIVE_ORDER) { it.path })
}

internal class GameFileList(private val onFolder: (String) -> Unit) :
    ListAdapter<GameFileRow, GameFileList.Holder>(object : DiffUtil.ItemCallback<GameFileRow>() {
        override fun areItemsTheSame(a: GameFileRow, b: GameFileRow) = a.path == b.path
        override fun areContentsTheSame(a: GameFileRow, b: GameFileRow) = a == b
    }) {
    class Holder(val binding: ItemGameFileBinding) : RecyclerView.ViewHolder(binding.root)

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) =
        Holder(ItemGameFileBinding.inflate(LayoutInflater.from(parent.context), parent, false))

    override fun onBindViewHolder(holder: Holder, position: Int) = with(holder.binding) {
        val item = getItem(position)
        val ctx = root.context
        val tint = ctx.getColor(if (item.pending == 0) R.color.browser_online_dot else R.color.game_file_missing)
        icon.setImageResource(if (item.folder) R.drawable.ic_game_folder else R.drawable.ic_game_file)
        icon.setColorFilter(tint)
        name.text = item.label
        status.setTextColor(tint)
        status.text = when {
            item.pending == 0 -> ctx.getString(R.string.files_verified)
            item.folder -> ctx.resources.getQuantityString(R.plurals.files_missing, item.pending, item.pending)
            else -> ctx.getString(R.string.files_pending)
        }
        root.setOnClickListener(if (item.folder) android.view.View.OnClickListener { onFolder(item.path) } else null)
        root.isClickable = item.folder
        root.isFocusable = item.folder
    }
}
