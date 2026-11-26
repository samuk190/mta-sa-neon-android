package com.saonline.neonlauncher.ui

import android.content.res.ColorStateList
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.content.ContextCompat
import androidx.core.view.isVisible
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.databinding.ItemServerBinding
import com.saonline.neonlauncher.network.ServerInfo

class ServerListAdapter(
    private val onSelect: (ServerInfo) -> Unit,
    private val onJoin: (ServerInfo) -> Unit,
    private val onFavorite: (ServerInfo) -> Unit
) : ListAdapter<ServerListAdapter.Row, ServerListAdapter.Holder>(Diff) {
    data class Row(val server: ServerInfo, val selected: Boolean)

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) =
        Holder(ItemServerBinding.inflate(LayoutInflater.from(parent.context), parent, false))

    override fun onBindViewHolder(holder: Holder, position: Int) = holder.bind(getItem(position))

    inner class Holder(private val b: ItemServerBinding) : RecyclerView.ViewHolder(b.root) {
        fun bind(row: Row) {
            val server = row.server
            val ctx = b.root.context
            fun color(id: Int) = ContextCompat.getColor(ctx, id)
            b.root.isSelected = row.selected
            b.root.strokeColor = color(if (row.selected) R.color.browser_amber else R.color.browser_line)
            b.root.setCardBackgroundColor(color(if (row.selected) R.color.browser_selected else R.color.browser_card))
            b.tvServerName.text = server.name
            b.tvGameMode.text = modeLine(server).ifEmpty { server.getKey() }
            b.ivLock.isVisible = server.passworded
            b.androidBadge.isVisible = server.supportsAndroid
            ServerLogo.bind(b.ivLogo, server.name, server.logoUrl)
            val wide = ctx.resources.configuration.screenWidthDp >= 900
            b.countries.isVisible = wide && server.countries.isNotEmpty()
            if (b.countries.isVisible) {
                b.countries.text = server.countries.take(5).joinToString(" ") { country ->
                    country.uppercase(java.util.Locale.ROOT).map { Character.toChars(0x1F1E6 + (it - 'A')).concatToString() }.joinToString("")
                }
                b.countries.contentDescription = server.countries.joinToString(", ")
            }
            val online = server.status == ServerInfo.Status.ONLINE
            b.tvPlayerCount.text = when (server.status) {
                ServerInfo.Status.ONLINE -> ctx.getString(R.string.players_count, server.players, server.maxPlayers)
                ServerInfo.Status.OFFLINE -> ctx.getString(R.string.offline)
                ServerInfo.Status.QUERYING -> ctx.getString(R.string.querying)
                ServerInfo.Status.DIRECT -> ""
            }
            b.tvPing.isVisible = online
            if (online) {
                b.tvPing.text = ctx.getString(R.string.ping_ms, server.ping)
                b.tvPing.setTextColor(color(pingColor(server.ping)))
            }
            b.btnFavorite.setIconResource(if (server.isFavorite) R.drawable.ic_star else R.drawable.ic_star_outline)
            b.btnFavorite.iconTint = ColorStateList.valueOf(color(if (server.isFavorite) R.color.browser_amber else R.color.browser_muted))
            b.btnFavorite.isSelected = server.isFavorite
            b.btnFavorite.setOnClickListener { onFavorite(server) }
            b.btnConnect.setOnClickListener { onJoin(server) }
            b.root.setOnClickListener { onSelect(server) }
        }
    }

    private object Diff : DiffUtil.ItemCallback<Row>() {
        override fun areItemsTheSame(oldItem: Row, newItem: Row) = oldItem.server.getKey() == newItem.server.getKey()
        override fun areContentsTheSame(oldItem: Row, newItem: Row) = oldItem == newItem
    }

    companion object {
        fun pingColor(ping: Int): Int = when {
            ping <= 0 -> R.color.browser_muted
            ping < 100 -> R.color.ping_good
            ping < 200 -> R.color.browser_amber
            else -> R.color.ping_bad
        }

        fun modeLine(server: ServerInfo): String =
            listOf(server.gameMode, server.mapName).filter { it.isNotBlank() && it != "None" }.joinToString("  ·  ")
    }
}
