#include "AssetManager.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#ifdef __ANDROID__
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

namespace WorldRPG::Sync {

// ---------------------------------------------------------------------------
// Manifest entry: relative path + expected SHA-256 + size
// ---------------------------------------------------------------------------

struct ManifestEntry {
    std::string relativePath;
    std::string sha256;
    int64_t size = 0;
};

// ---------------------------------------------------------------------------
// Minimal SHA-256 implementation (FIPS 180-4) for file verification
// Avoids external crypto dependency on Android NDK
// ---------------------------------------------------------------------------

namespace sha256 {

static constexpr uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static inline uint32_t sigma0(uint32_t x) { return rotr(x,2) ^ rotr(x,13) ^ rotr(x,22); }
static inline uint32_t sigma1(uint32_t x) { return rotr(x,6) ^ rotr(x,11) ^ rotr(x,25); }
static inline uint32_t gamma0(uint32_t x) { return rotr(x,7) ^ rotr(x,18) ^ (x >> 3); }
static inline uint32_t gamma1(uint32_t x) { return rotr(x,17) ^ rotr(x,19) ^ (x >> 10); }

std::string hashFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";

    uint32_t h0=0x6a09e667, h1=0xbb67ae85, h2=0x3c6ef372, h3=0xa54ff53a;
    uint32_t h4=0x510e527f, h5=0x9b05688c, h6=0x1f83d9ab, h7=0x5be0cd19;

    uint64_t totalBits = 0;
    uint8_t block[64];

    while (true) {
        std::memset(block, 0, 64);
        file.read(reinterpret_cast<char*>(block), 64);
        auto bytesRead = static_cast<size_t>(file.gcount());
        totalBits += bytesRead * 8;

        if (bytesRead < 64) {
            // Padding
            block[bytesRead] = 0x80;
            if (bytesRead >= 56) {
                // Need extra block
                uint32_t w[64] = {};
                for (int i = 0; i < 16; ++i)
                    w[i] = (uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|
                           (uint32_t(block[i*4+2])<<8)|uint32_t(block[i*4+3]);
                for (int i = 16; i < 64; ++i)
                    w[i] = gamma1(w[i-2]) + w[i-7] + gamma0(w[i-15]) + w[i-16];
                uint32_t a=h0,b=h1,c=h2,d=h3,e=h4,f=h5,g=h6,hh=h7;
                for (int i = 0; i < 64; ++i) {
                    uint32_t t1 = hh + sigma1(e) + ch(e,f,g) + k[i] + w[i];
                    uint32_t t2 = sigma0(a) + maj(a,b,c);
                    hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
                }
                h0+=a; h1+=b; h2+=c; h3+=d; h4+=e; h5+=f; h6+=g; h7+=hh;
                std::memset(block, 0, 64);
            }
            // Write length as big-endian 64-bit at end of block
            for (int i = 0; i < 8; ++i)
                block[56 + i] = static_cast<uint8_t>((totalBits >> (56 - i*8)) & 0xFF);
        }

        // Process block
        uint32_t w[64] = {};
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|
                   (uint32_t(block[i*4+2])<<8)|uint32_t(block[i*4+3]);
        for (int i = 16; i < 64; ++i)
            w[i] = gamma1(w[i-2]) + w[i-7] + gamma0(w[i-15]) + w[i-16];

        uint32_t a=h0,b=h1,c=h2,d=h3,e=h4,f=h5,g=h6,hh=h7;
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + sigma1(e) + ch(e,f,g) + k[i] + w[i];
            uint32_t t2 = sigma0(a) + maj(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h0+=a; h1+=b; h2+=c; h3+=d; h4+=e; h5+=f; h6+=g; h7+=hh;

        if (bytesRead < 64) break;
    }

    char hex[65];
    std::snprintf(hex, sizeof(hex), "%08x%08x%08x%08x%08x%08x%08x%08x",
                  h0, h1, h2, h3, h4, h5, h6, h7);
    return std::string(hex);
}

} // namespace sha256

// ---------------------------------------------------------------------------
// AssetManager::Impl
// ---------------------------------------------------------------------------

struct AssetManager::Impl {
    std::string localStoragePath;
    std::string cdnBaseUrl = ""; // Configured dynamically via setCdnBaseUrl or environment
    std::vector<ManifestEntry> manifest;
    std::atomic<bool> downloading{false};
    std::atomic<bool> cancelRequested{false};
    bool manifestLoaded = false;

