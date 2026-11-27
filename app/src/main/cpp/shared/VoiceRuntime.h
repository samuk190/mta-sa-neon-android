#pragma once

#include "VoiceCodecStub.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace WorldRPG::Audio::VoiceRuntime {

/// Maximum voice payload length matching desktop CPacketHandler::Packet_VoiceData (CPacketHandler.cpp:5808).
constexpr size_t kMaxVoiceBufferLength = 2048;

/// Default stall timeout before firing onClientPlayerVoiceStop (matches BASS_SYNC_STALL drain).
constexpr uint64_t kDefaultIdleTimeoutMs = 350;

/// Apply server voice configuration from SERVER_JOINEDGAME (CVoiceRecorder.cpp:55-124, 165-191).
void Configure(bool enabled, uint8_t sampleRateCode, uint8_t quality, uint32_t bitrate);

/// Clear all active voice streams, encoder state, and configuration on session reset/disconnect.
void Reset();

/// True when the connected server enabled voice chat.
bool IsEnabled();

/// Current resolved desktop voice codec config (Speex mono 20 ms at 8/16/32 kHz).
VoiceConfig CurrentConfig();

/// Enqueue one incoming PACKET_ID_VOICE_DATA (71) payload for a player element.
/// Safe to call from the network thread.
bool OnVoicePacket(uint32_t playerId, const uint8_t* data, size_t length);

/// Handle incoming PACKET_ID_VOICE_END (72) for a player element.
/// Safe to call from the network thread.
void OnVoiceEnd(uint32_t playerId);

/// Enable/disable push-to-talk capture. Stopping after active transmission sends PACKET_ID_VOICE_END (72).
void SetTransmitActive(bool active);
bool IsTransmitActive();

/// Feed raw 16-bit mono PCM microphone samples at CurrentConfig().sampleRate.
/// Accumulates into 20 ms Speex frames, encodes, and sends PACKET_ID_VOICE_DATA (71).
void OnMicPcm(const int16_t* samples, size_t sampleCount);

/// Per-player voice stream controls (desktop CClientPlayerVoice via setSoundVolume/Paused/Pan).
bool SetPlayerVolume(uint32_t playerId, float volume);
bool GetPlayerVolume(uint32_t playerId, float& volumeOut);
bool SetPlayerPaused(uint32_t playerId, bool paused);
bool GetPlayerPaused(uint32_t playerId, bool& pausedOut);
bool SetPlayerPan(uint32_t playerId, float pan);
bool GetPlayerPan(uint32_t playerId, float& panOut);
bool IsPlayerActive(uint32_t playerId);

/// Release per-player voice decoder state when a player quits or is removed.
void OnPlayerRemoved(uint32_t playerId);

/// Event emitter invoked on the VM thread; returns false when Lua cancelEvent() cancelled the event.
using EventEmitter = std::function<bool(const char* eventName, uint32_t playerId)>;

/// Drain queued voice packets, decode PCM to playback output, and fire voice start/stop/pause/resumed events.
/// Pass nowMs == 0 to use steady_clock.
void Pump(const EventEmitter& emitEvent, uint64_t nowMs = 0);

using PlaybackSink = std::function<void(uint32_t playerId, const int16_t* samples, size_t count, uint32_t sampleRate)>;
using PacketSink = std::function<bool(uint8_t gamePacketId, const uint8_t* body, size_t length)>;

void SetPlaybackSinkForTests(PlaybackSink sink);
void SetPacketSinkForTests(PacketSink sink);
void SetIdleTimeoutMsForTests(uint64_t timeoutMs);

}  // namespace WorldRPG::Audio::VoiceRuntime
