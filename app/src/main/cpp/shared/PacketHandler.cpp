#include "PacketHandler.h"
#include "CorePackets.h"
#include "SyncTypes.h"
#include "NativeTaskSync.h"
#include "MtaIds.h"
#include "ConnectionLoading.h"
#include "entity/EntityAddTypes.h"
#include "entity/EntityVehicleModels.h"
#include "entity/PedWire.h"
#include "entity/PedSyncState.h"
#include "PerformanceProfiler.h"
#include "DebugFlags.h"
#include "LuaVMStub.h"
#include "ControlToggles.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/RemoteVehicle.h"
#include "VehicleSync.h"
#include "ServerWorld.h"
#include "ServerRpc.h"
#include "PedQueries.h"
#include "PlayerWeaponSync.h"
#include "CombatSync.h"
#include "ResourceFiles.h"
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
bool SendVehicleJackAbort(uint32_t pedId, uint32_t vehicleId, uint8_t door);
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
        if (ch >= 0x20) text.push_back(static_cast<char>(ch));
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

// The network the packet handlers were registered with (PacketHandler::registerAll).
const WorldRPG::Net::RakNetWrapper* g_packetNetwork = nullptr;

bool HandleChatEcho(const uint8_t* body, size_t length, size_t bits) {
    // The standard header has 50 bits. The shortened mobile header has 25.
    // When a caller has only whole bytes, retain its legacy layout fallback.
    WorldRPG::Net::BitStreamReader mobile(body, length, bits);
    uint8_t red = 0, green = 0, blue = 0, coded = 0;
    if (!mobile.Read(red) || !mobile.Read(green) || !mobile.Read(blue) || !mobile.ReadBits(&coded, 1))
        return false;
    std::string text = ReadChatText(mobile);
    // Through PC Network the server writes the desktop header (it sees a
    // desktop bitstream version), and netc reports whole bytes, so the
    // bit-length test below cannot tell.
    const bool standardHeader = (bits % 8 == 2) || (g_packetNetwork && g_packetNetwork->IsPcNetwork());
    if (standardHeader || (bits % 8 == 0 && ReadableScore(text) < 2)) {
        WorldRPG::Net::BitStreamReader desktop(body, length, bits);
        uint8_t ignored = 0;
        uint32_t speaker = 0;
        if (desktop.Read(red) && desktop.Read(green) && desktop.Read(blue) &&
            desktop.ReadBits(&coded, 1) && ReadElementId(desktop, speaker) &&
            desktop.Read(ignored)) {
            const std::string other = ReadChatText(desktop);
            if (standardHeader || ReadableScore(other) > ReadableScore(text)) text = other;
        }
    }
    if (text.empty()) return false;
    char colored[400];
    std::snprintf(colored, sizeof(colored), "#%02X%02X%02X%s", red, green, blue, text.c_str());
    PKT_LOGI("ChatEcho: %s", colored);
    const bool notCancelled = WorldRPG::Script::ActiveVmOnChat(text.c_str(), red, green, blue);
    if (notCancelled)
        WorldRPG::JniBridge::PostGameLine(0, colored);
    return true;
}

// PACKET_ID_LUA_ELEMENT_RPC (77). Desktop CRPCFunctions::ProcessPacket (CRPCFunctions.cpp 125-172): u8 function id, then
// the source ElementID, then the handler's own fields. Ids from shared/MtaIds.h. Handlers ported here are the ped and
// player branches; other element types and functions log once and are ignored, as they were before.
bool ReadFlag(WorldRPG::Net::BitStreamReader& reader, bool& out);

