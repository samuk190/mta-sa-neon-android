#include "NeonJoinProtocol.h"
#include "../gtasa/RemoteVehicle.h"
#include "DisconnectMessages.h"
#include "../gtasa/GameEngine.h"
#include "../shared/CombatSync.h"
#include "../shared/ConnectionLoading.h"
#include "../shared/PlayerWeaponSync.h"
#include "../shared/ServerWorld.h"
#include "../shared/VehicleSync.h"
#include "../shared/entity/EntityVehicleModels.h"

#include <algorithm>
#include <atomic>
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
std::atomic<uint64_t> g_identitySequence{0};

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

// RakNet BitStream::WriteCompressed(unsigned int). The bytes are produced by
// BitStreamWriter::WriteCompressed so this helper cannot drift from the class.
[[maybe_unused]] void WriteCompressedU32(BitStreamWriter& w, uint32_t value) {
    w.WriteCompressed(value, 4);
}

bool ReadCompressedU32(BitStreamReader& r, uint32_t& out) {
    return r.ReadCompressed(out, 4);
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
// Finish each reliable request/notify handshake before sending the next local seat change.
// Net thread only: Pulse and the packet callback both run inside nativePulse.
enum class VehicleInOutPending : uint8_t { None, RequestIn, RequestOut, NotifyIn, NotifyOut };
VehicleInOutPending g_inOutPending = VehicleInOutPending::None;
uint32_t g_inOutVehicle = 0xFFFFFFFFu;
uint32_t g_inOutSinceMs = 0;  // when g_inOutPending was last set
// Desktop has no timer: GTA only starts the task after the server answers, and
// the flag clears with the answer or when the vehicle is removed (CPacketHandler.cpp:4522).
// Here GTA has already moved the ped, and the server drops some requests
// without a reply (CGame::Packet_VehicleInOut returns early for a vehicle or ped
// it cannot use), which would block every later seat change. DIAG deviation:
// give up on an unanswered request after this long.
constexpr uint32_t kVehicleInOutTimeoutMs = 5000;

void SetVehicleInOutPending(VehicleInOutPending pending) {
    g_inOutPending = pending;
    g_inOutSinceMs = NowMs();
}
// Normal entries and scripted warps share the occupancy map used by the receiver.
bool ConfirmedSeat(uint32_t player, const Game::LocalCarState& car) {
    uint32_t vehicle = 0;
    uint8_t seat = 0;
    return RemoteVehicle::FindOccupant(player, vehicle, seat) && vehicle == car.vehicleId && seat == car.seat;
}

} // anonymous namespace

