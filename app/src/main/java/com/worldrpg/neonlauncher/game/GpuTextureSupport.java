package com.worldrpg.neonlauncher.game;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/** The texture selection used by GTA SA 2.10 ARM64, rather than a phone-model allowlist. */
public final class GpuTextureSupport {
    public final String vendor;
    public final String renderer;
    public final String format;

    public GpuTextureSupport(String vendor, String renderer, String extensions) {
        this.vendor = vendor == null ? "Unknown" : vendor;
        this.renderer = renderer == null ? "Unknown GPU" : renderer;
        Set<String> caps = new HashSet<>(Arrays.asList(extensions == null ? new String[0] : extensions.trim().split("\\s+")));
        // RQ_Command_rqInit 0x261240-0x2612c4 sets RQCaps[3..5].
        // TextureDatabaseRuntime::Load 0x2877ec-0x287818 maps these to 4/1/5.
        if (caps.contains("GL_IMG_texture_compression_pvrtc")) format = "pvr";
        else if (caps.contains("GL_EXT_texture_compression_dxt1")
                || caps.contains("GL_EXT_texture_compression_s3tc")
                || caps.contains("GL_AMD_compressed_ATC_texture")) format = "dxt";
        else format = "etc";
    }

    public String label() {
        String lower = renderer.toLowerCase(Locale.ROOT);
        if (lower.contains("mali")) return "MALI (" + renderer + ")";
        if (lower.contains("adreno")) return "Adreno (" + renderer + ")";
        if (lower.contains("powervr")) return "PowerVR (" + renderer + ")";
        return renderer;
    }

    public List<String> requiredFiles() {
        List<String> paths = new ArrayList<>();
        // CGame::InitialiseRenderWare 0x55b684-0x55b720: four automatic DBs,
        // followed by player and menu explicitly in PVR. UseHiDetailPlayer is false.
        for (String name : new String[]{"mobile", "txd", "gta3", "gta_int", "player", "menu"}) {
            String prefix = "texdb/" + name + "/" + name;
            String selected = name.equals("player") || name.equals("menu") ? "pvr" : format;
            paths.add(prefix + ".txt");
            for (String suffix : new String[]{"tmb", "toc", "dat"}) paths.add(prefix + "." + selected + "." + suffix);
        }
        return paths;
    }

    public void validate(File root) throws IOException {
        List<String> missing = new ArrayList<>();
        List<String> paths = requiredFiles();
        // This port can omit cutscenes. If their catalogue is installed, GTA will load it too.
        if (new File(root, "texdb/cutscene/cutscene.txt").isFile()) {
            for (String suffix : new String[]{"tmb", "toc", "dat"}) {
                paths.add("texdb/cutscene/cutscene." + format + "." + suffix);
            }
        }
        for (String path : paths) {
            File file = new File(root, path);
            try (FileInputStream input = new FileInputStream(file)) {
                if (!file.isFile() || file.length() == 0 || input.read() < 0) missing.add(path);
            } catch (IOException | SecurityException error) {
                missing.add(path);
            }
        }
        if (!missing.isEmpty()) {
            throw new IOException("Missing game files for " + label() + " (" + format.toUpperCase(Locale.ROOT)
                    + " textures).\n\nSync game assets in Settings, then try again.\n\nMissing or unreadable files:\n"
                    + String.join("\n", missing));
        }
    }
}
