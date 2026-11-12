#include "PacketHandler.h"
#include "SyncTypes.h"
#include "NativeTaskSync.h"
#include "entity/PedSyncState.h"
#include "PerformanceProfiler.h"
#include "LuaVMStub.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/RemoteVehicle.h"
#include "VehicleSync.h"
#include "ServerWorld.h"
#include "CombatSync.h"
#include "../gtasa/RemoteCombat.h"

namespace PedSyncState = WorldRPG::Sync::PedSyncState;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text);
}

// Defined in net/NeonJoinProtocol.cpp: VEHICLE_INOUT replies for the local ped (net thread).
namespace WorldRPG::Net {
bool SendVehicleInOutNotify(uint32_t pedId, uint32_t vehicleId, uint8_t action);
void ClearVehicleInOutPending();
void ClearVehicleInOutConfirmed();
}

namespace {
constexpr uint32_t kElementIdBits = 17;

bool ReadElementId(WorldRPG::Net::BitStreamReader& reader, uint32_t& out) {
    uint8_t bytes[4] = {};
    if (!reader.ReadBits(bytes, kElementIdBits)) return false;
    uint32_t value = 0;
    std::memcpy(&value, bytes, 4);
    out = value == ((1u << kElementIdBits) - 1) ? 0xFFFFFFFFu : value;
    return true;
}

int ReadableScore(const std::string& text) {
    int score = 0;
    for (unsigned char ch : text) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            ++score;
    }
    return score;
}

