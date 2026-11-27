// Host checks for P0-3 core gameplay packets and ENTITY_ADD fixes:
//   - ENTITY_ADD WORLD_MESH (12), SCRIPTFILE (16), BUILDING (23)
//   - Removal of dead provisional handler map (0..7)
//   - LIGHTSYNC (34), VEHICLE_RESYNC (35), EXPLOSION (37), FIRE (38), PROJECTILE (39)
//   - PICKUP_HIDESHOW (52), PICKUP_HIT_CONFIRM (53)
//   - VEHICLE_SPAWN (57), VEHICLE_DAMAGE_SYNC (59), VEHICLE_TRAILER (60)
//   - OBJECT_STARTSYNC (86), OBJECT_STOPSYNC (87), OBJECT_SYNC (88)
//   - LATENT_TRANSFER (96), PED_TASK (101)

#include "game_script.h"
#include "gtasa/ProjectileEngine.h"
#include "gtasa/RemoteVehicle.h"
#include "net/raknet/RakNetWrapper.h"
#include "shared/CorePackets.h"
#include "shared/LuaApiExtra.h"
#include "shared/LuaVMStub.h"
#include "shared/LuaWorldObjectApi.h"
#include "shared/MtaIds.h"
#include "shared/PacketHandler.h"
#include "shared/ServerRpc.h"
#include "shared/ServerWorld.h"
#include "shared/entity/EntityAddTypes.h"

#include "lua.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace WorldRPG;
namespace PktId = WorldRPG::MtaIds::Packet;

