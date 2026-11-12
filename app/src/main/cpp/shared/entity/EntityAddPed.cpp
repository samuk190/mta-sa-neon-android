// PED (type 14) body of ENTITY_ADD. Port of the reference case CClientGame::PED in
// Packet_EntityAdd (mta_neon_source Client/mods/deathmatch/logic/CPacketHandler.cpp,
// case CClientGame::PED at line 3997 through the break at line 4179).
// Read order: position, model, rotation, health, armor, vehicle, flags, alpha,
// move anim, clothes, weapons, current weapon slot, animation.
// Widths are for the 0x0030 peer bitstream (NeonJoinProtocol pins it).
// The model id is stored raw; ResolveModelID / IsValidModel need the game and are not run here.

#include "EntityAddTypes.h"
#include "PedWire.h"

namespace WorldRPG::Entity {

bool ReadEntityBodyPed(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    // Position: SPositionSync(false). Two fixed24 components (SPositionComponentSync at 0x0030), then a float Z.
    float x = 0, y = 0, z = 0;
    if (!ReadPosition(reader, x, y, z)) ENTITY_BODY_FAIL();

    // Model: bitStream.ReadCompressed(ushort).
    uint32_t model = 0;
    if (!ReadCompressed(reader, 2, model)) ENTITY_BODY_FAIL();

    // Rotation: SPedRotationSync = SFloatAsBitsSync<16>(-pi, pi).
    float heading = 0;
    if (!PedWire::ReadAngle16(reader, heading)) ENTITY_BODY_FAIL();

    // Health: SPlayerHealthSync (8 bits, 0..255). Armor: SPlayerArmorSync (8 bits, 0..127.5).
    float health = 0, armor = 0;
    if (!PedWire::ReadFloatBits8(reader, 0.f, 255.f, health)) ENTITY_BODY_FAIL();
    if (!PedWire::ReadFloatBits8(reader, 0.f, 127.5f, armor)) ENTITY_BODY_FAIL();

    // Vehicle: one flag bit. When set, ElementID (17 bits) then SOccupiedSeatSync (4 bits).
    bool inVehicle = false;
    uint32_t vehicleId = 0xFFFFFFFFu;
    uint8_t seat = 0xFF;
    if (!ReadFlag(reader, inVehicle)) ENTITY_BODY_FAIL();
    if (inVehicle) {
        if (!ReadElementId(reader, vehicleId)) ENTITY_BODY_FAIL();
        uint8_t seatBits = 0;
        if (!reader.ReadBits(&seatBits, 4)) ENTITY_BODY_FAIL();
        seat = static_cast<uint8_t>(seatBits & 0x0Fu);
    }

    // Flags: jetpack, synced, headless, frozen (one bit each, reference order).
    bool hasJetpack = false, synced = false, headless = false, frozen = false;
    if (!ReadFlag(reader, hasJetpack) || !ReadFlag(reader, synced) || !ReadFlag(reader, headless) || !ReadFlag(reader, frozen))
        ENTITY_BODY_FAIL();

    // Alpha: SEntityAlphaSync = ReadCompressed(uchar), stored as 255 - raw.
    uint32_t alphaRaw = 0;
    if (!ReadCompressed(reader, 1, alphaRaw)) ENTITY_BODY_FAIL();
    const uint8_t alpha = static_cast<uint8_t>(255u - alphaRaw);

    // Move anim: raw uchar.
    uint8_t moveAnim = 0;
    if (!reader.Read(moveAnim)) ENTITY_BODY_FAIL();

    // Clothes: raw uchar count. Per item: raw uchar texture length, texture bytes,
    // raw uchar model length, model bytes, raw uchar clothes type.
    uint8_t clothesCount = 0;
    if (!reader.Read(clothesCount)) ENTITY_BODY_FAIL();
    for (uint8_t i = 0; i < clothesCount; ++i) {
        uint8_t textureLength = 0, modelLength = 0, clothesType = 0;
        if (!reader.Read(textureLength) || !SkipBytes(reader, textureLength)) ENTITY_BODY_FAIL();
        if (!reader.Read(modelLength) || !SkipBytes(reader, modelLength) || !reader.Read(clothesType)) ENTITY_BODY_FAIL();
    }

    // Weapons: raw uchar slot. Until slot 0xFF: raw uchar weapon type, raw ushort total ammo, next raw uchar slot.
    uint8_t slot = 0;
    if (!reader.Read(slot)) ENTITY_BODY_FAIL();
    uint32_t weaponCount = 0;
    while (slot != 0xFF) {
        uint8_t weaponType = 0;
        uint16_t totalAmmo = 0;
        if (!reader.Read(weaponType) || !reader.Read(totalAmmo) || !reader.Read(slot)) ENTITY_BODY_FAIL();
        ++weaponCount;
    }

    // Current weapon slot: raw uchar.
    uint8_t currentSlot = 0;
    if (!reader.Read(currentSlot)) ENTITY_BODY_FAIL();

    // Animation: one flag bit. When set: name strings, then the timing fields in reference order.
    bool hasAnim = false;
    if (!ReadFlag(reader, hasAnim)) ENTITY_BODY_FAIL();
    std::string blockName, animName;
    int32_t animTime = 0, blendTime = 0;
    float elapsedTime = 0, speed = 0;
    bool looped = false, updatePosition = false, interruptable = false, freezeLastFrame = false, taskRestore = false;
    if (hasAnim) {
        if (!PedWire::ReadLengthPrefixedString(reader, blockName) || !PedWire::ReadLengthPrefixedString(reader, animName) ||
            !reader.Read(animTime) || !ReadFlag(reader, looped) || !ReadFlag(reader, updatePosition) ||
            !ReadFlag(reader, interruptable) || !ReadFlag(reader, freezeLastFrame) || !reader.Read(blendTime) ||
            !ReadFlag(reader, taskRestore) || !reader.Read(elapsedTime) || !reader.Read(speed))
            ENTITY_BODY_FAIL();
    }

    visual.x = x;
    visual.y = y;
    visual.z = z;
    visual.heading = heading;
    visual.model = static_cast<int>(model);

    DIAG("ped stored id=%u model=%u pos=%.2f %.2f %.2f heading=%.3f", common.id, model, x, y, z, heading);
    DIAG("ped detail id=%u health=%.1f armor=%.1f vehicle=%u seat=%u jetpack=%d synced=%d headless=%d frozen=%d alpha=%u move=%u clothes=%u weapons=%u slot=%u anim=%d bit=%zu",
         common.id, health, armor, vehicleId, static_cast<unsigned>(seat), hasJetpack ? 1 : 0, synced ? 1 : 0, headless ? 1 : 0, frozen ? 1 : 0,
         static_cast<unsigned>(alpha), static_cast<unsigned>(moveAnim), static_cast<unsigned>(clothesCount), weaponCount,
         static_cast<unsigned>(currentSlot), hasAnim ? 1 : 0, reader.GetReadOffsetBits());
    if (hasAnim) {
        DIAG("ped anim id=%u block=%s anim=%s time=%d blend=%d loop=%d update=%d interrupt=%d freeze=%d restore=%d elapsed=%.1f speed=%.3f",
             common.id, blockName.c_str(), animName.c_str(), animTime, blendTime, looped ? 1 : 0, updatePosition ? 1 : 0, interruptable ? 1 : 0,
             freezeLastFrame ? 1 : 0, taskRestore ? 1 : 0, elapsedTime, speed);
    }
    return true;
}

} // namespace WorldRPG::Entity
