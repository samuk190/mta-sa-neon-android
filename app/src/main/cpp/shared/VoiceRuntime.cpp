#include "VoiceRuntime.h"

#include "NeonJoinProtocol.h"
#include "ServerWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldRPG::JniBridge {
void VoiceConfigChanged(bool enabled, uint32_t sampleRate);
void VoicePlaybackPcm(uint32_t playerId, const int16_t* samples, size_t sampleCount, uint32_t sampleRate);
}  // namespace WorldRPG::JniBridge

namespace WorldRPG::Audio::VoiceRuntime {
namespace {

constexpr uint8_t kPacketVoiceData = 71;  // PACKET_ID_VOICE_DATA (CVoiceRecorder.cpp:313)
constexpr uint8_t kPacketVoiceEnd = 72;   // PACKET_ID_VOICE_END  (CVoiceRecorder.cpp:335)
constexpr size_t kMaxQueuedPackets = 256;

struct QueuedPacket {
    uint32_t playerId = 0;
    std::vector<uint8_t> payload;
};

struct QueuedEvent {
    std::string name;
    uint32_t playerId = 0;
};

struct PlayerVoiceSlot {
    std::unique_ptr<VoiceCodecStub> decoder;
    bool active = false;
    bool paused = false;
    bool volumeExplicit = false;
    float volume = 1.0f;
    float pan = 0.0f;
    uint64_t lastPacketMs = 0;
};

std::mutex g_mutex;
bool g_enabled = false;
VoiceConfig g_config = DesktopVoiceConfig(1, 4, 0);
uint64_t g_idleTimeoutMs = kDefaultIdleTimeoutMs;

std::deque<QueuedPacket> g_incoming;
std::deque<QueuedEvent> g_events;
std::unordered_map<uint32_t, PlayerVoiceSlot> g_players;

std::unique_ptr<VoiceCodecStub> g_encoder;
std::vector<int16_t> g_captureBuffer;
bool g_transmitActive = false;
bool g_sentAnySinceStart = false;

PlaybackSink g_testPlaybackSink;
PacketSink g_testPacketSink;

uint64_t SteadyNowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

float DefaultVolumeForPlayer(uint32_t playerId) {
    // Desktop CClientPlayerVoice.cpp:95-99: local player voice starts muted (0.0f) so
    // push-to-talk does not echo back unless a script explicitly raises volume.
    const uint32_t localId = WorldRPG::ServerWorld::LocalPlayer();
    if (localId != 0 && localId != 0xFFFFFFFFu && playerId == localId) return 0.0f;
    return 1.0f;
}

PlayerVoiceSlot& EnsurePlayerSlotLocked(uint32_t playerId) {
    auto it = g_players.find(playerId);
    if (it != g_players.end()) return it->second;
    PlayerVoiceSlot slot;
    slot.volume = DefaultVolumeForPlayer(playerId);
    auto [inserted, _] = g_players.emplace(playerId, std::move(slot));
    return inserted->second;
}

bool EnsureDecoderLocked(PlayerVoiceSlot& slot) {
    if (slot.decoder && slot.decoder->isReady()) return true;
    auto dec = std::make_unique<VoiceCodecStub>();
    if (!dec->initialize(g_config)) return false;
    slot.decoder = std::move(dec);
    return true;
}

bool EnsureEncoderLocked() {
    if (g_encoder && g_encoder->isReady()) return true;
    auto enc = std::make_unique<VoiceCodecStub>();
    if (!enc->initialize(g_config)) return false;
    g_encoder = std::move(enc);
    return true;
}

bool SendVoicePacketRaw(uint8_t gamePacketId, const uint8_t* body, size_t length) {
    PacketSink sink;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        sink = g_testPacketSink;
    }
    if (sink) return sink(gamePacketId, body, length);

    WorldRPG::Net::BitStreamWriter packet;
    packet.Write(gamePacketId);
    if (body && length > 0) packet.WriteBytes(body, length);
    return WorldRPG::Net::SendGamePacket(packet, false);
}

void EmitDecodedAudio(uint32_t playerId, std::vector<int16_t>& pcm, float volume, uint32_t sampleRate) {
    if (pcm.empty() || volume <= 0.0f) return;
    if (volume != 1.0f) {
        for (int16_t& s : pcm) {
            const float scaled = std::clamp(static_cast<float>(s) * volume, -32768.0f, 32767.0f);
            s = static_cast<int16_t>(std::lrintf(scaled));
        }
    }
    PlaybackSink sink;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        sink = g_testPlaybackSink;
    }
    if (sink) {
        sink(playerId, pcm.data(), pcm.size(), sampleRate);
    } else {
        WorldRPG::JniBridge::VoicePlaybackPcm(playerId, pcm.data(), pcm.size(), sampleRate);
    }
}

}  // namespace