namespace {

unsigned checks = 0;

void Check(bool yes, const char* label) {
    ++checks;
    if (!yes) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

bool Near(double a, double b, double eps = 1e-2) { return std::fabs(a - b) <= eps; }

struct Pkt {
    Net::BitStreamWriter w;
    explicit Pkt(uint8_t packetId) { w.Write(packetId); }
    Pkt() = default;

    Pkt& U8(uint8_t v) { w.Write(v); return *this; }
    Pkt& U16(uint16_t v) { w.Write(v); return *this; }
    Pkt& U32(uint32_t v) { w.Write(v); return *this; }
    Pkt& F(float v) { w.Write(v); return *this; }
    Pkt& Bit(bool v) {
        const uint8_t b = v ? 1u : 0u;
        w.WriteBits(&b, 1);
        return *this;
    }
    Pkt& Bits(uint32_t v, unsigned count) {
        uint8_t bytes[4] = {};
        std::memcpy(bytes, &v, 4);
        w.WriteBits(bytes, count);
        return *this;
    }
    Pkt& Id(uint32_t id) { return Bits(id, 17); }
    Pkt& Compressed(uint32_t value, int bytes) {
        for (int i = bytes - 1; i > 0; --i) {
            const uint32_t mask = 0xFFu << (8 * i);
            if ((value & mask) != 0) {
                Bit(false);
                uint8_t raw[4] = {};
                for (int b = 0; b <= i; ++b) raw[b] = static_cast<uint8_t>((value >> (8 * b)) & 0xFFu);
                w.WriteBits(raw, static_cast<size_t>((i + 1) * 8));
                return *this;
            }
            Bit(true);
        }
        if ((value & 0xF0u) != 0) {
            Bit(false);
            U8(static_cast<uint8_t>(value & 0xFFu));
        } else {
            Bit(true);
            Bits(value & 0x0Fu, 4);
        }
        return *this;
    }
    Pkt& Pos(float x, float y, float z) {
        auto write24 = [&](float v) {
            int32_t raw = static_cast<int32_t>(std::lround(v * 1024.0f));
            uint8_t b[3] = {static_cast<uint8_t>(raw & 0xFF), static_cast<uint8_t>((raw >> 8) & 0xFF),
                            static_cast<uint8_t>((raw >> 16) & 0xFF)};
            w.WriteBits(b, 24);
        };
        write24(x);
        write24(y);
        F(z);
        return *this;
    }
    Pkt& LowPos(float x, float y, float z) {
        const uint16_t ux = static_cast<uint16_t>(((x + 8192.0f) / 16384.0f) * 65535.0f);
        const uint16_t uy = static_cast<uint16_t>(((y + 8192.0f) / 16384.0f) * 65535.0f);
        const uint16_t uz = static_cast<uint16_t>(std::lround(z + 110.0f));
        U16(ux);
        U16(uy);
        Bits(uz, 11);
        return *this;
    }
    Pkt& RotDeg(float rx, float ry, float rz) {
        U16(static_cast<uint16_t>(rx * (65536.f / 360.f)));
        U16(static_cast<uint16_t>(ry * (65536.f / 360.f)));
        U16(static_cast<uint16_t>(rz * (65536.f / 360.f)));
        return *this;
    }
    Pkt& RotRad(float rx, float ry, float rz) {
        constexpr float kInv = 65536.f / 6.283185307f;
        U16(static_cast<uint16_t>(rx * kInv));
        U16(static_cast<uint16_t>(ry * kInv));
        U16(static_cast<uint16_t>(rz * kInv));
        return *this;
    }
    Pkt& FloatBits(float v, unsigned bits, float minV, float maxV) {
        const float alpha = (v - minV) / (maxV - minV);
        const uint32_t raw = static_cast<uint32_t>(std::lround(alpha * static_cast<float>((1ull << bits) - 1)));
        return Bits(raw, bits);
    }
    Pkt& Float7_17(float v) {
        int32_t raw = static_cast<int32_t>(std::lround(static_cast<double>(v) * static_cast<double>(1 << 17)));
        uint8_t b[3] = {static_cast<uint8_t>(raw & 0xFF), static_cast<uint8_t>((raw >> 8) & 0xFF),
                        static_cast<uint8_t>((raw >> 16) & 0xFF)};
        w.WriteBits(b, 24);
        return *this;
    }
    Pkt& ZeroVel() { return Bit(false); }
    Pkt& Vel(float x, float y, float z) {
        const float mod = std::sqrt(x * x + y * y + z * z);
        if (mod < 1e-6f) return Bit(false);
        Bit(true);
        F(mod);
        const float nx = x / mod, ny = y / mod, nz = z / mod;
        // NetBitStream::WriteNormVector: x sign bit (true when x < 0), y zero flag + 16b, z zero flag + 16b.
        Bit(nx < 0.0f);
        auto writeComp = [&](float c) {
            if (std::fabs(c) < 1e-5f) {
                Bit(true);
            } else {
                Bit(false);
                const uint16_t q = static_cast<uint16_t>(std::lround((c + 1.0f) * 32767.5f));
                U16(q);
            }
        };
        writeComp(ny);
        writeComp(nz);
        return *this;
    }
    Pkt& AlignByte() {
        const size_t rem = w.GetBitLength() & 7u;
        if (rem != 0) Bits(0, static_cast<unsigned>(8u - rem));
        return *this;
    }
    Pkt& Bytes(const uint8_t* data, size_t len) {
        if (len != 0) w.WriteBits(data, len * 8u);
        return *this;
    }
};

// Writes an ENTITY_ADD common header (baseline 0x0030 layout: extendedHeader=false).
void WriteEntityCommon(Pkt& p, uint32_t id, uint8_t type, uint32_t parent = 0xFFFFFFFFu, uint8_t interior = 0,
                       uint16_t dim = 0, bool collisions = true) {
    p.Id(id).U8(type).Id(parent).U8(interior).Compressed(dim, 2).Bit(false).Bit(collisions);
    p.Compressed(0, 2);  // customData count = 0
    p.Compressed(0, 2);  // name length = 0
}

void AddServerObject(Sync::PacketHandler& h, uint32_t id, uint16_t model, float x, float y, float z) {
    Pkt p(PktId::EntityAdd);
    p.Compressed(1, 4);
    WriteEntityCommon(p, id, Entity::kTypeObject);
    p.Pos(x, y, z).RotRad(0.f, 0.f, 0.f).Compressed(model, 2);
    p.Bit(false).Bit(false).Bit(false);  // lowLod, doubleSided, breakable
    p.Bit(false);                        // visibleInAllDimensions
    p.Bit(true);                         // uniform scale
    p.F(1.0f);
    p.Bit(false);  // frozen
    p.FloatBits(1000.f, 11, 0.f, 1023.5f);
    p.Bit(false);  // moving
    p.Compressed(0, 1);  // alpha (255 - 0)
    Check(h.handlePacket(99, p.w.GetData(), p.w.GetByteLength(), p.w.GetBitLength()), "ENTITY_ADD object");
}

void AddServerPickup(Sync::PacketHandler& h, uint32_t id, uint16_t model, bool visible, float x, float y, float z) {
    Pkt p(PktId::EntityAdd);
    p.Compressed(1, 4);
    WriteEntityCommon(p, id, Entity::kTypePickup);
    p.Pos(x, y, z).Compressed(model, 2).Bit(visible).Bits(0, 3).U8(100);  // health pickup
    Check(h.handlePacket(99, p.w.GetData(), p.w.GetByteLength(), p.w.GetBitLength()), "ENTITY_ADD pickup");
}

struct Fixture {
    Sync::PacketHandler handler;
    Script::LuaVMStub vm;

    Fixture() {
        ServerWorld::ResetSession();
        RemoteVehicle::ResetSession();
        Projectiles::Reset();
        Headless::ResetProbe();
        ServerWorld::ConfigureEntityAdd(true, false);
        Check(vm.initialize(), "VM initializes");
    }
    ~Fixture() { vm.shutdown(); }

    void Pump() { vm.pumpDx(false); }

    void Lua(const std::string& script) {
        auto r = vm.executeBuffer(script.data(), script.size(), "core-packets-test");
        if (!r.success) std::fprintf(stderr, "Lua error: %s\n", r.errorMessage.c_str());
        Check(r.success, "Lua script ran cleanly");
    }

    bool Send(const Pkt& p) {
        return handler.handlePacket(99, p.w.GetData(), p.w.GetByteLength(), p.w.GetBitLength());
    }
};

void TestEntityAddFixes() {
    Fixture f;
    // Batch with WORLD_MESH (12), SCRIPTFILE (16), BUILDING (23), then a trailing PICKUP (6) to verify exact bit alignment.
    Pkt p(PktId::EntityAdd);
    p.Compressed(4, 4);
    WriteEntityCommon(p, 1001, Entity::kTypeWorldMesh);
    WriteEntityCommon(p, 1002, Entity::kTypeScriptFile);
    WriteEntityCommon(p, 1003, Entity::kTypeBuilding);
    p.Pos(123.5f, -456.25f, 14.5f).RotRad(0.25f, 0.5f, 1.5f).Compressed(3456, 2).Id(777);
    WriteEntityCommon(p, 1004, Entity::kTypePickup);
    p.Pos(10.f, 20.f, 30.f).Compressed(1240, 2).Bit(true).Bits(3, 3);  // custom pickup
    Check(f.Send(p), "ENTITY_ADD batch with worldmesh/scriptfile/building/pickup");

    Check(std::strcmp(ServerWorld::ElementKindName(1002), "scriptfile") == 0, "scriptfile kind");
    Check(std::strcmp(ServerWorld::ElementKindName(1003), "building") == 0, "building kind");
    Check(ServerWorld::ModelOf(1003) == 3456, "building model stored");
    float x = 0, y = 0, z = 0;
    Check(ServerWorld::ElementPose(1003, x, y, z) && Near(x, 123.5) && Near(y, -456.25) && Near(z, 14.5),
          "building position stored");
    uint32_t lowLod = 0;
    Check(ServerWorld::CopyBuildingLowLod(1003, lowLod) && lowLod == 777, "building LowLodObjectID stored");
    // Trailing pickup proves bit alignment after BUILDING and no-op types.
    Check(std::strcmp(ServerWorld::ElementKindName(1004), "pickup") == 0, "trailing pickup parsed");
    Check(ServerWorld::ModelOf(1004) == 1240, "trailing pickup model aligned");

    // Truncated BUILDING must fail cleanly without storing a half-parsed element.
    Pkt bad(PktId::EntityAdd);
    bad.Compressed(1, 4);
    WriteEntityCommon(bad, 1005, Entity::kTypeBuilding);
    bad.Pos(1.f, 2.f, 3.f);  // missing rotation, model, lowLodId
    f.Send(bad);
    Check(ServerWorld::ModelOf(1005) == -1, "truncated building not stored");
}

void TestDeadProvisionalHandlersRemoved() {
    Fixture f;
    // Provisional ids 0..4, 6..7 must no longer dispatch to dead stubs.
    for (uint8_t id : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4), uint8_t(6), uint8_t(7)}) {
        Pkt p(id);
        p.U8(1).U16(99).U8(0);
        Check(!f.Send(p), "dead provisional packet id rejected as unhandled");
    }
}

