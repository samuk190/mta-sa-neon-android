#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

struct lua_State;

namespace WorldRPG::Models {

// libGTASA 2.10 arm64 CObjectData::ms_aObjectInfo. Initialise copies 0x58-byte
// groups. SetObjectData (0x53f584) multiplies the group by 0x58 and reads mass
// at +0. CObject::ObjectDamage reads the smash multiplier at +0x38 and the break
// velocity at +0x3c/+0x40/+0x44, then randomness at +0x48. CObject::Explode reads
// the fx type at +0x20, the fx offset at +0x24, and the fx blueprint at +0x30.
// The model group id is the signed halfword at CBaseModelInfo+0x2a.
constexpr std::size_t kObjectGroupBytes = 0x58;
constexpr int kObjectGroupCount = 160;
constexpr std::size_t kGroupMass = 0;
constexpr std::size_t kGroupTurnMass = 4;
constexpr std::size_t kGroupAirResistance = 8;
constexpr std::size_t kGroupElasticity = 12;
constexpr std::size_t kGroupBuoyancy = 16;
constexpr std::size_t kGroupUprootLimit = 20;
constexpr std::size_t kGroupColDamageMultiplier = 24;
constexpr std::size_t kGroupColDamageEffect = 28;
constexpr std::size_t kGroupSpecialColResponse = 29;
constexpr std::size_t kGroupCameraAvoid = 30;
constexpr std::size_t kGroupCausesExplosion = 31;
constexpr std::size_t kGroupFxType = 32;
constexpr std::size_t kGroupFxOffset = 36;
constexpr std::size_t kGroupFxPointer = 48;
constexpr std::size_t kGroupSmash = 56;
constexpr std::size_t kGroupBreakVelocity = 60;
constexpr std::size_t kGroupBreakVelocityRand = 72;
constexpr std::size_t kGroupBreakMode = 76;
constexpr std::size_t kGroupSparks = 80;

struct ObjectGroup {
    float mass = 0;
    float turnMass = 0;
    float airResistance = 0;
    float elasticity = 0;
    float buoyancy = 0;
    float uprootLimit = 0;
    float colDamageMultiplier = 0;
    std::uint8_t colDamageEffect = 0;
    std::uint8_t specialColResponse = 0;
    std::uint8_t cameraAvoid = 0;
    std::uint8_t causesExplosion = 0;
    std::uint8_t fxType = 0;
    float fxOffset[3] = {};
    float smashMultiplier = 0;
    float breakVelocity[3] = {};
    float breakVelocityRand = 0;
    std::uint32_t breakMode = 0;
    std::uint32_t sparksOnImpact = 0;
    bool haveFx = false;
};

enum class GroupValueKind { Float, Bool, Vector, Damage, Response, FxType, BreakMode, FxSystem };

bool GroupProperty(std::string_view text, GroupValueKind& kind);
void ReadObjectGroup(const void* bytes, ObjectGroup& group);
void WriteObjectGroup(const ObjectGroup& group, void* bytes);

struct GroupOriginal {
    bool saved = false;
    unsigned char bytes[kObjectGroupBytes] = {};
};

void RememberGroup(GroupOriginal& saved, const void* bytes);
void AssignGroupFloat(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, float value);
void AssignGroupByte(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, std::uint8_t value);
void AssignGroupBoolByte(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, bool value);
void AssignGroupVector(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, const float value[3]);
void AssignGroupWord(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, std::uint32_t value);
void AssignGroupBoolWord(unsigned char* bytes, GroupOriginal& saved, std::size_t offset, bool value);
void ClearGroupFx(unsigned char* bytes, GroupOriginal& saved);
bool MutateLiveObjectGroup(int groupId, void (*apply)(unsigned char*, GroupOriginal&, void*), void* user);

// Model physical-properties group. 0xFFFF is reported as -1.
struct ModelGroupRecord {
    bool saved = false;
    std::int16_t original = -1;
    std::int16_t current = -1;
};

std::int16_t GroupIndexFromHalf(std::uint16_t bits);
std::uint16_t HalfFromGroupIndex(int group);
bool SetModelGroup(ModelGroupRecord& record, int group);
bool RestoreModelGroup(ModelGroupRecord& record);

bool ReadLiveModelGroup(std::uint16_t modelId, std::int16_t& group);
bool WriteLiveModelGroup(std::uint16_t modelId, int group);
bool RestoreLiveModelGroup(std::uint16_t modelId);

bool ReadLiveObjectGroup(int groupId, ObjectGroup& group);
bool WriteLiveObjectGroup(int groupId, const ObjectGroup& group, bool remember);
bool RestoreLiveObjectGroup(int groupId);

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
void BindObjectGroupLua(lua_State* L);
}
