#pragma once

// Resource start gating for the phone. Desktop equivalents: CResource::Load
// (CResource.cpp 1526-1630), CResource::LoadNoClientCacheScript (1789-1807),
// CResourceManager::OnDownloadGroupFinished (CResourceManager.cpp 95-130) and
// CPacketHandler::Packet_ResourceClientScripts (CPacketHandler.cpp 5710-5760).
//
// A resource starts when every auto-download file is cached or downloaded and
// all of its RESOURCE_CLIENT_SCRIPTS (packet 95) items have arrived. Resources
// start in RESOURCE_START order and a resource still downloading blocks the
// ones after it, as on desktop. Lua never runs here: Pump() returns jobs and the
// caller runs them on the VM thread.

#include <cstddef>
#include <cstdint>
#include <string>
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
    std::vector<ResourceScript> noCacheScripts;  // packet 95 order
    std::vector<ResourceScript> clientScripts;   // RESOURCE_START file list order
};

// What RESOURCE_STOP removed from the loader.
struct ForgottenResource {
    bool found = false;
    bool started = false;           // scripts ran, so the resource may have onClientResourceStop handlers
    std::string name;
    uint32_t rootElement = 0;
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

// Applies finished downloads and returns the resources that can start now.
std::vector<ResourceLoadJob> Pump();

// RESOURCE_STOP: removes the resource with this net id (desktop CResourceManager::RemoveResource).
ForgottenResource Forget(uint16_t netId);

// Root element of the newest resource with this name, 0 when unknown ("^R^name" values).
uint32_t RootElementOf(const std::string& name);

} // namespace ResourceLoader
} // namespace WorldRPG::Script
