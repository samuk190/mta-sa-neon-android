package com.nvidia.devtech;

/* compiled from: r8-map-id-eb2038ccfe378d12a5577dbd7f9bb9474169988104cef0231e3c6c55764b3b76 */
/* loaded from: classes.dex */
public abstract class NvEventQueueActivity extends android.app.Activity implements android.hardware.SensorEventListener {
    private static final int EGL_CONTEXT_CLIENT_VERSION = 12440;
    private static final int EGL_OPENGL_ES2_BIT = 4;
    private static final int EGL_OPENGL_ES3_BIT = 64;
    private static final int EGL_RENDERABLE_TYPE = 12352;
    protected android.view.SurfaceView view;
    protected android.os.Handler handler = null;
    protected boolean paused = false;
    protected boolean supportPauseResume = true;
    protected int mSensorDelay = 1;
    protected android.view.Display display = null;
    protected javax.microedition.khronos.egl.EGLSurface eglSurface = null;
    protected javax.microedition.khronos.egl.EGLDisplay eglDisplay = null;
    protected javax.microedition.khronos.egl.EGLContext eglContext = null;
    protected javax.microedition.khronos.egl.EGLConfig eglConfig = null;
    private final java.lang.Object eglSurfaceLock = new java.lang.Object();
    private volatile boolean surfaceReady = false;
    private volatile boolean surfaceDestroying = false;
    protected volatile android.view.SurfaceHolder cachedSurfaceHolder = null;
    protected volatile boolean ResumeEventDone = false;
    private volatile boolean activityDestroyed = false;
    private volatile boolean activityResumed = false;
    private volatile boolean engineStopping = false;
    private volatile boolean finishRequested = false;
    private volatile boolean framePresentedSinceResume = false;
    private Thread resumeThread;
    protected int redSize = 5;
    protected int greenSize = 6;
    protected int blueSize = 5;
    protected int alphaSize = 0;
    protected int stencilSize = 0;
    protected int depthSize = 16;
    protected int[] configAttrs = null;
    protected int[] contextAttrs = null;
    javax.microedition.khronos.egl.EGL10 egl = null;
    javax.microedition.khronos.opengles.GL11 gl = null;
    private int SwapBufferSkip = 0;
    private boolean ranInit = false;
    private int surfaceWidth = 0;
    private int surfaceHeight = 0;
    private int lastGameWindowWidth = 0;
    private int lastGameWindowHeight = 0;
    private int fixedWidth = 0;
    private int fixedHeight = 0;
    private boolean HasGLExtensions = false;
    private java.lang.String glVendor = null;
    private java.lang.String glExtensions = null;
    private java.lang.String glRenderer = null;
    private java.lang.String glVersion = null;
    private boolean viewIsActive = false;

    /* compiled from: r8-map-id-eb2038ccfe378d12a5577dbd7f9bb9474169988104cef0231e3c6c55764b3b76 */
    /* renamed from: com.nvidia.devtech.NvEventQueueActivity$1, reason: invalid class name */
    /* loaded from: classes.dex */
    public class AnonymousClass1 implements android.view.SurfaceHolder.Callback {
        public AnonymousClass1() {
        }

        /* JADX INFO: Access modifiers changed from: private */
        public static /* synthetic */ void lambda$surfaceCreated$0() {
            android.util.Log.d("NvEventQueueActivity", "ERR handler.post gl");
        }

        @Override // android.view.SurfaceHolder.Callback
        public void surfaceChanged(android.view.SurfaceHolder surfaceHolder, int i, int i2, int i3) {
            boolean z;
            java.lang.System.out.println(("Surface changed: " + i2 + ", " + i3));
            com.nvidia.devtech.NvEventQueueActivity.this.surfaceWidth = i2;
            com.nvidia.devtech.NvEventQueueActivity.this.surfaceHeight = i3;
            com.nvidia.devtech.NvEventQueueActivity nvEventQueueActivity = com.nvidia.devtech.NvEventQueueActivity.this;
            if (i2 > 0 && i3 > 0 && surfaceHolder.getSurface() != null && surfaceHolder.getSurface().isValid()) {
                z = true;
            } else {
                z = false;
            }
            nvEventQueueActivity.surfaceReady = z;
            com.nvidia.devtech.NvEventQueueActivity.this.surfaceDestroying = false;
            com.nvidia.devtech.NvEventQueueActivity nvEventQueueActivity2 = com.nvidia.devtech.NvEventQueueActivity.this;
            nvEventQueueActivity2.setGameWindowSize(nvEventQueueActivity2.surfaceWidth, com.nvidia.devtech.NvEventQueueActivity.this.surfaceHeight);
        }

        /* JADX WARN: Multi-variable type inference failed */
        /* JADX WARN: Type inference failed for: r0v10, types: [java.lang.Object, java.lang.Runnable] */
        @Override // android.view.SurfaceHolder.Callback
        public void surfaceCreated(android.view.SurfaceHolder surfaceHolder) {
            boolean z;
            boolean z2;
            java.lang.System.out.println("systemInit.surfaceCreated");
            com.nvidia.devtech.NvEventQueueActivity nvEventQueueActivity = com.nvidia.devtech.NvEventQueueActivity.this;
            if (nvEventQueueActivity.cachedSurfaceHolder == null) {
                z = true;
            } else {
                z = false;
            }
            nvEventQueueActivity.cachedSurfaceHolder = surfaceHolder;
            if (surfaceHolder.getSurface() != null && surfaceHolder.getSurface().isValid()) {
                z2 = true;
            } else {
                z2 = false;
            }
            nvEventQueueActivity.surfaceReady = z2;
            com.nvidia.devtech.NvEventQueueActivity.this.surfaceDestroying = false;
            if (com.nvidia.devtech.NvEventQueueActivity.this.fixedWidth != 0 && com.nvidia.devtech.NvEventQueueActivity.this.fixedHeight != 0) {
                java.lang.System.out.println("Setting fixed window size");
                surfaceHolder.setFixedSize(com.nvidia.devtech.NvEventQueueActivity.this.fixedWidth, com.nvidia.devtech.NvEventQueueActivity.this.fixedHeight);
            }
            com.nvidia.devtech.NvEventQueueActivity nvEventQueueActivity2 = com.nvidia.devtech.NvEventQueueActivity.this;
            if (!nvEventQueueActivity2.supportPauseResume) {
                try {
                    if (!nvEventQueueActivity2.init(true)) {
                        nvEventQueueActivity2.onEngineBootstrapError("GTA init(true) rejected the game surface");
                        return;
                    }
                } catch (java.lang.UnsatisfiedLinkError error) {
                    nvEventQueueActivity2.onEngineBootstrapError("GTA surface JNI is unavailable");
                    return;
                }
            }
            nvEventQueueActivity2.ranInit = true;
            if (!z && com.nvidia.devtech.NvEventQueueActivity.this.ResumeEventDone) {
                java.lang.System.out.println("entering resumeEvent");
                if (com.nvidia.devtech.NvEventQueueActivity.this.activityResumed) {
                    com.nvidia.devtech.NvEventQueueActivity.this.paused = false;
                    com.nvidia.devtech.NvEventQueueActivity.this.DoResumeEvent();
                }
                java.lang.System.out.println("returned from resumeEvent");
            }
            if (com.nvidia.devtech.NvEventQueueActivity.this.activityResumed) {
                com.nvidia.devtech.NvEventQueueActivity.this.paused = false;
                com.nvidia.devtech.NvEventQueueActivity.this.DoResumeEvent();
            }
            com.nvidia.devtech.NvEventQueueActivity.this.onEngineSurfaceReady(z2);
            com.nvidia.devtech.NvEventQueueActivity.this.onGameSurface(true);
        }

