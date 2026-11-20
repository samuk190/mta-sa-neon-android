// Host regression for UNOCCUPIED_VEHICLE_SYNC (packet 56).
//
// Desktop CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleSync (CUnoccupiedVehicleSync.cpp:178-208)
// calls CClientVehicle::SetTargetPosition / SetTargetRotation, which put the pose on the remote
// vehicle element. On the phone the equivalent queue is Game::NoteRemoteVehicle, read by
// ApplyRemoteCars on the game thread. Before the fix the decoder only logged, so an empty car the
// server moved stayed still. These checks drive the real packet handler and assert the pose that
// reaches the game layer.

#include "game_script.h"
#include "shared/PacketHandler.h"
#include "shared/VehicleSync.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace WorldRPG;

namespace {

unsigned checks = 0;

void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

bool ApplyPacket(Net::BitStreamWriter& writer) {
    Sync::PacketHandler handler;
    return handler.handlePacket(99, writer.GetData(), writer.GetByteLength(), writer.GetBitLength());
}

void BeginPacket(Net::BitStreamWriter& writer) {
    writer.Write(uint8_t(56));  // UNOCCUPIED_VEHICLE_SYNC (CUnoccupiedVehicleSync.cpp:47)
}

}  // namespace

int main() {
    // 1. One entry with position and rotation. The pose must reach the game layer with the
    //    heading converted from wire degrees to the radians NoteRemoteVehicle stores.
    {
        Net::BitStreamWriter writer;
        BeginPacket(writer);
        VehicleSync::UnoccupiedUpdate update;
        update.vehicleId = 4242;
        update.syncPosition = true;
        update.position = {100.f, 200.f, 10.f};
        update.syncRotation = true;
        update.rotation = {0.f, 0.f, 90.f};
        update.engineOn = true;
        VehicleSync::WriteUnoccupiedEntry(writer, update);
        Check(ApplyPacket(writer), "packet 56 with a pose is accepted");

        int model = 0;
        float x = 0, y = 0, z = 0, heading = 0;
        Check(Headless::CopySeenVehicle(4242, model, x, y, z, heading),
              "packet 56 pose reached the game layer");
        Check(std::fabs(x - 100.f) < 0.01f && std::fabs(y - 200.f) < 0.01f && std::fabs(z - 10.f) < 0.01f,
              "packet 56 position matches the wire value");
        Check(std::fabs(heading - 1.5707964f) < 0.001f, "packet 56 rotation Z is published in radians");
    }

    // 2. Rotation-only entry: the stored position is kept, so the car is not sent to the origin.
    {
        Net::BitStreamWriter writer;
        BeginPacket(writer);
        VehicleSync::UnoccupiedUpdate update;
        update.vehicleId = 4242;
        update.syncRotation = true;
        update.rotation = {0.f, 0.f, 180.f};
        VehicleSync::WriteUnoccupiedEntry(writer, update);
        Check(ApplyPacket(writer), "packet 56 with only a rotation is accepted");

        int model = 0;
        float x = 0, y = 0, z = 0, heading = 0;
        Check(Headless::CopySeenVehicle(4242, model, x, y, z, heading),
              "rotation-only update keeps a game-layer record");
        Check(std::fabs(x - 100.f) < 0.01f && std::fabs(y - 200.f) < 0.01f && std::fabs(z - 10.f) < 0.01f,
              "rotation-only update keeps the stored position");
        Check(std::fabs(heading - 3.1415927f) < 0.001f, "rotation-only update publishes the new heading");
    }

    // 3. An entry with no pose flags must not publish a pose (a zero position would drag the car
    //    to the origin). Desktop only calls the setters whose flag is set.
    {
        Net::BitStreamWriter writer;
        BeginPacket(writer);
        VehicleSync::UnoccupiedUpdate update;
        update.vehicleId = 4343;
        update.engineOn = true;
        VehicleSync::WriteUnoccupiedEntry(writer, update);
        Check(ApplyPacket(writer), "engine-only packet 56 entry is accepted");

        int model = 0;
        float x = 0, y = 0, z = 0, heading = 0;
        Check(!Headless::CopySeenVehicle(4343, model, x, y, z, heading),
              "engine-only entry does not publish a pose");
    }

    // 4. Two entries in one packet are both applied (desktop loops until the body ends).
    {
        Net::BitStreamWriter writer;
        BeginPacket(writer);
        for (uint32_t id : {1001u, 1002u}) {
            VehicleSync::UnoccupiedUpdate update;
            update.vehicleId = id;
            update.syncPosition = true;
            update.position = {static_cast<float>(id), 2.f, 3.f};
            VehicleSync::WriteUnoccupiedEntry(writer, update);
        }
        Check(ApplyPacket(writer), "packet 56 with two entries is accepted");
        int model = 0;
        float x = 0, y = 0, z = 0, heading = 0;
        Check(Headless::CopySeenVehicle(1001, model, x, y, z, heading), "first entry applied");
        Check(std::fabs(x - 1001.f) < 0.01f, "first entry position matches");
        Check(Headless::CopySeenVehicle(1002, model, x, y, z, heading), "second entry applied");
        Check(std::fabs(x - 1002.f) < 0.01f, "second entry position matches");
    }

    std::printf("vehicle_sync_tests: %u checks, 0 failure(s)\n", checks);
    return 0;
}
