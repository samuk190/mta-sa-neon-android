package com.worldrpg.neonlauncher.game;

import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.util.TypedValue;
import android.os.Build;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;
import com.worldrpg.neonlauncher.BuildConfig;
import com.worldrpg.neonlauncher.utils.PreferencesManager;
import com.rockstargames.gtasa.GTASA;
import com.worldrpg.engine.EngineCaseAliases;
import com.worldrpg.neonlauncher.utils.NativeAssetBridge;
import java.io.File;
import java.io.IOException;

/** Debug checkpoint for the native GTA Activity, Surface and EGL lifecycle. */
public final class EngineHostActivity extends GTASA {
    public static final String EXTRA_BOOT_ERROR = "engine_boot_error";
    private static final String TAG = "NeonEngineHost";
    private String gameDirectory;
    private String bootError;
    private View mtaPause;
    private View downloadPanel;
    private ProgressBar downloadBar;
    private TextView downloadLabel;
    private MtaChatView chatLog;
    private MtaDxView dxOverlay;
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
    private String connectIp = "";
    private int connectPort = 0;
    private String connectNick = "";
    private boolean joinedOnce = false;
    private DisconnectDialog disconnectDialog;
    private volatile boolean netRunning;
    private Thread netThread;

    private native boolean nativeInit();
    private native boolean nativeSetAssetPath(String assetPath);
    private native boolean nativeConnect(String ip, int port, String nickname);
    private native void nativePulse();
    private native void nativeDestroy();
    private native void nativeSetScreenSize(int width, int height);
    private native void nativeCursorEvent(int action, float x, float y);
    private native boolean nativeSendChat(String text);
    private native void nativeDropConnection();
    private native String nativeMapSnapshot();

    @Override
    public void onCreate(Bundle savedInstanceState) {
        try {
            String installed = new NativeAssetBridge(this).getReadyStoragePath();
            if (installed == null) throw new IOException("Sincronize os arquivos do jogo antes de testar o GTA.");
            File generation = new File(installed).getCanonicalFile();
            gameDirectory = generation.getPath() + File.separator;
            EngineCaseAliases.prepare(generation);
            validateLevelDefinitions(generation);
            // The VM calls the engine's JNI_OnLoad and registers the native
            // NvEventQueueActivity methods. A plain dlopen does not do this.
            System.loadLibrary("GTASA");
            // Loaded here, before the engine thread exists: its JNI_OnLoad gives libGTASA a heap inside one
            // 2 GB window (GameHeap.cpp). startMtaSession loads it again, which is then a no-op.
            try {
                System.loadLibrary("WorldRPGNeon");
            } catch (UnsatisfiedLinkError error) {
                Log.e(TAG, "MTA client library failed to load before the engine", error);
            }
            Log.i(TAG, "Native GTA host prepared with generation " + generation.getName());
        } catch (IOException | LinkageError | SecurityException error) {
            bootError = error.getMessage() == null ? "Nao foi possivel preparar a engine GTA." : error.getMessage();
            setResult(RESULT_CANCELED, new Intent().putExtra(EXTRA_BOOT_ERROR, bootError));
            Log.e(TAG, "Native GTA host preparation failed", error);
        }
        // The base class still receives Android's lifecycle after a failed
        // preparation. shouldStartEngine then refuses native initialization.
        super.onCreate(savedInstanceState);
        if (bootError == null && gameDirectory != null) {
            installMtaPause();
            installPlayerMap();
            installTransferAndChat();
        }
    }

    @Override
    protected void onEngineSurfaceReady(boolean ready) {
        Log.i(TAG, "Native GTA surface ready=" + ready);
        super.onEngineSurfaceReady(ready);
        if (ready) startMtaSession();
    }