void TestLightSync() {
    Fixture f;
    ServerWorld::SetLocalPlayer(10);
    ServerWorld::NotePlayerSkin(20, 7);
    ServerWorld::NotePlayerSkin(21, 9);
    ServerWorld::NotePlayerSkin(22, 11);
    ServerWorld::SetSyncTimeContext(22, 5);
    ServerWorld::NoteVehicle(300, 411);
    RemoteVehicle::SetOccupant(21, 300, 0);  // driver

    Pkt p(PktId::Lightsync);
    // Player 20: on foot, health+armor+pos.
    p.Id(20).U8(0).Compressed(42, 2).Bit(true).FloatBits(80.f, 8, 0.f, 255.f).FloatBits(50.f, 8, 0.f, 127.5f)
        .Bit(true).LowPos(100.f, -200.f, 25.f).Bit(false);
    // Player 22: stale syncTimeContext (4 != 5), must be skipped without breaking alignment.
    p.Id(22).U8(4).Compressed(10, 2).Bit(false).Bit(true).LowPos(999.f, 999.f, 50.f).Bit(false);
    // Unknown player 9999: must be skipped without breaking alignment.
    p.Id(9999).U8(0).Compressed(10, 2).Bit(true).FloatBits(50.f, 8, 0.f, 255.f).FloatBits(0.f, 8, 0.f, 127.5f).Bit(false);
    // Player 21: driver of vehicle 300, pos + vehicleHealth.
    p.Id(21).U8(0).Compressed(15, 2).Bit(false).Bit(true).LowPos(-50.f, 75.f, 12.f).Bit(true)
        .FloatBits(640.f, 8, 0.f, 2040.f);
    Check(f.Send(p), "LIGHTSYNC multi-player decoded");

    float x = 0, y = 0, z = 0;
    Check(ServerWorld::ElementPose(20, x, y, z) && Near(x, 100.0, 1.0) && Near(y, -200.0, 1.0) && Near(z, 25.0, 1.5),
          "LIGHTSYNC updated on-foot player pos");
    Check(ServerWorld::ElementPose(22, x, y, z) && Near(x, 0.0) && Near(y, 0.0),
          "LIGHTSYNC ignored stale sync context");
    ServerWorld::ActorSpawn veh;
    Check(ServerWorld::CopyVisual(300, veh) && Near(veh.x, -50.0, 1.0) && Near(veh.y, 75.0, 1.0) &&
              Near(veh.health, 640.0, 8.0),
          "LIGHTSYNC updated driven vehicle pos and health");

    // Truncated packet returns false.
    Pkt trunc(PktId::Lightsync);
    trunc.Id(20).U8(0);
    Check(!f.Send(trunc), "LIGHTSYNC truncated rejected");
}

