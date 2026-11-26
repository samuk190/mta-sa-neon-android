package com.saonline.neonlauncher.ui

import android.os.Bundle
import android.text.format.Formatter
import android.view.View
import android.widget.TextView
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.widget.SearchView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.isVisible
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.lifecycle.ViewModelProvider
import androidx.recyclerview.widget.LinearLayoutManager
import com.saonline.neonlauncher.R
import com.saonline.neonlauncher.databinding.FragmentGameFilesBinding
import com.saonline.neonlauncher.utils.NativeAssetBridge.DownloadState
import com.saonline.neonlauncher.utils.NativeAssetBridge.SyncFailure

class GameFilesFragment : Fragment(R.layout.fragment_game_files) {
    private var binding: FragmentGameFilesBinding? = null
    private val model by lazy { ViewModelProvider(requireActivity())[GameFilesModel::class.java] }
    private val adapter = GameFileList(::openFolder)
    private var rows = emptyList<GameFileRow>()
    private var folder = ""
    private var query = ""
    private var summary = ""
    private var picking = false
    private val back = object : OnBackPressedCallback(false) {
        override fun handleOnBackPressed() = openFolder(folder.substringBeforeLast('/', ""))
    }
    private val picker = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        picking = false
        binding?.transfer?.isEnabled = !model.isTransferring
        if (uri != null) model.transfer(uri)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        val b = FragmentGameFilesBinding.bind(view)
        binding = b
        folder = savedInstanceState?.getString("folder").orEmpty()
        query = savedInstanceState?.getString("query").orEmpty()
        picking = savedInstanceState?.getBoolean("picking") == true
        requireActivity().onBackPressedDispatcher.addCallback(viewLifecycleOwner, back)
        val gutter = resources.getDimensionPixelSize(R.dimen.screen_padding)
        ViewCompat.setOnApplyWindowInsetsListener(view) { content, insets ->
            val cutout = insets.getInsets(WindowInsetsCompat.Type.displayCutout())
            content.updatePadding(left = maxOf(gutter, cutout.left), right = maxOf(gutter, cutout.right))
            insets
        }
        b.files.layoutManager = LinearLayoutManager(requireContext())
        b.files.adapter = adapter
        b.files.itemAnimator = null
        b.transfer.setOnClickListener { requestTransfer() }
        b.refresh.setOnClickListener { model.refresh() }
        b.folder.setOnClickListener { back.handleOnBackPressed() }
        b.search.findViewById<TextView>(androidx.appcompat.R.id.search_src_text).apply {
            setTextAppearance(R.style.BrowserText_Small)
            setHintTextColor(requireContext().getColor(R.color.browser_muted))
        }
        b.search.setQuery(query, false)
        b.search.setOnQueryTextListener(object : SearchView.OnQueryTextListener {
            override fun onQueryTextChange(text: String): Boolean {
                query = text.trim()
                renderRows()
                return true
            }
            override fun onQueryTextSubmit(text: String): Boolean {
                b.search.clearFocus()
                return true
            }
        })
        model.inventoryState.observe(viewLifecycleOwner) { inventory ->
            rows = if (inventory is GameFilesModel.Inventory.Ready) gameFileRows(inventory.snapshot.files) else emptyList()
            summary = when (inventory) {
                GameFilesModel.Inventory.Checking -> getString(R.string.files_checking)
                GameFilesModel.Inventory.Unavailable -> getString(R.string.files_unavailable)
                is GameFilesModel.Inventory.Ready -> {
                    if (folder.isNotEmpty() && rows.none { it.folder && it.path == folder }) folder = ""
                    val files = inventory.snapshot.files
                    val missing = files.filterNot { it.verified }
                    val counts = if (missing.isEmpty()) resources.getQuantityString(R.plurals.assets_verified, files.size, files.size)
                    else getString(R.string.files_summary, resources.getQuantityString(R.plurals.files_missing, missing.size, missing.size),
                        Formatter.formatShortFileSize(requireContext(), missing.sumOf { it.size }))
                    if (inventory.snapshot.localReference) getString(R.string.files_local_summary, counts) else counts
                }
            }
            renderRows()
            renderProgress()
        }
        model.transferState.observe(viewLifecycleOwner) { renderProgress() }
        renderRows()
        renderProgress()
    }

    override fun onResume() {
        super.onResume()
        if (model.inventoryState.value == null) model.refresh()
    }

    internal fun requestTransfer() {
        if (picking || model.isTransferring) return
        if (model.usesCdn) model.transfer() else {
            picking = true
            binding?.transfer?.isEnabled = false
            picker.launch(null)
        }
    }

    private fun openFolder(path: String) {
        folder = path
        binding?.search?.apply {
            setQuery("", false)
            clearFocus()
        }
        query = ""
        renderRows()
        binding?.files?.scrollToPosition(0)
    }

    private fun renderRows() {
        val b = binding ?: return
        val prefix = if (folder.isEmpty()) "" else "$folder/"
        val visible = rows.mapNotNull { row ->
            if (!row.path.startsWith(prefix)) return@mapNotNull null
            val relative = row.path.removePrefix(prefix)
            val matches = if (query.isEmpty()) '/' !in relative else relative.contains(query, ignoreCase = true)
            if (matches) row.copy(label = relative) else null
        }
        adapter.submitList(visible)
        val ready = model.inventoryState.value is GameFilesModel.Inventory.Ready
        b.itemCount.isVisible = ready
        b.itemCount.text = resources.getQuantityString(R.plurals.files_items, visible.size, visible.size)
        b.empty.isVisible = ready && visible.isEmpty()
        b.folder.text = if (folder.isEmpty()) "/" else "‹  /$folder"
        b.folder.isEnabled = folder.isNotEmpty()
        b.folder.contentDescription = if (folder.isEmpty()) "/" else getString(R.string.files_parent) + " · /" + folder
        back.isEnabled = !isHidden && folder.isNotEmpty()
    }

    private fun renderProgress() {
        val b = binding ?: return
        val state = model.transferState.value
        val busy = model.isTransferring
        val checking = model.inventoryState.value == GameFilesModel.Inventory.Checking
        b.transfer.isEnabled = !picking && !busy
        b.refresh.isEnabled = !busy && !checking
        b.progress.isVisible = busy || checking
        b.summary.text = when (state) {
            DownloadState.FetchingManifest -> getString(R.string.assets_fetching_manifest)
            is DownloadState.Syncing -> getString(R.string.assets_syncing, state.current, state.total)
            is DownloadState.Verifying -> getString(R.string.assets_verifying, state.current, state.total)
            else -> summary
        }
        val percent = when (state) {
            is DownloadState.Syncing -> state.percent
            is DownloadState.Verifying -> state.current * 100 / state.total.coerceAtLeast(1)
            else -> null
        }
        b.progress.isIndeterminate = percent == null
        if (percent != null) b.progress.progress = percent
        b.transferError.isVisible = state is DownloadState.Error
        if (state is DownloadState.Error) {
            b.transferError.text = when (state.reason) {
                SyncFailure.ALREADY_RUNNING -> getString(R.string.assets_already_syncing)
                SyncFailure.LOCAL_FILES_MISSING, SyncFailure.INVALID_LOCAL_FOLDER -> getString(R.string.files_bad_folder)
                SyncFailure.INSUFFICIENT_SPACE -> getString(R.string.assets_insufficient_space)
                SyncFailure.STORAGE_UNAVAILABLE -> getString(R.string.assets_storage_unavailable)
                SyncFailure.INVALID_MANIFEST -> getString(R.string.assets_invalid_manifest)
                SyncFailure.FILE_INVALID -> getString(R.string.assets_file_invalid, state.detail)
                SyncFailure.HTTP_ERROR -> getString(R.string.assets_http_error, state.detail)
                SyncFailure.IO_ERROR -> getString(R.string.assets_sync_failed)
            }
        }
    }

    override fun onHiddenChanged(hidden: Boolean) {
        super.onHiddenChanged(hidden)
        back.isEnabled = !hidden && folder.isNotEmpty()
    }

    override fun onSaveInstanceState(state: Bundle) {
        super.onSaveInstanceState(state)
        state.putString("folder", folder)
        state.putString("query", query)
        state.putBoolean("picking", picking)
    }

    override fun onDestroyView() {
        binding?.files?.adapter = null
        binding = null
        super.onDestroyView()
    }
}