    /// Simple HTTP GET using POSIX sockets (no libcurl dependency)
    /// Returns response body or empty string on failure
    std::string httpGet(const std::string& url) {
        // Parse host and path from URL
        std::string host, path;
        size_t schemeEnd = url.find("://");
        if (schemeEnd == std::string::npos) return "";
        size_t hostStart = schemeEnd + 3;
        size_t pathStart = url.find('/', hostStart);
        if (pathStart == std::string::npos) {
            host = url.substr(hostStart);
            path = "/";
        } else {
            host = url.substr(hostStart, pathStart - hostStart);
            path = url.substr(pathStart);
        }

        // Resolve host
        struct addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), "443", &hints, &result) != 0 || !result) {
            ASSET_LOGE("httpGet: DNS resolution failed for %s", host.c_str());
            return "";
        }

        int sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (sock < 0) {
            freeaddrinfo(result);
            return "";
        }

        // Note: This is a plain TCP connect — HTTPS/TLS requires mbedtls or similar.
        // For production, use Android's HttpURLConnection via JNI or integrate mbedtls.
        // This stub demonstrates the architecture; actual downloads should go through
        // the Kotlin AssetDownloader which has proper TLS support.
        ASSET_LOGI("httpGet: TLS not implemented in native layer — use Kotlin AssetDownloader for actual downloads");
        close(sock);
        freeaddrinfo(result);
        return "";
    }

    /// Parse manifest_android.json (simple line-based parser for key-value entries)
    bool parseManifest(const std::string& jsonContent) {
        // Minimal JSON array parser for [{"path":"...","sha256":"...","size":N}, ...]
        // Production should use a proper JSON library (nlohmann/json or Android JSONObject via JNI)
        manifest.clear();

        size_t pos = 0;
        while ((pos = jsonContent.find("\"path\"", pos)) != std::string::npos) {
            ManifestEntry entry;

            // Extract path
            size_t pVal = jsonContent.find("\"", pos + 7);
            size_t pEnd = jsonContent.find("\"", pVal + 1);
            if (pVal == std::string::npos || pEnd == std::string::npos) break;
            entry.relativePath = jsonContent.substr(pVal + 1, pEnd - pVal - 1);

            // Extract sha256
            size_t sPos = jsonContent.find("\"sha256\"", pEnd);
            if (sPos != std::string::npos) {
                size_t sVal = jsonContent.find("\"", sPos + 9);
                size_t sEnd = jsonContent.find("\"", sVal + 1);
                if (sVal != std::string::npos && sEnd != std::string::npos)
                    entry.sha256 = jsonContent.substr(sVal + 1, sEnd - sVal - 1);
            }

            // Extract size
            size_t szPos = jsonContent.find("\"size\"", pEnd);
            if (szPos != std::string::npos) {
                size_t szVal = jsonContent.find(":", szPos + 6);
                if (szVal != std::string::npos) {
                    size_t szEnd = jsonContent.find_first_of(",}", szVal + 1);
                    std::string szStr = jsonContent.substr(szVal + 1, szEnd - szVal - 1);
                    // Trim whitespace
                    size_t first = szStr.find_first_not_of(" \t\n\r");
                    if (first != std::string::npos)
                        entry.size = std::stoll(szStr.substr(first));
                }
            }

            if (!entry.relativePath.empty()) {
                manifest.push_back(std::move(entry));
            }
            pos = pEnd + 1;
        }

        ASSET_LOGI("parseManifest: parsed %zu entries", manifest.size());
        return !manifest.empty();
    }
};

AssetManager::AssetManager() : pImpl_(std::make_unique<Impl>()) {}
AssetManager::~AssetManager() { cancelDownload(); }

bool AssetManager::initialize(const char* localStoragePath) {
    if (!localStoragePath) return false;
    pImpl_->localStoragePath = localStoragePath;

#ifdef __ANDROID__
    // Ensure storage directory exists
    std::string cmd = std::string("mkdir -p ") + localStoragePath;
    // Use mkdir directly instead of system()
    struct stat st{};
    if (stat(localStoragePath, &st) != 0) {
        mkdir(localStoragePath, 0755);
    }
#endif

    ASSET_LOGI("AssetManager: initialized with storage=%s", localStoragePath);
    return true;
}