        @Override // android.view.SurfaceHolder.Callback
        public void surfaceDestroyed(android.view.SurfaceHolder surfaceHolder) {
            java.lang.System.out.println("systemInit.surfaceDestroyed");
            com.nvidia.devtech.NvEventQueueActivity.this.onEngineSurfaceReady(false);
            com.nvidia.devtech.NvEventQueueActivity.this.onGameSurface(false);
            com.nvidia.devtech.NvEventQueueActivity.this.surfaceDestroying = true;
            com.nvidia.devtech.NvEventQueueActivity.this.surfaceReady = false;
            com.nvidia.devtech.NvEventQueueActivity.this.viewIsActive = false;
            com.nvidia.devtech.NvEventQueueActivity.this.lastGameWindowWidth = 0;
            com.nvidia.devtech.NvEventQueueActivity.this.lastGameWindowHeight = 0;
            if (!com.nvidia.devtech.NvEventQueueActivity.this.engineStopping
                    && com.nvidia.devtech.NvEventQueueActivity.this.ranInit
                    && com.nvidia.devtech.NvEventQueueActivity.this.ResumeEventDone) {
                com.nvidia.devtech.NvEventQueueActivity.this.pauseEvent();
            }
            com.nvidia.devtech.NvEventQueueActivity nvEventQueueActivity = com.nvidia.devtech.NvEventQueueActivity.this;
            nvEventQueueActivity.paused = true;
            nvEventQueueActivity.destroyEGLSurface();
        }
    }

    /* compiled from: r8-map-id-eb2038ccfe378d12a5577dbd7f9bb9474169988104cef0231e3c6c55764b3b76 */
    /* loaded from: classes.dex */
    public class RawData {
        public byte[] data;
        public int length;

        public RawData() {
        }
    }

    /* compiled from: r8-map-id-eb2038ccfe378d12a5577dbd7f9bb9474169988104cef0231e3c6c55764b3b76 */
    /* loaded from: classes.dex */
    public class RawTexture extends com.nvidia.devtech.NvEventQueueActivity.RawData {
        public int height;
        public int width;

        public RawTexture() {
            super();
        }
    }

    /* compiled from: r8-map-id-eb2038ccfe378d12a5577dbd7f9bb9474169988104cef0231e3c6c55764b3b76 */
    /* loaded from: classes.dex */
    public static class gSurfaceView extends android.view.SurfaceView {
        com.nvidia.devtech.NvEventQueueActivity myActivity;

        public gSurfaceView(android.content.Context context) {
            super(context);
            this.myActivity = null;
        }

        @Override // android.view.View
        public boolean onKeyPreIme(int i, android.view.KeyEvent keyEvent) {
            if (keyEvent.getAction() == 0 && i == 4) {
                this.myActivity.imeClosed();
                return false;
            }
            return false;
        }
    }

    public static int dpToPx(float f, android.content.Context context) {
        return (int) android.util.TypedValue.applyDimension(1, f, context.getResources().getDisplayMetrics());
    }

    private java.lang.String getEGLErrorString(int i) {
        switch (i) {
            case 12288:
                return "EGL_SUCCESS";
            case 12289:
                return "EGL_NOT_INITIALIZED";
            case 12290:
                return "EGL_BAD_ACCESS";
            case 12291:
                return "EGL_BAD_ALLOC";
            case 12292:
                return "EGL_BAD_ATTRIBUTE";
            case 12293:
                return "EGL_BAD_CONFIG";
            case 12294:
                return "EGL_BAD_CONTEXT";
            case 12295:
                return "EGL_BAD_CURRENT_SURFACE";
            case 12296:
                return "EGL_BAD_DISPLAY";
            case 12297:
                return "EGL_BAD_MATCH";
            case 12298:
                return "EGL_BAD_NATIVE_PIXMAP";
            case 12299:
                return "EGL_BAD_NATIVE_WINDOW";
            case 12300:
                return "EGL_BAD_PARAMETER";
            case 12301:
                return "EGL_BAD_SURFACE";
            default:
                return "Unknown EGL error: 0x" + java.lang.Integer.toHexString(i);
        }
    }

    private boolean isHolderSurfaceUsableLocked() {
        android.view.SurfaceHolder surfaceHolder;
        if (!this.surfaceDestroying && this.surfaceReady && (surfaceHolder = this.cachedSurfaceHolder) != null) {
            try {
                if (surfaceHolder.getSurface() != null) {
                    if (this.cachedSurfaceHolder.getSurface().isValid()) {
                        return true;
                    }
                }
                return false;
            } catch (java.lang.Exception e) {
                android.util.Log.w("NvEGL", "Surface validity check failed: " + e.getMessage());
            }
        }
        return false;
    }

