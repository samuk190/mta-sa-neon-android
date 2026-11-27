package com.saonline.neonlauncher.game;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.io.File;
import java.io.FileInputStream;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Offscreen Android WebView backend for MTA CLuaBrowserDefs (createBrowser, guiCreateBrowser).
 * Renders each active WebView into MtaDxView.MEMORY under key "tx:<browserId>" so dxDrawImage,
 * dxDrawImageSection, and gui-browser elements display live web content.
 */
final class BrowserEngine {
    private static final String TAG = "NeonBrowser";
    private static final long CAPTURE_INTERVAL_MS = 33L;

    interface Callbacks {
        void onLoadingStart(int id, String url, boolean mainFrame);
        void onDocumentReady(int id, String url, String title, boolean canBack, boolean canForward);
        void onLoadingFailed(int id, String url, int errorCode, String description);
        void onSourceReady(int id, String html);
    }

    private static final class Slot {
        final int id;
        final WebView webView;
        int width;
        int height;
        boolean paused;
        int lastMouseX;
        int lastMouseY;
        boolean mouseDown;
        long downTimeMs;

        Slot(int id, WebView webView, int width, int height) {
            this.id = id;
            this.webView = webView;
            this.width = width;
            this.height = height;
        }
    }

    private final Activity activity;
    private final File resourceCacheRoot;
    private final Callbacks callbacks;
    private final Handler uiHandler = new Handler(Looper.getMainLooper());
    private final Map<Integer, Slot> slots = new HashMap<>();
    private boolean tickerRunning = false;
    private boolean released = false;

    private final Runnable captureTick = new Runnable() {
        @Override
        public void run() {
            if (released || slots.isEmpty()) {
                tickerRunning = false;
                return;
            }
            for (Slot slot : slots.values()) {
                if (!slot.paused) captureSlot(slot);
            }
            uiHandler.postDelayed(this, CAPTURE_INTERVAL_MS);
        }
    };

    BrowserEngine(Activity activity, File resourceCacheRoot, Callbacks callbacks) {
        this.activity = activity;
        this.resourceCacheRoot = resourceCacheRoot;
        this.callbacks = callbacks;
    }

    void handleCommand(String raw) {
        if (raw == null || raw.isEmpty() || released) return;
        uiHandler.post(() -> dispatchOnUi(raw));
    }

    void release() {
        released = true;
        uiHandler.post(() -> {
            uiHandler.removeCallbacks(captureTick);
            tickerRunning = false;
            for (Slot slot : slots.values()) {
                destroyWebView(slot);
            }
            slots.clear();
        });
    }

