package android.text;
public class StaticLayout extends Layout {
    private final int lines;
    private final float extra;
    private final CharSequence value;
    private final int width;
    private final Alignment alignment;
    private final float size, spacing;
    private final android.graphics.Typeface face;
    private StaticLayout(Builder b) {
        value=b.value; width=b.width; alignment=b.alignment; size=b.paint.textSize;
        face=b.paint.typeface; spacing=b.paint.getFontSpacing(); extra=b.extra;
        int count=0;
        for(String line:value.toString().split("\n",-1)) count+=Math.max(1,(int)Math.ceil(line.length()*size*0.5f/width));
        lines=Math.min(count,b.maxLines);
    }
    public int getHeight() { return (int)Math.ceil(lines*spacing+(lines-1)*extra); }
    public int getLineCount() { return lines; }
    public void draw(android.graphics.Canvas canvas) { canvas.recordText(value.toString(),width,alignment,size,face,lines); }
    public static class Builder {
        private CharSequence value;
        private float extra;
        private int width, maxLines=Integer.MAX_VALUE;
        private TextPaint paint;
        private Alignment alignment=Alignment.ALIGN_NORMAL;
        public static Builder obtain(CharSequence text,int start,int end,TextPaint paint,int width) { Builder b=new Builder(); b.value=text.subSequence(start,end); b.paint=paint; b.width=width; return b; }
        public Builder setIncludePad(boolean value) { return this; }
        public Builder setAlignment(Alignment value) { alignment=value; return this; }
        public Builder setMaxLines(int value) { maxLines=value; return this; }
        public Builder setLineSpacing(float extra,float multiple) { this.extra=extra; return this; }
        public StaticLayout build() { return new StaticLayout(this); }
    }
}
