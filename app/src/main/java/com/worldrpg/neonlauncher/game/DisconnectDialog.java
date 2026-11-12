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
 * Connection-lost dialog with Retry and Quit (P3.1). The markup is the desktop
 * style: a web view loaded from the server (UI_DISCONNECT_URL), with the bundled
 * assets/ui/disconnect.html as the fallback.
 */
final class DisconnectDialog {
    interface Actions {
        void onRetry();
        void onQuit();
    }

    private static final String BUNDLED = "file:///android_asset/ui/disconnect.html";

    private final Activity activity;
    private final String message;
    private final Actions actions;
    private Dialog dialog;
    private boolean fellBack = false;

    DisconnectDialog(Activity activity, String message, Actions actions) {
        this.activity = activity;
        this.message = message;
        this.actions = actions;
    }

    void show() {
        WebView web = new WebView(activity);
        web.setBackgroundColor(Color.TRANSPARENT);
        web.getSettings().setJavaScriptEnabled(true);
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
                view.evaluateJavascript("if (window.setMessage) window.setMessage(" + quote(message) + ");", null);
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
            dialog.getWindow().setLayout(android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                    android.view.ViewGroup.LayoutParams.MATCH_PARENT);
        }
    }

    private void dismiss() {
        if (dialog != null && dialog.isShowing()) dialog.dismiss();
        dialog = null;
    }

    private static String quote(String text) {
        String safe = text == null ? "" : text;
        return "\"" + safe.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", " ") + "\"";
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
