package com.worldrpg.neonlauncher.game;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.os.Handler;
import android.os.Looper;
import android.view.Window;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import com.worldrpg.neonlauncher.BuildConfig;

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
    private Dialog dialog;
    private boolean fellBack = false;

    DisconnectDialog(Activity activity, String title, String message, String gameDirectory, Actions actions) {
        this.activity = activity;
        this.title = title;
        this.message = message;
        this.gameDirectory = gameDirectory;
        this.actions = actions;
    }

    void show() {
        WebView web = new WebView(activity);
        web.setBackgroundColor(Color.TRANSPARENT);
        web.getSettings().setJavaScriptEnabled(true);
        // The desktop modal font (SA Bank Gothic, MTA/cgui/sabankgothic.ttf) is read from the game folder.
        web.getSettings().setAllowFileAccess(true);
        web.addJavascriptInterface(new Bridge(), "Neon");
        web.setWebViewClient(new WebViewClient() {
            @Override
            public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                if (request.isForMainFrame() && !fellBack) {
                    fellBack = true;
                    view.loadUrl(BUNDLED);
                }
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                java.io.File font = gameDirectory == null ? null
                        : new java.io.File(gameDirectory, "MTA/cgui/sabankgothic.ttf");
                String fontUrl = font != null && font.isFile() ? "file://" + font.getAbsolutePath() : "";
                view.evaluateJavascript("if (window.setDialog) window.setDialog(" + quote(title) + "," + quote(message)
                        + "," + quote(fontUrl) + "); else if (window.setMessage) window.setMessage(" + quote(message) + ");",
                        null);
            }
        });

        String url = BuildConfig.UI_DISCONNECT_URL;
        web.loadUrl(url == null || url.isEmpty() ? BUNDLED : url);

        dialog = new Dialog(activity);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialog.setCancelable(false);
        dialog.setContentView(web);
        dialog.show();
        if (dialog.getWindow() != null) {
            Window window = dialog.getWindow();
            // Full screen, cutout included: the page draws its own dimmed backdrop.
            window.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
                android.view.WindowManager.LayoutParams attrs = window.getAttributes();
                attrs.layoutInDisplayCutoutMode =
                        android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                window.setAttributes(attrs);
            }
            window.setLayout(android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                    android.view.ViewGroup.LayoutParams.MATCH_PARENT);
        }
    }

    private void dismiss() {
        if (dialog != null && dialog.isShowing()) dialog.dismiss();
        dialog = null;
    }

    private static String quote(String text) {
        String safe = text == null ? "" : text;
        return "\"" + safe.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n") + "\"";
    }

    private final class Bridge {
        @JavascriptInterface
        public void retry() {
            new Handler(Looper.getMainLooper()).post(() -> {
                dismiss();
                actions.onRetry();
            });
        }

        @JavascriptInterface
        public void quit() {
            new Handler(Looper.getMainLooper()).post(() -> {
                dismiss();
                actions.onQuit();
            });
        }
    }
}
