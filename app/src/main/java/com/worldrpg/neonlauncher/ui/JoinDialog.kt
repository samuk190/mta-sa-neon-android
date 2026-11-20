package com.worldrpg.neonlauncher.ui

import android.content.res.ColorStateList
import android.view.LayoutInflater
import android.view.View
import android.view.inputmethod.EditorInfo
import androidx.core.content.ContextCompat
import androidx.core.widget.doAfterTextChanged
import androidx.fragment.app.Fragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.worldrpg.neonlauncher.BuildConfig
import com.worldrpg.neonlauncher.R
import com.worldrpg.neonlauncher.databinding.DialogJoinBinding
import com.worldrpg.neonlauncher.network.ServerInfo
import com.worldrpg.neonlauncher.utils.PreferencesManager

/** Join step: shows the server, asks for the nickname, then plays. */
object JoinDialog {

    fun show(fragment: Fragment, server: ServerInfo) {
        val ctx = fragment.requireContext()
        val prefs = PreferencesManager(ctx)
        val b = DialogJoinBinding.inflate(LayoutInflater.from(ctx))
        val modes = ctx.resources.getStringArray(R.array.connection_modes)
        var mode = com.worldrpg.neonlauncher.network.JoinProtocolSettings.read(ctx, server.ip, server.port)
        b.connectionMode.setSimpleItems(modes)
        b.connectionMode.setText(modes[mode], false)
        b.connectionMode.setOnItemClickListener { _, _, position, _ -> mode = position }

        ServerLogo.bind(b.joinLogo, server.name, server.logoUrl)
        b.joinName.text = server.name
        b.joinMode.text = ServerListAdapter.modeLine(server).ifEmpty { server.getKey() }
        val online = server.status == ServerInfo.Status.ONLINE
        b.joinPlayers.visibility = if (online) View.VISIBLE else View.GONE
        b.joinPing.visibility = if (online) View.VISIBLE else View.GONE
        b.joinVersion.visibility = if (server.version.isNotEmpty()) View.VISIBLE else View.GONE
        if (online) {
            b.joinPlayers.text = ctx.getString(R.string.players_count, server.players, server.maxPlayers)
            b.joinPing.text = ctx.getString(R.string.ping_ms, server.ping)
            b.joinPing.chipIconTint = ColorStateList.valueOf(
                ContextCompat.getColor(ctx, ServerListAdapter.pingColor(server.ping))
            )
        }
        b.joinVersion.text = "MTA ${server.version}"

        b.btnPlay.text = BuildConfig.LAUNCHER_PLAY_BUTTON_TEXT
        b.editNickname.setText(prefs.username)
        b.editNickname.setSelection(b.editNickname.text?.length ?: 0)
        b.editNickname.doAfterTextChanged { b.nicknameLayout.error = null }

        val dialog = MaterialAlertDialogBuilder(ctx)
            .setView(b.root)
            .create()

        fun play() {
            val nick = b.editNickname.text?.toString()?.trim().orEmpty()
            val error = when (NicknameRules.check(nick)) {
                NicknameRules.Result.OK -> null
                NicknameRules.Result.EMPTY -> R.string.nickname_empty
                NicknameRules.Result.INVALID -> R.string.nickname_invalid
                NicknameRules.Result.RESERVED -> R.string.nickname_reserved
            }
            if (error != null) {
                b.nicknameLayout.error = ctx.getString(error)
                return
            }
            prefs.username = nick
            if (!com.worldrpg.neonlauncher.network.JoinProtocolSettings.save(ctx, server.ip, server.port, mode)) {
                android.widget.Toast.makeText(ctx, "Unable to save connection mode.", android.widget.Toast.LENGTH_LONG).show()
                return
            }
            // Close the keyboard first: an open IME follows into the game window.
            b.editNickname.clearFocus()
            ctx.getSystemService(android.view.inputmethod.InputMethodManager::class.java)
                ?.hideSoftInputFromWindow(b.editNickname.windowToken, 0)
            dialog.dismiss()
            (fragment.activity as? MainActivity)?.launchGame(server)
        }

        b.btnPlay.setOnClickListener { play() }
        b.btnCancel.setOnClickListener { dialog.dismiss() }
        b.editNickname.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_GO) { play(); true } else false
        }

        dialog.show()
        // Wide enough for the two columns, never wider than the screen.
        val metrics = ctx.resources.displayMetrics
        val maxWidth = (640 * metrics.density).toInt()
        dialog.window?.setLayout(minOf(maxWidth, (metrics.widthPixels * 0.9f).toInt()), android.view.ViewGroup.LayoutParams.WRAP_CONTENT)
    }
}
