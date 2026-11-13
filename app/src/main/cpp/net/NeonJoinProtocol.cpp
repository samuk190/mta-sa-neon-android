#include "NeonJoinProtocol.h"
#include "../gtasa/GameEngine.h"
#include "../shared/CombatSync.h"
#include "../shared/ServerWorld.h"
#include "../shared/VehicleSync.h"
#include "../shared/entity/EntityVehicleModels.h"

#include <algorithm>
#include <cmath>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#define JOIN_LOG_TAG "WorldRPGNeon"
#define JOIN_LOGI(...) __android_log_print(ANDROID_LOG_INFO, JOIN_LOG_TAG, __VA_ARGS__)
#define JOIN_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, JOIN_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define JOIN_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define JOIN_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Net {

namespace {
RakNetWrapper* g_joinedNet = nullptr;

uint32_t NowMs() {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

constexpr uint32_t JOIN_STEP_TIMEOUT_MS = 15000;

void WriteStringRaw(BitStreamWriter& w, const std::string& value) {
    // MTA NetBitStreamInterface::WriteString: [uint16 length][chars], no NUL.
    const uint16_t len = static_cast<uint16_t>(value.size());
    w.Write(len);
    if (len > 0) {
        w.WriteBytes(reinterpret_cast<const uint8_t*>(value.data()), len);
    }
}

void WriteFixedRaw(BitStreamWriter& w, const std::string& value, size_t fixedLen) {
    // MTA Write(str.c_str(), N): exactly N bytes, zero padded, no NUL.
    std::vector<uint8_t> buf(fixedLen, 0);
    const size_t n = value.size() < fixedLen ? value.size() : fixedLen;
    if (n > 0) memcpy(buf.data(), value.data(), n);
    w.WriteBytes(buf.data(), fixedLen);
}

void WriteBit(BitStreamWriter& w, bool value) {
    uint8_t bit = value ? 1 : 0;
    w.WriteBits(&bit, 1);
}

bool ReadBit(BitStreamReader& r, bool& out) {
    uint8_t bit = 0;
    if (!r.ReadBits(&bit, 1)) return false;
    out = (bit & 1) != 0;
    return true;
}

// RakNet BitStream::WriteCompressed(unsigned int) — variable-length encoding:
// a 1-bit per all-zero high byte, then either a 1-bit + 4 low bits (when the
// final byte's high nibble is zero) or a 0-bit + the whole final byte.
[[maybe_unused]] void WriteCompressedU32(BitStreamWriter& w, uint32_t value) {
    uint8_t bytes[4];
    memcpy(bytes, &value, 4);
    for (int currentByte = 3; currentByte > 0; --currentByte) {
        if (bytes[currentByte] == 0) {
            WriteBit(w, true);
        } else {
            WriteBit(w, false);
            w.WriteBits(bytes, static_cast<size_t>(currentByte + 1) * 8);
            return;
        }
    }
    if ((bytes[0] & 0xF0) == 0) {
        WriteBit(w, true);
        w.WriteBits(bytes, 4);
    } else {
        WriteBit(w, false);
        w.WriteBits(bytes, 8);
    }
}

bool ReadCompressedU32(BitStreamReader& r, uint32_t& out) {
    uint8_t bytes[4] = {0, 0, 0, 0};
    for (int currentByte = 3; currentByte > 0; --currentByte) {
        bool flag = false;
        if (!ReadBit(r, flag)) return false;
        if (flag) {
            bytes[currentByte] = 0;
        } else {
            if (!r.ReadBits(bytes, static_cast<size_t>(currentByte + 1) * 8)) return false;
            memcpy(&out, bytes, 4);
            return true;
        }
    }
    bool half = false;
    if (!ReadBit(r, half)) return false;
    if (half) {
        if (!r.ReadBits(bytes, 4)) return false;
        bytes[0] &= 0x0F;
    } else {
        if (!r.ReadBits(bytes, 8)) return false;
    }
    memcpy(&out, bytes, 4);
    return true;
}

// ElementID: 17 significant bits (NumberOfSignificantBits(131072-1));
// the all-ones value means INVALID_ELEMENT_ID (bitstream.h Read(ElementID)).
bool ReadElementID(BitStreamReader& r, uint32_t& out) {
    uint8_t bytes[4] = {0, 0, 0, 0};
    if (!r.ReadBits(bytes, NEON_ELEMENT_ID_BITS)) return false;
    uint32_t value = 0;
    memcpy(&value, bytes, 4);
    if (value == (1u << NEON_ELEMENT_ID_BITS) - 1) value = 0xFFFFFFFF;
    out = value;
    return true;
}

bool ReadSmallUInt(BitStreamReader& r, uint32_t bits, uint8_t& out) {
    uint8_t byte = 0;
    if (!r.ReadBits(&byte, bits)) return false;
    out = byte;
    return true;
}

// LSB-first field writer, the same bit order as the low() lambda in SendPedSync.
void WriteFieldLsb(BitStreamWriter& w, uint32_t value, uint32_t bits) {
    uint8_t bytes[4] = {};
    for (int i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    w.WriteBits(bytes, static_cast<size_t>(bits));
}

// Local player's outstanding VEHICLE_INOUT request (desktop CClientPed::m_VehicleInOutID).
// Informational only: it does not gate new requests, because GTA has already moved the ped.
// Net thread only: Pulse and the packet callback both run inside nativePulse.
enum class VehicleInOutPending : uint8_t { None, RequestIn, RequestOut };
VehicleInOutPending g_inOutPending = VehicleInOutPending::None;
uint32_t g_inOutVehicle = 0xFFFFFFFFu;
// The vehicle the server has seated the local ped in: set when NOTIFY_IN is sent after REQUEST_IN_CONFIRMED, cleared
// on NOTIFY_OUT, REQUEST_OUT_CONFIRMED, ATTEMPT_FAILED, jack and disconnect. Desktop CNetAPI::DoPulse sends vehicle
// puresync only for GetOccupiedVehicle(), which is set the same way (CClientPed.cpp WarpIntoVehicle after notify).
uint32_t g_confirmedVehicle = 0xFFFFFFFFu;

} // anonymous namespace

const char* JoinStateName(JoinState state) {
    switch (state) {
        case JoinState::Idle: return "idle";
        case JoinState::WaitingRakNet: return "waiting-raknet";
        case JoinState::WaitingModName: return "waiting-mod-name";
        case JoinState::JoinDataSent: return "joindata-sent";
        case JoinState::ServerJoinComplete: return "server-join-complete";
        case JoinState::InGameNoticeSent: return "ingame-notice-sent";
        case JoinState::JoinedGame: return "joined-game";
        case JoinState::Complete: return "complete";
        case JoinState::Failed: return "failed";
    }
    return "unknown";
}

struct NeonJoinClient::Impl {
    RakNetWrapper* net = nullptr;
    JoinState state = JoinState::Idle;
    NeonJoinInfo info;
    StateCallback callback;
    std::string nickname;
    uint32_t deadlineMs = 0;

    void SetState(JoinState next, const char* detail) {
        state = next;
        deadlineMs = NowMs() + JOIN_STEP_TIMEOUT_MS;
        JOIN_LOGI("NeonJoin: state -> %s (%s)", JoinStateName(next), detail ? detail : "");
        if (callback) callback(next, detail ? detail : "");
    }

    void Fail(const std::string& reason) {
        g_confirmedVehicle = 0xFFFFFFFFu;
        info.lastError = reason;
        JOIN_LOGE("NeonJoin: FAILED: %s", reason.c_str());
        state = JoinState::Failed;
        if (callback) callback(JoinState::Failed, reason.c_str());
    }

    uint32_t lastSyncMs = 0;
    uint32_t lastKeyMs = 0;
    uint32_t lastPedMs = 0;
    uint32_t lastUnoccupiedMs = 0;
    // Last synced values per hosted vehicle (desktop CDeathmatchVehicle::m_LastSyncedData).
    struct UnoccupiedLastSynced {
        WorldRPG::VehicleSync::Vec3 position, rotation, velocity, turnVelocity;
        float health = 0.f;
    };
    std::unordered_map<uint32_t, UnoccupiedLastSynced> lastUnoccupied;
    float lastKeyHeading = 0.f;
    std::string pendingChat;

    // PLAYER_VEHICLE_PURESYNC (packet 33), driver form. Desktop CNetAPI::DoPulse (279-380) sends
    // WriteVehiclePuresync instead of WritePlayerPuresync while the local ped is in a car. Only seat 0 is
    // ported. Driving keys come from the local pad (CopyLocalCar, sampled in WatchLocalVehicle). Fire, action,
    // enter/exit and walk keys stay zero: no source yet. Player health comes from the CopyBody sample (armor has no
    // source, so 0, as on foot). The camera is assumed to look along the car heading, level pitch (no camera sample).
    void SendDriverPuresync(const WorldRPG::Game::LocalCarState& car) {
        // Train block is not ported. Return before ConsumeDamage so a pending hit is not lost.
        if (WorldRPG::Entity::VehicleModels::IsTrainModel(static_cast<uint32_t>(car.model))) return;

        float x = 0, y = 0, z = 0, heading = 0, vx = 0, vy = 0, vz = 0, health = 100.f;
        WorldRPG::Game::CopyBody(x, y, z, heading, vx, vy, vz, health);

        // Car front at zero pitch and roll is (-sin rz, cos rz) (desktop CMatrix::SetRotation, CMatrix.h 253-275);
        // the camera forward is (sin c, cos c), so c = -rz. Wrap to -pi..pi.
        const float pi = 3.14159265f;
        float camera = -car.rotZ * pi / 180.f;
        while (camera > pi) camera -= 2.f * pi;
        while (camera < -pi) camera += 2.f * pi;

        WorldRPG::VehicleSync::DriverPuresyncIn in;
        in.timeContext = 0;
        in.model = car.model;
        in.position = {car.x, car.y, car.z};
        in.rotation = {car.rotX, car.rotY, car.rotZ};
        in.velocity = {car.vx, car.vy, car.vz};
        in.turnVelocity = {car.tx, car.ty, car.tz};
        in.vehicleHealth = car.health;
        in.playerHealth = health;
        in.playerArmor = 0.f;
        in.cameraRotZ = camera;
        in.cameraRotX = 0.f;
        // Driving keys from the local CControllerState (desktop CNetAPI::WriteFullKeysync, CNetAPI.cpp 1915-1934).
        // WriteFullKeys writes an analog byte only for 1..254 and the bit from the same value, as on the desktop.
        in.keys.stickX = car.steering;             // LeftStickX (-128..128)
        in.keys.analogCross = car.accelerate;      // ButtonCross, accelerate
        in.keys.buttonCross = car.accelerate != 0;
        in.keys.analogSquare = car.brake;          // ButtonSquare, brake / reverse
        in.keys.buttonSquare = car.brake != 0;
        in.keys.rightShoulder1 = car.handBrake;    // RightShoulder1, handbrake
        in.keys.shockButtonL = car.horn;           // ShockButtonL, horn
        in.lookLeft = car.lookLeft;                // LeftShoulder2, written only when flags.aircraft
        in.lookRight = car.lookRight;              // RightShoulder2, same
        uint32_t attacker = 0;
        uint8_t weapon = 0;
        if (WorldRPG::Game::ConsumeDamage(attacker, weapon)) {
            in.hasDamage = true;
            in.damagerId = attacker;
            in.damageWeapon = weapon;
        }

        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(33));          // PLAYER_VEHICLE_PURESYNC
        if (!WorldRPG::VehicleSync::WriteDriverPuresync(w, in)) return;
        if (net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                            PacketPriority::MEDIUM, PacketReliability::UNRELIABLE_SEQUENCED)) {
            static int reports = 0;
            if (reports < 4) {
                ++reports;
                JOIN_LOGI("NeonJoin: sending PLAYER_VEHICLE_PURESYNC vehicle %u model %d at %.0f %.0f %.0f",
                          car.vehicleId, car.model, car.x, car.y, car.z);
            }
        }
    }

