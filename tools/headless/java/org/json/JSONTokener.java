package org.json;

public class JSONTokener {
    private final String input;
    private int pos = 0;

    public JSONTokener(String input) {
        this.input = input == null ? "" : input;
    }

    private void skipWhitespace() {
        while (pos < input.length()) {
            char c = input.charAt(pos);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\uFEFF') {
                pos++;
            } else {
                break;
            }
        }
    }

    public boolean more() {
        skipWhitespace();
        return pos < input.length();
    }

    public char nextClean() throws JSONException {
        skipWhitespace();
        if (pos >= input.length()) return 0;
        return input.charAt(pos++);
    }

    public Object nextValue() throws JSONException {
        skipWhitespace();
        if (pos >= input.length()) {
            throw new JSONException("End of input");
        }
        char c = input.charAt(pos);
        switch (c) {
            case '{': return readObject();
            case '[': return readArray();
            case '"': return readString();
            default: return readLiteral();
        }
    }

    private JSONObject readObject() throws JSONException {
        pos++; // '{'
        JSONObject obj = new JSONObject();
        skipWhitespace();
        if (pos < input.length() && input.charAt(pos) == '}') {
            pos++;
            return obj;
        }
        while (true) {
            skipWhitespace();
            if (pos >= input.length() || input.charAt(pos) != '"') {
                throw new JSONException("Expected string key at " + pos);
            }
            String key = readString();
            skipWhitespace();
            if (pos >= input.length() || input.charAt(pos) != ':') {
                throw new JSONException("Expected ':' at " + pos);
            }
            pos++;
            Object value = nextValue();
            obj.put(key, value == null ? JSONObject.NULL : value);
            skipWhitespace();
            if (pos >= input.length()) throw new JSONException("Unterminated object");
            char next = input.charAt(pos++);
            if (next == '}') return obj;
            if (next != ',') throw new JSONException("Expected ',' or '}' at " + (pos - 1));
        }
    }

    private JSONArray readArray() throws JSONException {
        pos++; // '['
        JSONArray arr = new JSONArray();
        skipWhitespace();
        if (pos < input.length() && input.charAt(pos) == ']') {
            pos++;
            return arr;
        }
        while (true) {
            arr.put(nextValue());
            skipWhitespace();
            if (pos >= input.length()) throw new JSONException("Unterminated array");
            char next = input.charAt(pos++);
            if (next == ']') return arr;
            if (next != ',') throw new JSONException("Expected ',' or ']' at " + (pos - 1));
        }
    }

    private String readString() throws JSONException {
        pos++; // '"'
        StringBuilder sb = new StringBuilder();
        while (pos < input.length()) {
            char c = input.charAt(pos++);
            if (c == '"') return sb.toString();
            if (c < 0x20) throw new JSONException("Unescaped control character in string");
            if (c == '\\') {
                if (pos >= input.length()) throw new JSONException("Unterminated escape");
                char e = input.charAt(pos++);
                switch (e) {
                    case '"': sb.append('"'); break;
                    case '\\': sb.append('\\'); break;
                    case '/': sb.append('/'); break;
                    case 'b': sb.append('\b'); break;
                    case 'f': sb.append('\f'); break;
                    case 'n': sb.append('\n'); break;
                    case 'r': sb.append('\r'); break;
                    case 't': sb.append('\t'); break;
                    case 'u':
                        if (pos + 4 > input.length()) throw new JSONException("Invalid unicode escape");
                        String hex = input.substring(pos, pos + 4);
                        pos += 4;
                        try {
                            sb.append((char) Integer.parseInt(hex, 16));
                        } catch (NumberFormatException ex) {
                            throw new JSONException("Invalid unicode escape: " + hex);
                        }
                        break;
                    default:
                        throw new JSONException("Invalid escape: \\" + e);
                }
            } else {
                sb.append(c);
            }
        }
        throw new JSONException("Unterminated string");
    }

    private Object readLiteral() throws JSONException {
        int start = pos;
        while (pos < input.length()) {
            char c = input.charAt(pos);
            if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
            pos++;
        }
        String token = input.substring(start, pos);
        if (token.isEmpty()) throw new JSONException("Expected value at " + start);
        if ("true".equals(token)) return Boolean.TRUE;
        if ("false".equals(token)) return Boolean.FALSE;
        if ("null".equals(token)) return JSONObject.NULL;
        try {
            if (token.indexOf('.') >= 0 || token.indexOf('e') >= 0 || token.indexOf('E') >= 0) {
                return Double.valueOf(token);
            }
            long l = Long.parseLong(token);
            if (l >= Integer.MIN_VALUE && l <= Integer.MAX_VALUE) return Integer.valueOf((int) l);
            return Long.valueOf(l);
        } catch (NumberFormatException e) {
            throw new JSONException("Invalid literal: " + token);
        }
    }
}
