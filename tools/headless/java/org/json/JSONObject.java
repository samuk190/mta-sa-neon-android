package org.json;

import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.Map;

public class JSONObject {
    public static final Object NULL = new Object() {
        @Override
        public boolean equals(Object o) {
            return o == this || o == null;
        }

        @Override
        public String toString() {
            return "null";
        }
    };

    private final Map<String, Object> entries = new LinkedHashMap<>();

    public JSONObject() {}

    public JSONObject(String json) throws JSONException {
        JSONTokener tokener = new JSONTokener(json);
        Object value = tokener.nextValue();
        if (!(value instanceof JSONObject)) {
            throw new JSONException("Value is not a JSONObject");
        }
        if (tokener.more()) {
            throw new JSONException("Trailing characters after JSONObject");
        }
        entries.putAll(((JSONObject) value).entries);
    }

    public int length() {
        return entries.size();
    }

    public boolean has(String name) {
        return entries.containsKey(name);
    }

    public boolean isNull(String name) {
        Object value = entries.get(name);
        return value == null || value == NULL;
    }

    public Iterator<String> keys() {
        return entries.keySet().iterator();
    }

    public JSONObject put(String name, Object value) throws JSONException {
        if (name == null) throw new JSONException("Null key");
        if (value == null) {
            entries.remove(name);
        } else {
            entries.put(name, value);
        }
        return this;
    }

    public JSONObject put(String name, int value) throws JSONException {
        return put(name, Integer.valueOf(value));
    }

    public JSONObject put(String name, long value) throws JSONException {
        return put(name, Long.valueOf(value));
    }

    public JSONObject put(String name, double value) throws JSONException {
        if (Double.isNaN(value) || Double.isInfinite(value)) {
            throw new JSONException("Forbidden numeric value: " + value);
        }
        return put(name, Double.valueOf(value));
    }

    public JSONObject put(String name, boolean value) throws JSONException {
        return put(name, Boolean.valueOf(value));
    }

    public Object opt(String name) {
        return name == null ? null : entries.get(name);
    }

    public Object get(String name) throws JSONException {
        Object value = opt(name);
        if (value == null) throw new JSONException("No value for " + name);
        return value;
    }

    public String getString(String name) throws JSONException {
        Object value = get(name);
        if (value == NULL) throw new JSONException("Null value for " + name);
        if (value instanceof String) return (String) value;
        return String.valueOf(value);
    }

    public String optString(String name) {
        return optString(name, "");
    }

    public String optString(String name, String fallback) {
        Object value = opt(name);
        if (value == null || value == NULL) return fallback;
        return String.valueOf(value);
    }

    public int getInt(String name) throws JSONException {
        Object value = get(name);
        if (value instanceof Number) return ((Number) value).intValue();
        try {
            return Integer.parseInt(String.valueOf(value));
        } catch (NumberFormatException e) {
            throw new JSONException("Value for " + name + " is not an int");
        }
    }

    public int optInt(String name) {
        return optInt(name, 0);
    }

    public int optInt(String name, int fallback) {
        Object value = opt(name);
        if (value instanceof Number) return ((Number) value).intValue();
        if (value instanceof String) {
            try { return Integer.parseInt((String) value); } catch (NumberFormatException ignored) {}
        }
        return fallback;
    }

    public long getLong(String name) throws JSONException {
        Object value = get(name);
        if (value instanceof Number) return ((Number) value).longValue();
        try {
            return Long.parseLong(String.valueOf(value));
        } catch (NumberFormatException e) {
            throw new JSONException("Value for " + name + " is not a long");
        }
    }

    public long optLong(String name) {
        return optLong(name, 0L);
    }

    public long optLong(String name, long fallback) {
        Object value = opt(name);
        if (value instanceof Number) return ((Number) value).longValue();
        if (value instanceof String) {
            try { return Long.parseLong((String) value); } catch (NumberFormatException ignored) {}
        }
        return fallback;
    }

    public double getDouble(String name) throws JSONException {
        Object value = get(name);
        if (value instanceof Number) return ((Number) value).doubleValue();
        try {
            return Double.parseDouble(String.valueOf(value));
        } catch (NumberFormatException e) {
            throw new JSONException("Value for " + name + " is not a double");
        }
    }

    public double optDouble(String name, double fallback) {
        Object value = opt(name);
        if (value instanceof Number) return ((Number) value).doubleValue();
        if (value instanceof String) {
            try { return Double.parseDouble((String) value); } catch (NumberFormatException ignored) {}
        }
        return fallback;
    }

    public boolean getBoolean(String name) throws JSONException {
        Object value = get(name);
        if (value instanceof Boolean) return (Boolean) value;
        throw new JSONException("Value for " + name + " is not a boolean");
    }

    public boolean optBoolean(String name) {
        return optBoolean(name, false);
    }

    public boolean optBoolean(String name, boolean fallback) {
        Object value = opt(name);
        if (value instanceof Boolean) return (Boolean) value;
        if (value instanceof String) {
            if ("true".equalsIgnoreCase((String) value)) return true;
            if ("false".equalsIgnoreCase((String) value)) return false;
        }
        return fallback;
    }

    public JSONObject getJSONObject(String name) throws JSONException {
        Object value = get(name);
        if (value instanceof JSONObject) return (JSONObject) value;
        throw new JSONException("Value for " + name + " is not a JSONObject");
    }

    public JSONObject optJSONObject(String name) {
        Object value = opt(name);
        return value instanceof JSONObject ? (JSONObject) value : null;
    }

    public JSONArray getJSONArray(String name) throws JSONException {
        Object value = get(name);
        if (value instanceof JSONArray) return (JSONArray) value;
        throw new JSONException("Value for " + name + " is not a JSONArray");
    }

    public JSONArray optJSONArray(String name) {
        Object value = opt(name);
        return value instanceof JSONArray ? (JSONArray) value : null;
    }

    public static String quote(String data) {
        if (data == null) return "\"\"";
        StringBuilder sb = new StringBuilder(data.length() + 2);
        sb.append('"');
        for (int i = 0; i < data.length(); i++) {
            char c = data.charAt(i);
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\b': sb.append("\\b"); break;
                case '\f': sb.append("\\f"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < 0x20) {
                        sb.append(String.format("\\u%04x", (int) c));
                    } else {
                        sb.append(c);
                    }
            }
        }
        sb.append('"');
        return sb.toString();
    }

    static void appendValue(StringBuilder sb, Object value) {
        if (value == null || value == NULL) {
            sb.append("null");
        } else if (value instanceof String) {
            sb.append(quote((String) value));
        } else if (value instanceof Boolean || value instanceof Number || value instanceof JSONObject || value instanceof JSONArray) {
            sb.append(value.toString());
        } else {
            sb.append(quote(String.valueOf(value)));
        }
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        sb.append('{');
        boolean first = true;
        for (Map.Entry<String, Object> e : entries.entrySet()) {
            if (!first) sb.append(',');
            first = false;
            sb.append(quote(e.getKey())).append(':');
            appendValue(sb, e.getValue());
        }
        sb.append('}');
        return sb.toString();
    }
}