    // PLAYER_BULLETSYNC (packet 98). Desktop CNetAPI::SendBulletSyncFire (CNetAPI.cpp 2391-2414), called from
    // CClientGame::PostWeaponFire (CClientGame.cpp 6024-6026). One reliable packet per shot queued by the CWeapon::Fire
    // hook (PlayerPedPort.cpp InstallLocalBulletFireHook). Body after the id: weapon u8, start 3 floats, end 3 floats,
    // order u8, hit bit. Only the no-damage form is sent (CNetAPI.cpp 2409-2412); hit reports are not ported yet.
    uint8_t bulletOrder = 0;  // desktop m_ucBulletSyncOrderCounter (CNetAPI.cpp 2398)
    void SendBulletSync() {
        if (!net || !net->IsConnected()) return;
        WorldRPG::Game::LocalShot shot;
        while (WorldRPG::Game::ConsumeLocalShot(shot)) {
            // Desktop CClientGame::PostWeaponFire (CClientGame.cpp 6024) sends only weapons in the SYNC_SETTINGS list.
            if (shot.weapon < 0 || shot.weapon > 255 || !WorldRPG::Combat::WeaponUsesBulletSync(static_cast<uint8_t>(shot.weapon)))
                continue;
            BitStreamWriter w;
            w.Write(static_cast<uint8_t>(98));           // PLAYER_BULLETSYNC
            w.Write(static_cast<uint8_t>(shot.weapon));  // stream->Write(char weapon)
            w.Write(shot.sx);                            // start CVector (raw floats, CNetAPI.cpp 2396)
            w.Write(shot.sy);
            w.Write(shot.sz);
            w.Write(shot.ex);                            // end CVector (CNetAPI.cpp 2397)
            w.Write(shot.ey);
            w.Write(shot.ez);
            w.Write(bulletOrder++);                      // order counter (CNetAPI.cpp 2398)
            WriteBit(w, false);                          // no damage (CNetAPI.cpp 2409-2412)
            if (net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                                PacketPriority::MEDIUM, PacketReliability::RELIABLE)) {
                static int reports = 0;
                if (reports < 4) {
                    ++reports;
                    JOIN_LOGI("NeonJoin: sending PLAYER_BULLETSYNC weapon %d from %.0f %.0f %.0f to %.0f %.0f %.0f",
                              shot.weapon, shot.sx, shot.sy, shot.sz, shot.ex, shot.ey, shot.ez);
                }
            }
        }
    }

    // Bitstream 0x0030, the version the join hook stores for this client:
    // no extended positions and no native-task block. Time context 0 is
    // accepted by CanUpdateSync. Without this packet the server never
    // relays a position, so a desktop client has no ped to draw.
    void SendPureSync() {
        if (!net || !net->IsConnected()) return;
        const uint32_t now = NowMs();
        if (now - lastSyncMs < 100) return;
        lastSyncMs = now;
        // In a mapped car the driver sends packet 33 instead (SendDriverPuresync); on foot, packet 32 below.
        WorldRPG::Game::LocalCarState car;
        if (WorldRPG::Game::CopyLocalCar(car)) {
            // Only the car the server seated us in. GTA can place the ped in a car before the server confirms it
            // (request pending, refused or jacked); the desktop sends the on-foot sync then.
            if (car.vehicleId == g_confirmedVehicle) {
                SendDriverPuresync(car);
                return;
            }
            static int unconfirmedReports = 0;
            if (unconfirmedReports < 4) {
                ++unconfirmedReports;
                JOIN_LOGI("NeonJoin: GTA car %u is not server-confirmed (confirmed %u); sending on-foot PURESYNC",
                          car.vehicleId, g_confirmedVehicle);
            }
        }
        float x = 0, y = 0, z = 0, heading = 0, vx = 0, vy = 0, vz = 0, health = 100.f;
        WorldRPG::Game::CopyBody(x, y, z, heading, vx, vy, vz, health);
        const float pi = 3.14159265f;
        while (heading < -pi) heading += 2.f * pi;
        while (heading > pi) heading -= 2.f * pi;
        const auto rotationBits = static_cast<uint32_t>(std::lround(65535.0 * ((heading + pi) / (2.0 * pi))));
        WorldRPG::Game::OnFootKeys foot;
        WorldRPG::Game::CopyOnFoot(heading, vx, vy, foot);
        const auto cameraBits = static_cast<uint32_t>(std::lround(4095.0 * ((foot.camera + pi) / (2.0 * pi)))) & 0xFFFu;
        auto low = [](BitStreamWriter& w, uint32_t value, int bits) {
            uint8_t bytes[4] = {};
            for (int i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
            w.WriteBits(bytes, static_cast<size_t>(bits));
        };
        auto fixed24 = [&](BitStreamWriter& w, float coord) {
            int32_t fixed = static_cast<int32_t>(std::lround(static_cast<double>(coord) * 1024.0));
            if (fixed > 8388607) fixed = 8388607;
            if (fixed < -8388608) fixed = -8388608;
            low(w, static_cast<uint32_t>(fixed) & 0xFFFFFFu, 24);
        };
        static float lastX = 0, lastY = 0;
        static bool hasLast = false;
        const float moveX = hasLast ? x - lastX : 0.f;
        const float moveY = hasLast ? y - lastY : 0.f;
        hasLast = true;
        lastX = x;
        lastY = y;
        const float move = std::sqrt(moveX * moveX + moveY * moveY);
        float speed = std::sqrt(vx * vx + vy * vy);
        if (speed < 0.02f && move > 0.04f) {
            const float scale = move > 0.25f ? 0.20f : 0.09f;
            const float inv = 1.f / move;
            vx = moveX * inv * scale;
            vy = moveY * inv * scale;
            vz = 0.f;
            speed = scale;
        }
        const bool moving = speed > 0.02f;
        const int stickX = foot.stickX;
        const int stickY = foot.stickY;
        uint8_t keys = 0;
        if (foot.fire) keys |= 0x10;
        if (foot.duck) keys |= 0x40;
        if (moving && speed <= 0.16f) keys |= 0x80;
        uint32_t syncFlags = moving ? 0x0402u : 0x0002u;
        if (foot.duck) syncFlags |= 0x0008u;
        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(32));          // PLAYER_PURESYNC
        w.Write(static_cast<uint8_t>(0));           // time context
        w.Write(keys);
        low(w, 0, 1);                               // no analog square
        low(w, 0, 1);                               // no analog cross
        w.Write(static_cast<uint8_t>(stickX));
        w.Write(static_cast<uint8_t>(stickY));
        low(w, syncFlags, 15);                      // ground, duck, velocity while moving
        fixed24(w, x);
        fixed24(w, y);
        w.Write(z);
        low(w, rotationBits, 16);
        if (moving) {
            const float modulus = std::max(speed, 0.001f);
            float nx = vx / modulus, ny = vy / modulus, nz = vz / modulus;
            auto writeNorm = [&](BitStreamWriter& out) {
                auto bit = [&](bool on) {
                    uint8_t value = on ? 1u : 0u;
                    out.WriteBits(&value, 1);
                };
                bit(nx < 0.f);
                if (ny == 0.f) bit(true);
                else {
                    bit(false);
                    const auto raw = static_cast<uint16_t>(std::lround((ny + 1.f) * 32767.5f));
                    out.Write(raw);
                }
                if (nz == 0.f) bit(true);
                else {
                    bit(false);
                    const auto raw = static_cast<uint16_t>(std::lround((nz + 1.f) * 32767.5f));
                    out.Write(raw);
                }
            };
            const float planar = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (planar > 0.001f) {
                nx /= planar;
                ny /= planar;
                nz /= planar;
            }
            uint8_t one = 1;
            w.WriteBits(&one, 1);
            w.Write(modulus);
            writeNorm(w);
        }
        const int healthByte = std::clamp(static_cast<int>(std::lround(health)), 0, 200);
        w.Write(static_cast<uint8_t>(healthByte));
        w.Write(static_cast<uint8_t>(0));           // armor
        low(w, cameraBits, 12);                     // camera faces the ped heading
        const auto camZ = static_cast<uint8_t>(std::lround(255.0 * ((foot.camera + pi) / (2.0 * pi))) & 0xFF);
        w.Write(camZ);
        w.Write(static_cast<uint8_t>(128));         // camera rot X, level
        low(w, 0, 1);                               // camera offset is relative
        low(w, 0, 2);                               // 3-bit offset table
        low(w, 4, 3);
        low(w, 4, 3);
        low(w, 4, 3);
        uint32_t attacker = 0;
        uint8_t weapon = 0;
        if (WorldRPG::Game::ConsumeDamage(attacker, weapon)) {
            low(w, 1, 1);
            uint32_t idBits = attacker & 0x1FFFFu;
            uint8_t idBytes[4] = {};
            std::memcpy(idBytes, &idBits, 4);
            w.WriteBits(idBytes, 17);
            low(w, weapon & 0x3Fu, 6);
            low(w, 0, 3);                           // torso
        } else {
            low(w, 0, 1);                           // no damage info
        }
        if (net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                            PacketPriority::MEDIUM, PacketReliability::UNRELIABLE_SEQUENCED)) {
            static float loggedX = 0, loggedY = 0;
            static int reports = 0;
            const float dx = x - loggedX;
            const float dy = y - loggedY;
            if (reports < 8 && (reports == 0 || dx * dx + dy * dy > 9.f)) {
                ++reports;
                loggedX = x;
                loggedY = y;
                JOIN_LOGI("NeonJoin: sending PLAYER_PURESYNC at %.0f %.0f %.0f heading %.2f", x, y, z, heading);
            }
        }
    }

    // CNetAPI::WriteKeysync for an on-foot player who is not aiming.
    // Sent between pure-sync samples when the heading changes, which is
    // what the desktop uses to turn the remote ped.
    void SendKeySync() {
        if (!net || !net->IsConnected()) return;
        const uint32_t now = NowMs();
        if (now - lastSyncMs < 40 || now - lastKeyMs < 50) return;
        float x = 0, y = 0, z = 0, heading = 0, vx = 0, vy = 0, vz = 0, health = 100.f;
        WorldRPG::Game::CopyBody(x, y, z, heading, vx, vy, vz, health);
        (void)x; (void)y; (void)z; (void)health;
        const float pi = 3.14159265f;
        while (heading < -pi) heading += 2.f * pi;
        while (heading > pi) heading -= 2.f * pi;
        float turn = heading - lastKeyHeading;
        while (turn > pi) turn -= 2.f * pi;
        while (turn < -pi) turn += 2.f * pi;
        const float speed = std::sqrt(vx * vx + vy * vy);
        WorldRPG::Game::OnFootKeys foot;
        WorldRPG::Game::CopyOnFoot(heading, vx, vy, foot);
        if (std::fabs(turn) < 0.05f && speed < 0.02f && !foot.fire && !foot.duck) return;
        lastKeyMs = now;
        lastKeyHeading = heading;
        auto low = [](BitStreamWriter& w, uint32_t value, int bits) {
            uint8_t bytes[4] = {};
            for (int i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
            w.WriteBits(bytes, static_cast<size_t>(bits));
        };
        const auto angleBits = static_cast<uint32_t>(std::lround(4095.0 * ((heading + pi) / (2.0 * pi)))) & 0xFFFu;
        const auto cameraBits = static_cast<uint32_t>(std::lround(4095.0 * ((foot.camera + pi) / (2.0 * pi)))) & 0xFFFu;
        uint8_t keys = 0;
        if (foot.fire) keys |= 0x10;
        if (foot.duck) keys |= 0x40;
        if (speed > 0.02f && speed <= 0.16f) keys |= 0x80;
        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(31));
        w.Write(keys);
        low(w, 0, 1);
        low(w, 0, 1);
        w.Write(static_cast<uint8_t>(foot.stickX));
        w.Write(static_cast<uint8_t>(foot.stickY));
        low(w, angleBits, 12);
        low(w, cameraBits, 12);
        low(w, foot.duck ? 1u : 0u, 4);
        if (foot.fire) {
            low(w, 1, 1);
            w.Write(static_cast<uint8_t>(0));
            low(w, 0, 4);
        }
        net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                        PacketPriority::MEDIUM, PacketReliability::UNRELIABLE_SEQUENCED);
    }

    void SendPedSync() {
        if (!net || !net->IsConnected()) return;
        const uint32_t now = NowMs();
        if (now - lastPedMs < 400) return;
        WorldRPG::Game::HostedPed peds[16];
        const int count = WorldRPG::Game::CopyHostedPeds(peds, 16);
        if (count <= 0) return;
        lastPedMs = now;
        auto low = [](BitStreamWriter& w, uint32_t value, int bits) {
            uint8_t bytes[4] = {};
            for (int i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
            w.WriteBits(bytes, static_cast<size_t>(bits));
        };
        auto fixed24 = [&](BitStreamWriter& w, float coord) {
            int32_t fixed = static_cast<int32_t>(std::lround(static_cast<double>(coord) * 1024.0));
            if (fixed > 8388607) fixed = 8388607;
            if (fixed < -8388608) fixed = -8388608;
            low(w, static_cast<uint32_t>(fixed) & 0xFFFFFFu, 24);
        };
        const float pi = 3.14159265f;
        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(63));
        for (int i = 0; i < count; ++i) {
            const auto& ped = peds[i];
            float heading = ped.heading;
            while (heading < -pi) heading += 2.f * pi;
            while (heading > pi) heading -= 2.f * pi;
            const float speed = std::sqrt(ped.vx * ped.vx + ped.vy * ped.vy + ped.vz * ped.vz);
            low(w, ped.id & 0x1FFFFu, 17);
            w.Write(static_cast<uint8_t>(0));
            w.Write(static_cast<uint8_t>(speed > 0.001f ? 0x07 : 0x03));
            w.Write(static_cast<uint8_t>(0));
            fixed24(w, ped.x);
            fixed24(w, ped.y);
            w.Write(ped.z);
            const auto rotationBits = static_cast<uint32_t>(std::lround(65535.0 * ((heading + pi) / (2.0 * pi))));
            low(w, rotationBits, 16);
            if (speed > 0.001f) {
                uint8_t one = 1;
                w.WriteBits(&one, 1);
                w.Write(speed);
                float nx = ped.vx / speed, ny = ped.vy / speed, nz = ped.vz / speed;
                auto bit = [&](bool on) {
                    uint8_t value = on ? 1u : 0u;
                    w.WriteBits(&value, 1);
                };
                bit(nx < 0.f);
                if (ny == 0.f) bit(true);
                else {
                    bit(false);
                    w.Write(static_cast<uint16_t>(std::lround((ny + 1.f) * 32767.5f)));
                }
                if (nz == 0.f) bit(true);
                else {
                    bit(false);
                    w.Write(static_cast<uint16_t>(std::lround((nz + 1.f) * 32767.5f)));
                }
            }
        }
        net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                        PacketPriority::MEDIUM, PacketReliability::UNRELIABLE_SEQUENCED);
    }

    // CUnoccupiedVehicleSync::DoPulse, UpdateStates and WriteVehicleInformation
    // (mta_neon_source/Client/mods/deathmatch/logic/CUnoccupiedVehicleSync.cpp, lines 57, 229, 270).
    // Every 400 ms each hosted vehicle is compared with its last synced values, using the thresholds
    // of WriteVehicleInformation: FLOAT_EPSILON for x/y position and velocity, 0.1 for z position and
    // z velocity, MIN_ROTATION_DIFF 0.1 with a wrap at 360, and an exact compare for turn velocity. A
    // vehicle is written only when a field changed (bSyncVehicle). All changed vehicles go in one
    // packet 56, unreliable sequenced. A vehicle seen for the first time sends its full state.
    // Engine-on, derailed, in-water, trailer and health are not sent (no verified Android source yet).
    void SendUnoccupiedVehicleSync() {
        if (!net || !net->IsConnected()) return;
        const uint32_t now = NowMs();
        if (now - lastUnoccupiedMs < 400) return;
        lastUnoccupiedMs = now;

        constexpr float kFloatEpsilon = 0.0001f;   // FLOAT_EPSILON (Shared/sdk/CVector.h 26)
        constexpr float kZThreshold = 0.1f;        // z position and z velocity threshold
        constexpr float kMinRotationDiff = 0.1f;   // MIN_ROTATION_DIFF (Client/.../CUnoccupiedVehicleSync.h 16)
        constexpr int kMaxVehicles = 48;           // same cap as the hosted vehicle loop in ApplyRemotePeds

        WorldRPG::Game::HostedCar cars[kMaxVehicles];
        const int count = WorldRPG::Game::CopyHostedCars(cars, kMaxVehicles);

        // GetSmallestWrapUnsigned(value, 360) (Shared/mods/deathmatch/logic/Utils.h 249).
        auto smallestWrap = [](float value) {
            float wrapped = value - 360.f * std::floor(value / 360.f);
            if (wrapped > 180.f) wrapped = 360.f - wrapped;
            return wrapped;
        };
        // Moving test used for the velocity rest transition (x/y with FLOAT_EPSILON, z with 0.1).
        auto moving = [kFloatEpsilon, kZThreshold](const WorldRPG::VehicleSync::Vec3& v) {
            return std::fabs(v.x) > kFloatEpsilon || std::fabs(v.y) > kFloatEpsilon || std::fabs(v.z) > kZThreshold;
        };

        std::vector<WorldRPG::VehicleSync::UnoccupiedUpdate> updates;
        // Rebuilt on every pass, so a vehicle that is no longer hosted is forgotten.
        std::unordered_map<uint32_t, UnoccupiedLastSynced> next;
        for (int i = 0; i < count; ++i) {
            const auto& car = cars[i];
            const WorldRPG::VehicleSync::Vec3 position{car.x, car.y, car.z};
            const WorldRPG::VehicleSync::Vec3 rotation{car.rotX, car.rotY, car.rotZ};
            const WorldRPG::VehicleSync::Vec3 velocity{car.vx, car.vy, car.vz};
            const WorldRPG::VehicleSync::Vec3 turnVelocity{car.tx, car.ty, car.tz};

            const auto found = lastUnoccupied.find(car.id);
            const bool firstSeen = found == lastUnoccupied.end();
            UnoccupiedLastSynced last = firstSeen ? UnoccupiedLastSynced{} : found->second;

            WorldRPG::VehicleSync::UnoccupiedUpdate update;
            update.vehicleId = car.id;
            update.timeContext = 0;

            // Position (non-boat branch of WriteVehicleInformation).
            if (firstSeen || std::fabs(last.position.x - position.x) > kFloatEpsilon ||
                std::fabs(last.position.y - position.y) > kFloatEpsilon || std::fabs(last.position.z - position.z) > kZThreshold) {
                update.syncPosition = true;
                update.position = position;
                last.position = position;
            }

            // Velocity. A vehicle coming to rest is sent too, so the server does not keep the last moving value.
            if (firstSeen || moving(velocity) || moving(last.velocity)) {
                update.syncVelocity = true;
                update.velocity = velocity;
                last.velocity = velocity;
            }

            // Rotation in degrees, wrapped at 360.
            const bool rotationChanged = smallestWrap(last.rotation.x - rotation.x) > kMinRotationDiff ||
                                         smallestWrap(last.rotation.y - rotation.y) > kMinRotationDiff ||
                                         smallestWrap(last.rotation.z - rotation.z) > kMinRotationDiff;
            if (firstSeen || rotationChanged) {
                update.syncRotation = true;
                update.rotation = rotation;
                last.rotation = rotation;
            }

            // Turn velocity: exact compare.
            if (firstSeen || last.turnVelocity.x != turnVelocity.x || last.turnVelocity.y != turnVelocity.y ||
                last.turnVelocity.z != turnVelocity.z) {
                update.syncTurnVelocity = true;
                update.turnVelocity = turnVelocity;
                last.turnVelocity = turnVelocity;
            }

            // Health: desktop WriteVehicleInformation (CUnoccupiedVehicleSync.cpp 360), changed by more than epsilon.
            if (firstSeen || std::fabs(last.health - car.health) > kFloatEpsilon) {
                update.syncHealth = true;
                update.health = car.health;
                last.health = car.health;
            }

            if (update.syncPosition || update.syncRotation || update.syncVelocity || update.syncTurnVelocity ||
                update.syncHealth) updates.push_back(update);
            next.emplace(car.id, last);
        }
        lastUnoccupied.swap(next);
        if (updates.empty()) return;

        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(56));  // PACKET_ID_UNOCCUPIED_VEHICLE_SYNC
        for (const auto& update : updates) WorldRPG::VehicleSync::WriteUnoccupiedEntry(w, update);
        SendGamePacket(w, false);
    }

    // VEHICLE_INOUT (game packet 58) request side for the local player. Desktop enter request:
    // ped, vehicle, action(4), seat(4), isOnWater(1), door(3) (CClientPed.cpp:10061-10072).
    // Desktop exit request: ped, vehicle, action(4), then a 2-bit door only for doors 2..5
    // (CClientPed.cpp:10178-10188). The server reads an absent exit door as 0xFF
    // (CVehicleInOutPacket::Read). Sent with SendUpe: priority HIGH, reliable ordered, as desktop does.
    // Events come from WatchLocalVehicle (GameEngine.cpp), so GTA has already moved the ped when this runs.
    void SendVehicleInOut() {
        if (!net || !net->IsConnected() || info.localPlayerId == 0xFFFFFFFFu) return;
        WorldRPG::Game::LocalVehicleEvent ev;
        while (WorldRPG::Game::ConsumeLocalVehicleEvent(ev)) {
            BitStreamWriter w;
            w.Write(static_cast<uint8_t>(58));  // PACKET_ID_VEHICLE_INOUT (Shared/sdk/net/Packets.h:96)
            WriteFieldLsb(w, info.localPlayerId & 0x1FFFFu, NEON_ELEMENT_ID_BITS);
            WriteFieldLsb(w, ev.vehicleId & 0x1FFFFu, NEON_ELEMENT_ID_BITS);
            if (ev.entered) {
                WriteFieldLsb(w, 0, 4);  // action VEHICLE_REQUEST_IN = 0 (CClientGame.h:174)
                WriteFieldLsb(w, 0, 4);  // seat 0
                WriteFieldLsb(w, 0, 1);  // isOnWater false
                WriteFieldLsb(w, 0, 3);  // door 0
            } else {
                WriteFieldLsb(w, 3, 4);  // action VEHICLE_REQUEST_OUT = 3 (CClientGame.h:177); no door bits
            }
            if (!SendUpe(w)) {
                JOIN_LOGE("VehInOut: send %s failed veh=%u", ev.entered ? "request_in" : "request_out", ev.vehicleId);
                continue;
            }
            if (g_inOutPending != VehicleInOutPending::None) {
                JOIN_LOGI("VehInOut: new request while a previous one is pending (server replies in order)");
            }
            g_inOutPending = ev.entered ? VehicleInOutPending::RequestIn : VehicleInOutPending::RequestOut;
            g_inOutVehicle = ev.vehicleId;
            JOIN_LOGI("VehInOut: sent %s ped=%u veh=%u", ev.entered ? "request_in seat=0 door=0" : "request_out",
                      info.localPlayerId, ev.vehicleId);
        }
    }

    bool SendUpe(BitStreamWriter& body) {
        if (!net) return false;
        return net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), body,
                               PacketPriority::HIGH, PacketReliability::RELIABLE_ORDERED);
    }

    // The Oracle net.so hook only finishes a join when it sees game packet 19
    // (PACKET_ID_MTA_RESERVED_13) of at least 700 bytes with the bytes
    // "1.6.0-" between offsets 650 and 699. It then rewrites the 8 bytes
    // before that marker into PLAYER_JOINDATA. A compact packet 4 is logged
    // and dropped, and net.so closes the socket.
    bool SendJoinData() {
        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(19));
        // Known-good mobile blob is 763 bytes with the marker at offset 676
        // and a 95-byte join body at offset 668. 667 zero bytes put that
        // body there when the packet id occupies byte 0.
        const std::vector<uint8_t> pad(667, 0);
        w.WriteBytes(pad.data(), pad.size());

        w.Write(NEON_NETCODE_VERSION);      // ushort, overwritten by the hook
        w.Write(NEON_MTA_VERSION);          // ushort
        w.Write(NEON_BITSTREAM_VERSION);    // ushort, overwritten to 0x0030

        // 15 characters, so the hook's in-place rewrite to
        // "1.7.0-9.29999.0" keeps the length prefix valid. With the id and
        // the 667-byte pad, the marker lands at offset 676.
        WriteStringRaw(w, "1.6.0-9.29999.0");

        WriteBit(w, false);                 // optional update info required
        w.Write(NEON_GAME_VERSION);         // uchar game version

        const std::string nick = NormalizeAscii(nickname);
        WriteFixedRaw(w, nick, NEON_MAX_PLAYER_NICK_LENGTH);

        const uint8_t emptyPasswordMd5[16] = {0};
        w.WriteBytes(emptyPasswordMd5, 16); // MD5 of empty password is zeros

        WriteFixedRaw(w, std::string(), NEON_MAX_SERIAL_LENGTH); // legacy community field

        // No trailing ticket. The 95-byte body captured from the working
        // mobile client ends on the 32-byte legacy field. Neon reads a
        // ticket only when another length prefix is present.
        return SendUpe(w);
    }

    // [99][RPC][uchar rpcId] — CNetAPI::RPC envelope, no arguments for the
    // two join RPCs.
    bool SendRpc(NeonServerRPC rpc) {
        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(NeonPacketID::RPC));
        w.Write(static_cast<uint8_t>(rpc));
        return SendUpe(w);
    }

    // MOD_NAME: [ushort bitstreamVersion][String modName]
    //           [uchar neonAuthMode][String serverId (mode != Disabled)]
    void HandleModName(const uint8_t* body, size_t length, size_t bitLength = 0) {
        BitStreamReader r(body, length, bitLength);
        uint16_t bsVersion = 0;
        std::string modName;
        if (!r.Read(bsVersion) || !r.ReadString(modName, 64)) {
            Fail("MOD_NAME truncated");
            return;
        }
        info.serverBitStreamVersion = bsVersion;
        info.modName = modName;

        if (r.GetRemainingBits() >= 8) {
            uint8_t mode = 0;
            if (!r.Read(mode) || mode > 2) {
                Fail("MOD_NAME bad neon auth mode");
                return;
            }
            info.authMode = static_cast<NeonAuthMode>(mode);
            if (info.authMode != NeonAuthMode::Disabled) {
                if (!r.ReadString(info.neonServerId, 128)) {
                    Fail("MOD_NAME truncated neon server id");
                    return;
                }
            }
        }

        if (modName != "deathmatch") {
            Fail("unsupported mod '" + modName + "'");
            return;
        }
        if (info.authMode == NeonAuthMode::Required) {
            Fail("server requires SA:O (Neon Identity) sign-in; not implemented");
            return;
        }

        JOIN_LOGI("NeonJoin: MOD_NAME mod=%s bs=0x%X auth=%u",
                  modName.c_str(), bsVersion, static_cast<unsigned>(info.authMode));

        if (!SendJoinData()) {
            Fail("failed to send PLAYER_JOINDATA");
            return;
        }
        SetState(JoinState::JoinDataSent, "PLAYER_JOINDATA sent");
    }

    // SERVER_JOIN_COMPLETE: [String connText][String versionSortable]
    bool ReadJoinCompleteStrings(const uint8_t* body, size_t length, size_t bitLength) {
        BitStreamReader r(body, length, bitLength);
        return r.ReadString(info.serverVersionText, 256) &&
               r.ReadString(info.serverVersionSortable, 256);
    }

    void HandleServerJoinComplete(const uint8_t* body, size_t length, size_t bitLength = 0) {
        // deathmatch writes 432 bits. net.so delivers 1456 bits: a 1024-bit
        // prefix, then those 432 bits. Reading at byte 0 sees a length of
        // 0xD000 and rejects the packet.
        const bool enveloped = bitLength >= 1024 + 16 && length > 128;
        if (!ReadJoinCompleteStrings(body, length, bitLength) &&
            !(enveloped && ReadJoinCompleteStrings(body + 128, length - 128, bitLength - 1024))) {
            Fail("SERVER_JOIN_COMPLETE version field truncated");
            return;
        }
        JOIN_LOGI("NeonJoin: SERVER_JOIN_COMPLETE [%s] sortable=[%s]",
                  info.serverVersionText.c_str(), info.serverVersionSortable.c_str());

        if (!SendRpc(NeonServerRPC::PLAYER_INGAME_NOTICE)) {
            Fail("failed to send RPC PLAYER_INGAME_NOTICE");
            return;
        }
        SetState(JoinState::InGameNoticeSent, "RPC PLAYER_INGAME_NOTICE sent");
    }

    // SERVER_JOINEDGAME — CPlayerJoinCompletePacket::Write order:
    // ElementID(17b) player, u8 numPlayers, ElementID(17b) root,
    // int32 enableClientChecks, bit voice, 2b sampleRate, 4b quality,
    // CompressedU32 bitrate, bit fakelag, int32 httpMaxConn, u8 httpType,
    // [u16 port | u16 port + String url], String serverName.
    void HandleServerJoinedGame(const uint8_t* body, size_t length, size_t bitLength = 0) {
        BitStreamReader r(body, length, bitLength);
        uint8_t numPlayers = 0;
        uint8_t sampleRate = 0;
        uint8_t quality = 0;
        bool voice = false;
        bool fakelag = false;

        bool ok = ReadElementID(r, info.localPlayerId) &&
                  r.Read(numPlayers) &&
                  ReadElementID(r, info.rootElementId);
        ok = ok && r.Read(info.enableClientChecks);
        ok = ok && ReadBit(r, voice);
        ok = ok && ReadSmallUInt(r, 2, sampleRate);
        ok = ok && ReadSmallUInt(r, 4, quality);
        uint32_t bitrate = 0;
        ok = ok && ReadCompressedU32(r, bitrate);
        ok = ok && ReadBit(r, fakelag);
        ok = ok && r.Read(info.httpMaxConnections);
        uint8_t httpType = 0;
        ok = ok && r.Read(httpType);
        // Only 0 (off), 1 (port) and 2 (URL) exist (CPlayerJoinCompletePacket). On the 152-bit mobile
        // profile this desktop read runs past the missing fakelag bit and can still succeed with a
        // garbage type (observed live: http=3 port=0). Treat it as a failed parse so the mobile path runs.
        ok = ok && httpType <= 2;

        if (ok && (httpType == 1 /*PORT*/ || httpType == 2 /*URL*/)) {
            ok = r.Read(info.httpPort);
            if (ok && httpType == 2) {
                ok = r.ReadString(info.httpUrl, 256);
            }
        }
        // The Oracle hook rewrites this packet for mobile clients: no fakelag
        // bit and no URL/server-name tail. That profile is 152 bits. The
        // desktop tail is several times longer.
        if (ok && bitLength > 200) ok = r.ReadString(info.serverName, 256);
        if (!ok && bitLength <= 200) {
            BitStreamReader mobile(body, length, bitLength);
            ok = ReadElementID(mobile, info.localPlayerId) &&
                 mobile.Read(numPlayers) &&
                 ReadElementID(mobile, info.rootElementId) &&
                 mobile.Read(info.enableClientChecks) &&
                 ReadBit(mobile, voice) &&
                 ReadSmallUInt(mobile, 2, sampleRate) &&
                 ReadSmallUInt(mobile, 4, quality) &&
                 ReadCompressedU32(mobile, bitrate) &&
                 mobile.Read(info.httpMaxConnections) &&
                 mobile.Read(httpType);
            if (ok && httpType != 0) ok = mobile.Read(info.httpPort);
            info.serverName.clear();
            fakelag = false;
        }
        if (!ok) {
            Fail("SERVER_JOINEDGAME truncated");
            return;
        }
        info.voiceEnabled = voice;
        info.voiceSampleRate = sampleRate;
        info.voiceQuality = quality;
        info.voiceBitrate = bitrate;
        info.fakeLagCommand = fakelag;
        info.httpDownloadType = httpType;
        // This join id is the local ped for the whole session. PacketHandler accepts a PLAYER_SPAWN only for it.
        if (info.localPlayerId != 0xFFFFFFFFu) WorldRPG::ServerWorld::SetLocalPlayer(info.localPlayerId);

        JOIN_LOGI("NeonJoin: SERVER_JOINEDGAME id=%u root=%u name=[%s] http=%u port=%u",
                  info.localPlayerId, info.rootElementId, info.serverName.c_str(),
                  httpType, info.httpPort);

        if (!SendRpc(NeonServerRPC::INITIAL_DATA_STREAM)) {
            Fail("failed to send RPC INITIAL_DATA_STREAM");
            return;
        }
        net->MarkSessionAccepted();
        SetState(JoinState::Complete, "joined; waiting initial data stream");
    }

    // SERVER_DISCONNECTED: [5b type][String duration when banned][String reason]
    void HandleServerDisconnected(const uint8_t* body, size_t length, size_t bitLength = 0) {
        BitStreamReader r(body, length, bitLength);
        uint8_t type = 0;
        if (r.ReadBits(&type, 5)) {
            info.disconnectType = type & 0x1F;
        }
        // BANNED_SERIAL=3, BANNED_IP=4 carry a duration string first.
        if (info.disconnectType == 3 || info.disconnectType == 4) {
            std::string duration;
            r.ReadString(duration, 64);
        }
        r.ReadString(info.disconnectReason, 256);
        const char* typeName = "refused";
        switch (info.disconnectType) {
            case 1: typeName = "invalid password"; break;
            case 2: typeName = "invalid nickname"; break;
            case 7: typeName = "join flood"; break;
            case 10: typeName = "bad version"; break;
            case 13: typeName = "nickname already in use"; break;
            case 16: typeName = "serial verification failed"; break;
            case 22: typeName = "serial already in use"; break;
            default: break;
        }
        Fail(std::string("server refused connection (") + typeName + "): " +
             (info.disconnectReason.empty() ? std::string("no reason") : info.disconnectReason));
    }
};

