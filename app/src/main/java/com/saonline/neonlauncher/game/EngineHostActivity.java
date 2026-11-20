package com.saonline.neonlauncher.game;

import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Bundle;
import android.util.Log;
import android.util.TypedValue;
import android.os.Build;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputConnectionWrapper;
import android.view.inputmethod.InputMethodManager;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;
import com.saonline.neonlauncher.BuildConfig;
import com.saonline.neonlauncher.network.JoinProtocolSettings;
import com.saonline.neonlauncher.utils.PreferencesManager;
import com.rockstargames.gtasa.GTASA;
import com.saonline.engine.EngineAssetLoader;
import com.saonline.engine.EngineCaseAliases;
import com.saonline.neonlauncher.utils.NativeAssetBridge;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;

/** Debug checkpoint for the native GTA Activity, Surface and EGL lifecycle. */
public final class EngineHostActivity extends GTASA {
    public static final String EXTRA_BOOT_ERROR = "engine_boot_error";
    public static final String EXTRA_GAME_ASSETS_REQUIRED = "game_assets_required";
    private static final String TAG = "NeonEngineHost";
    private static final int COMPACT_IME = EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN;
    private static final int PANEL_ELEVATION_DP = 24;
    private String gameDirectory;
    private String bootError;
    private View mtaPause;
    private View downloadPanel;
    private ProgressBar downloadBar;
    private TextView downloadLabel;
    private MtaChatView chatLog;
    private MtaDxView dxOverlay;
    private MtaTransferBox transferBox;
    private ConnectionLoadingView loadingView;
    private PlayerMapView playerMap;
    private final Handler mapHandler = new Handler(Looper.getMainLooper());
    private final Runnable mapRefresh = new Runnable() {
        @Override
        public void run() {
            if (playerMap != null && playerMap.getTag() instanceof View
                    && ((View) playerMap.getTag()).getVisibility() == View.VISIBLE) {
                playerMap.refresh();
                mapHandler.postDelayed(this, 200);
            }
        }
    };
    private View chatInputBar;
    private EditText chatInput;
    // Invisible field the soft keyboard types into for a DX edit or a gui-edit.
    private EditText scriptInput;
    private String scriptCommitted = "";
    private boolean scriptApplying;
    private boolean scriptKeyboardShown;
    private boolean scriptMultiline;
    // Chat took the keyboard. Cleared the next time the scripts stop asking for it.
    private boolean scriptKeyboardSuppressed;
    private boolean finishingField;
    private String connectIp = "";
    private int connectPort = 0;
    private String connectNick = "";
    private boolean joinedOnce = false;
    private DisconnectDialog disconnectDialog;
    private volatile boolean netRunning;
    private volatile boolean foreground;
    private boolean clientReady;
    private Thread netThread;
    private int interfaceScalePercent = InterfaceScale.DEFAULT_PERCENT;
    private int chatAnchor = HudLayout.TOP_LEFT;
    private int buttonAnchor = HudLayout.FOLLOW_CHAT;
    private int chatPercent = HudLayout.DEFAULT_CHAT_PERCENT;
    private boolean chatTouch = true;
    private View hudHost;
    private int keyboardInset;
    private int reportedUiWidth;
    private int reportedUiHeight;

    private native boolean nativeInit();
    private native boolean nativeSetAssetPath(String assetPath);
    private native boolean nativeConnect(String ip, int port, String nickname, int joinProfile);
    private native boolean nativeSetNetworkSerial(String serial);
    private native boolean nativeCancelIdentityRequest(long requestId);
    private native void nativeProvideIdentityTicket(long requestId, String ticket, String error);
    private com.saonline.neonlauncher.ui.NeonIdentityUi identityUi;

    public void onNativeIdentityRequest(long requestId, String serverId, String endpoint, int mode) {
        // JNI can be holding the state mutex. Always queue before calling JNI back.
        mapHandler.post(() -> {
            if (isFinishing() || isDestroyed()) return;
            if (identityUi == null) identityUi = new com.saonline.neonlauncher.ui.NeonIdentityUi(this);
            if (mode == 2 && !identityUi.isSignedIn()) {
                // Browser login can take minutes. Release the pending server connection,
                // then restart with a fresh ticket after authentication succeeds.
                if (!nativeCancelIdentityRequest(requestId)) return;
                if (loadingView != null) loadingView.end();
                hideDownload();
                new com.google.android.material.dialog.MaterialAlertDialogBuilder(this)
                    .setTitle("SA:O account")
                    .setMessage("This server requires SA:O sign-in. Connect your Discord account to continue.")
                    .setPositiveButton("Sign in with Discord", (dialog, which) -> identityUi.signIn(() -> {
                        if (!connectToServer()) {
                            showDisconnectDialog("SA:O account", "Unable to restart the server connection. Please retry.");
                        }
                    }, message -> showDisconnectDialog("SA:O account", message)))
                    .setNegativeButton(android.R.string.cancel, (dialog, which) ->
                        showDisconnectDialog("SA:O account", "Sign in to your SA:O account before connecting to this server."))
                    .setCancelable(false).show();
                return;
            }
            identityUi.authorize(serverId, endpoint, mode == 2,
                ticket -> nativeProvideIdentityTicket(requestId, ticket, ""),
                error -> nativeProvideIdentityTicket(requestId, "", error));
        });
    }
    private native void nativePulse(boolean renderFrame);
    private native void nativeDestroy();
    private native void nativeSetScreenSize(int width, int height);

    private void reportDxScreen(int width, int height) {
        if (!clientReady || width <= 0 || height <= 0) return;
        float density = getResources().getDisplayMetrics().density;
        int logicalWidth = InterfaceScale.logicalSize(width, density, interfaceScalePercent);
        int logicalHeight = InterfaceScale.logicalSize(height, density, interfaceScalePercent);
        if (logicalWidth == reportedUiWidth && logicalHeight == reportedUiHeight) return;
        nativeSetScreenSize(logicalWidth, logicalHeight);
        reportedUiWidth = logicalWidth;
        reportedUiHeight = logicalHeight;
        Log.i(TAG, "Interface viewport " + width + "x" + height + " -> " + logicalWidth + "x"
                + logicalHeight + " density=" + density + " scale=" + interfaceScalePercent + "%");
    }
    private native void nativeCursorEvent(int action, float x, float y);
    private native void nativeScriptInput(String text, int backspaces, String key);
    private native void nativeSoundEnded(int id);
    private native void nativeSoundLength(int id, double seconds);
    private final java.util.HashMap<Integer, MediaPlayer> sounds = new java.util.HashMap<>();
    private final java.util.HashMap<Integer, Float> soundVolume = new java.util.HashMap<>();
    private final java.util.HashMap<Integer, Float> soundPan = new java.util.HashMap<>();
    private final java.util.HashMap<Integer, Boolean> soundLoop = new java.util.HashMap<>();
    private final java.util.HashMap<Integer, Boolean> soundPaused = new java.util.HashMap<>();
    private native boolean nativeSendChat(String text);
    private native void nativeChatInputActive(boolean active);
    private native void nativeKeyEvent(String key, boolean down);
    private native String nativeBoundKeys();
    // On-screen key list (legacy SA-MP launcher ButtonPanel: ">>" opens a row of keys, "<<" closes it).
    private Button keysOpenButton;
    private LinearLayout keysRow;
    private View keysPanel;
    private boolean keyListAvailable;
    // F11 opens the player map only while the "radar" control is enabled (toggleControl, kind 20).
    private boolean radarControlEnabled = true;
    // Script-drawn radar (setMobileRadarArea, kind 21). A tap inside acts like F11.
    private View radarZone;
    private GameSettingsView settingsView;
    // Cog and Chat buttons; hidden with the key list while a script shows the cursor (its own panel is up).
    private View settingsButtonView;
    private View chatButtonView;
    private boolean hudRowHidden;
    private native boolean nativeSetGameSetting(int index, int value);
    private native int[] nativeGetGameSetting(int index);
    private native void nativeDropConnection();
    private native String nativeMapSnapshot();
    // Keeps the Wi-Fi radio out of power save while the game runs. Device 2026-11-14 04:30:57 (UTC-3): traffic stopped in
    // both directions at once while client and server kept running; the server timed the phone out 6 s later
    // (PLAYER_TIMEOUT, shim "MOBILE CLIENT REMOVED slot 0 packet 6"). A dozing radio drops UDP that way.
    private android.net.wifi.WifiManager.WifiLock wifiLock;

