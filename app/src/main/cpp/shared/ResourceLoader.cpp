#include "DebugFlags.h"
#include "ResourceLoader.h"

#include "LuaVMStub.h"
#include "ResourceFiles.h"
#include "ResourcePackets.h"
#include "../net/HttpDownloader.h"
#include "../net/RemoteCalls.h"
#include "LuaCryptApi.h"
#include "../net/raknet/RakNetWrapper.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <utility>

namespace WorldRPG::Script::ResourceLoader {

namespace {

constexpr uint32_t kMaxNoCacheSource = 4u * 1024u * 1024u;  // desktop accepts any size; a phone script above this is a bad packet
constexpr size_t kMaxOrphanNetIds = 64;
constexpr uint32_t kInvalidElement = 0xFFFFFFFFu;

std::mutex g_mutex;

enum class FileState : uint8_t { Idle, Pending, Ready, Failed };

struct TrackedFile {
    WorldRPG::Net::ResourceFile description;
    FileState state = FileState::Idle;
    std::string note;
    std::vector<uint32_t> requesters;
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
    std::vector<TrackedFile> files;             // manifest files, including downloads on demand
    std::vector<std::string> clientScriptFiles; // CLIENT_SCRIPT files in RESOURCE_START order
    std::vector<NoCacheItem> noCache;           // packet 95 items received so far
    std::string failure;                        // non-empty: the resource will not start
    bool started = false;
    bool stopping = false;
    uint32_t dynamic = 0;
    std::vector<std::string> exports;
    uint32_t startCounter = 0;                  // PLAYER_RESOURCE_START value
    bool startCounterFromWire = false;          // false: stripped by the mobile shim, assumed
    uint32_t generation = 0;                    // unique for every RESOURCE_START, even when the net id is reused
};

std::string StateOf(const Entry& entry) {
    if (entry.stopping) return "stopping";
    if (entry.started) return "running";
    if (!entry.failure.empty()) return "loaded";
    return "starting";
}

ResourceView ViewOf(const Entry& entry) {
    ResourceView view;
    view.name = entry.name;
    view.netId = entry.netId;
    view.startCounter = entry.startCounter;
    view.generation = entry.generation;
    view.root = entry.root;
    view.dynamic = entry.dynamic;
    view.state = StateOf(entry);
    view.exports = entry.exports;
    return view;
}

uint32_t g_nextGeneration = 1;
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
        if (it->name == name) return &*it;
    }
    return nullptr;
}

Entry* FindByGeneration(uint32_t generation) {
    for (auto& entry : g_entries)
        if (entry.generation == generation) return &entry;
    return nullptr;
}

TrackedFile* FindFile(Entry& entry, const std::string& name) {
    for (auto& file : entry.files)
        if (file.description.name == name) return &file;
    return nullptr;
}

bool QueueFile(const Entry& entry, TrackedFile& file) {
    using WorldRPG::Net::HttpDownloader::EnqueueResult;
    switch (WorldRPG::Net::HttpDownloader::Enqueue(entry.name, file.description, entry.generation)) {
        case EnqueueResult::Queued:
        case EnqueueResult::Duplicate:
            file.state = FileState::Pending;
            file.note.clear();
            return true;
        case EnqueueResult::Disabled:
            file.note = "download service not available";
            break;
        case EnqueueResult::Stopped:
            file.note = "downloader stopped";
            break;
    }
    file.state = FileState::Failed;
    return false;
}

} // namespace