NeonJoinClient::NeonJoinClient() : pImpl_(std::make_unique<Impl>()) {}
NeonJoinClient::~NeonJoinClient() = default;

bool NeonJoinClient::Begin(RakNetWrapper& net, const std::string& nickname, const std::string& password) {
    if (!password.empty()) {
        // Empty password hashes to 16 zero bytes, which matches the server's
        // expectation for unpassworded servers. A real password needs MD5;
        // refuse explicitly instead of sending a wrong hash.
        pImpl_->Fail("password-protected servers not supported yet (MD5 pending)");
        return false;
    }
    const std::string normalized = NormalizeAscii(nickname);
    if (normalized.empty() || normalized.size() > NEON_MAX_PLAYER_NICK_LENGTH) {
        pImpl_->Fail("invalid nickname length");
        return false;
    }
    pImpl_->net = &net;
    pImpl_->nickname = normalized;
    pImpl_->info = NeonJoinInfo{};
    pImpl_->SetState(JoinState::WaitingRakNet, "waiting for RakNet connect");
    return true;
}

namespace {
bool SendChatLine(RakNetWrapper& net, const std::string& command);
}

void NeonJoinClient::ForceConnectionLost() {
    if (pImpl_->state == JoinState::Complete) pImpl_->Fail("Connection lost");
}

