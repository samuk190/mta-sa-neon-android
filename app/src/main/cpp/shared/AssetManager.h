#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#ifdef __ANDROID__
#include <android/log.h>
#define ASSET_LOG_TAG "WorldRPGNeon"
#define ASSET_LOGI(...) __android_log_print(ANDROID_LOG_INFO, ASSET_LOG_TAG, __VA_ARGS__)
#define ASSET_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ASSET_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define ASSET_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define ASSET_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Sync {

/// Download progress callback: (bytesDownloaded, totalBytes, percentage 0..100)
using DownloadProgressCallback = std::function<void(int64_t downloaded, int64_t total, int percent)>;

/// Download completion callback: (success, errorMessage)
using DownloadCompleteCallback = std::function<void(bool success, const char* error)>;

/// Manages game asset loading and verification from local storage or configured CDN.
/// Handles manifest verification, SHA-256 checks, differential updates, and local caching.
class AssetManager {
public:
    AssetManager();
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    /// Initialize with local storage path for cached assets
    bool initialize(const char* localStoragePath);

    /// Set the base URL for remote game files (if using network CDN)
    void setCdnBaseUrl(const char* url);

    /// Fetch and parse the remote manifest (manifest_android.json)
    /// Returns true if manifest was successfully fetched and parsed
    bool fetchManifest();

    /// Check if all required assets are present and verified locally
    bool areAssetsReady() const;

    /// Start downloading missing/updated assets asynchronously
    void startDownload(DownloadProgressCallback onProgress,
                       DownloadCompleteCallback onComplete);

    /// Cancel an in-progress download
    void cancelDownload();

    /// Check if a download is currently running
    bool isDownloading() const;

    /// Get the local path for a specific asset file
    std::string getLocalAssetPath(const char* relativePath) const;

    /// Verify SHA-256 hash of a local file against manifest
    bool verifyFileHash(const char* relativePath) const;

    /// Get total size of assets that need downloading (bytes)
    int64_t getPendingDownloadSize() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Sync