void Reset() {
    // Drop fetchRemote callbacks while this VM is still alive. ActiveVmResetSession
    // calls Reset before it shuts the state down.
    WorldRPG::Net::RemoteCalls::AbandonAll();
    WorldRPG::Script::CryptAbandonAll();
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
    entry.dynamic = info.dynamicEntityId == kInvalidElement ? 0 : info.dynamicEntityId;
    entry.exports = info.exportedFunctions;
    entry.noCacheExpected = info.noClientCacheScripts;
    // The mobile-shim layout has no start counter on the wire (the shim strips it). The server's
    // first start of a resource is kFirstStartCounter, so that value is used; a restarted resource
    // will not match and the server then ignores the packet.
    entry.startCounterFromWire = info.layout == WorldRPG::Net::ResourceStartLayout::Desktop;
    entry.startCounter = entry.startCounterFromWire ? info.startCounter : ResourcePackets::kFirstStartCounter;
    entry.generation = g_nextGeneration++;

    size_t automatic = 0;
    for (const WorldRPG::Net::ResourceFile& file : info.files) {
        TrackedFile tracked;
        tracked.description = file;
        if (file.autoDownload) {
            ++automatic;
            if (file.type == WorldRPG::Net::kResourceFileTypeClientScript) entry.clientScriptFiles.push_back(file.name);
            if (!QueueFile(entry, tracked)) Fail(entry, file.name + ": " + tracked.note);
        }
        entry.files.push_back(std::move(tracked));
    }

    if (auto orphan = g_orphans.find(entry.netId); orphan != g_orphans.end()) {
        AddNoCacheItems(entry, std::move(orphan->second));
        g_orphans.erase(orphan);
    }
    // A restarted resource replaces its earlier entry.
    std::erase_if(g_entries, [&](const Entry& old) { return old.name == entry.name && !old.started; });
    LUA_LOGI("LuaVM: resource %s announced: %zu download file(s), %u no-cache script(s)", entry.name.c_str(),
             automatic, static_cast<unsigned>(entry.noCacheExpected));
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

bool RequestFile(std::string_view path, uint32_t callerGeneration) {
    if (path.empty() || path.find('\0') != std::string_view::npos) return false;
    std::string relative(path);
    std::replace(relative.begin(), relative.end(), '\\', '/');
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry* caller = FindByGeneration(callerGeneration);
    if (!caller || !caller->started || caller->stopping) return false;
    Entry* entry = caller;
    if (relative.front() == ':') {
        const size_t slash = relative.find('/');
        if (slash == std::string::npos || slash < 2) return false;
        entry = FindByName(relative.substr(1, slash - 1));
        relative.erase(0, slash + 1);
    }
    if (!entry || entry->stopping || !entry->failure.empty() || entry != FindByName(entry->name)) return false;
    const std::filesystem::path filePath(relative);
    if (filePath.empty() || filePath.is_absolute()) return false;
    for (const auto& part : filePath)
        if (part == "..") return false;
    TrackedFile* file = FindFile(*entry, filePath.lexically_normal().generic_string());
    if (!file) return false;
    if (file->state != FileState::Pending && !QueueFile(*entry, *file)) return false;
    if (std::find(file->requesters.begin(), file->requesters.end(), callerGeneration) == file->requesters.end())
        file->requesters.push_back(callerGeneration);
    return true;
}

bool IsFileReady(const std::string& resource, const std::string& file, uint32_t generation) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry* entry = FindByName(resource);
    if (!entry || (generation != 0 && entry->generation != generation) || !entry->failure.empty()) return false;
    const TrackedFile* tracked = FindFile(*entry, file);
    return tracked && tracked->state == FileState::Ready;
}