void NeonJoinClient::Pulse() {
    Impl& d = *pImpl_;
    g_joinedNet = d.state == JoinState::Complete ? d.net : nullptr;
    if (!d.net) return;

    switch (d.state) {
        case JoinState::WaitingRakNet:
            if (d.net->IsRakNetConnected()) {
                // The server net module emits PLAYER_JOIN when NIC arrives.
                // The desktop waits for MOD_NAME; it sends no application join.
                d.SetState(JoinState::WaitingModName, "transport ready; waiting MOD_NAME");
            } else if (NowMs() > d.deadlineMs) {
                d.Fail("timeout waiting for RakNet connect");
            }
            break;
        case JoinState::WaitingModName:
        case JoinState::JoinDataSent:
        case JoinState::InGameNoticeSent:
            if (NowMs() > d.deadlineMs) {
                d.Fail(std::string("timeout in state ") + JoinStateName(d.state));
            }
            break;
        case JoinState::Complete:
            if (!d.net->IsRakNetConnected()) {
                // The transport dropped after the join. Surface it so the launcher
                // can offer Retry / Quit (P3.1).
                d.Fail("Connection lost");
                break;
            }
            d.SendPureSync();
            d.SendKeySync();
            d.SendPedSync();
            d.SendUnoccupiedVehicleSync();
            d.SendVehicleInOut();
            d.SendBulletSync();
            if (!d.pendingChat.empty() && SendChatLine(*d.net, d.pendingChat)) d.pendingChat.clear();
            break;
        default:
            break;
    }
}

