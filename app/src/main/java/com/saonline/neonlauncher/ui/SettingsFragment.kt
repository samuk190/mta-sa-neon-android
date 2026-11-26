package com.saonline.neonlauncher.ui

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updateLayoutParams
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.saonline.neonlauncher.BuildConfig
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.databinding.FragmentSettingsBinding
import com.saonline.neonlauncher.game.EngineHostActivity
import com.saonline.neonlauncher.game.EngineDiagnostics
import com.saonline.neonlauncher.game.InterfaceScale
import com.saonline.neonlauncher.game.InterfaceScaleSettings
import com.saonline.neonlauncher.utils.NativeAssetBridge
import com.saonline.neonlauncher.utils.PreferencesManager
import java.util.Locale

class SettingsFragment : Fragment() {

    private var _binding: FragmentSettingsBinding? = null
    private val binding get() = _binding!!

    private lateinit var prefsManager: PreferencesManager
    private lateinit var nativeAssetBridge: NativeAssetBridge
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
        setupLayout()

        prefsManager = PreferencesManager(requireContext())
        nativeAssetBridge = NativeAssetBridge(requireContext())
        identityUi = NeonIdentityUi(requireActivity())
        binding.btnNeonAccount.setOnClickListener {
            val identity = identityUi ?: return@setOnClickListener
            if (identity.isSignedIn()) {
                MaterialAlertDialogBuilder(binding.root.context)
                    .setTitle(R.string.neon_account_sign_out)
                    .setMessage(R.string.neon_account_sign_out_confirm)
                    .setPositiveButton(R.string.neon_account_sign_out) { _, _ ->
                        identity.signOut({ updateIdentity() }, { message ->
                            context?.let { Toast.makeText(it, message, Toast.LENGTH_LONG).show() }
                        })
                    }
                    .setNegativeButton(R.string.cancel, null).show()
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
                Toast.makeText(context, R.string.engine_debug_save_failed, Toast.LENGTH_LONG).show()
                button.isChecked = EngineDiagnostics.isEnabled(requireContext())
            }
        }
        setupValueLabels()
        binding.btnResetInterfaceScale.setOnClickListener {
            binding.sliderInterfaceScale.value = InterfaceScale.DEFAULT_PERCENT.toFloat()
        }
        setupSaveButton()
        binding.btnSyncAssets.setOnClickListener { (activity as? MainActivity)?.openGameFilesForTransfer() }
        setupEngineTestButton()
    }

    private fun setupLayout() {
        val gutter = resources.getDimensionPixelSize(R.dimen.screen_padding)
        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { content, insets ->
            val cutout = insets.getInsets(WindowInsetsCompat.Type.displayCutout())
            content.updatePadding(left = maxOf(gutter, cutout.left), right = maxOf(gutter, cutout.right))
            insets
        }
        // Use the available width inside the cutout and make room for larger text.
        binding.settingsScroll.addOnLayoutChangeListener { _, left, _, right, _, _, _, _, _ ->
            val widthDp = (right - left) / resources.displayMetrics.density
            val wide = widthDp >= 600 && widthDp / resources.configuration.fontScale >= 540
            val direction = if (wide) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
            if (binding.settingsColumns.orientation != direction) {
                binding.settingsColumns.orientation = direction
                listOf(binding.gameColumn, binding.launcherColumn).forEach { column ->
                    column.updateLayoutParams<LinearLayout.LayoutParams> {
                        width = if (wide) 0 else ViewGroup.LayoutParams.MATCH_PARENT
                        weight = if (wide) 1f else 0f
                        marginStart = if (wide && column == binding.launcherColumn) gutter / 2 else 0
                    }
                }
            }
        }
    }

    private fun setupLanguagePicker() {
        val languages = listOf(
            "" to getString(R.string.language_system),
            "en" to "English",
            "pt-BR" to "Português (Brasil)",
            "es" to "Español",
            "de" to "Deutsch",
            "zh-Hans" to "简体中文",
            "ru" to "Русский"
        )
        val locale = AppCompatDelegate.getApplicationLocales()[0]
        var selected = languages.indexOfFirst { (tag, _) ->
            if (locale == null) tag.isEmpty()
            else tag.isNotEmpty() && LocaleListCompat.matchesLanguageAndScript(Locale.forLanguageTag(tag), locale)
        }
        val label = languages.getOrNull(selected)?.second ?: locale?.getDisplayName(locale).orEmpty()
        binding.btnLauncherLanguage.text = getString(R.string.language_selected, label)
        binding.btnLauncherLanguage.setOnClickListener {
            MaterialAlertDialogBuilder(binding.root.context)
                .setTitle(R.string.language)
                .setSingleChoiceItems(languages.map { it.second }.toTypedArray(), selected) { dialog, index ->
                    dialog.dismiss()
                    if (index != selected) {
                        selected = index
                        val (tag, name) = languages[index]
                        binding.btnLauncherLanguage.text = getString(R.string.language_selected, name)
                        AppCompatDelegate.setApplicationLocales(LocaleListCompat.forLanguageTags(tag))
                    }
                }
                .setNegativeButton(R.string.cancel, null)
                .show()
        }
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
        binding.volumeValue.text = getString(R.string.percentage, binding.sliderVolume.value.toInt())
        binding.touchValue.text = "${binding.sliderTouchSensitivity.value.toInt()}"
        binding.interfaceScaleValue.text = getString(R.string.percentage, binding.sliderInterfaceScale.value.toInt())
        binding.sliderInterfaceScale.addOnChangeListener { _, value, _ ->
            binding.interfaceScaleValue.text = getString(R.string.percentage, value.toInt())
        }
        binding.sliderVolume.addOnChangeListener { _, value, _ ->
            binding.volumeValue.text = getString(R.string.percentage, value.toInt())
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

    private fun setupEngineTestButton() {
        binding.btnTestGta.visibility = if (BuildConfig.DEBUG) View.VISIBLE else View.GONE
        binding.btnTestGta.setOnClickListener {
            if (!BuildConfig.DEBUG || (activity as? MainActivity)?.isTransferringGameFiles == true) return@setOnClickListener
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

    internal fun updateEngineTestButton() {
        val viewBinding = _binding ?: return
        viewBinding.btnTestGta.isEnabled = BuildConfig.DEBUG && (activity as? MainActivity)?.isTransferringGameFiles != true &&
            ::nativeAssetBridge.isInitialized &&
            EngineHostActivity.findReadyGameDirectory(requireContext(), nativeAssetBridge) != null
    }

    override fun onResume() {
        super.onResume()
        updateEngineTestButton()
        updateIdentity()
        _binding?.let {
            setupLanguagePicker()
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