std::string ReadChatText(WorldRPG::Net::BitStreamReader& reader) {
    const size_t chars = reader.GetRemainingBits() / 8;
    if (chars == 0 || chars > 256) return {};
    std::vector<uint8_t> raw(chars);
    if (!reader.ReadBits(raw.data(), chars * 8)) return {};
    std::string text;
    text.reserve(chars);
    for (unsigned char ch : raw) {
        if (ch >= 0x20 && ch <= 0x7E) text.push_back(static_cast<char>(ch));
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

bool HandleChatEcho(const uint8_t* body, size_t length, size_t bits) {
    // Mobile peers receive R,G,B and one color bit, then the text. The server
    // has already removed the 17-bit speaker id and the message type. A
    // desktop-shaped packet (those 25 bits still present) is tried when the
    // short header does not produce a readable line. That is how a sentence
    // typed on the desktop client was being dropped.
    WorldRPG::Net::BitStreamReader mobile(body, length, bits);
    uint8_t red = 0, green = 0, blue = 0, coded = 0;
    if (!mobile.Read(red) || !mobile.Read(green) || !mobile.Read(blue) || !mobile.ReadBits(&coded, 1))
        return false;
    std::string text = ReadChatText(mobile);
    if (ReadableScore(text) < 2) {
        WorldRPG::Net::BitStreamReader desktop(body, length, bits);
        uint8_t ignored = 0;
        uint32_t speaker = 0;
        if (desktop.Read(red) && desktop.Read(green) && desktop.Read(blue) &&
            desktop.ReadBits(&coded, 1) && ReadElementId(desktop, speaker) &&
            desktop.Read(ignored)) {
            const std::string other = ReadChatText(desktop);
            if (ReadableScore(other) > ReadableScore(text)) text = other;
        }
    }
    if (ReadableScore(text) < 1) return false;
    char colored[400];
    std::snprintf(colored, sizeof(colored), "#%02X%02X%02X%s", red, green, blue, text.c_str());
    PKT_LOGI("ChatEcho: %s", colored);
    if (!WorldRPG::Script::ActiveVmOnChat(text.c_str(), red, green, blue))
        WorldRPG::JniBridge::PostGameLine(0, colored);
    return true;
}

bool HandlePlayerSpawn(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t playerId = 0;
    uint8_t flags = 0;
    float x = 0, y = 0, z = 0, rotation = 0;
    if (!ReadElementId(reader, playerId) || !reader.Read(flags) ||
        !reader.Read(x) || !reader.Read(y) || !reader.Read(z) || !reader.Read(rotation)) {
        return false;
    }
    uint16_t skin = 7;
    if (reader.Read(skin) && skin > 312) skin = 7;
    char line[96];
    std::snprintf(line, sizeof(line), "Spawn %.0f %.0f %.0f", x, y, z);
    PKT_LOGI("PlayerSpawn: id=%u %s", playerId, line);
    // The join's SERVER_JOINEDGAME id is the local player. The first spawn is adopted only when no join id was
    // recorded; once it is known, a spawn is ours only when its id matches.
    const uint32_t local = WorldRPG::ServerWorld::LocalPlayer();
    const bool mine = local == 0 || playerId == local;
    if (mine) {
        if (local == 0) WorldRPG::ServerWorld::SetLocalPlayer(playerId);
        WorldRPG::Game::ApplyServerSpawn(x, y, z);
        WorldRPG::JniBridge::PostGameLine(2, "100|");
        WorldRPG::JniBridge::PostGameLine(1, line);
        return true;
    }
    WorldRPG::ServerWorld::NotePlayerSkin(playerId, static_cast<int>(skin));
    float heading = rotation;
    if (std::fabs(heading) > 6.5f) heading = heading * 3.14159265f / 180.f;
    WorldRPG::Game::NoteRemotePed(playerId, x, y, z, heading);
    return true;
}

bool ReadFixed24(WorldRPG::Net::BitStreamReader& reader, float& out);

bool ReadFlag(WorldRPG::Net::BitStreamReader& reader, bool& out) {
    uint8_t bit = 0;
    if (!reader.ReadBits(&bit, 1)) return false;
    out = (bit & 1u) != 0;
    return true;
}

// MTA blue / RakNet 3 WriteNormVector: sign of X, then Y and Z as
// optional 16-bit values. X is rebuilt so the vector stays unit length.
bool ReadMtaNorm(WorldRPG::Net::BitStreamReader& reader, float& x, float& y, float& z) {
    bool xNeg = false, yZero = false, zZero = false;
    if (!ReadFlag(reader, xNeg) || !ReadFlag(reader, yZero)) return false;
    if (yZero) y = 0.f;
    else {
        uint16_t raw = 0;
        if (!reader.Read(raw)) return false;
        y = (static_cast<float>(raw) / 32767.5f) - 1.f;
    }
    if (!ReadFlag(reader, zZero)) return false;
    if (zZero) z = 0.f;
    else {
        uint16_t raw = 0;
        if (!reader.Read(raw)) return false;
        z = (static_cast<float>(raw) / 32767.5f) - 1.f;
    }
    const float rest = 1.f - y * y - z * z;
    x = rest > 0.f ? std::sqrt(rest) : 0.f;
    if (xNeg) x = -x;
    return true;
}

bool ReadMtaVelocity(WorldRPG::Net::BitStreamReader& reader, float& x, float& y, float& z) {
    bool moving = false;
    if (!ReadFlag(reader, moving)) return false;
    if (!moving) {
        x = y = z = 0.f;
        return true;
    }
    float modulus = 0;
    if (!reader.Read(modulus) || !ReadMtaNorm(reader, x, y, z)) return false;
    x *= modulus;
    y *= modulus;
    z *= modulus;
    return true;
}

bool ReadCompressedU16(WorldRPG::Net::BitStreamReader& reader, uint16_t& out) {
    if (!reader.GetRemainingBits()) return false;
    bool highByteEmpty = false;
    if (!ReadFlag(reader, highByteEmpty)) return false;
    if (!highByteEmpty) {
        uint8_t bytes[2] = {};
        if (!reader.ReadBits(bytes, 16)) return false;
        out = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
        return true;
    }
    bool nibbleEmpty = false;
    if (!ReadFlag(reader, nibbleEmpty)) return false;
    if (!nibbleEmpty) {
        uint8_t low = 0;
        if (!reader.Read(low)) return false;
        out = low;
        return true;
    }
    uint8_t nibble = 0;
    if (!reader.ReadBits(&nibble, 4)) return false;
    out = static_cast<uint16_t>(nibble & 0x0Fu);
    return true;
}

// Server -> client PLAYER_PURESYNC. Stops at the position: one player per
// packet, and the camera tail is not needed to place the ped.
bool HandlePlayerPureSync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    uint8_t context = 0;
    uint16_t latency = 0;
    if (!ReadElementId(reader, id) || !reader.Read(context) || !ReadCompressedU16(reader, latency))
        return false;
    uint8_t keys = 0;
    if (!reader.Read(keys)) return false;
    bool analog = false;
    if (!ReadFlag(reader, analog) || (analog && !reader.Read(keys))) return false;
    if (!ReadFlag(reader, analog) || (analog && !reader.Read(keys))) return false;
    uint8_t stickX = 0, stickY = 0;
    if (!reader.Read(stickX) || !reader.Read(stickY)) return false;
    uint8_t flagBytes[2] = {};
    if (!reader.ReadBits(flagBytes, 15)) return false;
    const bool hasContact = (flagBytes[0] & 0x20) != 0;
    const bool hasVelocity = (flagBytes[1] & 0x04) != 0;
    if (hasContact) {
        uint32_t contact = 0;
        if (!ReadElementId(reader, contact)) return false;
    }
    float x = 0, y = 0, z = 0;
    if (!ReadFixed24(reader, x) || !ReadFixed24(reader, y) || !reader.Read(z)) return false;
    float heading = 0;
    uint8_t raw[2] = {};
    if (!reader.ReadBits(raw, 16)) return false;
    const unsigned value = raw[0] | (raw[1] << 8);
    constexpr float pi = 3.14159265f;
    heading = -pi + (static_cast<float>(value) / 65535.f) * (2.f * pi);
    float vx = 0, vy = 0, vz = 0;
    if (hasVelocity && !ReadMtaVelocity(reader, vx, vy, vz)) return false;
    (void)latency;
    if (id != WorldRPG::ServerWorld::LocalPlayer())
        WorldRPG::Game::NoteRemotePed(id, x, y, z, heading, vx, vy, vz);
    // Victim health (SPlayerHealthSync, 8 bits, 0..255), read after velocity as in
    // CNetAPI::ReadPlayerPuresync. A failed read keeps the position update.
    uint8_t health = 0;
    if (reader.Read(health)) {
        WorldRPG::Combat::RegisterVictimHealth(id, health, WorldRPG::ServerWorld::LocalPlayer());
    } else {
        PKT_LOGI("DIAG PURESYNC health read failed id=%u bit=%zu", id, reader.GetReadOffsetBits());
    }
    static int reports = 0;
    if (reports < 4) {
        ++reports;
        PKT_LOGI("PlayerSync: id=%u at %.0f %.0f %.0f", id, x, y, z);
    }
    return true;
}

bool ReadFixed24(WorldRPG::Net::BitStreamReader& reader, float& out) {
    uint8_t bytes[4] = {};
    if (!reader.ReadBits(bytes, 24)) return false;
    int32_t raw = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16);
    if (raw & 0x800000) raw |= ~0xFFFFFF;
    out = static_cast<float>(raw) / 1024.f;
    return true;
}

using WorldRPG::Sync::SNativeTaskLocomotionSync;
using WorldRPG::Sync::SNativeTaskWeaponPresentationSync;
using WorldRPG::Sync::SNativeTaskAnimationPresentationSync;
using WorldRPG::Sync::kEffectivePedSyncBitStream;
using WorldRPG::Sync::kBitStreamNativeTaskLocomotion;
using WorldRPG::Sync::kBitStreamNativeTaskPresentation;

