#pragma once

// Resource start gating for the phone. Desktop equivalents: CResource::Load
// (CResource.cpp 1526-1630), CResource::LoadNoClientCacheScript (1789-1807),
// CResourceManager::OnDownloadGroupFinished (CResourceManager.cpp 95-130) and
// CPacketHandler::Packet_ResourceClientScripts (CPacketHandler.cpp 5710-5760).
//
// A resource starts when every auto-download file is cached or downloaded and
// all of its RESOURCE_CLIENT_SCRIPTS (packet 95) items have arrived. Resources
// start in RESOURCE_START order and a resource still downloading blocks the
// ones after it, as on desktop. Pump() returns resource jobs and the caller
// runs those on the VM thread. The same Pump() delivers fetchRemote completions:
// it is the game pulse that already drains HttpDownloader::TakeCompletions.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace WorldRPG::Net {
struct ResourceStartInfo;
}

namespace WorldRPG::Script {

// One script of a resource that is ready to run.
struct ResourceScript {
    std::string chunkName;          // "<resource>/<file>" or "<resource>/no-cache-<n>"
    std::string cachePath;          // CLIENT_SCRIPT: local cache file; empty for no-cache scripts
    std::vector<uint8_t> source;    // no-cache scripts: decompressed source
};

struct ResourceLoadJob {
    std::string name;
    uint16_t netId = 0;
    uint32_t rootElement = 0;       // RESOURCE_START entity id (resource root)
    uint32_t startCounter = 0;      // echoed in PLAYER_RESOURCE_START
    bool startCounterFromWire = false;  // false: the mobile shim stripped it, startCounter is assumed
    uint32_t generation = 0;            // unique for this RESOURCE_START
    std::vector<ResourceScript> noCacheScripts;  // packet 95 order
    std::vector<ResourceScript> clientScripts;   // RESOURCE_START file list order
};

// What RESOURCE_STOP removed from the loader.
struct ForgottenResource {
    bool found = false;
    bool started = false;           // scripts ran, so the resource may have onClientResourceStop handlers
    std::string name;
    uint32_t rootElement = 0;
    uint32_t dynamicElement = 0;
    uint32_t generation = 0;
};

namespace ResourceLoader {

// Forgets every resource and pending script (new session).
void Reset();

// RESOURCE_START: records the resource and queues its auto-download files.
void OnResourceStart(const WorldRPG::Net::ResourceStartInfo& info);

// RESOURCE_CLIENT_SCRIPTS (packet 95) body after the game id. Returns false when
// the body is malformed. Items for a resource whose RESOURCE_START has not
// arrived yet are kept until it does.
bool OnClientScripts(const uint8_t* body, size_t length, size_t bits);

struct FileDownloadEvent {
    std::string file;
    uint32_t generation = 0;
    uint32_t requesterGeneration = 0;
    bool ok = false;
};

// Queues a manifest file from this resource or a :resource/path reference.
bool RequestFile(std::string_view path, uint32_t callerGeneration);
bool IsFileReady(const std::string& resource, const std::string& file, uint32_t generation);

// Applies finished downloads and returns the resources that can start now.
std::vector<ResourceLoadJob> Pump(std::vector<FileDownloadEvent>& events);

// RESOURCE_STOP: removes the resource with this net id (desktop CResourceManager::RemoveResource).
ForgottenResource Forget(uint16_t netId);

// Root element of the newest resource with this name, 0 when unknown ("^R^name" values).
uint32_t RootElementOf(const std::string& name);

// One resource the loader still has. state is the desktop CResource::GetState string
// (CResource.cpp 1688-1697): starting, running, stopping, or loaded.
struct ResourceView {
    std::string name;
    uint16_t netId = 0;
    uint32_t startCounter = 0;
    uint32_t generation = 0;
    uint32_t root = 0;
    uint32_t dynamic = 0;
    std::string state;
    std::vector<std::string> exports;
};

// RESOURCE_START order. CopyResource returns the newest entry for a name.
// CopyCallerResource returns the instance whose scripts are running: netId when
// the caller recorded one, otherwise the started entry for that name.
std::vector<ResourceView> CopyResources();
bool CopyResource(const std::string& name, ResourceView& out);
bool CopyResourceByNet(uint16_t netId, ResourceView& out);
// generation, when non-zero, is the instance whose scripts are running. A reused
// net id then still resolves to that start, not the newer one.
bool CopyCallerResource(uint16_t netId, uint32_t generation, const std::string& name, ResourceView& out);
bool CopyResourceByGeneration(uint32_t generation, ResourceView& out);

// RESOURCE_STOP marks the running start with this net id. FinishStop drops that
// generation only, so a newer start that reused the id stays.
void FinishStop(uint16_t netId, uint32_t generation);

} // namespace ResourceLoader
} // namespace WorldRPG::Script
