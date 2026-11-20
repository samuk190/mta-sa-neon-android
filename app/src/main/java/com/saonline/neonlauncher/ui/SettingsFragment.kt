package com.saonline.neonlauncher.ui

import android.content.Intent
import android.graphics.Rect
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.saonline.neonlauncher.BuildConfig
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.databinding.FragmentSettingsBinding
import com.saonline.neonlauncher.game.EngineHostActivity
import com.saonline.neonlauncher.game.EngineDiagnostics
import com.saonline.neonlauncher.game.InterfaceScale
import com.saonline.neonlauncher.game.InterfaceScaleSettings
import com.saonline.neonlauncher.utils.NativeAssetBridge
import com.saonline.neonlauncher.utils.PreferencesManager
import kotlinx.coroutines.launch

class SettingsFragment : Fragment() {

    private var _binding: FragmentSettingsBinding? = null
    private val binding get() = _binding!!

    private lateinit var prefsManager: PreferencesManager
    private lateinit var nativeAssetBridge: NativeAssetBridge
    private var syncingAssets = false
    private var identityUi: NeonIdentityUi? = null
    private var savedInterfaceScale = InterfaceScale.DEFAULT_PERCENT

    private val engineTest = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.data?.getBooleanExtra(EngineHostActivity.EXTRA_GAME_ASSETS_REQUIRED, false) == true) {
            (activity as? MainActivity)?.showGameAssetsSetup()
        }
        // MainActivity presents stored failures, including native crashes with no Activity result.
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
        identityUi = NeonIdentityUi(requireActivity())
        binding.btnNeonAccount.setOnClickListener {
            val identity = identityUi ?: return@setOnClickListener
            if (identity.isSignedIn()) {
                com.google.android.material.dialog.MaterialAlertDialogBuilder(requireContext())
                    .setTitle(R.string.neon_account_sign_out)
                    .setMessage("Sign out of your SA:O account on this device?")
                    .setPositiveButton(R.string.neon_account_sign_out) { _, _ ->
                        identity.signOut({ updateIdentity() }, { message ->
                            context?.let { Toast.makeText(it, message, Toast.LENGTH_LONG).show() }
                        })
                    }
                    .setNegativeButton(android.R.string.cancel, null).show()
            } else {
                identity.signIn({ updateIdentity() }, { message ->
                    context?.let { Toast.makeText(it, message, Toast.LENGTH_LONG).show() }
                    updateIdentity()
                })
            }
        }
        updateIdentity()

        loadCurrentSettings()
        binding.switchEngineDebug.isChecked = EngineDiagnostics.isEnabled(requireContext())
        binding.switchEngineDebug.setOnCheckedChangeListener { button, enabled ->
            if (enabled != EngineDiagnostics.isEnabled(requireContext()) &&
                !EngineDiagnostics.setEnabled(requireContext(), enabled)) {
                Toast.makeText(context, "Unable to save engine debug setting.", Toast.LENGTH_LONG).show()
                button.isChecked = EngineDiagnostics.isEnabled(requireContext())
            }
        }
        setupValueLabels()
        binding.btnResetInterfaceScale.setOnClickListener {
            binding.sliderInterfaceScale.value = InterfaceScale.DEFAULT_PERCENT.toFloat()
        }
        setupSaveButton()
        setupAssetSyncButton()
        setupEngineTestButton()
    }

    private fun loadCurrentSettings() {
        // The nickname is edited on the server screen, next to the play button.

        // Graphics Quality
        binding.graphicsToggle.check(
            when (prefsManager.graphicsQuality) {
                0 -> R.id.btnGraphicsLow
                2 -> R.id.btnGraphicsHigh
                else -> R.id.btnGraphicsMedium
            }
        )

        // Audio Volume
        binding.sliderVolume.value = prefsManager.audioVolume.toFloat()

        // Touch Sensitivity
        binding.sliderTouchSensitivity.value = prefsManager.touchSensitivity.toFloat()
        savedInterfaceScale = InterfaceScaleSettings.read(requireContext())
        binding.sliderInterfaceScale.value = savedInterfaceScale.toFloat()
    }

    private fun setupValueLabels() {
        binding.volumeValue.text = "${binding.sliderVolume.value.toInt()}%"
        binding.touchValue.text = "${binding.sliderTouchSensitivity.value.toInt()}"
        binding.interfaceScaleValue.text = "${binding.sliderInterfaceScale.value.toInt()}%"
        binding.sliderInterfaceScale.addOnChangeListener { _, value, _ ->
            binding.interfaceScaleValue.text = "${value.toInt()}%"
        }
        binding.sliderVolume.addOnChangeListener { _, value, _ ->
            binding.volumeValue.text = "${value.toInt()}%"
        }
        binding.sliderTouchSensitivity.addOnChangeListener { _, value, _ ->
            binding.touchValue.text = "${value.toInt()}"
        }
    }

    private fun setupSaveButton() {
        binding.btnSaveSettings.setOnClickListener {
            saveSettings()
        }
    }

    private fun setupAssetSyncButton() {
        binding.btnSyncAssets.setOnClickListener { syncGameAssets() }
    }

    internal fun syncGameAssets() {
        val viewBinding = _binding ?: return
        val status = viewBinding.assetStatus
        status.post {
            status.requestRectangleOnScreen(Rect(0, 0, status.width, status.height + viewBinding.btnSyncAssets.height))
        }
        if (syncingAssets) return
        viewBinding.btnSyncAssets.isEnabled = false
        syncingAssets = true
        updateEngineTestButton()
        showAssetProgress(getString(R.string.downloading_assets), null)
        viewLifecycleOwner.lifecycleScope.launch {
            try {
                nativeAssetBridge.syncAssets().collect { state ->
                    when (state) {
                        NativeAssetBridge.DownloadState.FetchingManifest ->
                            showAssetProgress("Fetching game asset manifest...", null)
                        is NativeAssetBridge.DownloadState.Verifying ->
                            showAssetProgress(
                                "Verifying game assets ${state.current}/${state.total}",
                                if (state.total > 0) state.current * 100 / state.total else null
                            )
                        is NativeAssetBridge.DownloadState.Syncing ->
                            showAssetProgress("Syncing game assets ${state.current}/${state.total}", state.percent)
                        is NativeAssetBridge.DownloadState.Complete ->
                            Toast.makeText(context, "${state.fileCount} game files verified", Toast.LENGTH_LONG).show()
                        is NativeAssetBridge.DownloadState.Error ->
                            Toast.makeText(context, state.message, Toast.LENGTH_LONG).show()
                    }
                }
            } finally {
                _binding?.let { b ->
                    b.assetStatus.visibility = View.GONE
                    b.assetProgress.visibility = View.GONE
                    b.btnSyncAssets.isEnabled = true
                }
                syncingAssets = false
                updateEngineTestButton()
            }
        }
    }

    /** [percent] null shows an indeterminate bar. */
    private fun showAssetProgress(status: String, percent: Int?) {
        val b = _binding ?: return
        b.assetStatus.visibility = View.VISIBLE
        b.assetStatus.text = status
        b.assetProgress.visibility = View.VISIBLE
        if (percent == null) {
            if (!b.assetProgress.isIndeterminate) {
                b.assetProgress.visibility = View.INVISIBLE
                b.assetProgress.isIndeterminate = true
                b.assetProgress.visibility = View.VISIBLE
            }
        } else {
            b.assetProgress.isIndeterminate = false
            b.assetProgress.setProgressCompat(percent.coerceIn(0, 100), true)
        }
    }

    private fun setupEngineTestButton() {
        binding.btnTestGta.visibility = if (BuildConfig.DEBUG) View.VISIBLE else View.GONE
        binding.btnTestGta.setOnClickListener {
            if (!BuildConfig.DEBUG || syncingAssets) return@setOnClickListener
            if (EngineHostActivity.findReadyGameDirectory(requireContext(), nativeAssetBridge) == null) {
                (activity as? MainActivity)?.showGameAssetsSetup()
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
            ::nativeAssetBridge.isInitialized &&
            EngineHostActivity.findReadyGameDirectory(requireContext(), nativeAssetBridge) != null
    }

    override fun onResume() {
        super.onResume()
        updateEngineTestButton()
        updateIdentity()
        _binding?.let {
            val current = InterfaceScaleSettings.read(requireContext())
            if (it.sliderInterfaceScale.value.toInt() == savedInterfaceScale)
                it.sliderInterfaceScale.value = current.toFloat()
            savedInterfaceScale = current
        }
    }

    private fun saveSettings() {
        val scale = binding.sliderInterfaceScale.value.toInt()
        if (!InterfaceScaleSettings.save(requireContext(), scale)) {
            Toast.makeText(context, R.string.interface_scale_save_failed, Toast.LENGTH_SHORT).show()
            return
        }
        savedInterfaceScale = scale
        // Save graphics quality
        prefsManager.graphicsQuality = when (binding.graphicsToggle.checkedButtonId) {
            R.id.btnGraphicsLow -> 0
            R.id.btnGraphicsHigh -> 2
            else -> 1
        }

        // Save audio volume
        prefsManager.audioVolume = binding.sliderVolume.value.toInt()

        // Save touch sensitivity
        prefsManager.touchSensitivity = binding.sliderTouchSensitivity.value.toInt()

        Toast.makeText(context, R.string.settings_saved, Toast.LENGTH_SHORT).show()
    }

    override fun onDestroyView() {
        identityUi?.close()
        identityUi = null
        super.onDestroyView()
        _binding = null
    }

    private fun updateIdentity() {
        val b = _binding ?: return
        val connected = identityUi?.isSignedIn() == true
        b.neonAccountStatus.setText(if (connected) R.string.neon_account_connected else R.string.neon_account_disconnected)
        b.btnNeonAccount.setText(if (connected) R.string.neon_account_sign_out else R.string.neon_account_sign_in)
    }
}