bool SendGamePacket(BitStreamWriter& body, bool reliable) {
    if (!g_joinedNet || !g_joinedNet->IsConnected()) return false;
    return g_joinedNet->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), body,
        PacketPriority::MEDIUM,
        reliable ? PacketReliability::RELIABLE_ORDERED : PacketReliability::UNRELIABLE_SEQUENCED);
}

// Same as SendGamePacket at the priority desktop uses for PLAYER_RESOURCE_START
// (CResource.cpp 1636: PACKET_PRIORITY_HIGH, PACKET_RELIABILITY_RELIABLE_ORDERED).
bool SendGamePacketHighPriority(BitStreamWriter& body) {
    if (!g_joinedNet || !g_joinedNet->IsConnected()) return false;
    return g_joinedNet->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), body,
        PacketPriority::HIGH, PacketReliability::RELIABLE_ORDERED);
}

// VEHICLE_INOUT replies for the local ped, called by PacketHandler on the net thread (game packet 58).
// action is a client NOTIFY value: 1 = VEHICLE_NOTIFY_IN, 4 = VEHICLE_NOTIFY_OUT (CClientGame.h:175, 178).
// Desktop sends ped, vehicle, action(4) only (CClientPed.cpp:10319-10324 notify out, 10369-10389 notify in).
bool SendVehicleInOutNotify(uint32_t pedId, uint32_t vehicleId, uint8_t action) {
    if (action != 1 && action != 4) return false;
    if (!g_joinedNet || !g_joinedNet->IsConnected() || pedId == 0xFFFFFFFFu) return false;
    BitStreamWriter w;
    w.Write(static_cast<uint8_t>(58));  // PACKET_ID_VEHICLE_INOUT
    WriteFieldLsb(w, pedId & 0x1FFFFu, NEON_ELEMENT_ID_BITS);
    WriteFieldLsb(w, vehicleId & 0x1FFFFu, NEON_ELEMENT_ID_BITS);
    WriteFieldLsb(w, action, 4);
    const bool sent = g_joinedNet->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                                              PacketPriority::HIGH, PacketReliability::RELIABLE_ORDERED);
    if (!sent) {
        JOIN_LOGE("VehInOut: send %s failed veh=%u", action == 1 ? "notify_in" : "notify_out", vehicleId);
        return false;
    }
    if (vehicleId != g_inOutVehicle) {
        JOIN_LOGI("VehInOut: reply for veh=%u differs from pending veh=%u", vehicleId, g_inOutVehicle);
    }
    g_inOutPending = VehicleInOutPending::None;
    // NOTIFY_IN seats the local ped on the server; NOTIFY_OUT unseats it.
    g_confirmedVehicle = action == 1 ? vehicleId : 0xFFFFFFFFu;
    JOIN_LOGI("VehInOut: sent %s ped=%u veh=%u", action == 1 ? "notify_in" : "notify_out", pedId, vehicleId);
    return true;
}

