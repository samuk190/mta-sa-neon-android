package com.worldrpg.neonlauncher.ui

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.worldrpg.neonlauncher.R
import com.worldrpg.neonlauncher.databinding.ItemServerBinding
import com.worldrpg.neonlauncher.network.ServerInfo

class ServerListAdapter(
    private val onConnectClick: (ServerInfo) -> Unit,
    private val onFavoriteClick: (ServerInfo) -> Unit
) : ListAdapter<ServerInfo, ServerListAdapter.ServerViewHolder>(ServerDiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ServerViewHolder {
        val binding = ItemServerBinding.inflate(
            LayoutInflater.from(parent.context),
            parent,
            false
        )
        return ServerViewHolder(binding)
    }

    override fun onBindViewHolder(holder: ServerViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    inner class ServerViewHolder(
        private val binding: ItemServerBinding
    ) : RecyclerView.ViewHolder(binding.root) {

        fun bind(server: ServerInfo) {
            binding.tvServerName.text = server.name
            binding.tvServerAddress.text = "${server.ip}:${server.port}"
            binding.tvPlayerCount.text = server.getPlayerCountText()
            binding.tvPing.text = server.getPingText()

            // Update favorite star icon
            binding.btnFavorite.setImageResource(
                if (server.isFavorite) android.R.drawable.star_on
                else android.R.drawable.star_off
            )

            binding.btnFavorite.setOnClickListener {
                onFavoriteClick(server)
            }

            binding.btnConnect.setOnClickListener {
                onConnectClick(server)
            }

            // Also allow clicking the whole card to connect
            binding.cardServer.setOnClickListener {
                onConnectClick(server)
            }
        }
    }

    class ServerDiffCallback : DiffUtil.ItemCallback<ServerInfo>() {
        override fun areItemsTheSame(oldItem: ServerInfo, newItem: ServerInfo): Boolean {
            return oldItem.getKey() == newItem.getKey()
        }

        override fun areContentsTheSame(oldItem: ServerInfo, newItem: ServerInfo): Boolean {
            return oldItem == newItem
        }
    }
}