    // JNI resume is issued only after a usable Surface and an actual Android
    // resume. The original synthetic Runnable was a no-op in this reconstruction.
    private void resumeWhenSurfaceReady() {
        long deadline = android.os.SystemClock.uptimeMillis() + 30000L;
        try {
            while (!this.activityDestroyed && !this.engineStopping && !Thread.currentThread().isInterrupted()) {
                if (this.activityResumed && this.surfaceReady && !this.surfaceDestroying
                        && this.cachedSurfaceHolder != null) {
                    try {
                        resumeEvent();
                        this.ResumeEventDone = true;
                        android.util.Log.i("WorldRPG.engine", "GTA resumeEvent delivered to valid surface");
                    } catch (java.lang.UnsatisfiedLinkError error) {
                        android.util.Log.e("WorldRPG.engine", "GTA resume JNI is unavailable", error);
                        onEngineBootstrapError("GTA resume JNI is unavailable");
                    }
                    return;
                }
                if (android.os.SystemClock.uptimeMillis() >= deadline) {
                    onEngineBootstrapError("Timed out awaiting resumed game surface");
                    return;
                }
                Thread.sleep(25L);
            }
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
        } finally {
            synchronized (this) {
                if (this.resumeThread == Thread.currentThread()) this.resumeThread = null;
            }
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    public /* synthetic */ void lambda$onCreate$0(int i) {
        if ((i & 4) == 0) {
            hideSystemUI();
        }
    }

    public synchronized void DoResumeEvent() {
        if (this.activityDestroyed || this.engineStopping
                || (this.resumeThread != null && this.resumeThread.isAlive())) return;
        this.resumeThread = new Thread(new Runnable() {
            @Override public void run() { resumeWhenSurfaceReady(); }
        }, "WorldRPG-GTA-resume");
        this.resumeThread.start();
    }

    public void GetGLExtensions() {
        javax.microedition.khronos.opengles.GL11 gl11;
        java.lang.String b;
        if (!this.HasGLExtensions && (gl11 = this.gl) != null && this.cachedSurfaceHolder != null) {
            try {
                this.glVendor = gl11.glGetString(7936);
                this.glExtensions = this.gl.glGetString(7939);
                this.glRenderer = this.gl.glGetString(7937);
                this.glVersion = this.gl.glGetString(7938);
                android.util.Log.d("NvEGL", "Vendor: " + this.glVendor);
                android.util.Log.d("NvEGL", "Renderer: " + this.glRenderer);
                android.util.Log.d("NvEGL", "glVersion: " + this.glVersion);
                java.lang.String b2 = "NvEGL";
                java.lang.StringBuilder sb = new java.lang.StringBuilder();
                sb.append("Extensions: ");
                if (this.glExtensions != null) {
                    java.lang.StringBuilder sb2 = new java.lang.StringBuilder();
                    java.lang.String str = this.glExtensions;
                    sb2.append(str.substring(0, java.lang.Math.min(100, str.length())));
                    sb2.append("...");
                    b = sb2.toString();
                } else {
                    b = "null";
                }
                sb.append(b);
                android.util.Log.d(b2, sb.toString());
                if (this.glVendor != null) {
                    this.HasGLExtensions = true;
                }
            } catch (java.lang.Exception e) {
                android.util.Log.e("NvEGL", "GetGLExtensions failed: " + e.getMessage(), e);
            }
        }
    }

    public android.view.View GetMainView() {
        return this.view;
    }

    public boolean InitEGLAndGLES2(int i) {
        boolean z;
        android.util.Log.d("NvEGL", "InitEGLAndGLES2 called with EGLVersion=" + i);
        if (this.cachedSurfaceHolder == null) {
            android.util.Log.e("NvEGL", "InitEGLAndGLES2 failed, cachedSurfaceHolder is null");
            return false;
        }
        if (!this.surfaceDestroying && this.cachedSurfaceHolder.getSurface() != null && this.cachedSurfaceHolder.getSurface().isValid()) {
            if (this.eglContext == null) {
                if (i >= 3) {
                    android.util.Log.d("NvEGL", "Attempting EGL 3 initialization with depth 24...");
                    try {
                        z = initEGL(3, 24);
                    } catch (java.lang.Exception e) {
                        android.util.Log.e("NvEGL", "initEGL(3, 24) threw exception: " + e.getMessage(), e);
                        z = false;
                    }
                    android.util.Log.d("NvEGL", "initEGL 3 result: " + z);
                } else {
                    z = false;
                }
                if (!z) {
                    android.util.Log.d("NvEGL", "EGL 3 failed, attempting EGL 2 with depthSize=" + this.depthSize);
                    this.configAttrs = null;
                    try {
                        z = initEGL(2, this.depthSize);
                    } catch (java.lang.Exception e2) {
                        android.util.Log.e("NvEGL", "initEGL(2, depthSize) threw exception: " + e2.getMessage(), e2);
                    }
                    android.util.Log.d("NvEGL", "initEGL 2 result: " + z);
                    if (!z) {
                        android.util.Log.d("NvEGL", "Attempting EGL 2 with depth 16 (fallback)...");
                        try {
                            z = initEGL(2, 16);
                        } catch (java.lang.Exception e3) {
                            android.util.Log.e("NvEGL", "initEGL(2, 16) threw exception: " + e3.getMessage(), e3);
                        }
                        android.util.Log.d("NvEGL", "initEGL 2 (depth 16) result: " + z);
                    }
                }
            } else {
                z = true;
            }
            if (z) {
                android.util.Log.d("NvEGL", "EGL initialized, viewIsActive=" + this.viewIsActive);
                if (!this.viewIsActive) {
                    android.util.Log.d("NvEGL", "Creating EGL surface...");
                    createEGLSurface(this.cachedSurfaceHolder);
                    android.util.Log.d("NvEGL", "EGL surface creation completed");
                }
                javax.microedition.khronos.egl.EGLSurface eGLSurface = this.eglSurface;
                if (eGLSurface != null && eGLSurface != javax.microedition.khronos.egl.EGL10.EGL_NO_SURFACE) {
                    this.viewIsActive = true;
                    this.surfaceReady = true;
                    this.surfaceDestroying = false;
                    this.SwapBufferSkip = 1;
                    android.util.Log.d("NvEGL", "InitEGLAndGLES2 completed successfully");
                    return true;
                }
                android.util.Log.w("NvEGL", "InitEGLAndGLES2 skipped, EGL surface was not created");
                this.viewIsActive = false;
                return false;
            }
            android.util.Log.e("NvEGL", "InitEGLAndGLES2 failed, core EGL init failure");
            return false;
        }
        android.util.Log.w("NvEGL", "InitEGLAndGLES2 skipped, surface is not valid");
        this.surfaceReady = false;
        return false;
    }

    public boolean IsPortrait() {
        return false;
    }

    public native boolean accelerometerEvent(float f, float f2, float f3);

    public native void changeConnection(boolean z);

    public native void cleanup();

    public void cleanupEGL() {
        javax.microedition.khronos.egl.EGLDisplay eGLDisplay;
        synchronized (this.eglSurfaceLock) {
            android.util.Log.d("NvEGL", "cleanupEGL called");
            destroyEGLSurface();
            javax.microedition.khronos.egl.EGL10 egl10 = this.egl;
            if (egl10 == null) {
                android.util.Log.w("NvEGL", "cleanupEGL: egl is null, resetting state only");
                this.eglDisplay = null;
                this.eglContext = null;
                this.eglSurface = null;
                this.ranInit = false;
                this.eglConfig = null;
                this.cachedSurfaceHolder = null;
                this.surfaceReady = false;
                this.surfaceDestroying = false;
                this.surfaceWidth = 0;
                this.surfaceHeight = 0;
                return;
            }
            try {
                javax.microedition.khronos.egl.EGLDisplay eGLDisplay2 = this.eglDisplay;
                if (eGLDisplay2 != null) {
                    javax.microedition.khronos.egl.EGLSurface eGLSurface = javax.microedition.khronos.egl.EGL10.EGL_NO_SURFACE;
                    egl10.eglMakeCurrent(eGLDisplay2, eGLSurface, eGLSurface, javax.microedition.khronos.egl.EGL10.EGL_NO_CONTEXT);
                }
                javax.microedition.khronos.egl.EGLContext eGLContext = this.eglContext;
                if (eGLContext != null && (eGLDisplay = this.eglDisplay) != null) {
                    this.egl.eglDestroyContext(eGLDisplay, eGLContext);
                }
                javax.microedition.khronos.egl.EGLDisplay eGLDisplay3 = this.eglDisplay;
                if (eGLDisplay3 != null) {
                    this.egl.eglTerminate(eGLDisplay3);
                }
            } catch (java.lang.Exception e) {
                android.util.Log.e("NvEGL", "cleanupEGL threw exception: " + e.getMessage(), e);
            }
            this.eglDisplay = null;
            this.eglContext = null;
            this.eglSurface = null;
            this.ranInit = false;
            this.eglConfig = null;
            this.cachedSurfaceHolder = null;
            this.surfaceReady = false;
            this.surfaceDestroying = false;
            this.surfaceWidth = 0;
            this.surfaceHeight = 0;
            android.util.Log.d("NvEGL", "cleanupEGL completed");
        }
    }

    public void createEGLSurface(android.view.SurfaceHolder surfaceHolder) {
        synchronized (this.eglSurfaceLock) {
            try {
                android.util.Log.d("NvEGL", "createEGLSurface called");
                if (this.egl == null) {
                    android.util.Log.e("NvEGL", "createEGLSurface: egl is null!");
                    return;
                }
                if (this.eglDisplay == null) {
                    android.util.Log.e("NvEGL", "createEGLSurface: eglDisplay is null!");
                    return;
                }
                if (this.eglConfig == null) {
                    android.util.Log.e("NvEGL", "createEGLSurface: eglConfig is null!");
                    return;
                }
                if (surfaceHolder == null) {
                    android.util.Log.e("NvEGL", "createEGLSurface: surface is null!");
                    return;
                }
                if (!this.surfaceDestroying && surfaceHolder.getSurface() != null && surfaceHolder.getSurface().isValid()) {
                    try {
                        this.eglSurface = this.egl.eglCreateWindowSurface(this.eglDisplay, this.eglConfig, surfaceHolder, null);
                        int eglGetError = this.egl.eglGetError();
                        android.util.Log.d("NvEGL", "eglSurface: " + this.eglSurface + ", err: " + getEGLErrorString(eglGetError));
                        javax.microedition.khronos.egl.EGLSurface eGLSurface = this.eglSurface;
                        if (eGLSurface != null && eGLSurface != javax.microedition.khronos.egl.EGL10.EGL_NO_SURFACE) {
                            this.surfaceReady = true;
                            this.surfaceDestroying = false;
                            int[] iArr = new int[1];
                            this.egl.eglQuerySurface(this.eglDisplay, this.eglSurface, 12375, iArr);
                            this.surfaceWidth = iArr[0];
                            this.egl.eglQuerySurface(this.eglDisplay, this.eglSurface, 12374, iArr);
                            this.surfaceHeight = iArr[0];
                            android.util.Log.d("NvEGL", "Surface size: " + this.surfaceWidth + "x" + this.surfaceHeight);
                            android.util.Log.d("NvEGL", "checking glVendor == null?");
                            if (this.glVendor == null) {
                                android.util.Log.d("NvEGL", "Making current and back to get vendor info");
                                makeCurrent();
                                unMakeCurrent();
                            }
                            android.util.Log.d("NvEGL", "createEGLSurface completed");
                            return;
                        }
                        android.util.Log.e("NvEGL", "Failed to create EGL surface!");
                        this.surfaceReady = false;
                        return;
                    } catch (java.lang.Exception e) {
                        android.util.Log.e("NvEGL", "eglCreateWindowSurface threw exception: " + e.getMessage(), e);
                        return;
                    }
                }
                android.util.Log.w("NvEGL", "createEGLSurface: surface is not valid yet");
                this.surfaceReady = false;
            } catch (java.lang.Throwable th) {
                throw th;
            }
        }
    }

    public void destroyEGLSurface() {
        javax.microedition.khronos.egl.EGLDisplay eGLDisplay;
        javax.microedition.khronos.egl.EGLSurface eGLSurface;
        synchronized (this.eglSurfaceLock) {
            android.util.Log.d("NvEGL", "destroyEGLSurface called");
            this.surfaceDestroying = true;
            this.surfaceReady = false;
            if (this.egl == null) {
                android.util.Log.w("NvEGL", "destroyEGLSurface: egl is null, skipping");
                this.eglSurface = null;
                return;
            }
            try {
                javax.microedition.khronos.opengles.GL11 gl11 = this.gl;
                if (gl11 != null && this.eglDisplay != null && (eGLSurface = this.eglSurface) != null && eGLSurface != javax.microedition.khronos.egl.EGL10.EGL_NO_SURFACE) {
                    try {
                        gl11.glFinish();
                    } catch (java.lang.Exception e) {
                        android.util.Log.w("NvEGL", "glFinish before destroy failed: " + e.getMessage());
                    }
                }
                javax.microedition.khronos.egl.EGLDisplay eGLDisplay2 = this.eglDisplay;
                if (eGLDisplay2 != null && this.eglSurface != null) {
                    javax.microedition.khronos.egl.EGL10 egl10 = this.egl;
                    javax.microedition.khronos.egl.EGLSurface eGLSurface2 = javax.microedition.khronos.egl.EGL10.EGL_NO_SURFACE;
                    egl10.eglMakeCurrent(eGLDisplay2, eGLSurface2, eGLSurface2, javax.microedition.khronos.egl.EGL10.EGL_NO_CONTEXT);
                }
                javax.microedition.khronos.egl.EGLSurface eGLSurface3 = this.eglSurface;
                if (eGLSurface3 != null && (eGLDisplay = this.eglDisplay) != null) {
                    this.egl.eglDestroySurface(eGLDisplay, eGLSurface3);
                }
            } catch (java.lang.Exception e2) {
                android.util.Log.e("NvEGL", "destroyEGLSurface threw exception: " + e2.getMessage(), e2);
            }
            this.eglSurface = null;
            android.util.Log.d("NvEGL", "destroyEGLSurface completed");
        }
    }

    public int getOrientation() {
        return this.display.getOrientation();
    }

    public boolean getSupportPauseResume() {
        return this.supportPauseResume;
    }

    public int getSurfaceHeight() {
        return this.surfaceHeight;
    }

    public int getSurfaceWidth() {
        return this.surfaceWidth;
    }

    public void hideSystemUI() {
        android.view.WindowInsetsController insetsController;
        int statusBars;
        int navigationBars;
        getWindow().getDecorView().setSystemUiVisibility(5894);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            insetsController = getWindow().getInsetsController();
            if (insetsController != null) {
                statusBars = android.view.WindowInsets.Type.statusBars();
                navigationBars = android.view.WindowInsets.Type.navigationBars();
                insetsController.hide(statusBars | navigationBars);
                insetsController.setSystemBarsBehavior(2);
            }
        }
    }

    public native void imeClosed();

    public native boolean init(boolean z);

    /* JADX WARN: Multi-variable type inference failed */
    public boolean initEGL(int i, int i2) {
        int i3;
        int[] iArr;
        int i4;
        int[] iArr2;
        int i5;
        android.util.Log.d("NvEGL", ("initEGL called with esVersion=" + i + ", depthBits=" + i2));
        android.util.Log.d("NvEGL", "Device: " + android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL + ", Android " + android.os.Build.VERSION.SDK_INT);
        if (this.configAttrs == null) {
            this.configAttrs = new int[]{12344};
        }
        int[] iArr3 = this.configAttrs;
        this.configAttrs = new int[iArr3.length + 2];
        int i6 = 0;
        while (true) {
            i3 = 1;
            int length = iArr3.length - 1;
            iArr = this.configAttrs;
            if (i6 >= length) {
                break;
            }
            iArr[i6] = iArr3[i6];
            i6++;
        }
        int i7 = i6 + 1;
        iArr[i6] = EGL_RENDERABLE_TYPE;
        if (i == 3) {
            i4 = i6 + 2;
            iArr[i7] = EGL_OPENGL_ES3_BIT;
        } else {
            i4 = i6 + 2;
            iArr[i7] = 4;
        }
        iArr[i4] = 12344;
        this.contextAttrs = new int[]{EGL_CONTEXT_CLIENT_VERSION, 2, 12344};
        if (iArr == null) {
            this.configAttrs = new int[]{12344};
        }
        int[] iArr4 = this.configAttrs;
        this.configAttrs = new int[iArr4.length + 12];
        int i8 = 0;
        while (true) {
            int length2 = iArr4.length - 1;
            iArr2 = this.configAttrs;
            if (i8 >= length2) {
                break;
            }
            iArr2[i8] = iArr4[i8];
            i8++;
        }
        int i9 = 12324;
        iArr2[i8] = 12324;
        iArr2[i8 + 1] = this.redSize;
        int i10 = 12323;
        iArr2[i8 + 2] = 12323;
        iArr2[i8 + 3] = this.greenSize;
        int i11 = 12322;
        iArr2[i8 + 4] = 12322;
        iArr2[i8 + 5] = this.blueSize;
        int i12 = 12321;
        iArr2[i8 + 6] = 12321;
        iArr2[i8 + 7] = this.alphaSize;
        iArr2[i8 + 8] = 12326;
        iArr2[i8 + 9] = this.stencilSize;
        int i13 = 12325;
        iArr2[i8 + 10] = 12325;
        iArr2[i8 + 11] = i2;
        iArr2[i8 + 12] = 12344;
        android.util.Log.d("NvEGL", "Config attrs prepared, getting EGL...");
        try {
            javax.microedition.khronos.egl.EGL10 egl10 = (javax.microedition.khronos.egl.EGL10) javax.microedition.khronos.egl.EGLContext.getEGL();
            this.egl = egl10;
            if (egl10 == null) {
                android.util.Log.e("NvEGL", "EGL instance is null!");
                return false;
            }
            android.util.Log.d("NvEGL", "EGL instance obtained successfully");
            try {
                this.egl.eglGetError();
            } catch (java.lang.Exception e) {
                android.util.Log.e("NvEGL", "eglGetError failed: " + e.getMessage(), e);
            }
            android.util.Log.d("NvEGL", "Calling eglGetDisplay...");
            try {
                if (android.os.Build.VERSION.SDK_INT >= 35) {
                    android.util.Log.d("NvEGL", "Android 16+ detected, adding small delay for GPU driver");
                    java.lang.Thread.sleep(50L);
                }
                javax.microedition.khronos.egl.EGLDisplay eglGetDisplay = this.egl.eglGetDisplay(javax.microedition.khronos.egl.EGL10.EGL_DEFAULT_DISPLAY);
                this.eglDisplay = eglGetDisplay;
                int i14 = 12288;
                if (eglGetDisplay != null && eglGetDisplay != javax.microedition.khronos.egl.EGL10.EGL_NO_DISPLAY) {
                    int eglGetError = this.egl.eglGetError();
                    android.util.Log.d("NvEGL", "eglDisplay: " + this.eglDisplay + ", err: " + getEGLErrorString(eglGetError));
                    int[] iArr5 = new int[2];
                    try {
                        boolean eglInitialize = this.egl.eglInitialize(this.eglDisplay, iArr5);
                        android.util.Log.d("NvEGL", "eglInitialize returned: " + eglInitialize + ", version: " + iArr5[0] + "." + iArr5[1]);
                        javax.microedition.khronos.egl.EGL10 egl102 = this.egl;
                        if (!eglInitialize) {
                            int eglGetError2 = egl102.eglGetError();
                            android.util.Log.e("NvEGL", "eglInitialize failed with error: " + getEGLErrorString(eglGetError2));
                            return false;
                        }
                        int eglGetError3 = egl102.eglGetError();
                        if (eglGetError3 != 12288) {
                            android.util.Log.e("NvEGL", "EGL error after initialize: " + getEGLErrorString(eglGetError3));
                            return false;
                        }
                        android.util.Log.d("NvEGL", "eglInitialize successful, err: " + getEGLErrorString(eglGetError3));
                        javax.microedition.khronos.egl.EGLConfig[] eGLConfigArr = new javax.microedition.khronos.egl.EGLConfig[20];
                        int[] iArr6 = new int[1];
                        android.util.Log.d("NvEGL", "Calling eglChooseConfig...");
                        try {
                            this.egl.eglChooseConfig(this.eglDisplay, this.configAttrs, eGLConfigArr, 20, iArr6);
                            int eglGetError4 = this.egl.eglGetError();
                            android.util.Log.d("NvEGL", "eglChooseConfig err: " + getEGLErrorString(eglGetError4) + ", num_configs: " + iArr6[0]);
                            if (iArr6[0] <= 0) {
                                android.util.Log.e("NvEGL", "No EGL configs found!");
                                return false;
                            }
                            int[] iArr7 = new int[1];
                            int i15 = 16777216;
                            int i16 = 0;
                            while (i16 < iArr6[0]) {
                                int i17 = 0;
                                while (true) {
                                    int length3 = (iArr3.length - i3) >> i3;
                                    javax.microedition.khronos.egl.EGL10 egl103 = this.egl;
                                    if (i17 < length3) {
                                        i5 = i3;
                                        int i18 = i17 * 2;
                                        egl103.eglGetConfigAttrib(this.eglDisplay, eGLConfigArr[i16], this.configAttrs[i18], iArr7);
                                        int i19 = iArr7[0];
                                        int i20 = this.configAttrs[i18 + 1];
                                        if ((i19 & i20) != i20) {
                                            break;
                                        }
                                        i17++;
                                        i3 = i5;
                                    } else {
                                        i5 = i3;
                                        egl103.eglGetConfigAttrib(this.eglDisplay, eGLConfigArr[i16], i9, iArr7);
                                        int i21 = iArr7[0];
                                        this.egl.eglGetConfigAttrib(this.eglDisplay, eGLConfigArr[i16], i10, iArr7);
                                        int i22 = iArr7[0];
                                        this.egl.eglGetConfigAttrib(this.eglDisplay, eGLConfigArr[i16], i11, iArr7);
                                        int i23 = iArr7[0];
                                        this.egl.eglGetConfigAttrib(this.eglDisplay, eGLConfigArr[i16], i12, iArr7);
                                        int i24 = iArr7[0];
                                        this.egl.eglGetConfigAttrib(this.eglDisplay, eGLConfigArr[i16], i13, iArr7);
                                        int i25 = iArr7[0];
                                        this.egl.eglGetConfigAttrib(this.eglDisplay, eGLConfigArr[i16], 12326, iArr7);
                                        int i26 = iArr7[0];
                                        java.lang.String b = "NvEGL";
                                        java.lang.StringBuilder sb = new java.lang.StringBuilder();
                                        sb.append(">>> EGL Config [").append(i16).append("] R").append(i21);
                                        sb.append("G").append(i22).append("B").append(i23);
                                        sb.append("A").append(i24).append(" D").append(i25);
                                        sb.append("S");
                                        sb.append(i26);
                                        android.util.Log.d(b, sb.toString());
                                        int abs = java.lang.Math.abs(i26 - this.stencilSize) + ((java.lang.Math.abs(i24 - this.alphaSize) + (java.lang.Math.abs(i23 - this.blueSize) + (java.lang.Math.abs(i22 - this.greenSize) + java.lang.Math.abs(i21 - this.redSize)))) << 16) + (java.lang.Math.abs(i25 - i2) << 8);
                                        if (abs < i15) {
                                            android.util.Log.d("NvEGL", "--------------------------");
                                            android.util.Log.d("NvEGL", ("New config chosen: " + i16 + " with score " + abs));
                                            int i27 = 0;
                                            while (true) {
                                                int[] iArr8 = this.configAttrs;
                                                if (i27 >= ((iArr8.length - 1) >> 1)) {
                                                    break;
                                                }
                                                int i28 = i27 * 2;
                                                this.egl.eglGetConfigAttrib(this.eglDisplay, eGLConfigArr[i16], iArr8[i28], iArr7);
                                                if (iArr7[0] >= this.configAttrs[i28 + 1]) {
                                                    android.util.Log.d("NvEGL", "setting " + i27 + ", matches: " + iArr7[0]);
                                                }
                                                i27++;
                                            }
                                            this.eglConfig = eGLConfigArr[i16];
                                            i15 = abs;
                                        }
                                        // This candidate has been scored. Continue with the next
                                        // config rather than evaluating the same one forever.
                                        break;
                                    }
                                }
                                i16++;
                                i3 = i5;
                                i13 = 12325;
                                i9 = 12324;
                                i10 = 12323;
                                i11 = 12322;
                                i12 = 12321;
                            }
                            boolean z = (i3 != 0);
                            if (this.eglConfig == null) {
                                android.util.Log.e("NvEGL", "No suitable EGL config found!");
                                return false;
                            }
                            android.util.Log.d("NvEGL", "Creating EGL context...");
                            try {
                                javax.microedition.khronos.egl.EGL10 egl104 = this.egl;
                                javax.microedition.khronos.egl.EGLDisplay eGLDisplay = this.eglDisplay;
                                javax.microedition.khronos.egl.EGLConfig eGLConfig = this.eglConfig;
                                javax.microedition.khronos.egl.EGLContext eGLContext = javax.microedition.khronos.egl.EGL10.EGL_NO_CONTEXT;
                                this.eglContext = egl104.eglCreateContext(eGLDisplay, eGLConfig, eGLContext, this.contextAttrs);
                                int eglGetError5 = this.egl.eglGetError();
                                android.util.Log.d("NvEGL", "eglCreateContext result: " + this.eglContext + ", err: " + getEGLErrorString(eglGetError5));
                                javax.microedition.khronos.egl.EGLContext eGLContext2 = this.eglContext;
                                if (eGLContext2 != null && eGLContext2 != eGLContext) {
                                    try {
                                        javax.microedition.khronos.opengles.GL11 gl11 = (javax.microedition.khronos.opengles.GL11) eGLContext2.getGL();
                                        this.gl = gl11;
                                        if (gl11 == null) {
                                            android.util.Log.e("NvEGL", "GL interface is null!");
                                            return false;
                                        }
                                        android.util.Log.d("NvEGL", "EGL initialization completed successfully!");
                                        return z;
                                    } catch (java.lang.Exception e2) {
                                        android.util.Log.e("NvEGL", "Failed to get GL from context: " + e2.getMessage(), e2);
                                        return false;
                                    }
                                }
                                android.util.Log.e("NvEGL", "Failed to create EGL context!");
                                return false;
                            } catch (java.lang.Exception e3) {
                                android.util.Log.e("NvEGL", "eglCreateContext threw exception: " + e3.getMessage(), e3);
                                return false;
                            }
                        } catch (java.lang.Exception e4) {
                            android.util.Log.e("NvEGL", "eglChooseConfig threw exception: " + e4.getMessage(), e4);
                            return false;
                        }
                    } catch (java.lang.Exception e5) {
                        android.util.Log.e("NvEGL", "eglInitialize threw exception: " + e5.getMessage(), e5);
                        return false;
                    }
                }
                try {
                    i14 = this.egl.eglGetError();
                } catch (java.lang.Exception e6) {
                    android.util.Log.e("NvEGL", "eglGetError after failed display: " + e6.getMessage());
                }
                android.util.Log.e("NvEGL", "eglGetDisplay failed! Display=" + this.eglDisplay + ", Error=" + getEGLErrorString(i14));
                return false;
            } catch (java.lang.Exception e7) {
                android.util.Log.e("NvEGL", "eglGetDisplay failed with exception: " + e7.getMessage(), e7);
                return false;
            }
        } catch (java.lang.Exception e8) {
            android.util.Log.e("NvEGL", "Failed to get EGL instance: " + e8.getMessage(), e8);
            return false;
        }
    }

    public native void jniNvAPKInit(java.lang.Object obj);

    public native boolean keyEvent(int i, int i2, int i3, int i4, android.view.KeyEvent keyEvent);

    private String engineDataDirectory() {
        String directory = com.nvidia.devtech.NvUtil.getInstance().getAppLocalValue("STORAGE_ROOT");
        return directory == null ? getFilesDir().getAbsolutePath() : directory;
    }

    public com.nvidia.devtech.NvEventQueueActivity.RawData loadFile(String name) {
        RawData result = new RawData();
        result.data = new byte[0];
        try {
            result.data = com.worldrpg.engine.EngineAssetLoader.read(this, engineDataDirectory(), name);
            result.length = result.data.length;
        } catch (java.io.IOException error) {
            android.util.Log.e("WorldRPG.engine", "Could not load engine file: " + name, error);
        }
        return result;
    }

    public com.nvidia.devtech.NvEventQueueActivity.RawTexture loadTexture(String name) {
        RawTexture result = new RawTexture();
        result.data = new byte[0];
        android.graphics.Bitmap bitmap = null;
        try (java.io.InputStream input = com.worldrpg.engine.EngineAssetLoader.open(this, engineDataDirectory(), name)) {
            bitmap = android.graphics.BitmapFactory.decodeStream(input);
            if (bitmap == null) throw new java.io.IOException("Asset is not a supported bitmap");
            int width = bitmap.getWidth();
            int height = bitmap.getHeight();
            long pixels = (long) width * height;
            if (width <= 0 || height <= 0 || pixels > 16L * 1024 * 1024) {
                throw new java.io.IOException("Engine texture dimensions are invalid or excessive");
            }
            result.data = new byte[(int) pixels * 4];
            int[] row = new int[width];
            int target = 0;
            // The native callback expects bottom-up RGBA, not Android ARGB.
            for (int y = height - 1; y >= 0; --y) {
                bitmap.getPixels(row, 0, width, 0, y, width, 1);
                for (int pixel : row) {
                    result.data[target++] = (byte) (pixel >>> 16);
                    result.data[target++] = (byte) (pixel >>> 8);
                    result.data[target++] = (byte) pixel;
                    result.data[target++] = (byte) (pixel >>> 24);
                }
            }
            result.width = width;
            result.height = height;
            result.length = result.data.length;
        } catch (java.io.IOException | RuntimeException error) {
            result.data = new byte[0];
            result.length = 0;
            result.width = 0;
            result.height = 0;
            android.util.Log.e("WorldRPG.engine", "Could not load engine texture: " + name, error);
        } finally {
            if (bitmap != null) bitmap.recycle();
        }
        return result;
    }

    public native void lowMemoryEvent();

    public void mSleep(long j) {
        try {
            java.lang.Thread.sleep(j);
        } catch (java.lang.InterruptedException unused) {
        }
    }

    public boolean makeCurrent() {
        synchronized (this.eglSurfaceLock) {
            try {
                if (!isHolderSurfaceUsableLocked()) {
                    return false;
                }
                javax.microedition.khronos.egl.EGLContext eGLContext = this.eglContext;
                if (eGLContext == null) {
                    android.util.Log.e("NvEGL", "makeCurrent: eglContext is null");
                    return false;
                }
                javax.microedition.khronos.egl.EGLSurface eGLSurface = this.eglSurface;
                if (eGLSurface == null) {
                    android.util.Log.e("NvEGL", "makeCurrent: eglSurface is null");
                    return false;
                }
                javax.microedition.khronos.egl.EGL10 egl10 = this.egl;
                if (egl10 == null) {
                    android.util.Log.e("NvEGL", "makeCurrent: egl is null");
                    return false;
                }
                javax.microedition.khronos.egl.EGLDisplay eGLDisplay = this.eglDisplay;
                if (eGLDisplay == null) {
                    android.util.Log.e("NvEGL", "makeCurrent: eglDisplay is null");
                    return false;
                }
                try {
                    if (!egl10.eglMakeCurrent(eGLDisplay, eGLSurface, eGLSurface, eGLContext)) {
                        int eglGetError = this.egl.eglGetError();
                        android.util.Log.e("NvEGL", "eglMakeCurrent failed with error: " + getEGLErrorString(eglGetError));
                        javax.microedition.khronos.egl.EGL10 egl102 = this.egl;
                        javax.microedition.khronos.egl.EGLDisplay eGLDisplay2 = this.eglDisplay;
                        javax.microedition.khronos.egl.EGLSurface eGLSurface2 = this.eglSurface;
                        if (!egl102.eglMakeCurrent(eGLDisplay2, eGLSurface2, eGLSurface2, this.eglContext)) {
                            int eglGetError2 = this.egl.eglGetError();
                            android.util.Log.e("NvEGL", "eglMakeCurrent retry failed with error: " + getEGLErrorString(eglGetError2));
                            return false;
                        }
                    }
                    GetGLExtensions();
                    return true;
                } catch (java.lang.Exception e) {
                    android.util.Log.e("NvEGL", "makeCurrent threw exception: " + e.getMessage(), e);
                    return false;
                }
            } catch (java.lang.Throwable th) {
                throw th;
            }
        }
    }

    public native boolean multiTouchEvent(int i, int i2, int i3, int i4, int i5, int i6, android.view.MotionEvent motionEvent);

    public native boolean multiTouchEvent4(int i, int i2, int i3, int i4, int i5, int i6, int i7, int i8, int i9, int i10, android.view.MotionEvent motionEvent);

    public native void notifyChange(java.lang.String str, int i);

    public native void nvAcquireTimeExtension();

    public native long nvGetSystemTime();

    @Override // android.app.Activity, android.content.ComponentCallbacks
    public void onConfigurationChanged(android.content.res.Configuration configuration) {
        super.onConfigurationChanged(configuration);
    }

    @Override // android.app.Activity
    @android.annotation.SuppressLint({"SuspiciousIndentation"})
    public void onCreate(android.os.Bundle bundle) {
        java.lang.System.out.println("**** onCreate");
        super.onCreate(bundle);
        if (!shouldStartEngine()) {
            java.lang.System.out.println("**** onCreate: engine start refused by the host; finishing");
            finish();
            return;
        }
        if (this.supportPauseResume) {
            java.lang.System.out.println("Calling init(false)");
            try {
                if (!init(false)) {
                    onEngineBootstrapError("GTA init(false) rejected initialization");
                    return;
                }
                this.ranInit = true;
            } catch (java.lang.UnsatisfiedLinkError error) {
                onEngineBootstrapError("GTA initialization JNI is unavailable");
                return;
            }
        }
        this.handler = new android.os.Handler();
        com.nvidia.devtech.NvUtil.getInstance().setActivity(this);
        com.nvidia.devtech.NvAPKFileHelper.getInstance().setContext(this);
        new com.nvidia.devtech.NvAPKFile().is = null;
        try {
            jniNvAPKInit(getAssets());
        } catch (java.lang.UnsatisfiedLinkError error) {
            onEngineBootstrapError("GTA APK asset JNI is unavailable");
            return;
        }
        this.display = ((android.view.WindowManager) getSystemService("window")).getDefaultDisplay();
        getWindow().addFlags(1024);
        setRequestedOrientation(6);
        systemInit();
        hideSystemUI();
        getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(new android.view.View.OnSystemUiVisibilityChangeListener() { // from class: tr
            @Override // android.view.View.OnSystemUiVisibilityChangeListener
            public final void onSystemUiVisibilityChange(int i) {
                com.nvidia.devtech.NvEventQueueActivity.this.lambda$onCreate$0(i);
            }
        });
    }

    @Override
    public void onDestroy() {
        this.activityDestroyed = true;
        this.activityResumed = false;
        synchronized (this) {
            if (this.resumeThread != null) this.resumeThread.interrupt();
        }
        if (!isChangingConfigurations()) systemCleanup();
        super.onDestroy();
    }

    @Override
    public void finish() {
        // The native thread also calls finish() before returning. Never join
        // from that callback, and keep Android callbacks available to the game.
        if (android.os.Looper.myLooper() != android.os.Looper.getMainLooper()) {
            runOnUiThread(() -> finish());
            return;
        }
        if (this.engineStopping) return;
        if (!this.ranInit) {
            super.finish();
            return;
        }
        this.finishRequested = true;
        finishWithActiveEngine();
    }

    private void finishWithActiveEngine() {
        if (!this.finishRequested || this.engineStopping || this.activityDestroyed
                || !this.activityResumed || !this.surfaceReady
                || !this.framePresentedSinceResume) return;
        this.engineStopping = true;
        android.util.Log.i("WorldRPG.engine", "Stopping GTA before Activity pause");
        new Thread(() -> {
            // Event 7 waits for the game thread. A paused game thread ignores
            // quit, so this must precede onPause/surfaceDestroyed. A real frame
            // after resume confirms progress; resumeEvent alone only enqueues.
            quitAndWait();
            runOnUiThread(() -> {
                cleanup();
                this.ranInit = false;
                this.ResumeEventDone = false;
                android.util.Log.i("WorldRPG.engine", "GTA threads stopped and native queue released");
                NvEventQueueActivity.super.finish();
            });
        }, "WorldRPG-GTA-exit").start();
    }

    protected final void finishAfterEngineBootstrapError() {
        // Startup may fail before there is any Surface to resume. The host's
        // isolated process then reclaims native state rather than waiting for
        // a frame that can never arrive or destroying a live native mutex.
        this.engineStopping = true;
        super.finish();
    }

    @Override // android.app.Activity, android.view.KeyEvent.Callback
    public boolean onKeyDown(int i, android.view.KeyEvent keyEvent) {
        if (i != 24 && i != 25) {
            boolean z = false;
            if (i != 89 && i != 85 && i != 90) {
                if (i != 82 && i != 4) {
                    z = super.onKeyDown(i, keyEvent);
                }
                if (!z) {
                    return keyEvent(keyEvent.getAction(), i, keyEvent.getUnicodeChar(), keyEvent.getMetaState(), keyEvent);
                }
            }
            return z;
        }
        return super.onKeyDown(i, keyEvent);
    }

    @Override // android.app.Activity, android.view.KeyEvent.Callback
    public boolean onKeyUp(int i, android.view.KeyEvent keyEvent) {
        com.nvidia.devtech.NvEventQueueActivity nvEventQueueActivity;
        android.view.KeyEvent keyEvent2;
        int i2;
        if (i == 115) {
            if (keyEvent.isCapsLockOn()) {
                i2 = 3;
            } else {
                i2 = 4;
            }
            nvEventQueueActivity = this;
            keyEvent2 = keyEvent;
            nvEventQueueActivity.keyEvent(i2, 115, 0, 0, keyEvent2);
        } else {
            nvEventQueueActivity = this;
            keyEvent2 = keyEvent;
        }
        if (i != 89 && i != 85 && i != 90) {
            boolean onKeyUp = super.onKeyUp(i, keyEvent2);
            if (onKeyUp) {
                return onKeyUp;
            }
            return nvEventQueueActivity.keyEvent(keyEvent2.getAction(), i, keyEvent2.getUnicodeChar(), keyEvent2.getMetaState(), keyEvent2);
        }
        return false;
    }

    @Override // android.app.Activity
    public void onPause() {
        this.activityResumed = false;
        this.framePresentedSinceResume = false;
        this.paused = true;
        if (!this.engineStopping && this.ranInit && this.ResumeEventDone) pauseEvent();
        super.onPause();
    }

    @Override // android.app.Activity
    public void onRestart() {
        super.onRestart();
    }

    @Override // android.app.Activity
    public void onResume() {
        super.onResume();
        this.framePresentedSinceResume = false;
        this.activityResumed = true;
        this.paused = false;
        if (this.view != null) DoResumeEvent();
    }

    @Override // android.app.Activity
    public void onStop() {
        super.onStop();
    }

    @Override // android.app.Activity
    public boolean onTouchEvent(android.view.MotionEvent motionEvent) {
        boolean onTouchEvent = super.onTouchEvent(motionEvent);
        if (!onTouchEvent) {
            int pointerCount = motionEvent.getPointerCount();
            int i = 0;
            int i2 = 0;
            int i3 = 0;
            int i4 = 0;
            int i5 = 0;
            for (int i6 = 0; i6 < pointerCount; i6++) {
                if (motionEvent.getPointerId(i6) < 2) {
                    if (i == 0) {
                        i2 = (int) motionEvent.getX(i6);
                        i3 = (int) motionEvent.getY(i6);
                    } else if (i == 1) {
                        i4 = (int) motionEvent.getX(i6);
                        i5 = (int) motionEvent.getY(i6);
                    }
                    i++;
                }
            }
            try {
                return multiTouchEvent(motionEvent.getAction(), i, i2, i3, i4, i5, motionEvent);
            } catch (java.lang.UnsatisfiedLinkError unused) {
                return false;
            }
        }
        return onTouchEvent;
    }

    @Override // android.app.Activity, android.view.Window.Callback
    public void onWindowFocusChanged(boolean z) {
        if (z) {
            hideSystemUI();
        }
        super.onWindowFocusChanged(z);
    }

    public native void pauseEvent();

    public native void postCleanup();

    public native boolean processTouchpadAsPointer(android.view.ViewParent viewParent, boolean z);

    public native void quitAndWait();

    public native void resumeEvent();

    public void setFixedSize(int i, int i2) {
        this.fixedWidth = i;
        this.fixedHeight = i2;
    }

    public void setGameWindowSize(int i, int i2) {
        if (i > 0 && i2 > 0) {
            if ((IsPortrait() && i > i2) || (!IsPortrait() && i2 > i)) {
                int previousWidth = i;
                i = i2;
                i2 = previousWidth;
            }
            if (i != this.lastGameWindowWidth || i2 != this.lastGameWindowHeight) {
                this.lastGameWindowWidth = i;
                this.lastGameWindowHeight = i2;
                setWindowSize(i, i2);
            }
        }
    }

    public native void setWindowSize(int i, int i2);

    public boolean shouldStartEngine() {
        return true;
    }

    public boolean swapBuffers() {
        synchronized (this.eglSurfaceLock) {
            try {
                int i = this.SwapBufferSkip;
                if (i > 0) {
                    this.SwapBufferSkip = i - 1;
                    return true;
                }
                if (!isHolderSurfaceUsableLocked()) {
                    return true;
                }
                javax.microedition.khronos.egl.EGL10 egl10 = this.egl;
                if (egl10 == null) {
                    android.util.Log.e("NvEGL", "swapBuffers: egl is null");
                    return false;
                }
                javax.microedition.khronos.egl.EGLDisplay eGLDisplay = this.eglDisplay;
                if (eGLDisplay == null) {
                    android.util.Log.e("NvEGL", "swapBuffers: eglDisplay is null");
                    return false;
                }
                javax.microedition.khronos.egl.EGLSurface eGLSurface = this.eglSurface;
                if (eGLSurface == null) {
                    android.util.Log.e("NvEGL", "swapBuffers: eglSurface is null");
                    return false;
                }
                try {
                    if (egl10.eglSwapBuffers(eGLDisplay, eGLSurface)) {
                        if (this.activityResumed && !this.engineStopping) {
                            this.framePresentedSinceResume = true;
                            if (this.finishRequested) runOnUiThread(() -> finishWithActiveEngine());
                        }
                        return true;
                    }
                    int eglGetError = this.egl.eglGetError();
                    android.util.Log.e("NvEGL", "eglSwapBuffers failed with error: " + getEGLErrorString(eglGetError));
                    if (eglGetError == 12301 || eglGetError == 12299) {
                        this.surfaceReady = false;
                        this.viewIsActive = false;
                    }
                    return false;
                } catch (java.lang.Exception e) {
                    android.util.Log.e("NvEGL", "swapBuffers threw exception: " + e.getMessage(), e);
                    return false;
                }
            } catch (java.lang.Throwable th) {
                throw th;
            }
        }
    }

    public void systemCleanup() {
        if (this.ranInit) {
            // Android may destroy a stopped Activity without calling finish.
            // Its Surface cannot resume the game now. The isolated engine
            // process owns these resources; never destroy a live queue or
            // block Android's UI thread waiting for the paused native loop.
            android.util.Log.w("WorldRPG.engine", "Activity destroyed before engine exit; retiring isolated process");
            return;
        }
        this.ResumeEventDone = false;
        cleanupEGL();
    }

    public boolean systemInit() {
        java.lang.System.out.println("ln systemInit");
        setContentView(com.worldrpg.neonlauncher.R.layout.game_screen);
        android.view.SurfaceView surfaceView = new android.view.SurfaceView(this);
        ((android.widget.FrameLayout) findViewById(com.worldrpg.neonlauncher.R.id.game_layout)).addView(surfaceView, new android.view.ViewGroup.LayoutParams(-1, -1));
        surfaceView.setZ(1.0f);
        this.view = surfaceView;
        android.view.SurfaceHolder holder = surfaceView.getHolder();
        holder.setType(2);
        holder.setKeepScreenOn(true);
        this.view.setFocusable(true);
        this.view.setFocusableInTouchMode(true);
        if (android.os.Build.VERSION.SDK_INT >= 26) {
            this.view.setDefaultFocusHighlightEnabled(false);
            findViewById(com.worldrpg.neonlauncher.R.id.game_layout).setDefaultFocusHighlightEnabled(false);
            getWindow().getDecorView().setDefaultFocusHighlightEnabled(false);
        }
        DoResumeEvent();
        holder.addCallback(new com.nvidia.devtech.NvEventQueueActivity.AnonymousClass1());
        return true;
    }

    public native boolean touchEvent(int i, int i2, int i3, android.view.MotionEvent motionEvent);

    public boolean unMakeCurrent() {
        synchronized (this.eglSurfaceLock) {
            try {
                javax.microedition.khronos.egl.EGL10 egl10 = this.egl;
                if (egl10 == null) {
                    android.util.Log.e("NvEGL", "unMakeCurrent: egl is null");
                    return false;
                }
                javax.microedition.khronos.egl.EGLDisplay eGLDisplay = this.eglDisplay;
                if (eGLDisplay == null) {
                    android.util.Log.e("NvEGL", "unMakeCurrent: eglDisplay is null");
                    return false;
                }
                try {
                    javax.microedition.khronos.egl.EGLSurface eGLSurface = javax.microedition.khronos.egl.EGL10.EGL_NO_SURFACE;
                    if (!egl10.eglMakeCurrent(eGLDisplay, eGLSurface, eGLSurface, javax.microedition.khronos.egl.EGL10.EGL_NO_CONTEXT)) {
                        int eglGetError = this.egl.eglGetError();
                        android.util.Log.e("NvEGL", "unMakeCurrent failed with error: " + getEGLErrorString(eglGetError));
                        return false;
                    }
                    return true;
                } catch (java.lang.Exception e) {
                    android.util.Log.e("NvEGL", "unMakeCurrent threw exception: " + e.getMessage(), e);
                    return false;
                }
            } catch (java.lang.Throwable th) {
                throw th;
            }
        }
    }

    /** Reports startup failure on either the UI or the resume thread. */
    protected void onEngineBootstrapError(String message) {
        android.util.Log.e("WorldRPG.engine", message);
        runOnUiThread(new Runnable() {
            @Override public void run() { finishAfterEngineBootstrapError(); }
        });
    }

    /** Activity-specific observer; this does not replace GTA's registered JNI callbacks. */
    protected void onEngineSurfaceReady(boolean ready) {
    }

    public void onGameSurface(boolean z) {
    }

    @Override // android.hardware.SensorEventListener
    public void onSensorChanged(android.hardware.SensorEvent sensorEvent) {
    }

    @Override // android.hardware.SensorEventListener
    public void onAccuracyChanged(android.hardware.Sensor sensor, int i) {
    }
}
