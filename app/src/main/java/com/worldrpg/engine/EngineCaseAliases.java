package com.worldrpg.engine;

import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import java.io.File;
import java.io.IOException;
import java.util.Locale;

/** GTA's external file reader retains Windows-style uppercase asset names. */
public final class EngineCaseAliases {
    private EngineCaseAliases() { }

    public static void prepare(File privateRoot) throws IOException {
        File root = privateRoot.getCanonicalFile();
        prepareDirectory(root, root, 0);
    }

    private static void prepareDirectory(File root, File directory, int depth) throws IOException {
        if (depth > 32) throw new IOException("Game data directory is too deep");
        File[] entries = directory.listFiles();
        if (entries == null) throw new IOException("Cannot list game directory: " + directory);
        for (File entry : entries) {
            try {
                // Never recurse through aliases or any externally supplied symlink.
                if (OsConstants.S_ISLNK(Os.lstat(entry.getPath()).st_mode)) continue;
                if (!entry.getCanonicalPath().startsWith(root.getPath() + File.separator)) {
                    throw new IOException("Game data path escapes the private directory");
                }
                if (entry.isDirectory()) prepareDirectory(root, entry, depth + 1);
                String name = entry.getName();
                alias(entry, name.toUpperCase(Locale.ROOT));
                alias(entry, name.toLowerCase(Locale.ROOT));
                int dot = name.lastIndexOf('.');
                if (dot > 0) {
                    String stem = name.substring(0, dot);
                    String ext = name.substring(dot);
                    // Suporte a diferencas de caixa em gta.dat (ex: vegepart.IDE, barriers.IDE, paths.IPL)
                    // gta.dat also keeps mixed stems such as LAn.IDE.
                    alias(entry, stem + ext.toUpperCase(Locale.ROOT));
                    alias(entry, stem + ext.toLowerCase(Locale.ROOT));
                    alias(entry, stem.toLowerCase(Locale.ROOT) + ext.toUpperCase(Locale.ROOT));
                    alias(entry, stem.toUpperCase(Locale.ROOT) + ext.toLowerCase(Locale.ROOT));
                }
            } catch (ErrnoException error) {
                throw new IOException("Cannot prepare game data path: " + entry, error);
            }
        }
    }

    /** Resolve the exact mixed-case spelling used in a GTA level definition. */
    public static File resolveReference(File root, String path) throws IOException {
        File base = root.getCanonicalFile();
        File directory = base;
        for (String part : path.split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) {
                throw new IOException("Invalid game data reference");
            }
            File requested = new File(directory, part);
            if (!requested.exists()) {
                File[] entries = directory.listFiles();
                if (entries == null) throw new IOException("Cannot list game data directory");
                File match = null;
                for (File entry : entries) {
                    if (!entry.getName().equalsIgnoreCase(part)) continue;
                    if (match != null && !match.getCanonicalFile().equals(entry.getCanonicalFile())) {
                        throw new IOException("Ambiguous game data filename: " + path);
                    }
                    match = entry;
                }
                if (match == null) throw new IOException("Arquivo de mapa indisponivel: " + path);
                try {
                    alias(match, part);
                } catch (ErrnoException error) {
                    throw new IOException("Cannot prepare game data reference: " + path, error);
                }
            }
            directory = requested.getCanonicalFile();
            if (!directory.getPath().startsWith(base.getPath() + File.separator)) {
                throw new IOException("Game data reference escapes the private directory");
            }
        }
        return directory;
    }

    private static void alias(File target, String name) throws ErrnoException, IOException {
        if (name.equals(target.getName())) return;
        File alias = new File(target.getParentFile(), name);
        try {
            Os.lstat(alias.getPath());
            if (!alias.getCanonicalFile().equals(target.getCanonicalFile())) {
                throw new IOException("Conflicting game data filename: " + alias);
            }
        } catch (ErrnoException error) {
            if (error.errno != OsConstants.ENOENT) throw error;
            // Relative aliases stay within the same private directory. Never
            // rename/overwrite originals or duplicate large game archives.
            Os.symlink(target.getName(), alias.getPath());
        }
    }
}
