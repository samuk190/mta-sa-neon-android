package android.graphics;
public class Canvas {
    public static final java.util.List<String> draws = new java.util.ArrayList<>();
    public final java.util.List<Float> textY = new java.util.ArrayList<>();
    public final java.util.List<TextDraw> texts = new java.util.ArrayList<>();
    public final java.util.List<RectF> clips = new java.util.ArrayList<>();
    public static final java.util.List<RectDraw> rectangles = new java.util.ArrayList<>();
    public static final class RectDraw {
        public boolean target;
        public float left,top,right,bottom;
    }
    public static final class TextDraw {
        public String value;
        public int width, lines;
        public android.text.Layout.Alignment alignment;
        public float x, y, size;
        public Typeface face;
    }
    private final java.util.ArrayDeque<float[]> saved = new java.util.ArrayDeque<>();
    private boolean target;
    private float x, y;
    private float scaleX=1f,scaleY=1f;
    public int pixel;
    public Canvas() {}
    public Canvas(Bitmap bitmap) { target=true; }
    public int save() { saved.push(new float[]{x,y,scaleX,scaleY}); return saved.size(); }
    public void restore() { float[] value=saved.pop(); x=value[0]; y=value[1]; scaleX=value[2];scaleY=value[3]; }
    public void translate(float deltaX,float deltaY) { x+=deltaX*scaleX; y+=deltaY*scaleY; }
    public void scale(float x,float y) { scaleX*=x;scaleY*=y; }
    public void rotate(float angle,float x,float y) {}
    public boolean clipRect(float l,float t,float r,float b) { RectF clip=new RectF(); clip.set(l,t,r,b); clips.add(clip); return true; }
    public void recordText() { textY.add(y); }
    public void recordText(String value,int width,android.text.Layout.Alignment alignment,float size,Typeface face,int lines) {
        recordText(); TextDraw draw=new TextDraw(); draw.value=value; draw.width=width; draw.alignment=alignment;
        draw.size=size*scaleY; draw.face=face; draw.x=x; draw.y=y; draw.lines=lines; texts.add(draw);
    }
    public void drawRect(float l,float t,float r,float b,Paint paint) {
        RectDraw rect=new RectDraw();rect.target=target;rect.left=x+l*scaleX;rect.top=y+t*scaleY;
        rect.right=x+r*scaleX;rect.bottom=y+b*scaleY;rectangles.add(rect);
        PorterDuff.Mode mode=paint.xfer instanceof PorterDuffXfermode ? ((PorterDuffXfermode)paint.xfer).mode : PorterDuff.Mode.SRC_OVER;
        draws.add((target?"RT:":"screen:")+mode);
        int a=paint.color>>>24;
        if (mode==PorterDuff.Mode.SRC || a==255 && mode!=PorterDuff.Mode.ADD) pixel=paint.color;
        else if (mode==PorterDuff.Mode.ADD) pixel=0xff000000 | Math.min(255,((pixel>>16)&255)+((paint.color>>16)&255))<<16 | Math.min(255,((pixel>>8)&255)+((paint.color>>8)&255))<<8 | Math.min(255,(pixel&255)+(paint.color&255));
    }
}
