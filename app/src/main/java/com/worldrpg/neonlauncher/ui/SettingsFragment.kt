package com.worldrpg.neonlauncher.ui

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.worldrpg.neonlauncher.BuildConfig
import com.worldrpg.neonlauncher.databinding.FragmentSettingsBinding
import com.worldrpg.neonlauncher.game.EngineHostActivity
import com.worldrpg.neonlauncher.utils.NativeAssetBridge
import com.worldrpg.neonlauncher.utils.PreferencesManager
import kotlinx.coroutines.launch

class SettingsFragment : Fragment() {

    private var _binding: FragmentSettingsBinding? = null
    private val binding get() = _binding!!

    private lateinit var prefsManager: PreferencesManager
    private lateinit var nativeAssetBridge: NativeAssetBridge
    private var syncingAssets = false

    private val engineTest = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        val error = result.data?.getStringExtra(EngineHostActivity.EXTRA_BOOT_ERROR)
        if (error != null) context?.let { Toast.makeText(it, error, Toast.LENGTH_LONG).show() }
        updateEngineTestButton()
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentSettingsBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        prefsManager = PreferencesManager(requireContext())
        nativeAssetBridge = NativeAssetBridge(requireContext())

        loadCurrentSettings()
        setupSaveButton()
        setupAssetSyncButton()
        setupEngineTestButton()
    }

    private fun loadCurrentSettings() {
        // Username
        binding.editUsername.setText(prefsManager.username)

        // Graphics Quality
        when (prefsManager.graphicsQuality) {
            0 -> binding.radioLow.isChecked = true
            1 -> binding.radioMedium.isChecked = true
            2 -> binding.radioHigh.isChecked = true
        }

        // Audio Volume
        binding.sliderVolume.value = prefsManager.audioVolume.toFloat()

        // Touch Sensitivity
        binding.sliderTouchSensitivity.value = prefsManager.touchSensitivity.toFloat()
    }

    private fun setupSaveButton() {
        binding.btnSaveSettings.setOnClickListener {
            saveSettings()
        }
    }

    private fun setupAssetSyncButton() {
        val button = binding.btnSyncAssets
        val idleText = button.text
        button.setOnClickListener {
            button.isEnabled = false
            syncingAssets = true
            updateEngineTestButton()
            viewLifecycleOwner.lifecycleScope.launch {
                try {
                    nativeAssetBridge.syncAssets().collect { state ->
                        when (state) {
                            NativeAssetBridge.DownloadState.FetchingManifest ->
                                button.text = "Fetching game asset manifest..."
                            is NativeAssetBridge.DownloadState.Verifying ->
                                button.text = "Verifying game assets ${state.current}/${state.total}"
                            is NativeAssetBridge.DownloadState.Downloading ->
                                button.text = "Downloading ${state.fileName}: ${state.percent}%"
                            is NativeAssetBridge.DownloadState.Complete ->
                                Toast.makeText(
                                    context,
                                    "${state.fileCount} game files verified and ready",
                                    Toast.LENGTH_LONG
                                ).show()
                            is NativeAssetBridge.DownloadState.Error ->
                                Toast.makeText(context, state.message, Toast.LENGTH_LONG).show()
                        }
                    }
                } finally {
                    button.text = idleText
                    button.isEnabled = true
                    syncingAssets = false
                    updateEngineTestButton()
                }
            }
        }
    }

    private fun setupEngineTestButton() {
        binding.btnTestGta.visibility = if (BuildConfig.DEBUG) View.VISIBLE else View.GONE
        binding.btnTestGta.setOnClickListener {
            if (!BuildConfig.DEBUG || syncingAssets) return@setOnClickListener
            if (nativeAssetBridge.getReadyStoragePath() == null) {
                Toast.makeText(context, "Sincronize os arquivos do jogo antes de testar o GTA.", Toast.LENGTH_LONG).show()
                updateEngineTestButton()
                return@setOnClickListener
            }
            engineTest.launch(Intent(requireContext(), EngineHostActivity::class.java).apply {
                // Reuse a paused host already in this task; GTA's globals
                // cannot belong to two Activities in the engine process.
                addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
            })
        }
        updateEngineTestButton()
    }

    private fun updateEngineTestButton() {
        val viewBinding = _binding ?: return
        viewBinding.btnTestGta.isEnabled = BuildConfig.DEBUG && !syncingAssets &&
            ::nativeAssetBridge.isInitialized && nativeAssetBridge.getReadyStoragePath() != null
    }

    override fun onResume() {
        super.onResume()
        updateEngineTestButton()
    }

    private fun saveSettings() {
        // Save username (ASCII-only validation)
        val username = binding.editUsername.text.toString().trim()
        if (username.isNotEmpty()) {
            prefsManager.username = username
        }

        // Save graphics quality
        prefsManager.graphicsQuality = when {
            binding.radioLow.isChecked -> 0
            binding.radioHigh.isChecked -> 2
            else -> 1
        }

        // Save audio volume
        prefsManager.audioVolume = binding.sliderVolume.value.toInt()

        // Save touch sensitivity
        prefsManager.touchSensitivity = binding.sliderTouchSensitivity.value.toInt()

        Toast.makeText(context, "Settings saved", Toast.LENGTH_SHORT).show()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}
