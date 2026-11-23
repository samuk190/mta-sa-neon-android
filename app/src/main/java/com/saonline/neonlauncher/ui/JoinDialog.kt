package com.saonline.neonlauncher.ui

import android.app.Dialog
import android.content.res.ColorStateList
import android.graphics.drawable.Drawable
import android.view.LayoutInflater
import android.view.View
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.core.view.updatePadding
import androidx.core.widget.doAfterTextChanged
import androidx.fragment.app.Fragment
import com.saonline.neonlauncher.BuildConfig
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.databinding.DialogJoinBinding
import com.saonline.neonlauncher.network.PcNetworkSettings
import com.saonline.neonlauncher.network.ServerInfo
import com.saonline.neonlauncher.utils.PreferencesManager

/** Join step: shows the server, asks for the nickname, then plays. */
object JoinDialog {

    fun show(fragment: Fragment, server: ServerInfo, wallpaper: Drawable?) {
        val ctx = fragment.requireContext()
        val prefs = PreferencesManager(ctx)
        val dialog = Dialog(ctx, R.style.Theme_WorldRPGNeon_Join)
        val b = DialogJoinBinding.inflate(LayoutInflater.from(dialog.context))
        b.wallpaper.setImageDrawable(wallpaper?.constantState?.newDrawable(ctx.resources))
        dialog.setContentView(b.root)
        val window = requireNotNull(dialog.window)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE or
            WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN)
        ViewCompat.setOnApplyWindowInsetsListener(b.joinContent) { view, insets ->
            val safe = insets.getInsets(WindowInsetsCompat.Type.displayCutout() or
                WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.ime())
            view.updatePadding(left = safe.left, top = safe.top, right = safe.right, bottom = safe.bottom)
            insets
        }
        val modes = ctx.resources.getStringArray(R.array.connection_modes)
        var mode = com.saonline.neonlauncher.network.JoinProtocolSettings.read(ctx, server.ip, server.port)
        b.connectionMode.setSimpleItems(modes)
        b.connectionMode.setText(modes[mode], false)
        b.connectionMode.setOnItemClickListener { _, _, position, _ -> mode = position }
        // Server version: Automatic detects it (and remembers what worked).
        val releases = ctx.resources.getStringArray(R.array.server_releases)
        var release = com.saonline.neonlauncher.network.ServerReleaseSettings.read(ctx, server.ip, server.port)
        b.serverRelease.setSimpleItems(releases)
        b.serverRelease.setText(releases[release], false)
        b.serverRelease.setOnItemClickListener { _, _, position, _ -> release = position }

        // "Use PC Network — Compatibility". Ticking it is the only time the
        // player is asked for netc.dll (NetcSourceDialog), and only if needed.
        val netcBuild = PcNetworkSettings.buildFor(server)
        fun updatePcNetworkHint() {
            // Server version only matters through PC Network; off it the
            // join is the stock one.
            val shown = if (b.pcNetwork.isChecked) View.VISIBLE else View.GONE
            b.pcNetworkHint.visibility = shown
            b.serverReleaseLayout.visibility = shown
        }
        fun askForModule() {
            b.pcNetwork.isEnabled = false  // until the module question is settled
            NetcSourceDialog.ensure(fragment, netcBuild) { ready ->
                b.pcNetwork.isEnabled = true
                if (!ready) b.pcNetwork.isChecked = false
            }
        }
        b.pcNetwork.isChecked = PcNetworkSettings.isEnabled(ctx, server.ip, server.port)
        updatePcNetworkHint()
        b.pcNetwork.setOnCheckedChangeListener { _, checked ->
            updatePcNetworkHint()
            if (checked) askForModule()
        }
        // Remembered as on, but this server's build is not on the phone (the
        // server changed version, or the file was removed): ask now, not at Play.
        if (b.pcNetwork.isChecked && com.saonline.neonlauncher.network.NetcModuleStore.installed(ctx, netcBuild) == null &&
            !com.saonline.neonlauncher.network.NetcModuleStore.isBundled(netcBuild)) {
            askForModule()
        }

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
        b.joinVersion.text = ctx.getString(R.string.mta_version, server.version)

        val customPlayLabel = BuildConfig.LAUNCHER_PLAY_BUTTON_TEXT
        if (customPlayLabel.isNotEmpty()) b.btnPlay.text = customPlayLabel
        b.editNickname.setText(prefs.username)
        b.editNickname.setSelection(b.editNickname.text?.length ?: 0)
        b.editNickname.doAfterTextChanged { b.nicknameLayout.error = null }

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
                b.editNickname.requestFocus()
                return
            }
            prefs.username = nick
            if (!b.pcNetwork.isEnabled) {  // still asking for netc.dll
                android.widget.Toast.makeText(ctx, R.string.pc_network_source_title, android.widget.Toast.LENGTH_SHORT).show()
                return
            }
            if (!com.saonline.neonlauncher.network.JoinProtocolSettings.save(ctx, server.ip, server.port, mode) ||
                !PcNetworkSettings.setEnabled(ctx, server.ip, server.port, b.pcNetwork.isChecked)) {
                android.widget.Toast.makeText(ctx, R.string.connection_mode_save_failed, android.widget.Toast.LENGTH_LONG).show()
                return
            }
            // Saved last, so a failed save above leaves the release unchanged too.
            if (!com.saonline.neonlauncher.network.ServerReleaseSettings.save(ctx, server.ip, server.port, release)) {
                android.widget.Toast.makeText(ctx, R.string.server_release_save_failed, android.widget.Toast.LENGTH_LONG).show()
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
        WindowCompat.getInsetsController(window, b.root).apply {
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
    }
}
