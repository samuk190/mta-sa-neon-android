package com.nvidia.devtech;

import android.content.Context;
import android.content.res.AssetManager;
import android.util.Log;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Asset callbacks retain their original JNI class/method names. */
public class NvAPKFileHelper {
    private static final NvAPKFileHelper instance = new NvAPKFileHelper();
    private static final int MAX_ASSET_COUNT = 100000;
    private static final int MAX_READ_BYTES = 64 * 1024 * 1024;
    String[] apkFiles = new String[0];
    int apkCount = 0;
    boolean hasAPKFiles = false;
    int myApkCount = 0;
    private Context context;

    public static NvAPKFileHelper getInstance() { return instance; }

    public void setContext(Context context) {
        this.context = context.getApplicationContext();
        this.hasAPKFiles = false;
    }

    private int findInAPKFiles(String name) {
        if (name == null) return -1;
        for (int i = 0; i < this.myApkCount; ++i) {
            // The optional .mp3 suffix is an APK packaging convention from GTA.
            if (name.equalsIgnoreCase(this.apkFiles[i])
                    || (name + ".mp3").equalsIgnoreCase(this.apkFiles[i])) return i;
        }
        return -1;
    }

    public void AddAssetFile(String name) {
        if (this.myApkCount >= MAX_ASSET_COUNT) return;
        if (this.myApkCount == this.apkFiles.length) {
            this.apkFiles = Arrays.copyOf(this.apkFiles, Math.min(MAX_ASSET_COUNT,
                    Math.max(16, this.apkFiles.length * 2)));
        }
        this.apkFiles[this.myApkCount++] = name;
    }

    private void listAssets(AssetManager assets, String path, List<String> files) throws IOException {
        String[] children = assets.list(path);
        if (children == null) throw new IOException("Asset listing unavailable: " + path);
        if (children.length == 0 && path.length() > 0) {
            // list() is empty for both files and empty directories. Only accept
            // a path that the AssetManager can really open as a file.
            try (InputStream ignored = assets.open(path)) {
                if (files.size() >= MAX_ASSET_COUNT) throw new IOException("Too many packaged assets");
                files.add(path);
            } catch (java.io.FileNotFoundException emptyDirectory) {
                // An empty packaged directory is not a readable asset.
            }
            return;
        }
        for (String child : children) {
            listAssets(assets, path.length() == 0 ? child : path + "/" + child, files);
        }
    }

    public void GetAssetList() {
        List<String> files = new ArrayList<String>();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                this.context.getAssets().open("assetfile.txt"), StandardCharsets.UTF_8))) {
            int expected = Integer.parseInt(reader.readLine());
            if (expected < 0 || expected > MAX_ASSET_COUNT) throw new IOException("Invalid asset count");
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.length() == 0) continue;
                if (files.size() >= expected) throw new IOException("Asset catalog count mismatch");
                files.add(line);
            }
            if (files.size() != expected) throw new IOException("Incomplete asset catalog");
        } catch (IOException | NumberFormatException error) {
            files.clear();
            try {
                listAssets(this.context.getAssets(), "", files);
            } catch (IOException listingError) {
                Log.e("WorldRPG.engine", "Could not enumerate packaged assets", listingError);
            }
        }
        this.apkFiles = files.toArray(new String[files.size()]);
        this.myApkCount = this.apkFiles.length;
        this.apkCount = this.myApkCount;
    }

    public int getDirectoryListing(AssetManager assets, String directory, int mode) {
        List<String> files = new ArrayList<String>();
        try {
            listAssets(assets, directory, files);
            if (mode > 0) {
                for (String file : files) AddAssetFile(file);
            } else {
                this.apkCount += files.size();
            }
        } catch (IOException error) {
            Log.e("WorldRPG.engine", "Could not enumerate asset directory: " + directory, error);
        }
        return 0;
    }

    public NvAPKFile openFileAndroid(String name) {
        if (this.context == null || name == null) return null;
        if (!this.hasAPKFiles) {
            GetAssetList();
            this.hasAPKFiles = true;
        }
        int index = findInAPKFiles(name);
        if (index < 0) return null;
        try {
            NvAPKFile file = new NvAPKFile();
            file.is = this.context.getAssets().open(this.apkFiles[index]);
            file.length = file.is.available();
            file.position = 0;
            file.bufferSize = 1024;
            file.data = new byte[file.bufferSize];
            file.is.mark(Integer.MAX_VALUE);
            return file;
        } catch (IOException error) {
            Log.e("WorldRPG.engine", "Could not open packaged asset: " + name, error);
            return null;
        }
    }

    public void readFileAndroid(NvAPKFile file, int requested) {
        if (file == null || file.is == null || requested < 0 || requested > MAX_READ_BYTES) return;
        if (requested > file.bufferSize) {
            file.data = new byte[requested];
            file.bufferSize = requested;
        }
        int read = 0;
        try {
            while (read < requested) {
                int count = file.is.read(file.data, read, requested - read);
                if (count < 0) break;
                if (count == 0) {
                    int value = file.is.read();
                    if (value < 0) break;
                    file.data[read++] = (byte) value;
                } else {
                    read += count;
                }
            }
        } catch (IOException error) {
            Log.e("WorldRPG.engine", "Could not finish packaged-asset read", error);
        }
        file.position += read;
        Arrays.fill(file.data, read, requested, (byte) 0);
    }

    public long seekFileAndroid(NvAPKFile file, int requested) {
        if (file == null || file.is == null || requested < 0) return -1;
        long position = 0;
        try {
            file.is.reset();
            long remaining = Math.min(requested, file.length);
            while (remaining > 0) {
                long skipped = file.is.skip(remaining);
                if (skipped == 0) {
                    if (file.is.read() < 0) break;
                    skipped = 1;
                }
                position += skipped;
                remaining -= skipped;
            }
            file.position = (int) position;
            return position;
        } catch (IOException error) {
            Log.e("WorldRPG.engine", "Could not seek packaged asset", error);
            return -1;
        }
    }

    public void closeFileAndroid(NvAPKFile file) {
        if (file == null) return;
        try {
            if (file.is != null) file.is.close();
        } catch (IOException error) {
            Log.w("WorldRPG.engine", "Could not close packaged asset", error);
        }
        file.is = null;
        file.data = new byte[0];
        file.bufferSize = 0;
    }
}