void TestVehicleResyncAndDamageSync() {
    Fixture f;
    ServerWorld::NoteVehicle(400, 411);

    // VEHICLE_RESYNC (35) with MethodeB: doors non-zero, wheels all-zero (bit=0), panels non-zero, lights unsynced.
    Pkt resync(PktId::VehicleResync);
    resync.Id(400)
        .Pos(50.5f, -30.25f, 12.5f)
        .RotDeg(10.f, 20.f, 90.f)
        .Vel(1.f, 2.f, 0.f)
        .ZeroVel()
        .FloatBits(850.5f, 12, 0.f, 2047.5f)
        .Bits(0x07u, 4);  // doors + wheels + panels synced, lights not synced
    // Doors: nonZero = true -> 6 x 3 bits
    resync.Bit(true).Bits(1, 3).Bits(2, 3).Bits(3, 3).Bits(4, 3).Bits(0, 3).Bits(2, 3);
    // Wheels: nonZero = false -> all zeros
    resync.Bit(false);
    // Panels: nonZero = true -> 7 x 2 bits
    resync.Bit(true).Bits(1, 2).Bits(2, 2).Bits(3, 2).Bits(0, 2).Bits(1, 2).Bits(2, 2).Bits(3, 2);
    Check(f.Send(resync), "VEHICLE_RESYNC decoded");

    ServerWorld::ActorSpawn v;
    Check(ServerWorld::CopyVisual(400, v) && Near(v.x, 50.5) && Near(v.y, -30.25) && Near(v.z, 12.5) &&
              Near(v.rotX, 10.0, 0.1) && Near(v.rotY, 20.0, 0.1) &&
              Near(v.heading, 90.0 * (3.14159265 / 180.0), 0.01) && Near(v.health, 850.5, 0.6),
          "VEHICLE_RESYNC updated pose and health");
    f.Lua("local v = {id=400, element=true}\n"
          "local rx, ry, rz = getElementRotation(v)\n"
          "assert(math.abs(rx - 10) < 0.1 and math.abs(ry - 20) < 0.1 and math.abs(rz - 90) < 0.1)\n"
          "assert(getVehicleDoorState(v, 0) == 1 and getVehicleDoorState(v, 3) == 4)\n"
          "local fl, rl, fr, rr = getVehicleWheelStates(v)\n"
          "assert(fl == 0 and rl == 0 and fr == 0 and rr == 0)\n"
          "assert(getVehiclePanelState(v, 0) == 1 and getVehiclePanelState(v, 6) == 3)\n"
          "assert(getVehicleLightState(v, 0) == 0)");

    // VEHICLE_DAMAGE_SYNC (59): delta sync updates only door 0 -> 2, wheel 1 -> 1, light 2 -> 1.
    Pkt dmg(PktId::VehicleDamageSync);
    dmg.Id(400);
    // Doors (6): only index 0 changed to 2
    dmg.Bit(true).Bits(2, 3).Bit(false).Bit(false).Bit(false).Bit(false).Bit(false);
    // Wheels (4): only index 1 changed to 1
    dmg.Bit(false).Bit(true).Bits(1, 2).Bit(false).Bit(false);
    // Panels (7): none changed
    for (int i = 0; i < 7; ++i) dmg.Bit(false);
    // Lights (4): only index 2 changed to 1
    dmg.Bit(false).Bit(false).Bit(true).Bits(1, 2).Bit(false);
    Check(f.Send(dmg), "VEHICLE_DAMAGE_SYNC decoded");

    f.Lua("local v = {id=400, element=true}\n"
          "assert(getVehicleDoorState(v, 0) == 2 and getVehicleDoorState(v, 3) == 4)\n"
          "local fl, rl, fr, rr = getVehicleWheelStates(v)\n"
          "assert(fl == 0 and rl == 1 and fr == 0 and rr == 0)\n"
          "assert(getVehiclePanelState(v, 6) == 3)\n"
          "assert(getVehicleLightState(v, 2) == 1 and getVehicleLightState(v, 0) == 0)");

    // NaN z in VEHICLE_RESYNC rejected.
    Pkt nanResync(PktId::VehicleResync);
    nanResync.Id(400).Pos(0.f, 0.f, std::numeric_limits<float>::quiet_NaN());
    Check(!f.Send(nanResync), "VEHICLE_RESYNC rejects NaN");
}