// CPedSync::Packet_PedSync — matches mta_neon Client/mods/deathmatch/logic/CPedSync.cpp:289
// Full ped sync with native task locomotion, weapon presentation, animation
// presentation, camera rotation, onfire/inwater bits, and animation kill flag.
bool HandlePedSync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    int applied = 0;
    PKT_LOGI("DIAG PED_SYNC body len=%zu bits=%zu", length, bits);
    // Reference Packet_PedSync: while (GetNumberOfUnreadBits() > 32). No entry cap.
    while (reader.GetRemainingBits() > 32) {
        uint32_t id = 0;
        uint8_t ucSyncTimeContext = 0, ucFlags = 0, flags2 = 0;
        if (!ReadElementId(reader, id) || !reader.Read(ucSyncTimeContext) || !reader.Read(ucFlags) || !reader.Read(flags2)) {
            PKT_LOGI("DIAG PED_SYNC short header bit=%zu", reader.GetReadOffsetBits());
            break;
        }
        PKT_LOGI("DIAG PED_SYNC id=%u ctx=%u flags=0x%02x flags2=0x%02x bit=%zu", id, ucSyncTimeContext, ucFlags, flags2, reader.GetReadOffsetBits());

        // Native task blocks - read before spatial data per mta_neon wire order
        // Blocks exist on the wire only when the negotiated bitstream carries them
        // (mta_neon: BitStream.Can(eBitStreamVersion::NativeTask...)). At 0x0030 they are skipped.
        const bool hasLocomotion = (flags2 & 0x02) &&
            kEffectivePedSyncBitStream >= kBitStreamNativeTaskLocomotion;
        const bool hasWeapon = (flags2 & 0x04) &&
            kEffectivePedSyncBitStream >= kBitStreamNativeTaskPresentation;
        const bool hasAnimation = (flags2 & 0x08) &&
            kEffectivePedSyncBitStream >= kBitStreamNativeTaskPresentation;

        SNativeTaskLocomotionSync nativeTaskLocomotion;
        if (hasLocomotion && !nativeTaskLocomotion.Read(reader)) {
            PKT_LOGI("DIAG PED_SYNC id=%u bad locomotion bit=%zu", id, reader.GetReadOffsetBits());
            break;
        }

        SNativeTaskWeaponPresentationSync nativeTaskWeaponPresentation;
        if (hasWeapon && !nativeTaskWeaponPresentation.Read(reader)) {
            PKT_LOGI("DIAG PED_SYNC id=%u bad weapon bit=%zu", id, reader.GetReadOffsetBits());
            break;
        }

        SNativeTaskAnimationPresentationSync nativeTaskAnimationPresentation;
        if (hasAnimation && !nativeTaskAnimationPresentation.Read(reader)) {
            PKT_LOGI("DIAG PED_SYNC id=%u bad animation bit=%zu", id, reader.GetReadOffsetBits());
            break;
        }

        float x = 0, y = 0, z = 0, heading = 0;
        bool hasPosition = false;
        if (ucFlags & 0x01) {
            if (!ReadFixed24(reader, x) || !ReadFixed24(reader, y) || !reader.Read(z)) {
                PKT_LOGI("DIAG PED_SYNC id=%u bad position bit=%zu", id, reader.GetReadOffsetBits());
                break;
            }
            hasPosition = true;
        }
        if (ucFlags & 0x02) {
            uint8_t raw[2] = {};
            if (!reader.ReadBits(raw, 16)) break;
            const unsigned value = raw[0] | (raw[1] << 8);
            constexpr float pi = 3.14159265f;
            heading = -pi + (static_cast<float>(value) / 65535.f) * (2.f * pi);
        }
        float vx = 0, vy = 0, vz = 0;
        if (ucFlags & 0x04) {
            if (!ReadMtaVelocity(reader, vx, vy, vz)) {
                PKT_LOGI("DIAG PED_SYNC id=%u bad velocity bit=%zu", id, reader.GetReadOffsetBits());
                break;
            }
        }
        float fHealth = 0, fArmor = 0;
        if (ucFlags & 0x08) { if (!reader.Read(fHealth)) break; }
        if (ucFlags & 0x10) { if (!reader.Read(fArmor)) break; }

        // Camera rotation - SPedRotationSync uses 12-bit FloatAsBits(-PI, PI)
        float cameraRotation = 0;
        if (flags2 & 0x01) {
            uint8_t camRaw[2] = {};
            if (!reader.ReadBits(camRaw, 12)) break;
            const unsigned camValue = camRaw[0] | ((camRaw[1] & 0x0Fu) << 8);
            constexpr float pi = 3.14159265f;
            cameraRotation = -pi + (static_cast<float>(camValue & 0xFFFu) / 4095.f) * (2.f * pi);
        }

        // OnFire and IsInWater state bits
        bool bOnFire = false, bIsInWater = false;
        if (ucFlags & 0x20) { uint8_t bit = 0; if (!reader.ReadBits(&bit, 1)) break; bOnFire = (bit & 1u) != 0; }
        if (ucFlags & 0x40) { uint8_t bit = 0; if (!reader.ReadBits(&bit, 1)) break; bIsInWater = (bit & 1u) != 0; }

        // Animation kill/cleanup flag - ucFlags & 0x80
        // When set, the syncer signals that a synchronized animation ended or
        // was overridden. Mirror the cleanup locally so the remote ped does not
        // remain permanently excluded from native-task locomotion presentation.
        const bool animationKilled = (ucFlags & 0x80) != 0;

        if (hasPosition || (ucFlags & 0x02)) {
            WorldRPG::Game::NoteRemotePed(id, x, y, z, heading, vx, vy, vz);
            // Stage S4: observed peds interpolate to the target (reference SetTargetPosition /
            // SetTargetRotation with the spatial receive interval). Hosted peds keep local authority.
            if (ucFlags & 0x07) {
                const uint64_t rate = PedSyncState::SpatialSyncRateMs(id, true);
                if (!PedSyncState::IsHosted(id)) {
                    if (hasPosition)
                        PedSyncState::SetObserverPosition(id, {x, y, z}, {vx, vy, vz}, rate);
                    if (ucFlags & 0x02)
                        PedSyncState::SetObserverRotation(id, heading, rate);
                }
            }
            // Apply native task states to remote ped
            if (hasLocomotion) WorldRPG::Game::SetPedNativeTaskLocomotion(id, nativeTaskLocomotion.data.uiMode);
            if (hasWeapon) WorldRPG::Game::SetPedNativeTaskWeapon(id, nativeTaskWeaponPresentation.data.ucWeaponType);
            if (hasAnimation) WorldRPG::Game::SetPedNativeTaskAnimation(id, nativeTaskAnimationPresentation.data.usAnimGroup, nativeTaskAnimationPresentation.data.usAnimId);
            if (flags2 & 0x01) WorldRPG::Game::SetPedCameraRotation(id, cameraRotation);
            WorldRPG::Game::SetPedOnFire(id, bOnFire);
            WorldRPG::Game::SetPedInWater(id, bIsInWater);
            if (animationKilled) WorldRPG::Game::KillPedAnimation(id);
            if (ucFlags & 0x08) WorldRPG::Game::LockPedHealth(id, fHealth);
            if (ucFlags & 0x10) WorldRPG::Game::LockPedArmor(id, fArmor);
            ++applied;
        }
    }
    if (applied > 0) {
        static int reports = 0;
        if (reports < 4) {
            ++reports;
            PKT_LOGI("PedSync: placed %d server ped(s) with native tasks", applied);
        }
    }
    return applied > 0;
}

