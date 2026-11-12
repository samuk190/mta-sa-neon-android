package com.worldrpg.neonlauncher.ui

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import com.worldrpg.neonlauncher.R
import com.worldrpg.neonlauncher.databinding.FragmentServerBrowserBinding
import com.worldrpg.neonlauncher.network.ServerBrowserService
import com.worldrpg.neonlauncher.network.ServerInfo
import com.worldrpg.neonlauncher.utils.PreferencesManager
import kotlinx.coroutines.launch

class ServerBrowserFragment : Fragment() {

    private var _binding: FragmentServerBrowserBinding? = null
    private val binding get() = _binding!!

    private lateinit var serverAdapter: ServerListAdapter
    private lateinit var browserService: ServerBrowserService
    private lateinit var prefsManager: PreferencesManager

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentServerBrowserBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        prefsManager = PreferencesManager(requireContext())
        browserService = ServerBrowserService()

        setupRecyclerView()
        setupSwipeRefresh()

        loadServers()
    }

    private fun setupRecyclerView() {
        serverAdapter = ServerListAdapter(
            onConnectClick = { server ->
                (activity as? MainActivity)?.launchGame(server)
            },
            onFavoriteClick = { server ->
                toggleFavorite(server)
            }
        )

        binding.serverList.apply {
            layoutManager = LinearLayoutManager(context)
            adapter = serverAdapter
            setHasFixedSize(true)
        }
    }

    private fun setupSwipeRefresh() {
        binding.swipeRefresh.setColorSchemeResources(
            R.color.colorPrimaryVariant
        )
        binding.swipeRefresh.setOnRefreshListener {
            loadServers()
        }
        binding.btnRetry.setOnClickListener {
            loadServers()
        }
    }

    private fun loadServers() {
        binding.loadingIndicator.visibility = View.VISIBLE
        binding.emptyState.visibility = View.GONE
        binding.serverList.visibility = View.GONE

        viewLifecycleOwner.lifecycleScope.launch {
            browserService.fetchServerList().collect { state ->
                when (state) {
                    is ServerBrowserService.BrowserState.Loading -> {
                        binding.loadingIndicator.visibility = View.VISIBLE
                    }
                    is ServerBrowserService.BrowserState.Success -> {
                        binding.loadingIndicator.visibility = View.GONE
                        binding.swipeRefresh.isRefreshing = false

                        val favorites = prefsManager.getFavorites()
                        val serversWithFav = state.servers.map { server ->
                            server.copy(isFavorite = favorites.contains(server.getKey()))
                        }

                        if (serversWithFav.isEmpty()) {
                            binding.emptyState.visibility = View.VISIBLE
                            binding.serverList.visibility = View.GONE
                        } else {
                            binding.emptyState.visibility = View.GONE
                            binding.serverList.visibility = View.VISIBLE
                            serverAdapter.submitList(serversWithFav)
                        }
                    }
                    is ServerBrowserService.BrowserState.Error -> {
                        binding.loadingIndicator.visibility = View.GONE
                        binding.swipeRefresh.isRefreshing = false
                        binding.emptyState.visibility = View.VISIBLE
                        binding.serverList.visibility = View.GONE
                    }
                }
            }
        }
    }

    private fun toggleFavorite(server: ServerInfo) {
        val key = server.getKey()
        if (prefsManager.isFavorite(key)) {
            prefsManager.removeFavorite(key)
        } else {
            prefsManager.addFavorite(key)
        }
        // Refresh list to update star state
        loadServers()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}