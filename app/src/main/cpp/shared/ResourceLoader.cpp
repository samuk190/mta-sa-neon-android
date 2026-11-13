#include "DebugFlags.h"
#include "ResourceLoader.h"

#include "LuaVMStub.h"
#include "ResourceFiles.h"
#include "ResourcePackets.h"
#include "../net/HttpDownloader.h"
#include "../net/raknet/RakNetWrapper.h"

#include <zlib.h>

#include <cstdio>
#include <map>
#include <mutex>
#include <utility>

namespace WorldRPG::Script::ResourceLoader {

namespace {

constexpr uint32_t kMaxNoCacheSource = 4u * 1024u * 1024u;  // desktop accepts any size; a phone script above this is a bad packet
constexpr size_t kMaxOrphanNetIds = 64;
constexpr uint32_t kInvalidElement = 0xFFFFFFFFu;

std::mutex g_mutex;

enum class FileState : uint8_t { Pending, Ready, Failed };

struct TrackedFile {
    std::string name;
    FileState state = FileState::Pending;
    std::string note;
};

struct NoCacheItem {
    bool ok = false;
    std::vector<uint8_t> source;
};

struct Entry {
    std::string name;
    uint16_t netId = 0;
    uint32_t root = 0;
    uint32_t noCacheExpected = 0;
    std::vector<TrackedFile> files;             // every auto-download file of the resource
    std::vector<std::string> clientScriptFiles; // CLIENT_SCRIPT files in RESOURCE_START order
    std::vector<NoCacheItem> noCache;           // packet 95 items received so far
    std::string failure;                        // non-empty: the resource will not start
    bool started = false;
    uint32_t startCounter = 0;                  // PLAYER_RESOURCE_START value
    bool startCounterFromWire = false;          // false: stripped by the mobile shim, assumed
};

std::vector<Entry> g_entries;                               // RESOURCE_START order
std::map<uint16_t, std::vector<NoCacheItem>> g_orphans;     // packet 95 before its RESOURCE_START

// Desktop Packet_ResourceClientScripts: 4-byte big-endian original length, then zlib data.
bool InflateItem(const uint8_t* data, uint32_t length, std::vector<uint8_t>& out) {
    if (length <= 4) return false;
    const uint32_t original = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
                              (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
    if (original == 0 || original > kMaxNoCacheSource) return false;
    out.assign(original, 0);
    uLongf dest = original;
    if (uncompress(out.data(), &dest, data + 4, length - 4) != Z_OK) return false;
    out.resize(dest);
    return true;
}

void Fail(Entry& entry, const std::string& reason) {
    if (!entry.failure.empty()) return;
    entry.failure = reason;
    LUA_LOGE("LuaVM: resource %s will not start: %s", entry.name.c_str(), reason.c_str());
}

// Desktop LoadNoClientCacheScript: items beyond the declared count are ignored.
void AddNoCacheItems(Entry& entry, std::vector<NoCacheItem>&& items) {
    for (NoCacheItem& item : items) {
        if (entry.noCache.size() >= entry.noCacheExpected) break;
        if (!item.ok) Fail(entry, "no-cache script " + std::to_string(entry.noCache.size() + 1) + " did not inflate");
        entry.noCache.push_back(std::move(item));
    }
}

// Latest entry for a resource net id (a restarted resource replaces its older entry).
Entry* FindByNetId(uint16_t netId) {
    for (auto it = g_entries.rbegin(); it != g_entries.rend(); ++it) {
        if (it->netId == netId) return &*it;
    }
    return nullptr;
}

Entry* FindByName(const std::string& name) {
    for (auto it = g_entries.rbegin(); it != g_entries.rend(); ++it) {
        if (it->name == name && !it->started) return &*it;
    }
    return nullptr;
}

} // namespace

void Reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries.clear();
    g_orphans.clear();
}

void OnResourceStart(const WorldRPG::Net::ResourceStartInfo& info) {
    if (WorldRPG::Debug::Flag("noresources")) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry entry;
    entry.name = info.name;
    entry.netId = info.resourceId;
    entry.root = info.entityId == kInvalidElement ? 0 : info.entityId;
    entry.noCacheExpected = info.noClientCacheScripts;
    // The mobile-shim layout has no start counter on the wire (the shim strips it). The server's
    // first start of a resource is kFirstStartCounter, so that value is used; a restarted resource
    // will not match and the server then ignores the packet.
    entry.startCounterFromWire = info.layout == WorldRPG::Net::ResourceStartLayout::Desktop;
    entry.startCounter = entry.startCounterFromWire ? info.startCounter : ResourcePackets::kFirstStartCounter;

    for (const WorldRPG::Net::ResourceFile& file : info.files) {
        if (!file.autoDownload) continue;
        TrackedFile tracked;
        tracked.name = file.name;
        if (file.type == WorldRPG::Net::kResourceFileTypeClientScript) entry.clientScriptFiles.push_back(file.name);
        switch (WorldRPG::Net::HttpDownloader::Enqueue(info.name, file)) {
            case WorldRPG::Net::HttpDownloader::EnqueueResult::Queued:
            case WorldRPG::Net::HttpDownloader::EnqueueResult::Duplicate:
                break;  // a completion for this file will arrive
            case WorldRPG::Net::HttpDownloader::EnqueueResult::Disabled:
                tracked.state = FileState::Failed;
                tracked.note = "download service not available";
                break;
            case WorldRPG::Net::HttpDownloader::EnqueueResult::Stopped:
                tracked.state = FileState::Failed;
                tracked.note = "downloader stopped";
                break;
        }
        entry.files.push_back(std::move(tracked));
    }

    if (auto orphan = g_orphans.find(entry.netId); orphan != g_orphans.end()) {
        AddNoCacheItems(entry, std::move(orphan->second));
        g_orphans.erase(orphan);
    }
    for (const TrackedFile& file : entry.files) {
        if (file.state == FileState::Failed) {
            Fail(entry, file.name + ": " + file.note);
            break;
        }
    }

    // A restarted resource replaces its earlier entry.
    std::erase_if(g_entries, [&](const Entry& old) { return old.name == entry.name && !old.started; });
    LUA_LOGI("LuaVM: resource %s announced: %zu download file(s), %u no-cache script(s)", entry.name.c_str(),
             entry.files.size(), static_cast<unsigned>(entry.noCacheExpected));
    g_entries.push_back(std::move(entry));
}

bool OnClientScripts(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint16_t netId = 0, count = 0;
    if (!reader.Read(netId) || !reader.Read(count)) return false;

    std::vector<NoCacheItem> items;
    bool wellFormed = true;
    for (uint16_t i = 0; i < count; ++i) {
        uint32_t length32 = 0;
        if (!reader.Read(length32) || length32 < 4 || length32 > reader.GetRemainingBits() / 8) {
            wellFormed = false;
            break;
        }
        std::vector<uint8_t> blob(length32);
        if (!reader.ReadBytes(blob.data(), length32)) {
            wellFormed = false;
            break;
        }
        NoCacheItem item;
        item.ok = InflateItem(blob.data(), length32, item.source);
        items.push_back(std::move(item));
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (Entry* entry = FindByNetId(netId)) {
        if (entry->started) {
            LUA_LOGI("LuaVM: RESOURCE_CLIENT_SCRIPTS for %s arrived after it started: ignored", entry->name.c_str());
        } else {
            AddNoCacheItems(*entry, std::move(items));
        }
    } else if (!items.empty()) {
        if (g_orphans.size() < kMaxOrphanNetIds || g_orphans.count(netId) != 0) {
            auto& held = g_orphans[netId];
            for (NoCacheItem& item : items) held.push_back(std::move(item));
            LUA_LOGI("LuaVM: RESOURCE_CLIENT_SCRIPTS for net id %u arrived before its RESOURCE_START: %zu item(s) held",
                     static_cast<unsigned>(netId), held.size());
        }
    }
    return wellFormed;
}

std::vector<ResourceLoadJob> Pump() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (WorldRPG::Net::HttpDownloader::Completion& done : WorldRPG::Net::HttpDownloader::TakeCompletions()) {
        Entry* entry = FindByName(done.resource);
        if (!entry) continue;
        for (TrackedFile& file : entry->files) {
            if (file.name != done.file || file.state != FileState::Pending) continue;
            file.state = done.ok ? FileState::Ready : FileState::Failed;
            file.note = done.note;
            if (!done.ok) Fail(*entry, file.name + ": " + file.note);
            break;
        }
    }

    std::vector<ResourceLoadJob> jobs;
    for (Entry& entry : g_entries) {
        if (entry.started) continue;
        if (!entry.failure.empty()) continue;
        // Desktop OnDownloadGroupFinished: a resource still downloading stops the loop.
        bool waiting = false;
        for (const TrackedFile& file : entry.files) waiting = waiting || file.state == FileState::Pending;
        if (waiting) break;
        if (entry.noCache.size() < entry.noCacheExpected) continue;

        ResourceLoadJob job;
        job.name = entry.name;
        job.netId = entry.netId;
        job.rootElement = entry.root;
        job.startCounter = entry.startCounter;
        job.startCounterFromWire = entry.startCounterFromWire;
        unsigned index = 0;
        for (NoCacheItem& item : entry.noCache) {
            ResourceScript script;
            script.chunkName = entry.name + "/no-cache-" + std::to_string(++index);
            script.source = std::move(item.source);
            job.noCacheScripts.push_back(std::move(script));
        }
        for (const std::string& file : entry.clientScriptFiles) {
            ResourceScript script;
            script.chunkName = entry.name + "/" + file;
            script.cachePath = WorldRPG::Net::HttpDownloader::CachedFilePath(entry.name, file);
            job.clientScripts.push_back(std::move(script));
        }
        entry.started = true;
        jobs.push_back(std::move(job));
    }
    return jobs;
}

ForgottenResource Forget(uint16_t netId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ForgottenResource out;
    if (const Entry* entry = FindByNetId(netId)) {
        out.found = true;
        out.started = entry->started;
        out.name = entry->name;
        out.rootElement = entry->root;
    }
    std::erase_if(g_entries, [&](const Entry& entry) { return entry.netId == netId; });
    g_orphans.erase(netId);
    return out;
}

uint32_t RootElementOf(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_entries.rbegin(); it != g_entries.rend(); ++it) {
        if (it->name == name) return it->root;
    }
    return 0;
}

} // namespace WorldRPG::Script::ResourceLoader