void TestExplosionFireProjectile() {
    Fixture f;
    ServerWorld::SetLocalPlayer(10);
    ServerWorld::NotePlayerSkin(20, 7);
    ServerWorld::NoteElementPosition(20, 10.f, 20.f, 30.f);
    ServerWorld::NoteVehicle(500, 411);
    ServerWorld::NoteElementPosition(500, 100.f, 200.f, 10.f);

    f.Lua("expLog = {}\n"
          "vehExpLog = {}\n"
          "projLog = {}\n"
          "addEventHandler('onClientExplosion', root, function(x, y, z, t)\n"
          "  expLog[#expLog + 1] = {source.id, x, y, z, t}\n"
          "end)\n"
          "addEventHandler('onClientVehicleExplode', root, function(withExp)\n"
          "  vehExpLog[#vehExpLog + 1] = {source.id, withExp}\n"
          "end)\n"
          "addEventHandler('onClientProjectileCreation', root, function(creator)\n"
          "  projLog[#projLog + 1] = {source.id, creator and creator.id or 0}\n"
          "end)");

    // 1. Explosion with creator 20 and origin 20 (offset added to 10, 20, 30 -> 15, 25, 32).
    Pkt exp1(PktId::Explosion);
    exp1.Bit(true).Id(20).Compressed(30, 2)
        .Bit(true).Id(20).Bit(false)
        .Pos(5.f, 5.f, 2.f)
        .Bits(0, 4);  // EXP_TYPE_GRENADE
    Check(f.Send(exp1), "EXPLOSION grenade decoded");

    // 2. Vehicle explosion (EXP_TYPE_CAR = 4) with blowVehicleWithoutExplosion = true -> withExplosion == false.
    Pkt exp2(PktId::Explosion);
    exp2.Bit(false)
        .Bit(true).Id(500).Bit(true).Bit(true)
        .Pos(0.f, 0.f, 0.f)
        .Bits(4, 4);  // EXP_TYPE_CAR
    Check(f.Send(exp2), "EXPLOSION car decoded");
    f.Pump();

    // 3. Creator in different dimension -> explosion cancelled, onClientExplosion not fired.
    ServerWorld::NoteElementDimension(20, 7);
    Pkt exp3(PktId::Explosion);
    exp3.Bit(true).Id(20).Compressed(10, 2).Bit(false).Pos(1.f, 2.f, 3.f).Bits(0, 4);
    Check(f.Send(exp3), "EXPLOSION dim mismatch decoded");
    f.Pump();
    ServerWorld::NoteElementDimension(20, 0);
    f.Lua("assert(#expLog == 2, 'dim-mismatch suppressed third explosion')\n"
          "assert(expLog[1][1] == 20 and math.abs(expLog[1][2] - 15) < 0.1 and math.abs(expLog[1][3] - 25) < 0.1)\n"
          "assert(#vehExpLog == 1 and vehExpLog[1][1] == 500 and vehExpLog[1][2] == false)\n"
          "assert(isVehicleBlown({id=500, element=true}) == true)");

    // FIRE (38): valid and malformed.
    Pkt fire(PktId::Fire);
    fire.Id(20).Compressed(20, 2).F(1.f).F(2.f).F(3.f).F(1.8f);
    Check(f.Send(fire), "FIRE decoded");
    Pkt badFire(PktId::Fire);
    badFire.Id(20).Compressed(20, 2).F(1.f).F(std::numeric_limits<float>::infinity()).F(3.f).F(1.f);
    Check(!f.Send(badFire), "FIRE rejects infinity");

    // PROJECTILE (39): grenade (16), homing rocket (20) with target, flare (58).
    Pkt proj1(PktId::Projectile);
    proj1.Bit(true).Id(20).Compressed(15, 2)
        .Bit(true).Id(20)
        .Pos(1.f, 2.f, 0.5f)
        .Bits(16, 6).U16(0)
        .Float7_17(0.75f).Vel(0.5f, 0.5f, 0.2f);
    Check(f.Send(proj1), "PROJECTILE grenade decoded");

    Pkt proj2(PktId::Projectile);
    proj2.Bit(true).Id(20).Compressed(15, 2)
        .Bit(false)
        .Pos(10.f, 20.f, 30.f)
        .Bits(20, 6).U16(0)
        .Bit(true).Id(500)
        .Vel(1.f, 0.f, 0.f)
        .F(0.f).F(0.f).F(0.f);
    Check(f.Send(proj2), "PROJECTILE rocket_hs decoded");

    Pkt proj3(PktId::Projectile);
    proj3.Bit(true).Id(20).Compressed(10, 2).Bit(false).Pos(5.f, 6.f, 7.f).Bits(58, 6).U16(0);
    Check(f.Send(proj3), "PROJECTILE flare decoded");

    // Truncated grenade (missing force + velocity) rejected.
    Pkt badProj(PktId::Projectile);
    badProj.Bit(true).Id(20).Compressed(10, 2).Bit(false).Pos(1.f, 2.f, 3.f).Bits(16, 6).U16(0);
    Check(!f.Send(badProj), "PROJECTILE truncated grenade rejected");

    // In headless host builds without libGTASA.so, Projectiles::Create returns 0 (no fake projectile).
    // Pumping verifies the queued creation lambda runs cleanly without crashing.
    f.Pump();
    f.Lua("assert(#projLog == 0, 'headless host creates no fake native projectile')");
}