// VEHICLE_NOTIFY_JACK_ABORT (client action 7). The port can't play the jack, so it answers REQUEST_JACK_CONFIRMED
// with the abort, as desktop CClientPed::UpdateVehicleInOut does when a jack is cancelled (CClientPed.cpp ~10418):
// door (3 bits), SDoorOpenRatioSync closed (uncompressed=0, notZero=0), started-jacking bit = 0. Server reader:
// CVehicleInOutPacket::Read. Without it the server keeps the ped in the jacking action and refuses every later
// entry with FAIL_ACTION (reason 7), seen in the live headless test.
bool SendVehicleJackAbort(uint32_t pedId, uint32_t vehicleId, uint8_t door) {
    if (!g_joinedNet || !g_joinedNet->IsConnected() || pedId == 0xFFFFFFFFu) return false;
    BitStreamWriter w;
    w.Write(static_cast<uint8_t>(58));  // PACKET_ID_VEHICLE_INOUT
    WriteFieldLsb(w, pedId & 0x1FFFFu, NEON_ELEMENT_ID_BITS);
    WriteFieldLsb(w, vehicleId & 0x1FFFFu, NEON_ELEMENT_ID_BITS);
    WriteFieldLsb(w, 7u, 4);              // VEHICLE_NOTIFY_JACK_ABORT
    WriteFieldLsb(w, door & 0x7u, 3);
    WriteFieldLsb(w, 0u, 1);              // SDoorOpenRatioSync: not uncompressed
    WriteFieldLsb(w, 0u, 1);              //   ratio 0 (closed)
    WriteFieldLsb(w, 0u, 1);              // bAlreadyStartedJacking
    const bool sent = g_joinedNet->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                                              PacketPriority::HIGH, PacketReliability::RELIABLE_ORDERED);
    g_inOutPending = VehicleInOutPending::None;
    g_confirmedVehicle = 0xFFFFFFFFu;
    JOIN_LOGI("VehInOut: sent notify_jack_abort ped=%u veh=%u door=%u ok=%d", pedId, vehicleId,
              static_cast<unsigned>(door), sent ? 1 : 0);
    return sent;
}