float Angle12(unsigned raw) {
    constexpr float pi = 3.14159265f;
    return -pi + (static_cast<float>(raw & 0xFFFu) / 4095.f) * (2.f * pi);
}

bool SlotHasAmmo(unsigned slot) {
    return slot != 0 && slot != 1 && slot != 10 && slot != 11 && slot != 12;
}

// Relayed CKeysyncPacket::Write. The client read is CNetAPI::ReadKeysync.
bool HandleKeySync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    if (!ReadElementId(reader, id) || id == WorldRPG::ServerWorld::LocalPlayer()) return false;
    uint8_t keys = 0;
    if (!reader.Read(keys)) return false;
    // First byte is the button bitfield: bit 0x10 = ButtonCircle, 0x02 = RightShoulder1.
    const uint8_t buttons = keys;
    bool analog = false;
    if (!ReadFlag(reader, analog) || (analog && !reader.Read(keys))) return false;
    if (!ReadFlag(reader, analog) || (analog && !reader.Read(keys))) return false;
    uint8_t stickX = 0, stickY = 0;
    if (!reader.Read(stickX) || !reader.Read(stickY)) return false;
    uint8_t rotRaw[2] = {};
    if (!reader.ReadBits(rotRaw, 12)) return false;
    const float heading = Angle12(rotRaw[0] | ((rotRaw[1] & 0x0Fu) << 8));
    uint8_t camRaw[2] = {};
    if (!reader.ReadBits(camRaw, 12)) return false;
    const float camera = Angle12(camRaw[0] | ((camRaw[1] & 0x0Fu) << 8));
    uint8_t flag = 0;
    if (!reader.ReadBits(&flag, 4)) return false;
    const int sx = static_cast<int>(static_cast<int8_t>(stickX)) * 128 / 127;
    const int sy = static_cast<int>(static_cast<int8_t>(stickY)) * 128 / 127;
    const bool ducked = (flag & 0x01u) != 0;
    const bool attack = (keys & 0x12u) != 0;
    bool punch = false;
    unsigned slot = 0;
    if (attack) {
        uint8_t slotByte = 0;
        if (!reader.ReadBits(&slotByte, 4)) return true;
        slot = slotByte & 0x0Fu;
        if (slot <= 1) {
            punch = true;
            // ButtonCircle with a melee slot: the remote ped runs CTaskSimpleFight.
            // No fixed damage is applied here. Hit damage comes from the engine fight task.
            if ((buttons & 0x10u) != 0) WorldRPG::Combat::StartRemoteMelee(id, heading);
        }
    }
    WorldRPG::Game::NoteRemoteAim(id, heading, camera, sx, sy, punch, ducked);
    if (!attack || punch || !SlotHasAmmo(slot)) return true;
    uint16_t clip = 0;
    if (!ReadCompressedU16(reader, clip)) return true;
    uint8_t armRaw[2] = {};
    if (!reader.ReadBits(armRaw, 16)) return true;
    float ox = 0, oy = 0, oz = 0;
    if (!reader.Read(ox) || !reader.Read(oy) || !reader.Read(oz)) return true;
    float nx = 0, nz = 0, ny = 0;
    if (!ReadMtaNorm(reader, nx, nz, ny)) return true;
    (void)armRaw; (void)clip; (void)ox; (void)oy; (void)oz; (void)nx; (void)ny; (void)nz; (void)flag;
    return true;
}

// Relayed CBulletsyncPacket::Write. Damage is applied by the victim, then reported in pure sync.
bool HandleBulletSync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    WorldRPG::Combat::SBulletSync bullet;
    if (!WorldRPG::Combat::ReadPlayerBulletSync(reader, bullet)) return true;
    const uint32_t local = WorldRPG::ServerWorld::LocalPlayer();
    if (bullet.shooter == local) return true;
    if (!WorldRPG::Combat::AcceptPlayerBulletSync(bullet)) return true;
    WorldRPG::Game::NoteRemoteWeapon(bullet.shooter, bullet.weapon);
    WorldRPG::Combat::ShowRemoteTracer(bullet.shooter, bullet.weapon, bullet.start, bullet.end);
    WorldRPG::Combat::ApplyBulletDamage(bullet, local);
    return true;
}

// SYNC_SETTINGS (99). Client read: CPacketHandler::Packet_SyncSettings. Only the bullet weapon list is ported;
// the rest of the packet (extrapolation and fix flags) is not read.
bool HandleSyncSettings(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    WorldRPG::Combat::ReadSyncSettingsWeapons(reader);
    return true;
}

// WEAPON_BULLETSYNC (100). Client read: CNetAPI::ReadWeaponBulletsync.
bool HandleWeaponBulletSync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    WorldRPG::Combat::SWeaponBulletSync shot;
    if (!WorldRPG::Combat::ReadWeaponBulletSync(reader, shot)) return true;
    WorldRPG::Combat::ShowRemoteWeaponFire(shot.weaponElement, shot.start, shot.end);
    return true;
}

