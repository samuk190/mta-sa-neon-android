package com.wardrumstudios.utils;

/* compiled from: r8-map-id-eb2038ccfe378d12a5577dbd7f9bb9474169988104cef0231e3c6c55764b3b76 */
/* loaded from: classes.dex */
public class WarMedia extends com.wardrumstudios.utils.WarGamepad {
    private java.lang.String baseDirectory;
    private java.lang.String baseDirectoryRoot;

    public boolean ConvertToBitmap(byte[] bArr, int i) {
        java.lang.System.out.println("**** ConvertToBitmap ");
        return false;
    }

    public void CreateTextBox(int i, int i2, int i3, int i4, int i5) {
        java.lang.System.out.println("**** CreateTextBox ");
    }

    public boolean DeleteFile(java.lang.String str) {
        try {
            return com.worldrpg.engine.EngineAssetLoader.resolveFile(GetGameBaseDirectory(), str).delete();
        } catch (java.io.IOException error) {
            android.util.Log.e("WorldRPG.engine", "Could not delete game file", error);
            return false;
        }
    }

    public java.lang.String FileGetArchiveName(int i) {
        java.lang.System.out.println("**** FileGetArchiveName " + i);
        if (i != 0) {
            if (i != 1) {
                if (i != 2) {
                    return "";
                }
                return "";
            }
            return "";
        }
        return "";
    }

    public boolean FileRename(java.lang.String str, java.lang.String str2, int i) {
        try {
            java.io.File source = com.worldrpg.engine.EngineAssetLoader.resolveFile(GetGameBaseDirectory(), str);
            java.io.File destination = com.worldrpg.engine.EngineAssetLoader.resolveFile(GetGameBaseDirectory(), str2);
            return source.renameTo(destination);
        } catch (java.io.IOException error) {
            android.util.Log.e("WorldRPG.engine", "Could not rename game file", error);
            return false;
        }
    }

    public java.lang.String GetAndroidBuildinfo(int i) {
        java.lang.System.out.println("**** GetAndroidBuildinfo " + i);
        if (i != 0) {
            if (i != 1) {
                if (i != 2) {
                    return "UNKNOWN";
                }
                return android.os.Build.MODEL;
            }
            return android.os.Build.PRODUCT;
        }
        return android.os.Build.MANUFACTURER;
    }

    public java.lang.String GetAppId() {
        return getPackageName();
    }

    public int GetAvailableMemory() {
        java.lang.System.out.println("**** GetAvailableMemory");
        return 0;
    }

    public int GetDeviceInfo(int i) {
        java.lang.System.out.println("**** GetDeviceInfo");
        if (i != 0) {
            if (i != 1) {
                return -1;
            }
            java.lang.System.out.println("Return for touchsreen 1");
        }
        return 1;
    }

    public int GetDeviceLocale() {
        java.lang.System.out.println("**** GetDeviceLocale");
        return 7;
    }

    public int GetDeviceType() {
        java.lang.System.out.println("Build info version device  " + android.os.Build.DEVICE);
        java.lang.System.out.println("Build MANUFACTURER  " + android.os.Build.MANUFACTURER);
        java.lang.System.out.println("Build BOARD  " + android.os.Build.BOARD);
        java.lang.System.out.println("Build DISPLAY  " + android.os.Build.DISPLAY);
        java.lang.System.out.println("Build CPU_ABI  " + android.os.Build.CPU_ABI);
        java.lang.System.out.println("Build CPU_ABI2  " + android.os.Build.CPU_ABI2);
        java.lang.System.out.println("Build HARDWARE  " + android.os.Build.HARDWARE);
        java.lang.System.out.println("Build MODEL  " + android.os.Build.MODEL);
        java.lang.System.out.println("Build PRODUCT  " + android.os.Build.PRODUCT);
        return (IsPhone() ? 1 : 0) + 516;
    }

    public java.lang.String GetGameBaseDirectory() {
        // Keep the GTA Java callbacks and WorldRPG JNI bootstrap on one private
        // data root. An activity may override this for its validated game tree.
        return getFilesDir().getAbsolutePath() + java.io.File.separator;
    }

    public int GetLowThreshhold() {
        java.lang.System.out.println("**** GetLowThreshhold");
        return 0;
    }

    public float GetScreenWidthInches() {
        java.lang.System.out.println("**** GetScreenWidthInches");
        return 0.0f;
    }

    public int GetSpecialBuildType() {
        java.lang.System.out.println("**** GetSpecialBuildType");
        return 0;
    }

    public int GetTotalMemory() {
        java.lang.System.out.println("**** GetTotalMemory");
        return 0;
    }

    public boolean IsAppInstalled(java.lang.String str) {
        java.lang.System.out.println("**** IsAppInstalled");
        return false;
    }

    public boolean IsCloudAvailable() {
        java.lang.System.out.println("**** IsCloudAvailable");
        return false;
    }

    public boolean IsKeyboardShown() {
        java.lang.System.out.println("**** IsKeyboardShown");
        return false;
    }

    public int IsMoviePlaying() {
        java.lang.System.out.println("**** IsMoviePlaying");
        return 0;
    }

    public boolean IsPhone() {
        java.lang.System.out.println("**** IsPhone");
        return true;
    }