const char* JoinStateName(JoinState state) {
    switch (state) {
        case JoinState::Idle: return "idle";
        case JoinState::WaitingRakNet: return "waiting-raknet";
        case JoinState::WaitingModName: return "waiting-mod-name";
        case JoinState::WaitingIdentity: return "waiting-identity";
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
    // Server release for Standard joins, and the Automatic fallback state.
    NetcodeRelease release = NetcodeRelease::Current;
    bool autoRelease = false, fallbackUsed = false, fallbackPending = false, fallbackArmed = false;
    JoinState state = JoinState::Idle;
    NeonJoinInfo info;
    StateCallback callback;
    IdentityCallback identityCallback;
    uint64_t identityRequestId = 0;
    std::string nickname;
    std::string password;
    std::string identityTicket;
    uint32_t deadlineMs = 0;
    uint32_t stateSinceMs = 0;  // when the current state was entered

    void SetState(JoinState next, const char* detail) {
        state = next;
        stateSinceMs = NowMs();
        deadlineMs = NowMs() + (next == JoinState::WaitingIdentity ? 20000 : JOIN_STEP_TIMEOUT_MS);
        JOIN_LOGI("NeonJoin: state -> %s (%s)", JoinStateName(next), detail ? detail : "");
        if (callback) callback(next, detail ? detail : "");
    }

    // reason goes to the log; box is the desktop error box the player sees (DisconnectMessages).
    void Fail(const std::string& reason, const DisconnectMessages::Box& box) {

        info.lastError = box.text;
        JOIN_LOGE("NeonJoin: FAILED: %s -> %s: %s", reason.c_str(), box.title.c_str(), box.text.c_str());
        state = JoinState::Failed;
        const std::string packed = DisconnectMessages::Pack(box);
        if (callback) callback(JoinState::Failed, packed.c_str());
    }

    uint32_t lastSyncMs = 0;
    uint32_t lastKeyMs = 0;
    uint32_t lastPedMs = 0;
    uint32_t lastUnoccupiedMs = 0;
    // Last values written by SendPedSync (desktop CClientPed::m_LastSyncedData, SLastSyncedPedData).
    struct PedLastSynced {
        float x = 0, y = 0, z = 0;
        float vx = 0, vy = 0, vz = 0;
        float rotation = 0;
        float health = 0;
        float armor = 0;
        bool onFire = false;
        bool inWater = false;
        bool reloading = false;
        float camera = 0;
    };
    std::unordered_map<uint32_t, PedLastSynced> lastPed;
    // Last synced values per hosted vehicle (desktop CDeathmatchVehicle::m_LastSyncedData).
    struct UnoccupiedLastSynced {
        WorldRPG::VehicleSync::Vec3 position, rotation, velocity, turnVelocity;
        float health = 0.f;
        bool engineOn = false;
    };
    std::unordered_map<uint32_t, UnoccupiedLastSynced> lastUnoccupied;
    float lastKeyHeading = 0.f;
    uint8_t lastWeaponButtons = 0;
    uint8_t lastWeaponSlot = 0;
    std::string pendingChat;

    // Desktop WriteVehiclePuresync uses the same packet for every occupant.
    // Rotation, velocity and vehicle-specific controls belong to the driver.
    bool SendDriverPuresync(const WorldRPG::Game::LocalCarState& car, const PlayerWeaponSync::LocalWeapon& localWeapon) {
        float x = 0, y = 0, z = 0, heading = 0, vx = 0, vy = 0, vz = 0, health = 100.f;
        WorldRPG::Game::CopyBody(x, y, z, heading, vx, vy, vz, health);

        // Car front at zero pitch and roll is (-sin rz, cos rz) (desktop CMatrix::SetRotation, CMatrix.h 253-275);
        // the camera forward is (sin c, cos c), so c = -rz. Wrap to -pi..pi.
        const float pi = 3.14159265f;
        float camera = car.camera;
        while (camera > pi) camera -= 2.f * pi;
        while (camera < -pi) camera += 2.f * pi;

        WorldRPG::VehicleSync::DriverPuresyncIn in;
        in.timeContext = ServerWorld::SyncTimeContext(ServerWorld::LocalPlayer());
        in.model = car.model;
        in.position = {car.x, car.y, car.z};
        in.rotation = {car.rotX, car.rotY, car.rotZ};
        in.velocity = {car.vx, car.vy, car.vz};
        in.turnVelocity = {car.tx, car.ty, car.tz};
        in.vehicleHealth = car.health;
        in.playerHealth = health;
        in.playerArmor = WorldRPG::Game::CopyLocalArmor();
        in.seat = car.seat;
        in.flags.gangDriveby = localWeapon.gangDriveby && localWeapon.aim.full;
        in.flags.hasWeapon = localWeapon.valid;
        in.weapon = localWeapon;
        in.cameraRotZ = camera;
        in.cameraRotX = 0.f;
        // Driving keys from the local CControllerState (desktop CNetAPI::WriteFullKeysync, CNetAPI.cpp 1915-1934).
        // WriteFullKeys writes an analog byte only for 1..254 and the bit from the same value, as on the desktop.
        in.keys.stickX = car.steering;             // LeftStickX (-128..128)
        in.keys.analogCross = car.accelerate;      // ButtonCross, accelerate
        in.keys.buttonCross = car.accelerate != 0;
        in.keys.analogSquare = car.brake;          // ButtonSquare, brake / reverse
        in.keys.buttonSquare = car.brake != 0;
        in.keys.rightShoulder1 = car.handBrake || (localWeapon.gangDriveby && localWeapon.aiming);
        in.keys.buttonCircle = car.fire || car.firePress;
        in.flags.aircraft = WorldRPG::Entity::VehicleModels::IsPlaneOrHeliModel(car.model);
        in.keys.shockButtonL = car.horn;           // ShockButtonL, horn
        in.lookLeft = car.lookLeft;                // LeftShoulder2, written only when flags.aircraft
        in.lookRight = car.lookRight;              // RightShoulder2, same
        const auto damage = WorldRPG::Game::PeekDamage();
        if (damage.serial) {
            in.hasDamage = true;
            in.damagerId = damage.attacker;
            in.damageWeapon = damage.weapon;
            in.damageBodyPart = damage.zone;
        }

        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(33));          // PLAYER_VEHICLE_PURESYNC
        if (!WorldRPG::VehicleSync::WriteDriverPuresync(w, in)) return false;
        if (!net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                             PacketPriority::MEDIUM, PacketReliability::UNRELIABLE_SEQUENCED)) return false;
        WorldRPG::Game::AcknowledgeDamage(damage.serial);
        RememberVehicleKeys(car, in.keys, in.lookLeft, in.lookRight);
        static int reports = 0;
        if (reports < 4) {
            ++reports;
            JOIN_LOGI("NeonJoin: sending PLAYER_VEHICLE_PURESYNC vehicle %u model %d at %.0f %.0f %.0f",
                      car.vehicleId, car.model, car.x, car.y, car.z);
        }
        return true;
    }

    // PLAYER_KEYSYNC (packet 31) while the server has the local ped seated as driver. Desktop CNetAPI::DoPulse (385-397)
    // sends WriteKeysync when no puresync went out on this pulse and IsSmallKeySyncNeeded (CNetAPI.cpp 462-547) sees a
    // change against the last sent controller state. WriteKeysync (729-828) then sets bSyncingVehicle and writes the
    // vehicle tail. The on-foot writer below was used here before, which relayed accelerate and brake as released
    // between two packets 33.
    WorldRPG::VehicleSync::FullKeys lastSentDriverKeys;  // desktop m_LastSentControllerState (SetLastSentControllerState)
    bool lastSentLookLeft = false;
    bool lastSentLookRight = false;
    void RememberVehicleKeys(const WorldRPG::Game::LocalCarState& car, const WorldRPG::VehicleSync::FullKeys& keys,
                             bool lookLeft, bool lookRight) {
        lastSentDriverKeys = keys;
        lastSentLookLeft = lookLeft;
        lastSentLookRight = lookRight;
        WorldRPG::Game::AcknowledgeVehicleFire(car.firePress);
    }
    bool pureSyncSentThisPulse = false;
    // Last on-foot sticks written by packet 32 or 31 (wire bytes, -127..127).
    // A change from non-zero to the pad's 0 is the release edge.
    int lastOnFootStickX = 0;
    int lastOnFootStickY = 0;
    // Keysync saw the release between pure-sync samples. The next packet 32 writes a zero SVelocitySync.
    bool pendingZeroVelocity = false;
    void SendDriverKeySync(const WorldRPG::Game::LocalCarState& car, const PlayerWeaponSync::LocalWeapon& localWeapon) {
        // Desktop DoPulse: keysync is the else branch of the puresync send.
        if (pureSyncSentThisPulse) return;

        WorldRPG::VehicleSync::DriverKeysyncIn in;
        in.seat = car.seat;
        if (!PlayerWeaponSync::SlotHasAmmo(localWeapon.slot) || localWeapon.aim.full) in.weapon = localWeapon;
        // The same pad sample and mapping as SendDriverPuresync.
        in.keys.stickX = car.steering;
        in.keys.analogCross = car.accelerate;
        in.keys.buttonCross = car.accelerate != 0;
        in.keys.analogSquare = car.brake;
        in.keys.buttonSquare = car.brake != 0;
        in.keys.rightShoulder1 = car.handBrake || (localWeapon.gangDriveby && localWeapon.aiming);
        in.keys.buttonCircle = car.fire || car.firePress;
        in.keys.shockButtonL = car.horn;
        in.lookLeft = car.lookLeft;
        in.lookRight = car.lookRight;
        in.model = car.model;

        // IsSmallKeySyncNeeded, the parts the phone samples. LeftShoulder1, ButtonCircle, ButtonTriangle and m_bPedWalk
        // have no source in a car and stay 0 on both sides of the compare.
        const WorldRPG::VehicleSync::FullKeys& last = lastSentDriverKeys;
        const bool aircraft = WorldRPG::Entity::VehicleModels::IsPlaneOrHeliModel(static_cast<uint32_t>(car.model));
        bool needed = aircraft && (in.lookLeft != lastSentLookLeft || in.lookRight != lastSentLookRight);
        needed = needed || in.keys.buttonCircle != last.buttonCircle || in.keys.rightShoulder1 != last.rightShoulder1 || in.keys.analogSquare != last.analogSquare ||
                 in.keys.analogCross != last.analogCross || in.keys.shockButtonL != last.shockButtonL;
        // Stick: a change of more than 32 on either axis. LeftStickY is 0 in a car, so the desktop's analog-move rate
        // limit (both axes inside 1..127 or -127..-1) never applies and the change is sent at once.
        needed = needed || std::abs(static_cast<int>(in.keys.stickX) - static_cast<int>(last.stickX)) > 32;
        // The camera-orbit and aim checks need iKeySyncRotation < iPureSync; both are 100 ms here (the pulse above),
        // so the desktop skips them too.
        if (!needed) return;

        float x = 0, y = 0, z = 0, heading = 0, vx = 0, vy = 0, vz = 0, health = 100.f;
        WorldRPG::Game::CopyBody(x, y, z, heading, vx, vy, vz, health);
        in.playerRotation = heading;                       // pPlayerModel->GetCurrentRotation()
        in.cameraRotation = car.camera;

        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(31));  // PLAYER_KEYSYNC
        WorldRPG::VehicleSync::WriteDriverKeysync(w, in);
        if (!net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                             PacketPriority::MEDIUM, PacketReliability::UNRELIABLE_SEQUENCED)) return;
        RememberVehicleKeys(car, in.keys, in.lookLeft, in.lookRight);
        static int reports = 0;
        if (reports < 4) {
            ++reports;
            JOIN_LOGI("NeonJoin: sending PLAYER_KEYSYNC (vehicle form) vehicle %u steer %d accel %u brake %u handbrake %d",
                      car.vehicleId, static_cast<int>(car.steering), static_cast<unsigned>(car.accelerate),
                      static_cast<unsigned>(car.brake), car.handBrake ? 1 : 0);
        }
    }

    uint64_t reportedDeathLife = UINT64_MAX;
    // DoWastedCheck and SendPedWastedPacket differ by the ped ID after position.
    bool SendWasted(const WorldRPG::Game::HostedWasted& dead, bool local) {
        auto fixed24 = [](float coord) {
            return static_cast<uint32_t>(std::clamp(std::lround(coord * 1024.0), -8388608L, 8388607L)) & 0xFFFFFFu;
        };
        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(local ? 27 : 64));
        w.WriteCompressed(static_cast<uint32_t>(dead.animGroup), 4);
        w.WriteCompressed(static_cast<uint32_t>(dead.animId), 4);
        WriteFieldLsb(w, dead.damager & 0x1FFFFu, 17);
        WriteFieldLsb(w, dead.weapon & 0x3Fu, 6);
        WriteFieldLsb(w, WorldRPG::Combat::EncodeBodypart(static_cast<uint8_t>(dead.bodyPart)), 3);
        WriteFieldLsb(w, fixed24(dead.x), 24);
        WriteFieldLsb(w, fixed24(dead.y), 24);
        w.Write(dead.z);
        if (!local) WriteFieldLsb(w, dead.id, 17);
        w.WriteCompressed(local ? WorldRPG::Game::CopyLocalWeapon().total : 0u, 2);
        return net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                               PacketPriority::HIGH, PacketReliability::RELIABLE_ORDERED);
    }

    void SendPedWasted() {
        if (!net || !net->IsConnected()) return;
        WorldRPG::Game::HostedWasted dead;
        auto life = reportedDeathLife;
        if (WorldRPG::Game::CopyLocalCombatDeath(life, dead) && SendWasted(dead, true)) reportedDeathLife = life;
        while (WorldRPG::Game::ConsumeHostedWasted(dead)) {
            if (WorldRPG::ServerWorld::ActorHosted(dead.id)) SendWasted(dead, false);
        }
    }

    // CClientGame::PostWeaponFire queues one reliable PLAYER_BULLETSYNC per shot.
    uint8_t bulletOrder = 0;  // desktop m_ucBulletSyncOrderCounter (CNetAPI.cpp 2398)
    static constexpr uint32_t kBulletSendGiveUpMs = 2000;  // how long a failing shot holds sync back
    uint64_t failedShotSerial = 0;
    uint32_t failedShotSinceMs = 0;
    bool SendBulletSync() {
        if (!net || !net->IsConnected()) return false;
        WorldRPG::Game::LocalShot shot;
        while (WorldRPG::Game::PeekLocalShot(shot)) {
            // Desktop CClientGame::PostWeaponFire (CClientGame.cpp 6024) sends only weapons in the SYNC_SETTINGS list.
            if (shot.weapon < 0 || shot.weapon > 255 || !WorldRPG::Combat::WeaponUsesBulletSync(static_cast<uint8_t>(shot.weapon))) {
                WorldRPG::Game::FinishLocalShot(shot.serial);
                continue;
            }
            BitStreamWriter w;
            w.Write(static_cast<uint8_t>(98));           // PLAYER_BULLETSYNC
            w.Write(static_cast<uint8_t>(shot.weapon));  // stream->Write(char weapon)
            w.Write(shot.sx);                            // start CVector (raw floats, CNetAPI.cpp 2396)
            w.Write(shot.sy);
            w.Write(shot.sz);
            w.Write(shot.ex);                            // end CVector (CNetAPI.cpp 2397)
            w.Write(shot.ey);
            w.Write(shot.ez);
            w.Write(bulletOrder);                      // order counter (CNetAPI.cpp 2398)
            Combat::WriteBulletHit(w, shot.hit);
            if (!net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                                 PacketPriority::MEDIUM, PacketReliability::RELIABLE)) {
                // Pure and key sync wait for this shot (see Pulse). A send that keeps
                // failing would stop them for good, so after kBulletSendGiveUpMs the
                // shot is dropped: the server then sees the ammo without this bullet,
                // as for a shot it never received. A short stall keeps every shot.
                const uint32_t now = NowMs();
                if (failedShotSerial != shot.serial) {
                    failedShotSerial = shot.serial;
                    failedShotSinceMs = now;
                }
                if (now - failedShotSinceMs < kBulletSendGiveUpMs) return false;
                JOIN_LOGE("DIAG NeonJoin: PLAYER_BULLETSYNC for weapon %d not sent for %u ms; dropped",
                          shot.weapon, now - failedShotSinceMs);
                WorldRPG::Game::FinishLocalShot(shot.serial);
                failedShotSerial = 0;
                continue;
            }
            failedShotSerial = 0;
            WorldRPG::Game::FinishLocalShot(shot.serial);
            ++bulletOrder;
            static int reports = 0;
            if (reports < 4) {
                ++reports;
                JOIN_LOGI("NeonJoin: sending PLAYER_BULLETSYNC weapon %d from %.0f %.0f %.0f to %.0f %.0f %.0f",
                          shot.weapon, shot.sx, shot.sy, shot.sz, shot.ex, shot.ey, shot.ez);
            }
        }
        return true;
    }

    // CClientGame::UpdatePlayerWeapons (1900-1969): send the slot reliably
    // before the unreliable packets whose weapon type the server validates against it.
    void SendWeaponSwitch(const PlayerWeaponSync::LocalWeapon& weapon) {
        if (!weapon.valid || weapon.slot == lastWeaponSlot || WorldRPG::Game::LocalPlacementPending()) return;
        const bool emptiedPrevious = (lastWeaponSlot == 7 || lastWeaponSlot == 8 || lastWeaponSlot == 9) &&
            weapon.totalBySlot[lastWeaponSlot] == 0;
        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(NeonPacketID::RPC));
        w.Write(static_cast<uint8_t>(NeonServerRPC::PLAYER_WEAPON));
        PlayerWeaponSync::WriteSwitch(w, weapon, emptiedPrevious);
        if (SendUpe(w)) lastWeaponSlot = weapon.slot;
    }

    static uint8_t WeaponButtons(const WorldRPG::Game::OnFootKeys& foot, const PlayerWeaponSync::LocalWeapon& weapon) {
        if (!weapon.valid || (PlayerWeaponSync::SlotHasAmmo(weapon.slot) && !weapon.aim.full)) return 0;
        uint8_t buttons = 0;
        if (foot.fire) buttons |= PlayerWeaponSync::kFire;
        if (weapon.aiming) buttons |= PlayerWeaponSync::kAim;
        return buttons;
    }

    // The Android peer stays at 0x0030: no extended positions or native-task block.
    void SendPureSync(const PlayerWeaponSync::LocalWeapon& localWeapon) {
        pureSyncSentThisPulse = false;
        if (!net || !net->IsConnected() || WorldRPG::Game::LocalPlacementPending()) return;
        const uint32_t now = NowMs();
        if (now - lastSyncMs < 100) return;
        // In a mapped car the driver sends packet 33 instead (SendDriverPuresync); on foot, packet 32 below.
        WorldRPG::Game::LocalCarState car;
        // Packet 33's train block still requires native train data. Keep its existing on-foot fallback.
        if (WorldRPG::Game::CopyLocalCar(car) && !Entity::VehicleModels::IsTrainModel(car.model)) {
            // Only the car the server seated us in. GTA can place the ped in a car before the server confirms it
            // (request pending, refused or jacked); the desktop sends the on-foot sync then.
            if (ConfirmedSeat(info.localPlayerId, car)) {
                pureSyncSentThisPulse = SendDriverPuresync(car, localWeapon);
                if (pureSyncSentThisPulse) lastSyncMs = now;
                return;
            } else {
                static int unconfirmedReports = 0;
                if (unconfirmedReports < 4) {
                    ++unconfirmedReports;
                    JOIN_LOGI("NeonJoin: GTA car %u is not server-confirmed; sending on-foot PURESYNC", car.vehicleId);
                }
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
        // A released pad must not be rebuilt from leftover CPhysical+0x68 or from the position step.
        if (!foot.padStickZero && speed < 0.02f && move > 0.04f) {
            const float scale = move > 0.25f ? 0.20f : 0.09f;
            const float inv = 1.f / move;
            vx = moveX * inv * scale;
            vy = moveY * inv * scale;
            vz = 0.f;
            speed = scale;
        }
        if (!foot.padStickZero) pendingZeroVelocity = false;
        const bool moving = !foot.padStickZero && speed > 0.02f;
        // Falling edge, or the keysync that already announced it: bSyncingVelocity with a zero
        // SVelocitySync so ReadPlayerPuresync calls SetMoveSpeed(0) (CNetAPI.cpp 891-896).
        const bool releaseVelocity = foot.padStickZero &&
            (pendingZeroVelocity || lastOnFootStickX != 0 || lastOnFootStickY != 0);
        const int stickX = foot.stickX;
        const int stickY = foot.stickY;
        uint8_t keys = WeaponButtons(foot, localWeapon);
        const uint8_t weaponButtons = keys;
        if (foot.duck) keys |= 0x40;
        // SFullKeysyncSync bButtonCross (0x08, sprint) and bButtonSquare (0x04, jump); SyncStructures.h 1004-1012.
        if (foot.sprint) keys |= 0x08;
        if (foot.jump) keys |= 0x04;
        // m_bPedWalk (0x80) is the PC walk key in GetCurrentControllerState. A touch phone has no walk key (slow walking
        // is a small stick), so the bit stays clear. It used to be set for any speed up to 0.16, which made desktop
        // observers play the walk cycle while the phone player ran (device 2026-11-14).
        uint32_t syncFlags = (moving || releaseVelocity) ? 0x0402u : 0x0002u;
        if (foot.duck) syncFlags |= 0x0008u;
        if (localWeapon.valid) syncFlags |= PlayerWeaponSync::kHasWeapon;
        if (localWeapon.aim.akimboUp) syncFlags |= 0x0080u;
        if (localWeapon.reloading) syncFlags |= PlayerWeaponSync::kReloading;
        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(32));          // PLAYER_PURESYNC
        // CNetAPI::WritePlayerPuresync writes GetSyncTimeContext (CNetAPI.cpp 1119).
        // The mobile client doesn't process every RPC that can change the local context.
        static bool loggedContext = false;
        if (!loggedContext) {
            loggedContext = true;
            JOIN_LOGI("DIAG PLAYER_PURESYNC skipped at bitstream 0x0030: "
                      "SNativeTaskLocomotionSync (NativeTaskLocomotionPresentation 0x3C). "
                      "Not read: HasJetPack, GetContactEntity and IsOnGround (bIsOnGround stays set). "
                      "WriteCameraOrientation (CNetAPI.cpp 1023-1114) has no camera matrix; "
                      "the 8-bit yaw below reuses GetCameraRotation and pitch stays level.");
        }
        w.Write(ServerWorld::SyncTimeContext(ServerWorld::LocalPlayer()));
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
        if (releaseVelocity) {
            // SVelocitySync::Write with modulus 0 (SyncStructures.h 582-587): one clear bit.
            uint8_t zeroBit = 0;
            w.WriteBits(&zeroBit, 1);
        } else if (moving) {
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
        // SPlayerHealthSync: SFloatAsBitsSync<8>(0, 255, preserveGreaterThanMin) (SyncStructures.h 189-193).
        // A raw clamp to 200 drops health above 200 and a value in (0, 0.5) to 0.
        auto playerHealthByte = [](float value) -> uint8_t {
            if (!std::isfinite(value) || value <= 0.f) return 0;
            float alpha = value / 255.f;
            if (alpha > 1.f) alpha = 1.f;
            int byte = static_cast<int>(std::lround(static_cast<double>(alpha) * 255.0));
            if (byte <= 0) return 1;
            if (byte > 255) return 255;
            return static_cast<uint8_t>(byte);
        };
        w.Write(playerHealthByte(health));
        // SPlayerArmorSync scales 0..127.5 to 0..255.
        const float armor = std::clamp(WorldRPG::Game::CopyLocalArmor(), 0.f, 127.5f);
        w.Write(static_cast<uint8_t>(std::lround(armor * 2.f)));
        low(w, cameraBits, 12);                     // SCameraRotationSync = GetCameraRotation
        const auto camZ = static_cast<uint8_t>(std::lround(255.0 * ((foot.camera + pi) / (2.0 * pi))) & 0xFF);
        w.Write(camZ);
        w.Write(static_cast<uint8_t>(128));         // camera rot X, level
        low(w, 0, 1);                               // camera offset is relative
        low(w, 0, 2);                               // 3-bit offset table
        low(w, 4, 3);
        low(w, 4, 3);
        low(w, 4, 3);
        if (localWeapon.valid) PlayerWeaponSync::WritePure(w, localWeapon, keys);
        const auto damage = WorldRPG::Game::PeekDamage();
        if (damage.serial) {
            low(w, 1, 1);
            uint32_t idBits = damage.attacker & 0x1FFFFu;
            uint8_t idBytes[4] = {};
            std::memcpy(idBytes, &idBits, 4);
            w.WriteBits(idBytes, 17);
            low(w, damage.weapon & 0x3Fu, 6);
            low(w, WorldRPG::Combat::EncodeBodypart(damage.zone), 3);  // SBodypartSync
        } else {
            low(w, 0, 1);                           // no damage info
        }
        if (net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                            PacketPriority::MEDIUM, PacketReliability::UNRELIABLE_SEQUENCED)) {
            pureSyncSentThisPulse = true;
            lastSyncMs = now;
            WorldRPG::Game::AcknowledgeDamage(damage.serial);
            lastWeaponButtons = weaponButtons;
            if (weaponButtons & PlayerWeaponSync::kFire) WorldRPG::Game::ConsumeLocalAttack(foot.attackPress);
            if (foot.jump) WorldRPG::Game::ConsumeLocalJump(foot.jumpPress);
            lastOnFootStickX = stickX;
            lastOnFootStickY = stickY;
            if (releaseVelocity) pendingZeroVelocity = false;
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

    // CNetAPI::WriteKeysync (CNetAPI.cpp 729-828) for an on-foot player.
    // CNetAPI::DoPulse (381-397) sends this only when that pulse did not send puresync.
    // A pad release (IsSmallKeySyncNeeded 507: the stick left the analog band) is sent at once.
    void SendKeySync(const PlayerWeaponSync::LocalWeapon& localWeapon) {
        if (!net || !net->IsConnected() || WorldRPG::Game::LocalPlacementPending()) return;
        // Driving a server-confirmed car: the vehicle form (SendDriverKeySync), as SendPureSync switches to packet 33.
        // The on-foot release below does not run in the driver branch.
        {
            WorldRPG::Game::LocalCarState car;
            if (WorldRPG::Game::CopyLocalCar(car) && ConfirmedSeat(info.localPlayerId, car)) {
                SendDriverKeySync(car, localWeapon);
                return;
            }
        }
        if (pureSyncSentThisPulse) return;
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
        const bool release = foot.padStickZero && (lastOnFootStickX != 0 || lastOnFootStickY != 0);
        const uint8_t weaponButtons = WeaponButtons(foot, localWeapon);
        const uint32_t now = NowMs();
        if (!release) {
            if (now - lastSyncMs < 40 || now - lastKeyMs < 50) return;
            // A fire release is sent too, so desktop sees separate presses (combos start on the press edge).
            if (std::fabs(turn) < 0.05f && speed < 0.02f && !weaponButtons && !lastWeaponButtons &&
                !foot.duck && !foot.jump) return;
        }
        auto low = [](BitStreamWriter& w, uint32_t value, int bits) {
            uint8_t bytes[4] = {};
            for (int i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
            w.WriteBits(bytes, static_cast<size_t>(bits));
        };
        const auto angleBits = static_cast<uint32_t>(std::lround(4095.0 * ((heading + pi) / (2.0 * pi)))) & 0xFFFu;
        const auto cameraBits = static_cast<uint32_t>(std::lround(4095.0 * ((foot.camera + pi) / (2.0 * pi)))) & 0xFFFu;
        uint8_t keys = weaponButtons;
        if (foot.duck) keys |= 0x40;
        // SFullKeysyncSync bButtonCross (0x08, sprint) and bButtonSquare (0x04, jump); SyncStructures.h 1004-1012.
        if (foot.sprint) keys |= 0x08;
        if (foot.jump) keys |= 0x04;
        // m_bPedWalk stays clear (see SendPureSync).
        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(31));
        w.Write(keys);
        low(w, 0, 1);
        low(w, 0, 1);
        w.Write(static_cast<uint8_t>(foot.stickX));
        w.Write(static_cast<uint8_t>(foot.stickY));
        low(w, angleBits, 12);
        low(w, cameraBits, 12);
        low(w, (foot.duck ? 1u : 0u) | (localWeapon.aim.akimboUp ? 4u : 0u), 4);
        if (PlayerWeaponSync::HasAimOrFire(keys)) PlayerWeaponSync::WriteKeys(w, localWeapon);
        if (net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), w,
                            PacketPriority::MEDIUM, PacketReliability::UNRELIABLE_SEQUENCED)) {
            lastKeyMs = now;
            lastWeaponButtons = weaponButtons;
            if (weaponButtons & PlayerWeaponSync::kFire) WorldRPG::Game::ConsumeLocalAttack(foot.attackPress);
            lastKeyHeading = heading;
            if (foot.jump) WorldRPG::Game::ConsumeLocalJump(foot.jumpPress);
            lastOnFootStickX = foot.stickX;
            lastOnFootStickY = foot.stickY;
            if (release) pendingZeroVelocity = true;
        }
    }

    // CPedSync::DoPulse (CPedSync.cpp 125-165) calls Update every iPedSync milliseconds
    // (CTickRateSettings.h 19, default 400). Update (733-767) writes one PED_SYNC for the
    // hosted list. WritePedInformation (924-1133) sets a flag only when that field changed
    // and returns without bytes when both flag bytes are 0.
    // Bitstream stays 0x0030. NativeTaskLocomotionPresentation is 0x3C and
    // NativeTaskWeaponPresentation is 0x3D (bitstream.h), so flags2 0x02 and 0x04 are not set.
    // UpdateNativeTaskAnimationPresentation returns immediately at this version for the same reason.
    void SendPedSync() {
        if (!net || !net->IsConnected()) return;
        const uint32_t now = NowMs();
        if (now - lastPedMs < 400) return;
        lastPedMs = now;
        static bool loggedGaps = false;
        if (!loggedGaps) {
            loggedGaps = true;
            JOIN_LOGI("DIAG PED_SYNC: flags2 0x02/0x04 stay clear at bitstream 0x0030 "
                      "(NativeTaskLocomotionPresentation 0x3C, NativeTaskWeaponPresentation 0x3D). "
                      "flags2 0x01 needs GetCameraRotation (TheCamera yaw is not hooked). "
                      "ucFlags 0x20 needs CPedSA::IsOnFire (pFireOnPed), 0x40 needs CPedSA::IsInWater "
                      "(TASK_COMPLEX_IN_WATER), 0x60 needs IsReloadingWeapon, 0x80 needs HasSyncedAnim. "
                      "Those reads are missing, so the flags stay clear. "
                      "Sync time context is the ENTITY_ADD byte; CPedRPCs/CElementRPCs SetSyncTimeContext "
                      "is not applied. The interval stays 400 ms; CWorldRPCs does not update iPedSync. "
                      "fCurrentRotation is not located; rotation is the matrix heading ReadBodyPos samples.");
        }
        // Same cap as the ped body quota. Update walks the whole sync list; the phone has at most 48 bodies.
        WorldRPG::Game::HostedPed peds[48];
        const int count = WorldRPG::Game::CopyHostedPeds(peds, 48);
        constexpr float kFloatEpsilon = 0.0001f;  // CVector::FLOAT_EPSILON (CVector.h 26, operator== 282-284)
        constexpr float kPi = 3.14159265f;
        constexpr float kSyncPositionLimit = 100000.f;  // SYNC_POSITION_LIMIT (SyncStructures.h 28)
        auto vectorChanged = [kFloatEpsilon](float ax, float ay, float az, float bx, float by, float bz) {
            return std::fabs(ax - bx) >= kFloatEpsilon || std::fabs(ay - by) >= kFloatEpsilon ||
                   std::fabs(az - bz) >= kFloatEpsilon;
        };
        auto fixed24 = [](BitStreamWriter& w, float coord) {
            // SFloatSync<14, 10> at bitstream 0x0030 (SPositionComponentSync): value * 1024, 24 bits.
            int32_t fixed = static_cast<int32_t>(std::lround(static_cast<double>(coord) * 1024.0));
            if (fixed > 8388607) fixed = 8388607;
            if (fixed < -8388608) fixed = -8388608;
            WriteFieldLsb(w, static_cast<uint32_t>(fixed) & 0xFFFFFFu, 24);
        };
        // SPedRotationSync (SyncStructures.h 651-662): one wrap into [-pi, pi], then 16-bit unlerp.
        auto writeRotation16 = [kPi](BitStreamWriter& w, float rotation) {
            if (rotation < -kPi) rotation += kPi * 2.f;
            else if (rotation > kPi) rotation -= kPi * 2.f;
            float alpha = (rotation + kPi) / (kPi * 2.f);
            if (alpha < 0.f) alpha = 0.f;
            if (alpha > 1.f) alpha = 1.f;
            const auto bits = static_cast<uint32_t>(std::lround(65535.0 * static_cast<double>(alpha)));
            WriteFieldLsb(w, bits > 65535u ? 65535u : bits, 16);
        };
        // SCameraRotationSync (SyncStructures.h 683-687): SFloatAsBitsSync<12>(-pi, pi, false, true).
        auto writeCamera12 = [kPi](BitStreamWriter& w, float rotation) {
            const float size = kPi * 2.f;
            rotation = rotation - size * std::floor((rotation + kPi) / size);
            float alpha = (rotation + kPi) / size;
            if (alpha < 0.f) alpha = 0.f;
            if (alpha > 1.f) alpha = 1.f;
            const auto bits = static_cast<uint32_t>(std::lround(4095.0 * static_cast<double>(alpha)));
            WriteFieldLsb(w, bits > 4095u ? 4095u : bits, 12);
        };
        BitStreamWriter w;
        w.Write(static_cast<uint8_t>(63));
        bool wrote = false;
        std::unordered_map<uint32_t, PedLastSynced> next;
        for (int i = 0; i < count; ++i) {
            const auto& ped = peds[i];
            if (!WorldRPG::ServerWorld::ActorHosted(ped.id)) continue;
            const auto found = lastPed.find(ped.id);
            const bool firstSeen = found == lastPed.end();
            PedLastSynced last = firstSeen ? PedLastSynced{} : found->second;
            // HasSyncedAnim is not hooked, so the synced-animation transform lock (931-936) stays off
            // and ucFlags 0x80 stays clear.
            const bool lockSyncedAnimationTransform = false;
            // The reads are not hooked, so these stay at the unset state. A later hook replaces the constants.
            const bool onFire = false;       // CPedSA::IsOnFire (pFireOnPed)
            const bool inWater = false;      // CPedSA::IsInWater (TASK_COMPLEX_IN_WATER)
            const bool reloading = false;    // CClientPed::IsReloadingWeapon
            const float camera = 0.f;        // CClientPed::GetCameraRotation
            uint8_t ucFlags = 0;
            if (!lockSyncedAnimationTransform && vectorChanged(ped.x, ped.y, ped.z, last.x, last.y, last.z))
                ucFlags |= 0x01;
            if (!lockSyncedAnimationTransform && ped.heading != last.rotation)
                ucFlags |= 0x02;
            if (!lockSyncedAnimationTransform && vectorChanged(ped.vx, ped.vy, ped.vz, last.vx, last.vy, last.vz))
                ucFlags |= 0x04;
            // Last health and armour start at 0. A body whose value is also 0 would not count as a
            // change, so the first sample is published anyway (desktop's first compare is against
            // an uninitialized SLastSyncedPedData).
            if (ped.bodyVitals && (firstSeen || ped.health != last.health))
                ucFlags |= 0x08;
            if (ped.bodyVitals && (firstSeen || ped.armor != last.armor))
                ucFlags |= 0x10;
            if (onFire != last.onFire)
                ucFlags |= 0x20;
            if (inWater != last.inWater)
                ucFlags |= 0x40;
            if (reloading != last.reloading)
                ucFlags |= 0x60;
            uint8_t flags2 = 0;
            // IsNearlyEqual(GetCameraRotation(), last, numeric_limits<float>::epsilon()). No yaw hook, so no bit.
            if (!lockSyncedAnimationTransform && camera != last.camera)
                flags2 |= 0x01;
            if (ucFlags != 0 || flags2 != 0) {
                wrote = true;
                WriteFieldLsb(w, ped.id & 0x1FFFFu, 17);
                w.Write(WorldRPG::ServerWorld::SyncTimeContext(ped.id));
                w.Write(ucFlags);
                w.Write(flags2);
                if (ucFlags & 0x01) {
                    fixed24(w, ped.x);
                    fixed24(w, ped.y);
                    float z = ped.z;
                    if (z > kSyncPositionLimit - 1.f) z = kSyncPositionLimit - 1.f;
                    if (z < -kSyncPositionLimit + 1.f) z = -kSyncPositionLimit + 1.f;
                    w.Write(z);
                    last.x = ped.x;
                    last.y = ped.y;
                    last.z = ped.z;
                }
                if (ucFlags & 0x02) {
                    writeRotation16(w, ped.heading);
                    last.rotation = ped.heading;
                }
                if (ucFlags & 0x04) {
                    const float speed = std::sqrt(ped.vx * ped.vx + ped.vy * ped.vy + ped.vz * ped.vz);
                    if (speed == 0.f) {
                        WriteBit(w, false);
                    } else {
                        WriteBit(w, true);
                        w.Write(speed);
                        w.WriteNormVector(ped.vx / speed, ped.vy / speed, ped.vz / speed);
                    }
                    last.vx = ped.vx;
                    last.vy = ped.vy;
                    last.vz = ped.vz;
                }
                if (ucFlags & 0x08) {
                    w.Write(ped.health);
                    last.health = ped.health;
                }
                if (ucFlags & 0x10) {
                    w.Write(ped.armor);
                    last.armor = ped.armor;
                }
                if (flags2 & 0x01) {
                    writeCamera12(w, camera);
                    last.camera = camera;
                }
                if (ucFlags & 0x20) {
                    WriteBit(w, onFire);
                    last.onFire = onFire;
                }
                if (ucFlags & 0x40) {
                    WriteBit(w, inWater);
                    last.inWater = inWater;
                }
                if (ucFlags & 0x60) {
                    WriteBit(w, reloading);
                    last.reloading = reloading;
                }
            }
            next.emplace(ped.id, last);
        }
        lastPed.swap(next);
        // Update sends the packet whenever the sync list is non-empty, including when every ped
        // returned early. CPedSyncPacket::Read then returns false (m_Syncs empty) and
        // CPacketTranslator.cpp 251-254 drops it. The phone sends only when a ped wrote bytes.
        if (!wrote) return;
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
    // Derailed, in-water and trailer fields have no verified Android source yet.
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
            if (!WorldRPG::ServerWorld::ActorHosted(car.id)) continue;
            const WorldRPG::VehicleSync::Vec3 position{car.x, car.y, car.z};
            const WorldRPG::VehicleSync::Vec3 rotation{car.rotX, car.rotY, car.rotZ};
            const WorldRPG::VehicleSync::Vec3 velocity{car.vx, car.vy, car.vz};
            const WorldRPG::VehicleSync::Vec3 turnVelocity{car.tx, car.ty, car.tz};

            const auto found = lastUnoccupied.find(car.id);
            const bool firstSeen = found == lastUnoccupied.end();
            UnoccupiedLastSynced last = firstSeen ? UnoccupiedLastSynced{} : found->second;

            WorldRPG::VehicleSync::UnoccupiedUpdate update;
            update.vehicleId = car.id;
            update.timeContext = WorldRPG::ServerWorld::SyncTimeContext(car.id);
            update.engineOn = car.engineOn;
            const bool engineChanged = firstSeen || last.engineOn != car.engineOn;
            last.engineOn = car.engineOn;

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
                update.syncHealth || engineChanged) updates.push_back(update);
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
        if (g_inOutPending != VehicleInOutPending::None && NowMs() - g_inOutSinceMs > kVehicleInOutTimeoutMs) {
            JOIN_LOGE("DIAG VehInOut: no reply for veh=%u in %u ms (pending=%d); next seat change goes ahead",
                      g_inOutVehicle, kVehicleInOutTimeoutMs, static_cast<int>(g_inOutPending));
            g_inOutPending = VehicleInOutPending::None;
        }
        WorldRPG::Game::LocalVehicleEvent ev;
        while (g_inOutPending == VehicleInOutPending::None && WorldRPG::Game::ConsumeLocalVehicleEvent(ev)) {
            // DIAG: trains are not requested. Once the server seats the ped it expects packet 33 with the train block
            // (CNetAPI::WriteVehiclePuresync, CNetAPI.cpp 1643-1654: train position, direction, speed, track), and no
            // hook samples those from CTrain yet. A seated ped without packet 33 sends nothing the server relays, so
            // other players would see it frozen where it boarded. Unrequested, the server keeps the ped on foot and
            // relays the on-foot sync that SendPureSync falls back to. The leave is skipped too: no seat was taken.
            uint32_t seatedVehicle = 0;
            uint8_t seatedIndex = 0;
            const bool seated = RemoteVehicle::FindOccupant(info.localPlayerId, seatedVehicle, seatedIndex);
            if ((ev.entered && seated && seatedVehicle == ev.vehicleId && seatedIndex == ev.seat) ||
                (!ev.entered && (!seated || seatedVehicle != ev.vehicleId || seatedIndex != ev.seat))) continue;
            const int evModel = WorldRPG::ServerWorld::ModelOf(ev.vehicleId);
            if (evModel >= 0 && WorldRPG::Entity::VehicleModels::IsTrainModel(static_cast<uint32_t>(evModel))) {
                JOIN_LOGI("DIAG VehInOut: %s train veh=%u model=%d not sent (train puresync block missing)",
                          ev.entered ? "request_in for" : "request_out for", ev.vehicleId, evModel);
                continue;
            }
            BitStreamWriter w;
            w.Write(static_cast<uint8_t>(58));  // PACKET_ID_VEHICLE_INOUT (Shared/sdk/net/Packets.h:96)
            WriteFieldLsb(w, info.localPlayerId & 0x1FFFFu, NEON_ELEMENT_ID_BITS);
            WriteFieldLsb(w, ev.vehicleId & 0x1FFFFu, NEON_ELEMENT_ID_BITS);
            if (ev.entered) {
                WriteFieldLsb(w, 0, 4);  // action VEHICLE_REQUEST_IN = 0 (CClientGame.h:174)
                WriteFieldLsb(w, ev.seat, 4);
                WriteFieldLsb(w, 0, 1);  // isOnWater false
                WriteFieldLsb(w, std::min<unsigned>(ev.seat, 3), 3);
            } else {
                WriteFieldLsb(w, 3, 4);  // action VEHICLE_REQUEST_OUT = 3 (CClientGame.h:177); no door bits
            }
            if (!SendUpe(w)) {
                JOIN_LOGE("VehInOut: send %s failed veh=%u", ev.entered ? "request_in" : "request_out", ev.vehicleId);
                continue;
            }
            SetVehicleInOutPending(ev.entered ? VehicleInOutPending::RequestIn : VehicleInOutPending::RequestOut);
            g_inOutVehicle = ev.vehicleId;
            JOIN_LOGI("VehInOut: sent %s ped=%u veh=%u", ev.entered ? "request_in" : "request_out",
                      info.localPlayerId, ev.vehicleId);
        }
    }

    bool SendUpe(BitStreamWriter& body) {
        if (!net) return false;
        return net->SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM), body,
                               PacketPriority::HIGH, PacketReliability::RELIABLE_ORDERED);
    }

    bool SendJoinData() {
        BitStreamWriter w;
        const bool valid = BuildJoinData(info.joinProfile, nickname, password, identityTicket, w, release);
        identityTicket.clear(); // Tickets are consumed once, including failed sends.
        if (!valid) return false;
        JOIN_LOGI("NeonJoin: sending logical packet %u profile=%s platform=android bitstream=0x30",
                  static_cast<unsigned>(w.GetData()[0]),
                  info.joinProfile == JoinProfile::LegacyMobile ? "legacy-mobile" : "standard");
        return SendUpe(w);
    }

    void FinishJoinData() {
        if (!SendJoinData()) {
            Fail("failed to send PLAYER_JOINDATA", DisconnectMessages::TransportFailure(DisconnectMessages::Transport::Other, false));
            return;
        }
        SetState(JoinState::JoinDataSent, "PLAYER_JOINDATA sent");
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
            Fail("MOD_NAME truncated", DisconnectMessages::ProtocolError(1));
            return;
        }
        info.serverBitStreamVersion = bsVersion;
        info.modName = modName;

        if (r.GetRemainingBits() >= 8) {
            uint8_t mode = 0;
            if (!r.Read(mode) || mode > 2) {
                Fail("MOD_NAME bad neon auth mode", DisconnectMessages::ProtocolError(1));
                return;
            }
            info.authMode = static_cast<NeonAuthMode>(mode);
            if (info.authMode != NeonAuthMode::Disabled) {
                if (!r.ReadString(info.neonServerId, 128)) {
                    Fail("MOD_NAME truncated neon server id", DisconnectMessages::ProtocolError(1));
                    return;
                }
            }
        }

        // Desktop CConnectManager.cpp:628: the net module learns the server's
        // bitstream version once MOD_NAME is validated (no-op off PC Network).
        if (net) net->SetServerBitStreamVersion(bsVersion);

        if (modName != "deathmatch") {
            Fail("unsupported mod '" + modName + "'", DisconnectMessages::Plain("Error", "This server runs the mod '" + modName + "', which this client cannot join."));
            return;
        }
        const bool standard = info.joinProfile == JoinProfile::Standard;
        // Release 189 writes one water flag bit (server CEntityAddPacket for
        // SetRelease189Protocol peers); current servers write two.
        WorldRPG::ServerWorld::ConfigureEntityAdd(standard && release != NetcodeRelease::Release189,
                                                  standard && bsVersion >= 0x0031);
        JOIN_LOGI("NeonJoin: MOD_NAME mod=%s bs=0x%X auth=%u",
                  modName.c_str(), bsVersion, static_cast<unsigned>(info.authMode));
        if (info.authMode != NeonAuthMode::Disabled && identityCallback) {
            identityRequestId = ++g_identitySequence;
            SetState(JoinState::WaitingIdentity, "authorizing SA:O server connection");
            identityCallback(identityRequestId, info.neonServerId, net->GetPeerEndpoint(), info.authMode);
            return;
        }
        if (info.authMode == NeonAuthMode::Required) {
            Fail("SA:O identity callback unavailable", DisconnectMessages::Plain("SA:O account", "Sign in to your SA:O account before connecting to this server."));
            return;
        }
        FinishJoinData();
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
            Fail("SERVER_JOIN_COMPLETE version field truncated", DisconnectMessages::ProtocolError(1));
            return;
        }
        JOIN_LOGI("NeonJoin: SERVER_JOIN_COMPLETE [%s] sortable=[%s]",
                  info.serverVersionText.c_str(), info.serverVersionSortable.c_str());
        // Desktop CPacketHandler::Packet_ServerConnected (CPacketHandler.cpp:344) opens the loading screen here.
        WorldRPG::ConnectionLoading::Begin("Starting San Andreas");

        if (!SendRpc(NeonServerRPC::PLAYER_INGAME_NOTICE)) {
            Fail("failed to send RPC PLAYER_INGAME_NOTICE", DisconnectMessages::TransportFailure(DisconnectMessages::Transport::Other, false));
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
        // Desktop CPacketHandler::Packet_ServerJoined (CPacketHandler.cpp:399).
        WorldRPG::ConnectionLoading::Update(WorldRPG::ConnectionLoading::Phase::World, "Preparing server data");
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
            Fail("SERVER_JOINEDGAME truncated", DisconnectMessages::ProtocolError(3));
            return;
        }
        info.voiceEnabled = voice;
        info.voiceSampleRate = sampleRate;
        info.voiceQuality = quality;
        info.voiceBitrate = bitrate;
        info.fakeLagCommand = fakelag;
        info.httpDownloadType = httpType;
        // This join id is the local ped for the whole session. PacketHandler accepts a PLAYER_SPAWN only for it.
        if (info.localPlayerId != 0xFFFFFFFFu) {
            WorldRPG::ServerWorld::SetLocalPlayer(info.localPlayerId);
            WorldRPG::ServerWorld::NotePlayerNick(info.localPlayerId, nickname);
        }

        JOIN_LOGI("NeonJoin: SERVER_JOINEDGAME id=%u root=%u name=[%s] http=%u port=%u",
                  info.localPlayerId, info.rootElementId, info.serverName.c_str(),
                  httpType, info.httpPort);

        if (!SendRpc(NeonServerRPC::INITIAL_DATA_STREAM)) {
            Fail("failed to send RPC INITIAL_DATA_STREAM", DisconnectMessages::TransportFailure(DisconnectMessages::Transport::Other, false));
            return;
        }
        net->MarkSessionAccepted();
        SetState(JoinState::Complete, "joined; waiting initial data stream");
    }

    // SERVER_DISCONNECTED, desktop CPacketHandler::Packet_ServerDisconnected (CPacketHandler.cpp:545-750):
    // [5b type][String duration for BANNED_SERIAL, BANNED_IP, BAN][String message][String trouble link].
    // INVALID_PASSWORD reads nothing more on desktop (it opens the password window instead).
    void HandleServerDisconnected(const uint8_t* body, size_t length, size_t bitLength = 0) {
        BitStreamReader r(body, length, bitLength);
        uint8_t type = 0;
        if (r.ReadBits(&type, 5)) {
            info.disconnectType = type & 0x1F;
        }
        std::string duration;
        if (DisconnectMessages::HasDuration(info.disconnectType)) r.ReadString(duration, 64);
        std::string troubleLink;
        if (info.disconnectType != DisconnectMessages::INVALID_PASSWORD) {
            r.ReadString(info.disconnectReason, 256);
            r.ReadString(troubleLink, 256);
        }
        // Server CGame::Packet_PlayerJoinData refuses an unknown netcode with a
        // custom "requires the current ... network protocol." message. In
        // Automatic mode that is the cue to retry once with the other release.
        // The stock version refusals (VERSION_MISMATCH, SERVER_NEWER, ...) are
        // about the MTA product version, which the other netcode cannot fix, so
        // they are shown at once instead of after a second netc load.
        const bool versionRefusal = info.disconnectReason.find("network protocol") != std::string::npos;
        if (autoRelease && !fallbackUsed && state != JoinState::Complete && versionRefusal) {
            fallbackUsed = true;
            fallbackPending = true;
            JOIN_LOGI("NeonJoin: server refused netcode 0x%X; retrying with the other release",
                      release == NetcodeRelease::Release189 ? NEON_RELEASE189_NETCODE_VERSION : NEON_NETCODE_VERSION);
            SetState(JoinState::Idle, "server release differs; reconnecting");
            return;
        }
        const DisconnectMessages::Box box =
            DisconnectMessages::ServerDisconnected(info.disconnectType, info.disconnectReason, duration);
        Fail("SERVER_DISCONNECTED type " + std::to_string(info.disconnectType) + ": " + info.disconnectReason, box);
    }
};