// CPedSync::Packet_PedStartSync — matches mta_neon Client/mods/deathmatch/logic/CPedSync.cpp:211
// Reads position, rotation, velocity, health/armor, camera rotation, and
// SNativeTaskAnimationPresentationSync physical takeover for airborne/climb anchor seeding.
bool HandlePedStartSync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    if (!ReadElementId(reader, id)) {
        PKT_LOGI("DIAG PED_STARTSYNC bad id bit=%zu", reader.GetReadOffsetBits());
        return false;
    }

    // Position (raw floats per mta_neon Packet_PedStartSync)
    float x = 0, y = 0, z = 0;
    if (!reader.Read(x) || !reader.Read(y) || !reader.Read(z)) {
        PKT_LOGI("DIAG PED_STARTSYNC id=%u bad position bit=%zu", id, reader.GetReadOffsetBits());
        return false;
    }

    // Rotation (raw float)
    float fRotation = 0;
    if (!reader.Read(fRotation)) {
        PKT_LOGI("DIAG PED_STARTSYNC id=%u bad rotation bit=%zu", id, reader.GetReadOffsetBits());
        return false;
    }

    // Velocity (raw floats)
    float vx = 0, vy = 0, vz = 0;
    if (!reader.Read(vx) || !reader.Read(vy) || !reader.Read(vz)) {
        PKT_LOGI("DIAG PED_STARTSYNC id=%u bad velocity bit=%zu", id, reader.GetReadOffsetBits());
        return false;
    }

    // Health and armor (raw floats)
    float fHealth = 0, fArmor = 0;
    if (!reader.Read(fHealth) || !reader.Read(fArmor)) {
        PKT_LOGI("DIAG PED_STARTSYNC id=%u bad health/armor bit=%zu", id, reader.GetReadOffsetBits());
        return false;
    }
    // Reference sets health/armor after UnlockHealth/UnlockArmor. No engine hook for that yet.
    (void)fHealth; (void)fArmor;

    // Camera rotation and physical takeover exist only from 0x0031 (device evidence 2026-11-12:
    // the server sends PED STARTSYNC at 305 bits to the 0x0030 peer, which ends after health/armor).
    float cameraRotation = 0;
    SNativeTaskAnimationPresentationSync nativeTaskPhysicalTakeover;
    if (kEffectivePedSyncBitStream >= 0x0031) {
        if (!reader.Read(cameraRotation)) {
            PKT_LOGI("DIAG PED_STARTSYNC id=%u bad camera bit=%zu", id, reader.GetReadOffsetBits());
            return false;
        }
        if (!nativeTaskPhysicalTakeover.Read(reader)) {
            PKT_LOGI("DIAG PED_STARTSYNC id=%u bad physical takeover bit=%zu", id, reader.GetReadOffsetBits());
            return false;
        }
    }

    WorldRPG::Game::NoteRemotePed(id, x, y, z, fRotation, vx, vy, vz);
    WorldRPG::Game::SetPedCameraRotation(id, cameraRotation);
    WorldRPG::Game::SetPedNativeTaskPhysicalTakeover(id, nativeTaskPhysicalTakeover.data.uiMode,
        nativeTaskPhysicalTakeover.data.usAnimGroup, nativeTaskPhysicalTakeover.data.usAnimId,
        nativeTaskPhysicalTakeover.data.fProgress);
    WorldRPG::Game::SetPedHost(id, true);
    // Reference CPedSync::AddPed: become syncer (SetSyncing true). No wander task is started here;
    // the ped keeps its own AI, exactly as the desktop client does.
    PedSyncState::BeginHosted(id, {x, y, z}, fRotation, {vx, vy, vz});
    WorldRPG::ServerWorld::SetActorHost(id, true);
    PKT_LOGI("PedStartSync: id=%u at %.0f %.0f %.0f physMode=%u", id, x, y, z, nativeTaskPhysicalTakeover.data.uiMode);
    return true;
}

// CPedSync::Packet_PedStopSync — matches mta_neon Client/mods/deathmatch/logic/CPedSync.cpp:269
// Locks health and armor before removing sync ownership, preserving final state.
bool HandlePedStopSync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    if (!ReadElementId(reader, id)) {
        PKT_LOGI("DIAG PED_STOPSYNC bad id bit=%zu", reader.GetReadOffsetBits());
        return false;
    }

    // Lock health and armor at current values before releasing sync ownership.
    // The mta_neon client calls pPed->LockHealth(pPed->GetHealth()) and
    // pPed->LockArmor(pPed->GetArmor()) here to freeze the final synced state.
    WorldRPG::Game::LockPedHealthCurrent(id);
    WorldRPG::Game::LockPedArmorCurrent(id);

    // Stage S4: release ownership and clear the wander task (reference RemovePed).
    PedSyncState::EndHosted(id);
    WorldRPG::ServerWorld::SetActorHost(id, false);
    PKT_LOGI("PedStopSync: id=%u locked and released", id);
    return true;
}

bool HandleVehicleStartSync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    float x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
    float vx = 0, vy = 0, vz = 0, tx = 0, ty = 0, tz = 0, health = 0;
    if (!ReadElementId(reader, id)) return false;
    if (!reader.Read(x) || !reader.Read(y) || !reader.Read(z)) return false;
    if (!reader.Read(rx) || !reader.Read(ry) || !reader.Read(rz)) return false;
    if (!reader.Read(vx) || !reader.Read(vy) || !reader.Read(vz)) return false;
    if (!reader.Read(tx) || !reader.Read(ty) || !reader.Read(tz) || !reader.Read(health)) return false;
    (void)vx; (void)vy; (void)vz; (void)tx; (void)ty; (void)tz; (void)health; (void)rx; (void)ry;
    constexpr float pi = 3.14159265f;
    WorldRPG::Game::NoteRemoteVehicle(id, 0, x, y, z, rz * pi / 180.f);
    WorldRPG::ServerWorld::SetActorHost(id, true);
    return true;
}

bool HandleVehicleStopSync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    if (!ReadElementId(reader, id)) return false;
    WorldRPG::ServerWorld::SetActorHost(id, false);
    return true;
}

bool HandleUnoccupiedVehicleSync(const uint8_t* body, size_t length, size_t bits) {
    // Packet 56 (CUnoccupiedVehicleSync.cpp 178): entries until fewer than 8 bits remain.
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    int applied = 0;
    while (reader.GetRemainingBits() >= 8) {
        WorldRPG::VehicleSync::UnoccupiedUpdate update;
        if (!WorldRPG::VehicleSync::ReadUnoccupiedEntry(reader, update)) break;
        WorldRPG::RemoteVehicle::ApplyUnoccupied(update);
        ++applied;
    }
    return applied > 0;
}

