package com.worldrpg.neonlauncher.game;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.AtomicFile;
import android.util.Log;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/** Private, bounded diagnostics shared with the launcher, which survives a native engine crash. */
public final class EngineDiagnostics {
    private static final String TAG = "EngineDiagnostics";
    private static final int LOG_LIMIT = 256 * 1024;
    private static final AtomicBoolean checking = new AtomicBoolean();
    private static java.lang.Process logcat;
    private static JSONObject session;
    private static Thread.UncaughtExceptionHandler previousHandler;
    private static Thread.UncaughtExceptionHandler engineHandler;

    private EngineDiagnostics() {}

    private static AtomicFile file(Context context, String name) {
        return new AtomicFile(new File(context.getFilesDir(), name));
    }

    private static void write(AtomicFile file, String text) throws java.io.IOException {
        FileOutputStream out = null;
        try {
            out = file.startWrite();
            out.write(text.getBytes(StandardCharsets.UTF_8));
            file.finishWrite(out);
        } catch (java.io.IOException error) {
            file.failWrite(out);
            throw error;
        }
    }

    private static String read(AtomicFile file) throws java.io.IOException {
        try (InputStream input = file.openRead()) { return readLimited(input, LOG_LIMIT); }
    }

    public static boolean isEnabled(Context context) {
        try { return "1".equals(read(file(context, "engine-debug-enabled"))); }
        catch (Exception ignored) { return false; }
    }

    public static boolean setEnabled(Context context, boolean enabled) {
        try {
            write(file(context, "engine-debug-enabled"), enabled ? "1" : "0");
            if (!enabled) {
                new File(context.getFilesDir(), "engine-debug.log").delete();
                new File(context.getFilesDir(), "engine-debug.previous.log").delete();
            }
            return true;
        } catch (Exception error) {
            Log.w(TAG, "Cannot save engine debug setting", error);
            return false;
        }
    }

    public static synchronized void begin(Context context) {
        // A recreated Activity shares the current engine process and its diagnostics.
        if (session != null) return;
        Context app = context.getApplicationContext();
        boolean debug = isEnabled(app);
        try {
            session = new JSONObject().put("pid", android.os.Process.myPid())
                    .put("started", System.currentTimeMillis()).put("debug", debug).put("state", "running");
            write(file(app, "engine-session.json"), session.toString());
        } catch (Exception error) { Log.w(TAG, "Cannot record engine session", error); }
        if (debug) startLogcat(app);
        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        previousHandler = previous;
        engineHandler = (thread, error) -> {
            failure(app, "Engine Java failure: " + error);
            if (previous != null) previous.uncaughtException(thread, error);
        };
        Thread.setDefaultUncaughtExceptionHandler(engineHandler);
    }

    public static synchronized void noteGpu(Context context, GpuTextureSupport gpu) {
        if (session == null) return;
        try {
            session.put("gpu", gpu.label()).put("format", gpu.format);
            write(file(context, "engine-session.json"), session.toString());
            Log.i(TAG, "GPU=" + gpu.renderer + ", vendor=" + gpu.vendor + ", texture format=" + gpu.format);
        } catch (Exception error) { Log.w(TAG, "Cannot record GPU", error); }
    }

    public static synchronized void failure(Context context, String message) {
        if (session == null) return;
        try {
            session.put("state", "failed").put("error", message);
            write(file(context, "engine-session.json"), session.toString());
        } catch (Exception error) { Log.w(TAG, "Cannot record engine failure", error); }
    }

    public static synchronized void end(Context context) {
        try {
            if (session != null && !"failed".equals(session.optString("state"))) {
                session.put("state", "closed");
                write(file(context, "engine-session.json"), session.toString());
            }
        } catch (Exception error) { Log.w(TAG, "Cannot close engine session", error); }
        session = null;
        if (logcat != null) logcat.destroy();
        logcat = null;
        if (engineHandler != null && Thread.getDefaultUncaughtExceptionHandler() == engineHandler) {
            Thread.setDefaultUncaughtExceptionHandler(previousHandler);
        }
        engineHandler = null;
        previousHandler = null;
    }

