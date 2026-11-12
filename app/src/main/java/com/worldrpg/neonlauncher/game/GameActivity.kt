package com.worldrpg.neonlauncher.game

import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.MotionEvent
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.worldrpg.neonlauncher.BuildConfig
import com.worldrpg.neonlauncher.databinding.ActivityGameBinding
import com.worldrpg.neonlauncher.network.ConnectionManager
import com.worldrpg.neonlauncher.utils.NativeAssetBridge
import com.worldrpg.neonlauncher.utils.PreferencesManager
import kotlinx.coroutines.launch
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/** Hosts the native client surface and forwards its lifecycle and input to C++. */
class GameActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_SERVER_IP = "server_ip"
        const val EXTRA_SERVER_PORT = "server_port"
        const val EXTRA_SERVER_NAME = "server_name"
    }

    private lateinit var binding: ActivityGameBinding
    private lateinit var connectionManager: ConnectionManager
    private lateinit var prefsManager: PreferencesManager
    private lateinit var adminPermissionGate: AdminPermissionGate
    private lateinit var hudLayoutPersistence: HUDLayoutPersistence
    private lateinit var adminCommandHandler: AdminCommandHandler

    private var serverIp: String = ""
    private var serverPort: Int = BuildConfig.DEFAULT_SERVER_PORT
    private var nativeInitialized = false // GL thread only
    @Volatile private var closing = false
    private var surfaceConfigured = false
    private val activePointers = mutableSetOf<Int>()
    private val chatLines = ArrayDeque<String>()

    // --- JNI Bridge Methods ---

    /**
     * Initializes the client bridge on the GL thread.
     */
    private external fun nativeInit(): Boolean

    /**
     * Starts a native transport attempt. A true return does not confirm a Neon join.
     * @param ip Server IP address (ASCII)
     * @param port Server UDP port
     * @param nickname Player nickname (ASCII-only)
     */
    private external fun nativeConnect(ip: String, port: Int, nickname: String): Boolean

    /** Coordinates are pixels in the client surface. */
    private external fun nativeOnTouch(x: Float, y: Float, action: Int, pointerId: Int)

    private external fun nativeSurfaceChanged(width: Int, height: Int)

    /**
     * Loads libGTASA.so and installs the verified offset table on the GL thread.
     * True means the engine library is resident and hooks are ready; it does
     * not mean the game world is running yet (full boot is P1A.4).
     */
    private external fun nativeBootEngine(): Boolean

    /** Gates engine/transport frame work while the Activity is paused. */
    private external fun nativePause()

    private external fun nativeResume()

    /**
     * Process incoming network packets. Must be called every frame.
     */
    private external fun nativePulse()

    /**
     * Releases the client state owned by this Activity.
     */
    private external fun nativeDestroy()

    // --- JNI HUD Admin Commands (Phase 6/7) ---

    /**
     * Set HUD element parameters for runtime admin calibration.
     * Maps to /admin module hud <element> <x> <y> <width> <height> [visible]
     */
    external fun nativeSetHUDElementParams(
        elementName: String, x: Float, y: Float,
        width: Float, height: Float, visible: Boolean
    )

    /**
     * Get current HUD element parameters as formatted ASCII string.
     */
    external fun nativeGetHUDElementParams(elementName: String): String

    /**
     * List all configurable HUD element names with their current parameters.
     */
    external fun nativeListHUDElements(): String

    /**
     * Add a chat message to the native HUD overlay.
     * Message is ASCII-normalized on the native side.
     */
    external fun nativeAddChatMessage(message: String, color: Int)

    /**
     * Set the local asset storage path for native AssetManager initialization.
     * Called after NativeAssetBridge completes a successful sync.
     */
    private external fun nativeSetAssetPath(assetPath: String): Boolean

    /**
     * Called from native C++ via JNI after Neon join completion or failure.
     * Bridges the session result to the Kotlin ConnectionManager.
     * @param connected true only after the Neon join completes
     * @param message Human-readable status message (ASCII)
     */
    /**
     * Called from native packet handling. kind 0 is a chat line, kind 1 is a
     * status line such as a spawn position or a resource name.
     */
    @Suppress("unused")
    fun onNativeGameLine(kind: Int, text: String?) {
        val line = text?.takeIf { it.isNotBlank() } ?: return
        runOnUiThread {
            if (closing) return@runOnUiThread
            if (kind == 0) {
                chatLines.addLast(line)
                while (chatLines.size > 8) chatLines.removeFirst()
                binding.tvChatLog.text = chatLines.joinToString("\n")
                binding.tvChatLog.visibility = View.VISIBLE
            } else {
                binding.tvConnectionStatus.text = line
                binding.tvConnectionStatus.visibility = View.VISIBLE
            }
        }
    }

    @Suppress("unused") // Called from JNI
    fun onNativeConnectionStateChange(connected: Boolean, message: String?) {
        runOnUiThread {
            if (closing) return@runOnUiThread
            android.util.Log.i("GameActivity", "NativeConnectionState: connected=$connected msg=$message")
            if (connected) {
                connectionManager.setConnected(serverIp, serverPort)
            } else {
                val errorMsg = message ?: "Desconectado"
                val genericClose = errorMsg.equals("Disconnected by server", ignoreCase = true) ||
                    errorMsg.equals("Connection lost", ignoreCase = true)
                val current = connectionManager.connectionState.value
                if (genericClose && current is ConnectionManager.ConnectionState.Error) {
                    return@runOnUiThread
                }
                if (genericClose) {
                    connectionManager.setDisconnected()
                } else {
                    connectionManager.setError(errorMsg)
                }
            }
        }
    }

    // --- Lifecycle ---

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Extract server parameters from intent
        serverIp = intent.getStringExtra(EXTRA_SERVER_IP)?.takeIf { it.isNotBlank() } ?: BuildConfig.DEFAULT_SERVER_IP
        serverPort = intent.getIntExtra(EXTRA_SERVER_PORT, BuildConfig.DEFAULT_SERVER_PORT)

        val assetPath = NativeAssetBridge(this).getReadyStoragePath()
        if (assetPath == null) {
            Toast.makeText(this, "Baixe os arquivos do jogo em Configuracoes.", Toast.LENGTH_LONG).show()
            finish()
            return
        }

        try {
// libc++ is linked statically into WorldRPGNeon (c++_static); no shared runtime to preload.
            System.loadLibrary("WorldRPGNeon")
        } catch (error: UnsatisfiedLinkError) {
            android.util.Log.e("GameActivity", "Falha ao carregar WorldRPGNeon", error)
            Toast.makeText(this, "Falha ao carregar o cliente nativo.", Toast.LENGTH_LONG).show()
            finish()
            return
        }

        prefsManager = PreferencesManager(this)
        connectionManager = ConnectionManager()
        adminPermissionGate = AdminPermissionGate()
        hudLayoutPersistence = HUDLayoutPersistence(this)
        adminCommandHandler = AdminCommandHandler(this)

        binding = ActivityGameBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupFullscreenImmersive()

        setupGLSurfaceView(assetPath)
        surfaceConfigured = true
        setupOverlayHud()

        // Prevent screen from sleeping during gameplay
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    private fun setupFullscreenImmersive() {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            window.insetsController?.apply {
                hide(WindowInsets.Type.systemBars())
                systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                )
        }
    }

    private fun setupGLSurfaceView(assetPath: String) {
        binding.glSurfaceView.apply {
            setEGLContextClientVersion(2)
            setRenderer(object : GLSurfaceView.Renderer {
                override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                    if (closing || nativeInitialized) return

                    if (!nativeInit()) {
                        connectionManager.setError("Falha ao iniciar o cliente nativo.")
                        return
                    }
                    if (!nativeSetAssetPath(assetPath)) {
                        nativeDestroy()
                        connectionManager.setError("Os arquivos instalados nao puderam ser abertos.")
                        return
                    }
                    if (!nativeBootEngine()) {
                        nativeDestroy()
                        connectionManager.setError("Falha ao carregar a engine GTA (libGTASA).")
                        return
                    }
                    nativeInitialized = true

                    for ((name, layout) in hudLayoutPersistence.loadAllLayouts()) {
                        nativeSetHUDElementParams(
                            name, layout.x, layout.y,
                            layout.width, layout.height, layout.visible
                        )
                    }

                    connectionManager.setConnecting(serverIp, serverPort)
                    if (!nativeConnect(serverIp, serverPort, prefsManager.username)) {
                        connectionManager.setError("Falha ao iniciar a conexao.")
                    }
                }

                override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                    if (!closing && nativeInitialized) nativeSurfaceChanged(width, height)
                }

                override fun onDrawFrame(gl: GL10?) {
                    if (!closing && nativeInitialized) nativePulse()
                }
            })

            setOnTouchListener { _, event ->
                handleTouchEvent(event)
                true
            }
        }
    }

    private fun setupOverlayHud() {
        binding.btnDisconnect.setOnClickListener {
            finish()
        }

        // Observe connection state for HUD updates
        lifecycleScope.launch {
            connectionManager.connectionState.collect { state ->
                when (state) {
                    is ConnectionManager.ConnectionState.Connected -> {
                        binding.tvConnectionStatus.text = "Conectado. Aguardando dados do jogo."
                        binding.tvConnectionStatus.visibility = View.VISIBLE
                        binding.loadingIndicator.visibility = View.GONE
                        binding.btnDisconnect.visibility = View.VISIBLE
                    }
                    is ConnectionManager.ConnectionState.Connecting -> {
                        binding.tvConnectionStatus.text = "Conectando ao servidor..."
                        binding.tvConnectionStatus.visibility = View.VISIBLE
                        binding.loadingIndicator.visibility = View.VISIBLE
                        binding.btnDisconnect.visibility = View.VISIBLE
                    }
                    is ConnectionManager.ConnectionState.Error -> {
                        binding.tvConnectionStatus.text = "Erro: ${state.message}"
                        binding.tvConnectionStatus.visibility = View.VISIBLE
                        binding.loadingIndicator.visibility = View.GONE
                        binding.btnDisconnect.visibility = View.VISIBLE
                    }
                    is ConnectionManager.ConnectionState.Disconnected -> {
                        binding.tvConnectionStatus.text = "Desconectado"
                        binding.tvConnectionStatus.visibility = View.VISIBLE
                        binding.loadingIndicator.visibility = View.GONE
                        binding.btnDisconnect.visibility = View.VISIBLE
                    }
                }
            }
        }
    }

    private fun handleTouchEvent(event: MotionEvent) {
        if (closing) return

        fun forward(index: Int, action: Int) {
            val pointer = event.getPointerId(index)
            val x = event.getX(index)
            val y = event.getY(index)
            binding.glSurfaceView.queueEvent {
                if (!closing && nativeInitialized) nativeOnTouch(x, y, action, pointer)
            }
        }

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                activePointers.add(event.getPointerId(event.actionIndex))
                forward(event.actionIndex, MotionEvent.ACTION_DOWN)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                activePointers.remove(event.getPointerId(event.actionIndex))
                forward(event.actionIndex, MotionEvent.ACTION_UP)
            }
            MotionEvent.ACTION_MOVE -> {
                for (index in 0 until event.pointerCount) {
                    forward(index, MotionEvent.ACTION_MOVE)
                }
            }
            MotionEvent.ACTION_CANCEL -> cancelTouches()
        }
    }

    private fun cancelTouches() {
        val pointers = activePointers.toIntArray()
        activePointers.clear()
        if (pointers.isEmpty()) return
        binding.glSurfaceView.queueEvent {
            if (nativeInitialized) {
                for (pointer in pointers) nativeOnTouch(0f, 0f, MotionEvent.ACTION_CANCEL, pointer)
            }
        }
    }

    override fun onPause() {
        if (surfaceConfigured) {
            cancelTouches()
            // Queued before the render thread stops, so pause ordering is
            // serialized with any in-flight pulse/touch events.
            binding.glSurfaceView.queueEvent {
                if (nativeInitialized && !closing) nativePause()
            }
            binding.glSurfaceView.onPause()
        }
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        if (surfaceConfigured) {
            binding.glSurfaceView.onResume()
            binding.glSurfaceView.queueEvent {
                if (nativeInitialized && !closing) nativeResume()
            }
            setupFullscreenImmersive()
        }
    }

    override fun onDestroy() {
        closing = true
        if (surfaceConfigured) {
            // The view was paused before destruction. JNI still checks the owning
            // Activity and serializes disposal with any remaining queued event.
            nativeDestroy()
        }
        super.onDestroy()
    }
}