// VEHICLE_ATTEMPT_FAILED (action 9) and NOTIFY_*_RETURN for the local ped end the outstanding request.
// NOTIFY_IN_RETURN also arrives for the local ped after our NOTIFY_IN, so this must not touch g_confirmedVehicle.
void ClearVehicleInOutPending() {
    g_inOutPending = VehicleInOutPending::None;
}

// REQUEST_OUT_CONFIRMED, ATTEMPT_FAILED and REQUEST_JACK_CONFIRMED for the local ped: the server no longer has it seated.
void ClearVehicleInOutConfirmed() {
    g_confirmedVehicle = 0xFFFFFFFFu;
}

void NeonJoinClient::OnTransportFailure(const std::string& reason) {
    if (pImpl_->state == JoinState::Idle || pImpl_->state == JoinState::Failed) return;
    pImpl_->Fail(reason.empty() ? "Transport connection failed" : reason);
}

bool NeonJoinClient::ProcessGamePacket(uint8_t gameId, const uint8_t* body, size_t length, size_t bitLength) {
    Impl& d = *pImpl_;
    // Late packets from an ended attempt must not replace its failure or
    // fall through to entity handlers after the session has already failed.
    if (d.state == JoinState::Failed) return true;

    // Accepted in any join state (also post-join, before the caller tears
    // this client down).
    if (gameId == static_cast<uint8_t>(NeonPacketID::SERVER_DISCONNECTED)) {
        d.HandleServerDisconnected(body, length, bitLength);
        return true;
    }
    // The connecting client tolerates these netc-forwarded packets before
    // MOD_NAME (Client CConnectManager StaticProcessPacket).
    if (gameId == static_cast<uint8_t>(NeonPacketID::SERVER_JOIN) ||
        gameId == static_cast<uint8_t>(NeonPacketID::SERVER_JOIN_DATA)) {
        JOIN_LOGI("NeonJoin: ignoring pre-join packet id=%u", gameId);
        return IsActive();
    }

    if (!IsActive()) return false;

    const auto expected = static_cast<uint8_t>(NeonPacketID::MOD_NAME);
    switch (d.state) {
        case JoinState::WaitingRakNet:
        case JoinState::WaitingModName:
            // MOD_NAME often sits in the same datagram burst as the connect
            // accept. The join state only advances to WaitingModName after
            // DoPulse returns, so the name was discarded and the download
            // line waited until this step timed out.
            if (gameId == expected) {
                d.HandleModName(body, length, bitLength);
                return true;
            }
            break;
        case JoinState::JoinDataSent:
            if (gameId == static_cast<uint8_t>(NeonPacketID::SERVER_JOIN_COMPLETE)) {
                d.HandleServerJoinComplete(body, length, bitLength);
                return true;
            }
            break;
        case JoinState::InGameNoticeSent:
            if (gameId == static_cast<uint8_t>(NeonPacketID::SERVER_JOINEDGAME)) {
                d.HandleServerJoinedGame(body, length, bitLength);
                return true;
            }
            break;
        default:
            return false;
    }

    // Strict on purpose: any other application packet while joining is a
    // protocol divergence that must surface instead of being silently dropped
    // (Plano 22 P1B acceptance rule).
    d.Fail("unexpected packet id " + std::to_string(gameId) + " in state " + JoinStateName(d.state));
    return true;
}