std::vector<ResourceLoadJob> Pump(std::vector<FileDownloadEvent>& events) {
    // Same VM thread that drains resource downloads. Callbacks run here, and
    // not under g_mutex, so a callback cannot deadlock on the resource loader.
    WorldRPG::Net::RemoteCalls::DeliverCompletions();
    // encodeString / passwordHash / generateKeyPair callbacks (LuaCryptApi.cpp).
    WorldRPG::Script::CryptDeliverCompletions();
    std::lock_guard<std::mutex> lock(g_mutex);
    for (WorldRPG::Net::HttpDownloader::Completion& done : WorldRPG::Net::HttpDownloader::TakeCompletions()) {
        Entry* entry = FindByGeneration(done.generation);
        if (!entry || entry->name != done.resource || entry->stopping || entry != FindByName(done.resource)) continue;
        TrackedFile* file = FindFile(*entry, done.file);
        if (!file || file->state != FileState::Pending) continue;
        file->state = done.ok ? FileState::Ready : FileState::Failed;
        file->note = std::move(done.note);
        if (!done.ok && !entry->started && file->description.autoDownload) Fail(*entry, done.file + ": " + file->note);
        for (uint32_t requester : file->requesters)
            events.push_back({done.file, entry->generation, requester, done.ok});
        file->requesters.clear();
    }

    std::vector<ResourceLoadJob> jobs;
    for (Entry& entry : g_entries) {
        if (entry.started) continue;
        if (!entry.failure.empty()) continue;
        // Desktop OnDownloadGroupFinished: a resource still downloading stops the loop.
        bool waiting = false;
        for (const TrackedFile& file : entry.files)
            waiting = waiting || (file.description.autoDownload && file.state == FileState::Pending);
        if (waiting) break;
        if (entry.noCache.size() < entry.noCacheExpected) continue;

        ResourceLoadJob job;
        job.name = entry.name;
        job.netId = entry.netId;
        job.rootElement = entry.root;
        job.startCounter = entry.startCounter;
        job.startCounterFromWire = entry.startCounterFromWire;
        job.generation = entry.generation;
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
    // The record stays until FinishStop so getResourceState is "stopping" during
    // onClientResourceStop (CResource::GetState, CResource.cpp 1688-1697).
    // The oldest started entry is the one the stop packet means. A newer start
    // that reused the net id is left alone.
    std::lock_guard<std::mutex> lock(g_mutex);
    ForgottenResource out;
    Entry* target = nullptr;
    for (Entry& entry : g_entries) {
        if (entry.netId != netId || entry.stopping || !entry.failure.empty()) continue;
        if (!target) target = &entry;
        if (entry.started) {
            target = &entry;
            break;
        }
    }
    if (!target) return out;
    out.found = true;
    out.started = target->started;
    out.name = target->name;
    out.rootElement = target->root;
    out.dynamicElement = target->dynamic;
    out.generation = target->generation;
    target->stopping = true;
    return out;
}

void FinishStop(uint16_t netId, uint32_t generation) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::erase_if(g_entries, [&](const Entry& entry) {
        if (generation != 0) return entry.generation == generation;
        return entry.netId == netId && entry.stopping;
    });
    if (generation == 0) g_orphans.erase(netId);
}

uint32_t RootElementOf(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_entries.rbegin(); it != g_entries.rend(); ++it) {
        if (it->name == name && it->failure.empty()) return it->root;
    }
    return 0;
}

bool Listed(const Entry& entry) { return entry.failure.empty(); }

std::vector<ResourceView> CopyResources() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<ResourceView> out;
    out.reserve(g_entries.size());
    for (const Entry& entry : g_entries)
        if (Listed(entry)) out.push_back(ViewOf(entry));
    return out;
}

bool CopyResource(const std::string& name, ResourceView& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_entries.rbegin(); it != g_entries.rend(); ++it) {
        if (it->name != name || !Listed(*it)) continue;
        out = ViewOf(*it);
        return true;
    }
    return false;
}

bool CopyResourceByGeneration(uint32_t generation, ResourceView& out) {
    if (generation == 0) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const Entry& entry : g_entries) {
        if (entry.generation != generation || !Listed(entry)) continue;
        out = ViewOf(entry);
        return true;
    }
    return false;
}

bool CopyResourceByNet(uint16_t netId, ResourceView& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const Entry* found = nullptr;
    for (const Entry& entry : g_entries) {
        if (entry.netId != netId || !Listed(entry)) continue;
        if (!found) found = &entry;
        if (entry.started && !entry.stopping) {
            found = &entry;
            break;
        }
    }
    if (!found) return false;
    out = ViewOf(*found);
    return true;
}

bool CopyCallerResource(uint16_t netId, uint32_t generation, const std::string& name, ResourceView& out) {
    if (generation != 0 && CopyResourceByGeneration(generation, out)) return true;
    if (netId != 0 && CopyResourceByNet(netId, out)) return true;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const Entry& entry : g_entries) {
        if (entry.name != name || !entry.started || entry.stopping || !Listed(entry)) continue;
        out = ViewOf(entry);
        return true;
    }
    return false;
}

} // namespace WorldRPG::Script::ResourceLoader
