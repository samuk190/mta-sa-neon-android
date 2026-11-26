package com.saonline.neonlauncher.ui

import android.app.Application
import android.net.Uri
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import com.saonline.neonlauncher.utils.GameFileInventory
import com.saonline.neonlauncher.utils.NativeAssetBridge
import com.saonline.neonlauncher.utils.NativeAssetBridge.DownloadState
import com.saonline.neonlauncher.utils.readGameAssetTree
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

class GameFilesModel(app: Application) : AndroidViewModel(app) {
    private val bridge = NativeAssetBridge(app)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var scan: Job? = null
    private val resolver = app.contentResolver
    val usesCdn = bridge.usesCdn()

    sealed interface Inventory {
        data object Checking : Inventory
        data object Unavailable : Inventory
        data class Ready(val snapshot: GameFileInventory) : Inventory
    }

    private val inventory = MutableLiveData<Inventory>()
    val inventoryState: LiveData<Inventory> = inventory
    private val transfer = MutableLiveData<DownloadState>()
    val transferState: LiveData<DownloadState> = transfer
    val isTransferring get() = when (transfer.value) {
        DownloadState.FetchingManifest, is DownloadState.Syncing, is DownloadState.Verifying -> true
        else -> false
    }

    fun refresh() {
        if (isTransferring || scan?.isActive == true) return
        inventory.value = Inventory.Checking
        scan = scope.launch {
            inventory.value = try {
                Inventory.Ready(bridge.gameFileInventory())
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                Log.w("GameFiles", "Could not verify the game file list", error)
                Inventory.Unavailable
            }
        }
    }

    fun transfer(folder: Uri? = null) {
        if (isTransferring || (!usesCdn && folder == null)) return
        scan?.cancel()
        transfer.value = DownloadState.FetchingManifest
        scope.launch {
            try {
                val flow = if (folder == null) bridge.syncAssets() else bridge.syncAssets { readGameAssetTree(resolver, folder) }
                flow.collect { transfer.value = it }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                Log.w("GameFiles", "Could not transfer the game files", error)
                transfer.value = DownloadState.Error(NativeAssetBridge.SyncFailure.IO_ERROR)
            }
            refresh()
        }
    }

    override fun onCleared() {
        scope.cancel()
        super.onCleared()
    }
}
