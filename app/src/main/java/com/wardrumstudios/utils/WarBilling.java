package com.wardrumstudios.utils;

/* compiled from: r8-map-id-eb2038ccfe378d12a5577dbd7f9bb9474169988104cef0231e3c6c55764b3b76 */
/* loaded from: classes.dex */
public class WarBilling extends com.wardrumstudios.utils.WarBase {
    public void AddSKU(java.lang.String str) {
        java.lang.System.out.println("**** AddSKU: " + str);
    }

    public boolean InitBilling() {
        android.util.Log.i("WorldRPG.engine", "Billing unavailable in the WorldRPG bootstrap");
        return false;
    }

    public java.lang.String LocalizedPrice(java.lang.String str) {
        java.lang.System.out.println("**** LocalizedPrice: " + str);
        return "";
    }

    public boolean RequestPurchase(java.lang.String str) {
        android.util.Log.w("WorldRPG.engine", "Purchase unavailable in the WorldRPG bootstrap");
        return false;
    }

    public void SetBillingKey(java.lang.String str) {
        java.lang.System.out.println("**** SetBillingKey: " + str);
    }

    @Override // com.nvidia.devtech.NvEventQueueActivity
    public native void changeConnection(boolean z);

    @Override // com.nvidia.devtech.NvEventQueueActivity
    public native void notifyChange(java.lang.String str, int i);
}
