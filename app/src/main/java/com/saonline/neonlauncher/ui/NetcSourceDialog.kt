package com.saonline.neonlauncher.ui

import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.network.NetcModuleStore
import com.saonline.neonlauncher.network.PcNetworkSettings.NetcBuild
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.IOException

/**
 * Makes sure the PC netc.dll for [build] is on the phone. Asked only when the
 * player ticks "Use PC Network — Compatibility": a bundled copy installs
 * silently; otherwise the player chooses the file from a PC install or
 * downloads it from MTA. [done] runs once, with true when the
 * module is ready.
 */
object NetcSourceDialog {

    fun ensure(fragment: Fragment, build: NetcBuild, done: (Boolean) -> Unit) {
        val ctx = fragment.requireContext().applicationContext
        var finished = false
        fun finish(ok: Boolean) {
            if (finished) return
            finished = true
            done(ok)
        }
        val scope = fragment.viewLifecycleOwner.lifecycleScope
        scope.launch {
            if (withContext(Dispatchers.IO) { NetcModuleStore.resolve(ctx, build) } != null) {
                finish(true)
                return@launch
            }
            if (!fragment.isAdded) return@launch finish(false)
            val activity = fragment.requireActivity()

            // Installs from a background job and reports the result.
            fun install(job: () -> Unit, failureText: Int) {
                scope.launch {
                    val error = withContext(Dispatchers.IO) {
                        try {
                            job()
                            null
                        } catch (e: IOException) {
                            e.message ?: e.javaClass.simpleName
                        }
                    }
                    if (error == null) Toast.makeText(ctx, R.string.pc_network_installed, Toast.LENGTH_SHORT).show()
                    else Toast.makeText(ctx, ctx.getString(failureText, error), Toast.LENGTH_LONG).show()
                    finish(error == null)
                }
            }

            // Closing the dialog without a choice means "not ready"; a choice
            // that started a pick or download reports on its own.
            var choiceMade = false
            val dialog = MaterialAlertDialogBuilder(activity)
                .setTitle(R.string.pc_network_source_title)
                .setMessage(R.string.pc_network_source_message)
                .setNegativeButton(R.string.cancel) { _, _ -> finish(false) }
                .setOnDismissListener { if (!choiceMade) finish(false) }
                .setPositiveButton(R.string.pc_network_choose_file) { _, _ ->
                    choiceMade = true
                    val main = activity as? MainActivity ?: return@setPositiveButton finish(false)
                    main.pickNetcFile(build) { uri ->
                        if (uri == null) finish(false)
                        else install({ NetcModuleStore.importFrom(ctx, build, uri) }, R.string.pc_network_import_failed)
                    }
                }
            if (NetcModuleStore.canDownload(build)) {
                dialog.setNeutralButton(R.string.pc_network_download) { _, _ ->
                    choiceMade = true
                    Toast.makeText(ctx, R.string.pc_network_downloading, Toast.LENGTH_SHORT).show()
                    install({ NetcModuleStore.download(ctx, build) }, R.string.pc_network_download_failed)
                }
            }
            dialog.show()
        }
    }
}
