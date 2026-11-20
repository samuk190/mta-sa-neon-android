package com.worldrpg.neonlauncher.game;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** Readable subset of Android 12+ exit traces. Schema:
 * https://android.googlesource.com/platform/system/core/+/main/debuggerd/proto/tombstone.proto */
final class NativeCrashTrace {
    private NativeCrashTrace() {}

    static String decode(byte[] data) throws IOException {
        Message tombstone = new Message(data, 0, data.length);
        List<Field> root = tombstone.fields();
        if (find(root, 5) == null || find(root, 10) == null) throw new IOException("Not an Android tombstone");
        StringBuilder out = new StringBuilder("Timestamp: ").append(string(root, 4))
                .append("\nBuild: ").append(string(root, 2)).append("\npid: ").append(number(root, 5))
                .append(", tid: ").append(number(root, 6)).append('\n');
        for (Field field : root) if (field.id == 9) out.append("Process: ").append(field.text()).append('\n');
        List<Field> signal = find(root, 10).message().fields();
        out.append("Signal: ").append(string(signal, 2)).append(" (").append(number(signal, 1))
                .append("), code: ").append(string(signal, 4)).append(" (Numeric ")
                .append(number(signal, 3)).append("), fault address: 0x").append(Long.toHexString(number(signal, 9))).append('\n');
        String abort = string(root, 14);
        if (!abort.isEmpty()) out.append("Abort: ").append(abort).append('\n');
        for (Field field : root) if (field.id == 15) {
            out.append("Cause: ").append(string(field.message().fields(), 1)).append('\n');
        }
        long crashingTid = number(root, 6);
        // Show the crashing thread first, then the other threads (bounded output).
        for (boolean primary : new boolean[]{true, false}) for (Field field : root) {
            if (field.id != 16) continue;
            List<Field> entry = field.message().fields();
            Field value = find(entry, 2);
            if (value == null) continue;
            List<Field> thread = value.message().fields();
            long tid = number(thread, 1);
            if ((tid == crashingTid) != primary) continue;
            out.append("\nThread ").append(tid).append(" ").append(string(thread, 2)).append('\n');
            if (primary) for (Field register : thread) if (register.id == 3) {
                List<Field> values = register.message().fields();
                out.append(string(values, 1)).append(" 0x").append(Long.toHexString(number(values, 2))).append('\n');
            }
            int frameIndex = 0;
            for (Field frame : thread) if (frame.id == 4) {
                List<Field> values = frame.message().fields();
                out.append(String.format(Locale.ROOT, "#%02d pc %016x ", frameIndex++, number(values, 1)))
                        .append(string(values, 6)).append(" (").append(string(values, 4))
                        .append('+').append(number(values, 5)).append(")\n");
                String build = string(values, 8);
                if (!build.isEmpty()) out.append("  BuildId: ").append(build).append('\n');
            }
            if (out.length() > 256 * 1024) return out.substring(0, 256 * 1024) + "\n[Trace truncated]\n";
        }
        if (tombstone.truncated) out.append("\n[Trace truncated by size limit]\n");
        return out.toString();
    }

    private static Field find(List<Field> fields, int id) {
        for (Field field : fields) if (field.id == id) return field;
        return null;
    }
    private static String string(List<Field> fields, int id) {
        Field field = find(fields, id);
        return field == null ? "" : field.text();
    }
    private static long number(List<Field> fields, int id) {
        Field field = find(fields, id);
        return field == null ? 0 : field.value;
    }

    private static final class Field {
        int id;
        long value;
        Message bytes;
        String text() { return bytes == null ? "" : new String(bytes.data, bytes.start, bytes.end - bytes.start, StandardCharsets.UTF_8); }
        Message message() throws IOException {
            if (bytes == null) throw new IOException("Invalid tombstone field type");
            return bytes;
        }
    }

    private static final class Message {
        final byte[] data;
        final int start, end;
        int cursor;
        boolean truncated;
        Message(byte[] data, int start, int end) { this.data = data; this.start = start; this.end = end; cursor = start; }
        long varint() throws IOException {
            long result = 0;
            for (int shift = 0; shift < 64; shift += 7) {
                if (cursor == end) throw new IOException("Truncated tombstone integer");
                int value = data[cursor++] & 255;
                if (shift == 63 && (value & 254) != 0) throw new IOException("Invalid tombstone integer");
                result |= (long) (value & 127) << shift;
                if ((value & 128) == 0) return result;
            }
            throw new IOException("Invalid tombstone integer");
        }
        List<Field> fields() throws IOException {
            List<Field> result = new ArrayList<>();
            cursor = start;
            while (cursor < end) {
                long tag = varint();
                Field field = new Field();
                field.id = (int) (tag >>> 3);
                if (field.id <= 0) throw new IOException("Invalid tombstone tag");
                switch ((int) (tag & 7)) {
                    case 0: field.value = varint(); break;
                    case 1: if (end - cursor < 8) throw new IOException("Truncated tombstone field"); cursor += 8; break;
                    case 5: if (end - cursor < 4) throw new IOException("Truncated tombstone field"); cursor += 4; break;
                    case 2:
                        long size = varint();
                        if (size < 0 || size > end - cursor) { truncated = true; return result; }
                        field.bytes = new Message(data, cursor, cursor + (int) size);
                        cursor += (int) size;
                        break;
                    default: throw new IOException("Unsupported tombstone wire type");
                }
                result.add(field);
                if (result.size() > 100000) throw new IOException("Too many tombstone fields");
            }
            return result;
        }
    }
}
