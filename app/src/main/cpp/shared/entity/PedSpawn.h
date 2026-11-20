#pragma once

// Every field of the ENTITY_ADD ped body (docs/ROADMAP_PEDS_SYNC.md phase 1).
// Desktop: CPacketHandler::Packet_EntityAdd, case CClientGame::PED (CPacketHandler.cpp 3997-4187) stores these on the
// CClientPed, and CClientPed::_CreateModel (CClientPed.cpp 5669-6224) applies them to the GTA body when it streams in.
// This record is that stored state. The parser fills it; the game thread applies it (phase 2).

#include <cstdint>
#include <string>
#include <vector>

namespace WorldRPG::Entity {

struct PedClothes {
    std::string texture;
    std::string model;
    uint8_t type = 0;
};

struct PedWeapon {
    uint8_t slot = 0;
    uint8_t type = 0;
    uint16_t totalAmmo = 0;
};

// CStaticFunctionDefinitions::SetPedAnimation arguments plus the cache fields Packet_EntityAdd writes (4172-4183).
struct PedAnimation {
    bool present = false;
    std::string block;
    std::string name;
    int32_t time = 0;
    int32_t blendTime = 0;
    bool looped = false;
    bool updatePosition = false;
    bool interruptable = false;
    bool freezeLastFrame = false;
    bool taskRestore = false;
    float elapsedTime = 0;  // ms; desktop startTime = GetTickCount64_() - elapsedTime
    float speed = 1.f;
    uint64_t receivedAtMs = 0;  // steady clock at parse, so startTime can be rebuilt later
};

struct PedSpawn {
    float health = 100.f;   // SetHealth (4057)
    float armor = 0.f;      // SetArmor (4058)
    bool synced = false;    // LockHealth / LockArmor with the same values (4059-4063)
    uint32_t vehicleId = 0xFFFFFFFFu;  // WarpIntoVehicle (4064-4065)
    uint8_t seat = 0xFF;
    bool hasJetpack = false;  // SetHasJetPack (4066)
    bool headless = false;    // SetHeadless (4067)
    bool frozen = false;      // SetFrozen (4068)
    uint8_t alpha = 255;      // SetAlpha (4073)
    uint8_t moveAnim = 0;     // SetMoveAnim (4078); MOVE_DEFAULT is 0
    std::vector<PedClothes> clothes;  // AddClothes x n, then RebuildModel (4102-4108)
    std::vector<PedWeapon> weapons;   // GiveWeapon per slot (4130-4135)
    uint8_t currentSlot = 0;          // SetCurrentWeaponSlot (4145)
    bool collisions = true;           // SetUsesCollision(bCollisonsEnabled) from the common header (4148)
    uint32_t dimension = 0;           // common header
    PedAnimation animation;           // SetPedAnimation + SetHasSyncedAnim(true) (4151-4184)
};

}  // namespace WorldRPG::Entity
