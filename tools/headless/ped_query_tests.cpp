#include "shared/PedQueries.h"
#include "shared/ServerWorld.h"
#include "shared/PacketHandler.h"
#include "shared/MtaIds.h"
#include "shared/LuaVMStub.h"
#include "gtasa/GameEngine.h"
#include "gtasa/RemoteVehicle.h"
#include "stubs/test_ped_queries.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstdlib>
#include <string>
using namespace WorldRPG;
namespace Q = WorldRPG::PedQueries;
unsigned checks = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
void Bits(Net::BitStreamWriter& w, uint32_t n, size_t count) {
    const uint8_t raw[] = {uint8_t(n), uint8_t(n >> 8), uint8_t(n >> 16), uint8_t(n >> 24)};
    w.WriteBits(raw, count);
}
void CheckWeaponWire() {
    namespace W = PlayerWeaponSync;
    W::LocalWeapon ak;
    ak.valid = true;
    ak.slot = 5;
    ak.type = 30;
    ak.total = 273;
    ak.clip = 23;
    ak.aim.arm = -512;
    Check(W::SetAimLine(ak.aim, {100.f, 200.f, 12.f}, {97.f, 204.f, 24.f}), "normalize AK-47 aim");
    auto checkClient = [&](bool keysync, bool firing, bool switching) {
        Net::BitStreamWriter w;
        if (switching) W::WriteSwitch(w, ak, true);
        else if (keysync) W::WriteKeys(w, ak);
        else W::WritePure(w, ak, firing ? W::kFire : 0);
        Bits(w, 1, 1);  // detect offset errors at the next field
        Net::BitStreamReader r(w.GetData(), w.GetByteLength(), w.GetBitLength());
        uint8_t bit = 0;
        uint8_t type = 0, slot = 0;
        uint32_t total = 0, clip = 0;
        if (switching || keysync) Check(r.ReadBits(&bit, 1) && bit, "weapon presence or emptied-previous bit");
        if (!switching) Check(r.Read(type) && type == 30, "client sends AK-47 type");
        Check(r.ReadBits(&slot, 4) && slot == 5, "client sends rifle slot");
        if (!keysync) Check(r.ReadCompressed(total, 2) && total == 273, "client sends actual total ammo");
        Check(r.ReadCompressed(clip, 2) && clip == 23, "client sends current clip");
        if (!switching) {
            uint16_t arm = 0;
            Check(r.Read(arm) && static_cast<int16_t>(arm) == -512, "signed arm angle");
            if (keysync || firing) {
                float x = 0, y = 0, z = 0;
                Check(r.Read(x) && r.Read(y) && r.Read(z) && x == 100 && y == 200 && z == 12, "full aim origin");
                Check(r.ReadNormVector(x, z, y) && x < 0 && y > 0.3f && z > 0.9f, "aim wire components X/Z/Y");
            }
            if (keysync) Check(r.Read(type) && type == 0, "on-foot driveby direction byte");
        }
        Check(r.ReadBits(&bit, 1) && bit && r.GetRemainingBits() == 0, "next field after weapon is aligned");
    };
    checkClient(false, false, true);
    checkClient(false, false, false);
    checkClient(false, true, false);
    checkClient(true, true, false);

    VehicleSync::DriverKeysyncIn driver;
    driver.model = 400;
    driver.keys.rightShoulder1 = true;
    driver.weapon = ak;
    Net::BitStreamWriter driverPacket;
    VehicleSync::WriteDriverKeysync(driverPacket, driver);
    Net::BitStreamReader car(driverPacket.GetData(), driverPacket.GetByteLength(), driverPacket.GetBitLength());
    uint8_t control = 0, bit = 0, slot = 0;
    uint16_t rotation = 0;
    Check(car.Read(control) && (control & W::kAim) && car.ReadBits(&bit, 1) && !bit &&
          car.ReadBits(&bit, 1) && !bit && car.Read(control) && car.Read(control) &&
          car.ReadBits(reinterpret_cast<uint8_t*>(&rotation), 12) && car.ReadBits(reinterpret_cast<uint8_t*>(&rotation), 12) &&
          car.ReadBits(&control, 4) && (control & 8), "driver handbrake header");
    Check(car.ReadBits(&bit, 1) && bit && car.Read(control) && control == ak.type &&
          car.ReadBits(&slot, 4) && slot == ak.slot, "driver handbrake retains the rifle");

    for (uint8_t seat : {1, 2, 3}) {
        VehicleSync::DriverPuresyncIn passenger;
        passenger.seat = seat;
        passenger.model = 601; // turret model: no driver tail for passengers
        passenger.keys.buttonCircle = true;
        passenger.flags.gangDriveby = passenger.flags.hasWeapon = true;
        passenger.playerHealth = 93.f;
        passenger.playerArmor = 20.f;
        passenger.weapon = ak;
        passenger.weapon.aim.drivebyDirection = seat;
        Net::BitStreamWriter wire;
        Check(VehicleSync::WriteDriverPuresync(wire, passenger), "write armed passenger puresync");
        Net::BitStreamReader in(wire.GetData(), wire.GetByteLength(), wire.GetBitLength());
        uint8_t scratch[24] = {}, value = 0;
        // Context (8), full controls (26), model (32), fixed position (80), camera (28).
        Check(in.ReadBits(scratch, 174) && in.ReadBits(&value, 4) && value == seat, "passenger seat on wire");
        Check(in.ReadBits(&value, 1) && !value && in.Read(value) && value == 93 && in.Read(value) && value == 40,
              "passenger vitals immediately after seat and damage bit");
        Check(in.ReadBits(scratch, 11) && scratch[0] == 2 && scratch[1] == 2 &&
              in.ReadBits(&value, 4) && value == 5, "passenger flags and rifle slot");
        uint32_t ammo = 0;
        W::Aim aim;
        Check(in.ReadCompressed(ammo, 2) && ammo == ak.total && in.ReadCompressed(ammo, 2) && ammo == ak.clip &&
              W::ReadAim(in, aim, true) && aim.arm == -512 && in.ReadBits(&value, 2) && value == seat &&
              in.ReadBits(&value, 1) && !value && in.GetRemainingBits() == 0, "passenger ammo, aim, two-bit direction and no driver tail");

        VehicleSync::DriverKeysyncIn keys;
        keys.model = 601;
        keys.seat = seat;
        keys.keys.buttonCircle = true;
        keys.weapon = passenger.weapon;
        Net::BitStreamWriter small;
        VehicleSync::WriteDriverKeysync(small, keys);
        Net::BitStreamReader kr(small.GetData(), small.GetByteLength(), small.GetBitLength());
        Check(kr.ReadBits(scratch, 50) && kr.ReadBits(&value, 4) && value == 0 && kr.ReadBits(&value, 1) && value &&
              kr.Read(value) && value == ak.type && kr.ReadBits(&value, 4) && value == ak.slot &&
              kr.ReadCompressed(ammo, 2) && ammo == ak.clip && W::ReadAim(kr, aim, true) &&
              kr.Read(value) && value == seat && kr.GetRemainingBits() == 0, "passenger keysync ends with byte direction");
    }

    W::Aim scope;
    Check(W::SetFirstPersonAim(scope, {10.f, 20.f, 30.f}, {0.f, 0.f, -4.f}) &&
          scope.origin.x == 10.f && scope.origin.z == 28.f && scope.direction.z == -1.f,
          "scoped camera advances two units along the current view even when looking down");
    Check(!W::SetFirstPersonAim(scope, {}, {}) &&
          !W::SetFirstPersonAim(scope, {}, {std::numeric_limits<float>::infinity(), 0, 0}),
          "invalid camera front falls back to the regular aim sample");

    W::AimInterpolation interp;
    auto first = ak.aim;
    first.yaw = 3.f;
    interp.update(first, 1000);
    auto next = first;
    next.origin.y += 4.f;
    next.yaw = -3.f;
    next.arm += 100;
    next.akimboUp = true;
    interp.update(next, 1100);
    const auto halfway = interp.sample(1150);
    Check(halfway.origin.y == 202.f && halfway.arm == -462 && halfway.yaw == -3.f && !halfway.akimboUp,
          "on-foot aim blends and crosses the yaw wrap like desktop");
    Check(interp.sample(1200).akimboUp && interp.sample(1200).arm == next.arm, "akimbo state at end of interpolation");
    next.inVehicle = true;
    next.origin.y = 199.f;
    interp.update(next, 1201);
    Check(interp.sample(1201).origin.y == 199.f, "seated aim applies immediately");
    interp.update({}, 1202);
    Check(!interp.sample(1202).full, "release clears visual aim");

    // The server relay never includes the weapon type or total on foot.
    Net::BitStreamWriter relay;
    Bits(relay, 5, 4);
    relay.WriteCompressed(7, 2);
    relay.Write(static_cast<uint16_t>(-128));
    relay.Write(11.f); relay.Write(22.f); relay.Write(33.f);
    relay.WriteNormVector(-0.6f, 0.8f, 0.f);
    relay.Write(uint8_t{2});
    W::RemoteWeapon remote;
    Net::BitStreamReader r(relay.GetData(), relay.GetByteLength(), relay.GetBitLength());
    Check(W::ReadRelay(r, remote, true, true) && r.GetRemainingBits() == 0, "complete weapon keysync relay");
    Check(remote.slot == 5 && remote.ammoSynced && remote.clip == 7 && remote.aim.full &&
        remote.aim.arm == -128 && remote.aim.origin.z == 33.f &&
        remote.aim.direction.x < -0.59f && remote.aim.direction.y == 0.f && remote.aim.direction.z > 0.79f,
        "remote rifle clip and aim");
    Net::BitStreamReader shortRelay(relay.GetData(), relay.GetByteLength(), relay.GetBitLength() - 1);
    Check(!W::ReadRelay(shortRelay, remote, true, true), "truncated keysync weapon is rejected");

    Net::BitStreamWriter melee;
    Bits(melee, 1, 4);
    Net::BitStreamReader meleeRelay(melee.GetData(), melee.GetByteLength(), melee.GetBitLength());
    remote = {};
    Check(W::ReadRelay(meleeRelay, remote, true, true) && remote.slot == 1 && !remote.ammoSynced,
        "melee relay has no ammo, aim or driveby fields");
}

