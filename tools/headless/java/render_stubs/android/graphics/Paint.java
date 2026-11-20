package android.graphics;
public class Paint {
    public static final int ANTI_ALIAS_FLAG=1, FILTER_BITMAP_FLAG=2;
    public enum Style { FILL, STROKE }
    public Xfermode xfer;
    public int color;
    public float textSize=15;
    public Typeface typeface = Typeface.DEFAULT_BOLD;
    public Paint() {}
    public Paint(int flags) {}
    public Xfermode setXfermode(Xfermode value) { xfer=value; return value; }
    public Typeface setTypeface(Typeface font) { typeface=font; return font; }
    public void setShadowLayer(float a,float b,float c,int d) {}
    public void setStyle(Style style) {}
    public void setColor(int value) { color=value; }
    public void setTextSize(float size) { textSize=size; }
    public float measureText(String value) { return value.length()*8; }
    public float getFontSpacing() { return textSize * 1.2f; }
}
