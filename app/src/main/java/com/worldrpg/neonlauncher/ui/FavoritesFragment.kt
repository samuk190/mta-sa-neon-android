package com.worldrpg.neonlauncher.ui

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.GridLayoutManager
import com.worldrpg.neonlauncher.databinding.FragmentFavoritesBinding
import com.worldrpg.neonlauncher.network.ServerInfo
import com.worldrpg.neonlauncher.network.ServerQuery
import com.worldrpg.neonlauncher.utils.PreferencesManager
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

class FavoritesFragment : Fragment() {

    private var _binding: FragmentFavoritesBinding? = null
    private val binding get() = _binding!!

    private lateinit var serverAdapter: ServerListAdapter
    private lateinit var prefsManager: PreferencesManager
    private var servers: List<ServerInfo> = emptyList()
    private val queries = mutableListOf<Job>()

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentFavoritesBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        prefsManager = PreferencesManager(requireContext())
        serverAdapter = ServerListAdapter(
            onJoinClick = { server -> JoinDialog.show(this, server) },
            onFavoriteClick = { server -> removeFavorite(server) }
        )
        binding.favoritesList.layoutManager =
            GridLayoutManager(context, ServerBrowserFragment.columnCount(resources.configuration.screenWidthDp))
        binding.favoritesList.adapter = serverAdapter
        childFragmentManager.setFragmentResultListener(AddFavoriteDialog.RESULT_KEY, viewLifecycleOwner) { _, _ ->
            loadFavorites()
        }
        binding.btnAddFavorite.setOnClickListener {
            if (childFragmentManager.findFragmentByTag(AddFavoriteDialog.TAG) == null) {
                AddFavoriteDialog().show(childFragmentManager, AddFavoriteDialog.TAG)
            }
        }
    }

    /** Lists the favorites and queries each one live. */
    private fun loadFavorites() {
        queries.forEach { it.cancel() }
        queries.clear()
        servers = prefsManager.getFavorites().sorted().mapNotNull(ServerInfo::fromFavoriteKey)
        binding.emptyState.visibility = if (servers.isEmpty()) View.VISIBLE else View.GONE
        binding.favoritesList.visibility = if (servers.isEmpty()) View.GONE else View.VISIBLE
        serverAdapter.submitList(servers)
        servers.forEach { s ->
            queries += viewLifecycleOwner.lifecycleScope.launch {
                val updated = s.withQuery(ServerQuery.query(s.ip, s.port))
                servers = servers.map { if (it.getKey() == s.getKey()) updated else it }
                serverAdapter.submitList(servers)
            }
        }
    }

    private fun removeFavorite(server: ServerInfo) {
        prefsManager.removeFavorite(server.getKey())
        servers = servers.filter { it.getKey() != server.getKey() }
        serverAdapter.submitList(servers)
        val empty = servers.isEmpty()
        binding.emptyState.visibility = if (empty) View.VISIBLE else View.GONE
        binding.favoritesList.visibility = if (empty) View.GONE else View.VISIBLE
    }

    override fun onResume() {
        super.onResume()
        loadFavorites()
    }

    override fun onDestroyView() {
        queries.forEach { it.cancel() }
        queries.clear()
        super.onDestroyView()
        _binding = null
    }
}
