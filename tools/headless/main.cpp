// neon_headless: host-side join client for the MTA Neon protocol.
//
// Usage: neon_headless [host] [port] [nickname] [seconds] [mode]
//   defaults: worldrpgbroficial.duckdns.org 22003 HeadlessBot 60 observer
//   mode:     observer  passive; logs relayed vehicle, bullet and in/out packets
//             driver    enters the first drivable car, drives 20 s, exits, fires 3 shots
//             (see game_script.cpp)
//
// Routing matches jni/JNI_OnLoad.cpp: the join state machine consumes the handshake
// packets; once the join is Complete every other application packet goes to
// PacketHandler (which strips the [99][gameId] envelope itself).
// Exit code: 0 when the join reached JoinState::Complete at any point, else 1.

#include <mutex>  // ElementManager.cpp needs std::unique_lock; libstdc++ does not pull <mutex> in

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>

#include "net/NeonJoinProtocol.h"
#include "net/raknet/RakNetWrapper.h"
#include "gtasa/GameEngine.h"
#include "gtasa/RemoteVehicle.h"
#include "shared/CombatSync.h"
#include "shared/ElementManager.h"
#include "shared/LuaVMStub.h"
#include "shared/PacketHandler.h"
#include "shared/ServerWorld.h"
#include "shared/SyncManager.h"
#include "shared/VehicleSync.h"

#include "game_script.h"
#include "packet_log.h"
#include "send_tap.h"

