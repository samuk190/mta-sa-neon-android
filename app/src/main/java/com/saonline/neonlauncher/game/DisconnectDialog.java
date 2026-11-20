package com.saonline.neonlauncher.game;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewGroup.LayoutParams;
import android.view.Window;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import com.saonline.neonlauncher.BuildConfig;
import com.saonline.neonlauncher.R;
import org.json.JSONObject;

/**
 * The error popup for every refusal, timeout and drop, with Reconnect and Close (P3.1).
 * Title and text are the desktop error box (net/DisconnectMessages, CCore::ShowErrorMessageBox);
 * the bundled assets/ui/disconnect.html draws it as the Neon desktop web menu's system modal
 * (Tools/server-browser-prototype NativeMessageModal + index.css .modal--system.modal--error).
 * A server page (UI_DISCONNECT_URL) replaces it when set; it receives the same setDialog call.
 */
final class DisconnectDialog {
    interface Actions {
        void onRetry();
        void onQuit();
    }

    private static final String BUNDLED = "file:///android_asset/ui/disconnect.html";

    private final Activity activity;
    private final String title;
    private final String message;
    private final String gameDirectory;
    private final Actions actions;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private Dialog dialog;
    private FrameLayout content;
    private WebView web;
    private boolean fellBack = false;

    DisconnectDialog(Activity activity, String title, String message, String gameDirectory, Actions actions) {
        this.activity = activity;
        this.title = title;
        this.message = message;
        this.gameDirectory = gameDirectory;
        this.actions = actions;
    }

    void show() {
        content = new FrameLayout(activity);
        content.setBackgroundColor(Color.BLACK);
        ViewCompat.setOnApplyWindowInsetsListener(content, (view, insets) -> {
            // The backdrop covers the cutout. Keep the page and buttons in the safe area.
            Insets safe = insets.getInsets(WindowInsetsCompat.Type.displayCutout() | WindowInsetsCompat.Type.systemBars());
            view.setPadding(safe.left, safe.top, safe.right, safe.bottom);
            return WindowInsetsCompat.CONSUMED;
        });
        dialog = new Dialog(activity, R.style.Theme_WorldRPGNeon_Disconnect);
        dialog.setCancelable(false);
        dialog.setContentView(content, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        Window window = dialog.getWindow();
        WindowCompat.setDecorFitsSystemWindows(window, false);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams attrs = window.getAttributes();
            attrs.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                    ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                    : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            window.setAttributes(attrs);
        }
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN
                | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
        try {
            loadPage();
        } catch (RuntimeException error) {
            Log.w("DisconnectDialog", "Could not initialize the error page", error);
            showNativeActions();
        }
        dialog.show();
        window.setLayout(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
        WindowInsetsControllerCompat controller = WindowCompat.getInsetsController(window, content);
        controller.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        controller.hide(WindowInsetsCompat.Type.systemBars() | WindowInsetsCompat.Type.ime());
        ViewCompat.requestApplyInsets(content);
    }

    private void loadPage() {
        web = new WebView(activity);
        web.setBackgroundColor(Color.TRANSPARENT);
        web.getSettings().setJavaScriptEnabled(true);
        // The desktop modal font (SA Bank Gothic, MTA/cgui/sabankgothic.ttf) is read from the game folder.
        web.getSettings().setAllowFileAccess(true);
        web.addJavascriptInterface(new Bridge(), "Neon");
        web.setWebViewClient(new WebViewClient() {
            @Override
            public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                if (request.isForMainFrame()) handlePageFailure(view);
            }

            @Override
            public void onReceivedHttpError(WebView view, WebResourceRequest request, WebResourceResponse response) {
                if (request.isForMainFrame()) handlePageFailure(view);
            }

            @Override
            public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
                if (view == web) showNativeActions();
                return true;
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                if (view != web) return;
                java.io.File font = gameDirectory == null ? null
                        : new java.io.File(gameDirectory, "MTA/cgui/sabankgothic.ttf");
                String fontUrl = font != null && font.isFile() ? "file://" + font.getAbsolutePath() : "";
                view.evaluateJavascript("if (window.setDialog) window.setDialog(" + quote(title) + "," + quote(message)
                        + "," + quote(fontUrl) + "); else if (window.setMessage) window.setMessage(" + quote(message) + ");",
                        null);
            }
        });

        String url = BuildConfig.UI_DISCONNECT_URL;
        fellBack = url == null || url.isBlank() || BUNDLED.equals(url);
        content.addView(web, new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
        web.loadUrl(fellBack ? BUNDLED : url);
    }

    private void handlePageFailure(WebView view) {
        if (view != web) return;
        if (fellBack) {
            showNativeActions();
        } else {
            fellBack = true;
            view.loadUrl(BUNDLED);
        }
    }

    private void releaseWebView() {
        if (web == null) return;
        WebView old = web;
        web = null;
        content.removeView(old);
        old.destroy();
    }

    private void showNativeActions() {
        releaseWebView();
        LinearLayout panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        int pad = Math.round(24 * activity.getResources().getDisplayMetrics().density);
        panel.setPadding(pad, pad, pad, pad);
        TextView heading = new TextView(activity);
        heading.setText(title);
        heading.setTextSize(24);
        panel.addView(heading);
        ScrollView scroll = new ScrollView(activity);
        scroll.setFillViewport(true);
        TextView text = new TextView(activity);
        text.setText(message);
        text.setGravity(Gravity.CENTER_VERTICAL);
        scroll.addView(text);
        panel.addView(scroll, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 0, 1));
        LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.END);
        Button close = new Button(activity);
        close.setText(R.string.disconnect_close);
        close.setOnClickListener(v -> finish(false));
        row.addView(close);
        Button retry = new Button(activity);
        retry.setText(R.string.disconnect_retry);
        retry.setOnClickListener(v -> finish(true));
        row.addView(retry);
        panel.addView(row);
        content.addView(panel, new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
    }

    void dismiss() {
        if (dialog == null) return;
        Dialog old = dialog;
        dialog = null;
        releaseWebView();
        old.dismiss();
        content = null;
    }

    private void finish(boolean retry) {
        if (dialog == null || activity.isFinishing() || activity.isDestroyed()) return;
        dismiss();
        if (retry) actions.onRetry();
        else actions.onQuit();
    }

    private static String quote(String text) {
        return JSONObject.quote(text == null ? "" : text);
    }

    private final class Bridge {
        @JavascriptInterface
        public void retry() {
            mainHandler.post(() -> finish(true));
        }

        @JavascriptInterface
        public void quit() {
            mainHandler.post(() -> finish(false));
        }
    }
}
