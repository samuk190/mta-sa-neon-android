package com.worldrpg.neonlauncher.ui

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.GridLayoutManager
import com.google.android.material.color.MaterialColors
import com.worldrpg.neonlauncher.R
import com.worldrpg.neonlauncher.databinding.FragmentServerBrowserBinding
import com.worldrpg.neonlauncher.network.ServerBrowserService
import com.worldrpg.neonlauncher.network.ServerInfo
import com.worldrpg.neonlauncher.utils.PreferencesManager
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

class ServerBrowserFragment : Fragment() {

    private var _binding: FragmentServerBrowserBinding? = null
    private val binding get() = _binding!!

    private lateinit var serverAdapter: ServerListAdapter
    private val browserService = ServerBrowserService()
    private lateinit var prefsManager: PreferencesManager

    private var servers: List<ServerInfo> = emptyList()
    private var loadJob: Job? = null

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentServerBrowserBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        prefsManager = PreferencesManager(requireContext())

        serverAdapter = ServerListAdapter(
            onJoinClick = { server -> JoinDialog.show(this, server) },
            onFavoriteClick = { server -> toggleFavorite(server) }
        )
        binding.serverList.layoutManager = GridLayoutManager(context, columnCount(resources.configuration.screenWidthDp))
        binding.serverList.adapter = serverAdapter

        binding.swipeRefresh.setColorSchemeColors(
            MaterialColors.getColor(binding.swipeRefresh, com.google.android.material.R.attr.colorPrimary)
        )
        binding.swipeRefresh.setProgressBackgroundColorSchemeColor(
            MaterialColors.getColor(binding.swipeRefresh, com.google.android.material.R.attr.colorSurfaceContainerHigh)
        )
        binding.swipeRefresh.setOnRefreshListener { loadServers() }
        binding.btnRetry.setOnClickListener { loadServers() }
        binding.btnRefresh.setOnClickListener { loadServers() }

        loadServers()
    }

    private fun loadServers() {
        loadJob?.cancel()
        binding.emptyState.visibility = View.GONE
        binding.browserStatus.visibility = View.GONE
        binding.swipeRefresh.isRefreshing = true
        loadJob = viewLifecycleOwner.lifecycleScope.launch {
            browserService.fetchServerList(prefsManager.getFavorites()).collect { state ->
                when (state) {
                    is ServerBrowserService.BrowserState.Loading -> Unit
                    is ServerBrowserService.BrowserState.Success -> showServers(state.servers)
                    is ServerBrowserService.BrowserState.Error -> _binding?.let {
                        it.browserStatus.setText(R.string.neon_registry_unavailable)
                        it.browserStatus.visibility = View.VISIBLE
                    }
                }
            }
            _binding?.swipeRefresh?.isRefreshing = false
        }
    }

    private fun showServers(list: List<ServerInfo>) {
        val b = _binding ?: return
        val favoriteKeys = prefsManager.getFavorites()
        servers = list.map { it.copy(isFavorite = it.getKey() in favoriteKeys) }
        serverAdapter.submitList(servers)
        b.emptyState.visibility = if (list.isEmpty()) View.VISIBLE else View.GONE
        val online = list.filter { it.status == ServerInfo.Status.ONLINE }
        val players = online.sumOf { it.players }
        b.serverCount.text = resources.getQuantityString(R.plurals.servers_count, list.size, list.size) + ", " +
            resources.getQuantityString(R.plurals.players_online, players, players)
        if (list.none { it.status == ServerInfo.Status.QUERYING }) b.swipeRefresh.isRefreshing = false
    }

    private fun toggleFavorite(server: ServerInfo) {
        val key = server.getKey()
        if (prefsManager.isFavorite(key)) prefsManager.removeFavorite(key) else prefsManager.addFavorite(key)
        applyFavorites()
    }

    private fun applyFavorites() {
        val favorites = prefsManager.getFavorites()
        servers = servers.map { it.copy(isFavorite = favorites.contains(it.getKey())) }
        serverAdapter.submitList(servers)
    }

    override fun onResume() {
        super.onResume()
        // Favorites may have changed on the other tab.
        if (servers.isNotEmpty()) applyFavorites()
    }

    override fun onDestroyView() {
        loadJob?.cancel()
        super.onDestroyView()
        _binding = null
    }

    companion object {
        /** One column per 560 dp, so a card has room for name, stats and Join. */
        fun columnCount(widthDp: Int): Int = (widthDp / 560).coerceIn(1, 3)
    }
}