void Configure(bool enabled, uint8_t sampleRateCode, uint8_t quality, uint32_t bitrate) {
    VoiceConfig cfg = DesktopVoiceConfig(sampleRateCode, quality, bitrate);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_enabled = enabled;
        g_config = cfg;
        g_incoming.clear();
        g_events.clear();
        g_players.clear();
        g_encoder.reset();
        g_captureBuffer.clear();
        g_transmitActive = false;
        g_sentAnySinceStart = false;
    }
    VOICE_LOGI("VoiceRuntime: configured enabled=%d rate=%u quality=%u bitrate=%u",
               enabled ? 1 : 0, cfg.sampleRate, static_cast<unsigned>(cfg.quality), cfg.bitrate);
    WorldRPG::JniBridge::VoiceConfigChanged(enabled, cfg.sampleRate);
}

void Reset() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_enabled = false;
        g_config = DesktopVoiceConfig(1, 4, 0);
        g_incoming.clear();
        g_events.clear();
        g_players.clear();
        g_encoder.reset();
        g_captureBuffer.clear();
        g_transmitActive = false;
        g_sentAnySinceStart = false;
    }
    WorldRPG::JniBridge::VoiceConfigChanged(false, 0);
}

bool IsEnabled() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_enabled;
}

VoiceConfig CurrentConfig() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_config;
}

bool OnVoicePacket(uint32_t playerId, const uint8_t* data, size_t length) {
    if (!data || length == 0 || length > kMaxVoiceBufferLength || playerId == 0) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled) return false;
    if (g_incoming.size() >= kMaxQueuedPackets) g_incoming.pop_front();
    QueuedPacket pkt;
    pkt.playerId = playerId;
    pkt.payload.assign(data, data + length);
    g_incoming.push_back(std::move(pkt));
    return true;
}

void OnVoiceEnd(uint32_t playerId) {
    if (playerId == 0) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_players.find(playerId);
    if (it != g_players.end() && it->second.active) {
        it->second.active = false;
        g_events.push_back(QueuedEvent{"onClientPlayerVoiceStop", playerId});
    }
}

void SetTransmitActive(bool active) {
    bool sendEnd = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_enabled) active = false;
        if (g_transmitActive && !active) {
            sendEnd = g_sentAnySinceStart;
            g_captureBuffer.clear();
            g_sentAnySinceStart = false;
        }
        g_transmitActive = active;
    }
    if (sendEnd) {
        SendVoicePacketRaw(kPacketVoiceEnd, nullptr, 0);
    }
}

bool IsTransmitActive() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_enabled && g_transmitActive;
}

void OnMicPcm(const int16_t* samples, size_t sampleCount) {
    if (!samples || sampleCount == 0) return;
    std::vector<std::vector<uint8_t>> encodedFrames;
    uint32_t localId = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_enabled || !g_transmitActive) return;
        if (!EnsureEncoderLocked()) return;
        const size_t frameSamples = static_cast<size_t>(g_config.sampleRate) * g_config.frameSizeMs / 1000u;
        if (frameSamples == 0) return;

        g_captureBuffer.insert(g_captureBuffer.end(), samples, samples + sampleCount);
        while (g_captureBuffer.size() >= frameSamples) {
            std::vector<uint8_t> encoded = g_encoder->encode(g_captureBuffer.data(), frameSamples);
            g_captureBuffer.erase(g_captureBuffer.begin(), g_captureBuffer.begin() + static_cast<std::ptrdiff_t>(frameSamples));
            if (!encoded.empty() && encoded.size() <= kMaxVoiceBufferLength) {
                encodedFrames.push_back(std::move(encoded));
                g_sentAnySinceStart = true;
            }
        }
        localId = WorldRPG::ServerWorld::LocalPlayer();
    }

    for (const auto& frame : encodedFrames) {
        // CVoiceRecorder.cpp:301: feed local voice instance so onClientPlayerVoiceStart triggers for local player.
        if (localId != 0 && localId != 0xFFFFFFFFu) {
            OnVoicePacket(localId, frame.data(), frame.size());
        }
        // CVoiceRecorder.cpp:310-314: [uint16_t audioBufferLength][audioBuffer[length]].
        WorldRPG::Net::BitStreamWriter body;
        body.Write(static_cast<uint16_t>(frame.size()));
        body.WriteBytes(frame.data(), frame.size());
        SendVoicePacketRaw(kPacketVoiceData, body.GetData(), body.GetByteLength());
    }
}

bool SetPlayerVolume(uint32_t playerId, float volume) {
    if (playerId == 0 || !std::isfinite(volume)) return false;
    volume = std::clamp(volume, 0.0f, 1.0f);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled) return false;
    PlayerVoiceSlot& slot = EnsurePlayerSlotLocked(playerId);
    slot.volume = volume;
    slot.volumeExplicit = true;
    return true;
}