    public void LoadAllGamesFromCloud() {
        java.lang.System.out.println("**** LoadAllGamesFromCloud");
    }

    public java.lang.String LoadGameFromCloud(int i, byte[] bArr) {
        java.lang.System.out.println("**** LoadGameFromCloud");
        return "";
    }

    public void MovieClearText(boolean z) {
        java.lang.System.out.println("**** MovieClearText");
    }

    public void MovieDisplayText(boolean z) {
        java.lang.System.out.println("**** MovieDisplayText");
    }

    public void MovieKeepAspectRatio(boolean z) {
        java.lang.System.out.println("**** MovieKeepAspectRatio");
    }

    public void MovieSetSkippable(boolean z) {
        java.lang.System.out.println("**** MovieSetSkippable");
    }

    public void MovieSetText(java.lang.String str, boolean z, boolean z2) {
        java.lang.System.out.println("**** MovieSetText");
    }

    public void MovieSetTextScale(int i) {
        java.lang.System.out.println("**** MovieSetTextScale");
    }

    public boolean NewCloudSaveAvailable(int i) {
        java.lang.System.out.println("**** NewCloudSaveAvailable");
        return false;
    }

    public java.lang.String OBFU_GetDeviceID() {
        java.lang.System.out.println("**** OBFU_GetDeviceID");
        return "no id";
    }

    public void OpenLink(java.lang.String str) {
        startActivity(new android.content.Intent("android.intent.action.VIEW", android.net.Uri.parse(str)));
        java.lang.System.out.println("**** OpenLink");
    }

    public void PlayMovie(java.lang.String str, float f) {
        java.lang.System.out.println("**** PlayMovie");
    }

    public void PlayMovieInFile(java.lang.String str, float f, int i, int i2) {
        java.lang.System.out.println("**** PlayMovieInFile");
    }

    public void PlayMovieInWindow(java.lang.String str, int i, int i2, int i3, int i4, float f, int i5, int i6, int i7) {
        java.lang.System.out.println("**** PlayMovieInWindow");
    }

    public void SaveGameToCloud(int i, byte[] bArr, int i2) {
        java.lang.System.out.println("**** SaveGameToCloud");
    }

    public void ScreenSetWakeLock(boolean z) {
        final boolean keepAwake = z;
        runOnUiThread(new Runnable() {
            @Override public void run() {
                getWindow().getDecorView().setKeepScreenOn(keepAwake);
            }
        });
    }

    public void SendStatEvent(java.lang.String str) {
        java.lang.System.out.println("**** SendStatEvent");
    }

    public void SendTimedStatEventEnd(java.lang.String str) {
        java.lang.System.out.println("**** SendTimedStatEventEnd");
    }

    public boolean ServiceAppCommand(java.lang.String str, java.lang.String str2) {
        java.lang.System.out.println("**** ServiceAppCommand " + str + " " + str2);
        return false;
    }

    public boolean ServiceAppCommandInt(java.lang.String str, int i) {
        java.lang.System.out.println("**** ServiceAppCommandInt " + str + " " + i);
        return false;
    }

    public int ServiceAppCommandValue(java.lang.String str, java.lang.String str2) {
        java.lang.System.out.println("**** ServiceAppCommandValue " + str + " " + str2);
        return 0;
    }

    public void ShowKeyboard(int i) {
        java.lang.System.out.println("**** showKeyboard");
    }

    public void StopMovie() {
        java.lang.System.out.println("**** StopMovie");
    }

    public void VibratePhone(int i) {
        java.lang.System.out.println("**** VibratePhone ");
    }

    public void VibratePhoneEffect(int i) {
        java.lang.System.out.println("**** VibratePhoneEffect ");
    }

    public boolean isNetworkAvailable() {
        java.lang.System.out.println("**** isNetworkAvailable ");
        return false;
    }

    public boolean isTV() {
        java.lang.System.out.println("**** isTV ");
        return false;
    }

    public boolean isWiFiAvailable() {
        java.lang.System.out.println("**** isWiFiAvailable ");
        return false;
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity, android.app.Activity
    public void onCreate(android.os.Bundle bundle) {
        this.baseDirectory = GetGameBaseDirectory();
        this.baseDirectoryRoot = this.baseDirectory;
        com.nvidia.devtech.NvUtil.getInstance().setActivity(this);
        com.nvidia.devtech.NvUtil.getInstance().setAppLocalValue("STORAGE_ROOT", this.baseDirectory);
        com.nvidia.devtech.NvUtil.getInstance().setAppLocalValue("STORAGE_ROOT_BASE", this.baseDirectoryRoot);
        super.onCreate(bundle);
    }

    public void PlayMovieInWindow(java.lang.String str, int i, int i2, int i3, int i4, float f, int i5, int i6, int i7, boolean z) {
        java.lang.System.out.println("**** PlayMovieInWindow ");
    }

    public void SendStatEvent(java.lang.String str, boolean z) {
        java.lang.System.out.println("**** SendStatEvent");
    }

    public void SendStatEvent(java.lang.String str, java.lang.String str2, java.lang.String str3, boolean z) {
        java.lang.System.out.println("**** SendStatEvent");
    }

    public void SendStatEvent(java.lang.String str, java.lang.String str2, java.lang.String str3) {
        java.lang.System.out.println("**** SendStatEvent1");
    }
}