// NetBitStream::ReadString<unsigned char>: a raw u8 length, then the bytes.
static bool ReadUcharString(WorldRPG::Net::BitStreamReader& reader, std::string& out) {
    uint8_t length = 0;
    out.clear();
    if (!reader.Read(length)) return false;
    if (length == 0) return true;
    std::vector<uint8_t> bytes(length);
    if (!reader.ReadBits(bytes.data(), static_cast<size_t>(length) * 8)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

// CClientPickupManager::IsValidWeaponID (CClientPickupManager.cpp 106-109).
static bool IsValidWeaponId(unsigned id) { return id > 0 && id != 19 && id != 20 && id != 21 && id <= 46; }

bool HandleLuaElementRPC(const uint8_t* body, size_t length, size_t bits) {
    namespace Rpc = WorldRPG::MtaIds::Rpc;
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint8_t func = 0;
    uint32_t id = 0;
    if (!reader.Read(func) || func >= Rpc::NumRpcFuncs || !ReadElementId(reader, id)) return false;
    const char* kind = WorldRPG::ServerWorld::ElementKindName(id);
    const bool vehicleLike = kind && std::strcmp(kind, "vehicle") == 0;
    const bool pedLike = kind && (std::strcmp(kind, "ped") == 0 || std::strcmp(kind, "player") == 0);
    auto logUnported = [&](const char* why) {
        static bool seen[256] = {};
        if (!seen[func]) {
            seen[func] = true;
            PKT_LOGI("DIAG LUA_ELEMENT_RPC func=%u id=%u kind=%s %s", static_cast<unsigned>(func), id, kind ? kind : "?", why);
        }
    };
    // CColShapeRPCs.cpp:30-111. Same payload widths/order as desktop; only
    // the client geometry changes. Collider refresh runs on the VM pulse.
    using Shape=WorldRPG::Spatial::ShapeType;
    auto updateShape=[&](const auto& update) { WorldRPG::ServerWorld::UpdateColshape(id,update); };
    switch(func) {
        case Rpc::SetColshapeRadius: {
            float radius=0; if(!reader.Read(radius)) return false;
            updateShape([&](auto& g) {
                if(g.type!=Shape::Circle&&g.type!=Shape::Sphere&&g.type!=Shape::Tube) return false;
                g.radius=(radius<0?0.f:radius); return true;
            });
            return true;
        }
        case Rpc::SetColshapeSize: {
            float x=0,y=0,z=0; if(!reader.Read(x)||!reader.Read(y)||!reader.Read(z)) return false;
            updateShape([&](auto& g) {
                if(g.type==Shape::Cuboid||g.type==Shape::Rectangle) g.size={(x<0?0.f:x),(y<0?0.f:y),(z<0?0.f:z)};
                else if(g.type==Shape::Tube) g.height=(x<0?0.f:x); else return false;
                return true;
            });
            return true;
        }
        case Rpc::SetColPolygonHeight: {
            float floor=0,ceiling=0; if(!reader.Read(floor)||!reader.Read(ceiling)) return false;
            updateShape([&](auto& g) {
                if(g.type!=Shape::Polygon || (g.floor==floor && g.ceiling==ceiling)) return false;
                g.floor=floor; g.ceiling=ceiling; return true;
            });
            return true;
        }
        case Rpc::AddColPolygonPoint:
        case Rpc::UpdateColPolygonPoint: {
            float x=0,y=0; uint32_t index=0;
            if(!WorldRPG::Entity::ReadPosition2(reader,x,y)) return false;
            bool indexed=reader.Read(index);
            if(func==Rpc::UpdateColPolygonPoint && !indexed) return false;
            updateShape([&](auto& g) {
                if(g.type!=Shape::Polygon) return false;
                if(func==Rpc::UpdateColPolygonPoint) { if(index>=g.points.size()) return false; g.points[index]={x,y,0}; }
                else if(!indexed) g.points.push_back({x,y,0});
                else { if(index>g.points.size()) return false; g.points.insert(g.points.begin()+index,{x,y,0}); }
                return true;
            });
            return true;
        }
        case Rpc::RemoveColPolygonPoint: {
            uint32_t index=0; if(!reader.Read(index)) return false;
            updateShape([&](auto& g) {
                if(g.type!=Shape::Polygon || g.points.size()<=3 || index>=g.points.size()) return false;
                g.points.erase(g.points.begin()+index); return true;
            });
            return true;
        }
        default: break;
    }
    switch (func) {
        case Rpc::SetElementData:
            // Any element type (CElementRPCs.cpp:84-102). ServerWorld fires onClientElementDataChange (LifecycleHooks).
            return WorldRPG::ServerWorld::ReadSetElementDataRpc(id, reader);
        case Rpc::RemoveElementData:
            return WorldRPG::ServerWorld::ReadRemoveElementDataRpc(id, reader);
        case Rpc::SetVehicleColor: {
            WorldRPG::VehicleColors colors;
            if (!WorldRPG::ReadVehicleColors(reader, colors)) return false;
            WorldRPG::ServerWorld::SetVehicleColors(id, colors);
            return true;
        }
        case Rpc::SetVehicleLocked: {
            // CVehicleRPCs::SetVehicleLocked (161-175) reads a full byte.
            uint8_t locked = 0;
            if (!reader.Read(locked)) return false;
            WorldRPG::ServerWorld::SetVehicleDoorLockMode(id, locked != 0 ? 2 : 1);
            return true;
        }
        case Rpc::AddVehicleUpgrade: {
            // CVehicleRPCs.cpp:255-270 sends the full upgrade id as uint16.
            uint16_t upgrade = 0;
            if (!reader.Read(upgrade)) return false;
            if (WorldRPG::Entity::VehicleModels::IsNitroUpgrade(upgrade))
                WorldRPG::ServerWorld::SetVehicleNitroUpgrade(id, upgrade, true);
            else
                logUnported("non-nitro vehicle upgrades are not applied");
            return true;
        }
        case Rpc::AddAllVehicleUpgrades:
            // CVehicleRPCs.cpp:272-283. The desktop adds in ascending order, ending the nitro slot at 1010.
            WorldRPG::ServerWorld::SetVehicleNitroUpgrade(id, WorldRPG::Entity::VehicleModels::kNitroUpgrade10x, true);
            logUnported("non-nitro vehicle upgrades are not applied");
            return true;
        case Rpc::RemoveVehicleUpgrade: {
            // CVehicleRPCs.cpp:285-303 sends the upgrade id minus 1000 as uint8.
            uint8_t upgrade = 0;
            if (!reader.Read(upgrade)) return false;
            const auto modelId = static_cast<uint16_t>(1000 + upgrade);
            if (WorldRPG::Entity::VehicleModels::IsNitroUpgrade(modelId))
                WorldRPG::ServerWorld::SetVehicleNitroUpgrade(id, modelId, false);
            else
                logUnported("non-nitro vehicle upgrades are not applied");
            return true;
        }
        case Rpc::SetElementPosition: {
            // CElementRPCs::SetElementPosition (122-170): x, y, z floats, u8 time context; a player then has an
            // optional u8 warp (Teleport vs SetPosition, both place the body here).
            float x = 0, y = 0, z = 0;
            uint8_t context = 0;
            if (!reader.Read(x) || !reader.Read(y) || !reader.Read(z) || !reader.Read(context)) return false;
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            WorldRPG::ServerWorld::SetSyncTimeContext(id, context);
            WorldRPG::ServerWorld::NoteElementPosition(id, x, y, z);
            if (pedLike) WorldRPG::Game::RpcSetPosition(id, x, y, z);
            else if (kind && std::strcmp(kind, "vehicle") == 0) WorldRPG::Game::RpcSetVehiclePosition(id, x, y, z);
            return true;
        }
        case Rpc::SetElementVelocity:
        case Rpc::SetVehicleTurnSpeed: {
            // CElementRPCs.cpp:172-214 and CVehicleRPCs.cpp:124-137.
            float x = 0, y = 0, z = 0;
            if (!reader.Read(x) || !reader.Read(y) || !reader.Read(z)) return false;
            if (vehicleLike) WorldRPG::Game::RpcSetVehicleVelocity(id, x, y, z, func == Rpc::SetVehicleTurnSpeed);
            else logUnported("native velocity is unavailable for this element");
            return true;
        }
        case Rpc::SetVehicleRotation: {
            float x = 0, y = 0, z = 0;
            uint8_t context = 0;
            if (!reader.Read(x) || !reader.Read(y) || !reader.Read(z) || !reader.Read(context)) return false;
            if (vehicleLike) {
                WorldRPG::Game::RpcSetVehicleRotation(id, x, y, z);
                WorldRPG::ServerWorld::SetSyncTimeContext(id, context);
            }
            return true;
        }
        case Rpc::SetVehicleEngineState: {
            bool enabled = false;
            if (!ReadFlag(reader, enabled)) return false;
            if (vehicleLike) WorldRPG::Game::SetTrafficVehicleEngine(id, enabled);
            return true;
        }
        case Rpc::SetVehicleFrozen:
        case Rpc::SetElementFrozen: {
            // CElementRPCs::SetElementFrozen (697-728): one bit.
            bool frozen = false;
            if (!ReadFlag(reader, frozen)) return false;
            if (!pedLike && !vehicleLike) { logUnported("native frozen state is unavailable for this element"); return true; }
            if (id == WorldRPG::ServerWorld::LocalPlayer()) logUnported("local SetFrozen not ported");
            WorldRPG::Game::RpcSetFrozen(id, frozen);
            return true;
        }
        case Rpc::SetElementHealth: {
            // CElementRPCs::SetElementHealth (471-535): float health, u8 time context.
            float health = 0;
            uint8_t context = 0;
            if (!reader.Read(health) || !reader.Read(context)) return false;
            WorldRPG::ServerWorld::SetSyncTimeContext(id, context);
            if (!pedLike) {
                if (kind && std::strcmp(kind, "vehicle") == 0) WorldRPG::Game::RpcSetVehicleHealth(id, health);
                else logUnported("health on a non-ped element not ported");
                return true;
            }
            bool healthLocked = false, armorLocked = false;
            float lockedHealth = 0.f, lockedArmor = 0.f;
            WorldRPG::Game::CopyPedVitalsLock(id, healthLocked, lockedHealth, armorLocked, lockedArmor);
            // LockHealth stores the value; only SetHealth clears m_bDead (CClientPed.cpp:3655-3675,3745-3749).
            WorldRPG::PedQueries::Health(id, health, !healthLocked);
            WorldRPG::Game::RpcSetHealth(id, health);
            return true;
        }
        case Rpc::SetPedArmor: {
            // CPedRPCs::SetPedArmor (42-60): u8 armor (value / 1.25), u8 time context.
            uint8_t armor = 0, context = 0;
            if (!reader.Read(armor) || !reader.Read(context)) return false;
            WorldRPG::ServerWorld::SetSyncTimeContext(id, context);
            WorldRPG::Game::RpcSetArmor(id, static_cast<float>(armor) / 1.25f);
            return true;
        }
        case Rpc::SetPedRotation: {
            // CPedRPCs::SetPedRotation (62-90): SPedRotationSync (16-bit angle), u8 time context, optional u8 newWay.
            // Desktop stores the time context after the rotation (CPedRPCs.cpp 87). Both rotation forms write the
            // current rotation here.
            float heading = 0;
            uint8_t context = 0;
            if (!WorldRPG::Entity::PedWire::ReadAngle16(reader, heading) || !reader.Read(context)) return false;
            WorldRPG::Game::RpcSetRotation(id, heading);
            if (pedLike) WorldRPG::ServerWorld::SetSyncTimeContext(id, context);
            if (id == WorldRPG::ServerWorld::LocalPlayer()) logUnported("local SetCurrentRotation not ported");
            return true;
        }
        case Rpc::SetPedDoingGangDriveby: {
            bool enabled = false;
            uint8_t context = 0;
            if (!ReadFlag(reader, enabled) || !reader.Read(context)) return false;
            if (pedLike) {
                WorldRPG::ServerWorld::SetSyncTimeContext(id, context);
                WorldRPG::Game::NoteDriveby(id, enabled);
            }
            return true;
        }
        case Rpc::WarpPedIntoVehicle: {
            // CPedRPCs.cpp:190-212. Occupancy is available before the bodies stream in.
            uint32_t vehicle = 0;
            uint8_t seat = 0, context = 0;
            if (!ReadElementId(reader, vehicle) || !reader.Read(seat) || !reader.Read(context)) return false;
            if (pedLike) WorldRPG::ServerWorld::SetSyncTimeContext(id, context);
            if (pedLike &&
                std::strcmp(WorldRPG::ServerWorld::ElementKindName(vehicle), "vehicle") == 0) {
                WorldRPG::RemoteVehicle::SetOccupant(id, vehicle, seat);
                WorldRPG::Game::NoteRemoteSeat(id, vehicle, seat);
            }
            return true;
        }
        case Rpc::RemovePedFromVehicle: {
            // CPedRPCs.cpp:214-236. Release the seat and update its sync time context.
            uint8_t context = 0;
            if (!reader.Read(context)) return false;
            if (pedLike) WorldRPG::ServerWorld::SetSyncTimeContext(id, context);
            if (pedLike) {
                WorldRPG::RemoteVehicle::ClearOccupant(id);
                WorldRPG::Game::NoteRemoteUnseat(id);
            }
            return true;
        }
        case Rpc::SetElementInterior: {
            // CElementRPCs::SetElementInterior (252-280): u8 interior, u8 setPosition, then x, y, z when it is 1.
            uint8_t interior = 0, setPosition = 0;
            if (!reader.Read(interior) || !reader.Read(setPosition)) return false;
            if(kind && std::strcmp(kind,"colshape")==0) {
                float x=0,y=0,z=0;
                if(setPosition==1 && (!reader.Read(x)||!reader.Read(y)||!reader.Read(z))) return false;
                WorldRPG::ServerWorld::NoteElementInterior(id,interior);
                if(setPosition==1) WorldRPG::ServerWorld::NoteElementPosition(id,x,y,z);
                return true;
            }
            if (!pedLike) {
                float x=0,y=0,z=0;
                if(setPosition==1 && (!reader.Read(x)||!reader.Read(y)||!reader.Read(z))) return false;
                WorldRPG::ServerWorld::NoteElementInterior(id,interior);
                if(setPosition==1) WorldRPG::ServerWorld::NoteElementPosition(id,x,y,z);
                logUnported("native interior on a non-ped element not ported; client area stored"); return true;
            }
            WorldRPG::ServerWorld::NoteElementInterior(id, interior);
            WorldRPG::Game::RpcSetInterior(id, interior);
            if (setPosition == 1) {
                float x = 0, y = 0, z = 0;
                if (!reader.Read(x) || !reader.Read(y) || !reader.Read(z)) return false;
                WorldRPG::ServerWorld::NoteElementPosition(id, x, y, z);
                WorldRPG::Game::RpcSetPosition(id, x, y, z);
            }
            return true;
        }
        case Rpc::SetElementModel: {
            // CElementRPCs::SetElementModel (536-647): u16 model, then ResolveModelID and SetModel. The model record
            // change makes ApplyRemotePeds replace the body (_ChangeModel). Server-registry ids are not resolved.
            uint16_t model = 0;
            if (!reader.Read(model)) return false;
            if (!pedLike) { logUnported("model on a non-ped element not ported"); return true; }
            if (model > 312) { logUnported("server model id outside 0..312 not resolved (ResolveModelID)"); return true; }
            WorldRPG::ServerWorld::NoteElementModel(id, model);
            return true;
        }
        case Rpc::SetPedAnimation: {
            // CPedRPCs::SetPedAnimation (259-320): ReadString<uchar> block; an empty block is KillAnimation. Otherwise
            // ReadString<uchar> anim, int time, loop / updatePosition / interruptible / freezeLastFrame bits, then an
            // optional int blend (default 250) and an optional task-restore bit (only honoured while ducked; the duck
            // restore is not ported).
            WorldRPG::Game::PedRpcOp op;
            if (!ReadUcharString(reader, op.block)) return false;
            if (!pedLike) { logUnported("animation on a non-ped element"); return true; }
            if (op.block.empty()) {
                op.kind = WorldRPG::Game::PedRpcOp::Kind::KillAnimation;
                WorldRPG::Game::QueuePedRpc(id, op);
                return true;
            }
            int32_t time = 0;
            if (!ReadUcharString(reader, op.name) || !reader.Read(time) || !ReadFlag(reader, op.loop) ||
                !ReadFlag(reader, op.updatePosition) || !ReadFlag(reader, op.interruptible) ||
                !ReadFlag(reader, op.freezeLastFrame))
                return false;
            op.time = time;
            int32_t blend = 250;
            if (reader.Read(blend)) op.blend = blend;
            op.kind = WorldRPG::Game::PedRpcOp::Kind::Animation;
            WorldRPG::Game::QueuePedRpc(id, op);
            return true;
        }
        case Rpc::SetPedAnimationProgress:
        case Rpc::SetPedAnimationSpeed: {
            // CPedRPCs::SetPedAnimationProgress (322-350): ReadString<uchar> anim; empty is KillAnimation; else float.
            // CPedRPCs::SetPedAnimationSpeed (352-375): the same, an empty name does nothing.
            WorldRPG::Game::PedRpcOp op;
            if (!ReadUcharString(reader, op.name)) return false;
            if (!pedLike) { logUnported("animation progress / speed on a non-ped element"); return true; }
            if (op.name.empty()) {
                if (func == Rpc::SetPedAnimationProgress) {
                    op.kind = WorldRPG::Game::PedRpcOp::Kind::KillAnimation;
                    WorldRPG::Game::QueuePedRpc(id, op);
                }
                return true;
            }
            if (!reader.Read(op.value)) return false;
            op.kind = func == Rpc::SetPedAnimationProgress ? WorldRPG::Game::PedRpcOp::Kind::AnimationProgress
                                                           : WorldRPG::Game::PedRpcOp::Kind::AnimationSpeed;
            WorldRPG::Game::QueuePedRpc(id, op);
            return true;
        }
        case Rpc::GiveWeapon: {
            // CWeaponRPCs::GiveWeapon (26-111): SWeaponTypeSync (6 bits), SWeaponAmmoSync total only (ReadCompressed
            // ushort), set-as-current bit. Weapon 0 or a valid pickup weapon id; ammo capped at 9999.
            uint8_t weapon = 0;
            uint32_t ammo = 0;
            bool setCurrent = false;
            if (!reader.ReadBits(&weapon, 6) || !WorldRPG::Entity::ReadCompressed(reader, 2, ammo)) return false;
            if (!ReadFlag(reader, setCurrent)) setCurrent = false;
            weapon &= 0x3F;
            if (!pedLike) { logUnported("weapon on a non-ped element"); return true; }
            if (weapon != 0 && !IsValidWeaponId(weapon)) return true;
            WorldRPG::Game::PedRpcOp op;
            op.kind = WorldRPG::Game::PedRpcOp::Kind::GiveWeapon;
            op.weapon = weapon;
            op.ammo = ammo > 9999 ? 9999 : ammo;
            op.setCurrent = setCurrent;
            op.queryGeneration = WorldRPG::PedQueries::Generation(id);
            WorldRPG::Game::QueuePedRpc(id, op);
            return true;
        }
        case Rpc::TakeWeapon: {
            // CWeaponRPCs::TakeWeapon (113-186): SWeaponTypeSync; with a following SWeaponAmmoSync the ammo is taken,
            // without it the weapon is removed, for the local player only ("affects sync").
            uint8_t weapon = 0;
            if (!reader.ReadBits(&weapon, 6)) return false;
            weapon &= 0x3F;
            if (!pedLike || !IsValidWeaponId(weapon)) return true;
            WorldRPG::Game::PedRpcOp op;
            op.weapon = weapon;
            uint32_t ammo = 0;
            if (WorldRPG::Entity::ReadCompressed(reader, 2, ammo)) {
                op.kind = WorldRPG::Game::PedRpcOp::Kind::TakeWeaponAmmo;
                op.ammo = ammo;
            } else if (id == WorldRPG::ServerWorld::LocalPlayer()) {
                op.kind = WorldRPG::Game::PedRpcOp::Kind::RemoveWeapon;
            } else {
                return true;
            }
            op.queryGeneration = WorldRPG::PedQueries::Generation(id);
            WorldRPG::Game::QueuePedRpc(id, op);
            return true;
        }
        case Rpc::TakeAllWeapons: {
            // CWeaponRPCs::TakeAllWeapons (188-196): no fields.
            if (!pedLike) return true;
            WorldRPG::Game::PedRpcOp op;
            op.kind = WorldRPG::Game::PedRpcOp::Kind::TakeAllWeapons;
            op.queryGeneration = WorldRPG::PedQueries::Generation(id);
            WorldRPG::Game::QueuePedRpc(id, op);
            return true;
        }
        case Rpc::SetWeaponAmmo: {
            // CWeaponRPCs::SetWeaponAmmo (211-260): SWeaponTypeSync, SWeaponAmmoSync total and in-clip (two
            // ReadCompressed ushort).
            uint8_t weapon = 0;
            uint32_t ammo = 0, clip = 0;
            if (!reader.ReadBits(&weapon, 6) || !WorldRPG::Entity::ReadCompressed(reader, 2, ammo) ||
                !WorldRPG::Entity::ReadCompressed(reader, 2, clip))
                return false;
            weapon &= 0x3F;
            if (!pedLike || !IsValidWeaponId(weapon)) return true;
            WorldRPG::Game::PedRpcOp op;
            op.kind = WorldRPG::Game::PedRpcOp::Kind::SetWeaponAmmo;
            op.weapon = weapon;
            op.ammo = ammo;
            op.ammoInClip = clip;
            op.queryGeneration = WorldRPG::PedQueries::Generation(id);
            WorldRPG::Game::QueuePedRpc(id, op);
            return true;
        }
        case Rpc::SetWeaponSlot: {
            // CWeaponRPCs::SetWeaponSlot (198-209): SWeaponSlotSync (4 bits).
            uint8_t slot = 0;
            if (!reader.ReadBits(&slot, 4)) return false;
            if (!pedLike) return true;
            WorldRPG::Game::PedRpcOp op;
            op.kind = WorldRPG::Game::PedRpcOp::Kind::SetWeaponSlot;
            op.slot = slot & 0x0F;
            op.queryGeneration = WorldRPG::PedQueries::Generation(id);
            WorldRPG::Game::QueuePedRpc(id, op);
            return true;
        }
        case Rpc::SetElementAlpha: {
            // CElementRPCs::SetElementAlpha (422-454): u8 alpha; peds and players -> CClientPed::SetAlpha.
            uint8_t alpha = 255;
            if (!reader.Read(alpha)) return false;
            if (!pedLike && !vehicleLike) { logUnported("native alpha is unavailable for this element"); return true; }
            WorldRPG::Game::RpcSetAlpha(id, alpha);
            return true;
        }
        case Rpc::SetElementCollisionsEnabled: {
            // CElementRPCs::SetElementCollisionsEnabled (658-696): one bit; peds and players -> SetUsesCollision.
            bool enabled = true;
            if (!ReadFlag(reader, enabled)) return false;
            if (!pedLike) { logUnported("collisions on a non-ped element not ported"); return true; }
            WorldRPG::Game::RpcSetCollisions(id, enabled);
            return true;
        }
        case Rpc::SetElementDimension: {
            // CElementRPCs::SetElementDimension (281-317): u16 dimension. The local player also sets every streamer
            // (SetAllDimensions). Teams (each member) are not tracked here.
            uint16_t dimension = 0;
            if (!reader.Read(dimension)) return false;
            if (kind && std::strcmp(kind, "team") == 0) { logUnported("team dimension (team members not tracked)"); return true; }
            if (id == WorldRPG::ServerWorld::LocalPlayer()) WorldRPG::ServerWorld::SetLocalDimension(dimension);
            WorldRPG::ServerWorld::NoteElementDimension(id, dimension);
            return true;
        }
        default: {
            const auto result = WorldRPG::ServerRpc::HandleElement(func, id, reader);
            if (result == WorldRPG::ServerRpc::Result::Malformed) return false;
            if (result == WorldRPG::ServerRpc::Result::NotHandled) logUnported("function not ported");
            return true;
        }
    }
}

// PACKET_ID_PLAYER_WASTED (27) and PACKET_ID_PED_WASTED (64), both read by CPacketHandler::Packet_PlayerWasted
// (CPacketHandler.cpp 1277-1366): ElementID, killer ElementID, SWeaponTypeSync (6 bits), SBodypartSync (3 bits), stealth
// bit, u8 time context, ReadCompressed AssocGroupId, ReadCompressed AnimationId. Then SetSyncTimeContext and Kill.
// The local player's own death is decided locally and is not touched here. HandleWasted fires the wasted Lua events.
bool HandleWasted(const uint8_t* body, size_t length, size_t bits, uint8_t packet) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0, killer = 0, group = 0, anim = 0;
    uint8_t weapon = 0, part = 0, context = 0;
    bool stealth = false;
    if (!ReadElementId(reader, id) || !ReadElementId(reader, killer) || !reader.ReadBits(&weapon, 6) ||
        !reader.ReadBits(&part, 3) || !ReadFlag(reader, stealth) || !reader.Read(context) ||
        !WorldRPG::Entity::ReadCompressed(reader, 4, group) || !WorldRPG::Entity::ReadCompressed(reader, 4, anim)) {
        PKT_LOGI("DIAG WASTED packet %u short (bits=%zu)", static_cast<unsigned>(packet), bits);
        return false;
    }
    WorldRPG::PedQueries::Wasted(id);
    WorldRPG::ServerWorld::SetSyncTimeContext(id, context);
    PKT_LOGI("Wasted: packet %u id=%u killer=%u weapon=%u part=%u stealth=%d anim=%u/%u", static_cast<unsigned>(packet), id,
             killer, static_cast<unsigned>(weapon & 0x3Fu), static_cast<unsigned>(part & 0x7u), stealth ? 1 : 0, group, anim);
    // onClientPlayerWasted / onClientPedWasted (CPacketHandler.cpp 1333-1357). Desktop fires it for the local player
    // when it sends its own death, and here only when that did not happen; the phone does not fire it on the local
    // death, so the server's packet always does.
    const bool player = id == WorldRPG::ServerWorld::LocalPlayer() || WorldRPG::ServerWorld::IsKnownPlayer(id);
    auto fire = [&] {
        WorldRPG::Script::ActiveVmFireWasted(player, id, killer, weapon & 0x3F, (part & 0x7) + 3, stealth,
                                             static_cast<int>(group), static_cast<int>(anim));
    };
    if (id == WorldRPG::ServerWorld::LocalPlayer()) {
        // Desktop runs Kill on the local player too (CPacketHandler.cpp 1319-1331, "needs to be done on the local
        // player to avoid desync"). Not wired on the phone: no local CTaskComplexDie hook.
        PKT_LOGI("DIAG WASTED local player: CClientPed::Kill not ported");
        fire();
        return true;
    }
    WorldRPG::Game::NoteRemoteWasted(id, weapon & 0x3F, static_cast<int>(group), static_cast<int>(anim));
    fire();
    return true;
}

bool HandleLuaRPC(const uint8_t* body, size_t length, size_t bits) {
    // CRPCFunctions::ProcessPacket (CRPCFunctions.cpp:125-148): the function id alone is a full packet for an RPC with
    // no payload (the reset RPCs).
    if (length < 1) return false;
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint8_t funcId = 0;
    if (!reader.Read(funcId)) return false;
    // SHOW_CHAT is 119 in rpc_enums.h (compiled, shared/MtaIds.h) and arrives on PACKET_ID_LUA (76); the device log
    // showed packet 76 with function 119. The old match was 120 on packet 68 (RCON_BAN), which never fired.
    // CInputRPCs::ToggleControl (CInputRPCs.cpp:180-196): uchar name length, the name bytes, uchar enabled; an empty
    // name is ignored and enabled counts only when it is exactly 1.
    if (funcId == WorldRPG::MtaIds::Rpc::ToggleControlAbility) {
        uint8_t nameLength = 0;
        if (!reader.Read(nameLength)) return false;
        std::string name(nameLength, '\0');
        uint8_t enabled = 0;
        if ((nameLength && !reader.ReadBits(reinterpret_cast<uint8_t*>(name.data()), nameLength * 8u)) ||
            !reader.Read(enabled)) return false;
        name.resize(std::strlen(name.c_str()));
        if (!name.empty()) WorldRPG::Controls::SetControlEnabled(name.c_str(), enabled == 1);
        return true;
    }
    // CInputRPCs::ToggleAllControls (CInputRPCs.cpp:198-206): uchar gta, uchar mta, uchar enabled.
    if (funcId == WorldRPG::MtaIds::Rpc::ToggleAllControlAbility) {
        uint8_t gta = 0, mta = 0, enabled = 0;
        if (!reader.Read(gta) || !reader.Read(mta) || !reader.Read(enabled)) return false;
        WorldRPG::Controls::SetAllControlsEnabled(gta == 1, mta == 1, enabled == 1);
        return true;
    }
    // Every other server RPC of packet 76 (ServerRpc.cpp): world, camera, money, HUD, sound, chat, input binds.
    const auto result = WorldRPG::ServerRpc::HandleGlobal(funcId, reader);
    if (result == WorldRPG::ServerRpc::Result::NotHandled && funcId < WorldRPG::MtaIds::Rpc::NumRpcFuncs) {
        static bool seen[256] = {};
        if (!seen[funcId]) {
            seen[funcId] = true;
            PKT_LOGI("DIAG LUA RPC %u: not ported (bits=%zu)", static_cast<unsigned>(funcId), bits);
        }
    }
    return result == WorldRPG::ServerRpc::Result::Handled;
}

// PLAYER_CLOTHES (30). Desktop CPacketHandler::Packet_PlayerClothes (CPacketHandler.cpp 5160-5195): ElementID,
// raw ushort count, then per item a raw uchar texture length and bytes, a raw uchar model length and bytes, and a raw
// uchar type; AddClothes each, then RebuildModel. The rebuild runs on the game thread (DressRemoteCj).
bool HandlePlayerClothes(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    uint16_t count = 0;
    if (!ReadElementId(reader, id) || !reader.Read(count)) return false;
    std::vector<WorldRPG::Entity::PedClothes> clothes;
    for (uint16_t i = 0; i < count; ++i) {
        WorldRPG::Entity::PedClothes item;
        uint8_t textureLength = 0, modelLength = 0;
        if (!reader.Read(textureLength)) return false;
        std::vector<uint8_t> texture(textureLength);
        if (textureLength && !reader.ReadBits(texture.data(), textureLength * 8u)) return false;
        if (!reader.Read(modelLength)) return false;
        std::vector<uint8_t> model(modelLength);
        if (modelLength && !reader.ReadBits(model.data(), modelLength * 8u)) return false;
        if (!reader.Read(item.type)) return false;
        item.texture.assign(texture.begin(), texture.end());
        item.model.assign(model.begin(), model.end());
        clothes.push_back(std::move(item));
    }
    static int reports = 0;
    if (reports < 4) {
        ++reports;
        PKT_LOGI("PlayerClothes: id=%u items=%u", id, static_cast<unsigned>(count));
    }
    WorldRPG::Game::NoteRemoteClothes(id, clothes);
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
    const bool haveSkin = reader.Read(skin);
    if (haveSkin && skin > 312) skin = 7;
    // Packet_PlayerSpawn (CPacketHandler.cpp 1196-1243): u8 interior, u16 dimension, then SetDimension after Spawn; the
    // local player's spawn also runs SetAllDimensions. Left unchanged when the fields are missing.
    uint8_t spawnInterior = 0;
    uint16_t spawnDimension = 0;
    const bool haveDimension = haveSkin && reader.Read(spawnInterior) && reader.Read(spawnDimension);
    uint32_t team = 0;
    uint8_t spawnContext = 0;
    if (haveDimension && reader.GetRemainingBits() > 7 &&
        (!ReadElementId(reader, team) || !reader.Read(spawnContext))) return false;
    char line[96];
    std::snprintf(line, sizeof(line), "Spawn %.0f %.0f %.0f", x, y, z);
    PKT_LOGI("PlayerSpawn: id=%u %s", playerId, line);
    // The join's SERVER_JOINEDGAME id is the local player. The first spawn is adopted only when no join id was
    // recorded; once it is known, a spawn is ours only when its id matches.
    const uint32_t local = WorldRPG::ServerWorld::LocalPlayer();
    const bool mine = local == 0 || playerId == local;
    if (!local) WorldRPG::ServerWorld::SetLocalPlayer(playerId);
    if (!mine) WorldRPG::ServerWorld::NotePlayerSkin(playerId, static_cast<int>(skin));
    WorldRPG::ServerWorld::SetSyncTimeContext(playerId, spawnContext);
    WorldRPG::RemoteVehicle::ClearOccupant(playerId);
    WorldRPG::PedQueries::Ensure(playerId, true);
    WorldRPG::PedQueries::Spawn(playerId);
    WorldRPG::Game::PedRpcOp clearWeapons;
    clearWeapons.kind = WorldRPG::Game::PedRpcOp::Kind::TakeAllWeapons;
    clearWeapons.queryGeneration = WorldRPG::PedQueries::Generation(playerId);
    WorldRPG::Game::QueuePedRpc(playerId, clearWeapons);
    if (mine) {
        WorldRPG::Net::ClearVehicleInOutPending();
        if (haveDimension) {
            WorldRPG::ServerWorld::SetLocalDimension(spawnDimension);
            WorldRPG::ServerWorld::NoteElementInterior(playerId,spawnInterior);
            WorldRPG::Game::RpcSetInterior(playerId, spawnInterior);
        }
        WorldRPG::Game::ApplyServerSpawn(x, y, z);
        WorldRPG::JniBridge::PostGameLine(2, "100|");
        WorldRPG::JniBridge::PostGameLine(1, line);
        WorldRPG::Script::ActiveVmFirePlayerSpawn(playerId, team);  // CPacketHandler.cpp 1267-1273
        return true;
    }
    if (haveDimension) {
        WorldRPG::ServerWorld::NoteElementDimension(playerId, spawnDimension);
        WorldRPG::ServerWorld::NoteElementInterior(playerId,spawnInterior);
    }
    WorldRPG::Game::NoteRemoteSpawn(playerId);
    float heading = rotation;
    if (std::fabs(heading) > 6.5f) heading = heading * 3.14159265f / 180.f;
    WorldRPG::Game::NoteRemotePed(playerId, x, y, z, heading);
    WorldRPG::Script::ActiveVmFirePlayerSpawn(playerId, team);  // CPacketHandler.cpp 1267-1273
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

// Server -> client PLAYER_PURESYNC. CNetAPI::ReadPlayerPuresync (CNetAPI.cpp 830-1021).
// One player per packet. The server omits weapon type and total ammo from the relay.
bool HandlePlayerPureSync(const uint8_t* body, size_t length, size_t bits) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    uint8_t context = 0;
    uint16_t latency = 0;
    if (!ReadElementId(reader, id) || !reader.Read(context)) return false;
    if (!WorldRPG::ServerWorld::CanUpdateSync(id, context)) return true;
    if (!ReadCompressedU16(reader, latency)) return false;
    uint8_t keys = 0;
    if (!reader.Read(keys)) return false;
    // SFullKeysyncSync: the first 8 bits are the button flags (SyncStructures.h 1004-1012); the analog reads below
    // reuse `keys`, so the flags are kept here for the remote controller (SetControllerState, CNetAPI.cpp 1001).
    const uint8_t buttonBits = keys;
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
    // CNetAPI::ReadPlayerPuresync (CNetAPI.cpp 898-1004): health, armor, camera, then
    // SetMoveSpeed (already read), SetTargetPosition / SetTargetRotation, SetControllerState.
    // SPlayerHealthSync / SPlayerArmorSync are SFloatAsBitsSync<8> (SyncStructures.h 189-198).
    uint8_t healthRaw = 0, armorRaw = 0;
    uint8_t camRaw[2] = {};
    if (!reader.Read(healthRaw) || !reader.Read(armorRaw) || !reader.ReadBits(camRaw, 12)) return false;
    const float health = static_cast<float>(healthRaw);
    const float armor = static_cast<float>(armorRaw) * 0.5f;
    const unsigned camValue = camRaw[0] | ((camRaw[1] & 0x0Fu) << 8);
    const float camera = -pi + static_cast<float>(camValue) / 4095.f * (2.f * pi);

    // The relay carries a four-bit slot and clip only. Use the original
    // button byte to decide whether the aim vectors follow the arm angle.
    WorldRPG::PlayerWeaponSync::RemoteWeapon weapon;
    const bool hasWeapon = (flagBytes[1] & (WorldRPG::PlayerWeaponSync::kHasWeapon >> 8)) != 0;
    if (hasWeapon && !WorldRPG::PlayerWeaponSync::ReadRelay(reader, weapon,
            WorldRPG::PlayerWeaponSync::HasAimOrFire(buttonBits), false)) return false;
    if (weapon.slot >= WorldRPG::PedQueries::Slots) return false;
    weapon.aim.yaw = heading;
    weapon.aim.akimboUp = (flagBytes[0] & 0x80u) != 0;

    WorldRPG::Combat::RegisterVictimHealth(id, healthRaw, WorldRPG::ServerWorld::LocalPlayer());
    // SFullKeysyncSync stores the stick as short(char * 128/127) (SyncStructures.h 972-973).
    const int sx = static_cast<int>(static_cast<int8_t>(stickX)) * 128 / 127;
    const int sy = static_cast<int>(static_cast<int8_t>(stickY)) * 128 / 127;
    // ReadPlayerPuresync clears ShockButtonL and ducks from the flag (CNetAPI.cpp 986-1004).
    const bool ducked = (flagBytes[0] & 0x08) != 0;
    if (id != WorldRPG::ServerWorld::LocalPlayer()) {
        WorldRPG::ServerWorld::NotePlayerSeen(id);
        if (weapon.aim.full)
            WorldRPG::PedQueries::Aim(id, weapon.aim.origin.x, weapon.aim.origin.y, weapon.aim.origin.z);
        WorldRPG::Game::NoteRemotePlayerSync(id, x, y, z, heading, vx, vy, vz, hasVelocity,
            sx, sy, camera, buttonBits, ducked, weapon, true, health, true, armor);
        WorldRPG::Game::LockPedHealth(id, health);
        WorldRPG::Game::LockPedArmor(id, armor);
        WorldRPG::Game::SetPedCameraRotation(id, camera);
    }
    static bool loggedGaps = false;
    if (!loggedGaps) {
        loggedGaps = true;
        PKT_LOGI("DIAG ReadPlayerPuresync (CNetAPI.cpp 830-1021): applied controller, SetMoveSpeed, "
                 "health/armor at CPed +0x6AC/+0x6B4, target position and SetTargetRotation. "
                 "Not run: SetLatency, SetHasJetPack, "
                 "SetWearingGoggles, contact origin, SetChoking, SetOnFire, SetStealthAiming, "
                 "RunClimbingTask, RunSwimTask. "
                 "Duck is the flag PresentOnFoot passes to SetTaskDuckSecondary. "
                 "SetCameraRotation is stored; SwitchContext does not write TheCamera+0x154. "
                 "LockHealth is written each game tick from StreamedInPulse (CClientPed.cpp 4738-4748). "
                 "The dead-to-alive recreate in InternalSetHealth is logged, not run. "
                 "Native task block is absent at bitstream 0x0030.");
    }
    static int reports = 0;
    if (reports < 4) {
        ++reports;
        PKT_LOGI("PlayerSync: id=%u at %.0f %.0f %.0f stick %d %d", id, x, y, z, sx, sy);
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
    static const bool trace = WorldRPG::Debug::Flag("tracepedsync");
    if (trace) PKT_LOGI("DIAG PED_SYNC body len=%zu bits=%zu", length, bits);
    // Reference Packet_PedSync: while (GetNumberOfUnreadBits() > 32). No entry cap.
    while (reader.GetRemainingBits() > 32) {
        uint32_t id = 0;
        uint8_t ucSyncTimeContext = 0, ucFlags = 0, flags2 = 0;
        if (!ReadElementId(reader, id) || !reader.Read(ucSyncTimeContext) || !reader.Read(ucFlags) || !reader.Read(flags2)) {
            PKT_LOGI("DIAG PED_SYNC short header bit=%zu", reader.GetReadOffsetBits());
            break;
        }
        if (trace) PKT_LOGI("DIAG PED_SYNC id=%u ctx=%u flags=0x%02x flags2=0x%02x bit=%zu", id, ucSyncTimeContext, ucFlags, flags2, reader.GetReadOffsetBits());

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

        // CPedSync::Packet_PedSync applies an entry only when pPed->CanUpdateSync(ucSyncTimeContext) (CPedSync.cpp 376):
        // the contexts match or either is 0 (CClientEntity.cpp 180-185). This drops PED_SYNC written before a server
        // setElementPosition / setElementHealth, which bumped the context (LUA_ELEMENT_RPC, HandleLuaElementRPC).
        // Every field above is still read, so the next entry starts at the right bit.
        {
            const uint8_t local = WorldRPG::ServerWorld::SyncTimeContext(id);
            if (!(local == ucSyncTimeContext || ucSyncTimeContext == 0 || local == 0)) {
                static int stale = 0;
                if (stale < 6) {
                    ++stale;
                    PKT_LOGI("DIAG PED_SYNC id=%u context %u != element %u: entry dropped (CanUpdateSync)", id,
                             static_cast<unsigned>(ucSyncTimeContext), static_cast<unsigned>(local));
                }
                continue;
            }
        }
        if (std::strcmp(WorldRPG::ServerWorld::ElementKindName(id), "ped") != 0) continue;
        // CPedSync applies each field independently. A presentation-only packet also
        // needs to reach the ped, and an explicit zero velocity means it stopped.
        if (!PedSyncState::IsHosted(id)) {
            if (ucFlags & 0x07) {
                const float position[3] = {x, y, z};
                const float velocity[3] = {vx, vy, vz};
                WorldRPG::Game::NoteObservedPedSync(id, hasPosition ? position : nullptr,
                    (ucFlags & 0x02) ? &heading : nullptr, (ucFlags & 0x04) ? velocity : nullptr,
                    PedSyncState::SpatialSyncRateMs(id));
            }
            if (ucFlags & 0x08) WorldRPG::Game::LockPedHealth(id, fHealth);
            if (ucFlags & 0x10) WorldRPG::Game::LockPedArmor(id, fArmor);
            if (flags2 & 0x01) WorldRPG::Game::SetPedCameraRotation(id, cameraRotation);
            if (hasLocomotion) WorldRPG::Game::SetPedNativeTaskLocomotion(id, nativeTaskLocomotion.data.uiMode);
            if (hasWeapon) WorldRPG::Game::SetPedNativeTaskWeapon(id, nativeTaskWeaponPresentation.data.ucWeaponType);
            if (hasAnimation) WorldRPG::Game::SetPedNativeTaskAnimation(id, nativeTaskAnimationPresentation.data.usAnimGroup, nativeTaskAnimationPresentation.data.usAnimId);
            if (animationKilled) WorldRPG::Game::KillPedAnimation(id);
            if (ucFlags & 0x20) {
                WorldRPG::Game::SetPedOnFire(id, bOnFire);
                WorldRPG::PedQueries::OnFire(id, bOnFire);
            }
            if (ucFlags & 0x40) {
                WorldRPG::Game::SetPedInWater(id, bIsInWater);
                WorldRPG::PedQueries::InWater(id, bIsInWater);
            }
        }
        ++applied;
    }
    if (applied > 0) {
        static int reports = 0;
        if (reports < 4) {
            ++reports;
            PKT_LOGI("PedSync: accepted %d server ped updates", applied);
        }
    }
    return applied > 0;
}

float Angle12(unsigned raw) {
    constexpr float pi = 3.14159265f;
    return -pi + (static_cast<float>(raw & 0xFFFu) / 4095.f) * (2.f * pi);
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
    // SKeysyncFlags (SyncStructures.h 930-936): bit 0 ducked, bit 1 choking.
    const bool choking = (flag & 0x02u) != 0;
    const bool attack = WorldRPG::PlayerWeaponSync::HasAimOrFire(buttons);
    WorldRPG::PlayerWeaponSync::RemoteWeapon weapon;
    if (attack) {
        if (!WorldRPG::PlayerWeaponSync::ReadRelay(reader, weapon, true, true) ||
            weapon.slot >= WorldRPG::PedQueries::Slots) return false;
        if (weapon.aim.full)
            WorldRPG::PedQueries::Aim(id, weapon.aim.origin.x, weapon.aim.origin.y, weapon.aim.origin.z);
    }
    WorldRPG::PedQueries::Keys(id, ducked, choking);
    uint32_t vehicleId = 0;
    uint8_t seat = 0;
    weapon.aim.inVehicle = WorldRPG::RemoteVehicle::FindOccupant(id, vehicleId, seat);
    weapon.aim.akimboUp = (flag & 4u) != 0;
    weapon.aim.yaw = heading;
    WorldRPG::Game::NoteRemoteAim(id, heading, camera, sx, sy, buttons, ducked,
                                  attack && (weapon.aim.inVehicle || !(flag & 0x08u)) ? &weapon : nullptr);
    WorldRPG::Game::SetPedCameraRotation(id, camera);
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
    if (WorldRPG::Combat::WeaponUsesBulletSync(bullet.weapon))
        WorldRPG::Game::QueueRemoteBullet(bullet);
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

    // SetPosition, SetCurrentRotation, SetMoveSpeed (CPedSync.cpp 251-253).
    WorldRPG::Game::NoteRemotePed(id, x, y, z, fRotation, vx, vy, vz);
    WorldRPG::Game::RpcSetPosition(id, x, y, z);
    WorldRPG::Game::RpcSetRotation(id, fRotation);
    // UnlockHealth, UnlockArmor, then SetHealth, SetArmor (CPedSync.cpp 256-261). The values used to be dropped, so
    // the new owner kept the observer lock and reported the old health back in its first PED_SYNC.
    WorldRPG::Game::UnlockPedVitals(id);
    WorldRPG::Game::SetPedVitals(id, fHealth, fArmor);
    WorldRPG::Game::SetPedCameraRotation(id, cameraRotation);
    WorldRPG::Game::SetPedNativeTaskPhysicalTakeover(id, nativeTaskPhysicalTakeover.data.uiMode,
        nativeTaskPhysicalTakeover.data.usAnimGroup, nativeTaskPhysicalTakeover.data.usAnimId,
        nativeTaskPhysicalTakeover.data.fProgress);
    WorldRPG::Game::SetPedHost(id, true);
    // Reference CPedSync::AddPed: become syncer (SetSyncing true). No wander task is started here;
    // the ped keeps its own AI, exactly as the desktop client does.
    PedSyncState::BeginHosted(id);
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
    WorldRPG::Game::SetPedHost(id, false);
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
    // Desktop CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleStartSync (CUnoccupiedVehicleSync.cpp 104-158):
    // SetTargetPosition, SetTargetRotation, SetMoveSpeed, SetTurnSpeed, SetHealth, then AddVehicle. The values reach
    // the body in GameEngine.cpp ApplyCarSeed. They used to be dropped, so the first packet 56 for this car reported
    // the body's constructor health (1000) and an upright rotation back to the server.
    const float pos[3] = {x, y, z};
    const float rot[3] = {rx, ry, rz};
    const float move[3] = {vx, vy, vz};
    const float turn[3] = {tx, ty, tz};
    WorldRPG::Game::NoteVehicleStartSync(id, pos, rot, move, turn, health);
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
            if (WorldRPG::RemoteVehicle::ClearMatchingSeat(pedId, vehicleId, seat))
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
        if (action == 1 || action == 4 || action == 6)
            WorldRPG::Script::ActiveVmFireVehicleSeat(action == 1, pedId, vehicleId, seat);
        return true;
    }
    if (action == 9) {
        PKT_LOGI("VehInOut: recv attempt_failed veh=%u reason=%u (request rejected by server)", vehicleId,
                 static_cast<unsigned>(reason));
    } else {
        PKT_LOGI("VehInOut: recv %s veh=%u seat=%u door=%u", name, vehicleId, static_cast<unsigned>(seat),
                 static_cast<unsigned>(door));
    }
    if (action == 1 || action == 5) {
        WorldRPG::RemoteVehicle::SetOccupant(pedId, vehicleId, seat);
        if (action == 5) WorldRPG::Game::NoteRemoteSeat(pedId, vehicleId, seat);
    }
    if (action == 2 || action == 4 || action == 6) {
        const bool removed = WorldRPG::RemoteVehicle::ClearMatchingSeat(pedId, vehicleId, seat);
        uint32_t otherVehicle = 0;
        uint8_t otherSeat = 0;
        WorldRPG::Game::LocalCarState car;
        if (removed || (!WorldRPG::RemoteVehicle::FindOccupant(pedId, otherVehicle, otherSeat) &&
            WorldRPG::Game::CopyLocalCar(car) && car.vehicleId == vehicleId && car.seat == seat))
            WorldRPG::Game::NoteRemoteUnseat(pedId);
    }
    switch (action) {
        case 0:  // REQUEST_IN_CONFIRMED: the server allows the entry, so send NOTIFY_IN (1)
            if (WorldRPG::Net::SendVehicleInOutNotify(pedId, vehicleId, 1))
                WorldRPG::RemoteVehicle::SetOccupant(pedId, vehicleId, seat);
            break;
        case 3: {  // REQUEST_OUT_CONFIRMED
            uint32_t confirmedVehicle = 0;
            uint8_t confirmedSeat = 0;
            if (WorldRPG::RemoteVehicle::FindOccupant(pedId, confirmedVehicle, confirmedSeat) &&
                (confirmedVehicle != vehicleId || confirmedSeat != seat))
                WorldRPG::Net::ClearVehicleInOutPending();
            else WorldRPG::Net::SendVehicleInOutNotify(pedId, vehicleId, 4);
            break;
        }
        case 7:  // REQUEST_JACK_CONFIRMED: jacking is not ported (no animation), so abort it like desktop does
            WorldRPG::Net::ClearVehicleInOutPending();
            WorldRPG::Net::SendVehicleJackAbort(pedId, vehicleId, door);
            PKT_LOGI("VehInOut: jack not ported; sent notify_jack_abort for veh=%u, no packet 33", vehicleId);
            break;
        case 9: {  // CPacketHandler::Packet_Vehicle_InOut restores the last confirmed seat on a denied exit.
            uint32_t confirmedVehicle = 0;
            uint8_t confirmedSeat = 0;
            WorldRPG::Game::LocalCarState car;
            if (WorldRPG::RemoteVehicle::FindOccupant(pedId, confirmedVehicle, confirmedSeat))
                WorldRPG::Game::NoteRemoteSeat(pedId, confirmedVehicle, confirmedSeat);
            else if (WorldRPG::Game::CopyLocalCar(car) && car.vehicleId == vehicleId)
                WorldRPG::Game::NoteRemoteUnseat(pedId);
            WorldRPG::Net::ClearVehicleInOutPending();
            break;
        }
        case 1:  // NOTIFY_IN_RETURN
        case 2:  // NOTIFY_IN_ABORT_RETURN
        case 4:  // NOTIFY_OUT_RETURN
        case 5:  // NOTIFY_OUT_ABORT_RETURN
        case 6:  // NOTIFY_FELL_OFF_RETURN
            WorldRPG::Net::ClearVehicleInOutPending();
            break;
        default:
            break;
    }
    if (action == 1 || action == 4 || action == 6)
        WorldRPG::Script::ActiveVmFireVehicleSeat(action == 1, pedId, vehicleId, seat);
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
    if (!WorldRPG::ServerWorld::CanUpdateSync(playerId, sync.timeContext)) return true;
    // ReadVehiclePuresync (CNetAPI.cpp 1514-1572): switch the occupied ped
    // to the relayed slot, including the total and clip for a gang drive-by.
    WorldRPG::PlayerWeaponSync::RemoteWeapon weapon;
    if (sync.hasWeapon && sync.weapon.slot < WorldRPG::PedQueries::Slots) {
        weapon.slot = sync.weapon.slot;
        weapon.ammoSynced = sync.weapon.ammoSynced;
        weapon.total = sync.weapon.totalAmmo;
        weapon.clip = sync.weapon.ammoInClip;
        weapon.aim = {sync.weapon.armRaw,
            {sync.weapon.aimOrigin.x, sync.weapon.aimOrigin.y, sync.weapon.aimOrigin.z},
            {sync.weapon.aimDirection.x, sync.weapon.aimDirection.y, sync.weapon.aimDirection.z}, sync.weapon.aimFull};
    }
    weapon.aim.inVehicle = true;
    weapon.aim.drivebyDirection = sync.weapon.drivebyDirection;
    const uint8_t buttons = (sync.keys.buttonCircle ? WorldRPG::PlayerWeaponSync::kFire : 0)
                          | (sync.keys.rightShoulder1 ? WorldRPG::PlayerWeaponSync::kAim : 0);
    if (playerId != WorldRPG::ServerWorld::LocalPlayer()) {
        WorldRPG::Game::NoteVehicleCombat(playerId, weapon, buttons, sync.flags.gangDriveby);
        WorldRPG::Game::SetPedVitals(playerId, sync.playerHealth, sync.playerArmor);
        WorldRPG::Game::LockPedHealth(playerId, sync.playerHealth);
        WorldRPG::Game::LockPedArmor(playerId, sync.playerArmor);
        WorldRPG::Combat::RegisterVictimHealth(playerId, static_cast<uint8_t>(sync.playerHealth), WorldRPG::ServerWorld::LocalPlayer());
        if (weapon.aim.full)
            WorldRPG::PedQueries::Aim(playerId, weapon.aim.origin.x, weapon.aim.origin.y, weapon.aim.origin.z);
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

// Loading-screen line for one resource start. Shared by the full descriptor path and the name-only fallback.
bool ShowResourceProgress(const std::string& name) {
    static int resourceCount = 0;
    ++resourceCount;
    // No progress line: desktop shows no box for a resource start. The transfer box follows the
    // real HTTP downloads (HttpDownloader::GetTransfer, posted from nativePulse).
    PKT_LOGI("Recurso %s (%d)", name.c_str(), resourceCount);
    return true;
}

// RESOURCE_START (82): parse the full descriptor (ResourceFiles.cpp, desktop
// Packet_ResourceStart layout or the mobile-shim layout) and queue the client
// files with auto-download for the HTTP cache (HttpDownloader.cpp).
bool HandleResourceStart(const uint8_t* body, size_t length, size_t bits) {
    // Desktop CPacketHandler::Packet_ResourceStart (CPacketHandler.cpp:5255).
    WorldRPG::ConnectionLoading::Update(WorldRPG::ConnectionLoading::Phase::Process, "Processing resources");
    WorldRPG::Net::ResourceStartInfo info;
    std::string error;
    if (WorldRPG::Net::ParseResourceStart(body, length, bits, info, error)) {
        // md5 is only meaningful when the packet lists files; a zero-file packet does not show the field either way.
        PKT_LOGI("Resource %s id=%u noCache=%u: %zu files (%zu client files, %zu auto-download, %llu bytes) layout=%s md5=%s", info.name.c_str(),
                 static_cast<unsigned>(info.resourceId), static_cast<unsigned>(info.noClientCacheScripts),
                 info.files.size(), info.ClientFileCount(), info.AutoDownloadCount(),
                 static_cast<unsigned long long>(info.AutoDownloadBytes()),
                 info.layout == WorldRPG::Net::ResourceStartLayout::Desktop ? "desktop" : "mobile-shim",
                 info.files.empty() ? "n/a" : (info.md5Present ? "yes" : "no"));
        WorldRPG::ServerWorld::OnResourceStart(info);
        WorldRPG::Script::ActiveVmOnResourceStart(info);
        return ShowResourceProgress(info.name);
    }

    PKT_LOGI("RESOURCE_START descriptor not parsed: %s", error.c_str());
    // Name-only fallback, the behaviour before the descriptor parser existed.
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    uint8_t nameLength = 0;
    if (!reader.Read(nameLength) || nameLength == 0 || nameLength > 64) return false;
    std::vector<uint8_t> raw(nameLength);
    if (!reader.ReadBits(raw.data(), nameLength * 8)) return false;
    std::string name(raw.begin(), raw.end());
    name = WorldRPG::Net::NormalizeAscii(name);
    if (name.empty()) return false;
    return ShowResourceProgress(name);
}
} // namespace

namespace WorldRPG::Sync {

struct PacketHandler::Impl {
    SyncManager* syncManager = nullptr;
};

PacketHandler::PacketHandler() : pImpl_(std::make_unique<Impl>()) {}
PacketHandler::~PacketHandler() = default;

bool PacketHandler::initialize(SyncManager& syncManager) {
    pImpl_->syncManager = &syncManager;
    PKT_LOGI("PacketHandler: initialized");
    return true;
}

void PacketHandler::registerAll(Net::RakNetWrapper& network) {
    if (!pImpl_->syncManager) {
        PKT_LOGE("PacketHandler::registerAll: not initialized");
        return;
    }
    g_packetNetwork = &network;

    // Set the network packet handler callback to route user packets through us.
    // All game packets are dispatched by their real ePacketID in handleGamePacket.
    network.SetPacketHandler([this](uint8_t packetId, const uint8_t* data, size_t length, size_t bitLength) -> bool {
        return handlePacket(packetId, data, length, bitLength);
    });

    PKT_LOGI("PacketHandler: network packet callback registered");
}

bool PacketHandler::handleGamePacket(uint8_t gameId, const uint8_t* body, size_t bodyLength, size_t bodyBits) {
        // ROADMAP_PEDS_SYNC 0.1: which ped-relevant packets reach the 0x0030 peer, and with which RPC function ids.
        // Counting ePacketID (Packets.h) gives PED_WASTED 64, LUA 76, LUA_ELEMENT_RPC 77, PED_TASK 101; the phone
        // routes Lua RPCs on 68. Log only, no dispatch change.
        if (gameId == 64 || gameId == 68 || gameId == 76 || gameId == 77 || gameId == 101) {
            static int seen[256] = {};
            if (seen[gameId] < 8) {
                ++seen[gameId];
                PKT_LOGI("DIAG ped-roadmap packet id=%u len=%zu bits=%zu first=%u", gameId, bodyLength, bodyBits,
                         bodyLength > 0 ? static_cast<unsigned>(body[0]) : 0u);
            }
        }
        // Real ePacketID values from Shared/sdk/net/Packets.h.
        if (gameId == 43) return HandleChatEcho(body, bodyLength, bodyBits);
        if (gameId == 31) return HandleKeySync(body, bodyLength, bodyBits);
        if (gameId == 32) return HandlePlayerPureSync(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::Lightsync) return CorePackets::HandleLightSync(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::VehicleResync) return CorePackets::HandleVehicleResync(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::Explosion) return CorePackets::HandleExplosion(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::Fire) return CorePackets::HandleFire(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::Projectile) return CorePackets::HandleProjectile(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::PickupHideShow) return CorePackets::HandlePickupHideShow(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::PickupHitConfirm) return CorePackets::HandlePickupHitConfirm(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::VehicleSpawn) return CorePackets::HandleVehicleSpawn(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::VehicleDamageSync) return CorePackets::HandleVehicleDamageSync(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::VehicleTrailer) return CorePackets::HandleVehicleTrailer(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::ObjectStartSync) return CorePackets::HandleObjectStartSync(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::ObjectStopSync) return CorePackets::HandleObjectStopSync(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::ObjectSync) return CorePackets::HandleObjectSync(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::LatentTransfer) {
            return CorePackets::HandleLatentTransfer(
                body, bodyLength, bodyBits,
                [this](uint8_t innerId, const uint8_t* innerBody, size_t innerLen, size_t innerBits) {
                    return handleGamePacket(innerId, innerBody, innerLen, innerBits);
                });
        }
        if (gameId == MtaIds::Packet::PedTask) return CorePackets::HandlePedTask(body, bodyLength, bodyBits);
        if (gameId == MtaIds::Packet::PlayerNetworkStatus) {
            Net::BitStreamReader reader(body, bodyLength, bodyBits);
            uint8_t type = 0;
            uint32_t ticks = 0;
            if (!reader.Read(type) || !reader.Read(ticks)) return false;
            if (const auto local = ServerWorld::LocalPlayer()) {
                Combat::SetLocalNetworkInterrupted(type == 0);
                Script::ActiveVmFirePlayerNetworkStatus(local, type, ticks);
            }
            return true;
        }
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
        if (gameId == 5) {
            // PACKET_ID_PLAYER_QUIT. Desktop Packet_PlayerQuit: ElementID, SQuitReasonSync, then QuitPlayer.
            // CPlayerQuitPacket::Write is ElementID + SQuitReasonSync, but on the mobile peer the first 17 bits decoded
            // to id 2 while the players were 677 and 2630 (device 2026-11-14), like PLAYER_LIST, which carries an
            // extra prefix there. The bytes are logged, and the first byte offset whose id is a known player is used.
            {
                static int dumps = 0;
                if (dumps < 4) {
                    ++dumps;
                    char hex[33] = {};
                    for (size_t i = 0; i < bodyLength && i < 16; ++i) std::snprintf(hex + i * 2, 3, "%02x", static_cast<unsigned>(body[i]));
                    PKT_LOGI("DIAG PLAYER_QUIT body bytes=%zu bits=%zu %s", bodyLength, bodyBits, hex);
                }
            }
            for (size_t offset = 0; offset <= 4 && offset * 8 + 17 <= bodyBits; ++offset) {
                WorldRPG::Net::BitStreamReader reader(body + offset, bodyLength - offset, bodyBits - offset * 8);
                uint32_t quitId = 0;
                if (!ReadElementId(reader, quitId)) break;
                if (WorldRPG::ServerWorld::IsKnownPlayer(quitId)) {
                    if (offset) PKT_LOGI("DIAG PLAYER_QUIT id=%u found at byte %zu", quitId, offset);
                    // SQuitReasonSync (3 bits) -> CClientGame::QuitPlayer's reason text (CClientGame.cpp 3484-3505).
                    static const char* const kReasons[] = {"Quit", "Kicked", "Banned", "Bad Connection", "Timed Out"};
                    uint8_t reason = 0xFF;
                    const char* text = "Unknown";
                    if (reader.ReadBits(&reason, 3) && (reason & 7u) < 5) text = kReasons[reason & 7u];
                    WorldRPG::ServerWorld::OnPlayerQuit(quitId, text);
                    return true;
                }
            }
            PKT_LOGI("DIAG PLAYER_QUIT: no known player id in the packet");
            return true;
        }
        if (gameId == 30) return HandlePlayerClothes(body, bodyLength, bodyBits);  // PACKET_ID_PLAYER_CLOTHES
        if (gameId == WorldRPG::MtaIds::Packet::PlayerWasted || gameId == WorldRPG::MtaIds::Packet::PedWasted)
            return HandleWasted(body, bodyLength, bodyBits, gameId);
        if (gameId == 25) { WorldRPG::ServerWorld::OnPlayerList(body, bodyLength, bodyBits); return true; }
        if (gameId == 50) { WorldRPG::ServerWorld::OnEntityAdd(body, bodyLength, bodyBits); return true; }
        if (gameId == 51) { WorldRPG::ServerWorld::OnEntityRemove(body, bodyLength, bodyBits); return true; }
        if (gameId == 84) { WorldRPG::ServerWorld::OnCustomData(body, bodyLength, bodyBits); return true; }
        if (gameId == WorldRPG::MtaIds::Packet::Lua) return HandleLuaRPC(body, bodyLength, bodyBits);
        if (gameId == WorldRPG::MtaIds::Packet::LuaElementRpc) return HandleLuaElementRPC(body, bodyLength, bodyBits);
        if (gameId == 82) return HandleResourceStart(body, bodyLength, bodyBits);
        if (gameId == 83) {
            WorldRPG::Script::ActiveVmOnResourceStop(body, bodyLength, bodyBits);
            return true;
        }
        if (gameId == 95) {
            // Scripts run later, from ActiveVmPulseResources, once the resource is ready.
            WorldRPG::Script::ActiveVmFeedScripts(body, bodyLength, bodyBits);
            return true;
        }
        if (gameId != 32 && gameId != 33 && gameId != 56 && gameId != 63 && gameId != 77)
            PKT_LOGI("PacketHandler: unhandled game packet ID %u (%zu bytes)", gameId, bodyLength);
        return false;
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
        return handleGamePacket(gameId, body, bodyLength, bodyBits);
    }

    return false;
}

} // namespace WorldRPG::Sync
