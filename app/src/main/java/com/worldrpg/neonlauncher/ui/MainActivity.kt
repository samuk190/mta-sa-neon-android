package com.worldrpg.neonlauncher.ui

import android.content.Intent
import android.os.Bundle
import android.widget.ImageView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.worldrpg.neonlauncher.BuildConfig
import com.worldrpg.neonlauncher.R
import com.worldrpg.neonlauncher.databinding.ActivityMainBinding
import com.worldrpg.neonlauncher.game.EngineHostActivity
import com.worldrpg.neonlauncher.network.JoinProtocolSettings
import com.worldrpg.neonlauncher.game.EngineDiagnostics
import com.worldrpg.neonlauncher.game.GameActivity
import com.worldrpg.neonlauncher.network.ServerInfo
import com.worldrpg.neonlauncher.utils.NativeAssetBridge
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var checkingGameAssets = false
    private val game = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.data?.getBooleanExtra(EngineHostActivity.EXTRA_GAME_ASSETS_REQUIRED, false) == true) {
            showGameAssetsSetup()
        }
    }

    override fun onResume() {
        super.onResume()
        EngineDiagnostics.checkForFailure(this)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) EngineDiagnostics.checkForFailure(this)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        title = BuildConfig.LAUNCHER_TITLE
        applyWindowInsets()
        setupRailHeader()
        setupTabs(savedInstanceState == null)
        setupNavigation()
    }

    /**
     * Edge to edge: the rail background runs under the status bar and the
     * camera cutout, and the content is padded so nothing sits beneath them.
     */
    private fun applyWindowInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(binding.rootLayout) { _, insets ->
            val bars = insets.getInsets(
                WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout()
            )
            binding.navigationRail.updatePadding(left = bars.left, top = bars.top, bottom = bars.bottom)
            binding.fragmentContainer.updatePadding(top = bars.top, right = bars.right, bottom = bars.bottom)
            WindowInsetsCompat.CONSUMED
        }
    }

    private fun setupRailHeader() {
        val logo = binding.navigationRail.headerView?.findViewById<ImageView>(R.id.railLogo) ?: return
        logo.contentDescription = BuildConfig.DEFAULT_SERVER_NAME
        ServerLogo.bind(logo, BuildConfig.DEFAULT_SERVER_NAME, BuildConfig.DEFAULT_SERVER_LOGO_URL)
    }

    /**
     * The three tabs live in one container; the selected one is shown and resumed,
     * the others hidden and paused. ViewPager2 is not used: with swiping off it added
     * nothing, and its page recycling cancelled taps on the server cards
     * (RecyclerView.removeDetachedView -> ViewGroup.cancelTouchTarget during a remeasure).
     */
    private val tabTags = listOf("tab_servers", "tab_favorites", "tab_settings")

    private fun setupTabs(fresh: Boolean) {
        if (fresh) {
            val tx = supportFragmentManager.beginTransaction().setReorderingAllowed(true)
            listOf(ServerBrowserFragment(), FavoritesFragment(), SettingsFragment()).forEachIndexed { i, f ->
                tx.add(R.id.fragmentContainer, f, tabTags[i])
            }
            tx.commitNow()
        }
        val ids = listOf(R.id.nav_servers, R.id.nav_favorites, R.id.nav_settings)
        showTab(ids.indexOf(binding.navigationRail.selectedItemId).coerceAtLeast(0))
    }

    private var selectedTab = 0

    private fun showTab(index: Int) {
        selectedTab = index
        val tx = supportFragmentManager.beginTransaction().setReorderingAllowed(true)
        tabTags.forEachIndexed { i, tag ->
            val f = supportFragmentManager.findFragmentByTag(tag) ?: return@forEachIndexed
            if (i == index) {
                tx.show(f).setMaxLifecycle(f, Lifecycle.State.RESUMED)
            } else {
                tx.hide(f).setMaxLifecycle(f, Lifecycle.State.STARTED)
            }
        }
        tx.commit()
    }

    private fun setupNavigation() {
        binding.navigationRail.setOnItemSelectedListener { item ->
            when (item.itemId) {
                R.id.nav_servers -> { showTab(0); true }
                R.id.nav_favorites -> { showTab(1); true }
                R.id.nav_settings -> { showTab(2); true }
                else -> false
            }
        }
    }

    /**
     * Opens the real GTA activity. That host draws the world and the MobileSA
     * touch controls. The blank GameActivity surface only pumped the network.
     */
    fun launchGame(server: ServerInfo) {
        if (checkingGameAssets) return
        checkingGameAssets = true
        lifecycleScope.launch {
            try {
                val ready = withContext(Dispatchers.IO) {
                    EngineHostActivity.findReadyGameDirectory(applicationContext, NativeAssetBridge(applicationContext)) != null
                }
                if (isFinishing || !lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) return@launch
                if (!ready) {
                    showGameAssetsSetup()
                    return@launch
                }
                game.launch(Intent(this@MainActivity, EngineHostActivity::class.java).apply {
                    putExtra(GameActivity.EXTRA_SERVER_IP, server.ip)
                    putExtra(GameActivity.EXTRA_SERVER_PORT, server.port)
                    putExtra(GameActivity.EXTRA_SERVER_NAME, server.name)
                    putExtra(JoinProtocolSettings.EXTRA_JOIN_PROFILE, JoinProtocolSettings.read(this@MainActivity, server.ip, server.port))
                })
            } finally {
                checkingGameAssets = false
            }
        }
    }

    internal fun showGameAssetsSetup() {
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.game_setup_title)
            .setMessage(R.string.game_setup_message)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.sync_assets) { _, _ ->
                binding.navigationRail.selectedItemId = R.id.nav_settings
                (supportFragmentManager.findFragmentByTag(tabTags[2]) as? SettingsFragment)?.syncGameAssets()
            }
            .show()
    }
}