// Desktop CPacketHandler::Packet_Vehicle_InOut (mta_neon_source/Client/mods/deathmatch/logic/CPacketHandler.cpp:1804).
// Server layout (Server/mods/deathmatch/logic/packets/CVehicleInOutPacket.cpp, Write): ElementID ped (17 bits),
// ElementID vehicle (17), seat (4), action (4), then per action:
//   0 REQUEST_IN_CONFIRMED (CPacketHandler.cpp:1858)  door (3)
//   2 NOTIFY_IN_ABORT_RETURN (:1931)                  door (3), SDoorOpenRatioSync (not parsed)
//   3 REQUEST_OUT_CONFIRMED (:1995)                   door (2), optional
//   7 REQUEST_JACK_CONFIRMED (:2110)                  door (3)
//   9 ATTEMPT_FAILED (:2254)                          reason (8), SPositionSync when reason 5 (not parsed)
// Other actions carry jack or fell-off fields the local ped does not need, so they are logged only.
// Other players, the non-local branches of Packet_Vehicle_InOut (CPacketHandler.cpp 1858-2084). The seat is applied on the
// game thread (GameEngine ApplyRemoteSeat). Occupancy is set here, which is what makes packet 33 for this ped readable
// (CNetAPI.cpp 1306 reads it only for an occupied ped; the desktop sets it in CClientVehicle::SetPedOccupiedVehicle).
void ApplyRemoteVehicleInOut(uint8_t action, uint32_t pedId, uint32_t vehicleId, uint8_t seat) {
    if (pedId == 0 || pedId == 0xFFFFFFFFu) return;
    switch (action) {
        case 0:  // REQUEST_IN_CONFIRMED: desktop GetIntoVehicle (CPacketHandler.cpp 1886). The enter animation is not
                 // played, so the seat is taken at once.
        case 1:  // NOTIFY_IN_RETURN: desktop WarpIntoVehicle (CPacketHandler.cpp 1901)
        case 5:  // NOTIFY_OUT_ABORT_RETURN: desktop WarpIntoVehicle back in (CPacketHandler.cpp 2070)
            WorldRPG::RemoteVehicle::SetOccupant(pedId, vehicleId, seat);
            // Driver (seat 0) and passenger seats. GameEngine maps a passenger seat to its game door (PassengerGameDoor).
            WorldRPG::Game::NoteRemoteSeat(pedId, vehicleId, seat);
            break;
        case 2:  // NOTIFY_IN_ABORT_RETURN: desktop RemoveFromVehicle (CPacketHandler.cpp 1988)
        case 4:  // NOTIFY_OUT_RETURN: desktop RemoveFromVehicle (CPacketHandler.cpp 2036)
        case 6:  // NOTIFY_FELL_OFF_RETURN: desktop RemoveFromVehicle (CPacketHandler.cpp 2084)
            WorldRPG::RemoteVehicle::ClearOccupant(pedId);
            WorldRPG::Game::NoteRemoteUnseat(pedId);
            break;
        // 3 REQUEST_OUT_CONFIRMED keeps the seat until its return (desktop GetOutOfVehicle, CPacketHandler.cpp 2015).
        // 7 and 8 are jacking, not ported. 9 ATTEMPT_FAILED has nothing to undo for a remote ped.
        default:
            break;
    }
}

// Only the local ped is acted on here. Other peds go to ApplyRemoteVehicleInOut.
bool HandleVehicleInOut(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t pedId = 0, vehicleId = 0;
    uint8_t seat = 0, action = 0;
    if (!ReadElementId(reader, pedId) || !ReadElementId(reader, vehicleId) ||
        !reader.ReadBits(&seat, 4) || !reader.ReadBits(&action, 4)) {
        PKT_LOGE("VehInOut: truncated header");
        return false;
    }
    uint8_t door = 0;
    uint8_t reason = 0;
    switch (action) {
        case 0:  // REQUEST_IN_CONFIRMED
        case 2:  // NOTIFY_IN_ABORT_RETURN
        case 7:  // REQUEST_JACK_CONFIRMED
            if (!reader.ReadBits(&door, 3)) return false;
            break;
        case 3:  // REQUEST_OUT_CONFIRMED: the door is optional (CVehicleInOutPacket::Write writes it for doors < 4)
            if (!reader.ReadBits(&door, 2)) door = 0xFF;
            break;
        case 9:  // ATTEMPT_FAILED
            if (!reader.ReadBits(&reason, 8)) return false;
            break;
        default:
            break;
    }
    static const char* const kNames[] = {
        "request_in_confirmed", "notify_in_return", "notify_in_abort_return", "request_out_confirmed",
        "notify_out_return", "notify_out_abort_return", "notify_fell_off_return", "request_jack_confirmed",
        "notify_jack_return", "attempt_failed"};
    const char* name = action < 10 ? kNames[action] : "unknown";
    const bool mine = pedId != 0 && pedId == WorldRPG::ServerWorld::LocalPlayer();
    if (!mine) {
        PKT_LOGI("VehInOut: %s ped=%u veh=%u seat=%u door=%u reason=%u", name, pedId, vehicleId,
                 static_cast<unsigned>(seat), static_cast<unsigned>(door), static_cast<unsigned>(reason));
        ApplyRemoteVehicleInOut(action, pedId, vehicleId, seat);
        return true;
    }
    if (action == 9) {
        PKT_LOGI("VehInOut: recv attempt_failed veh=%u reason=%u (request rejected by server)", vehicleId,
                 static_cast<unsigned>(reason));
    } else {
        PKT_LOGI("VehInOut: recv %s veh=%u seat=%u door=%u", name, vehicleId, static_cast<unsigned>(seat),
                 static_cast<unsigned>(door));
    }
    switch (action) {
        case 0:  // REQUEST_IN_CONFIRMED: the server allows the entry, so send NOTIFY_IN (1)
            WorldRPG::Net::SendVehicleInOutNotify(pedId, vehicleId, 1);
            break;
        case 3:  // REQUEST_OUT_CONFIRMED: the server allows the exit, so send NOTIFY_OUT (4)
            WorldRPG::Net::ClearVehicleInOutConfirmed();
            WorldRPG::Net::SendVehicleInOutNotify(pedId, vehicleId, 4);
            break;
        case 7:  // REQUEST_JACK_CONFIRMED: jacking is not ported (no animation, no NOTIFY_JACK). Treat as not seated.
            WorldRPG::Net::ClearVehicleInOutPending();
            WorldRPG::Net::ClearVehicleInOutConfirmed();
            PKT_LOGI("VehInOut: jack not ported; local ped veh=%u not treated as seated, no packet 33", vehicleId);
            break;
        case 9:  // ATTEMPT_FAILED: the server refused the request, so the ped is not seated
            WorldRPG::Net::ClearVehicleInOutConfirmed();
            WorldRPG::Net::ClearVehicleInOutPending();
            break;
        case 1:  // NOTIFY_IN_RETURN
        case 2:  // NOTIFY_IN_ABORT_RETURN
        case 4:  // NOTIFY_OUT_RETURN
        case 5:  // NOTIFY_OUT_ABORT_RETURN
            WorldRPG::Net::ClearVehicleInOutPending();
            break;
        default:
            break;
    }
    return true;
}

