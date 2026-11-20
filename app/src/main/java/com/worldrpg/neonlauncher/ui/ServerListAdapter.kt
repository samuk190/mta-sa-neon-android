package com.worldrpg.neonlauncher.ui

import android.content.res.ColorStateList
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.color.MaterialColors
import com.worldrpg.neonlauncher.R
import com.worldrpg.neonlauncher.databinding.ItemServerBinding
import com.worldrpg.neonlauncher.network.ServerInfo

/** Server cards. Join (or a tap on the card) opens the join dialog. */
class ServerListAdapter(
    private val onJoinClick: (ServerInfo) -> Unit,
    private val onFavoriteClick: (ServerInfo) -> Unit
) : ListAdapter<ServerInfo, ServerListAdapter.ServerViewHolder>(ServerDiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ServerViewHolder {
        val binding = ItemServerBinding.inflate(LayoutInflater.from(parent.context), parent, false)
        return ServerViewHolder(binding)
    }

    override fun onBindViewHolder(holder: ServerViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    inner class ServerViewHolder(
        private val binding: ItemServerBinding
    ) : RecyclerView.ViewHolder(binding.root) {

        fun bind(server: ServerInfo) {
            val ctx = binding.root.context
            binding.tvServerName.text = server.name
            binding.tvServerAddress.text = server.getKey()
            binding.tvGameMode.text = modeLine(server)
            binding.tvGameMode.visibility = if (binding.tvGameMode.text.isEmpty()) View.GONE else View.VISIBLE
            binding.ivLock.visibility = if (server.passworded) View.VISIBLE else View.GONE
            ServerLogo.bind(binding.ivLogo, server.name, server.logoUrl)

            binding.queryProgress.visibility = if (server.status == ServerInfo.Status.QUERYING) View.VISIBLE else View.GONE
            binding.tvOffline.visibility = if (server.status == ServerInfo.Status.OFFLINE) View.VISIBLE else View.GONE
            binding.statsOnline.visibility = if (server.status == ServerInfo.Status.ONLINE) View.VISIBLE else View.INVISIBLE
            if (server.status == ServerInfo.Status.ONLINE) {
                binding.tvPlayerCount.text = ctx.getString(R.string.players_count, server.players, server.maxPlayers)
                binding.tvPing.text = ctx.getString(R.string.ping_ms, server.ping)
                binding.ivPingDot.imageTintList = ColorStateList.valueOf(ContextCompat.getColor(ctx, pingColor(server.ping)))
            }

            binding.btnFavorite.setIconResource(if (server.isFavorite) R.drawable.ic_star else R.drawable.ic_star_outline)
            binding.btnFavorite.iconTint = ColorStateList.valueOf(
                MaterialColors.getColor(
                    binding.btnFavorite,
                    if (server.isFavorite) com.google.android.material.R.attr.colorSecondary
                    else com.google.android.material.R.attr.colorOnSurfaceVariant
                )
            )
            binding.btnFavorite.setOnClickListener { onFavoriteClick(server) }
            binding.btnConnect.setOnClickListener { onJoinClick(server) }
            binding.cardServer.setOnClickListener { onJoinClick(server) }
        }
    }

    class ServerDiffCallback : DiffUtil.ItemCallback<ServerInfo>() {
        override fun areItemsTheSame(oldItem: ServerInfo, newItem: ServerInfo): Boolean =
            oldItem.getKey() == newItem.getKey()

        override fun areContentsTheSame(oldItem: ServerInfo, newItem: ServerInfo): Boolean =
            oldItem == newItem
    }

    companion object {
        fun pingColor(ping: Int): Int = when {
            ping <= 0 -> R.color.md_outline
            ping < 100 -> R.color.ping_good
            ping < 200 -> R.color.ping_fair
            else -> R.color.ping_bad
        }

        /** "Game mode - map", whichever the server reported. */
        fun modeLine(server: ServerInfo): String =
            listOf(server.gameMode, server.mapName).filter { it.isNotBlank() && it != "None" }.joinToString("  -  ")
    }
}