// Complete server relays, including analog controls before the weapon fields.
void CheckCombatPackets(Sync::PacketHandler& handler) {
    auto receive = [&](const Net::BitStreamWriter& w, size_t trim = 0) {
        return handler.handlePacket(99, w.GetData(), w.GetByteLength(), w.GetBitLength() - trim);
    };
    auto spawn = [&](uint32_t id, uint8_t context) {
        Net::BitStreamWriter w;
        w.Write(uint8_t(26)); Bits(w, id, 17); w.Write(uint8_t(0));
        w.Write(10.f); w.Write(20.f); w.Write(3.f); w.Write(0.f);
        w.Write(uint16_t(7)); w.Write(uint8_t(0)); w.Write(uint16_t(0));
        Bits(w, 0x1FFFFu, 17); w.Write(context);
        return receive(w);
    };
    const auto previousLife = Q::Generation(2);
    Check(spawn(2, 11) && !Q::Current(2, previousLife) && ServerWorld::SyncTimeContext(2) == 11,
          "spawn of a live player replaces the life and stores the server context");
    Check(spawn(1, 7) && ServerWorld::SyncTimeContext(1) == 7, "local spawn context before PLAYER_LIST");
    for (bool pure : {false, true}) {
        Net::BitStreamWriter w;
        w.Write(uint8_t(pure ? 32 : 31));
        Bits(w, 2, 17);
        if (pure) { w.Write(uint8_t(11)); w.WriteCompressed(25, 2); }
        w.Write(uint8_t(0x12));
        Bits(w, 1, 1); w.Write(uint8_t(1));   // Analog square, with fire bit clear in its value
        Bits(w, 0, 1);
        w.Write(uint8_t(0)); w.Write(uint8_t(0));
        if (pure) {
            Bits(w, 0x202, 15);
            Bits(w, 20 * 1024, 24); Bits(w, 10 * 1024, 24); w.Write(3.f);
            Bits(w, 30000, 16); w.Write(uint8_t(90)); w.Write(uint8_t(80)); Bits(w, 2048, 12);
        } else {
            Bits(w, 2048, 12); Bits(w, 2048, 12); Bits(w, 0, 4);
        }
        Bits(w, 5, 4); w.WriteCompressed(16, 2); w.Write(uint16_t(128));
        w.Write(20.f); w.Write(10.f); w.Write(4.f);
        w.WriteNormVector(-0.6f, 0.8f, 0.f);
        if (!pure) w.Write(uint8_t(0));

        Check(!receive(w, 1), "reject truncated armed relay");
        Check(!Headless::TakePlayerCombatUpdate().weapon, "partial packet leaves weapon unchanged");
        Check(receive(w), "complete armed relay accepted");
        const auto update = Headless::TakePlayerCombatUpdate();
        Check(update.player == 2 && update.weapon && update.weapon->slot == 5 && update.weapon->clip == 16,
              "relay equips the remote rifle");
        Check(update.buttons == 0x12 && update.fire && update.weapon->aim.full, "analog value cannot replace fire");
        if (pure) {
            Check(spawn(2, 12), "next spawn advances the sync context");
            Check(receive(w) && !Headless::TakePlayerCombatUpdate().weapon, "ignore previous-life rifle and vitals");
            Check(ServerWorld::CanUpdateSync(2, 0) && !ServerWorld::CanUpdateSync(2, 11),
                  "zero context remains the desktop wildcard");
        }
    }

    auto passenger = [&](uint8_t context, bool shooting, uint8_t direction, bool badOrigin = false) {
        Net::BitStreamWriter w;
        w.Write(uint8_t(33)); Bits(w, 2, 17); w.Write(context); w.WriteCompressed(12, 2);
        w.Write(uint8_t(shooting ? 0x12 : 0)); Bits(w, 0, 1); Bits(w, 0, 1); w.Write(uint8_t(0)); w.Write(uint8_t(0));
        w.Write(int32_t(400)); w.Write(uint8_t(100)); w.Write(uint8_t(0));
        Bits(w, shooting ? 0x202 : 0x200, 11); Bits(w, 5, 4);
        if (shooting) {
            w.WriteCompressed(75, 2); w.WriteCompressed(9, 2); w.Write(uint16_t(-250));
            w.Write(badOrigin ? 100001.f : 19.f); w.Write(21.f); w.Write(4.f);
            w.WriteNormVector(-0.6f, 0.8f, 0.f); Bits(w, direction, 2);
        }
        Bits(w, 0, 4); Bits(w, 0, 1);
        return receive(w);
    };
    RemoteVehicle::SetOccupant(2, 501, 1);
    Check(passenger(11, true, 1) && !Headless::TakePlayerCombatUpdate().weapon, "stale passenger weapon ignored");
    for (uint8_t direction = 0; direction < 4; ++direction) {
        Check(passenger(12, true, direction), "desktop drive-by relay accepted");
        const auto seated = Headless::TakePlayerCombatUpdate();
        Check(seated.player == 2 && seated.fire && seated.weapon && seated.weapon->slot == 5 &&
              seated.weapon->total == 75 && seated.weapon->clip == 9 && seated.weapon->aim.arm == -250 &&
              seated.weapon->aim.inVehicle && seated.weapon->aim.drivebyDirection == direction && Game::IsDriveby(2),
              "seated fire retains direction, weapon and ammo");
    }
    Net::BitStreamWriter passengerKey;
    passengerKey.Write(uint8_t(31)); Bits(passengerKey, 2, 17); passengerKey.Write(uint8_t(0x12));
    Bits(passengerKey, 0, 1); Bits(passengerKey, 0, 1); passengerKey.Write(uint8_t(0)); passengerKey.Write(uint8_t(0));
    Bits(passengerKey, 2020, 12); Bits(passengerKey, 2048, 12); Bits(passengerKey, 0, 4);
    Bits(passengerKey, 5, 4); passengerKey.WriteCompressed(8, 2); passengerKey.Write(uint16_t(725));
    passengerKey.Write(11.f); passengerKey.Write(13.f); passengerKey.Write(15.f);
    passengerKey.WriteNormVector(1.f, 0.f, 0.f); passengerKey.Write(uint8_t(3));
    Check(receive(passengerKey), "passenger keysync between vehicle updates");
    const auto key = Headless::TakePlayerCombatUpdate();
    Check(key.weapon && key.weapon->aim.inVehicle && key.weapon->aim.drivebyDirection == 3 && key.weapon->aim.arm == 725,
          "passenger flag is inferred from occupancy in keysync");
    Check(!passenger(12, true, 1, true) && !Headless::TakePlayerCombatUpdate().weapon, "invalid aim cannot change passenger controls");
    Check(passenger(12, false, 0), "drive-by release accepted");
    const auto released = Headless::TakePlayerCombatUpdate();
    Check(!released.fire && released.weapon && !released.weapon->aim.full && !Game::IsDriveby(2), "released passenger keeps the weapon");
    Check(spawn(2, 13), "spawn while seated");
    uint32_t car = 0;
    uint8_t seat = 0;
    Check(!RemoteVehicle::FindOccupant(2, car, seat), "respawn clears the previous seat");

    auto vehicleEvent = [&](uint32_t id, uint32_t vehicle, uint8_t index, uint8_t action) {
        Net::BitStreamWriter w;
        w.Write(uint8_t(58)); Bits(w, id, 17); Bits(w, vehicle, 17);
        Bits(w, index, 4); Bits(w, action, 4);
        if (action == 9) w.Write(uint8_t(1));
        return receive(w);
    };
    RemoteVehicle::SetOccupant(2, 501, 1);
    RemoteVehicle::SetOccupant(3, 501, 1);
    Check(!RemoteVehicle::FindOccupant(2, car, seat) && RemoteVehicle::FindOccupant(3, car, seat),
          "a reassigned passenger seat has exactly one occupant");
    RemoteVehicle::SetOccupant(3, 501, 2);
    Check(vehicleEvent(3, 501, 1, 4) && RemoteVehicle::FindOccupant(3, car, seat) && seat == 2,
          "late exit for previous seat in the same car preserves the new seat");
    Check(vehicleEvent(3, 501, 2, 4) && !RemoteVehicle::FindOccupant(3, car, seat),
          "exit confirmation releases the matching seat");
    RemoteVehicle::SetOccupant(1, 501, 2);
    Check(vehicleEvent(1, 501, 0, 9) && RemoteVehicle::FindOccupant(1, car, seat) && seat == 2,
          "denial with seat zero restores the local passenger's authoritative seat");
    RemoteVehicle::ClearOccupant(1);

    Net::BitStreamWriter unarmed;
    unarmed.Write(uint8_t(31)); Bits(unarmed, 2, 17); unarmed.Write(uint8_t(0x10));
    Bits(unarmed, 0, 1); Bits(unarmed, 0, 1); unarmed.Write(uint8_t(0)); unarmed.Write(uint8_t(0));
    Bits(unarmed, 2048, 12); Bits(unarmed, 2048, 12); Bits(unarmed, 0, 4); Bits(unarmed, 0, 4);
    Check(receive(unarmed), "fists relay accepted");
    const auto fists = Headless::TakePlayerCombatUpdate();
    Check(fists.fire && fists.weapon && fists.weapon->slot == 0, "switch back to fists");

    auto shot = [&](uint8_t order, Combat::BulletHit hit) {
        Net::BitStreamWriter w;
        w.Write(uint8_t(98)); Bits(w, 2, 17); w.Write(uint8_t(30));
        w.Write(20.f); w.Write(10.f); w.Write(4.f); w.Write(60.f); w.Write(10.f); w.Write(4.f);
        w.Write(order); Combat::WriteBulletHit(w, hit);
        Check(receive(w), "bullet packet accepted");
        return Headless::TakePlayerCombatUpdate().bullet;
    };
    auto ray = shot(254, {});
    Check(ray && !ray->hit && ray->end[0] == 60.f, "shot without backup goes to native replay");
    auto hit = shot(255, {1, 12.5f, 9});
    Check(hit && hit->hit && hit->victim == 1 && hit->damage == 12.5f && hit->zone == 9,
          "backup names the struck player and body part");
    auto status = [&](uint8_t type, size_t trim = 0) {
        Net::BitStreamWriter w;
        w.Write(MtaIds::Packet::PlayerNetworkStatus); w.Write(type); w.Write(uint32_t(1500));
        return receive(w, trim);
    };
    Check(Combat::RemainingBackupDamage(*hit, 1, 0.f) == 0.f, "normal connection trusts the native impact");
    Check(!status(0, 1) && Combat::RemainingBackupDamage(*hit, 1, 0.f) == 0.f, "short network status is ignored");
    Check(status(0) && Combat::RemainingBackupDamage(*hit, 1, 4.f) == 8.5f, "only unpaid damage during interruption");
    Check(Combat::RemainingBackupDamage(*hit, 1, 15.f) == 0.f && Combat::RemainingBackupDamage(*hit, 2, 0.f) == 0.f,
          "no double damage or compensation for a different victim");
    Check(status(1) && Combat::RemainingBackupDamage(*hit, 1, 0.f) == 12.5f, "backup during recovery");
    Check(Combat::RemainingBackupDamage(*hit, 1, 0.f, std::chrono::steady_clock::now() + std::chrono::seconds(1)) == 0.f,
          "end of recovery window");
    Check(!shot(255, {1, 12.5f, 9}), "duplicate bullet is not replayed twice");
    Check(bool(shot(0, {})), "consecutive rounds survive order wrap");
    auto invalid = shot(1, {1, std::numeric_limits<float>::infinity(), 9});
    Check(invalid && !invalid->hit, "unusable backup does not discard a valid shot");
    Combat::PendingDamage pending;
    pending.record(2, 28, 9);
    const auto firstHit = pending.peek();
    Check(firstHit.serial && firstHit.attacker == 2 && firstHit.weapon == 28 && firstHit.zone == 9 &&
          pending.peek().serial == firstHit.serial, "a rejected pure sync leaves the complete hit pending");
    pending.record(2, 28, 9);
    pending.acknowledge(firstHit.serial);
    const auto secondHit = pending.peek();
    Check(secondHit.serial && secondHit.serial != firstHit.serial, "ack of an older identical hit preserves the next hit");
    pending.clear();
    pending.record(2, 28, 0);
    pending.acknowledge(secondHit.serial);
    Check(pending.peek().zone == 3 && pending.peek().serial, "previous-life ack leaves a new torso hit pending");
    pending.acknowledge(pending.peek().serial);
    Check(!pending.peek().serial, "accepted damage report clears once");
    float health = 100.f, armor = 20.f;
    Check(Combat::ApplyBackupDamage(12.f, health, armor) && health == 100.f && armor == 8.f,
          "backup hit depletes armor first");
    Check(Combat::ApplyBackupDamage(12.f, health, armor) && health == 96.f && armor == 0.f,
          "consecutive hit carries remaining damage into health");
    Check(Combat::ApplyBackupDamage(100.f, health, armor) && health == 0.f &&
          !Combat::ApplyBackupDamage(10.f, health, armor), "lethal backup hit stops at zero");
    Combat::ForgetShooter(2);
    Check(bool(shot(1, {})), "reused player ID has fresh duplicate state");
    Combat::ResetBulletSync();
    Check(Combat::RemainingBackupDamage(*hit, 1, 0.f) == 0.f, "reconnect clears interruption state");
}