// Packet 33 (CNetAPI.cpp 1306). The reference reads the body only when the local player has an
// occupied vehicle, so the same gate is used here: no known occupancy, packet ignored.
bool HandlePlayerVehiclePureSync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t playerId = 0;
    if (!ReadElementId(reader, playerId)) {
        DIAG("PLAYER_VEHICLE_PURESYNC fail line=%d player id", __LINE__);
        return false;
    }
    uint32_t vehicleId = 0;
    uint8_t seat = 0;
    if (!WorldRPG::RemoteVehicle::FindOccupant(playerId, vehicleId, seat)) {
        DIAG("PLAYER_VEHICLE_PURESYNC player=%u: no occupied vehicle known, body ignored", playerId);
        return true;
    }
    WorldRPG::VehicleSync::OccupiedSync sync;
    if (!WorldRPG::VehicleSync::ReadOccupiedSync(reader, seat, sync)) {
        DIAG("PLAYER_VEHICLE_PURESYNC fail player=%u veh=%u seat=%u", playerId, vehicleId, static_cast<unsigned>(seat));
        return false;
    }
    WorldRPG::RemoteVehicle::ApplyOccupied(playerId, vehicleId, seat, sync);
    // Driver move and turn speed: desktop CNetAPI::ReadVehiclePuresync sets them at CNetAPI.cpp 1420-1421 for the driver
    // block. Hosted cars are skipped as in ApplyOccupied. Trains are also skipped here: the desktop sets speed for them
    // too, but the phone has no train path yet (deviation).
    if (seat == 0 && sync.hasDriverBlock && !sync.trainBlock && !WorldRPG::ServerWorld::ActorHosted(vehicleId)) {
        WorldRPG::Game::NoteRemoteVehicleMotion(vehicleId, sync.velocity.x, sync.velocity.y, sync.velocity.z,
                                                sync.turnVelocity.x, sync.turnVelocity.y, sync.turnVelocity.z);
    }
    return true;
}

bool HandleResourceStart(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint8_t nameLength = 0;
    if (!reader.Read(nameLength) || nameLength == 0 || nameLength > 64) return false;
    std::vector<uint8_t> raw(nameLength);
    if (!reader.ReadBits(raw.data(), nameLength * 8)) return false;
    std::string name(raw.begin(), raw.end());
    name = WorldRPG::Net::NormalizeAscii(name);
    if (name.empty()) return false;
    static int resourceCount = 0;
    ++resourceCount;
    const int percent = resourceCount > 24 ? 95 : resourceCount * 4;
    char progress[160];
    std::snprintf(progress, sizeof(progress), "%d|Downloading %s", percent, name.c_str());
    PKT_LOGI("Recurso %s", name.c_str());
    WorldRPG::JniBridge::PostGameLine(2, progress);
    return true;
}
} // namespace