namespace {

using WorldRPG::Net::JoinState;
using WorldRPG::Net::NeonPacketID;

const char* GameIdName(uint8_t id) {
    switch (static_cast<NeonPacketID>(id)) {
        case NeonPacketID::SERVER_JOIN: return "SERVER_JOIN";
        case NeonPacketID::SERVER_JOIN_DATA: return "SERVER_JOIN_DATA";
        case NeonPacketID::SERVER_JOIN_COMPLETE: return "SERVER_JOIN_COMPLETE";
        case NeonPacketID::PLAYER_JOIN: return "PLAYER_JOIN";
        case NeonPacketID::PLAYER_JOINDATA: return "PLAYER_JOINDATA";
        case NeonPacketID::PLAYER_QUIT: return "PLAYER_QUIT";
        case NeonPacketID::PLAYER_TIMEOUT: return "PLAYER_TIMEOUT";
        case NeonPacketID::MOD_NAME: return "MOD_NAME";
        case NeonPacketID::PACKET_PROGRESS: return "PACKET_PROGRESS";
        case NeonPacketID::SERVER_JOINEDGAME: return "SERVER_JOINEDGAME";
        case NeonPacketID::SERVER_DISCONNECTED: return "SERVER_DISCONNECTED";
        case NeonPacketID::RPC: return "RPC";
        case NeonPacketID::PLAYER_LIST: return "PLAYER_LIST";
        case NeonPacketID::ENTITY_ADD: return "ENTITY_ADD";
        case NeonPacketID::ENTITY_REMOVE: return "ENTITY_REMOVE";
        case NeonPacketID::RESOURCE_START: return "RESOURCE_START";
        case NeonPacketID::RESOURCE_STOP: return "RESOURCE_STOP";
        case NeonPacketID::RESOURCE_CLIENT_SCRIPTS: return "RESOURCE_CLIENT_SCRIPTS";
    }
    switch (id) {
        case 26: return "PLAYER_SPAWN";
        case 32: return "PLAYER_PURESYNC";
        case 33: return "PLAYER_VEHICLE_PURESYNC";
        case 58: return "VEHICLE_INOUT";
        case 98: return "PLAYER_BULLETSYNC";
        case 100: return "WEAPON_BULLETSYNC";
    }
    return "GAME_ID";
}

struct Options {
    std::string host = "worldrpgbroficial.duckdns.org";
    uint16_t port = 22003;
    std::string nickname = "HeadlessBot";
    int seconds = 60;
    WorldRPG::Headless::Mode mode = WorldRPG::Headless::Mode::Observer;
};

Options ParseArgs(int argc, char** argv) {
    Options o;
    if (argc > 1) o.host = argv[1];
    if (argc > 2) o.port = static_cast<uint16_t>(std::atoi(argv[2]));
    if (argc > 3) o.nickname = argv[3];
    if (argc > 4) o.seconds = std::atoi(argv[4]);
    if (argc > 5 && std::string(argv[5]) == "driver") o.mode = WorldRPG::Headless::Mode::Driver;
    return o;
}

// What the observer saw from the server (packets relayed from other players).
struct RelayStats {
    std::map<uint8_t, uint64_t> vehInOut;  // action -> count
    struct PlayerTrack {
        uint64_t count = 0;
        float first[3] = {0, 0, 0};
        float last[3] = {0, 0, 0};
        float maxStep = 0.f;  // largest distance between consecutive samples
        bool hasFirst = false;
    };
    std::map<uint32_t, PlayerTrack> puresync33;  // player -> track
    std::map<uint8_t, uint64_t> bullets;         // weapon -> count
    uint64_t bulletsUndecodable = 0;
};
RelayStats g_relay;

// localPlayer: the join's local ElementID, so the driver script sees only its own VEHICLE_INOUT replies.
void LogReceived(uint8_t gameId, const uint8_t* body, size_t length, size_t bits, uint32_t localPlayer) {
    using namespace WorldRPG::Headless;
    const double t = ElapsedSeconds();
    switch (gameId) {
        case 58: {
            VehInOutFields f;
            if (!DecodeVehInOutServer(body, length, bits, f)) {
                std::printf("[%7.3fs] RECV 58 vehinout undecodable bytes=%zu\n", t, length);
                return;
            }
            ++g_relay.vehInOut[f.action];
            std::printf("[%7.3fs] RECV 58 vehinout ped=%u veh=%u seat=%u action=%u (%s) door=%u reason=%u\n", t,
                        f.ped, f.veh, static_cast<unsigned>(f.seat), static_cast<unsigned>(f.action),
                        VehInOutName(f.action), static_cast<unsigned>(f.door), static_cast<unsigned>(f.reason));
            if (f.ped == localPlayer) NoteServerVehicleInOut(f.veh, f.action);
            return;
        }
        case 33: {
            WorldRPG::Net::BitStreamReader r(body, length, bits);
            uint32_t player = kNoElement;
            if (!ReadElementIdBits(r, player)) {
                std::printf("[%7.3fs] RECV 33 puresync header undecodable bytes=%zu\n", t, length);
                return;
            }
            uint32_t vehicle = kNoElement;
            uint8_t seat = 0;
            const bool known = WorldRPG::RemoteVehicle::FindOccupant(player, vehicle, seat);
            WorldRPG::VehicleSync::OccupiedSync sync;
            if (!WorldRPG::VehicleSync::ReadOccupiedSync(r, seat, sync) || !sync.hasDriverBlock) {
                std::printf("[%7.3fs] RECV 33 puresync player=%u seat=%u occupant=%s body not decoded (bytes=%zu)\n",
                            t, player, static_cast<unsigned>(seat), known ? "known" : "unknown", length);
                return;
            }
            auto& track = g_relay.puresync33[player];
            const float p[3] = {sync.position.x, sync.position.y, sync.position.z};
            if (track.hasFirst) {
                const float dx = p[0] - track.last[0], dy = p[1] - track.last[1], dz = p[2] - track.last[2];
                const float step = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (step > track.maxStep) track.maxStep = step;
            } else {
                for (int i = 0; i < 3; ++i) track.first[i] = p[i];
                track.hasFirst = true;
            }
            for (int i = 0; i < 3; ++i) track.last[i] = p[i];
            ++track.count;
            std::printf("[%7.3fs] RECV 33 puresync player=%u veh=%u seat=%u occupant=%s pos=(%.2f,%.2f,%.2f) "
                        "vel=(%.3f,%.3f,%.3f) rotZ=%.1f vehHp=%.0f playerHp=%.0f accel=%u steer=%d\n",
                        t, player, vehicle, static_cast<unsigned>(seat), known ? "known" : "unknown", p[0], p[1],
                        p[2], sync.velocity.x, sync.velocity.y, sync.velocity.z, sync.rotation.z,
                        sync.vehicleHealth, sync.playerHealth, static_cast<unsigned>(sync.keys.analogCross),
                        static_cast<int>(sync.keys.stickX));
            return;
        }
        case 98: {
            WorldRPG::Net::BitStreamReader r(body, length, bits);
            WorldRPG::Combat::SBulletSync b;
            if (!WorldRPG::Combat::ReadPlayerBulletSync(r, b)) {
                ++g_relay.bulletsUndecodable;
                std::printf("[%7.3fs] RECV 98 bullet undecodable bytes=%zu\n", t, length);
                return;
            }
            ++g_relay.bullets[b.weapon];
            std::printf("[%7.3fs] RECV 98 bullet shooter=%u weapon=%u order=%u from (%.2f,%.2f,%.2f) to (%.2f,%.2f,%.2f) hit=%d\n",
                        t, b.shooter, static_cast<unsigned>(b.weapon), static_cast<unsigned>(b.order), b.start[0],
                        b.start[1], b.start[2], b.end[0], b.end[1], b.end[2], b.hit ? 1 : 0);
            return;
        }
        case 26: {
            WorldRPG::Net::BitStreamReader r(body, length, bits);
            uint32_t player = kNoElement;
            if (ReadElementIdBits(r, player)) {
                std::printf("[%7.3fs] RECV 26 player_spawn id=%u (local so far: %u)\n", t, player,
                            WorldRPG::ServerWorld::LocalPlayer());
            }
            return;
        }
        case 5: case 6: case 23: {
            std::printf("[%7.3fs] RECV %u %s bytes=%zu hex=", t, static_cast<unsigned>(gameId), GameIdName(gameId),
                        length);
            for (size_t i = 0; i < length && i < 48; ++i) std::printf("%02x", body[i]);
            std::printf("\n");
            return;
        }
        default:
            return;
    }
}

} // namespace