    private void acquireWifiLock() {
        try {
            android.net.wifi.WifiManager wifi =
                (android.net.wifi.WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            if (wifi == null) return;
            final int mode = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
                ? android.net.wifi.WifiManager.WIFI_MODE_FULL_LOW_LATENCY
                : android.net.wifi.WifiManager.WIFI_MODE_FULL_HIGH_PERF;
            wifiLock = wifi.createWifiLock(mode, "worldrpg:game-network");
            wifiLock.setReferenceCounted(false);
            wifiLock.acquire();
            Log.i(TAG, "Wi-Fi lock held (mode " + mode + ")");
        } catch (RuntimeException error) {
            Log.w(TAG, "Wi-Fi lock not available", error);
        }
    }

    private void releaseWifiLock() {
        try {
            if (wifiLock != null && wifiLock.isHeld()) wifiLock.release();
        } catch (RuntimeException ignored) {
            // Already released by the system.
        }
        wifiLock = null;
    }

    public static String findReadyGameDirectory(android.content.Context context, NativeAssetBridge assets) {
        String installed = assets.getReadyStoragePath();
        if (installed == null) return null;
        return GameLibraries.areAvailable(context, new File(installed), assets.findLocalAssetsFolder())
                ? installed : null;
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        NativeAssetBridge assets = new NativeAssetBridge(this);
        String installed = findReadyGameDirectory(this, assets);
        if (installed == null) {
            setResult(RESULT_CANCELED, new Intent().putExtra(EXTRA_GAME_ASSETS_REQUIRED, true));
            super.onCreate(savedInstanceState); // The base closes the Activity without initializing GTA.
            return;
        }
        EngineDiagnostics.begin(this);
        try {
            File generation = new File(installed).getCanonicalFile();
            gameDirectory = generation.getPath() + File.separator;
            GpuTextureSupport gpu = GpuProbe.inspect();
            EngineDiagnostics.noteGpu(this, gpu);
            EngineCaseAliases.prepare(generation);
            validateGameText(assets.usesCdn());
            gpu.validate(generation);
            validateLevelDefinitions(generation);
            // The VM calls the engine's JNI_OnLoad and registers the native
            // NvEventQueueActivity methods. A plain dlopen does not do this.
            GameLibraries.load(this, generation, assets.findLocalAssetsFolder(), assets.usesCdn());
            // Loaded here, before the engine thread exists: its JNI_OnLoad gives libGTASA a heap inside one
            // 2 GB window (GameHeap.cpp). startMtaSession loads it again, which is then a no-op.
            System.loadLibrary("WorldRPGNeon");
            Log.i(TAG, "Native GTA host prepared with generation " + generation.getName());
        } catch (IOException | LinkageError | SecurityException error) {
            bootError = error.getMessage() == null ? "Nao foi possivel preparar a engine GTA." : error.getMessage();
            EngineDiagnostics.failure(this, bootError);
            setResult(RESULT_CANCELED, new Intent().putExtra(EXTRA_BOOT_ERROR, bootError));
            Log.e(TAG, "Native GTA host preparation failed", error);
        }
        // The base class still receives Android's lifecycle after a failed
        // preparation. shouldStartEngine then refuses native initialization.
        super.onCreate(savedInstanceState);
        if (bootError == null && gameDirectory != null) {
            acquireWifiLock();
            installMtaPause();
            installPlayerMap();
            installTransferAndChat();
            installSettings();
        }
    }

    @Override
    protected void onEngineSurfaceReady(boolean ready) {
        Log.i(TAG, "Native GTA surface ready=" + ready);
        super.onEngineSurfaceReady(ready);
        if (ready) startMtaSession();
    }

    @Override
    public void onResume() {
        super.onResume();
        foreground = true;
        // The launcher can change this while the separate engine process is paused.
        interfaceScalePercent = InterfaceScaleSettings.read(this);
        GameSettingsView.applyHudSettings(this, settingsHost);
        if (dxOverlay != null) reportDxScreen(dxOverlay.getWidth(), dxOverlay.getHeight());
    }

    @Override
    public void onPause() {
        foreground = false;
        super.onPause();
    }

    private void startMtaSession() {
        String ip = getIntent().getStringExtra(GameActivity.EXTRA_SERVER_IP);
        if (ip == null || ip.isEmpty()) ip = BuildConfig.DEFAULT_SERVER_IP;
        if (ip == null || ip.isEmpty() || clientReady || bootError != null) return;
        int port = getIntent().getIntExtra(GameActivity.EXTRA_SERVER_PORT, BuildConfig.DEFAULT_SERVER_PORT);
        try {
            // libc++ is linked statically into WorldRPGNeon.
            System.loadLibrary("WorldRPGNeon");
            Log.i(TAG, "MTA client library loaded");
        } catch (UnsatisfiedLinkError error) {
            Log.e(TAG, "MTA client library failed to load", error);
            onEngineBootstrapError("Unable to load the MTA client library.");
            return;
        }
        if (!nativeInit() || !nativeSetAssetPath(gameDirectory)) {
            onEngineBootstrapError("Unable to initialize the MTA client.");
            return;
        }
        try {
            if (!nativeSetNetworkSerial(com.saonline.neonlauncher.network.NetDeviceIdentity.get(this)))
                throw new IllegalStateException("Unable to set network identity");
        } catch (RuntimeException error) {
            Log.e(TAG, "Network identity initialization failed", error);
            onEngineBootstrapError("Unable to initialize this device's network identity.");
            return;
        }
        clientReady = true;
        int screenW = dxOverlay != null ? dxOverlay.getWidth() : 0;
        int screenH = dxOverlay != null ? dxOverlay.getHeight() : 0;
        if (screenW <= 0 || screenH <= 0) {
            android.util.DisplayMetrics metrics = getResources().getDisplayMetrics();
            screenW = metrics.widthPixels;
            screenH = metrics.heightPixels;
        }
        interfaceScalePercent = InterfaceScaleSettings.read(this);
        reportDxScreen(screenW, screenH);
        String nick = new PreferencesManager(this).getUsername();
        if (nick == null || nick.isEmpty()) nick = BuildConfig.DEFAULT_PLAYER_NICK;
        connectIp = ip;
        connectPort = port;
        connectNick = nick;
        if (!connectToServer()) {
            // Desktop CConnectManager::Connect when StartNetwork fails (CConnectManager.cpp:246-255).
            showDisconnectDialog("Error [CC22]", "Connecting to " + ip + " at port " + port + " failed!");
        }
    }

    private boolean connectToServer() {
        if (!clientReady || isFinishing() || isDestroyed()) return false;
        int joinProfile = getIntent().getIntExtra(JoinProtocolSettings.EXTRA_JOIN_PROFILE, 0);
        if (!nativeConnect(connectIp, connectPort, connectNick, joinProfile)) return false;
        if (loadingView != null) loadingView.update("Connecting to " + connectIp + ":" + connectPort, 0f, 0f, 1f);
        else showDownload(0, "Connecting to server");
        if (netRunning) return true;
        netRunning = true;
        netThread = new Thread(() -> {
            while (netRunning) {
                nativePulse(foreground);
                try {
                    Thread.sleep(foreground ? 16 : 100);
                } catch (InterruptedException ignored) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }, "mta-net");
        netThread.start();
        return true;
    }

    private void installTransferAndChat() {
        LinearLayout transfer = new LinearLayout(this);
        transfer.setOrientation(LinearLayout.VERTICAL);
        transfer.setBackgroundColor(0xE0101820);
        transfer.setPadding(dp(16), dp(12), dp(16), dp(12));
        transfer.setVisibility(View.GONE);
        TextView heading = new TextView(this);
        heading.setText("Downloading");
        heading.setTextColor(Color.WHITE);
        heading.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        transfer.addView(heading);
        downloadBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        downloadBar.setMax(100);
        LinearLayout.LayoutParams barParams = new LinearLayout.LayoutParams(dp(420), dp(18));
        barParams.topMargin = dp(8);
        downloadBar.setLayoutParams(barParams);
        transfer.addView(downloadBar);
        downloadLabel = new TextView(this);
        downloadLabel.setTextColor(0xFFD5DEE8);
        downloadLabel.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        downloadLabel.setPadding(0, dp(6), 0, 0);
        transfer.addView(downloadLabel);
        FrameLayout.LayoutParams transferParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER);
        addContentView(transfer, transferParams);
        downloadPanel = transfer;

        // Desktop transfer box for the HTTP file downloads (kind 3 lines from nativePulse).
        transferBox = new MtaTransferBox(this);
        addContentView(transferBox, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        loadingView = new ConnectionLoadingView(this, gameDirectory);
        addContentView(loadingView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        dxOverlay = new MtaDxView(this);
        dxOverlay.setVisibility(View.GONE);
        dxOverlay.setElevation(dp(12));
        dxOverlay.setCursorListener(this::onCursorTouch);
        dxOverlay.addOnLayoutChangeListener((v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            int w = right - left;
            int h = bottom - top;
            boolean legacyIme = Build.VERSION.SDK_INT < 30 && scriptKeyboardShown;
            if (!legacyIme && w > 0 && h > 0 && (w != oldRight - oldLeft || h != oldBottom - oldTop)) reportDxScreen(w, h);
            updateKeyboardViewport();
        });
        if (Build.VERSION.SDK_INT >= 30) {
            dxOverlay.setOnApplyWindowInsetsListener((v, insets) -> {
                keyboardInset = insets.getInsets(WindowInsets.Type.ime()).bottom;
                updateKeyboardViewport();
                return insets;
            });
        }
        addContentView(dxOverlay, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        chatLog = new MtaChatView(this);
        chatLog.setVisibility(View.GONE);
        addContentView(chatLog, new FrameLayout.LayoutParams(0, 0));
        hudHost = (View) chatLog.getParent();
        hudHost.addOnLayoutChangeListener((v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop) layoutHud();
        });
        chatLog.setOnClickListener(v -> openChatInput());
        installRadarZone();
        ImageButton settingsButton = new ImageButton(this);
        settingsButton.setImageResource(com.saonline.neonlauncher.R.drawable.ic_settings_hud);
        settingsButton.setImageTintList(ColorStateList.valueOf(Color.WHITE));
        settingsButton.setBackground(hudButtonBackground());
        settingsButton.setContentDescription("Settings");
        settingsButton.setPadding(dp(8), dp(8), dp(8), dp(8));
        settingsButton.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
        settingsButton.setOnClickListener(v -> toggleMtaPause());
        addContentView(settingsButton, new FrameLayout.LayoutParams(dp(40), dp(40)));
        Button chatButton = new Button(this);
        chatButton.setText("Chat");
        chatButton.setAllCaps(false);
        chatButton.setTextColor(Color.WHITE);
        chatButton.setBackground(hudButtonBackground());
        chatButton.setOnClickListener(v -> openChatInput());
        addContentView(chatButton, new FrameLayout.LayoutParams(dp(88), dp(40)));
        settingsButtonView = settingsButton;
        chatButtonView = chatButton;
        installKeyList();
        GameSettingsView.applyHudSettings(this, settingsHost);

        chatInput = new EditText(this);
        chatInput.setHint("Say:");
        chatInput.setSingleLine(true);
        // Desktop CChat default character limit (CChat.h:308). setChatboxCharacterLimit changes it (kind 17).
        chatInput.setFilters(new InputFilter[] {new InputFilter.LengthFilter(96)});
        chatInput.setTextColor(Color.WHITE);
        chatInput.setHintTextColor(0xFFACD5FE);
        chatInput.setImeOptions(EditorInfo.IME_ACTION_SEND | COMPACT_IME);
        chatInput.setOnEditorActionListener((v, action, event) -> {
            if (action == EditorInfo.IME_ACTION_SEND) {
                submitChat();
                return true;
            }
            return false;
        });
        Button send = new Button(this);
        send.setText("Send");
        send.setOnClickListener(v -> submitChat());
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setBackgroundColor(0xF0101820);
        bar.setPadding(dp(8), dp(6), dp(8), dp(6));
        bar.addView(chatInput, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        bar.addView(send);
        bar.setVisibility(View.GONE);
        if (Build.VERSION.SDK_INT >= 30) {
            bar.setOnApplyWindowInsetsListener((v, insets) -> {
                int ime = insets.getInsets(WindowInsets.Type.ime()).bottom;
                v.setTranslationY(ime > 0 ? -ime : 0f);
                return insets;
            });
        }
        FrameLayout.LayoutParams inputParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM);
        addContentView(bar, inputParams);
        chatInputBar = bar;
        installScriptInput();
    }

    // The key list of the legacy SA-MP launcher (samp/gui/samp_widgets/buttonpanel.cpp): ">>" opens a
    // horizontal row and "<<" closes it. The row holds the keys that scripts bound with bindKey, read
    // when it opens. A tap is one key press (down, then up).
    private void installKeyList() {
        keysOpenButton = new Button(this);
        keysOpenButton.setText(">>");
        keysOpenButton.setTextColor(Color.WHITE);
        keysOpenButton.setBackground(hudButtonBackground());
        keysOpenButton.setOnClickListener(v -> openKeyList());
        // Shown while connected (legacy ButtonPanel::draw returns unless GAMESTATE_CONNECTED).
        keysOpenButton.setVisibility(View.GONE);
        addContentView(keysOpenButton, new FrameLayout.LayoutParams(dp(56), dp(40)));

        keysRow = new LinearLayout(this);
        keysRow.setOrientation(LinearLayout.HORIZONTAL);
        android.widget.HorizontalScrollView scroll = new android.widget.HorizontalScrollView(this);
        scroll.setHorizontalScrollBarEnabled(false);
        scroll.addView(keysRow, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT));
        scroll.setVisibility(View.GONE);
        addContentView(scroll, new FrameLayout.LayoutParams(0, dp(40)));
        keysPanel = scroll;
    }

    private void layoutHud() {
        if (hudHost == null || settingsButtonView == null || keysPanel == null) return;
        int width = hudHost.getWidth();
        int height = hudHost.getHeight();
        if (width <= 0 || height <= 0) return;
        int marginX = Math.min(width / 4, Math.max(dp(12), Math.round(width * 0.02f)));
        int marginY = Math.min(height / 4, Math.max(dp(12), Math.round(height * 0.03f)));
        int gap = dp(8);
        int rowHeight = dp(40);
        int chatWidth = Math.min(width - 2 * marginX,
                Math.round(Math.max(dp(280), width * 0.44f) * chatPercent / 100f));
        int chatLimit = Math.max(1, height - 2 * marginY - rowHeight - gap);
        float line = Math.min(chatLimit / 10f, Math.max(22f, height * 22f / 720f) * chatPercent / 100f);
        int chatHeight = (int) Math.ceil(line * 10);
        int chatLeft = HudLayout.left(chatAnchor, width, chatWidth, marginX);
        int chatTop = HudLayout.top(chatAnchor, height, chatHeight, marginY);
        positionHudView(chatLog, chatLeft, chatTop, chatWidth, chatHeight);
        chatLog.setLayoutOptions(line, chatTouch, chatAnchor >= HudLayout.BOTTOM_LEFT);

        int anchor = buttonAnchor == HudLayout.FOLLOW_CHAT ? chatAnchor : buttonAnchor;
        int settingsWidth = dp(40), chatButtonWidth = dp(88), keysWidth = dp(56);
        int rowWidth = settingsWidth + chatButtonWidth + keysWidth + 2 * gap;
        int rowLeft = HudLayout.left(anchor, width, rowWidth, marginX);
        int rowTop = HudLayout.top(anchor, height, rowHeight, marginY);
        if (rowLeft < chatLeft + chatWidth && chatLeft < rowLeft + rowWidth)
            rowTop = HudLayout.besideChat(rowTop, rowHeight, chatTop, chatHeight, anchor, height, marginY, gap);

        int chatButtonLeft = rowLeft + settingsWidth + gap;
        int keysLeft = chatButtonLeft + chatButtonWidth + gap;
        positionHudView(settingsButtonView, rowLeft, rowTop, settingsWidth, rowHeight);
        positionHudView(chatButtonView, chatButtonLeft, rowTop, chatButtonWidth, rowHeight);
        positionHudView(keysOpenButton, keysLeft, rowTop, keysWidth, rowHeight);
        int listWidth = Math.min(width - 2 * marginX, dp(480));
        int listLeft = keysLeft;
        int listTop = rowTop;
        if (width - marginX - keysLeft < dp(160)) {
            listLeft = HudLayout.left(anchor, width, listWidth, marginX);
            listTop = rowTop + rowHeight + gap;
            if (listTop + rowHeight > height - marginY) listTop = Math.max(marginY, rowTop - rowHeight - gap);
        } else {
            listWidth = Math.min(listWidth, width - marginX - keysLeft);
        }
        positionHudView(keysPanel, listLeft, listTop, listWidth, rowHeight);
    }

    private static void positionHudView(View view, int x, int y, int width, int height) {
        FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) view.getLayoutParams();
        if (params.gravity == (Gravity.TOP | Gravity.LEFT) && params.leftMargin == x && params.topMargin == y
                && params.width == width && params.height == height) return;
        params.gravity = Gravity.TOP | Gravity.LEFT;
        params.leftMargin = x;
        params.topMargin = y;
        params.width = width;
        params.height = height;
        view.setLayoutParams(params);
    }

    private Button keyListButton(String label) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(Color.WHITE);
        button.setBackground(hudButtonBackground());
        button.setMinWidth(dp(48));
        button.setMinimumWidth(dp(48));
        button.setPadding(dp(10), 0, dp(10), 0);
        LinearLayout.LayoutParams params =
                new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, dp(40));
        params.rightMargin = dp(4);
        button.setLayoutParams(params);
        return button;
    }

    private void openKeyList() {
        if (keysRow == null || keysPanel == null) return;
        keysRow.removeAllViews();
        Button close = keyListButton("<<");
        close.setOnClickListener(v -> closeKeyList());
        keysRow.addView(close);
        String bound = nativeBoundKeys();
        if (bound == null || bound.isEmpty()) {
            TextView none = new TextView(this);
            none.setText("No keys bound");
            none.setTextColor(0xFFD5DEE8);
            none.setGravity(Gravity.CENTER_VERTICAL);
            none.setPadding(dp(8), 0, dp(8), 0);
            keysRow.addView(none, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, dp(40)));
        } else {
            for (String key : bound.split("\\|")) {
                if (key.isEmpty()) continue;
                Button button = keyListButton(key.length() == 1 ? key.toUpperCase(java.util.Locale.ROOT) : key);
                button.setOnClickListener(v -> {
                    nativeKeyEvent(key, true);
                    // The release is a later event, so getKeyState is true for a few frames.
                    v.postDelayed(() -> nativeKeyEvent(key, false), 80);
                });
                keysRow.addView(button);
            }
        }
        keysOpenButton.setVisibility(View.GONE);
        keysPanel.setVisibility(View.VISIBLE);
    }

    private void setHudRowHidden(boolean hidden) {
        if (hidden == hudRowHidden) return;
        hudRowHidden = hidden;
        int visibility = hidden ? View.GONE : View.VISIBLE;
        if (settingsButtonView != null) settingsButtonView.setVisibility(visibility);
        if (chatButtonView != null) chatButtonView.setVisibility(visibility);
        closeKeyList();
    }

    private void closeKeyList() {
        if (keysPanel != null) keysPanel.setVisibility(View.GONE);
        if (keysOpenButton != null) {
            keysOpenButton.setVisibility(keyListAvailable && !hudRowHidden ? View.VISIBLE : View.GONE);
        }
    }

    // The key list exists only during a session, and gives way to the chat input bar, which opens
    // over the same strip of the screen.
    private void setKeyListAvailable(boolean available) {
        keyListAvailable = available;
        closeKeyList();
    }

    // The game surface is not an Android editor. This field only exists so InputMethodManager
    // has a focused view. A "K" line from nativePump opens it; the diff of its text is delivered
    // as onClientCharacter / onClientKey, or into the focused gui-edit.
    private void installScriptInput() {
        scriptInput = new EditText(this) {
            @Override
            public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
                InputConnection base = super.onCreateInputConnection(outAttrs);
                if (base == null) return null;
                return new InputConnectionWrapper(base, true) {
                    @Override
                    public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                        if (beforeLength > 0 && afterLength == 0 && scriptCommitted.isEmpty()) {
                            nativeScriptInput("", beforeLength, "");
                            return true;
                        }
                        return super.deleteSurroundingText(beforeLength, afterLength);
                    }

                    @Override
                    public boolean sendKeyEvent(KeyEvent event) {
                        if (event.getAction() == KeyEvent.ACTION_DOWN
                                && event.getKeyCode() == KeyEvent.KEYCODE_ENTER) {
                            if (scriptMultiline) {
                                // Keep the IME's scratch buffer and Lua in the same state,
                                // so the next composing edit/backspace includes this newline.
                                scriptInput.append("\n");
                                return true;
                            }
                            finishScriptField();
                            return true;
                        }
                        if (event.getAction() == KeyEvent.ACTION_DOWN
                                && event.getKeyCode() == KeyEvent.KEYCODE_DEL
                                && scriptCommitted.isEmpty()) {
                            nativeScriptInput("", 1, "");
                            return true;
                        }
                        return super.sendKeyEvent(event);
                    }
                };
            }
        };
        scriptInput.setSingleLine(true);
        scriptInput.setAlpha(0f);
        scriptInput.setBackgroundColor(Color.TRANSPARENT);
        scriptInput.setFocusable(true);
        scriptInput.setFocusableInTouchMode(true);
        scriptInput.setShowSoftInputOnFocus(true);
        // NEXT: the action key moves to the next DX field (tab) or closes the keyboard.
        // It does not send enter, which the login screen treats as submit.
        scriptInput.setImeOptions(EditorInfo.IME_ACTION_NEXT | COMPACT_IME);
        scriptInput.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        scriptInput.setOnEditorActionListener((v, action, event) -> {
            int id = action & EditorInfo.IME_MASK_ACTION;
            if (id == EditorInfo.IME_ACTION_NEXT || id == EditorInfo.IME_ACTION_DONE
                    || id == EditorInfo.IME_ACTION_GO || id == EditorInfo.IME_ACTION_SEND) {
                finishScriptField();
                return true;
            }
            return false;
        });
        scriptInput.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override public void afterTextChanged(Editable s) { deliverScriptText(s == null ? "" : s.toString()); }
        });
        addContentView(scriptInput, new FrameLayout.LayoutParams(1, 1, Gravity.TOP | Gravity.START));
    }

    private void deliverScriptText(String next) {
        if (scriptApplying || !scriptKeyboardShown) return;
        if (scriptMultiline) next = next.replace("\r\n", "\n").replace('\r', '\n');
        String previous = scriptCommitted;
        int i = 0;
        int j = 0;
        while (i < previous.length() && j < next.length()) {
            int a = previous.codePointAt(i);
            int b = next.codePointAt(j);
            if (a != b) break;
            i += Character.charCount(a);
            j += Character.charCount(b);
        }
        int removed = previous.codePointCount(i, previous.length());
        String inserted = next.substring(j);
        scriptCommitted = next;
        StringBuilder keep = new StringBuilder();
        for (int k = 0; k < inserted.length();) {
            int cp = inserted.codePointAt(k);
            if (scriptMultiline || (cp != '\n' && cp != '\r')) keep.appendCodePoint(cp);
            k += Character.charCount(cp);
        }
        if (removed == 0 && keep.length() == 0) return;
        nativeScriptInput(keep.toString(), removed, "");
    }

    // Commits whatever is in the field, moves a DX edit to the next one, and closes the keyboard.
    // The login screen treats Enter as submit, so this path sends tab instead.
    private void finishScriptField() {
        if (finishingField || (scriptKeyboardSuppressed && !scriptKeyboardShown)) return;
        finishingField = true;
        try {
            if (scriptInput != null && scriptInput.getText() != null) {
                deliverScriptText(scriptInput.getText().toString());
            }
            nativeScriptInput("", 0, "tab");
            scriptKeyboardSuppressed = true;
            scriptKeyboardShown = false;
            hideScriptKeyboard();
        } finally {
            finishingField = false;
        }
    }

    private void showScriptKeyboard() {
        if (scriptInput == null) return;
        scriptApplying = true;
        scriptInput.setText("");
        scriptCommitted = "";
        scriptApplying = false;
        raiseScriptKeyboard();
    }

    // The game surface keeps window focus, so a later showSoftInput is ignored.
    // Called from the tap as well as from the K line.
    private void raiseScriptKeyboard() {
        if (scriptInput == null) return;
        getWindow().setSoftInputMode(keyboardAdjustMode()
                | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
        if (view != null) view.clearFocus();
        scriptInput.requestFocus();
        if (Build.VERSION.SDK_INT >= 30) {
            android.view.WindowInsetsController controller = scriptInput.getWindowInsetsController();
            if (controller != null) controller.show(WindowInsets.Type.ime());
        }
        InputMethodManager keyboard = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (keyboard != null) keyboard.showSoftInput(scriptInput, InputMethodManager.SHOW_IMPLICIT);
        dxOverlay.requestApplyInsets();
    }

    private int keyboardAdjustMode() {
        return Build.VERSION.SDK_INT >= 30 ? WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                : WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE;
    }

    private void updateKeyboardViewport() {
        if (dxOverlay == null) return;
        float scale = scriptKeyboardShown ? HudLayout.keyboardScale(dxOverlay.getHeight(), keyboardInset) : 1f;
        dxOverlay.setKeyboardScale(scale);
    }

    private void hideScriptKeyboard() {
        if (scriptInput == null) return;
        scriptApplying = true;
        scriptInput.setText("");
        scriptCommitted = "";
        scriptApplying = false;
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
        scriptInput.clearFocus();
        if (Build.VERSION.SDK_INT >= 30 && scriptInput.getWindowInsetsController() != null) {
            scriptInput.getWindowInsetsController().hide(WindowInsets.Type.ime());
        }
        InputMethodManager keyboard = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (keyboard != null) keyboard.hideSoftInputFromWindow(scriptInput.getWindowToken(), 0);
        updateKeyboardViewport();
    }

    // The login form asks for the keyboard from a server event, which Android ignores.
    // This down event is the gesture. Lua then keeps the field focused, or the next
    // frame closes the keyboard if the tap was a button.
    private void onCursorTouch(int action, float x, float y) {
        nativeCursorEvent(action, x, y);
        // K is already set when the login form is open. This tap is the gesture Android
        // accepts. A tap before that line exists still queues the click, and the click
        // calls guiSetInputEnabled, which sends K on the next frame.
        if (action != 0 || scriptInput == null) return;
        if (!scriptKeyboardShown) {
            scriptKeyboardSuppressed = false;
            return;
        }
        scriptKeyboardSuppressed = false;
        raiseScriptKeyboard();
    }

    /** "K" requests text input; "K multiline" enables line breaks for a gui-memo. */
    private void syncScriptKeyboard(String frame) {
        boolean want = false;
        boolean multiline = false;
        if (frame != null) {
            for (String line : frame.split("\n", -1)) {
                if ("K".equals(line) || "K multiline".equals(line)) {
                    want = true;
                    multiline = "K multiline".equals(line);
                    break;
                }
            }
        }
        if (!want) scriptKeyboardSuppressed = false;
        boolean show = want && !scriptKeyboardSuppressed;
        boolean changedMode = show && scriptMultiline != multiline;
        if (changedMode && scriptInput != null) {
            scriptApplying = true;
            scriptMultiline = multiline;
            scriptInput.setText("");
            scriptCommitted = "";
            scriptInput.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                    | (multiline ? InputType.TYPE_TEXT_FLAG_MULTI_LINE : 0));
            scriptInput.setSingleLine(!multiline);
            scriptInput.setImeOptions(COMPACT_IME | (multiline ? EditorInfo.IME_ACTION_NONE | EditorInfo.IME_FLAG_NO_ENTER_ACTION
                    : EditorInfo.IME_ACTION_NEXT));
            scriptApplying = false;
            InputMethodManager keyboard = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (keyboard != null) keyboard.restartInput(scriptInput);
        }
        if (show == scriptKeyboardShown && !changedMode) return;
        scriptKeyboardShown = show;
        if (show) showScriptKeyboard();
        else hideScriptKeyboard();
    }

    private void openChatInput() {
        if (chatInputBar == null || chatInput == null) return;
        scriptKeyboardSuppressed = true;
        scriptKeyboardShown = false;
        hideScriptKeyboard();
        chatInputBar.setVisibility(View.VISIBLE);
        raisePanel(chatInputBar);
        getWindow().setSoftInputMode(keyboardAdjustMode());
        nativeChatInputActive(true);
        // The chat bar covers the key list; hide both key views until the chat closes.
        if (keysPanel != null) keysPanel.setVisibility(View.GONE);
        if (keysOpenButton != null) keysOpenButton.setVisibility(View.GONE);
        chatInput.requestFocus();
        InputMethodManager keyboard = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (keyboard != null) keyboard.showSoftInput(chatInput, InputMethodManager.SHOW_IMPLICIT);
        if (Build.VERSION.SDK_INT >= 30) chatInputBar.requestApplyInsets();
    }

    private void submitChat() {
        if (chatInput == null) return;
        String typed = chatInput.getText() == null ? "" : chatInput.getText().toString().trim();
        chatInput.setText("");
        if (chatInputBar != null) chatInputBar.setVisibility(View.GONE);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
        nativeChatInputActive(false);
        closeKeyList();
        InputMethodManager keyboard = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (keyboard != null) keyboard.hideSoftInputFromWindow(chatInput.getWindowToken(), 0);
        if (typed.isEmpty()) return;
        String command = typed.startsWith("/") ? typed.substring(1).trim() : "say " + typed;
        if (command.equalsIgnoreCase("disconnect")) {
            // Test hook for the disconnect dialog: stops the transport so the join
            // protocol reports "Connection lost" the same way a real drop does.
            nativeDropConnection();
            return;
        }
        if (command.isEmpty() || !nativeSendChat(command)) {
            appendChat("#FF6666Message was not sent.");
        }
    }

    private void showDownload(int percent, String label) {
        if (downloadPanel == null) return;
        downloadPanel.setVisibility(View.VISIBLE);
        downloadBar.setProgress(Math.max(0, Math.min(100, percent)));
        downloadLabel.setText(label == null ? "" : label);
    }

    private void hideDownload() {
        if (downloadPanel != null) downloadPanel.setVisibility(View.GONE);
    }

    private void appendChat(String line) {
        if (chatLog == null || line == null || line.isEmpty()) return;
        chatLog.push(line);
    }

    @SuppressWarnings("unused")
    public void onNativeConnectionStateChange(boolean connected, String message) {
        // Ticket completion originates on the UI thread too; defer native callbacks
        // until the JNI state mutex has been released before updating controls.
        mapHandler.post(() -> {
            if (isFinishing() || isDestroyed()) return;
            if (connected) {
                joinedOnce = true;
                // The "Connecting to server" panel ends with the join, as the desktop connect dialog does.
                hideDownload();
                appendChat("#ACD5FEConnected to the server.");
                setKeyListAvailable(true);
                // The game loaded its own options file at boot; put the player's choices back.
                applySavedSettings();
                mapHandler.postDelayed(this::applySavedSettings, 3000);
            } else {
                hideDownload();
                // Never leave the loading screen over a disconnect (desktop ~CClientGame ends it first).
                if (loadingView != null) loadingView.end();
                setKeyListAvailable(false);
                radarControlEnabled = true;
                setRadarArea("");
                // Native sends the desktop error box as "title<0x1F>text" (net/DisconnectMessages).
                String title = "Disconnected";
                String text = message == null || message.isEmpty() ? "Disconnected from the server." : message;
                int split = text.indexOf('\u001f');
                if (split >= 0) {
                    title = text.substring(0, split);
                    text = text.substring(split + 1);
                }
                appendChat("#FF6666" + title + ": " + text.replace('\n', ' '));
                // Every refusal, timeout and drop is a popup, as on the desktop (ShowErrorMessageBox), with
                // Reconnect / Close (P3.1) so the player is never left in a dead session.
                joinedOnce = false;
                showDisconnectDialog(title, text);
            }
        });
    }

    private void playUrlSound(String body) {
        int bar = body.indexOf('|');
        int bar2 = bar < 0 ? -1 : body.indexOf('|', bar + 1);
        if (bar <= 0 || bar2 <= bar) return;
        int id;
        try { id = Integer.parseInt(body.substring(0, bar)); }
        catch (NumberFormatException error) { return; }
        boolean loop = "1".equals(body.substring(bar + 1, bar2));
        String url = body.substring(bar2 + 1);
        // Each Lua sound owns its player. Resource cleanup or stopSound releases
        // that id; starting another sound must not stop unrelated audio.
        stopUrlSound(Integer.toString(id));
        MediaPlayer player = new MediaPlayer();
        player.setAudioAttributes(new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build());
        try {
            player.setDataSource(url);
            player.setLooping(loop);
            player.setOnPreparedListener(ready -> {
                if (sounds.get(id) != ready) return;
                nativeSoundLength(id, Math.max(0, ready.getDuration()) / 1000.0);
                applySoundGains(id, ready);
                Boolean looped = soundLoop.get(id);
                if (looped != null) ready.setLooping(looped);
                if (Boolean.TRUE.equals(soundPaused.get(id))) return;
                ready.start();
            });
            player.setOnCompletionListener(done -> {
                if (sounds.get(id) != done) return;
                stopUrlSound(Integer.toString(id));
                nativeSoundEnded(id);
            });
            player.setOnErrorListener((failed, what, extra) -> {
                if (sounds.get(id) == failed) {
                    stopUrlSound(Integer.toString(id));
                    nativeSoundEnded(id);
                }
                return true;
            });
            sounds.put(id, player);
            player.prepareAsync();
        } catch (Exception error) {
            if (sounds.get(id) == player) stopUrlSound(Integer.toString(id));
            else player.release();
            nativeSoundEnded(id);
        }
    }

    private void stopUrlSound(String body) {
        int id;
        try { id = Integer.parseInt(body); }
        catch (NumberFormatException error) { return; }
        soundVolume.remove(id);
        soundPan.remove(id);
        soundLoop.remove(id);
        soundPaused.remove(id);
        MediaPlayer player = sounds.remove(id);
        if (player == null) return;
        player.setOnCompletionListener(null);
        player.setOnPreparedListener(null);
        player.setOnErrorListener(null);
        try { player.stop(); } catch (Exception ignored) {}
        player.release();
    }

    private void volumeUrlSound(String body) {
        int bar = body.indexOf('|');
        if (bar <= 0) return;
        int id;
        float volume;
        try {
            id = Integer.parseInt(body.substring(0, bar));
            volume = Float.parseFloat(body.substring(bar + 1));
        } catch (NumberFormatException error) { return; }
        if (!Float.isFinite(volume)) return;
        float gain = Math.max(0f, Math.min(1f, volume));
        soundVolume.put(id, gain);
        MediaPlayer player = sounds.get(id);
        if (player == null) return;
        applySoundGains(id, player);
    }

    private void applySoundGains(int id, MediaPlayer player) {
        float volume = soundVolume.containsKey(id) ? soundVolume.get(id) : 1f;
        float pan = soundPan.containsKey(id) ? soundPan.get(id) : 0f;
        // Full left (-1) silences right; full right (+1) silences left.
        float left = volume * (pan > 0f ? 1f - pan : 1f);
        float right = volume * (pan < 0f ? 1f + pan : 1f);
        try { player.setVolume(left, right); } catch (Exception ignored) {}
    }

    private void panUrlSound(String body) {
        int bar = body.indexOf('|');
        if (bar <= 0) return;
        int id;
        float pan;
        try {
            id = Integer.parseInt(body.substring(0, bar));
            pan = Float.parseFloat(body.substring(bar + 1));
        } catch (NumberFormatException error) { return; }
        if (!Float.isFinite(pan) || pan < -1f || pan > 1f) return;
        soundPan.put(id, pan);
        MediaPlayer player = sounds.get(id);
        if (player != null) applySoundGains(id, player);
    }

    private void loopUrlSound(String body) {
        int bar = body.indexOf('|');
        if (bar <= 0) return;
        int id;
        boolean loop;
        try {
            id = Integer.parseInt(body.substring(0, bar));
            loop = "1".equals(body.substring(bar + 1));
        } catch (NumberFormatException error) { return; }
        soundLoop.put(id, loop);
        MediaPlayer player = sounds.get(id);
        if (player == null) return;
        try { player.setLooping(loop); } catch (Exception ignored) {}
    }

    private void pauseUrlSound(String body) {
        int bar = body.indexOf('|');
        if (bar <= 0) return;
        int id;
        boolean paused;
        try {
            id = Integer.parseInt(body.substring(0, bar));
            paused = "1".equals(body.substring(bar + 1));
        } catch (NumberFormatException error) { return; }
        soundPaused.put(id, paused);
        MediaPlayer player = sounds.get(id);
        if (player == null) return;
        try {
            if (paused) player.pause();
            else if (!player.isPlaying()) player.start();
        } catch (Exception ignored) {}
    }

    private void seekUrlSound(String body) {
        int bar = body.indexOf('|');
        if (bar <= 0) return;
        int id;
        float seconds;
        try {
            id = Integer.parseInt(body.substring(0, bar));
            seconds = Float.parseFloat(body.substring(bar + 1));
        } catch (NumberFormatException error) { return; }
        MediaPlayer player = sounds.get(id);
        if (player == null) return;
        try { player.seekTo(Math.max(0, (int) (seconds * 1000f))); } catch (Exception ignored) {}
    }

    private void showDisconnectDialog(String title, String message) {
        if (isFinishing() || isDestroyed() || disconnectDialog != null) return;
        disconnectDialog = new DisconnectDialog(this, title, message, gameDirectory, new DisconnectDialog.Actions() {
            @Override public void onRetry() {
                disconnectDialog = null;
                if (!connectToServer()) {
                    String failed = "Connecting to " + connectIp + " at port " + connectPort + " failed!";
                    appendChat("#FF6666" + failed);
                    showDisconnectDialog("Error [CC22]", failed);
                }
            }
            @Override public void onQuit() {
                disconnectDialog = null;
                finish();
            }
        });
        disconnectDialog.show();
    }

    @SuppressWarnings("unused")
    public void onNativeGameLine(int kind, String text) {
        runOnUiThread(() -> {
            if (text == null) return;
            if (kind == 9) {
                // GTA asked for its pause/map menu (a tap on its own radar): same as F11.
                pressMapKey();
                return;
            }
            if (kind == 20) {
                if (text.startsWith("radar|")) radarControlEnabled = !text.endsWith("|0");
                return;
            }
            if (kind == 21) {
                setRadarArea(text);
                return;
            }
            if (kind == 8) {
                if (chatLog != null) chatLog.clearLines();
                return;
            }
            if (kind == 0) {
                appendChat(text);
                return;
            }
            if (kind == 11) {
                playUrlSound(text);
                return;
            }
            if (kind == 12) {
                stopUrlSound(text);
                return;
            }
            if (kind == 13) {
                volumeUrlSound(text);
                return;
            }
            if (kind == 14) {
                loopUrlSound(text);
                return;
            }
            if (kind == 15) {
                pauseUrlSound(text);
                return;
            }
            if (kind == 16) {
                seekUrlSound(text);
                return;
            }
            if (kind == 18) {
                panUrlSound(text);
                return;
            }
            if (kind == 10) {
                if (chatLog != null) chatLog.setChatVisible(!"0".equals(text));
                return;
            }
            if (kind == 17) {
                // Chat input length limit in UTF-16 units (setChatboxCharacterLimit, desktop CChat.cpp:806).
                if (chatInput != null) {
                    try {
                        int limit = Math.max(0, Math.min(255, Integer.parseInt(text)));
                        chatInput.setFilters(new InputFilter[] {new InputFilter.LengthFilter(limit)});
                    } catch (NumberFormatException ignored) {
                    }
                }
                return;
            }
            if (kind == 19) {
                // Connection loading screen (native ConnectionLoading): "B|fill|segStart|segEnd|status",
                // "U|..." or "E".
                if (loadingView == null) return;
                if (text.startsWith("E")) {
                    loadingView.end();
                    return;
                }
                String[] parts = text.split("\\|", 5);
                if (parts.length < 5) return;
                float fill, segStart, segEnd;
                try {
                    fill = Float.parseFloat(parts[1]);
                    segStart = Float.parseFloat(parts[2]);
                    segEnd = Float.parseFloat(parts[3]);
                } catch (NumberFormatException ignored) {
                    return;
                }
                // The screen opened for "Connecting" keeps its artwork when native opens it.
                if ("B".equals(parts[0]) && !loadingView.isActive()) loadingView.begin(parts[4], fill, segStart, segEnd);
                else loadingView.update(parts[4], fill, segStart, segEnd);
                if (downloadPanel != null) hideDownload();
                return;
            }
            if (kind == 3) {
                // "downloaded|total" in bytes; an empty line means the downloads ended.
                long downloaded = 0, total = 0;
                int bar = text.indexOf('|');
                if (bar > 0) {
                    try {
                        downloaded = Long.parseLong(text.substring(0, bar));
                        total = Long.parseLong(text.substring(bar + 1));
                    } catch (NumberFormatException ignored) {
                        total = 0;
                    }
                }
                if (transferBox != null) transferBox.setProgress(downloaded, total);
                return;
            }
            if (kind != 2) return;
            int split = text.indexOf('|');
            int percent = 0;
            String label = text;
            if (split > 0) {
                try {
                    percent = Integer.parseInt(text.substring(0, split));
                } catch (NumberFormatException ignored) {
                    percent = 0;
                }
                label = text.substring(split + 1);
            }
            if (percent >= 100 || label.isEmpty()) hideDownload();
            else showDownload(percent, label);
        });
    }

    /**
     * Width of one line of text for dxGetTextWidth and dxGetTextSize: in the DX overlay font, or
     * in the dxCreateFont file when fontFile is not null.
     */
    @SuppressWarnings("unused")
    public float onNativeMeasureText(String text, float pixelSize, String fontFile, boolean bold) {
        return MtaDxView.measureLine(text, pixelSize, fontFile, bold);
    }

    public float onNativeFontHeight(String source, float pixelSize, boolean bold) {
        return MtaDxView.fontHeight(source, pixelSize, bold);
    }

    // Bitmap work for the pixel calls (dxGetTexturePixels, dxSetTexturePixels, dxConvertPixels,
    // dxCreateTexture from pixels). Called on the script thread; see MtaDxView.
    @SuppressWarnings("unused")
    public int[] onNativeLoadPixels(String source) {
        return MtaDxView.loadPixels(source);
    }

    @SuppressWarnings("unused")
    public int[] onNativeDecodePixels(byte[] data) {
        return MtaDxView.decodePixels(data);
    }

    @SuppressWarnings("unused")
    public byte[] onNativeEncodePixels(int[] argb, int width, int height, boolean jpeg, int quality) {
        return MtaDxView.encodePixels(argb, width, height, jpeg, quality);
    }

    @SuppressWarnings("unused")
    public boolean onNativeStorePixels(String name, int[] argb, int width, int height) {
        return MtaDxView.storePixels(name, argb, width, height);
    }

    /** dxUpdateScreenSource: copies the game surface into a script bitmap (see MtaDxView.copyScreen). */
    @SuppressWarnings("unused")
    public void onNativeCopyScreen(String name, int width, int height) {
        runOnUiThread(() -> MtaDxView.copyScreen(view, name, width, height));
    }

    /** Draws an SVG document into a script bitmap. See MtaDxView.renderSvg for the result codes. */
    @SuppressWarnings("unused")
    public int onNativeRenderSvg(String name, byte[] xml, int width, int height) {
        return MtaDxView.renderSvg(name, xml, width, height);
    }

    @SuppressWarnings("unused")
    public void onNativeDxFrame(String commands) {
        if (!foreground) return;
        final String flags = debugFlags();
        if (flags.contains("nooverlay")) return;
        final String shown = flags.contains("overlayempty") ? "X" : commands;
        runOnUiThread(() -> {
            if (!foreground) return;
            syncScriptKeyboard(shown);
            if (dxOverlay != null) {
                boolean wasInteractive = dxOverlay.isCursorShown();
                dxOverlay.setFrame(shown);
                if (wasInteractive != dxOverlay.isCursorShown()) {
                    if (dxOverlay.isCursorShown()) raisePanel(dxOverlay);
                    else dxOverlay.setElevation(dp(12));
                }
                setHudRowHidden(dxOverlay.isCursorShown());
            }
        });
    }

    private String debugFlagsCache;

    /** Same file as the native WorldRPG::Debug::Flag switches (files/neon_debug_flags). */
    private String debugFlags() {
        if (debugFlagsCache != null) return debugFlagsCache;
        String value = "";
        try {
            java.io.File file = new java.io.File(getFilesDir(), "neon_debug_flags");
            if (file.isFile()) value = new String(java.nio.file.Files.readAllBytes(file.toPath()));
        } catch (Exception ignored) {
        }
        debugFlagsCache = value;
        return value;
    }

    /** Back and the cog open this panel. It does not open the GTA story pause menu. */
    private void installMtaPause() {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(0xB3000000);
        root.setVisibility(View.GONE);
        root.setClickable(true);
        // A tap on the dimmed area outside the card resumes, like Back.
        root.setOnClickListener(v -> showMtaPause(false));

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setClickable(true);
        panel.setPadding(dp(24), dp(20), dp(24), dp(20));
        GradientDrawable card = new GradientDrawable();
        card.setColor(0xF2141A22);
        card.setCornerRadius(dp(18));
        card.setStroke(dp(1), 0x33FFFFFF);
        panel.setBackground(card);

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        android.widget.ImageView cog = new android.widget.ImageView(this);
        cog.setImageResource(com.saonline.neonlauncher.R.drawable.ic_settings_hud);
        cog.setImageTintList(ColorStateList.valueOf(0xFF7FB8FF));
        header.addView(cog, new LinearLayout.LayoutParams(dp(28), dp(28)));
        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.setPadding(dp(12), 0, 0, 0);
        TextView title = new TextView(this);
        title.setText(BuildConfig.LAUNCHER_TITLE);
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        titles.addView(title);
        String server = getIntent().getStringExtra(GameActivity.EXTRA_SERVER_NAME);
        TextView subtitle = new TextView(this);
        subtitle.setText(server == null || server.isEmpty() ? "Paused" : server);
        subtitle.setTextColor(0xFFB7C3CE);
        subtitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        subtitle.setSingleLine(true);
        subtitle.setEllipsize(android.text.TextUtils.TruncateAt.END);
        titles.addView(subtitle);
        header.addView(titles, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        panel.addView(header);

        View divider = new View(this);
        divider.setBackgroundColor(0x22FFFFFF);
        LinearLayout.LayoutParams dividerParams = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(1));
        dividerParams.topMargin = dp(16);
        dividerParams.bottomMargin = dp(8);
        panel.addView(divider, dividerParams);

        panel.addView(pauseButton("Resume", com.saonline.neonlauncher.R.drawable.ic_play_hud, 0xFF2F6FD6, 0xFF2559AD,
                v -> showMtaPause(false)));
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        Button map = pauseButton("Map", 0, 0xFF243140, 0xFF31435A, v -> {
            showMtaPause(false);
            pressMapKey();
        });
        Button settings = pauseButton("Settings", com.saonline.neonlauncher.R.drawable.ic_settings_hud, 0xFF243140,
                0xFF31435A, v -> {
                    showMtaPause(false);
                    showSettings(true);
                });
        LinearLayout.LayoutParams half = new LinearLayout.LayoutParams(0, dp(48), 1f);
        half.topMargin = dp(8);
        LinearLayout.LayoutParams half2 = new LinearLayout.LayoutParams(0, dp(48), 1f);
        half2.topMargin = dp(8);
        half2.leftMargin = dp(8);
        row.addView(map, half);
        row.addView(settings, half2);
        panel.addView(row);
        panel.addView(pauseButton("Disconnect", 0, 0xFF5A1F24, 0xFF7A2A31, v -> finish()));

        FrameLayout.LayoutParams panelParams = new FrameLayout.LayoutParams(
                Math.min(dp(380), getResources().getDisplayMetrics().widthPixels - dp(32)),
                FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER);
        root.addView(panel, panelParams);
        addContentView(root, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        mtaPause = root;
    }

    private Button pauseButton(String label, int icon, int color, int pressed, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(Color.WHITE);
        button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        button.setBackground(roundedStates(color, pressed, dp(12)));
        if (icon != 0) {
            android.graphics.drawable.Drawable drawable = getDrawable(icon);
            if (drawable != null) {
                drawable = drawable.mutate();
                drawable.setTint(Color.WHITE);
                drawable.setBounds(0, 0, dp(20), dp(20));
                button.setCompoundDrawables(drawable, null, null, null);
                button.setCompoundDrawablePadding(dp(8));
            }
        }
        button.setPadding(dp(16), 0, dp(16), 0);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(48));
        params.topMargin = dp(8);
        button.setLayoutParams(params);
        button.setOnClickListener(listener);
        return button;
    }

    private static StateListDrawable roundedStates(int color, int pressed, int radius) {
        GradientDrawable normal = new GradientDrawable();
        normal.setColor(color);
        normal.setCornerRadius(radius);
        GradientDrawable down = new GradientDrawable();
        down.setColor(pressed);
        down.setCornerRadius(radius);
        StateListDrawable states = new StateListDrawable();
        states.addState(new int[] {android.R.attr.state_pressed}, down);
        states.addState(new int[] {}, normal);
        return states;
    }

    /** Background of the on-screen HUD buttons (cog, Chat, key list). */
    private StateListDrawable hudButtonBackground() {
        return roundedStates(0xCC101820, 0xEE2A3646, dp(8));
    }

    // Desktop F11 (CKeyBinds.cpp:238): the key goes to the script binds, and the player map toggles
    // only while the "radar" control is enabled. A script that draws its own map disables the
    // control and binds F11 itself. The radar tap, the pause-menu Map button and the hardware F11
    // all come here.
    private void pressMapKey() {
        Log.i(TAG, "map key: clientReady=" + clientReady + " radarControl=" + radarControlEnabled);
        if (clientReady) {
            nativeKeyEvent("F11", true);
            mapHandler.postDelayed(() -> nativeKeyEvent("F11", false), 80);
        }
        if (radarControlEnabled) showPlayerMap(!isPlayerMapShowing());
    }

    private boolean isPlayerMapShowing() {
        return playerMap != null && playerMap.getTag() instanceof View
                && ((View) playerMap.getTag()).getVisibility() == View.VISIBLE;
    }

    // A transparent view over the script's radar. While a script shows the cursor the touch goes on
    // to the DX overlay, so the radar tap never opens the map over another panel.
    private void installRadarZone() {
        View zone = new View(this);
        zone.setVisibility(View.GONE);
        zone.setOnTouchListener(new View.OnTouchListener() {
            private float downX, downY;
            private long downAt;

            @Override
            public boolean onTouch(View v, android.view.MotionEvent event) {
                if (event.getActionMasked() == android.view.MotionEvent.ACTION_DOWN) {
                    Log.i(TAG, "radar zone touch, cursor=" + (dxOverlay != null && dxOverlay.isCursorShown()));
                }
                if (dxOverlay != null && dxOverlay.isCursorShown()) return false;
                switch (event.getActionMasked()) {
                    case android.view.MotionEvent.ACTION_DOWN:
                        downX = event.getRawX();
                        downY = event.getRawY();
                        downAt = event.getEventTime();
                        return true;
                    case android.view.MotionEvent.ACTION_UP:
                        float slop = android.view.ViewConfiguration.get(v.getContext()).getScaledTouchSlop();
                        if (Math.abs(event.getRawX() - downX) <= slop && Math.abs(event.getRawY() - downY) <= slop
                                && event.getEventTime() - downAt <= 500) {
                            pressMapKey();
                        }
                        return true;
                    default:
                        return true;
                }
            }
        });
        addContentView(zone, new FrameLayout.LayoutParams(0, 0, Gravity.TOP | Gravity.START));
        radarZone = zone;
    }

    /** "x|y|w|h" in DX overlay pixels, or empty to remove the zone. */
    private void setRadarArea(String text) {
        Log.i(TAG, "radar area: \"" + text + "\" zone=" + (radarZone != null));
        if (radarZone == null) return;
        String[] parts = text == null ? new String[0] : text.split("\\|");
        if (parts.length != 4) {
            radarZone.setVisibility(View.GONE);
            return;
        }
        try {
            float x = Float.parseFloat(parts[0]);
            float y = Float.parseFloat(parts[1]);
            float w = Float.parseFloat(parts[2]);
            float h = Float.parseFloat(parts[3]);
            FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) radarZone.getLayoutParams();
            params.leftMargin = Math.round(x);
            params.topMargin = Math.round(y);
            params.width = Math.max(1, Math.round(w));
            params.height = Math.max(1, Math.round(h));
            radarZone.setLayoutParams(params);
            radarZone.setVisibility(View.VISIBLE);
        } catch (NumberFormatException ignored) {
            radarZone.setVisibility(View.GONE);
        }
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void installPlayerMap() {
        FrameLayout container = new FrameLayout(this);
        container.setVisibility(View.GONE);
        playerMap = new PlayerMapView(this, new PlayerMapView.Host() {
            @Override
            public String snapshot() {
                try {
                    return nativeMapSnapshot();
                } catch (UnsatisfiedLinkError error) {
                    return "";
                }
            }

            @Override
            public void onClose() {
                showPlayerMap(false);
            }
        });
        container.addView(playerMap, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        Button close = new Button(this);
        close.setText("Close");
        close.setAllCaps(false);
        close.setOnClickListener(v -> showPlayerMap(false));
        FrameLayout.LayoutParams closeParams = new FrameLayout.LayoutParams(dp(120), dp(48), Gravity.TOP | Gravity.END);
        closeParams.topMargin = dp(16);
        closeParams.rightMargin = dp(16);
        container.addView(close, closeParams);
        addContentView(container, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        playerMap.setTag(container);
    }

    private void showPlayerMap(boolean show) {
        if (playerMap == null) return;
        Object tag = playerMap.getTag();
        if (tag instanceof View) {
            ((View) tag).setVisibility(show ? View.VISIBLE : View.GONE);
            if (show) raisePanel((View) tag);
        }
        mapHandler.removeCallbacks(mapRefresh);
        if (show) mapHandler.post(mapRefresh);
    }

    private final GameSettingsView.Host settingsHost = new GameSettingsView.Host() {
        @Override public int[] get(int index) {
            try {
                return nativeGetGameSetting(index);
            } catch (UnsatisfiedLinkError error) {
                return null;
            }
        }

        @Override public boolean set(int index, int value) {
            try {
                return nativeSetGameSetting(index, value);
            } catch (UnsatisfiedLinkError error) {
                return false;
            }
        }

        @Override public void setChatShown(boolean shown) {
            if (chatLog != null) chatLog.setChatVisible(shown);
        }

        @Override public void setHudLayout(int chat, int buttons, int size, boolean touch) {
            chatAnchor = chat;
            buttonAnchor = buttons;
            chatPercent = size;
            chatTouch = touch;
            layoutHud();
        }

        @Override public int getInterfaceScalePercent() {
            return interfaceScalePercent;
        }

        @Override public boolean setInterfaceScalePercent(int percent) {
            if (!InterfaceScaleSettings.save(EngineHostActivity.this, percent)) return false;
            interfaceScalePercent = InterfaceScale.clampPercent(percent);
            if (dxOverlay != null) reportDxScreen(dxOverlay.getWidth(), dxOverlay.getHeight());
            return true;
        }

        @Override public void onClose() {
            showSettings(false);
            showMtaPause(true);
        }
    };

    private void installSettings() {
        settingsView = new GameSettingsView(this, settingsHost);
        settingsView.setVisibility(View.GONE);
        addContentView(settingsView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
    }

    private void showSettings(boolean show) {
        if (settingsView == null) return;
        if (show) settingsView.refresh();
        settingsView.setVisibility(show ? View.VISIBLE : View.GONE);
        if (show) raisePanel(settingsView);
    }

    private void applySavedSettings() {
        GameSettingsView.applySaved(this, settingsHost);
    }

    private void showMtaPause(boolean show) {
        if (mtaPause == null) return;
        mtaPause.setVisibility(show ? View.VISIBLE : View.GONE);
        if (show) raisePanel(mtaPause);
    }

    // A single elevation band for panels. Draw and touch order follow the
    // most recent opening without promoting the DX view on every frame.
    private void raisePanel(View panel) {
        panel.setElevation(dp(PANEL_ELEVATION_DP));
        panel.bringToFront();
    }

    private void toggleMtaPause() {
        if (mtaPause == null) {
            finish();
            return;
        }
        showMtaPause(mtaPause.getVisibility() != View.VISIBLE);
    }

    private void validateGameText(boolean usesCdn) throws IOException {
        // GTA 2.10's CText::Load reads the result of OpenFile without checking
        // for a null handle. Check the same external and packaged text table here.
        final String path = "Text/american.gxt";
        try (InputStream text = EngineAssetLoader.open(this, gameDirectory, path)) {
            if (text.read() < 0) throw new IOException("Empty text table");
        } catch (IOException error) {
            String action = usesCdn ? " Run Sync game assets in Settings." : " Add it to the game folder.";
            throw new IOException("Game text table is missing or empty (" + path + ")." + action, error);
        }
    }

    private static void validateLevelDefinitions(File root) throws IOException {
        // The native loader dereferences null file handles. Check each required
        // map definition with its exact spelling before transferring control.
        for (String level : new String[]{"data/default.dat", "data/gta.dat"}) {
            try (java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader(new File(root, level)))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    String[] fields = line.trim().split("\\s+", 2);
                    if (fields.length != 2 || (!fields[0].equals("IDE") && !fields[0].equals("IPL"))) continue;
                    String path = fields[1].trim().replace('\\', '/');
                    File definition = EngineCaseAliases.resolveReference(root, path);
                    if (!definition.getCanonicalPath().startsWith(root.getPath() + File.separator)
                            || !definition.isFile()) {
                        throw new IOException("Arquivo de mapa indisponivel: " + path);
                    }
                }
            }
        }
    }

    @Override
    public String GetGameBaseDirectory() {
        return gameDirectory == null ? getFilesDir().getAbsolutePath() + File.separator : gameDirectory;
    }

    @Override
    public boolean shouldStartEngine() {
        return bootError == null && gameDirectory != null;
    }

    @Override
    protected void onEngineBootstrapError(String message) {
        bootError = message;
        EngineDiagnostics.failure(this, message);
        Log.e(TAG, message);
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            setResult(RESULT_CANCELED, new Intent().putExtra(EXTRA_BOOT_ERROR, message));
            finishAfterEngineBootstrapError();
        });
    }

    @Override
    public void onDestroy() {
        if (disconnectDialog != null) {
            disconnectDialog.dismiss();
            disconnectDialog = null;
        }
        if (identityUi != null) identityUi.close();
        releaseWifiLock();
        for (Integer id : new java.util.ArrayList<>(sounds.keySet())) stopUrlSound(Integer.toString(id));
        if (transferBox != null) transferBox.setProgress(0, 0);
        if (loadingView != null) loadingView.end();
        netRunning = false;
        if (netThread != null) netThread.interrupt();
        try {
            nativeDestroy();
        } catch (UnsatisfiedLinkError ignored) {
            // The session library was never loaded.
        }
        super.onDestroy();
        if (!isChangingConfigurations()) {
            EngineDiagnostics.end(this);
            // GTA owns process-wide singletons that survive JNI cleanup. This
            // Activity runs in :engine, so a later launch gets a fresh engine
            // while the launcher and its downloads remain alive.
            Log.i(TAG, "Retiring isolated GTA process");
            new android.os.Handler(android.os.Looper.getMainLooper()).post(
                    () -> android.os.Process.killProcess(android.os.Process.myPid()));
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_BACK) {
            if (playerMap != null && playerMap.getTag() instanceof View
                    && ((View) playerMap.getTag()).getVisibility() == View.VISIBLE) {
                showPlayerMap(false);
                return true;
            }
            if (settingsView != null && settingsView.getVisibility() == View.VISIBLE) {
                showSettings(false);
                showMtaPause(true);
                return true;
            }
            toggleMtaPause();
            return true;
        }
        // A hardware keyboard key for bindKey and onClientKey. Held-key repeats are not new presses.
        String key = mtaKeyName(keyCode);
        if (key != null && event.getRepeatCount() == 0) nativeKeyEvent(key, true);
        if (keyCode == android.view.KeyEvent.KEYCODE_F11 && event.getRepeatCount() == 0 && radarControlEnabled) {
            showPlayerMap(!isPlayerMapShowing());
        }
        return super.onKeyDown(keyCode, event);
    }

    /** Desktop key-bind name (CScriptKeyBinds.cpp g_bkKeys) for an Android key code, or null. */
    private static String mtaKeyName(int keyCode) {
        if (keyCode >= android.view.KeyEvent.KEYCODE_A && keyCode <= android.view.KeyEvent.KEYCODE_Z) {
            return String.valueOf((char) ('a' + (keyCode - android.view.KeyEvent.KEYCODE_A)));
        }
        if (keyCode >= android.view.KeyEvent.KEYCODE_0 && keyCode <= android.view.KeyEvent.KEYCODE_9) {
            return String.valueOf((char) ('0' + (keyCode - android.view.KeyEvent.KEYCODE_0)));
        }
        if (keyCode >= android.view.KeyEvent.KEYCODE_F1 && keyCode <= android.view.KeyEvent.KEYCODE_F12) {
            // F8 is the desktop console key and is not bindable.
            if (keyCode == android.view.KeyEvent.KEYCODE_F8) return null;
            return "F" + (1 + keyCode - android.view.KeyEvent.KEYCODE_F1);
        }
        if (keyCode >= android.view.KeyEvent.KEYCODE_NUMPAD_0 && keyCode <= android.view.KeyEvent.KEYCODE_NUMPAD_9) {
            return "num_" + (keyCode - android.view.KeyEvent.KEYCODE_NUMPAD_0);
        }
        switch (keyCode) {
            case android.view.KeyEvent.KEYCODE_SPACE: return "space";
            case android.view.KeyEvent.KEYCODE_ENTER: return "enter";
            case android.view.KeyEvent.KEYCODE_TAB: return "tab";
            case android.view.KeyEvent.KEYCODE_DEL: return "backspace";
            case android.view.KeyEvent.KEYCODE_FORWARD_DEL: return "delete";
            case android.view.KeyEvent.KEYCODE_DPAD_LEFT: return "arrow_l";
            case android.view.KeyEvent.KEYCODE_DPAD_UP: return "arrow_u";
            case android.view.KeyEvent.KEYCODE_DPAD_RIGHT: return "arrow_r";
            case android.view.KeyEvent.KEYCODE_DPAD_DOWN: return "arrow_d";
            case android.view.KeyEvent.KEYCODE_SHIFT_LEFT: return "lshift";
            case android.view.KeyEvent.KEYCODE_SHIFT_RIGHT: return "rshift";
            case android.view.KeyEvent.KEYCODE_CTRL_LEFT: return "lctrl";
            case android.view.KeyEvent.KEYCODE_CTRL_RIGHT: return "rctrl";
            case android.view.KeyEvent.KEYCODE_ALT_LEFT: return "lalt";
            case android.view.KeyEvent.KEYCODE_ALT_RIGHT: return "ralt";
            case android.view.KeyEvent.KEYCODE_PAGE_UP: return "pgup";
            case android.view.KeyEvent.KEYCODE_PAGE_DOWN: return "pgdn";
            case android.view.KeyEvent.KEYCODE_MOVE_HOME: return "home";
            case android.view.KeyEvent.KEYCODE_MOVE_END: return "end";
            case android.view.KeyEvent.KEYCODE_INSERT: return "insert";
            default: return null;
        }
    }

    @Override
    public boolean onKeyUp(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_BACK) return true;
        String key = mtaKeyName(keyCode);
        if (key != null) nativeKeyEvent(key, false);
        return super.onKeyUp(keyCode, event);
    }

    @Override
    public void onBackPressed() {
        toggleMtaPause();
    }
}