namespace WorldRPG::Sync {

struct PacketHandler::Impl {
    SyncManager* syncManager = nullptr;
    std::unordered_map<uint8_t, PacketHandlerFn> handlers;
};

PacketHandler::PacketHandler() : pImpl_(std::make_unique<Impl>()) {}
PacketHandler::~PacketHandler() = default;

bool PacketHandler::initialize(SyncManager& syncManager) {
    pImpl_->syncManager = &syncManager;
    PKT_LOGI("PacketHandler: initialized");
    return true;
}

void PacketHandler::registerHandler(MTAPacketID id, PacketHandlerFn handler) {
    const uint8_t gameId = static_cast<uint8_t>(id);
    pImpl_->handlers[gameId] = std::move(handler);
    PKT_LOGI("PacketHandler: registered handler for game packet ID %u", gameId);
}

void PacketHandler::registerAll(Net::RakNetWrapper& network) {
    if (!pImpl_->syncManager) {
        PKT_LOGE("PacketHandler::registerAll: not initialized");
        return;
    }

    // Register built-in handlers
    registerHandler(MTAPacketID::ELEMENT_CREATE, [this](const uint8_t* data, size_t len) -> bool {
        if (len < 3) return false;
        Net::BitStreamReader reader(data, len);
        uint8_t elemType = 0;
        uint16_t elemId = 0;
        if (!reader.Read(elemType) || !reader.Read(elemId)) return false;
        auto type = static_cast<ElementType>(elemType);
        pImpl_->syncManager->getElementManager().createElement(type, static_cast<ElementID>(elemId));
        return true;
    });

    registerHandler(MTAPacketID::ELEMENT_DESTROY, [this](const uint8_t* data, size_t len) -> bool {
        if (len < 3) return false;
        Net::BitStreamReader reader(data, len);
        uint8_t elemType = 0;
        uint16_t elemId = 0;
        if (!reader.Read(elemType) || !reader.Read(elemId)) return false;
        pImpl_->syncManager->getElementManager().destroyElement(
            static_cast<ElementType>(elemType), static_cast<ElementID>(elemId));
        return true;
    });

    registerHandler(MTAPacketID::PLAYER_SYNC, [this](const uint8_t* data, size_t len) -> bool {
        Net::BitStreamReader reader(data, len);
        PlayerSyncData sync;
        if (!sync.readFromBitStream(reader)) return false;
        pImpl_->syncManager->getElementManager().updatePlayerSync(sync);
        return true;
    });

    registerHandler(MTAPacketID::VEHICLE_SYNC, [this](const uint8_t* data, size_t len) -> bool {
        Net::BitStreamReader reader(data, len);
        VehicleSyncData sync;
        if (!sync.readFromBitStream(reader)) return false;
        pImpl_->syncManager->getElementManager().updateVehicleSync(sync);
        return true;
    });

    registerHandler(MTAPacketID::OBJECT_SYNC, [this](const uint8_t* data, size_t len) -> bool {
        Net::BitStreamReader reader(data, len);
        ObjectSyncData sync;
        if (!sync.readFromBitStream(reader)) return false;
        pImpl_->syncManager->getElementManager().updateObjectSync(sync);
        return true;
    });

    registerHandler(MTAPacketID::PED_SYNC, [this](const uint8_t* data, size_t len) -> bool {
        Net::BitStreamReader reader(data, len);
        PedSyncData sync;
        if (!sync.readFromBitStream(reader)) return false;
        pImpl_->syncManager->getElementManager().updatePedSync(sync);
        return true;
    });

    registerHandler(MTAPacketID::CHAT_MESSAGE, [this](const uint8_t* data, size_t len) -> bool {
        if (len < 2) return false;
        Net::BitStreamReader reader(data, len);
        uint8_t color = 0;
        if (!reader.Read(color)) return false;
        std::string msg;
        if (!reader.ReadString(msg, 512)) return false;
        // Chat messages are already ASCII-normalized by server
        PKT_LOGI("ChatMessage: [%u] %s", color, msg.c_str());
        return true;
    });

    registerHandler(MTAPacketID::SERVER_MESSAGE, [this](const uint8_t* data, size_t len) -> bool {
        Net::BitStreamReader reader(data, len);
        std::string msg;
        if (!reader.ReadString(msg, 512)) return false;
        PKT_LOGI("ServerMessage: %s", msg.c_str());
        return true;
    });

    // Set the network packet handler callback to route user packets through us
    network.SetPacketHandler([this](uint8_t packetId, const uint8_t* data, size_t length, size_t bitLength) -> bool {
        return handlePacket(packetId, data, length, bitLength);
    });

    PKT_LOGI("PacketHandler: all handlers registered with network layer");
}

bool PacketHandler::handlePacket(uint8_t packetId, const uint8_t* data, size_t length, size_t bitLength) {
    PROFILE_SCOPE("PacketHandler::handlePacket");

    // Wire envelope: RID_USER_PACKET_ENUM (99) followed by the game packet
    // type byte. Strip the UPE prefix before dispatching so handlers see the
    // game ID and body only.
    if (packetId == USER_PACKET_ENUM_ID) {
        if (length < 1) {
            PKT_LOGE("PacketHandler: truncated UPE packet (no game ID byte)");
            return false;
        }
        const uint8_t gameId = data[0];
        const uint8_t* body = data + 1;
        const size_t bodyLength = length - 1;
        const size_t bodyBits = bitLength >= 8 ? bitLength - 8 : bodyLength * 8;
        // Real ePacketID values from Shared/sdk/net/Packets.h. The map below
        // still uses the provisional 0..7 numbering and does not match these.
        if (gameId == 43) return HandleChatEcho(body, bodyLength, bodyBits);
        if (gameId == 31) return HandleKeySync(body, bodyLength, bodyBits);
        if (gameId == 32) return HandlePlayerPureSync(body, bodyLength, bodyBits);
        if (gameId == 98) return HandleBulletSync(body, bodyLength, bodyBits);
        if (gameId == 99) return HandleSyncSettings(body, bodyLength, bodyBits);
        if (gameId == 100) return HandleWeaponBulletSync(body, bodyLength, bodyBits);
        if (gameId == 63) return HandlePedSync(body, bodyLength, bodyBits);
        if (gameId == 61) return HandlePedStartSync(body, bodyLength, bodyBits);
        if (gameId == 62) return HandlePedStopSync(body, bodyLength, bodyBits);
        if (gameId == 54) return HandleVehicleStartSync(body, bodyLength, bodyBits);
        if (gameId == 55) return HandleVehicleStopSync(body, bodyLength, bodyBits);
        if (gameId == 33) return HandlePlayerVehiclePureSync(body, bodyLength, bodyBits);
        if (gameId == 56) return HandleUnoccupiedVehicleSync(body, bodyLength, bodyBits);
        if (gameId == 58) return HandleVehicleInOut(body, bodyLength, bodyBits);
        if (gameId == 81) return WorldRPG::Script::ActiveVmOnServerEvent(body, bodyLength, bodyBits);
        if (gameId == 26) return HandlePlayerSpawn(body, bodyLength, bodyBits);
        if (gameId == 25) { WorldRPG::ServerWorld::OnPlayerList(body, bodyLength, bodyBits); return true; }
        if (gameId == 50) { WorldRPG::ServerWorld::OnEntityAdd(body, bodyLength, bodyBits); return true; }
        if (gameId == 51) { WorldRPG::ServerWorld::OnEntityRemove(body, bodyLength, bodyBits); return true; }
        if (gameId == 84) { WorldRPG::ServerWorld::OnCustomData(body, bodyLength, bodyBits); return true; }
        if (gameId == 82) {
            WorldRPG::ServerWorld::OnResourceStart(body, bodyLength, bodyBits);
            return HandleResourceStart(body, bodyLength, bodyBits);
        }
        if (gameId == 95) {
            char progress[64];
            const int percent = std::min(98, 40 + static_cast<int>(bodyLength / 1024));
            std::snprintf(progress, sizeof(progress), "%d|Downloading resources", percent);
            WorldRPG::JniBridge::PostGameLine(2, progress);
            const bool hadChat = WorldRPG::Script::ActiveVmChatHooked();
            WorldRPG::Script::ActiveVmFeedScripts(body, bodyLength, bodyBits);
            if (!hadChat && WorldRPG::Script::ActiveVmChatHooked())
                WorldRPG::JniBridge::PostGameLine(8, "");
            return true;
        }
        auto it = pImpl_->handlers.find(gameId);
        if (it != pImpl_->handlers.end()) {
            return it->second(body, bodyLength);
        }
        if (gameId != 32 && gameId != 33 && gameId != 56 && gameId != 63 && gameId != 77)
            PKT_LOGI("PacketHandler: unhandled game packet ID %u (%zu bytes)", gameId, bodyLength);
        return false;
    }

    return false;
}

} // namespace WorldRPG::Sync