NeonJoinClient::NeonJoinClient() : pImpl_(std::make_unique<Impl>()) {}
NeonJoinClient::~NeonJoinClient() = default;

bool NeonJoinClient::Begin(RakNetWrapper& net, const std::string& nickname, const std::string& password,
                           JoinProfile profile, NetcodeRelease release, NetcodeRelease hint) {
    const std::string normalized = NormalizeAscii(nickname);
    if (normalized.empty() || normalized.size() > NEON_MAX_PLAYER_NICK_LENGTH) {
        pImpl_->Fail("invalid nickname length", DisconnectMessages::InvalidNick());
        return false;
    }
    pImpl_->net = &net;
    pImpl_->nickname = normalized;
    pImpl_->password = password;
    pImpl_->lastWeaponSlot = 0;
    pImpl_->lastWeaponButtons = 0;
    pImpl_->lastSentDriverKeys = {};
    pImpl_->lastSentLookLeft = pImpl_->lastSentLookRight = false;
    pImpl_->lastOnFootStickX = pImpl_->lastOnFootStickY = 0;
    pImpl_->pendingZeroVelocity = pImpl_->pureSyncSentThisPulse = false;
    pImpl_->lastSyncMs = pImpl_->lastKeyMs = 0;
    pImpl_->lastKeyHeading = 0.f;
    pImpl_->bulletOrder = 0;
    pImpl_->identityTicket.clear();
    pImpl_->identityRequestId = 0;
    pImpl_->info = NeonJoinInfo{};
    pImpl_->info.joinProfile = SelectJoinProfile(profile);
    // A fallback attempt arrives as Automatic with the other release as hint;
    // it may not fall back again.
    const bool isFallback = pImpl_->fallbackArmed;
    pImpl_->fallbackArmed = false;
    pImpl_->autoRelease = release == NetcodeRelease::Automatic;
    if (!isFallback) pImpl_->fallbackUsed = false;
    pImpl_->release = release != NetcodeRelease::Automatic ? release
                      : hint == NetcodeRelease::Release189 ? NetcodeRelease::Release189 : NetcodeRelease::Current;
    pImpl_->SetState(JoinState::WaitingRakNet, "waiting for RakNet connect");
    return true;
}