    private void dispatchOnUi(String raw) {
        if (released) return;
        int p1 = raw.indexOf('|');
        if (p1 <= 0) return;
        String verb = raw.substring(0, p1);
        String rest = raw.substring(p1 + 1);
        try {
            if ("create".equals(verb)) {
                String[] parts = rest.split("\\|", 5);
                if (parts.length < 5) return;
                int id = Integer.parseInt(parts[0]);
                int w = Math.max(1, Math.min(4096, Integer.parseInt(parts[1])));
                int h = Math.max(1, Math.min(4096, Integer.parseInt(parts[2])));
                boolean transparent = "1".equals(parts[4]);
                createSlot(id, w, h, transparent);
            } else if ("destroy".equals(verb)) {
                int id = Integer.parseInt(rest);
                Slot slot = slots.remove(id);
                if (slot != null) destroyWebView(slot);
            } else if ("load".equals(verb)) {
                int bar = rest.indexOf('|');
                if (bar <= 0) return;
                int id = Integer.parseInt(rest.substring(0, bar));
                String url = rest.substring(bar + 1);
                Slot slot = slots.get(id);
                if (slot != null) slot.webView.loadUrl(url);
            } else if ("resize".equals(verb)) {
                String[] parts = rest.split("\\|", 3);
                if (parts.length < 3) return;
                int id = Integer.parseInt(parts[0]);
                int w = Math.max(1, Math.min(4096, Integer.parseInt(parts[1])));
                int h = Math.max(1, Math.min(4096, Integer.parseInt(parts[2])));
                Slot slot = slots.get(id);
                if (slot != null) {
                    slot.width = w;
                    slot.height = h;
                    layoutWebView(slot.webView, w, h);
                    captureSlot(slot);
                }
            } else if ("js".equals(verb)) {
                int bar = rest.indexOf('|');
                if (bar <= 0) return;
                int id = Integer.parseInt(rest.substring(0, bar));
                String code = rest.substring(bar + 1);
                Slot slot = slots.get(id);
                if (slot != null) slot.webView.evaluateJavascript(code, null);
            } else if ("pause".equals(verb)) {
                String[] parts = rest.split("\\|", 2);
                if (parts.length < 2) return;
                int id = Integer.parseInt(parts[0]);
                boolean paused = "1".equals(parts[1]);
                Slot slot = slots.get(id);
                if (slot != null) {
                    slot.paused = paused;
                    if (paused) slot.webView.onPause();
                    else slot.webView.onResume();
                }
            } else if ("mouse_move".equals(verb)) {
                String[] parts = rest.split("\\|", 3);
                if (parts.length < 3) return;
                Slot slot = slots.get(Integer.parseInt(parts[0]));
                if (slot != null) {
                    slot.lastMouseX = Integer.parseInt(parts[1]);
                    slot.lastMouseY = Integer.parseInt(parts[2]);
                    if (slot.mouseDown) {
                        long now = SystemClock.uptimeMillis();
                        MotionEvent ev = MotionEvent.obtain(slot.downTimeMs, now, MotionEvent.ACTION_MOVE,
                                slot.lastMouseX, slot.lastMouseY, 0);
                        slot.webView.dispatchTouchEvent(ev);
                        ev.recycle();
                    }
                }
            } else if ("mouse_down".equals(verb)) {
                String[] parts = rest.split("\\|", 2);
                Slot slot = slots.get(Integer.parseInt(parts[0]));
                if (slot != null) {
                    slot.mouseDown = true;
                    slot.downTimeMs = SystemClock.uptimeMillis();
                    MotionEvent ev = MotionEvent.obtain(slot.downTimeMs, slot.downTimeMs, MotionEvent.ACTION_DOWN,
                            slot.lastMouseX, slot.lastMouseY, 0);
                    slot.webView.dispatchTouchEvent(ev);
                    ev.recycle();
                }
            } else if ("mouse_up".equals(verb)) {
                String[] parts = rest.split("\\|", 2);
                Slot slot = slots.get(Integer.parseInt(parts[0]));
                if (slot != null) {
                    long now = SystemClock.uptimeMillis();
                    MotionEvent ev = MotionEvent.obtain(slot.downTimeMs, now, MotionEvent.ACTION_UP,
                            slot.lastMouseX, slot.lastMouseY, 0);
                    slot.webView.dispatchTouchEvent(ev);
                    ev.recycle();
                    slot.mouseDown = false;
                }
            } else if ("wheel".equals(verb)) {
                String[] parts = rest.split("\\|", 3);
                if (parts.length < 3) return;
                Slot slot = slots.get(Integer.parseInt(parts[0]));
                if (slot != null) {
                    int dy = Integer.parseInt(parts[1]);
                    int dx = Integer.parseInt(parts[2]);
                    slot.webView.scrollBy(-dx, -dy);
                }
            } else if ("back".equals(verb)) {
                Slot slot = slots.get(Integer.parseInt(rest));
                if (slot != null && slot.webView.canGoBack()) slot.webView.goBack();
            } else if ("forward".equals(verb)) {
                Slot slot = slots.get(Integer.parseInt(rest));
                if (slot != null && slot.webView.canGoForward()) slot.webView.goForward();
            } else if ("reload".equals(verb)) {
                String[] parts = rest.split("\\|", 2);
                Slot slot = slots.get(Integer.parseInt(parts[0]));
                if (slot != null) slot.webView.reload();
            } else if ("source".equals(verb)) {
                int id = Integer.parseInt(rest);
                Slot slot = slots.get(id);
                if (slot != null) {
                    slot.webView.evaluateJavascript(
                            "(function(){return document.documentElement?document.documentElement.outerHTML:'';})()",
                            value -> callbacks.onSourceReady(id, unquoteJsString(value)));
                }
            } else if ("prop".equals(verb)) {
                String[] parts = rest.split("\\|", 3);
                if (parts.length < 3) return;
                Slot slot = slots.get(Integer.parseInt(parts[0]));
                if (slot != null && "mobile".equals(parts[1])) {
                    boolean mobile = "1".equals(parts[2]) || "true".equalsIgnoreCase(parts[2]);
                    WebSettings ws = slot.webView.getSettings();
                    ws.setUseWideViewPort(!mobile);
                    ws.setLoadWithOverviewMode(!mobile);
                }
            }
        } catch (Exception error) {
            Log.w(TAG, "Browser command failed: " + verb + " -> " + error.getMessage());
        }
    }

    private void createSlot(int id, int width, int height, boolean transparent) {
        Slot prev = slots.remove(id);
        if (prev != null) destroyWebView(prev);

        WebView webView = new WebView(activity);
        webView.setBackgroundColor(transparent ? Color.TRANSPARENT : Color.WHITE);
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setMediaPlaybackRequiresUserGesture(false);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);