void TestPickups() {
    Fixture f;
    AddServerPickup(f.handler, 601, 1240, true, 1.f, 2.f, 3.f);
    AddServerPickup(f.handler, 602, 1242, true, 4.f, 5.f, 6.f);

    Pkt hs(PktId::PickupHideShow);
    hs.Bit(false).Id(601).Compressed(1242, 2).Id(602).Compressed(1240, 2);
    Check(f.Send(hs), "PICKUP_HIDESHOW multi-entry decoded");

    bool vis1 = true, vis2 = true;
    int mod1 = 0, mod2 = 0;
    Check(ServerWorld::CopyPickupState(601, vis1, mod1) && !vis1 && mod1 == 1242, "pickup 601 hidden & model updated");
    Check(ServerWorld::CopyPickupState(602, vis2, mod2) && !vis2 && mod2 == 1240, "pickup 602 hidden & model updated");

    // PICKUP_HIT_CONFIRM: desktop passes bHide directly to SetVisible(bHide).
    Pkt hit(PktId::PickupHitConfirm);
    hit.Id(601).Bit(true).Bit(true);
    Check(f.Send(hit), "PICKUP_HIT_CONFIRM decoded");
    Check(ServerWorld::CopyPickupState(601, vis1, mod1) && vis1, "pickup 601 visibility updated by hit confirm");
}

