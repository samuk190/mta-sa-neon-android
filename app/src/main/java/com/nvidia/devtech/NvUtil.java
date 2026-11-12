package com.nvidia.devtech;

/* compiled from: r8-map-id-eb2038ccfe378d12a5577dbd7f9bb9474169988104cef0231e3c6c55764b3b76 */
/* loaded from: classes.dex */
public class NvUtil {
    private static final com.nvidia.devtech.NvUtil instance = new com.nvidia.devtech.NvUtil();
    private android.app.Activity activity = null;
    private final java.util.HashMap<java.lang.String, java.lang.String> appLocalValues;

    private NvUtil() {
        java.util.HashMap<java.lang.String, java.lang.String> hashMap = new java.util.HashMap<>();
        this.appLocalValues = hashMap;
        hashMap.put("STORAGE_ROOT", android.os.Environment.getExternalStorageDirectory().getAbsolutePath());
    }

    public static com.nvidia.devtech.NvUtil getInstance() {
        return instance;
    }

    public java.lang.String getAppLocalValue(java.lang.String str) {
        return this.appLocalValues.get(str);
    }

    public java.lang.String getParameter(java.lang.String str) {
        return this.activity.getIntent().getStringExtra(str);
    }

    public boolean hasAppLocalValue(java.lang.String str) {
        return this.appLocalValues.containsKey(str);
    }

    public void setActivity(android.app.Activity activity) {
        this.activity = activity;
    }

    public void setAppLocalValue(java.lang.String str, java.lang.String str2) {
        this.appLocalValues.put(str, str2);
    }
}
