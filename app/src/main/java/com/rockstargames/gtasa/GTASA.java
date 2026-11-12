package com.rockstargames.gtasa;

/* compiled from: r8-map-id-eb2038ccfe378d12a5577dbd7f9bb9474169988104cef0231e3c6c55764b3b76 */
/* loaded from: classes.dex */
public class GTASA extends com.wardrumstudios.utils.WarMedia {
    @Override // com.wardrumstudios.utils.WarMedia
    public boolean ServiceAppCommand(java.lang.String str, java.lang.String str2) {
        return false;
    }

    @Override // com.wardrumstudios.utils.WarMedia
    public int ServiceAppCommandValue(java.lang.String str, java.lang.String str2) {
        return 0;
    }

    @Override // android.app.Activity
    public void onActivityResult(int i, int i2, android.content.Intent intent) {
        super.onActivityResult(i, i2, intent);
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity, android.app.Activity, android.content.ComponentCallbacks
    public void onConfigurationChanged(android.content.res.Configuration configuration) {
        super.onConfigurationChanged(configuration);
    }

    @Override // com.wardrumstudios.utils.WarMedia, com.nvidia.devtech.NvEventQueueActivity, android.app.Activity
    public void onCreate(android.os.Bundle bundle) {
        java.lang.System.out.println("GTASA onCreate");
        super.onCreate(bundle);
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity, android.app.Activity
    public void onDestroy() {
        java.lang.System.out.println("GTASA onDestroy");
        super.onDestroy();
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity, android.app.Activity, android.view.KeyEvent.Callback
    public boolean onKeyDown(int i, android.view.KeyEvent keyEvent) {
        return super.onKeyDown(i, keyEvent);
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity, android.app.Activity
    public void onPause() {
        java.lang.System.out.println("GTASA onPause");
        super.onPause();
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity, android.app.Activity
    public void onRestart() {
        java.lang.System.out.println("GTASA onRestart");
        super.onRestart();
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity, android.app.Activity
    public void onResume() {
        java.lang.System.out.println("GTASA onResume");
        super.onResume();
    }

    @Override // android.app.Activity
    public void onStart() {
        java.lang.System.out.println("GTASA onStart");
        super.onStart();
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity, android.app.Activity
    public void onStop() {
        java.lang.System.out.println("GTASA onStop");
        super.onStop();
    }
}
