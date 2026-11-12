// DUMMY (type 0) body. Port of the reference case CClientGame::DUMMY in Packet_EntityAdd
// (CPacketHandler.cpp ~line 4189): type name, then optional position.

#include "EntityAddTypes.h"

namespace WorldRPG::Entity {

constexpr uint32_t kMaxDummyTypeNameLength = 4096;

bool ReadEntityBodyDummy(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    // Type name: ReadCompressed ushort length, then the characters.
    std::string typeName;
    if (!ReadSizedName(reader, typeName, kMaxDummyTypeNameLength)) ENTITY_BODY_FAIL();

    // Optional position (SPositionSync(false), 80 bits). The reference ignores its read result.
    bool hasPosition = false;
    if (!ReadFlag(reader, hasPosition)) ENTITY_BODY_FAIL();
    if (hasPosition) {
        float x = 0, y = 0, z = 0;
        if (!ReadPosition(reader, x, y, z)) ENTITY_BODY_FAIL();
        visual.x = x;
        visual.y = y;
        visual.z = z;
    }
    DIAG("ENTITY_ADD dummy id=%u type=%s hasPos=%d bit=%zu", common.id, typeName.c_str(), hasPosition ? 1 : 0, reader.GetReadOffsetBits());
    return true;
}

} // namespace WorldRPG::Entity