void TestVehicleSpawnAndTrailer() {
    Fixture f;
    ServerWorld::NoteVehicle(700, 515);  // tractor
    ServerWorld::NoteVehicle(701, 435);  // trailer

    f.Lua("evLog = {}\n"
          "addEventHandler('onClientTrailerAttach', root, function(veh) evLog[#evLog+1] = {'attach', source.id, veh.id} end)\n"
          "addEventHandler('onClientTrailerDetach', root, function(veh) evLog[#evLog+1] = {'detach', source.id, veh.id} end)\n"
          "addEventHandler('onClientVehicleRespawn', root, function() evLog[#evLog+1] = {'respawn', source.id} end)");

    // Attach trailer 701 to vehicle 700.
    Pkt att(PktId::VehicleTrailer);
    att.Id(700).Id(701).Bit(true).Pos(20.f, 30.f, 5.f).RotDeg(0.f, 0.f, 180.f).ZeroVel();
    Check(f.Send(att), "VEHICLE_TRAILER attach decoded");
    f.Pump();

    ServerWorld::ActorSpawn tr;
    Check(ServerWorld::CopyVisual(701, tr) && Near(tr.x, 20.0) && Near(tr.y, 30.0) &&
              Near(tr.heading, 180.0 * (3.14159265 / 180.0), 0.01),
          "trailer pose updated on attach");
    f.Lua("local t = {id=700, element=true}; local tr = {id=701, element=true}\n"
          "local rx, ry, rz = getElementRotation(tr)\n"
          "assert(math.abs(rz - 180) < 0.1)\n"
          "assert(getVehicleTowedByVehicle(t).id == 701)\n"
          "assert(getVehicleTowingVehicle(tr).id == 700)\n"
          "assert(#evLog == 1 and evLog[1][1] == 'attach' and evLog[1][2] == 701 and evLog[1][3] == 700)");

    // Mark trailer blown + damaged, then VEHICLE_SPAWN (57) should fix it, detach towedBy, set pose/colors, fire respawn.
    Script::VehicleApplyBlownState(701, true);
    Pkt sp(PktId::VehicleSpawn);
    sp.Id(701).U8(9).F(100.f).F(200.f).F(15.f).F(1.f).F(2.f).F(90.f)
        .Bits(1, 2)  // 2 RGB colors (ucNumColors = 1 -> 0..1)
        .U8(10).U8(20).U8(30)
        .U8(40).U8(50).U8(60);
    Check(f.Send(sp), "VEHICLE_SPAWN decoded");
    f.Pump();

    Check(ServerWorld::SyncTimeContext(701) == 9, "VEHICLE_SPAWN updated syncTimeContext");
    Check(ServerWorld::CopyVisual(701, tr) && Near(tr.x, 100.0) && Near(tr.y, 200.0) && Near(tr.z, 15.0) &&
              Near(tr.health, 1000.0) && tr.vehicleColors.has_value() && tr.vehicleColors->rgb[0][0] == 10 &&
              tr.vehicleColors->rgb[1][2] == 60,
          "VEHICLE_SPAWN updated pose, health and colors");
    f.Lua("local t = {id=700, element=true}; local tr = {id=701, element=true}\n"
          "assert(isVehicleBlown(tr) == false)\n"
          "assert(getVehicleTowingVehicle(tr) == false and getVehicleTowedByVehicle(t) == false)\n"
          "assert(#evLog == 2 and evLog[2][1] == 'respawn' and evLog[2][2] == 701)");

    // Re-attach then explicitly detach via VEHICLE_TRAILER.
    Check(f.Send(att), "VEHICLE_TRAILER re-attach");
    Pkt det(PktId::VehicleTrailer);
    det.Id(700).Id(701).Bit(false);
    Check(f.Send(det), "VEHICLE_TRAILER detach decoded");
    f.Pump();
    f.Lua("local t = {id=700, element=true}; local tr = {id=701, element=true}\n"
          "assert(getVehicleTowedByVehicle(t) == false and getVehicleTowingVehicle(tr) == false)\n"
          "assert(#evLog == 4 and evLog[4][1] == 'detach' and evLog[4][2] == 701 and evLog[4][3] == 700)");
}

void TestObjectSync() {
    Fixture f;
    AddServerObject(f.handler, 800, 1337, 1.f, 2.f, 3.f);

    // OBJECT_STARTSYNC (86): sets hosted and health, does not overwrite position.
    Pkt start(PktId::ObjectStartSync);
    start.Id(800).Bit(true).Pos(99.f, 99.f, 99.f).RotRad(1.f, 1.f, 1.f).ZeroVel().ZeroVel()
        .FloatBits(512.f, 11, 0.f, 1023.5f);
    Check(f.Send(start), "OBJECT_STARTSYNC decoded");
    Check(ServerWorld::ObjectSyncHosted(800), "OBJECT_STARTSYNC marked object hosted");
    float ox = 0, oy = 0, oz = 0;
    Check(ServerWorld::ElementPose(800, ox, oy, oz) && Near(ox, 1.0) && Near(oy, 2.0) && Near(oz, 3.0),
          "OBJECT_STARTSYNC kept existing pose");

    // OBJECT_STOPSYNC (87)
    Pkt stop(PktId::ObjectStopSync);
    stop.Id(800);
    Check(f.Send(stop), "OBJECT_STOPSYNC decoded");
    Check(!ServerWorld::ObjectSyncHosted(800), "OBJECT_STOPSYNC cleared hosted");

    // OBJECT_SYNC (88): pos + rot + health (flags = 0x07), context = 0.
    Pkt sync(PktId::ObjectSync);
    sync.Id(800).U8(0).Bits(0x07u, 5).Pos(42.5f, -18.25f, 9.5f).RotRad(0.1f, 0.2f, 0.5f)
        .FloatBits(777.5f, 11, 0.f, 1023.5f);
    Check(f.Send(sync), "OBJECT_SYNC decoded");
    f.Pump();

    Check(ServerWorld::ElementPose(800, ox, oy, oz) && Near(ox, 42.5) && Near(oy, -18.25) && Near(oz, 9.5),
          "OBJECT_SYNC updated ServerWorld pose");
    float pos[3] = {}, rot[3] = {}, scale[3] = {};
    bool moving = false;
    Check(Script::CopyObjectState(800, pos, rot, scale, moving) && Near(pos[0], 42.5) && Near(rot[2], 0.5, 0.01),
          "OBJECT_SYNC updated Lua object state");

    // Stale syncTimeContext must be ignored.
    ServerWorld::SetSyncTimeContext(800, 3);
    Pkt stale(PktId::ObjectSync);
    stale.Id(800).U8(2).Bits(0x01u, 5).Pos(999.f, 999.f, 999.f);
    Check(f.Send(stale), "OBJECT_SYNC stale context consumed");
    f.Pump();
    Check(ServerWorld::ElementPose(800, ox, oy, oz) && Near(ox, 42.5), "OBJECT_SYNC ignored stale context");
}