void PedAdd(uint32_t id) {
    Net::BitStreamWriter w;
    w.WriteCompressed(1,4);
    Bits(w,id,17); w.Write(uint8_t(14)); Bits(w,0,17); w.Write(uint8_t(0)); w.WriteCompressed(0,2);
    Bits(w,0,1); Bits(w,1,1); w.WriteCompressed(0,2); w.WriteCompressed(0,2);
    Bits(w,100*1024,24); Bits(w,50*1024,24); w.Write(12.f); w.WriteCompressed(61,2);
    Bits(w,32768,16); w.Write(uint8_t(100)); w.Write(uint8_t(0)); Bits(w,0,1);
    Bits(w,0,1); Bits(w,0,1); Bits(w,0,1); Bits(w,0,1);
    w.WriteCompressed(0,1); w.Write(uint8_t(0)); w.Write(uint8_t(0));
    w.Write(uint8_t(2)); w.Write(uint8_t(22)); w.Write(uint16_t(170));
    w.Write(uint8_t(255)); w.Write(uint8_t(2)); Bits(w,0,1);
    ServerWorld::OnEntityAdd(w.GetData(),w.GetByteLength(),w.GetBitLength());
    Check(std::string(ServerWorld::ElementKindName(id)) == "ped", "ENTITY_ADD registered a ped");
}
void Rpc(Sync::PacketHandler& handler, uint8_t fn, uint32_t id, unsigned weapon = 0,
         unsigned ammo = 0, unsigned clip = 0, bool current = false) {
    Net::BitStreamWriter w;
    w.Write(MtaIds::Packet::LuaElementRpc); w.Write(fn); Bits(w,id,17);
    if (fn == MtaIds::Rpc::GiveWeapon || fn == MtaIds::Rpc::TakeWeapon || fn == MtaIds::Rpc::SetWeaponAmmo) {
        Bits(w,weapon,6); w.WriteCompressed(ammo,2);
        if (fn == MtaIds::Rpc::GiveWeapon) Bits(w,current,1);
        if (fn == MtaIds::Rpc::SetWeaponAmmo) w.WriteCompressed(clip,2);
    } else if (fn == MtaIds::Rpc::SetWeaponSlot) Bits(w,weapon,4);
    Check(handler.handlePacket(99,w.GetData(),w.GetByteLength(),w.GetBitLength()), "weapon RPC parsed");
}
void Lua(Script::LuaVMStub& vm, const char* script) {
    auto r = vm.executeBuffer(script,std::char_traits<char>::length(script),"ped-queries");
    if (!r.success) std::fprintf(stderr,"%s\n",r.errorMessage.c_str());
    Check(r.success,"Lua query behavior");
}
void CheckPedDeltas(Sync::PacketHandler& handler) {
    auto send = [&](uint8_t context, uint8_t flags, const auto& body) {
        Net::BitStreamWriter w;
        w.Write(uint8_t(63)); Bits(w, 900, 17);
        w.Write(context); w.Write(flags); w.Write(uint8_t(0)); body(w);
        return handler.handlePacket(99, w.GetData(), w.GetByteLength(), w.GetBitLength());
    };

    Check(send(0, 0x02, [](auto& w) { Bits(w, 32768, 16); }), "accept PED_SYNC rotation alone");
    auto fields = Headless::TakePedSyncFields();
    Check(fields.id == 900 && !fields.position && fields.heading && !fields.velocity, "rotation does not replace position or velocity");

    Check(send(0, 0x04, [](auto& w) { Bits(w, 0, 1); }), "accept PED_SYNC velocity alone");
    fields = Headless::TakePedSyncFields();
    Check(fields.id == 900 && !fields.position && !fields.heading && fields.velocity &&
        fields.velocity->x == 0 && fields.velocity->y == 0 && fields.velocity->z == 0, "zero velocity is a present field");

    ServerWorld::SetSyncTimeContext(900, 11);
    Check(!send(12, 0x02, [](auto& w) { Bits(w, 32768, 16); }), "reject stale ped context");
    Check(Headless::TakePedSyncFields().id == 0, "stale ped packet does not reach the game thread");
}
int main() {
    CheckWeaponWire();
    ServerWorld::ResetSession(); ServerWorld::SetLocalPlayer(1);
    ServerWorld::NotePlayerSkin(2,7); PedAdd(900);
    Sync::PacketHandler trafficHandler;
    CheckCombatPackets(trafficHandler);
    CheckPedDeltas(trafficHandler);
    Script::LuaVMStub vm; Check(vm.initialize(),"VM initializes");
    Lua(vm, R"(
        addEventHandler("onClientPlayerNetworkStatus", localPlayer, function(kind, elapsed)
            assert(source == localPlayer and kind == 1)
            networkRecoveryTicks = elapsed
        end)
    )");
    Net::BitStreamWriter recovery;
    recovery.Write(MtaIds::Packet::PlayerNetworkStatus); recovery.Write(uint8_t(1)); recovery.Write(uint32_t(321));
    Check(trafficHandler.handlePacket(99, recovery.GetData(), recovery.GetByteLength(), recovery.GetBitLength()),
          "network recovery packet delivered with a live Lua VM");
    Lua(vm, "assert(networkRecoveryTicks == 321)");
    Lua(vm, R"(
        seatEdges = {}
        addEventHandler("onClientPlayerVehicleEnter", localPlayer, function(vehicle, seat)
            assert(source == localPlayer and vehicle.id == 501)
            table.insert(seatEdges, seat)
        end)
        addEventHandler("onClientPlayerVehicleExit", localPlayer, function(vehicle, seat, jacker)
            assert(source == localPlayer and vehicle.id == 501 and jacker == false)
            table.insert(seatEdges, -seat)
        end)
    )");
    for (unsigned action : {1u, 4u}) {
        Net::BitStreamWriter event;
        event.Write(uint8_t(58)); Bits(event, 1, 17); Bits(event, 501, 17); Bits(event, 2, 4); Bits(event, action, 4);
        Check(trafficHandler.handlePacket(99, event.GetData(), event.GetByteLength(), event.GetBitLength()),
              "seat return dispatched");
    }
    Lua(vm, "assert(#seatEdges == 2 and seatEdges[1] == 2 and seatEdges[2] == -2)");
    Combat::ResetBulletSync();
    Lua(vm,R"(
        p={id=900}; other={id=2}
        assert(getPedWeapon(p)==22 and getPedWeaponSlot(p)==2)
        assert(getPedTotalAmmo(p)==170 and getPedAmmoInClip(p)==false)
        assert(getPedWeapon(p,13)==0 and getPedTotalAmmo(p,13)==false)
        assert(getPedWeapon(p,nil)==22 and getPedWeapon(p,'2')==22)
        assert(getPedWeapon(p,255)==22 and getPedWeapon(p,256)==0)
        assert(getPedWeapon(p,-1)==false and getPedWeapon(p,{})==false)
        assert(getPedWeapon(p,0/0)==false and getPedWeapon(p,1/0)==false)
        assert(getPedWeapon({id=999})==false and getPedWeapon(1)==false)
        assert(isPedDead({id=999})==nil and isPedDead(false)==nil)
        assert(isPedDead(p)==false and isPedDead(localPlayer)==false)
        assert(getPedTotalAmmo(other)==false and getPedAmmoInClip(other)==false)
    )");
    Q::Snapshot state; Check(Q::Copy(900,state),"copy initial ped state");
    const auto generation = state.generation;
    state.streamed=true; state.currentSlot=5; state.weapons[5]={31,70000,65538};
    Headless::SetPedQueryNative(900,state);
    Lua(vm,R"(
        assert(getPedWeapon(p)==31 and getPedWeaponSlot(p)==5)
        assert(getPedTotalAmmo(p)==4464 and getPedAmmoInClip(p)==2)
        assert(getPedTotalAmmo(p,0)==1 and getPedTotalAmmo(p,1)==1)
        assert(getPedTotalAmmo(p,10)==1 and getPedTotalAmmo(p,11)==1 and getPedTotalAmmo(p,12)==1)
        assert(getPedTotalAmmo(p,3)==0 and getPedAmmoInClip(p,3)==0)
    )");
    for (int task=211;task<=219;++task) {
        state.taskType=task; state.health=100;
        Headless::SetPedQueryNative(900,state);
        Lua(vm,(std::string("assert(isPedDead(p)==")+(task>=212&&task<=218?"true)":"false)")).c_str());
    }
    state.taskType=0; state.health=0; Headless::SetPedQueryNative(900,state);
    Lua(vm,"assert(isPedDead(p)==false)"); // zero health alone is insufficient
    Q::Wasted(2); Q::Snapshot player; Q::Copy(2,player); player.taskType=0; player.health=0;
    Headless::SetPedQueryNative(2,player); Lua(vm,"assert(isPedDead(other)==true)");
    player.health=100; Headless::SetPedQueryNative(2,player);
    Lua(vm,"assert(isPedDead(other)==false)"); // stale network death after revival
    Q::Health(2,100); Headless::ClearPedQueryNative(); Lua(vm,"assert(isPedDead(other)==false)");
    // Real packet dispatch updates the same client inventory queried by Lua.
    Sync::PacketHandler handler;
    Rpc(handler,MtaIds::Rpc::GiveWeapon,900,31,90,0,true);
    Lua(vm,"assert(getPedWeapon(p)==31 and getPedTotalAmmo(p)==90 and getPedWeaponSlot(p)==5)");
    Rpc(handler,MtaIds::Rpc::GiveWeapon,900,31,20);
    Lua(vm,"assert(getPedTotalAmmo(p,5)==110)");
    Rpc(handler,MtaIds::Rpc::TakeWeapon,900,31,40);
    Lua(vm,"assert(getPedTotalAmmo(p,5)==70)");
    Rpc(handler,MtaIds::Rpc::SetWeaponAmmo,900,31,600,12);
    Lua(vm,"assert(getPedTotalAmmo(p,5)==600 and getPedAmmoInClip(p,5)==false)");
    Rpc(handler,MtaIds::Rpc::TakeWeapon,900,31,1000);
    Lua(vm,"assert(getPedTotalAmmo(p,5)==false)");
    Rpc(handler,MtaIds::Rpc::SetWeaponSlot,900,2);
    Lua(vm,"assert(getPedWeaponSlot(p)==2 and getPedWeapon(p)==22)");
    Rpc(handler,MtaIds::Rpc::SetWeaponAmmo,900,22,65530,0);
    Rpc(handler,MtaIds::Rpc::GiveWeapon,900,22,20);
    Lua(vm,"assert(getPedTotalAmmo(p,2)==14)"); // unsigned-short cache wrap
    Rpc(handler,MtaIds::Rpc::GiveWeapon,900,36,65000);
    Lua(vm,"assert(getPedTotalAmmo(p,7)==9999)"); // GiveWeapon RPC clamp
    Rpc(handler,MtaIds::Rpc::SetWeaponSlot,900,15);
    Lua(vm,"assert(getPedWeaponSlot(p)==2)");
    Rpc(handler,MtaIds::Rpc::GiveWeapon,2,31,99,0,true);
    Lua(vm,"assert(getPedWeapon(other)==31 and getPedTotalAmmo(other)==false)");
    Rpc(handler,MtaIds::Rpc::TakeAllWeapons,900);
    Lua(vm,"assert(getPedWeapon(p)==0 and getPedWeaponSlot(p)==0 and getPedTotalAmmo(p,2)==false)");
    // A consumed/reloaded native inventory is preserved on stream-out. The
    // offscreen RPCs then update the client cache before model creation.
    PedAdd(901); Q::Copy(901,state); state.streamed=true; state.weapons[2]={22,123,7};
    Q::StreamOut(901,state); Q::Snapshot saved; Q::Copy(901,saved);
    Check(saved.cachedAmmo[2]==123,"stream-out saves actual remaining ammo");
    Game::PedRpcOp give; give.kind=Game::PedRpcOp::Kind::GiveWeapon; give.weapon=22; give.ammo=20;
    state.weapons[2].total=100;
    Q::StreamOut(901,state); Q::WeaponRpc(901,give); Q::Copy(901,saved);
    Check(saved.cachedAmmo[2]==120,"offscreen give adds to actual stream-out ammo");
    give.weapon=24; Q::WeaponRpc(901,give); Q::CopyRestore(901,saved);
    Check(saved.weapons[2].type==24 && saved.weapons[2].total==140,
          "offscreen weapon replacement restores client ammo instead of replaying native give");
    // Entity deletion/reuse rejects an old GTA body's sample by generation.
    Net::BitStreamWriter remove; Bits(remove,900,17);
    ServerWorld::OnEntityRemove(remove.GetData(),remove.GetByteLength(),remove.GetBitLength());
    Lua(vm,"assert(getPedWeapon(p)==false and isPedDead(p)==nil)");
    PedAdd(900); Q::Copy(900,saved); Check(saved.generation!=generation,"ID reuse gets a new generation");
    Check(!Q::Current(900,generation) && Q::Current(900,saved.generation),"old RPC generation is rejected");
    state.generation=generation; state.weapons[2].total=999; Q::StreamOut(900,state);
    Lua(vm,"assert(getPedTotalAmmo(p)==170)");
    Q::Wasted(900); Lua(vm,"assert(isPedDead(p)==true)");
    Q::Health(900,100,false); Lua(vm,"assert(isPedDead(p)==true)"); // LockHealth does not clear m_bDead
    Q::Health(900,50);
    Lua(vm,"assert(isPedDead(p)==false)");
    Q::Spawn(2); Lua(vm,"assert(isPedDead(other)==false and getPedWeapon(other)==0)");
    ServerWorld::OnPlayerQuit(2); Check(!Q::Copy(2,saved),"player quit forgets state");
    Q::Snapshot live;
    Check(Q::Copy(900, live), "sample live actor");
    live.streamed = true;
    live.taskType = 218;
    live.currentSlot = 2;
    live.weapons[2].type = 24;
    Q::Frame frame{{900, live}};
    Q::PublishFrame(frame);
    Check(Q::CopyFrame(900, saved) && Q::IsDead(saved) && saved.weapons[2].type == 24,
          "published death task and equipped weapon");
    Q::SetCurrentSlot(900, 0, false);
    Check(!Q::CopyFrame(900, saved), "slot mutation invalidates an earlier observation");
    Q::Create(900, false);
    Check(!Q::CopyFrame(900, saved), "reused ID rejects old frame");
    frame.clear();
    Q::PublishFrame(frame);
    Check(Q::CopyFrame(900, saved) && !saved.streamed && !Q::IsDead(saved), "stream-out reads client state");
    ServerWorld::ResetSession(); Check(!Q::Copy(900,saved),"session reset forgets state");
    Check(!Q::CopyFrame(900, saved), "disconnect clears published native state");
    std::printf("ped query tests: %u checks passed\n",checks);
}
