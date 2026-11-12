package com.worldrpg.neonlauncher.ui

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.recyclerview.widget.LinearLayoutManager
import com.worldrpg.neonlauncher.databinding.FragmentFavoritesBinding
import com.worldrpg.neonlauncher.network.ServerInfo
import com.worldrpg.neonlauncher.utils.PreferencesManager

class FavoritesFragment : Fragment() {

    private var _binding: FragmentFavoritesBinding? = null
    private val binding get() = _binding!!

    private lateinit var serverAdapter: ServerListAdapter
    private lateinit var prefsManager: PreferencesManager

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentFavoritesBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        prefsManager = PreferencesManager(requireContext())

        setupRecyclerView()
        loadFavorites()
    }

    private fun setupRecyclerView() {
        serverAdapter = ServerListAdapter(
            onConnectClick = { server ->
                (activity as? MainActivity)?.launchGame(server)
            },
            onFavoriteClick = { server ->
                removeFavorite(server)
            }
        )

        binding.favoritesList.apply {
            layoutManager = LinearLayoutManager(context)
            adapter = serverAdapter
            setHasFixedSize(true)
        }
    }

    private fun loadFavorites() {
        val favoriteKeys = prefsManager.getFavorites()

        if (favoriteKeys.isEmpty()) {
            binding.emptyState.visibility = View.VISIBLE
            binding.favoritesList.visibility = View.GONE
        } else {
            binding.emptyState.visibility = View.GONE
            binding.favoritesList.visibility = View.VISIBLE

            val servers = favoriteKeys.map { key ->
                ServerInfo.fromFavoriteKey(key).copy(isFavorite = true)
            }
            serverAdapter.submitList(servers)
        }
    }

    private fun removeFavorite(server: ServerInfo) {
        prefsManager.removeFavorite(server.getKey())
        loadFavorites()
    }

    override fun onResume() {
        super.onResume()
        // Refresh favorites when returning to this tab
        loadFavorites()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}