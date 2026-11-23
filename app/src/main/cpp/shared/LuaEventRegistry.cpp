#include "LuaEventRegistry.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace WorldRPG::Script::Events {
namespace {

constexpr std::string_view kBuiltIn[] = {
#include "LuaEventNames.inc"
};

struct Owner {
    std::string name;
    uint16_t netId = 0;
    uint32_t generation = 0;
    bool operator==(const Owner& other) const {
        return name == other.name && netId == other.netId && generation == other.generation;
    }
};

struct Event {
    bool allowRemoteTrigger = false;
    std::vector<Owner> owners;
};

// VM thread only, like the handler table in LuaVMStub.cpp.
std::unordered_map<std::string, Event> g_added;

const std::unordered_set<std::string_view>& BuiltIn() {
    static const std::unordered_set<std::string_view> names(std::begin(kBuiltIn), std::end(kBuiltIn));
    return names;
}

}  // namespace

bool Exists(const std::string& name) {
    return BuiltIn().count(name) != 0 || g_added.count(name) != 0;
}

bool Add(const std::string& name, bool allowRemoteTrigger, const std::string& owner, uint16_t netId,
         uint32_t generation) {
    // A built-in event never allows a remote trigger, and it stays when the resource stops.
    if (BuiltIn().count(name) != 0) return !allowRemoteTrigger;
    const Owner added{owner, netId, generation};
    auto found = g_added.find(name);
    if (found == g_added.end()) {
        g_added.emplace(name, Event{allowRemoteTrigger, {added}});
        return true;
    }
    if (found->second.allowRemoteTrigger != allowRemoteTrigger) return false;
    auto& owners = found->second.owners;
    if (std::find(owners.begin(), owners.end(), added) == owners.end()) owners.push_back(added);
    return true;
}

Remote RemoteState(const std::string& name) {
    if (BuiltIn().count(name) != 0) return Remote::NotRemote;
    const auto found = g_added.find(name);
    if (found == g_added.end()) return Remote::NotAdded;
    return found->second.allowRemoteTrigger ? Remote::Allowed : Remote::NotRemote;
}

void DropOwner(const std::string& owner, uint16_t netId, uint32_t generation) {
    for (auto it = g_added.begin(); it != g_added.end();) {
        auto& owners = it->second.owners;
        owners.erase(std::remove_if(owners.begin(), owners.end(), [&](const Owner& held) {
            if (held.name != owner) return false;
            if (netId != 0 && held.netId != 0 && held.netId != netId) return false;
            if (generation != 0 && held.generation != 0 && held.generation != generation) return false;
            return true;
        }), owners.end());
        it = owners.empty() ? g_added.erase(it) : std::next(it);
    }
}

void Reset() { g_added.clear(); }

}  // namespace WorldRPG::Script::Events
