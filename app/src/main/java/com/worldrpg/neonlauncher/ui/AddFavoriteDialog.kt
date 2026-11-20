package com.worldrpg.neonlauncher.ui

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import androidx.core.os.bundleOf
import androidx.core.widget.doAfterTextChanged
import androidx.fragment.app.DialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.worldrpg.neonlauncher.R
import com.worldrpg.neonlauncher.databinding.DialogAddFavoriteBinding
import com.worldrpg.neonlauncher.network.ServerAddress
import com.worldrpg.neonlauncher.utils.PreferencesManager

class AddFavoriteDialog : DialogFragment() {
    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val context = requireContext()
        val binding = DialogAddFavoriteBinding.inflate(LayoutInflater.from(context))
        val prefs = PreferencesManager(context)
        binding.editHost.doAfterTextChanged { binding.hostLayout.error = null }
        binding.editPort.doAfterTextChanged { binding.portLayout.error = null }
        val dialog = MaterialAlertDialogBuilder(context)
            .setTitle(R.string.add_favorite)
            .setView(binding.root)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.add_server, null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(androidx.appcompat.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val host = ServerAddress.normalizeHost(binding.editHost.text?.toString().orEmpty())
                val port = ServerAddress.parsePort(binding.editPort.text?.toString().orEmpty())
                binding.hostLayout.error = if (host == null) getString(R.string.server_host_invalid) else null
                binding.portLayout.error = if (port == null) getString(R.string.server_port_invalid) else null
                if (host == null || port == null) return@setOnClickListener
                val address = ServerAddress(host, port)
                if (prefs.isFavorite(address.key())) {
                    binding.hostLayout.error = getString(R.string.server_already_favorite)
                    return@setOnClickListener
                }
                prefs.addFavorite(address.key())
                parentFragmentManager.setFragmentResult(RESULT_KEY, bundleOf())
                context.getSystemService(InputMethodManager::class.java)
                    ?.hideSoftInputFromWindow(binding.editPort.windowToken, 0)
                dismiss()
            }
            binding.editPort.setOnEditorActionListener { _, action, _ ->
                if (action == EditorInfo.IME_ACTION_DONE) {
                    dialog.getButton(androidx.appcompat.app.AlertDialog.BUTTON_POSITIVE).performClick()
                    true
                } else false
            }
        }
        return dialog
    }

    companion object {
        const val TAG = "add-favorite"
        const val RESULT_KEY = "favorite-added"
    }
}