bool NeonJoinClient::TakeReleaseFallback() {
    // One-shot: a reconnect that fails is not retried every frame.
    if (!pImpl_->fallbackPending) return false;
    pImpl_->fallbackPending = false;
    pImpl_->fallbackArmed = true;  // the next Begin is the fallback attempt
    return true;
}

NetcodeRelease NeonJoinClient::FallbackRelease() const {
    return pImpl_->release == NetcodeRelease::Release189 ? NetcodeRelease::Current : NetcodeRelease::Release189;
}

NetcodeRelease NeonJoinClient::ActiveRelease() const { return pImpl_->release; }

void NeonJoinClient::SetIdentityCallback(IdentityCallback callback) {
    pImpl_->identityCallback = std::move(callback);
}

bool NeonJoinClient::CancelIdentityRequest(uint64_t requestId) {
    if (!requestId || pImpl_->state != JoinState::WaitingIdentity || pImpl_->identityRequestId != requestId) return false;
    pImpl_->identityRequestId = 0;
    pImpl_->identityTicket.clear();
    pImpl_->state = JoinState::Idle;
    return true;
}

bool NeonJoinClient::ProvideIdentityTicket(uint64_t requestId, const std::string& ticket, const std::string& error) {
    Impl& d = *pImpl_;
    if (d.state != JoinState::WaitingIdentity || !requestId || d.identityRequestId != requestId) return false;
    if (!error.empty()) {
        d.Fail("Neon Identity authorization failed", DisconnectMessages::Plain("SA:O account", error));
    } else if (ticket.size() > 4096 || ticket.find('\0') != std::string::npos) {
        d.Fail("invalid Neon ticket length", DisconnectMessages::Plain("SA:O account", "The account service returned an invalid connection ticket."));
    } else if (ticket.empty() && d.info.authMode == NeonAuthMode::Required) {
        d.Fail("required Neon ticket missing", DisconnectMessages::Plain("SA:O account", "Sign in to your SA:O account before connecting to this server."));
    } else if (!ticket.empty() && d.info.joinProfile == JoinProfile::LegacyMobile) {
        d.Fail("legacy bridge cannot carry Neon ticket", DisconnectMessages::Plain("SA:O account", "This server's legacy mobile bridge cannot carry SA:O tickets. Select Standard connection mode or ask the server owner to update the bridge."));
    } else {
        d.identityTicket = ticket;
        d.FinishJoinData();
    }
    return true;
}