    private static void startLogcat(Context context) {
        File current = new File(context.getFilesDir(), "engine-debug.log");
        File previous = new File(context.getFilesDir(), "engine-debug.previous.log");
        current.delete();
        previous.delete();
        try {
            // Only this UID's engine PID; never capture other apps or clear the system log.
            int pid = android.os.Process.myPid();
            // The watcher also stops logcat after SIGSEGV, when Java cleanup never runs.
            // All interpolated values are OS-supplied integers, with no user input.
            String command = "logcat -v threadtime --pid=" + pid + " -T 1 & child=$!; "
                    + "trap 'kill \"$child\" 2>/dev/null' EXIT TERM INT; "
                    + "while kill -0 " + pid + " 2>/dev/null; do sleep 1; done";
            logcat = new ProcessBuilder("sh", "-c", command).redirectErrorStream(true).start();
            java.lang.Process readerProcess = logcat;
            Thread reader = new Thread(() -> {
                try (InputStream input = readerProcess.getInputStream()) {
                    byte[] buffer = new byte[4096];
                    FileOutputStream output = new FileOutputStream(current);
                    int size = 0;
                    try {
                        int count;
                        while ((count = input.read(buffer)) != -1) {
                            if (size + count > LOG_LIMIT) {
                                output.close();
                                previous.delete();
                                if (!current.renameTo(previous)) current.delete();
                                output = new FileOutputStream(current);
                                size = 0;
                            }
                            output.write(buffer, 0, count);
                            output.flush();
                            size += count;
                        }
                    } finally { output.close(); }
                } catch (Exception error) { Log.w(TAG, "Engine log capture stopped", error); }
            }, "engine-debug-log");
            reader.setDaemon(true);
            reader.start();
        } catch (Exception error) { Log.w(TAG, "Engine log capture is unavailable", error); }
    }

