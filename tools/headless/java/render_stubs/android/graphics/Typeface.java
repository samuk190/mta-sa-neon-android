package android.graphics;
public class Typeface {
    public final String version;
    public static int loads;
    private Typeface(String value) { version = value; }
    public static final Typeface DEFAULT = new Typeface("normal");
    public static final Typeface DEFAULT_BOLD = new Typeface("default");
    public static final int BOLD = 1;
    public static Typeface createFromFile(String path) {
        loads++;
        try { return new Typeface(new String(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(path)), java.nio.charset.StandardCharsets.UTF_8)); }
        catch (java.io.IOException error) { throw new RuntimeException(error); }
    }
    public static Typeface create(Typeface font, int style) { return font; }
}
