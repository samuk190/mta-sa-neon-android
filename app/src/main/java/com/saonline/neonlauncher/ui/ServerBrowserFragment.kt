package com.saonline.neonlauncher.ui

import android.content.ClipData
import android.content.res.ColorStateList
import android.content.ClipboardManager
import android.graphics.BitmapFactory
import android.os.Bundle
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.widget.PopupMenu
import androidx.appcompat.widget.SearchView
import androidx.core.os.bundleOf
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.core.view.doOnLayout
import androidx.core.view.isVisible
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.databinding.FragmentServerBrowserBinding
import com.saonline.neonlauncher.databinding.ServerFactBinding
import com.saonline.neonlauncher.network.ServerAddress
import com.saonline.neonlauncher.network.ServerBrowserService
import com.saonline.neonlauncher.network.ServerInfo
import com.saonline.neonlauncher.utils.PreferencesManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class ServerBrowserFragment : Fragment(R.layout.fragment_server_browser) {
    private enum class Tab { ALL, FAVORITES, RECENT }

    private var _binding: FragmentServerBrowserBinding? = null
    private val binding get() = requireNotNull(_binding)
    private lateinit var prefs: PreferencesManager
    private lateinit var adapter: ServerListAdapter
    private lateinit var closeDetail: OnBackPressedCallback
    private val browser = ServerBrowserService()
    private var loadJob: Job? = null
    private var catalogueKeys: Set<String> = emptySet()
    private var servers = emptyList<ServerInfo>()
    private var hasCatalogue = false
    private var tab = Tab.ALL
    private var search = ""
    private var onlyOnline = false
    private var selectedKey: String? = null
    private var wide = false
    private var detailOpen = false
    private var displayed: ServerInfo? = null

    override fun onViewCreated(view: View, state: Bundle?) {
        _binding = FragmentServerBrowserBinding.bind(view)
        prefs = PreferencesManager(requireContext())
        if (state != null) {
            tab = Tab.valueOf(state.getString("tab") ?: Tab.ALL.name)
            search = state.getString("query").orEmpty()
            selectedKey = state.getString("selected")
            onlyOnline = state.getBoolean("online")
            detailOpen = state.getBoolean("details")
        }

        closeDetail = object : OnBackPressedCallback(false) {
            override fun handleOnBackPressed() = closeDetails()
        }
        requireActivity().onBackPressedDispatcher.addCallback(viewLifecycleOwner, closeDetail)
        adapter = ServerListAdapter(
            onSelect = { server ->
                selectedKey = server.getKey()
                detailOpen = !wide
                render()
            },
            onJoin = { JoinDialog.show(this, it) },
            onFavorite = { server ->
                val key = server.getKey()
                if (prefs.isFavorite(key)) prefs.removeFavorite(key) else prefs.addFavorite(key)
                render()
            }
        )
        binding.serverList.layoutManager = LinearLayoutManager(requireContext())
        binding.serverList.adapter = adapter
        binding.serverList.itemAnimator = null
        binding.allTab.setOnClickListener { selectTab(Tab.ALL) }
        binding.favoriteTab.setOnClickListener { selectTab(Tab.FAVORITES) }
        binding.recentTab.setOnClickListener { selectTab(Tab.RECENT) }
        binding.btnAddFavorite.setOnClickListener { openAddressDialog(quick = false) }
        binding.details.back.setOnClickListener { closeDetails() }
        binding.btnRefresh.setOnClickListener { loadServers() }
        binding.swipeRefresh.setColorSchemeResources(R.color.browser_amber)
        binding.swipeRefresh.setProgressBackgroundColorSchemeResource(R.color.browser_card)
        binding.swipeRefresh.setOnRefreshListener { loadServers() }
        binding.btnFilter.setOnClickListener { anchor ->
            PopupMenu(requireContext(), anchor).apply {
                menu.add(R.string.browser_online).apply {
                    isCheckable = true
                    isChecked = onlyOnline
                }
                setOnMenuItemClickListener {
                    onlyOnline = !onlyOnline
                    render()
                    true
                }
            }.show()
        }
        setupSearch()
        val gutter = resources.getDimensionPixelSize(R.dimen.screen_padding)
        ViewCompat.setOnApplyWindowInsetsListener(binding.browserContent) { content, insets ->
            val cutout = insets.getInsets(WindowInsetsCompat.Type.displayCutout())
            content.updatePadding(left = maxOf(gutter, cutout.left), right = maxOf(gutter, cutout.right))
            insets
        }
        childFragmentManager.setFragmentResultListener(AddFavoriteDialog.RESULT_KEY, viewLifecycleOwner) { _, _ -> loadServers() }
        childFragmentManager.setFragmentResultListener(AddFavoriteDialog.QUICK_CONNECT, viewLifecycleOwner) { _, result ->
            result.getString("address")?.let(::joinAddress)
        }
        binding.browserContent.addOnLayoutChangeListener { _, left, _, right, _, oldLeft, _, oldRight, _ ->
            if (right - left != oldRight - oldLeft) {
                val content = binding.browserContent
                val widthDp = (right - left - content.paddingLeft - content.paddingRight) / resources.displayMetrics.density
                val fullToolbar = widthDp / resources.configuration.fontScale >= 815
                if (binding.search.isIconfiedByDefault == fullToolbar) {
                    binding.search.setIconifiedByDefault(!fullToolbar)
                    if (search.isNotEmpty()) {
                        binding.search.isIconified = false
                        binding.search.setQuery(search, false)
                    }
                }
                binding.btnFilter.text = if (fullToolbar) getString(R.string.browser_filters) else ""
                val listParams = binding.listPane.layoutParams as LinearLayout.LayoutParams
                val listWeight = if (widthDp >= 900) 2.8f else 2f
                if (listWeight != listParams.weight) {
                    listParams.weight = listWeight
                    binding.listPane.layoutParams = listParams
                }
                wide = widthDp >= 600 && widthDp / resources.configuration.fontScale >= 540
                val tabSize = if (wide) 14f else 12f
                listOf(binding.allTab, binding.favoriteTab, binding.recentTab).forEach { it.textSize = tabSize }
                render()
            }
        }
        val assets = requireContext().assets
        viewLifecycleOwner.lifecycleScope.launch {
            val wallpaper = withContext(Dispatchers.IO) {
                assets.open("MTA/cgui/images/loadscreens/loadsc4.jpg").use { BitmapFactory.decodeStream(it) }
            }
            binding.wallpaper.setImageBitmap(wallpaper)
        }
        loadServers()
    }

    private fun setupSearch() {
        val field = binding.search
        field.findViewById<TextView>(androidx.appcompat.R.id.search_src_text).apply {
            setTextAppearance(R.style.BrowserText_Small)
            setHintTextColor(requireContext().getColor(R.color.browser_muted))
        }
        if (search.isNotEmpty()) {
            field.isIconified = false
            field.setQuery(search, false)
            field.clearFocus()
        }
        field.setOnQueryTextListener(object : SearchView.OnQueryTextListener {
            override fun onQueryTextChange(text: String): Boolean {
                search = text.trim()
                render()
                return true
            }
            override fun onQueryTextSubmit(text: String): Boolean {
                field.clearFocus()
                return joinAddress(text.trim())
            }
        })
    }

    private fun joinAddress(text: String): Boolean {
        val address = ServerAddress.fromKey(text) ?: return false
        val server = servers.firstOrNull { it.getKey() == address.key() }
            ?: ServerInfo(name = address.key(), ip = address.host, port = address.port, status = ServerInfo.Status.DIRECT)
        JoinDialog.show(this, server)
        return true
    }

    fun openAddressDialog(quick: Boolean) {
        if (childFragmentManager.findFragmentByTag(AddFavoriteDialog.TAG) != null) return
        AddFavoriteDialog().apply { arguments = bundleOf(AddFavoriteDialog.QUICK_CONNECT to quick) }
            .show(childFragmentManager, AddFavoriteDialog.TAG)
    }

    private fun selectTab(next: Tab) {
        tab = next
        detailOpen = false
        binding.serverList.scrollToPosition(0)
        render()
    }

    private fun closeDetails() {
        detailOpen = false
        render()
    }

    private fun loadServers() {
        loadJob?.cancel()
        binding.browserStatus.isVisible = false
        binding.swipeRefresh.isRefreshing = true
        loadJob = viewLifecycleOwner.lifecycleScope.launch {
            browser.fetchServerList(prefs.getFavorites(), prefs.getRecentServers()).collect { state ->
                when (state) {
                    ServerBrowserService.BrowserState.Loading -> Unit
                    is ServerBrowserService.BrowserState.Error -> binding.browserStatus.isVisible = true
                    is ServerBrowserService.BrowserState.Success -> {
                        servers = state.servers
                        catalogueKeys = state.catalogueKeys
                        hasCatalogue = true
                        render()
                    }
                }
            }
            binding.swipeRefresh.isRefreshing = false
        }
    }

    private fun render() {
        val b = _binding ?: return
        val favorites = prefs.getFavorites()
        val recent = prefs.getRecentServers()
        val available = servers.associateBy { it.getKey() }
        val entries = when (tab) {
            Tab.ALL -> servers.filter { it.getKey() in catalogueKeys }
            Tab.FAVORITES -> favorites.sorted().mapNotNull(available::get)
            Tab.RECENT -> recent.mapNotNull(available::get)
        }
        val visible = entries.filter { server ->
            (!onlyOnline || server.status == ServerInfo.Status.ONLINE) &&
                listOf(server.name, server.getKey(), server.gameMode, server.mapName).any { it.contains(search, ignoreCase = true) }
        }
        val selected = visible.firstOrNull { it.getKey() == selectedKey } ?: visible.firstOrNull()
        if (hasCatalogue) {
            selectedKey = selected?.getKey()
            if (selected == null) detailOpen = false
        }
        b.allTab.text = getString(R.string.browser_tab_count, getString(R.string.browser_all), catalogueKeys.size)
        b.favoriteTab.text = getString(R.string.browser_tab_count, getString(R.string.favorites), favorites.size)
        b.allTab.isSelected = tab == Tab.ALL
        b.favoriteTab.isSelected = tab == Tab.FAVORITES
        b.recentTab.isSelected = tab == Tab.RECENT
        b.btnAddFavorite.isVisible = tab == Tab.FAVORITES
        b.btnFilter.isSelected = onlyOnline
        b.btnFilter.iconTint = ColorStateList.valueOf(requireContext().getColor(
            if (onlyOnline) R.color.browser_amber else R.color.browser_muted))
        val online = servers.filter { it.getKey() in catalogueKeys && it.status == ServerInfo.Status.ONLINE }
        val players = online.sumOf { it.players }
        b.serverCount.isVisible = wide && hasCatalogue
        b.serverCount.compoundDrawableTintList = ColorStateList.valueOf(requireContext().getColor(
            if (online.isEmpty()) R.color.browser_muted else R.color.browser_online_dot))
        b.serverCount.text = getString(R.string.server_browser_summary,
            resources.getQuantityString(R.plurals.servers_count, catalogueKeys.size, catalogueKeys.size),
            resources.getQuantityString(R.plurals.players_online, players, players))
        b.subtitle.text = if (!wide && hasCatalogue) b.serverCount.text else getString(R.string.browser_subtitle)

        adapter.submitList(visible.map { ServerListAdapter.Row(it.copy(isFavorite = it.getKey() in favorites), it == selected) })
        b.emptyState.isVisible = hasCatalogue && visible.isEmpty()
        b.emptyState.setText(when {
            search.isNotEmpty() || onlyOnline -> R.string.browser_no_match
            tab == Tab.FAVORITES -> R.string.no_favorites_hint
            tab == Tab.RECENT -> R.string.browser_no_recent
            else -> R.string.no_servers
        })
        b.listPane.isVisible = wide || !detailOpen
        b.details.root.isVisible = selected != null && (wide || detailOpen)
        b.details.back.isVisible = !wide
        closeDetail.isEnabled = !isHidden && !wide && detailOpen
        if (b.details.root.isVisible && selected != null && displayed != selected) {
            bindDetails(selected)
            displayed = selected
        }
    }

    private fun bindDetails(server: ServerInfo) = with(binding.details) {
        val old = displayed
        if (old?.getKey() != server.getKey()) root.scrollTo(0, 0)
        val artwork = server.bannerUrl.ifEmpty { server.logoUrl }
        if (old?.name != server.name || old.bannerUrl.ifEmpty { old.logoUrl } != artwork) {
            banner.doOnLayout { ServerLogo.bind(banner, server.name, artwork) }
        }
        name.text = server.name
        description.text = server.description
        description.isVisible = server.description.isNotBlank()
        connect.setOnClickListener { JoinDialog.show(this@ServerBrowserFragment, server) }
        copyIp.setOnClickListener {
            val key = server.getKey()
            requireContext().getSystemService(ClipboardManager::class.java).setPrimaryClip(ClipData.newPlainText("IP", key))
            if (android.os.Build.VERSION.SDK_INT < 33) Toast.makeText(requireContext(), R.string.browser_copied, Toast.LENGTH_SHORT).show()
        }
        val online = server.status == ServerInfo.Status.ONLINE
        fun fact(row: ServerFactBinding, label: Int, value: String) {
            row.root.isVisible = value.isNotBlank()
            row.label.setText(label)
            row.value.text = value
        }
        fact(endpoint, R.string.browser_endpoint, server.getKey())
        fact(players, R.string.browser_players, if (online) getString(R.string.players_count, server.players, server.maxPlayers) else "")
        fact(ping, R.string.browser_ping, if (online) getString(R.string.ping_ms, server.ping) else "")
        fact(mode, R.string.browser_mode, server.gameMode)
        fact(map, R.string.browser_map, server.mapName)
        fact(version, R.string.browser_version, server.version)
        fact(languages, R.string.browser_languages, server.languages.joinToString(", "))
    }

    override fun onResume() {
        super.onResume()
        // Quick connect can add a new endpoint while the browser is paused in the game.
        if (hasCatalogue && prefs.getRecentServers().any { key -> servers.none { it.getKey() == key } }) loadServers()
        render()
    }

    override fun onHiddenChanged(hidden: Boolean) {
        super.onHiddenChanged(hidden)
        if (_binding != null) render()
    }

    override fun onSaveInstanceState(state: Bundle) {
        super.onSaveInstanceState(state)
        state.putString("tab", tab.name)
        state.putString("query", search)
        state.putBoolean("online", onlyOnline)
        state.putBoolean("details", detailOpen)
        state.putString("selected", selectedKey)
    }

    override fun onDestroyView() {
        binding.serverList.adapter = null
        loadJob?.cancel()
        displayed = null
        _binding = null
        super.onDestroyView()
    }
}
