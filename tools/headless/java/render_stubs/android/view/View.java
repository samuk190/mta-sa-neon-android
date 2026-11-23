package android.view;
public class View {
    public static final int GONE=8,VISIBLE=0;
    private int visibility;
    public int width=3120,height=1440;
    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public View(android.content.Context context) {}
    public void setClickable(boolean value) {}
    public void setVisibility(int value) { visibility=value; }
    public int getVisibility() { return visibility; }
    public void invalidate() {}
    protected void onDraw(android.graphics.Canvas canvas) {}
    public boolean onTouchEvent(android.view.MotionEvent event) { return false; }
}
