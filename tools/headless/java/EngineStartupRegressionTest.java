package com.worldrpg.neonlauncher.game;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/** Run with javac/java; exercises production GPU policy and native crash trace decoding. */
public final class EngineStartupRegressionTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static GpuTextureSupport gpu(String renderer, String extensions) {
        return new GpuTextureSupport("test vendor", renderer, extensions);
    }

    public static void main(String[] args) throws Exception {
        check(gpu("Mali-G68", "GL_OES_texture_npot").format.equals("etc"), "Mali ETC fallback");
        check(gpu("Adreno", "GL_EXT_texture_compression_s3tc").format.equals("dxt"), "S3TC path");
        check(gpu("Adreno", "GL_AMD_compressed_ATC_texture").format.equals("dxt"), "GTA ATC path");
        check(gpu("Other", "GL_EXT_texture_compression_dxt1").format.equals("dxt"), "DXT1 path");
        check(gpu("PowerVR", "GL_EXT_texture_compression_s3tc GL_IMG_texture_compression_pvrtc").format.equals("pvr"), "PVR priority");
        check(gpu("Adreno", "").format.equals("etc"), "Do not guess format by GPU name");
        check(gpu("Other", "GL_EXT_texture_compression_s3tc_fake").format.equals("etc"), "Exact extension tokens");
        check(gpu("Other", null).format.equals("etc"), "Null extension set");
        GpuTextureSupport mali = gpu("Mali-G68", "");
        Path root = Files.createTempDirectory("neon-gpu-regression");
        try {
            // Reproduce the CDN's former DXT-only installation.
            for (String path : gpu("Adreno", "GL_EXT_texture_compression_s3tc").requiredFiles()) {
                Path file = root.resolve(path);
                Files.createDirectories(file.getParent()); Files.write(file, new byte[]{1});
            }
            try { mali.validate(root.toFile()); throw new AssertionError("DXT-only install passed on Mali"); }
            catch (IOException error) {
                check(error.getMessage().contains("Missing game files for MALI (Mali-G68) (ETC textures)"), "English GPU error");
                check(error.getMessage().contains("mobile.etc.tmb"), "Names missing file");
                check(!error.getMessage().contains("player.etc"), "Player is always PVR");
                check(!error.getMessage().contains("menu.etc"), "Menu is always PVR");
            }
            for (String path : mali.requiredFiles()) {
                Path file = root.resolve(path);
                Files.createDirectories(file.getParent()); Files.write(file, new byte[]{1});
            }
            mali.validate(root.toFile());
            Path missing = root.resolve("texdb/mobile/mobile.etc.tmb");
            Files.write(missing, new byte[0]);
            try { mali.validate(root.toFile()); throw new AssertionError("Empty thumbnail table accepted"); }
            catch (IOException expected) { check(expected.getMessage().contains("mobile.etc.tmb"), "Empty file diagnostic"); }
            Files.write(missing, new byte[]{1});
            Path cutscene = root.resolve("texdb/cutscene/cutscene.txt");
            Files.createDirectories(cutscene.getParent()); Files.write(cutscene, new byte[]{1});
            try { mali.validate(root.toFile()); throw new AssertionError("Installed cutscene catalogue bypassed format check"); }
            catch (IOException expected) { check(expected.getMessage().contains("cutscene.etc.tmb"), "Optional cutscene check"); }
        } finally {
            try (var paths = Files.walk(root)) {
                for (Path path : paths.sorted(java.util.Comparator.reverseOrder()).toList()) Files.delete(path);
            }
        }
        byte[] frame = cat(num(1, 0x286f68), str(4, "TextureDatabaseRuntime::SortEntries(bool)"), num(5, 624), str(6, "libGTASA.so"), str(8, "abc123"));
        byte[] thread = cat(num(1, 23052), str(2, "NVEvent"), msg(3, cat(str(1, "x14"), num(2, 0x67b))), msg(4, frame));
        byte[] trace = cat(num(5, 23026), num(6, 23052), str(2, "samsung/a54"), str(4, "2026-11-16"),
                str(9, "test:engine"), msg(10, cat(num(1, 11), str(2, "SIGSEGV"), num(3, 1), str(4, "SEGV_MAPERR"), num(9, 0x688))),
                msg(15, str(1, "null pointer dereference")), msg(16, cat(num(1, 23052), msg(2, thread))), str(999, "ignored OEM field"));
        String decoded = NativeCrashTrace.decode(trace);
        for (String expected : List.of("SIGSEGV", "0x688", "null pointer dereference", "NVEvent", "0000000000286f68", "SortEntries(bool)+624", "x14 0x67b", "BuildId: abc123")) {
            check(decoded.contains(expected), "Missing trace detail: " + expected);
        }
        byte[] zeroFault = cat(num(5, 1), msg(10, cat(num(1, 11), str(2, "SIGSEGV"))));
        check(NativeCrashTrace.decode(zeroFault).contains("fault address: 0x0"), "Proto3 omitted default address");
        try { NativeCrashTrace.decode(new byte[]{0}); throw new AssertionError("Invalid trace accepted"); }
        catch (IOException expected) { }
        try { NativeCrashTrace.decode(new byte[]{8, (byte) 128}); throw new AssertionError("Truncated varint accepted"); }
        catch (IOException expected) { }
        if (args.length > 0) {
            for (GpuTextureSupport profile : List.of(gpu("Mali", ""),
                    gpu("Adreno", "GL_EXT_texture_compression_s3tc"),
                    gpu("PowerVR", "GL_IMG_texture_compression_pvrtc"))) {
                profile.validate(new java.io.File(args[0]));
            }
            System.out.println("PASS: real GPU file availability for ETC, DXT and PVR");
        }
        System.out.println("PASS: GPU selection, missing/empty assets, optional cutscenes and readable native tombstones");
    }

    private static byte[] cat(byte[]... values) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (byte[] value : values) out.write(value);
        return out.toByteArray();
    }
    private static void varint(ByteArrayOutputStream out, long value) {
        while ((value & ~127L) != 0) { out.write((int) value & 127 | 128); value >>>= 7; }
        out.write((int) value);
    }
    private static byte[] num(int id, long value) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        varint(out, (long) id << 3); varint(out, value); return out.toByteArray();
    }
    private static byte[] msg(int id, byte[] value) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        varint(out, (long) id << 3 | 2); varint(out, value.length); out.write(value); return out.toByteArray();
    }
    private static byte[] str(int id, String text) throws IOException { return msg(id, text.getBytes(StandardCharsets.UTF_8)); }
}
