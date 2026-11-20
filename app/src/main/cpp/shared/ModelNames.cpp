#include "ModelNames.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace WorldRPG::Models {
namespace {

struct ModelNameRow {
    std::uint16_t id;
    const char* name;
};

#include "ModelNamesTable.inc"

std::unordered_map<std::uint16_t, const char*> g_idToName;
std::unordered_map<std::string, std::uint32_t> g_nameToId;

// SharedUtil::tolower (SharedUtil.Misc.h:1544-1546): A-Z only, other bytes stay.
std::string Lower(std::string_view text) {
    std::string out;
    out.resize(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        out[i] = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    return out;
}

void EnsureMaps() {
    // CModelNames::InitializeMaps (CModelNames.cpp:29362-29382). IDE rows first,
    // then vehicle display names for 400..610. A later row replaces an earlier
    // id or lowercase name. Model 611 is not in that vehicle loop.
    static std::once_flag once;
    std::call_once(once, [] {
        g_idToName.reserve(sizeof(kIdeModelNames) / sizeof(kIdeModelNames[0]) + 211);
        g_nameToId.reserve(sizeof(kIdeModelNames) / sizeof(kIdeModelNames[0]) + 211);
        for (const ModelNameRow& row : kIdeModelNames) {
            g_idToName[row.id] = row.name;
            g_nameToId[Lower(row.name)] = row.id;
        }
        static_assert(sizeof(kVehicleDisplayNames) / sizeof(kVehicleDisplayNames[0]) == 211,
                      "vehicle display names are models 400..610");
        for (std::uint32_t i = 0; i < 211; ++i) {
            const std::uint32_t model = 400u + i;
            g_idToName[static_cast<std::uint16_t>(model)] = kVehicleDisplayNames[i];
            g_nameToId[Lower(kVehicleDisplayNames[i])] = model;
        }
    });
}

}  // namespace

std::uint32_t ModelIdFromName(std::string_view name) {
    EnsureMaps();
    const auto found = g_nameToId.find(Lower(name));
    if (found == g_nameToId.end()) return kInvalidModelId;
    return found->second;
}

const char* ModelNameFromId(std::uint32_t id) {
    EnsureMaps();
    if (id > 0xFFFFu) return "";
    const auto found = g_idToName.find(static_cast<std::uint16_t>(id));
    if (found == g_idToName.end()) return "";
    return found->second;
}

}  // namespace WorldRPG::Models
