package com.saonline.neonlauncher.ui

import android.app.Activity
import android.app.Application
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.SystemClock
import android.util.Log
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.network.NeonIdentityApi
import com.saonline.neonlauncher.network.NeonIdentitySession
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import java.io.IOException
import java.util.function.Consumer

/** Shared by launcher and engine. Never keeps a game handshake alive during browser login. */
class NeonIdentityUi(private val activity: Activity) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val session = NeonIdentitySession(activity)
    private val api = NeonIdentityApi()
    private var signInJob: Job? = null
    private var dialog: androidx.appcompat.app.AlertDialog? = null

    fun isSignedIn(): Boolean = session.load() != null
    fun signOut(success: Runnable, failure: Consumer<String>) {
        scope.launch {
            try {
                withContext(Dispatchers.IO) { session.clear() }
                success.run()
            } catch (cancel: CancellationException) {
                throw cancel
            } catch (_: Exception) {
                failure.accept(activity.getString(R.string.neon_account_sign_out_failed))
            }
        }
    }

    fun signIn(success: Runnable, failure: Consumer<String>) {
        if (signInJob?.isActive == true) return
        signInJob = scope.launch {
            val waiting = MaterialAlertDialogBuilder(activity).setTitle(R.string.neon_account_title)
                .setMessage(R.string.neon_account_waiting_for_browser)
                .setNegativeButton(R.string.cancel) { _, _ ->
                    signInJob?.cancel()
                    failure.accept(activity.getString(R.string.neon_account_sign_in_canceled))
                }.setCancelable(false).show()
            dialog = waiting
            try {
                val flow = api.start()
                val token = awaitBrowserSession(flow)
                withContext(Dispatchers.IO) {
                    currentCoroutineContext().ensureActive()
                    session.save(token)
                }
            } catch (cancel: CancellationException) {
                throw cancel
            } catch (error: Exception) {
                Log.w("NeonIdentity", "Browser sign-in failed (${error.javaClass.simpleName})")
                failure.accept(activity.getString(R.string.neon_account_sign_in_failed))
                return@launch
            } finally {
                waiting.dismiss()
                if (dialog === waiting) dialog = null
            }
            success.run()
        }
    }

    private suspend fun awaitBrowserSession(flow: NeonIdentityApi.Flow): String {
        val resumed = MutableStateFlow(true)
        var pauses = 0
        val lifecycle = object : Application.ActivityLifecycleCallbacks {
            override fun onActivityResumed(owner: Activity) {
                if (owner === activity) resumed.value = true
            }

            override fun onActivityPaused(owner: Activity) {
                if (owner !== activity) return
                pauses++
                resumed.value = false
            }

            override fun onActivityCreated(owner: Activity, state: Bundle?) = Unit
            override fun onActivityStarted(owner: Activity) = Unit
            override fun onActivityStopped(owner: Activity) = Unit
            override fun onActivitySaveInstanceState(owner: Activity, state: Bundle) = Unit
            override fun onActivityDestroyed(owner: Activity) = Unit
        }
        activity.application.registerActivityLifecycleCallbacks(lifecycle)
        try {
            activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(flow.authorizationUrl))
                .addCategory(Intent.CATEGORY_BROWSABLE))
            val deadline = SystemClock.elapsedRealtime() + 300_000
            while (true) {
                delay(2_500)
                val remaining = deadline - SystemClock.elapsedRealtime()
                if (remaining <= 0 || withTimeoutOrNull(remaining) { resumed.first { it } } == null ||
                    SystemClock.elapsedRealtime() >= deadline) {
                    throw IOException("SA:O sign-in expired. Please try again.")
                }
                // Android can suspend the launcher's network access while the browser is on top.
                val pausesBeforePoll = pauses
                try {
                    api.poll(flow)?.let { return it }
                } catch (error: IOException) {
                    if (pausesBeforePoll == pauses) throw error
                }
            }
        } finally {
            activity.application.unregisterActivityLifecycleCallbacks(lifecycle)
        }
    }

    fun authorize(serverId: String, endpoint: String, required: Boolean, success: Consumer<String>, failure: Consumer<String>) {
        scope.launch {
            try {
                val token = withContext(Dispatchers.IO) { session.load() }
                if (token == null) {
                    if (required) failure.accept(activity.getString(R.string.neon_account_sign_in_required))
                    else success.accept("")
                    return@launch
                }
                val ticket = try {
                    api.ticket(token, serverId, endpoint)
                } catch (_: NeonIdentityApi.SessionExpired) {
                    // A response for the old token can arrive after the launcher saved a new one.
                    withContext(Dispatchers.IO) { session.clearIfCurrent(token) }
                    failure.accept(activity.getString(R.string.neon_account_session_expired))
                    return@launch
                }
                success.accept(ticket)
            } catch (cancel: CancellationException) {
                throw cancel
            } catch (_: Exception) {
                failure.accept(activity.getString(R.string.neon_account_authorization_failed))
            }
        }
    }

    fun close() {
        scope.cancel()
        dialog?.dismiss()
        dialog = null
    }
}