bool GetPlayerVolume(uint32_t playerId, float& volumeOut) {
    if (playerId == 0) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled) return false;
    auto it = g_players.find(playerId);
    volumeOut = (it != g_players.end()) ? it->second.volume : DefaultVolumeForPlayer(playerId);
    return true;
}

bool SetPlayerPaused(uint32_t playerId, bool paused) {
    if (playerId == 0) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled) return false;
    PlayerVoiceSlot& slot = EnsurePlayerSlotLocked(playerId);
    if (slot.paused == paused) return true;
    slot.paused = paused;
    // Desktop CClientPlayerVoice.cpp:462-472.
    g_events.push_back(QueuedEvent{paused ? "onClientPlayerVoicePause" : "onClientPlayerVoiceResumed", playerId});
    return true;
}

bool GetPlayerPaused(uint32_t playerId, bool& pausedOut) {
    if (playerId == 0) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled) return false;
    auto it = g_players.find(playerId);
    pausedOut = (it != g_players.end()) ? it->second.paused : false;
    return true;
}

bool SetPlayerPan(uint32_t playerId, float pan) {
    if (playerId == 0 || !std::isfinite(pan) || pan < -1.0f || pan > 1.0f) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled) return false;
    PlayerVoiceSlot& slot = EnsurePlayerSlotLocked(playerId);
    slot.pan = pan;
    return true;
}

bool GetPlayerPan(uint32_t playerId, float& panOut) {
    if (playerId == 0) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled) return false;
    auto it = g_players.find(playerId);
    panOut = (it != g_players.end()) ? it->second.pan : 0.0f;
    return true;
}

bool IsPlayerActive(uint32_t playerId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_players.find(playerId);
    return it != g_players.end() && it->second.active;
}

void OnPlayerRemoved(uint32_t playerId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_players.erase(playerId);
}

void Pump(const EventEmitter& emitEvent, uint64_t nowMs) {
    if (nowMs == 0) nowMs = SteadyNowMs();

    std::deque<QueuedPacket> packets;
    std::deque<QueuedEvent> pendingEvents;
    uint32_t sampleRate = 16000;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_enabled) return;
        sampleRate = g_config.sampleRate;
        packets.swap(g_incoming);
        pendingEvents.swap(g_events);
    }

    // Service queued pause/resumed events first (CClientPlayerVoice::ServiceEventQueue).
    for (const auto& ev : pendingEvents) {
        if (emitEvent) emitEvent(ev.name.c_str(), ev.playerId);
    }

    // Process incoming voice packets in arrival order (CClientPlayerVoice::DecodeAndBuffer, lines 125-156).
    for (const auto& pkt : packets) {
        bool needStartEvent = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_enabled) return;
            PlayerVoiceSlot& slot = EnsurePlayerSlotLocked(pkt.playerId);
            needStartEvent = !slot.active;
        }

        if (needStartEvent) {
            const bool allowed = !emitEvent || emitEvent("onClientPlayerVoiceStart", pkt.playerId);
            if (!allowed) continue;
        }

        std::vector<int16_t> pcm;
        float volume = 1.0f;
        bool shouldPlay = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_enabled) return;
            PlayerVoiceSlot& slot = EnsurePlayerSlotLocked(pkt.playerId);
            slot.active = true;
            slot.lastPacketMs = nowMs;
            if (!slot.paused && EnsureDecoderLocked(slot)) {
                pcm = slot.decoder->decode(pkt.payload.data(), pkt.payload.size());
                volume = slot.volume;
                shouldPlay = !pcm.empty();
            }
        }
        if (shouldPlay) {
            EmitDecodedAudio(pkt.playerId, pcm, volume, sampleRate);
        }
    }

    // Check stall/idle timeout for active speakers -> onClientPlayerVoiceStop (CClientPlayerVoice.cpp:54-65).
    std::vector<uint32_t> stoppedPlayers;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_enabled) return;
        for (auto& [playerId, slot] : g_players) {
            if (slot.active && nowMs >= slot.lastPacketMs && (nowMs - slot.lastPacketMs) >= g_idleTimeoutMs) {
                slot.active = false;
                stoppedPlayers.push_back(playerId);
            }
        }
    }
    for (uint32_t playerId : stoppedPlayers) {
        if (emitEvent) emitEvent("onClientPlayerVoiceStop", playerId);
    }
}

void SetPlaybackSinkForTests(PlaybackSink sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_testPlaybackSink = std::move(sink);
}

void SetPacketSinkForTests(PacketSink sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_testPacketSink = std::move(sink);
}

void SetIdleTimeoutMsForTests(uint64_t timeoutMs) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_idleTimeoutMs = timeoutMs > 0 ? timeoutMs : kDefaultIdleTimeoutMs;
}

}  // namespace WorldRPG::Audio::VoiceRuntime
