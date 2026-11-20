package com.caverock.androidsvg;
public class SVG {
    public static SVG getFromString(String value) throws SVGParseException { return new SVG(); }
    public android.graphics.RectF getDocumentViewBox() { return null; }
    public float getDocumentWidth() { return 1; }
    public float getDocumentHeight() { return 1; }
    public void setDocumentViewBox(float x,float y,float w,float h) {}
    public void setDocumentWidth(float value) {}
    public void setDocumentHeight(float value) {}
    public void renderToCanvas(android.graphics.Canvas canvas) {}
}