    /** Called by the launcher on return from GTA, and after relaunching the app following a crash. */
    public static void checkForFailure(Activity activity) {
        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            if (activity.isFinishing() || activity.isDestroyed() || !activity.hasWindowFocus()
                    || !checking.compareAndSet(false, true)) return;
            new Thread(() -> {
                try {
                    AtomicFile marker = file(activity, "engine-session.json");
                    JSONObject old = new JSONObject(read(marker));
                    String state = old.optString("state");
                    if ("closed".equals(state) || "reported".equals(state)) return;
                    int pid = old.optInt("pid");
                    ActivityManager manager = (ActivityManager) activity.getSystemService(Context.ACTIVITY_SERVICE);
                    List<ActivityManager.RunningAppProcessInfo> processes = manager == null ? null : manager.getRunningAppProcesses();
                    // A recorded startup error can be reported while the Activity finishes retiring its process.
                    if ("running".equals(state)) {
                        if (processes != null) for (ActivityManager.RunningAppProcessInfo process : processes) {
                            if (process.pid == pid && (activity.getPackageName() + ":engine").equals(process.processName)) return;
                        }
                        if (processes == null && pid > 0) {
                            try { android.system.Os.kill(pid, 0); return; }
                            catch (android.system.ErrnoException ignored) { }
                        }
                    }
                    String message = old.optString("error", "The game engine stopped unexpectedly.");
                    boolean debug = old.optBoolean("debug") && isEnabled(activity);
                    String report = debug ? buildReport(activity, manager, old, message) : message;
                    activity.runOnUiThread(() -> {
                        if (!activity.isFinishing() && !activity.isDestroyed() && activity.hasWindowFocus()) {
                            try {
                                JSONObject latest = new JSONObject(read(marker));
                                if (latest.optLong("started") != old.optLong("started")
                                        || "reported".equals(latest.optString("state"))
                                        || "closed".equals(latest.optString("state"))) return;
                                old.put("state", "reported");
                                write(marker, old.toString());
                            } catch (Exception error) { Log.w(TAG, "Cannot acknowledge engine failure", error); }
                            showDialog(activity, debug, report);
                        }
                    });
                } catch (Exception ignored) {
                    // No session exists on a fresh install.
                } finally { checking.set(false); }
            }, "engine-failure-report").start();
        }, 800);
    }

    private static String buildReport(Context context, ActivityManager manager, JSONObject old, String message) {
        StringBuilder text = new StringBuilder(message).append("\n\nDevice: ").append(Build.MANUFACTURER)
                .append(' ').append(Build.MODEL).append("\nAndroid: ").append(Build.VERSION.RELEASE)
                .append("\nGPU: ").append(old.optString("gpu", "not detected"))
                .append("\nTexture format: ").append(old.optString("format", "not detected")).append('\n');
        // Native signals cannot be caught by a Java handler. Android keeps their exit traces.
        if (Build.VERSION.SDK_INT >= 30 && manager != null) {
            try {
                for (ApplicationExitInfo info : manager.getHistoricalProcessExitReasons(context.getPackageName(), old.optInt("pid"), 8)) {
                    if (!(context.getPackageName() + ":engine").equals(info.getProcessName())
                            || info.getTimestamp() < old.optLong("started")) continue;
                    text.append("\nExit reason: ").append(info.getReason()).append(" / status: ").append(info.getStatus())
                            .append("\n").append(info.getDescription()).append('\n');
                    try (InputStream trace = info.getTraceInputStream()) {
                        if (trace != null) {
                            text.append("\nEngine crash trace:\n");
                            if (info.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE && Build.VERSION.SDK_INT >= 31) {
                                text.append(limitText(NativeCrashTrace.decode(readBytes(trace, 8 * 1024 * 1024)), 96 * 1024));
                            } else text.append(readLimited(trace, LOG_LIMIT));
                        }
                    }
                    break;
                }
            } catch (Exception error) { text.append("\nAndroid crash trace unavailable.\n"); }
        }
        for (String name : new String[]{"engine-debug.previous.log", "engine-debug.log"}) {
            try (java.io.RandomAccessFile input = new java.io.RandomAccessFile(new File(context.getFilesDir(), name), "r")) {
                long start = Math.max(0, input.length() - 64 * 1024);
                input.seek(start);
                byte[] tail = new byte[(int) (input.length() - start)];
                input.readFully(tail);
                text.append("\nEngine log:\n");
                if (start > 0) text.append("[Earlier log lines omitted]\n");
                text.append(new String(tail, StandardCharsets.UTF_8));
            } catch (Exception ignored) { }
        }
        text.append("\n\nNative crash traces depend on Android availability; captured engine logs are shown above.");
        // Keep the clipboard transaction well below Android's shared 1 MiB Binder limit.
        return limitText(text.toString(), 240 * 1024);
    }

    private static String limitText(String text, int limit) {
        return text.length() <= limit ? text : text.substring(0, limit) + "\n[Report truncated]\n";
    }

    private static String readLimited(InputStream input, int limit) throws java.io.IOException {
        return new String(readBytes(input, limit), StandardCharsets.UTF_8);
    }

    private static byte[] readBytes(InputStream input, int limit) throws java.io.IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        int count;
        while (bytes.size() < limit && (count = input.read(buffer, 0, Math.min(buffer.length, limit - bytes.size()))) != -1) {
            bytes.write(buffer, 0, count);
        }
        return bytes.toByteArray();
    }

    private static void showDialog(Activity activity, boolean debug, String report) {
        TextView text = new TextView(activity);
        text.setText(report);
        text.setTextIsSelectable(true);
        text.setTextSize(debug ? 12 : 16);
        int pad = (int) (20 * activity.getResources().getDisplayMetrics().density);
        text.setPadding(pad, pad, pad, pad);
        if (debug) text.setTypeface(android.graphics.Typeface.MONOSPACE);
        ScrollView scroll = new ScrollView(activity);
        scroll.addView(text);
        AlertDialog.Builder dialog = new AlertDialog.Builder(activity)
                .setTitle(debug ? "Engine failure log" : "Unable to start or continue game")
                .setView(scroll).setPositiveButton("Close", null);
        if (debug) dialog.setNeutralButton("Copy log", (d, which) -> {
            ClipboardManager clipboard = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
            if (clipboard != null) clipboard.setPrimaryClip(ClipData.newPlainText("Engine failure log", report));
        });
        dialog.show();
    }
}