void AssetManager::setCdnBaseUrl(const char* url) {
    if (url) pImpl_->cdnBaseUrl = url;
}

bool AssetManager::fetchManifest() {
    // In production, this should be called via Kotlin AssetDownloader with proper TLS
    // and the result passed back to native via JNI. The native layer handles parsing
    // and verification only.
    ASSET_LOGI("AssetManager: fetchManifest — use Kotlin AssetDownloader for HTTPS manifest fetch");
    return false;
}

bool AssetManager::areAssetsReady() const {
    if (!pImpl_->manifestLoaded || pImpl_->manifest.empty()) return false;

    for (const auto& entry : pImpl_->manifest) {
        std::string fullPath = pImpl_->localStoragePath + "/" + entry.relativePath;
        struct stat st{};
        if (stat(fullPath.c_str(), &st) != 0) return false;
        if (static_cast<int64_t>(st.st_size) != entry.size) return false;
    }
    return true;
}

void AssetManager::startDownload(DownloadProgressCallback onProgress,
                                  DownloadCompleteCallback onComplete) {
    if (pImpl_->downloading.load()) {
        if (onComplete) onComplete(false, "Download already in progress");
        return;
    }

    pImpl_->downloading.store(true);
    pImpl_->cancelRequested.store(false);

    // Actual download must go through Kotlin layer (TLS support).
    // This native method coordinates verification and progress tracking.
    std::thread([this, onProgress, onComplete]() {
        ASSET_LOGI("AssetManager: download thread started");

        int64_t totalSize = getPendingDownloadSize();
        int64_t downloaded = 0;

        // Verify existing files against manifest
        for (const auto& entry : pImpl_->manifest) {
            if (pImpl_->cancelRequested.load()) {
                pImpl_->downloading.store(false);
                if (onComplete) onComplete(false, "Download cancelled");
                return;
            }

            std::string fullPath = pImpl_->localStoragePath + "/" + entry.relativePath;
            struct stat st{};
            if (stat(fullPath.c_str(), &st) == 0 &&
                static_cast<int64_t>(st.st_size) == entry.size) {
                // File exists with correct size — verify hash
                std::string hash = sha256::hashFile(fullPath.c_str());
                if (hash == entry.sha256) {
                    downloaded += entry.size;
                    if (onProgress) {
                        int pct = totalSize > 0 ? static_cast<int>(downloaded * 100 / totalSize) : 100;
                        onProgress(downloaded, totalSize, pct);
                    }
                    continue;
                }
                ASSET_LOGI("AssetManager: hash mismatch for %s, needs re-download", entry.relativePath.c_str());
            }

            // File missing or invalid — mark for download via Kotlin layer
            ASSET_LOGI("AssetManager: needs download: %s (%lld bytes)", entry.relativePath.c_str(), (long long)entry.size);
        }

        pImpl_->downloading.store(false);
        if (onComplete) onComplete(true, nullptr);
        ASSET_LOGI("AssetManager: verification pass complete");
    }).detach();
}

void AssetManager::cancelDownload() {
    pImpl_->cancelRequested.store(true);
}

bool AssetManager::isDownloading() const {
    return pImpl_->downloading.load();
}

std::string AssetManager::getLocalAssetPath(const char* relativePath) const {
    if (!relativePath) return "";
    return pImpl_->localStoragePath + "/" + relativePath;
}

bool AssetManager::verifyFileHash(const char* relativePath) const {
    if (!relativePath) return false;

    for (const auto& entry : pImpl_->manifest) {
        if (entry.relativePath == relativePath) {
            std::string fullPath = pImpl_->localStoragePath + "/" + relativePath;
            std::string hash = sha256::hashFile(fullPath.c_str());
            return hash == entry.sha256;
        }
    }
    return false;
}

int64_t AssetManager::getPendingDownloadSize() const {
    int64_t total = 0;
    for (const auto& entry : pImpl_->manifest) {
        std::string fullPath = pImpl_->localStoragePath + "/" + entry.relativePath;
        struct stat st{};
        if (stat(fullPath.c_str(), &st) != 0 ||
            static_cast<int64_t>(st.st_size) != entry.size) {
            total += entry.size;
        }
    }
    return total;
}

} // namespace WorldRPG::Sync