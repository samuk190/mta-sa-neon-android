package com.worldrpg.neonlauncher.game;

import java.util.LinkedHashMap;
import java.util.Iterator;
import java.util.Map;

/** Keeps live GUI fonts; bounds the cache of other font versions. */
final class DxFontCache<T> {
    interface Loader<T> { T load(String path, boolean bold); }
    private final int capacity;
    private final Loader<T> loader;
    private final LinkedHashMap<String, T> entries = new LinkedHashMap<>(16, 0.75f, true);
    private final LinkedHashMap<String, T> pinned = new LinkedHashMap<>();

    DxFontCache(int capacity, Loader<T> loader) {
        if (capacity < 1) throw new IllegalArgumentException("font cache capacity");
        this.capacity = capacity;
        this.loader = loader;
    }

    synchronized T get(String source, boolean bold) {
        String key = (bold ? "b:" : "n:") + source;
        T live = pinned.get(key);
        if (live != null) return live;
        T value = entries.get(key);
        if (value != null || entries.containsKey(key)) return value;
        value = loader.load(path(source), bold);
        entries.put(key, value);
        trim();
        return value;
    }

    // GUI font creation loads the face once. Cache pressure or replacing its
    // source file must not change a font while scripts still own its element.
    synchronized T pin(String source, boolean bold) {
        return pin(source, bold, get(source, bold));
    }

    // Keeps the validated face even if cache pressure occurred while measuring it.
    synchronized T pin(String source, boolean bold, T value) {
        if (value != null) {
            String key = (bold ? "b:" : "n:") + source;
            entries.remove(key);
            pinned.put(key, value);
        }
        return value;
    }

    // D is consumed in draw order, after queued draws using this instance.
    synchronized void releaseInstance(String id) {
        String prefix = "@font:" + id + ":";
        Iterator<Map.Entry<String, T>> iterator = pinned.entrySet().iterator();
        while (iterator.hasNext()) {
            Map.Entry<String, T> entry = iterator.next();
            if (!entry.getKey().startsWith(prefix, 2)) continue; // Skip weight prefix.
            entries.put(entry.getKey(), entry.getValue());
            iterator.remove();
        }
        trim();
    }

    private void trim() {
        while (entries.size() > capacity) {
            Map.Entry<String, T> oldest = entries.entrySet().iterator().next();
            entries.remove(oldest.getKey());
        }
    }

    static String path(String source) {
        if (source.startsWith("@font:")) {
            int end = source.indexOf(':', 6);
            if (end >= 0) return source.substring(end + 1);
        }
        return source;
    }
}