int main(int argc, char** argv) {
    using namespace WorldRPG::Net;

    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const Options opt = ParseArgs(argc, argv);
    WorldRPG::Headless::SetMode(opt.mode);
    const bool driver = opt.mode == WorldRPG::Headless::Mode::Driver;

    std::printf("neon_headless: %s:%u nick=%s seconds=%d mode=%s\n", opt.host.c_str(), opt.port,
                opt.nickname.c_str(), opt.seconds, driver ? "driver" : "observer");

    RakNetWrapper net;
    NeonJoinClient join;
    bool reachedComplete = false;

    // Client subsystems exactly as JNI nativeInit builds them.
    WorldRPG::Sync::ElementManager elements;
    WorldRPG::Sync::SyncManager syncManager;
    WorldRPG::Sync::PacketHandler packetHandler;
    WorldRPG::Script::LuaVMStub luaVM;
    if (!syncManager.initialize(elements) || !packetHandler.initialize(syncManager) || !luaVM.initialize()) {
        std::printf("neon_headless: client subsystem initialization failed\n");
        return 1;
    }
    packetHandler.registerAll(net);

    join.SetStateCallback([&](JoinState state, const char* detail) {
        std::printf("[%7.3fs] STATE %s: %s\n", WorldRPG::Headless::ElapsedSeconds(), JoinStateName(state),
                    detail ? detail : "");
        if (state == JoinState::Complete) {
            reachedComplete = true;
            const NeonJoinInfo& info = join.Info();
            std::printf("[%7.3fs] JOIN local_player=%u root=%u\n", WorldRPG::Headless::ElapsedSeconds(),
                        info.localPlayerId, info.rootElementId);
            WorldRPG::Headless::MarkJoined();
        }
    });

    net.SetConnectionStateCallback([&](bool connected, const char* message) {
        std::printf("[%7.3fs] TRANSPORT %s: %s\n", WorldRPG::Headless::ElapsedSeconds(),
                    connected ? "connected" : "failed", message ? message : "");
        if (!connected) join.OnTransportFailure(message ? message : "Transport connection failed");
    });

    // Packet tally. Key: game id for RID_USER_PACKET_ENUM payloads, or
    // 0x100 + wire RID for any other transport packet.
    struct Tally { uint64_t count = 0; size_t firstLength = 0; };
    std::map<uint16_t, Tally> tally;

    // Same composite routing as JNI_OnLoad.cpp (join first, PacketHandler after Complete).
    net.SetPacketHandler([&](uint8_t packetId, const uint8_t* data, size_t length, size_t bitLength) -> bool {
        if (packetId != static_cast<uint8_t>(PacketID::USER_PACKET_ENUM) || length < 1) {
            Tally& t = tally[static_cast<uint16_t>(0x100 + packetId)];
            if (t.count++ == 0) {
                std::printf("[%7.3fs] PACKET transport rid=%u len=%zu\n", WorldRPG::Headless::ElapsedSeconds(),
                            packetId, length);
            }
            return false;
        }
        const uint8_t gameId = data[0];
        Tally& t = tally[gameId];
        if (t.count++ == 0) {
            t.firstLength = length - 1;
            std::printf("[%7.3fs] PACKET id=%u %s len=%zu (first)\n", WorldRPG::Headless::ElapsedSeconds(), gameId,
                        GameIdName(gameId), length - 1);
        }
        const size_t bodyBits = bitLength >= 8 ? bitLength - 8 : 0;
        LogReceived(gameId, data + 1, length - 1, bodyBits, join.Info().localPlayerId);
        if (join.ProcessGamePacket(gameId, data + 1, length - 1, bodyBits)) return true;
        if (join.State() != JoinState::Complete) return false;
        return packetHandler.handlePacket(packetId, data, length, bitLength);
    });

    // nativeConnect: transport first, then the join attempt.
    if (!net.StartNetwork(opt.host.c_str(), opt.port) ||
        !join.Begin(net, opt.nickname, "")) {
        std::printf("neon_headless: could not start the join attempt\n");
        net.StopNetwork();
        return 1;
    }

    // nativePulse loop at 10 ms cadence for the requested duration. A Failed
    // join is terminal, so the loop ends early in that case.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(opt.seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        WorldRPG::Game::BeginServerSession();
        net.DoPulse();
        join.Pulse();
        if (join.State() == JoinState::Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const JoinState finalState = join.State();
    const NeonJoinInfo& info = join.Info();
    std::printf("\n== summary (%.1fs) mode=%s nick=%s ==\n", WorldRPG::Headless::ElapsedSeconds(),
                driver ? "driver" : "observer", opt.nickname.c_str());
    std::printf("final state: %s\n", JoinStateName(finalState));
    std::printf("reached complete: %s\n", reachedComplete ? "yes" : "no");
    std::printf("server: name=\"%s\" version=\"%s\" bitstream=%u mod=\"%s\" error=\"%s\"\n",
                info.serverName.c_str(), info.serverVersionText.c_str(),
                static_cast<unsigned>(info.serverBitStreamVersion), info.modName.c_str(),
                info.lastError.c_str());
    std::printf("disconnect: type=%u reason=\"%s\"\n", static_cast<unsigned>(info.disconnectType),
                info.disconnectReason.c_str());
    std::printf("received (relayed) vehicle in/out by action:");
    for (const auto& [action, count] : g_relay.vehInOut) {
        std::printf(" %u(%s)=%llu", static_cast<unsigned>(action), WorldRPG::Headless::VehInOutName(action),
                    static_cast<unsigned long long>(count));
    }
    std::printf("\n");
    for (const auto& [player, track] : g_relay.puresync33) {
        std::printf("received 33 player=%u count=%llu first=(%.2f,%.2f,%.2f) last=(%.2f,%.2f,%.2f) max_step=%.2f\n",
                    player, static_cast<unsigned long long>(track.count), track.first[0], track.first[1],
                    track.first[2], track.last[0], track.last[1], track.last[2], track.maxStep);
    }
    if (g_relay.puresync33.empty()) std::printf("received 33: none decoded\n");
    std::printf("received 98 bullets by weapon:");
    for (const auto& [weapon, count] : g_relay.bullets) {
        std::printf(" %u=%llu", static_cast<unsigned>(weapon), static_cast<unsigned long long>(count));
    }
    std::printf(" undecodable=%llu\n", static_cast<unsigned long long>(g_relay.bulletsUndecodable));
    WorldRPG::Headless::PrintSentSummary();
    std::printf("packets received:\n");
    for (const auto& [key, t] : tally) {
        if (key >= 0x100) {
            std::printf("  transport rid=%u  count=%llu\n", static_cast<unsigned>(key - 0x100),
                        static_cast<unsigned long long>(t.count));
        } else {
            std::printf("  id=%-3u %-24s count=%llu\n", static_cast<unsigned>(key),
                        GameIdName(static_cast<uint8_t>(key)),
                        static_cast<unsigned long long>(t.count));
        }
    }

    join.Reset();
    net.StopNetwork();
    return reachedComplete ? 0 : 1;
}
