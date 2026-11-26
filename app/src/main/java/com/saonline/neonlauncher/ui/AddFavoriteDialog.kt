package com.saonline.neonlauncher.ui

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.LinearLayout
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatDialog
import androidx.core.os.bundleOf
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.core.view.updateLayoutParams
import androidx.core.widget.doAfterTextChanged
import androidx.fragment.app.DialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.databinding.DialogAddFavoriteBinding
import com.saonline.neonlauncher.databinding.DialogQuickConnectBinding
import com.saonline.neonlauncher.network.ServerAddress
import com.saonline.neonlauncher.utils.PreferencesManager

class AddFavoriteDialog : DialogFragment() {
    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val context = requireContext()
        val quick = arguments?.getBoolean(QUICK_CONNECT) == true
        val popup = if (quick) AppCompatDialog(context, R.style.Theme_WorldRPGNeon_QuickConnect) else null
        val panel = popup?.let { DialogQuickConnectBinding.inflate(LayoutInflater.from(it.context)) }
        val binding = panel?.addressFields ?: DialogAddFavoriteBinding.inflate(LayoutInflater.from(context))
        val prefs = PreferencesManager(context)
        binding.editHost.doAfterTextChanged { binding.hostLayout.error = null }
        binding.editPort.doAfterTextChanged { binding.portLayout.error = null }

        fun submit() {
            val host = ServerAddress.normalizeHost(binding.editHost.text?.toString().orEmpty())
            val port = ServerAddress.parsePort(binding.editPort.text?.toString().orEmpty())
            binding.hostLayout.error = if (host == null) getString(R.string.server_host_invalid) else null
            binding.portLayout.error = if (port == null) getString(R.string.server_port_invalid) else null
            if (host == null || port == null) return
            val address = ServerAddress(host, port)
            if (!quick && prefs.isFavorite(address.key())) {
                binding.hostLayout.error = getString(R.string.server_already_favorite)
                return
            }
            context.getSystemService(InputMethodManager::class.java)
                ?.hideSoftInputFromWindow(binding.editPort.windowToken, 0)
            if (quick) {
                parentFragmentManager.setFragmentResult(QUICK_CONNECT, bundleOf("address" to address.key()))
            } else {
                prefs.addFavorite(address.key())
                parentFragmentManager.setFragmentResult(RESULT_KEY, bundleOf())
            }
            dismiss()
        }

        binding.editPort.setOnEditorActionListener { _, action, _ ->
            if (action == EditorInfo.IME_ACTION_DONE) { submit(); true } else false
        }

        if (popup != null && panel != null) {
            popup.setTitle(R.string.browser_quick)
            popup.setContentView(panel.root)
            popup.setCanceledOnTouchOutside(true)
            panel.cancel.setOnClickListener { dismiss() }
            panel.connect.setOnClickListener { submit() }
            binding.editHost.imeOptions = binding.editHost.imeOptions or EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
            binding.editPort.imeOptions = binding.editPort.imeOptions or EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
            popup.setOnShowListener {
                val window = requireNotNull(popup.window)
                val metrics = resources.displayMetrics
                val config = resources.configuration
                val width = minOf(resources.getDimensionPixelSize(R.dimen.quick_connect_max_width),
                    (config.screenWidthDp * metrics.density).toInt() - 2 * resources.getDimensionPixelSize(R.dimen.screen_padding))
                window.setLayout(width, ViewGroup.LayoutParams.WRAP_CONTENT)
                if (width / metrics.density / config.fontScale >= 420) {
                    binding.root.orientation = LinearLayout.HORIZONTAL
                    binding.root.isBaselineAligned = false
                    binding.hostLayout.updateLayoutParams<LinearLayout.LayoutParams> { this.width = 0; weight = 2.5f }
                    binding.portLayout.updateLayoutParams<LinearLayout.LayoutParams> {
                        this.width = 0
                        weight = 1f
                        marginStart = topMargin
                        topMargin = 0
                    }
                }
                WindowCompat.getInsetsController(window, panel.root).apply {
                    systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                    hide(WindowInsetsCompat.Type.systemBars())
                }
            }
            return popup
        }

        val dialog = MaterialAlertDialogBuilder(context)
            .setTitle(R.string.add_favorite)
            .setView(binding.root)
            .setNegativeButton(R.string.cancel, null)
            .setPositiveButton(R.string.add_server, null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener { submit() }
        }
        return dialog
    }

    companion object {
        const val QUICK_CONNECT = "quick-connect"
        const val TAG = "add-favorite"
        const val RESULT_KEY = "favorite-added"
    }
}