        webView.setWebChromeClient(new WebChromeClient());
        webView.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageStarted(WebView view, String url, Bitmap favicon) {
                callbacks.onLoadingStart(id, url != null ? url : "", true);
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                Slot s = slots.get(id);
                if (s != null && !s.paused) captureSlot(s);
                callbacks.onDocumentReady(id, url != null ? url : "",
                        view.getTitle() != null ? view.getTitle() : "",
                        view.canGoBack(), view.canGoForward());
            }

            @Override
            public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                if (request != null && request.isForMainFrame()) {
                    callbacks.onLoadingFailed(id,
                            request.getUrl() != null ? request.getUrl().toString() : "",
                            error != null ? error.getErrorCode() : -1,
                            error != null && error.getDescription() != null ? error.getDescription().toString() : "error");
                }
            }

            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                if (request == null || request.getUrl() == null) return null;
                return tryServeLocalResource(request.getUrl());
            }
        });

        layoutWebView(webView, width, height);
        Slot slot = new Slot(id, webView, width, height);
        slots.put(id, slot);
        captureSlot(slot);
        if (!tickerRunning) {
            tickerRunning = true;
            uiHandler.postDelayed(captureTick, CAPTURE_INTERVAL_MS);
        }
    }

    private WebResourceResponse tryServeLocalResource(Uri uri) {
        if (resourceCacheRoot == null) return null;
        String scheme = uri.getScheme() != null ? uri.getScheme().toLowerCase(Locale.US) : "";
        String host = uri.getHost() != null ? uri.getHost().toLowerCase(Locale.US) : "";
        String relPath = null;
        if ("mta".equals(scheme)) {
            String p = uri.getPath();
            relPath = (host.isEmpty() ? "" : host) + (p != null ? p : "");
        } else if (("http".equals(scheme) || "https".equals(scheme)) && "mta".equals(host)) {
            String p = uri.getPath();
            if (p != null && p.startsWith("/")) p = p.substring(1);
            relPath = p;
        }
        if (relPath == null || relPath.isEmpty() || relPath.contains("..")) return null;
        // Strip leading "local/" if resource-relative lookup finds file directly under cache root.
        File candidate = new File(resourceCacheRoot, relPath);
        if (!candidate.isFile() && relPath.startsWith("local/")) {
            candidate = new File(resourceCacheRoot, relPath.substring(6));
        }
        if (!candidate.isFile()) return null;
        try {
            String canonRoot = resourceCacheRoot.getCanonicalPath();
            String canonFile = candidate.getCanonicalPath();
            if (!canonFile.startsWith(canonRoot)) return null;
            return new WebResourceResponse(mimeForPath(canonFile), "UTF-8", new FileInputStream(candidate));
        } catch (Exception ignored) {
            return null;
        }
    }

    private static String mimeForPath(String path) {
        String lower = path.toLowerCase(Locale.US);
        if (lower.endsWith(".html") || lower.endsWith(".htm")) return "text/html";
        if (lower.endsWith(".css")) return "text/css";
        if (lower.endsWith(".js") || lower.endsWith(".mjs")) return "application/javascript";
        if (lower.endsWith(".json")) return "application/json";
        if (lower.endsWith(".png")) return "image/png";
        if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
        if (lower.endsWith(".webp")) return "image/webp";
        if (lower.endsWith(".svg")) return "image/svg+xml";
        if (lower.endsWith(".woff2")) return "font/woff2";
        if (lower.endsWith(".ttf")) return "font/ttf";
        return "application/octet-stream";
    }

    private static void layoutWebView(WebView webView, int width, int height) {
        int mw = View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY);
        int mh = View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY);
        webView.measure(mw, mh);
        webView.layout(0, 0, width, height);
    }

    private static void captureSlot(Slot slot) {
        try {
            Bitmap bmp = Bitmap.createBitmap(slot.width, slot.height, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bmp);
            slot.webView.draw(canvas);
            MtaDxView.putMemoryBitmap("tx:" + slot.id, bmp);
        } catch (RuntimeException | OutOfMemoryError ignored) {
        }
    }

    private static void destroyWebView(Slot slot) {
        MtaDxView.removeMemoryBitmap("tx:" + slot.id);
        try {
            slot.webView.stopLoading();
            slot.webView.loadUrl("about:blank");
            slot.webView.destroy();
        } catch (Exception ignored) {
        }
    }

    private static String unquoteJsString(String raw) {
        if (raw == null || "null".equals(raw)) return "";
        if (raw.length() >= 2 && raw.startsWith("\"") && raw.endsWith("\"")) {
            String inner = raw.substring(1, raw.length() - 1);
            return inner.replace("\\u003C", "<")
                    .replace("\\u003E", ">")
                    .replace("\\n", "\n")
                    .replace("\\r", "\r")
                    .replace("\\t", "\t")
                    .replace("\\\"", "\"")
                    .replace("\\\\", "\\");
        }
        return raw;
    }
}
