#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace WorldRPG {
using VehicleRgb = std::array<uint8_t, 3>;
struct VehicleColors {
    std::array<VehicleRgb, 4> rgb{};
    std::optional<std::array<uint8_t, 4>> palette;
};

// ENTITY_ADD and SET_VEHICLE_COLOR both encode one to four RGB triplets.
template <typename Reader>
bool ReadVehicleColors(Reader& reader, VehicleColors& result) {
    uint8_t extra = 0;
    if (!reader.ReadBits(&extra, 2)) return false;
    VehicleColors colors;
    for (unsigned slot = 0; slot <= extra; ++slot) {
        for (auto& component : colors.rgb[slot]) {
            if (!reader.Read(component)) return false;
        }
    }
    result = colors;
    return true;
}
} // namespace WorldRPG
