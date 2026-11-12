#pragma once

#include "ElementTypes.h"
#include "../net/raknet/RakNetWrapper.h"

#include <cstdint>
#include <cstring>

namespace WorldRPG::Sync {

/// Player synchronization data — matches MTA desktop packet format
struct PlayerSyncData {
    ElementID elementId = INVALID_ELEMENT_ID;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    float health = 100.0f;
    float armor = 0.0f;
    uint8_t weapon = 0;
    ElementID vehicleId = INVALID_ELEMENT_ID;
    uint8_t seat = 0;
    float moveSpeed[3] = {0.0f, 0.0f, 0.0f};
    uint32_t timestamp = 0;

    void writeToBitStream(Net::BitStreamWriter& bs) const {
        bs.Write(static_cast<uint16_t>(elementId));
        for (int i = 0; i < 3; ++i) bs.Write(position[i]);
        for (int i = 0; i < 3; ++i) bs.Write(rotation[i]);
        bs.Write(health);
        bs.Write(armor);
        bs.Write(weapon);
        bs.Write(static_cast<uint16_t>(vehicleId));
        bs.Write(seat);
        for (int i = 0; i < 3; ++i) bs.Write(moveSpeed[i]);
        bs.Write(timestamp);
    }

    bool readFromBitStream(Net::BitStreamReader& bs) {
        uint16_t eid = 0;
        if (!bs.Read(eid)) return false;
        elementId = static_cast<ElementID>(eid);
        for (int i = 0; i < 3; ++i) if (!bs.Read(position[i])) return false;
        for (int i = 0; i < 3; ++i) if (!bs.Read(rotation[i])) return false;
        if (!bs.Read(health)) return false;
        if (!bs.Read(armor)) return false;
        if (!bs.Read(weapon)) return false;
        uint16_t vid = 0;
        if (!bs.Read(vid)) return false;
        vehicleId = static_cast<ElementID>(vid);
        if (!bs.Read(seat)) return false;
        for (int i = 0; i < 3; ++i) if (!bs.Read(moveSpeed[i])) return false;
        if (!bs.Read(timestamp)) return false;
        return true;
    }
};

/// Vehicle synchronization data
struct VehicleSyncData {
    ElementID elementId = INVALID_ELEMENT_ID;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    float velocity[3] = {0.0f, 0.0f, 0.0f};
    float turnVelocity[3] = {0.0f, 0.0f, 0.0f};
    float health = 1000.0f;
    uint16_t model = 0;
    uint8_t colors[2] = {0, 0};
    uint32_t damageModel = 0;
    uint32_t timestamp = 0;

    void writeToBitStream(Net::BitStreamWriter& bs) const {
        bs.Write(static_cast<uint16_t>(elementId));
        for (int i = 0; i < 3; ++i) bs.Write(position[i]);
        for (int i = 0; i < 3; ++i) bs.Write(rotation[i]);
        for (int i = 0; i < 3; ++i) bs.Write(velocity[i]);
        for (int i = 0; i < 3; ++i) bs.Write(turnVelocity[i]);
        bs.Write(health);
        bs.Write(model);
        bs.Write(colors[0]);
        bs.Write(colors[1]);
        bs.Write(damageModel);
        bs.Write(timestamp);
    }

    bool readFromBitStream(Net::BitStreamReader& bs) {
        uint16_t eid = 0;
        if (!bs.Read(eid)) return false;
        elementId = static_cast<ElementID>(eid);
        for (int i = 0; i < 3; ++i) if (!bs.Read(position[i])) return false;
        for (int i = 0; i < 3; ++i) if (!bs.Read(rotation[i])) return false;
        for (int i = 0; i < 3; ++i) if (!bs.Read(velocity[i])) return false;
        for (int i = 0; i < 3; ++i) if (!bs.Read(turnVelocity[i])) return false;
        if (!bs.Read(health)) return false;
        if (!bs.Read(model)) return false;
        if (!bs.Read(colors[0])) return false;
        if (!bs.Read(colors[1])) return false;
        if (!bs.Read(damageModel)) return false;
        if (!bs.Read(timestamp)) return false;
        return true;
    }
};

/// Object synchronization data
struct ObjectSyncData {
    ElementID elementId = INVALID_ELEMENT_ID;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    uint16_t model = 0;
    uint8_t alpha = 255;
    float scale[3] = {1.0f, 1.0f, 1.0f};
    uint32_t timestamp = 0;

    void writeToBitStream(Net::BitStreamWriter& bs) const {
        bs.Write(static_cast<uint16_t>(elementId));
        for (int i = 0; i < 3; ++i) bs.Write(position[i]);
        for (int i = 0; i < 3; ++i) bs.Write(rotation[i]);
        bs.Write(model);
        bs.Write(alpha);
        for (int i = 0; i < 3; ++i) bs.Write(scale[i]);
        bs.Write(timestamp);
    }

    bool readFromBitStream(Net::BitStreamReader& bs) {
        uint16_t eid = 0;
        if (!bs.Read(eid)) return false;
        elementId = static_cast<ElementID>(eid);
        for (int i = 0; i < 3; ++i) if (!bs.Read(position[i])) return false;
        for (int i = 0; i < 3; ++i) if (!bs.Read(rotation[i])) return false;
        if (!bs.Read(model)) return false;
        if (!bs.Read(alpha)) return false;
        for (int i = 0; i < 3; ++i) if (!bs.Read(scale[i])) return false;
        if (!bs.Read(timestamp)) return false;
        return true;
    }
};

/// Ped synchronization data
struct PedSyncData {
    ElementID elementId = INVALID_ELEMENT_ID;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    uint16_t model = 0;
    float health = 100.0f;
    float armor = 0.0f;
    uint8_t taskType = 0;
    uint32_t timestamp = 0;

    void writeToBitStream(Net::BitStreamWriter& bs) const {
        bs.Write(static_cast<uint16_t>(elementId));
        for (int i = 0; i < 3; ++i) bs.Write(position[i]);
        for (int i = 0; i < 3; ++i) bs.Write(rotation[i]);
        bs.Write(model);
        bs.Write(health);
        bs.Write(armor);
        bs.Write(taskType);
        bs.Write(timestamp);
    }

    bool readFromBitStream(Net::BitStreamReader& bs) {
        uint16_t eid = 0;
        if (!bs.Read(eid)) return false;
        elementId = static_cast<ElementID>(eid);
        for (int i = 0; i < 3; ++i) if (!bs.Read(position[i])) return false;
        for (int i = 0; i < 3; ++i) if (!bs.Read(rotation[i])) return false;
        if (!bs.Read(model)) return false;
        if (!bs.Read(health)) return false;
        if (!bs.Read(armor)) return false;
        if (!bs.Read(taskType)) return false;
        if (!bs.Read(timestamp)) return false;
        return true;
    }
};

} // namespace WorldRPG::Sync