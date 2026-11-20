package com.worldrpg.neonlauncher.game;

import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;

import java.io.IOException;

/** A short-lived ES2 pbuffer, probed before GTA creates any threads or native state. */
final class GpuProbe {
    private GpuProbe() {}

    static GpuTextureSupport inspect() throws IOException {
        EGLDisplay previousDisplay = EGL14.eglGetCurrentDisplay();
        EGLContext previousContext = EGL14.eglGetCurrentContext();
        EGLSurface previousDraw = EGL14.eglGetCurrentSurface(EGL14.EGL_DRAW);
        EGLSurface previousRead = EGL14.eglGetCurrentSurface(EGL14.EGL_READ);
        EGLDisplay display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        EGLContext context = EGL14.EGL_NO_CONTEXT;
        EGLSurface surface = EGL14.EGL_NO_SURFACE;
        boolean initialized = false;
        try {
            int[] version = new int[2];
            if (display == EGL14.EGL_NO_DISPLAY || !EGL14.eglInitialize(display, version, 0, version, 1)) {
                throw new IOException("Unable to detect GPU texture support. Please restart the app and try again.");
            }
            initialized = true;
            EGLConfig[] configs = new EGLConfig[1];
            int[] count = new int[1];
            int[] attrs = {EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
                    EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                    EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_NONE};
            if (!EGL14.eglChooseConfig(display, attrs, 0, configs, 0, 1, count, 0) || count[0] == 0) {
                throw new IOException("Unable to detect GPU texture support: no OpenGL ES 2 configuration.");
            }
            context = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT,
                    new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE}, 0);
            surface = EGL14.eglCreatePbufferSurface(display, configs[0],
                    new int[]{EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE}, 0);
            if (context == EGL14.EGL_NO_CONTEXT || surface == EGL14.EGL_NO_SURFACE
                    || !EGL14.eglMakeCurrent(display, surface, surface, context)) {
                throw new IOException("Unable to detect GPU texture support: OpenGL initialization failed.");
            }
            String renderer = GLES20.glGetString(GLES20.GL_RENDERER);
            String extensions = GLES20.glGetString(GLES20.GL_EXTENSIONS);
            if (renderer == null || extensions == null) throw new IOException("Unable to read GPU texture support.");
            return new GpuTextureSupport(GLES20.glGetString(GLES20.GL_VENDOR), renderer, extensions);
        } finally {
            if (initialized) {
                EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
                if (surface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, surface);
                if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context);
                if (previousDisplay != EGL14.EGL_NO_DISPLAY && previousContext != EGL14.EGL_NO_CONTEXT) {
                    EGL14.eglMakeCurrent(previousDisplay, previousDraw, previousRead, previousContext);
                }
                // The default display is shared across threads. Only destroy this probe's
                // objects; terminating the display could invalidate Android's UI or GTA.
                // The host reuses the initialized display, and process retirement reclaims it.
            }
        }
    }
}
