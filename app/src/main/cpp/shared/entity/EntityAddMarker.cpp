// MARKER (type 4) body. Port of the reference case CClientGame::MARKER in Packet_EntityAdd
// (CPacketHandler.cpp ~lines 3775-3846).

#include "EntityAddTypes.h"

#include <cstring>

namespace WorldRPG::Entity {

namespace {
constexpr uint8_t kMarkerCheckpoint = 0;  // CClientGame MARKER_CHECKPOINT
constexpr uint8_t kMarkerRing = 1;        // CClientGame MARKER_RING
} // namespace

bool ReadEntityBodyMarker(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    // SPositionSync(false): 80 bits.
    float x = 0, y = 0, z = 0;
    if (!ReadPosition(reader, x, y, z)) ENTITY_BODY_FAIL();
    visual.x = x;
    visual.y = y;
    visual.z = z;

    // SMarkerTypeSync: 3 bits. Every 3-bit value is below MARKER_INVALID (0xFF), so no range check.
    uint8_t markerType = 0;
    if (!reader.ReadBits(&markerType, 3)) ENTITY_BODY_FAIL();
    visual.markerType = markerType & 7;

    // Float size, then SColorSync: 4 bytes R, G, B, A (32 bits).
    // DIAG: read the raw 32 bits first so the device log shows the exact bytes and bit offset.
    uint8_t rawSize[4] = {};
    const size_t sizeBit = reader.GetReadOffsetBits();
    if (!reader.ReadBits(rawSize, 32)) ENTITY_BODY_FAIL();
    std::memcpy(&visual.size, rawSize, 4);
    DIAG("ENTITY_ADD marker raw size id=%u bit=%zu bytes=%02x%02x%02x%02x pos=%.2f,%.2f,%.2f mtype_bits_ok", common.id, sizeBit,
         rawSize[0], rawSize[1], rawSize[2], rawSize[3], static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
    uint8_t color[4] = {};
    if (!reader.ReadBits(color, 32)) ENTITY_BODY_FAIL();
    visual.r = color[0];
    visual.g = color[1];
    visual.b = color[2];
    visual.a = color[3];

    // Checkpoints and rings may carry a target (next position, and arrow colour/size for checkpoints).
    const uint8_t type = static_cast<uint8_t>(visual.markerType);
    if (type == kMarkerCheckpoint || type == kMarkerRing) {
        bool hasTarget = false;
        if (!ReadFlag(reader, hasTarget)) ENTITY_BODY_FAIL();
        if (hasTarget) {
            float tx = 0, ty = 0, tz = 0;
            if (!ReadPosition(reader, tx, ty, tz)) ENTITY_BODY_FAIL();
            if (type == kMarkerCheckpoint) {
                uint8_t arrow[4] = {};
                float arrowSize = 0;
                if (!reader.ReadBits(arrow, 32) || !reader.Read(arrowSize)) ENTITY_BODY_FAIL();
            }
        }
    }

    // Alpha-limit override flag.
    bool ignoreAlphaLimits = false;
    if (!ReadFlag(reader, ignoreAlphaLimits)) ENTITY_BODY_FAIL();
    DIAG("ENTITY_ADD marker id=%u mtype=%u size=%.2f bit=%zu", common.id, static_cast<unsigned>(type), static_cast<double>(visual.size), reader.GetReadOffsetBits());
    return true;
}

} // namespace WorldRPG::Entity