void TestLatentTransferAndPedTask() {
    Fixture f;
    AddServerPickup(f.handler, 900, 1240, false, 0.f, 0.f, 0.f);

    // Build an inner PICKUP_HIT_CONFIRM body (id=900, hide=true, playSound=false).
    Pkt inner;
    inner.Id(900).Bit(true).Bit(false);
    const uint32_t innerBits = static_cast<uint32_t>(inner.w.GetBitLength());
    std::vector<uint8_t> payload;
    payload.push_back(PktId::PickupHitConfirm);
    payload.resize(5);
    std::memcpy(payload.data() + 1, &innerBits, 4);
    payload.insert(payload.end(), inner.w.GetData(), inner.w.GetData() + inner.w.GetByteLength());

    // Split payload across HEAD (3 bytes), middle chunk (2 bytes), and TAIL (rest).
    const size_t cut1 = 3;
    const size_t cut2 = 5;
    Pkt head(PktId::LatentTransfer);
    head.Bits(42, 15).Bit(true).U8(0)  // FLAG_HEAD
        .U16(1)                         // CATEGORY_PACKET
        .U32(static_cast<uint32_t>(payload.size()))
        .U32(50000)
        .U16(0xFFFFu)
        .AlignByte()
        .U16(static_cast<uint16_t>(cut1))
        .Bytes(payload.data(), cut1);
    Check(f.Send(head), "LATENT_TRANSFER HEAD");

    Pkt mid(PktId::LatentTransfer);
    mid.Bits(42, 15).Bit(false).AlignByte().U16(static_cast<uint16_t>(cut2 - cut1))
        .Bytes(payload.data() + cut1, cut2 - cut1);
    Check(f.Send(mid), "LATENT_TRANSFER body chunk");

    Pkt tail(PktId::LatentTransfer);
    tail.Bits(42, 15).Bit(true).U8(1)  // FLAG_TAIL
        .AlignByte()
        .U16(static_cast<uint16_t>(payload.size() - cut2))
        .Bytes(payload.data() + cut2, payload.size() - cut2);
    Check(f.Send(tail), "LATENT_TRANSFER TAIL reassembled and dispatched");

    bool vis = false;
    int model = 0;
    Check(ServerWorld::CopyPickupState(900, vis, model) && vis, "LATENT_TRANSFER dispatched inner PICKUP_HIT_CONFIRM");

    // CANCEL resets active transfer cleanly, and huge finalSize is rejected cleanly.
    Pkt h2(PktId::LatentTransfer);
    h2.Bits(77, 15).Bit(true).U8(0).U16(1).U32(100).U32(5000).U16(0xFFFFu).AlignByte().U16(0);
    Check(f.Send(h2), "LATENT_TRANSFER second HEAD");
    Pkt cancel(PktId::LatentTransfer);
    cancel.Bits(77, 15).Bit(true).U8(2).AlignByte().U16(0);
    Check(f.Send(cancel), "LATENT_TRANSFER CANCEL");
    Pkt huge(PktId::LatentTransfer);
    huge.Bits(78, 15).Bit(true).U8(0).U16(1).U32(200u * 1024u * 1024u).U32(5000).U16(0xFFFFu).AlignByte().U16(0);
    Check(!f.Send(huge), "LATENT_TRANSFER rejects >100MB finalSize");

    // PED_TASK (101): TASK_SIMPLE_BE_HIT (1008) valid and truncated.
    Pkt task(PktId::PedTask);
    task.Id(20).U16(1008).Id(10).U8(3).U8(1).U8(24);
    Check(f.Send(task), "PED_TASK BE_HIT decoded");
    Pkt badTask(PktId::PedTask);
    badTask.Id(20).U16(1008).Id(10);
    Check(!f.Send(badTask), "PED_TASK BE_HIT truncated rejected");
}

}  // namespace

int main() {
    TestEntityAddFixes();
    TestDeadProvisionalHandlersRemoved();
    TestLightSync();
    TestVehicleResyncAndDamageSync();
    TestExplosionFireProjectile();
    TestPickups();
    TestVehicleSpawnAndTrailer();
    TestObjectSync();
    TestLatentTransferAndPedTask();
    std::printf("core_packets_tests: %u checks passed\n", checks);
    return 0;
}