void NeonJoinClient::SetStateCallback(StateCallback callback) {
    pImpl_->callback = std::move(callback);
}

JoinState NeonJoinClient::State() const {
    return pImpl_->state;
}

bool NeonJoinClient::IsActive() const {
    const JoinState s = pImpl_->state;
    return s != JoinState::Idle && s != JoinState::Failed && s != JoinState::Complete;
}

const NeonJoinInfo& NeonJoinClient::Info() const {
    return pImpl_->info;
}

namespace {
bool SendChatLine(RakNetWrapper& net, const std::string& command) {
    if (!net.IsConnected() || command.empty() || command.size() > 240) return false;
    BitStreamWriter writer;
    writer.Write(static_cast<uint8_t>(42));
    writer.WriteBytes(reinterpret_cast<const uint8_t*>(command.data()), command.size());
    const bool sent = net.SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), writer,
        PacketPriority::HIGH, PacketReliability::RELIABLE_ORDERED, PacketOrdering::CHAT);
    if (sent) JOIN_LOGI("NeonJoin: chat command sent (%zu bytes)", command.size());
    return sent;
}
}

bool NeonJoinClient::SendChat(const std::string& command) {
    Impl& d = *pImpl_;
    if (command.empty() || command.size() > 240) return false;
    if (d.state != JoinState::Complete || !d.net) {
        d.pendingChat = command;
        return true;
    }
    return SendChatLine(*d.net, command);
}

void NeonJoinClient::Reset() {
    g_inOutPending = VehicleInOutPending::None;
    g_inOutVehicle = 0xFFFFFFFFu;
    g_confirmedVehicle = 0xFFFFFFFFu;
    WorldRPG::ServerWorld::SetLocalPlayer(0);
    pImpl_->net = nullptr;
    pImpl_->state = JoinState::Idle;
    pImpl_->info = NeonJoinInfo{};
    pImpl_->nickname.clear();
    pImpl_->callback = {};
}

} // namespace WorldRPG::Net
