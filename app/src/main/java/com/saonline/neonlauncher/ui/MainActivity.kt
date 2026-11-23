package com.saonline.neonlauncher.ui

import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.core.view.updatePadding
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.saonline.neonlauncher.BuildConfig
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.databinding.ActivityMainBinding
import com.saonline.neonlauncher.game.EngineHostActivity
import com.saonline.neonlauncher.network.AndroidInstallCredential
import com.saonline.neonlauncher.network.JoinProtocolSettings
import com.saonline.neonlauncher.network.NetcModuleStore
import com.saonline.neonlauncher.network.PcNetworkSettings
import com.saonline.neonlauncher.game.EngineDiagnostics
import com.saonline.neonlauncher.game.GameActivity
import com.saonline.neonlauncher.network.ServerInfo
import com.saonline.neonlauncher.utils.PreferencesManager
import com.saonline.neonlauncher.utils.NativeAssetBridge
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

    // netc.dll import (NetcSourceDialog): the system file picker reports here.
    // The pending build survives a recreate (saved state), so a file picked
    // while the activity was rebuilt is still imported.
    private var netcPickCallback: ((android.net.Uri?) -> Unit)? = null
    private var netcPickBuild: String? = null
    private val netcPicker = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val callback = netcPickCallback
        val buildId = netcPickBuild
        netcPickCallback = null
        netcPickBuild = null
        if (callback != null) return@registerForActivityResult callback(uri)
        val build = PcNetworkSettings.NetcBuild.values().firstOrNull { it.id == buildId }
        if (uri == null || build == null) return@registerForActivityResult
        lifecycleScope.launch {
            val error = withContext(Dispatchers.IO) {
                try {
                    NetcModuleStore.importFrom(applicationContext, build, uri)
                    null
                } catch (e: java.io.IOException) {
                    e.message ?: e.javaClass.simpleName
                }
            }
            Toast.makeText(this@MainActivity,
                if (error == null) getString(R.string.pc_network_installed) else getString(R.string.pc_network_import_failed, error),
                Toast.LENGTH_LONG).show()
        }
    }

    fun pickNetcFile(build: PcNetworkSettings.NetcBuild, callback: (android.net.Uri?) -> Unit) {
        netcPickCallback?.invoke(null)
        netcPickCallback = callback
        netcPickBuild = build.id
        // DLLs have no reliable MIME type across file providers.
        netcPicker.launch(arrayOf("application/x-msdownload", "application/octet-stream", "*/*"))
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
        // The old rail stored a removed FavoritesFragment in the fragment-manager snapshot.
        val state = savedInstanceState?.takeUnless { it.containsKey("launcher_nav_item") }
        super.onCreate(state)
        netcPickBuild = savedInstanceState?.getString(STATE_NETC_PICK_BUILD)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        lifecycleScope.launch {
            try {
                val certificate = AndroidInstallCredential.enroll(applicationContext)
                if (certificate == null) Log.i("NeonIdentity", "The issuer has not enabled Android install enrollment.")
                else Log.i("NeonIdentity", "Android install credential ready.")
            } catch (cancelled: kotlinx.coroutines.CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                Log.w("NeonIdentity", "Android install enrollment unavailable (${error.javaClass.simpleName}).")
            }
        }

        title = BuildConfig.LAUNCHER_TITLE
        applyWindowInsets()
        setupNavigation(state)
        binding.quickConnect.setOnClickListener {
            showSettings(false)
            val browser = supportFragmentManager.findFragmentByTag("tab_servers") as ServerBrowserFragment
            browser.openAddressDialog(quick = true)
        }
        binding.quit.setOnClickListener { finish() }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putBoolean("settings_open", binding.navSettings.isSelected)
        netcPickBuild?.let { outState.putString(STATE_NETC_PICK_BUILD, it) }
        super.onSaveInstanceState(outState)
    }

    private fun applyWindowInsets() {
        WindowCompat.getInsetsController(window, binding.root).apply {
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
        ViewCompat.setOnApplyWindowInsetsListener(binding.rootLayout) { _, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.ime())
            binding.rootLayout.updatePadding(top = bars.top, bottom = bars.bottom)
            val settings = binding.navSettings.isSelected
            binding.fragmentContainer.updatePadding(left = if (settings) bars.left else 0, right = if (settings) bars.right else 0)
            binding.dock.updatePadding(left = bars.left, right = bars.right)
            insets
        }
    }

    private fun setupNavigation(saved: Bundle?) {
        val fm = supportFragmentManager
        if (fm.findFragmentByTag("tab_servers") == null) {
            fm.beginTransaction()
                .add(R.id.fragmentContainer, ServerBrowserFragment(), "tab_servers")
                .add(R.id.fragmentContainer, SettingsFragment(), "tab_settings")
                .commitNow()
        }
        binding.navServers.setOnClickListener { showSettings(false) }
        binding.navSettings.setOnClickListener { showSettings(true) }
        showSettings(saved?.getBoolean("settings_open") == true)
    }

    private fun showSettings(settings: Boolean) {
        binding.navServers.isSelected = !settings
        binding.navSettings.isSelected = settings
        ViewCompat.requestApplyInsets(binding.rootLayout)
        val fm = supportFragmentManager
        val browser = fm.findFragmentByTag("tab_servers") ?: return
        val options = fm.findFragmentByTag("tab_settings") ?: return
        val visible = if (settings) options else browser
        val hidden = if (settings) browser else options
        fm.beginTransaction().setReorderingAllowed(true)
            .hide(hidden).setMaxLifecycle(hidden, Lifecycle.State.STARTED)
            .show(visible).setMaxLifecycle(visible, Lifecycle.State.RESUMED)
            .commitNow()
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
                val netcBuild = PcNetworkSettings.buildFor(server)
                val pcNetwork = PcNetworkSettings.isEnabled(this@MainActivity, server.ip, server.port)
                // An empty path means the normal transport. The join dialog made
                // sure the module exists; a bundled copy is installed here.
                val netcPath = if (!pcNetwork) "" else withContext(Dispatchers.IO) {
                    NetcModuleStore.resolve(applicationContext, netcBuild)?.absolutePath
                }
                if (netcPath == null) {
                    Toast.makeText(this@MainActivity, R.string.pc_network_source_title, Toast.LENGTH_LONG).show()
                    return@launch
                }
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
                    putExtra(PcNetworkSettings.EXTRA_NETC_PATH, netcPath)
                    putExtra(PcNetworkSettings.EXTRA_NETC_BUILD, netcBuild.id)
                })
                PreferencesManager(this@MainActivity).rememberServer(server.getKey())
            } finally {
                checkingGameAssets = false
            }
        }
    }

    internal fun showGameAssetsSetup() {
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.game_setup_title)
            .setMessage(R.string.game_setup_message)
            .setNegativeButton(R.string.cancel, null)
            .setPositiveButton(R.string.sync_assets) { _, _ ->
                showSettings(true)
                (supportFragmentManager.findFragmentByTag("tab_settings") as? SettingsFragment)?.syncGameAssets()
            }
            .show()
    }

    private companion object {
        const val STATE_NETC_PICK_BUILD = "netc_pick_build"
    }
}
