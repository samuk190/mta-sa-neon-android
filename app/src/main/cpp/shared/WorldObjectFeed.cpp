#include "WorldObjectFeed.h"

#include <mutex>

namespace WorldRPG::WorldObjects {
namespace {
std::mutex g_mutex;
std::vector<State> g_sources[2];
uint64_t g_revision = 1;
}  // namespace

void Publish(Source source, std::vector<State> states) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto& slot = g_sources[static_cast<int>(source)];
    if (slot.size() == states.size()) {
        bool same = true;
        for (size_t i = 0; same && i < states.size(); ++i) {
            const State& a = slot[i];
            const State& b = states[i];
            same = a.id == b.id && a.model == b.model && a.x == b.x && a.y == b.y && a.z == b.z && a.rx == b.rx &&
                   a.ry == b.ry && a.rz == b.rz && a.dimension == b.dimension && a.interior == b.interior &&
                   a.collisions == b.collisions && a.visible == b.visible && a.frozen == b.frozen &&
                   a.allDimensions == b.allDimensions && a.alpha == b.alpha;
        }
        if (same) return;
    }
    slot = std::move(states);
    ++g_revision;
}

uint64_t Revision() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_revision;
}

void Copy(std::vector<State>& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    out.clear();
    for (const auto& source : g_sources) out.insert(out.end(), source.begin(), source.end());
}

void Reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& source : g_sources) source.clear();
    ++g_revision;
}

}  // namespace WorldRPG::WorldObjects
