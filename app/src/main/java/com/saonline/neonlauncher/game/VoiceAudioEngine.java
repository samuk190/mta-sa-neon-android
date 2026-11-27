package com.saonline.neonlauncher.game;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.util.Log;

import androidx.core.content.ContextCompat;

import java.util.HashMap;
import java.util.Map;

/**
 * Android AudioTrack/AudioRecord backend for MTA Neon voice chat.
 * Playback uses one streaming mono 16-bit PCM AudioTrack per active speaker.
 * Capture runs on a background thread only while push-to-talk is active and
 * RECORD_AUDIO has been granted.
 */
final class VoiceAudioEngine {
    private static final String TAG = "WorldRPGNeon";

    interface MicCallback {
        void onMicPcm(short[] samples, int count);
    }

    private final Context appContext;
    private final MicCallback micCallback;
    private final Object lock = new Object();
    private final Map<Integer, AudioTrack> playbackTracks = new HashMap<>();

    private boolean enabled = false;
    private int sampleRate = 16000;
    private volatile boolean capturing = false;
    private Thread captureThread = null;

    VoiceAudioEngine(Context context, MicCallback callback) {
        this.appContext = context.getApplicationContext();
        this.micCallback = callback;
    }

    void configure(boolean isEnabled, int rateHz) {
        synchronized (lock) {
            final int resolvedRate = (rateHz == 8000 || rateHz == 16000 || rateHz == 32000) ? rateHz : 16000;
            if (enabled == isEnabled && sampleRate == resolvedRate) return;
            stopCaptureLocked();
            releaseTracksLocked();
            enabled = isEnabled;
            sampleRate = resolvedRate;
        }
    }

    void writePlaybackPcm(int playerId, short[] pcm, int rateHz) {
        if (pcm == null || pcm.length == 0) return;
        AudioTrack track;
        synchronized (lock) {
            if (!enabled) return;
            final int rate = (rateHz == 8000 || rateHz == 16000 || rateHz == 32000) ? rateHz : sampleRate;
            track = playbackTracks.get(playerId);
            if (track == null || track.getSampleRate() != rate) {
                if (track != null) {
                    try {
                        track.stop();
                        track.release();
                    } catch (RuntimeException ignored) {
                    }
                    playbackTracks.remove(playerId);
                }
                track = createTrack(rate);
                if (track == null) return;
                playbackTracks.put(playerId, track);
            }
        }
        try {
            if (track.getPlayState() != AudioTrack.PLAYSTATE_PLAYING) {
                track.play();
            }
            track.write(pcm, 0, pcm.length, AudioTrack.WRITE_NON_BLOCKING);
        } catch (RuntimeException e) {
            Log.w(TAG, "VoiceAudioEngine: playback write failed: " + e.getMessage());
        }
    }

    boolean setCaptureActive(boolean active) {
        synchronized (lock) {
            if (!enabled || !active) {
                stopCaptureLocked();
                return false;
            }
            if (capturing) return true;
            if (ContextCompat.checkSelfPermission(appContext, Manifest.permission.RECORD_AUDIO)
                    != PackageManager.PERMISSION_GRANTED) {
                Log.i(TAG, "VoiceAudioEngine: RECORD_AUDIO permission not granted");
                return false;
            }
            final int rate = sampleRate;
            final int frameSamples = Math.max(160, (rate * 20) / 1000);
            final int minBufBytes = AudioRecord.getMinBufferSize(
                    rate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
            if (minBufBytes <= 0) {
                Log.w(TAG, "VoiceAudioEngine: invalid AudioRecord buffer size for rate " + rate);
                return false;
            }
            final int bufBytes = Math.max(minBufBytes, frameSamples * 4);
            final AudioRecord recorder;
            try {
                recorder = new AudioRecord(
                        MediaRecorder.AudioSource.VOICE_COMMUNICATION,
                        rate,
                        AudioFormat.CHANNEL_IN_MONO,
                        AudioFormat.ENCODING_PCM_16BIT,
                        bufBytes);
            } catch (SecurityException | IllegalArgumentException e) {
                Log.w(TAG, "VoiceAudioEngine: AudioRecord init failed: " + e.getMessage());
                return false;
            }
            if (recorder.getState() != AudioRecord.STATE_INITIALIZED) {
                recorder.release();
                return false;
            }
            capturing = true;
            captureThread = new Thread(() -> runCaptureLoop(recorder, frameSamples), "NeonVoiceCapture");
            captureThread.start();
            return true;
        }
    }

    void release() {
        synchronized (lock) {
            enabled = false;
            stopCaptureLocked();
            releaseTracksLocked();
        }
    }

    private void runCaptureLoop(AudioRecord recorder, int frameSamples) {
        final short[] buffer = new short[frameSamples];
        try {
            recorder.startRecording();
            while (capturing) {
                final int read = recorder.read(buffer, 0, buffer.length);
                if (read > 0 && micCallback != null) {
                    micCallback.onMicPcm(buffer, read);
                } else if (read < 0) {
                    break;
                }
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "VoiceAudioEngine: capture loop error: " + e.getMessage());
        } finally {
            try {
                recorder.stop();
            } catch (RuntimeException ignored) {
            }
            try {
                recorder.release();
            } catch (RuntimeException ignored) {
            }
        }
    }

    private void stopCaptureLocked() {
        capturing = false;
        if (captureThread != null) {
            captureThread.interrupt();
            captureThread = null;
        }
    }

    private void releaseTracksLocked() {
        for (AudioTrack track : playbackTracks.values()) {
            try {
                track.stop();
                track.release();
            } catch (RuntimeException ignored) {
            }
        }
        playbackTracks.clear();
    }

    private static AudioTrack createTrack(int rateHz) {
        final int minBytes = AudioTrack.getMinBufferSize(
                rateHz, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        if (minBytes <= 0) return null;
        final int frameBytes = Math.max(320, (rateHz * 20 / 1000) * 2);
        final int bufBytes = Math.max(minBytes, frameBytes * 4);
        try {
            AudioTrack track = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                            .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                            .build())
                    .setAudioFormat(new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setSampleRate(rateHz)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                            .build())
                    .setBufferSizeInBytes(bufBytes)
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .build();
            if (track.getState() != AudioTrack.STATE_INITIALIZED) {
                track.release();
                return null;
            }
            return track;
        } catch (RuntimeException e) {
            Log.w(TAG, "VoiceAudioEngine: AudioTrack init failed: " + e.getMessage());
            return null;
        }
    }
}