namespace {
bool SendChatLine(RakNetWrapper& net, const std::string& command);
}

void NeonJoinClient::ForceConnectionLost() {
    if (pImpl_->state == JoinState::Complete) pImpl_->Fail("Connection lost (local /disconnect)", DisconnectMessages::Plain("Disconnected", "You disconnected from the server."));
}

void NeonJoinClient::Pulse() {
    Impl& d = *pImpl_;
    g_joinedNet = d.state == JoinState::Complete ? d.net : nullptr;
    if (!d.net) return;

    switch (d.state) {
        case JoinState::WaitingRakNet:
            // PC Network: the step clock starts once netc is loaded and its
            // RakNet is up; the transport times out its own load and connect.
            if (d.net->IsTransportStarting()) {
                // netc's own steps have long timeouts (DllMain and InitNetInterface
                // 60 s each); the whole load gets this budget.
                constexpr uint32_t kPcNetworkLoadBudgetMs = 90000;
                if (NowMs() - d.stateSinceMs > kPcNetworkLoadBudgetMs) {
                    d.Fail("PC network module took too long to start", DisconnectMessages::ConnectTimedOut());
                    break;
                }
                d.deadlineMs = NowMs() + JOIN_STEP_TIMEOUT_MS;
                break;
            }
            if (d.net->IsRakNetConnected()) {
                // The server net module emits PLAYER_JOIN when NIC arrives.
                // The desktop waits for MOD_NAME; it sends no application join.
                d.SetState(JoinState::WaitingModName, "transport ready; waiting MOD_NAME");
            } else if (NowMs() > d.deadlineMs) {
                d.Fail("timeout waiting for RakNet connect", DisconnectMessages::ConnectTimedOut());
            }
            break;
        case JoinState::WaitingModName:
        case JoinState::WaitingIdentity:
        case JoinState::JoinDataSent:
            if (NowMs() > d.deadlineMs) {
                d.Fail(std::string("timeout in state ") + JoinStateName(d.state), DisconnectMessages::JoinTimedOut());
            }
            break;
        case JoinState::InGameNoticeSent:
            if (d.net && d.net->IsRakNetConnected()) {
                const uint32_t now = NowMs();
                if (now - d.lastSyncMs >= 1000) {
                    d.SendPureSync({});
                }
            }
            if (NowMs() > d.deadlineMs) {
                d.Fail(std::string("timeout in state ") + JoinStateName(d.state), DisconnectMessages::JoinTimedOut());
            }
            break;
        case JoinState::Complete:
            if (!d.net->IsRakNetConnected()) {
                // The transport dropped after the join. Surface it so the launcher
                // can offer Retry / Quit (P3.1).
                d.Fail("transport dropped after join", DisconnectMessages::ConnectionLost());
                break;
            }
            // Flush queued shots before pure sync publishes the post-shot reserve.
            // CGame::Packet_Bulletsync checks total ammo even for the final round.
            if (d.SendBulletSync()) {
                const auto localWeapon = WorldRPG::Game::CopyLocalWeapon();
                d.SendWeaponSwitch(localWeapon);
                d.SendPureSync(localWeapon);
                d.SendKeySync(localWeapon);
            }
            d.SendPedSync();
            d.SendUnoccupiedVehicleSync();
            d.SendVehicleInOut();
            d.SendPedWasted();
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
        // The server is still waiting for this notify and will not answer the request
        // again; end it so the next seat change is not blocked behind it.
        JOIN_LOGE("VehInOut: send %s failed veh=%u", action == 1 ? "notify_in" : "notify_out", vehicleId);
        g_inOutPending = VehicleInOutPending::None;
        return false;
    }
    if (vehicleId != g_inOutVehicle) {
        JOIN_LOGI("VehInOut: reply for veh=%u differs from pending veh=%u", vehicleId, g_inOutVehicle);
    }
    SetVehicleInOutPending(action == 1 ? VehicleInOutPending::NotifyIn : VehicleInOutPending::NotifyOut);
    // NOTIFY_IN seats the local ped on the server; NOTIFY_OUT unseats it.

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

    JOIN_LOGI("VehInOut: sent notify_jack_abort ped=%u veh=%u door=%u ok=%d", pedId, vehicleId,
              static_cast<unsigned>(door), sent ? 1 : 0);
    return sent;
}

