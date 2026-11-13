#pragma once

// HTTP client-file cache for resource files listed in RESOURCE_START.
//
// Desktop reference: CResourceFileDownloadManager::BeginResourceFileDownload
// (URL is "<server>/<resource>/<file>") and CDownloadableResource checksum
// comparison. Files are cached under <cacheRoot>/<resource>/<file>. A cached
// copy whose size and CRC-32 (and MD5 when the packet carries one) match is
// reused. Downloads run on worker threads and never on the game/network thread.

#include "../shared/ResourceFiles.h"

#include <cstdint>
#include <string>
#include <vector>

namespace WorldRPG::Net {

struct NeonJoinInfo;

namespace HttpDownloader {

struct Sample {
    std::string path;        // local path of a verified file
    uint64_t size = 0;
    uint32_t crc = 0;
    std::string md5Hex;      // MD5 computed from the downloaded bytes
    bool packetHadMd5 = false;
    std::string packetMd5Hex;  // MD5 from RESOURCE_START (empty when absent)
    uint32_t packetCrc = 0;
};

struct Stats {
    uint64_t queued = 0;
    uint64_t skippedDuplicate = 0;  // same file already in flight
    uint64_t cached = 0;            // local copy matched, no transfer
    uint64_t downloaded = 0;        // transferred and verified
    uint64_t md5Verified = 0;       // verified against the packet MD5 (and CRC)
    uint64_t crcVerified = 0;       // packet had no MD5: verified by CRC-32 and size only
    uint64_t mismatches = 0;        // bytes arrived but size, CRC or MD5 differ; discarded
    uint64_t failed = 0;            // connect, HTTP status, short body or I/O error
    uint64_t bytes = 0;             // bytes of verified downloads
    uint64_t dropped = 0;           // queued work discarded by Shutdown
    std::vector<std::string> failures;  // first 20 failure notes
    std::vector<Sample> samples;        // first 4 verified downloads
};

// Directory that holds the cache. Empty disables downloads (default).
// Android: call from the JNI join path before the first RESOURCE_START
// (see the TODO(http-cache) in jni/JNI_OnLoad.cpp).
void SetResourceCacheRoot(const std::string& root);

// Base URL of the HTTP file service, "http://host:port" (no trailing slash).
// Empty disables downloads. Build it with BuildServerBase after SERVER_JOINEDGAME.
void SetServerBase(const std::string& baseUrl);

// Desktop CPacketHandler::Packet_ServerJoined: download type 1 means
// http://<host>:<httpPort>, type 2 means the external URL, type 0 means none.
std::string BuildServerBase(const NeonJoinInfo& info, const std::string& host);

enum class EnqueueResult : uint8_t {
    Queued,     // accepted; a Completion follows
    Duplicate,  // the same file is already in flight; its Completion follows
    Disabled,   // no cache root or no HTTP service yet; no Completion will come
    Stopped,    // shut down; no Completion will come
};

// Queues one client file (or CLIENT_SCRIPT) for download.
EnqueueResult Enqueue(const std::string& resourceName, const ResourceFile& file);

// One finished request, in completion order. ok is true for a verified local copy
// (cached or downloaded); otherwise note says why the file is not available.
struct Completion {
    std::string resource;
    std::string file;
    bool ok = false;
    std::string note;
};

// Returns and clears the completions recorded since the last call (thread-safe).
std::vector<Completion> TakeCompletions();

// Cache path of a file, or empty when no cache root is set.
std::string CachedFilePath(const std::string& resourceName, const std::string& fileName);

// Blocks until the queue is empty and no transfer runs, or the timeout expires.
bool WaitIdle(int timeoutMs);

Stats GetStats();

// Drops queued work, waits for running transfers, joins the workers.
void Shutdown();

} // namespace HttpDownloader
} // namespace WorldRPG::Net
