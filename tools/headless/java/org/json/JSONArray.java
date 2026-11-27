package org.json;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

public class JSONArray {
    private final List<Object> values = new ArrayList<>();

    public JSONArray() {}

    public JSONArray(String json) throws JSONException {
        JSONTokener tokener = new JSONTokener(json);
        Object value = tokener.nextValue();
        if (!(value instanceof JSONArray)) {
            throw new JSONException("Value is not a JSONArray");
        }
        if (tokener.more()) {
            throw new JSONException("Trailing characters after JSONArray");
        }
        values.addAll(((JSONArray) value).values);
    }

    public JSONArray(Collection<?> collection) {
        if (collection != null) {
            for (Object item : collection) {
                values.add(item == null ? JSONObject.NULL : item);
            }
        }
    }

    public int length() {
        return values.size();
    }

    public JSONArray put(Object value) {
        values.add(value == null ? JSONObject.NULL : value);
        return this;
    }

    public JSONArray put(int value) {
        values.add(value);
        return this;
    }

    public JSONArray put(long value) {
        values.add(value);
        return this;
    }

    public JSONArray put(double value) throws JSONException {
        if (Double.isNaN(value) || Double.isInfinite(value)) {
            throw new JSONException("Forbidden numeric value: " + value);
        }
        values.add(value);
        return this;
    }

    public JSONArray put(boolean value) {
        values.add(value);
        return this;
    }

    public Object opt(int index) {
        if (index < 0 || index >= values.size()) return null;
        return values.get(index);
    }

    public Object get(int index) throws JSONException {
        Object value = opt(index);
        if (value == null) throw new JSONException("Index " + index + " out of range");
        return value;
    }

    public boolean isNull(int index) {
        Object value = opt(index);
        return value == null || value == JSONObject.NULL;
    }

    public String getString(int index) throws JSONException {
        Object value = get(index);
        if (value == JSONObject.NULL) throw new JSONException("Value at " + index + " is null");
        if (value instanceof String) return (String) value;
        return String.valueOf(value);
    }

    public String optString(int index) {
        return optString(index, "");
    }

    public String optString(int index, String fallback) {
        Object value = opt(index);
        if (value == null || value == JSONObject.NULL) return fallback;
        return String.valueOf(value);
    }

    public int getInt(int index) throws JSONException {
        Object value = get(index);
        if (value instanceof Number) return ((Number) value).intValue();
        try {
            return Integer.parseInt(String.valueOf(value));
        } catch (NumberFormatException e) {
            throw new JSONException("Value at " + index + " is not an int");
        }
    }

    public long getLong(int index) throws JSONException {
        Object value = get(index);
        if (value instanceof Number) return ((Number) value).longValue();
        try {
            return Long.parseLong(String.valueOf(value));
        } catch (NumberFormatException e) {
            throw new JSONException("Value at " + index + " is not a long");
        }
    }

    public boolean getBoolean(int index) throws JSONException {
        Object value = get(index);
        if (value instanceof Boolean) return (Boolean) value;
        throw new JSONException("Value at " + index + " is not a boolean");
    }

    public JSONObject getJSONObject(int index) throws JSONException {
        Object value = get(index);
        if (value instanceof JSONObject) return (JSONObject) value;
        throw new JSONException("Value at " + index + " is not a JSONObject");
    }

    public JSONObject optJSONObject(int index) {
        Object value = opt(index);
        return value instanceof JSONObject ? (JSONObject) value : null;
    }

    public JSONArray getJSONArray(int index) throws JSONException {
        Object value = get(index);
        if (value instanceof JSONArray) return (JSONArray) value;
        throw new JSONException("Value at " + index + " is not a JSONArray");
    }

    public JSONArray optJSONArray(int index) {
        Object value = opt(index);
        return value instanceof JSONArray ? (JSONArray) value : null;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        sb.append('[');
        for (int i = 0; i < values.size(); i++) {
            if (i > 0) sb.append(',');
            JSONObject.appendValue(sb, values.get(i));
        }
        sb.append(']');
        return sb.toString();
    }
}