// VEHICLE_ATTEMPT_FAILED (action 9) and NOTIFY_*_RETURN for the local ped end the outstanding request.
// The next queued change can now begin.
void ClearVehicleInOutPending() {
    g_inOutPending = VehicleInOutPending::None;
}

void NeonJoinClient::OnTransportFailure(const std::string& reason) {
    if (pImpl_->state == JoinState::Idle || pImpl_->state == JoinState::Failed) return;
    pImpl_->Fail(reason.empty() ? "Transport connection failed" : reason,
                 DisconnectMessages::TransportFailure(DisconnectMessages::ClassifyTransport(reason), pImpl_->state == JoinState::Complete));
}

namespace {

// Ids CNetAPI::ProcessPacket returns true for with no m_Status check
// (CNetAPI.cpp:39-226), plus the ped and unoccupied ids CPacketHandler
// consumes the same way (CPedSync.cpp:99-122, CUnoccupiedVehicleSync.cpp:31-54).
// CConnectManager aborts every other id while it is still the handler
// (CConnectManager.cpp:687-698), so these are not ignored before MOD_NAME.
bool IgnoredInboundSyncWhileJoining(uint8_t gameId, const char*& name) {
    switch (gameId) {
        case 31: name = "PLAYER_KEYSYNC"; return true;
        case 32: name = "PLAYER_PURESYNC"; return true;
        case 33: name = "PLAYER_VEHICLE_PURESYNC"; return true;
        case 34: name = "LIGHTSYNC"; return true;
        case 35: name = "VEHICLE_RESYNC"; return true;
        case 36: name = "RETURN_SYNC"; return true;
        case 54: name = "UNOCCUPIED_VEHICLE_STARTSYNC"; return true;
        case 55: name = "UNOCCUPIED_VEHICLE_STOPSYNC"; return true;
        case 56: name = "UNOCCUPIED_VEHICLE_SYNC"; return true;
        case 61: name = "PED_STARTSYNC"; return true;
        case 62: name = "PED_STOPSYNC"; return true;
        case 63: name = "PED_SYNC"; return true;
        case 98: name = "PLAYER_BULLETSYNC"; return true;
        case 100: name = "WEAPON_BULLETSYNC"; return true;
        default: return false;
    }
}

// Deathmatch's handler is installed only after MOD_NAME. JoinDataSent is
// STATUS_CONNECTING; InGameNoticeSent is STATUS_JOINING until packet 22.
bool DesktopConsumesSyncInJoinState(JoinState state) {
    return state == JoinState::JoinDataSent || state == JoinState::InGameNoticeSent;
}

}  // namespace

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
            if (gameId == static_cast<uint8_t>(NeonPacketID::PLAYER_JOINDATA) || gameId == 103) {
                JOIN_LOGI("NeonJoin: ignoring packet %u in state JoinDataSent", static_cast<unsigned>(gameId));
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

    // CNetAPI::ProcessPacket and the ped/unoccupied handlers consume these
    // during STATUS_CONNECTING and STATUS_JOINING and do not fail the session.
    // The body is not applied: PacketHandler runs only after JoinState::Complete.
    const char* syncName = nullptr;
    if (DesktopConsumesSyncInJoinState(d.state) && IgnoredInboundSyncWhileJoining(gameId, syncName)) {
        JOIN_LOGI("NeonJoin: ignoring %s (%u) in state %s", syncName, static_cast<unsigned>(gameId),
                  JoinStateName(d.state));
        return true;
    }

    // Strict on purpose: any other application packet while joining is a
    // protocol divergence that must surface instead of being silently dropped
    // (Plano 22 P1B acceptance rule). CConnectManager still aborts these sync
    // ids before MOD_NAME, and CPacketHandler raises a protocol error for
    // PLAYER_LIST, PLAYER_QUIT, nick changes, pickups and text items before
    // STATUS_JOINED, so those keep failing here.
    d.Fail("unexpected packet id " + std::to_string(gameId) + " in state " + JoinStateName(d.state), DisconnectMessages::ProtocolError(4));
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

    WorldRPG::ServerWorld::SetLocalPlayer(0);
    pImpl_->net = nullptr;
    pImpl_->state = JoinState::Idle;
    pImpl_->info = NeonJoinInfo{};
    pImpl_->release = NetcodeRelease::Current;
    pImpl_->autoRelease = pImpl_->fallbackUsed = pImpl_->fallbackPending = pImpl_->fallbackArmed = false;
    pImpl_->nickname.clear();
    pImpl_->password.clear();
    pImpl_->identityTicket.clear();
    pImpl_->identityRequestId = 0;
    pImpl_->callback = {};
    pImpl_->identityCallback = {};
}

} // namespace WorldRPG::Net