    private void startMtaSession() {
        String ip = getIntent().getStringExtra(GameActivity.EXTRA_SERVER_IP);
        if (ip == null || ip.isEmpty()) ip = BuildConfig.DEFAULT_SERVER_IP;
        if (ip == null || ip.isEmpty() || netRunning) return;
        int port = getIntent().getIntExtra(GameActivity.EXTRA_SERVER_PORT, BuildConfig.DEFAULT_SERVER_PORT);
        try {
            // Same directory the :engine linker searches for DT_NEEDED.
// libc++ is linked statically into WorldRPGNeon (c++_static); no shared runtime to preload.
            System.loadLibrary("WorldRPGNeon");
            Log.i(TAG, "MTA client library loaded");
        } catch (UnsatisfiedLinkError error) {
            Log.e(TAG, "MTA client library failed to load", error);
            return;
        }
        if (!nativeInit() || !nativeSetAssetPath(gameDirectory)) {
            Log.e(TAG, "MTA client failed to start");
            return;
        }
        android.util.DisplayMetrics metrics = getResources().getDisplayMetrics();
        nativeSetScreenSize(metrics.widthPixels, metrics.heightPixels);
        String nick = new PreferencesManager(this).getUsername();
        if (nick == null || nick.isEmpty()) nick = BuildConfig.DEFAULT_PLAYER_NICK;
        connectIp = ip;
        connectPort = port;
        connectNick = nick;
        if (!nativeConnect(ip, port, nick)) {
            Log.e(TAG, "MTA connect failed to start");
            return;
        }
        showDownload(0, "Connecting to server");
        netRunning = true;
        netThread = new Thread(() -> {
            while (netRunning) {
                nativePulse();
                try {
                    Thread.sleep(16);
                } catch (InterruptedException ignored) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }, "mta-net");
        netThread.start();
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

        android.util.DisplayMetrics metrics = getResources().getDisplayMetrics();
        int chatWidth = Math.max(dp(280), (int) (metrics.widthPixels * 0.44f));
        float line = Math.max(22f, metrics.heightPixels / 720f * 22f);
        chatLog = new MtaChatView(this);
        chatLog.setVisibility(View.GONE);
        FrameLayout.LayoutParams chatParams = new FrameLayout.LayoutParams(
                chatWidth, (int) (line * 10 + dp(20)), Gravity.TOP | Gravity.START);
        chatParams.leftMargin = Math.max(dp(12), (int) (metrics.widthPixels * 0.02f));
        chatParams.topMargin = Math.max(dp(12), (int) (metrics.heightPixels * 0.03f));
        addContentView(chatLog, chatParams);

        dxOverlay = new MtaDxView(this);
        dxOverlay.setVisibility(View.GONE);
        dxOverlay.setCursorListener(this::nativeCursorEvent);
        addContentView(dxOverlay, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        chatLog.setOnClickListener(v -> openChatInput());
        Button chatButton = new Button(this);
        chatButton.setText("Chat");
        chatButton.setTextColor(Color.WHITE);
        chatButton.setBackgroundColor(0xCC101820);
        chatButton.setOnClickListener(v -> openChatInput());
        FrameLayout.LayoutParams buttonParams = new FrameLayout.LayoutParams(dp(88), dp(40), Gravity.BOTTOM | Gravity.START);
        buttonParams.leftMargin = chatParams.leftMargin;
        buttonParams.bottomMargin = dp(24);
        addContentView(chatButton, buttonParams);

        chatInput = new EditText(this);
        chatInput.setHint("Say:");
        chatInput.setSingleLine(true);
        chatInput.setTextColor(Color.WHITE);
        chatInput.setHintTextColor(0xFFACD5FE);
        chatInput.setImeOptions(EditorInfo.IME_ACTION_SEND);
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
    }

    private void openChatInput() {
        if (chatInputBar == null || chatInput == null) return;
        chatInputBar.setVisibility(View.VISIBLE);
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
        runOnUiThread(() -> {
            if (connected) {
                joinedOnce = true;
                appendChat("#ACD5FEConnected to the server.");
            } else {
                hideDownload();
                String text = message == null || message.isEmpty() ? "Disconnected." : message;
                appendChat("#FF6666" + text);
                // Only a drop after a join offers Retry / Quit (P3.1).
                if (joinedOnce) {
                    joinedOnce = false;
                    showDisconnectDialog(text);
                }
            }
        });
    }

    private void showDisconnectDialog(String message) {
        if (isFinishing() || disconnectDialog != null) return;
        disconnectDialog = new DisconnectDialog(this, message, new DisconnectDialog.Actions() {
            @Override public void onRetry() {
                disconnectDialog = null;
                if (!nativeConnect(connectIp, connectPort, connectNick)) {
                    appendChat("#FF6666Falha ao reconectar.");
                    showDisconnectDialog("Falha ao reconectar.");
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
                showPlayerMap(true);
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

    @SuppressWarnings("unused")
    public void onNativeDxFrame(String commands) {
        final String flags = debugFlags();
        if (flags.contains("nooverlay")) return;
        final String shown = flags.contains("overlayempty") ? "X" : commands;
        runOnUiThread(() -> {
            if (dxOverlay != null) dxOverlay.setFrame(shown);
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

    /** Back opens this panel. It does not open the GTA story pause menu. */
    private void installMtaPause() {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(0xCC101418);
        root.setVisibility(View.GONE);
        root.setClickable(true);

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(Gravity.CENTER_HORIZONTAL);
        int pad = dp(24);
        panel.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText(BuildConfig.LAUNCHER_TITLE);
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 28);
        title.setGravity(Gravity.CENTER);
        panel.addView(title);

        String server = getIntent().getStringExtra(GameActivity.EXTRA_SERVER_NAME);
        TextView subtitle = new TextView(this);
        subtitle.setText(server == null || server.isEmpty() ? "Pause" : server);
        subtitle.setTextColor(0xFFB7C3CE);
        subtitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        subtitle.setGravity(Gravity.CENTER);
        subtitle.setPadding(0, dp(6), 0, dp(18));
        panel.addView(subtitle);

        panel.addView(pauseButton("Resume", v -> showMtaPause(false)));
        panel.addView(pauseButton("Map", v -> {
            showMtaPause(false);
            showPlayerMap(true);
        }));
        panel.addView(pauseButton("Settings", v -> Toast.makeText(this, "Ajustes ainda nao ligados.", Toast.LENGTH_SHORT).show()));
        panel.addView(pauseButton("Disconnect", v -> finish()));

        FrameLayout.LayoutParams panelParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER);
        root.addView(panel, panelParams);
        addContentView(root, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        mtaPause = root;
    }

    private Button pauseButton(String label, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(Color.WHITE);
        button.setBackgroundColor(0xFF243140);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(280), dp(48));
        params.topMargin = dp(8);
        button.setLayoutParams(params);
        button.setOnClickListener(listener);
        return button;
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
        if (tag instanceof View) ((View) tag).setVisibility(show ? View.VISIBLE : View.GONE);
        mapHandler.removeCallbacks(mapRefresh);
        if (show) mapHandler.post(mapRefresh);
    }

    private void showMtaPause(boolean show) {
        if (mtaPause != null) mtaPause.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    private void toggleMtaPause() {
        if (mtaPause == null) {
            finish();
            return;
        }
        showMtaPause(mtaPause.getVisibility() != View.VISIBLE);
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
        Log.e(TAG, message);
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            setResult(RESULT_CANCELED, new Intent().putExtra(EXTRA_BOOT_ERROR, message));
            finishAfterEngineBootstrapError();
        });
    }

    @Override
    public void onDestroy() {
        netRunning = false;
        if (netThread != null) netThread.interrupt();
        try {
            nativeDestroy();
        } catch (UnsatisfiedLinkError ignored) {
            // The session library was never loaded.
        }
        super.onDestroy();
        if (!isChangingConfigurations()) {
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
            toggleMtaPause();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_BACK) return true;
        return super.onKeyUp(keyCode, event);
    }

    @Override
    public void onBackPressed() {
        toggleMtaPause();
    }
}
