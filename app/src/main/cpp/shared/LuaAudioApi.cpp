// Client-side MTA Audio & Voice System (CLuaAudioDefs).
// Follows CLuaAudioDefs.cpp, CStaticFunctionDefinitions.cpp, and CClientSound.cpp.

#include "LuaAudioApi.h"

#include "LuaApiExtra.h"
#include "LuaFileApi.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"
#include "VoiceRuntime.h"

#include "lua.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __ANDROID__
#include "../gtasa/GameTasks.h"
#include <dlfcn.h>
#endif

namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text);
}

namespace WorldRPG::Script {
namespace {

constexpr uint32_t kFirstSoundId = 0x61000001u;
constexpr size_t kMaxSounds = 64;

// Desktop CStaticFunctionDefinitions / CLuaAudioDefs mission-audio constants
// (CLuaAudioDefs.cpp:20-55, CClientAudio.h).
constexpr uint32_t kMissionAudioSlotCount = 4;
constexpr int kMissionAudioScriptBankFirst = 1800;
constexpr int kMissionAudioScriptBankLast = 1829;
constexpr int kMissionAudioScriptSlotFirst = 2000;
constexpr int kMissionAudioScriptSlotLast = 45400;
constexpr int kMissionAudioEventMin = 1000;
constexpr int kMissionAudioEventMax = 1190;

struct CVector3 {
    float x, y, z;
};

struct NativeAudioApi {
    bool probed = false;
    void* audioEngine = nullptr;
    void (*reportFrontend)(void*, int, float, float) = nullptr;
    void (*preloadBeatTrack)(void*, short) = nullptr;
    void (*playPreloadedBeatTrack)(void*, uint8_t) = nullptr;
    void (*preloadMissionAudio)(void*, uint8_t, int) = nullptr;
    int (*getMissionAudioEvent)(void*, uint8_t) = nullptr;
    int8_t (*getMissionAudioLoadingStatus)(void*, uint8_t) = nullptr;
    void (*playLoadedMissionAudio)(void*, uint8_t) = nullptr;
    bool (*isMissionAudioSampleFinished)(void*, uint8_t) = nullptr;
    void (*clearMissionAudio)(void*, uint8_t) = nullptr;
    void (*reportMissionAudioPos)(void*, uint16_t, CVector3*) = nullptr;
    void (*retuneRadio)(void*, int8_t) = nullptr;
    void (*startRadio)(void*, int8_t, int8_t) = nullptr;
    void (*stopRadio)(void*, void*, uint8_t) = nullptr;
    int8_t (*getCurrentRadioStation)(void*) = nullptr;
};

NativeAudioApi g_nativeAudio;
std::atomic<bool> g_hostNativeAudioMock{false};

#ifdef __ANDROID__
void ProbeNativeAudio() {
    if (g_nativeAudio.probed) return;
    g_nativeAudio.probed = true;
    void* lib = dlopen("libGTASA.so", RTLD_NOLOAD);
    if (!lib) return;
    g_nativeAudio.audioEngine = dlsym(lib, "AudioEngine");
    g_nativeAudio.reportFrontend =
        reinterpret_cast<void (*)(void*, int, float, float)>(
            dlsym(lib, "_ZN12CAudioEngine24ReportFrontendAudioEventEiff"));
    g_nativeAudio.preloadBeatTrack =
        reinterpret_cast<void (*)(void*, short)>(
            dlsym(lib, "_ZN12CAudioEngine16PreloadBeatTrackEs"));
    g_nativeAudio.playPreloadedBeatTrack =
        reinterpret_cast<void (*)(void*, uint8_t)>(
            dlsym(lib, "_ZN12CAudioEngine22PlayPreloadedBeatTrackEh"));
    g_nativeAudio.preloadMissionAudio =
        reinterpret_cast<void (*)(void*, uint8_t, int)>(
            dlsym(lib, "_ZN12CAudioEngine19PreloadMissionAudioEhi"));
    g_nativeAudio.getMissionAudioEvent =
        reinterpret_cast<int (*)(void*, uint8_t)>(
            dlsym(lib, "_ZN12CAudioEngine20GetMissionAudioEventEh"));
    g_nativeAudio.getMissionAudioLoadingStatus =
        reinterpret_cast<int8_t (*)(void*, uint8_t)>(
            dlsym(lib, "_ZN12CAudioEngine28GetMissionAudioLoadingStatusEh"));
    g_nativeAudio.playLoadedMissionAudio =
        reinterpret_cast<void (*)(void*, uint8_t)>(
            dlsym(lib, "_ZN12CAudioEngine22PlayLoadedMissionAudioEh"));
    g_nativeAudio.isMissionAudioSampleFinished =
        reinterpret_cast<bool (*)(void*, uint8_t)>(
            dlsym(lib, "_ZN12CAudioEngine28IsMissionAudioSampleFinishedEh"));
    g_nativeAudio.clearMissionAudio =
        reinterpret_cast<void (*)(void*, uint8_t)>(
            dlsym(lib, "_ZN12CAudioEngine17ClearMissionAudioEh"));
    g_nativeAudio.reportMissionAudioPos =
        reinterpret_cast<void (*)(void*, uint16_t, CVector3*)>(
            dlsym(lib, "_ZN12CAudioEngine23ReportMissionAudioEventEtR7CVector"));
    g_nativeAudio.retuneRadio =
        reinterpret_cast<void (*)(void*, int8_t)>(
            dlsym(lib, "_ZN12CAudioEngine11RetuneRadioEa"));
    g_nativeAudio.startRadio =
        reinterpret_cast<void (*)(void*, int8_t, int8_t)>(
            dlsym(lib, "_ZN12CAudioEngine10StartRadioEaa"));
    g_nativeAudio.stopRadio =
        reinterpret_cast<void (*)(void*, void*, uint8_t)>(
            dlsym(lib, "_ZN12CAudioEngine9StopRadioEP21tVehicleAudioSettingsh"));
    g_nativeAudio.getCurrentRadioStation =
        reinterpret_cast<int8_t (*)(void*)>(
            dlsym(lib, "_ZN12CAudioEngine24GetCurrentRadioStationIDEv"));
}
#endif

bool HasNativeAudioEngine() {
#ifdef __ANDROID__
    ProbeNativeAudio();
    return g_nativeAudio.audioEngine != nullptr;
#else
    return g_hostNativeAudioMock.load(std::memory_order_relaxed);
#endif
}

struct MissionAudioSlot {
    bool inUse = false;
    uint32_t handle = 0;
    int eventId = 0;
    bool loaded = false;
    bool playing = false;
    bool finished = false;
    std::string owner;
    uint16_t ownerNetId = 0;
    uint32_t ownerGeneration = 0;
};

std::array<MissionAudioSlot, kMissionAudioSlotCount> g_missionSlots{};
uint32_t g_nextMissionHandle = 1;

std::uint64_t DxNow() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

struct SoundSlot {
    uint32_t id = 0;
    bool alive = false;
    bool ended = false;
    float volume = 1.0f;
    bool loop = false;
    bool paused = false;
    double position = 0.0;
    std::uint64_t positionAt = 0;
    float minDistance = 5.0f;
    float maxDistance = 20.0f;
    float speed = 1.0f;
    float pan = 0.0f;
    double length = 0.0;
    std::string owner;
    uint16_t ownerNetId = 0;
    uint32_t ownerGeneration = 0;

    // 3D Spatial Audio
    bool is3D = false;
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;
    bool throttled = true;
    bool panEnabled = true;
    float effectiveVolume = 1.0f;
    float effectivePan = 0.0f;
    float lastSentVolume = -1.0f;
    float lastSentPan = -99.0f;
    bool isStream = false;
    bool isSfx = false;
    bool hasMediaPlayer = true;

    // Stream / DSP properties
    float sampleRate = 44100.0f;
    float tempo = 0.0f;
    float pitch = 0.0f;
    bool reversed = false;
    double bufferLength = 0.0;
    float bpm = 0.0f;

    // Live analysis buffers (Visualizer / injected test frames)
    int liveLeftLevel = -1;
    int liveRightLevel = -1;
    std::vector<float> liveFft;
    std::vector<float> liveWave;

    // Effects & Metadata (Lot 2)
    std::unordered_map<std::string, bool> fxEffects;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> fxParams;
    std::unordered_map<std::string, std::string> metaTags;
};

std::mutex g_soundMu;
SoundSlot g_sounds[kMaxSounds];
uint32_t g_nextSound = kFirstSoundId;
std::vector<SoundLifecycleEvent> g_soundEvents;

bool g_diagFrontEnd = false;
bool g_diagSfxPak = false;
bool g_showSound = false;

// Lot 3: World & Ambient Audio, SFX & Radio
uint8_t g_radioChannel = 0;
bool g_ambientGeneral = true;
bool g_ambientGunfire = true;
std::set<std::pair<int, int>> g_disabledWorldSounds;

SoundSlot* FindSound(uint32_t id) {
    if (!id) return nullptr;
    for (auto& slot : g_sounds) {
        if (slot.alive && slot.id == id) return &slot;
    }
    return nullptr;
}

float ComputeEffectiveSpeed(const SoundSlot& slot) {
    float eff = slot.speed;
    if (slot.tempo != 0.0f) eff *= (1.0f + slot.tempo * 0.01f);
    if (slot.pitch != 0.0f) eff *= std::pow(2.0f, slot.pitch / 12.0f);
    return std::clamp(eff, 0.1f, 4.0f);
}

void PostSoundSpeedLine(uint32_t id, float speed) {
    char line[64];
    std::snprintf(line, sizeof(line), "%u|%.4f", id, std::clamp(speed, 0.1f, 4.0f));
    ::WorldRPG::JniBridge::PostGameLine(22, line);
}

void ComputeSpatial3DMix(const SoundSlot& slot, float lx, float ly, float lz,
                         float rx, float ry, float rz, float& outVol, float& outPan) {
    if (!slot.is3D) {
        outVol = std::clamp(slot.volume, 0.0f, 1.0f);
        outPan = std::clamp(slot.pan, -1.0f, 1.0f);
        return;
    }
    float sx = slot.posX, sy = slot.posY, sz = slot.posZ;
    WorldRPG::ServerWorld::Attachment att;
    if (WorldRPG::ServerWorld::CopyAttachment(slot.id, att) && att.parent != 0xFFFFFFFFu) {
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        if (WorldRPG::ServerWorld::ElementPose(att.parent, px, py, pz)) {
            sx = px + att.position[0];
            sy = py + att.position[1];
            sz = pz + att.position[2];
        }
    }
    const float dx = sx - lx;
    const float dy = sy - ly;
    const float dz = sz - lz;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float minD = std::max(0.001f, slot.minDistance);
    const float maxD = std::max(minD, slot.maxDistance);
    float atten = 1.0f;
    if (dist >= maxD) {
        atten = 0.0f;
    } else if (dist > minD) {
        const float span = maxD - minD;
        if (span > 1e-4f) {
            const float t = (dist - minD) / span;
            // Smooth rolloff close to OpenAL inverse-distance clamped curve, reaching 0 at maxDistance.
            atten = (minD / dist) * (1.0f - t);
        } else {
            atten = 0.0f;
        }
    }
    outVol = std::clamp(slot.volume * atten, 0.0f, 1.0f);

    float spatialPan = 0.0f;
    if (slot.panEnabled && dist > 1e-4f) {
        const float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
        if (rLen > 1e-4f) {
            const float invDist = 1.0f / dist;
            const float invRight = 1.0f / rLen;
            spatialPan = std::clamp((dx * rx + dy * ry + dz * rz) * invDist * invRight, -1.0f, 1.0f);
        }
    }
    outPan = std::clamp(spatialPan + slot.pan, -1.0f, 1.0f);
}

std::string SoundSource(lua_State* L) {
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text) return {};
    const std::string_view source(text, length);
    if (source.find('\0') != std::string_view::npos) return {};
    if (source.starts_with("http://") || source.starts_with("https://")) return std::string(source);
    std::filesystem::path file;
    return ResolveFilePath(source, file) ? file.string() : std::string();
}

uint32_t AllocSound(const std::string& path, bool loop, bool is3D = false, float x = 0.0f, float y = 0.0f,
                    float z = 0.0f, bool throttled = true, bool hasMediaPlayer = true, bool isSfx = false) {
    uint32_t id = 0;
    const bool isStream = path.starts_with("http://") || path.starts_with("https://");
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        // Java delivers asynchronous callbacks with signed int IDs. Do not
        // reuse one across sessions while a previous callback may still arrive.
        if (g_nextSound > 0x7fffffffu) return 0;
        for (auto& slot : g_sounds) {
            if (slot.alive) continue;
            slot = SoundSlot{};
            slot.id = id = g_nextSound++;
            slot.alive = true;
            slot.loop = loop;
            slot.positionAt = DxNow();
            slot.owner = ActiveVmOwner();
            slot.ownerNetId = ActiveVmOwnerNet();
            slot.ownerGeneration = ActiveVmOwnerGeneration();
            slot.is3D = is3D;
            slot.posX = x;
            slot.posY = y;
            slot.posZ = z;
            slot.throttled = throttled;
            slot.isStream = isStream;
            slot.isSfx = isSfx;
            slot.hasMediaPlayer = hasMediaPlayer;
            slot.effectiveVolume = slot.volume;
            slot.effectivePan = slot.pan;
            break;
        }
    }
    if (id && hasMediaPlayer && !path.empty()) {
        // MediaPlayer reads everything after the two separators as the source.
        const std::string body = std::to_string(id) + (loop ? "|1|" : "|0|") + path;
        ::WorldRPG::JniBridge::PostGameLine(11, body.c_str());
    }
    return id;
}

double SoundPosition(const SoundSlot& slot) {
    if (slot.paused) return slot.position;
    return slot.position + static_cast<double>(DxNow() - slot.positionAt) / 1000.0;
}

int SetSoundNumber(lua_State* L, float SoundSlot::*field, float minValue, float maxValue) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float value = static_cast<float>(lua_tonumber(L, 2));
    if (!std::isfinite(value) || value < minValue || value > maxValue) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->*field = value;
    lua_pushboolean(L, 1);
    return 1;
}

int GetSoundNumber(lua_State* L, float SoundSlot::*field) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, slot->*field);
    return 1;
}

// --- Basic Sound Handlers ---

int l_playSound(lua_State* L) {
    const std::string path = SoundSource(L);
    if (path.empty()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool loop = lua_toboolean(L, 2) != 0;
    const uint32_t id = AllocSound(path, loop);
    if (!id) {
        lua_pushboolean(L, 0);
        return 1;
    }
    PushElement(L, id);
    return 1;
}

bool IsVoicePlayerTarget(uint32_t id) {
    if (id == 0 || id == 0xFFFFFFFFu) return false;
    const uint32_t localId = WorldRPG::ServerWorld::LocalPlayer();
    if (localId != 0 && localId != 0xFFFFFFFFu && id == localId) return true;
    return WorldRPG::ServerWorld::IsKnownPlayer(id);
}

int l_stopSound(lua_State* L) {
    lua_pushboolean(L, AudioStopSound(ElementIdOf(L, 1)));
    return 1;
}

int l_setSoundVolume(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float volume = static_cast<float>(lua_tonumber(L, 2));
    if (!std::isfinite(volume)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    if (!AudioSoundAlive(id)) {
        if (IsVoicePlayerTarget(id) && WorldRPG::Audio::VoiceRuntime::SetPlayerVolume(id, volume)) {
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushboolean(L, 0);
        return 1;
    }
    bool is3D = false;
    bool hasPlayer = false;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        SoundSlot* slot = FindSound(id);
        if (slot) {
            slot->volume = volume;
            slot->lastSentVolume = -1.0f;
            is3D = slot->is3D;
            hasPlayer = slot->hasMediaPlayer;
            if (!is3D) slot->effectiveVolume = volume;
        }
    }
    if (hasPlayer && !is3D) {
        char line[64];
        std::snprintf(line, sizeof(line), "%u|%.3f", id, volume);
        ::WorldRPG::JniBridge::PostGameLine(13, line);
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSoundVolume(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        for (const auto& slot : g_sounds) {
            if (slot.alive && slot.id == id) {
                lua_pushnumber(L, slot.volume);
                return 1;
            }
        }
    }
    float voiceVol = 0.0f;
    if (IsVoicePlayerTarget(id) && WorldRPG::Audio::VoiceRuntime::GetPlayerVolume(id, voiceVol)) {
        lua_pushnumber(L, voiceVol);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_setSoundLooped(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->loop = lua_toboolean(L, 2) != 0;
    char line[32];
    std::snprintf(line, sizeof(line), "%u|%d", id, slot->loop ? 1 : 0);
    ::WorldRPG::JniBridge::PostGameLine(14, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_isSoundLooped(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, slot->loop ? 1 : 0);
    return 1;
}

int l_setSoundPaused(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool pause = lua_toboolean(L, 2) != 0;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        SoundSlot* slot = FindSound(id);
        if (slot) {
            const auto now = DxNow();
            if (pause && !slot->paused)
                slot->position += static_cast<double>(now - slot->positionAt) / 1000.0;
            else if (!pause && slot->paused)
                slot->positionAt = now;
            slot->paused = pause;
            char line[32];
            std::snprintf(line, sizeof(line), "%u|%d", id, slot->paused ? 1 : 0);
            ::WorldRPG::JniBridge::PostGameLine(15, line);
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    if (IsVoicePlayerTarget(id) && WorldRPG::Audio::VoiceRuntime::SetPlayerPaused(id, pause)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_isSoundPaused(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        const SoundSlot* slot = FindSound(id);
        if (slot) {
            lua_pushboolean(L, slot->paused ? 1 : 0);
            return 1;
        }
    }
    bool voicePaused = false;
    if (IsVoicePlayerTarget(id) && WorldRPG::Audio::VoiceRuntime::GetPlayerPaused(id, voicePaused)) {
        lua_pushboolean(L, voicePaused ? 1 : 0);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_getSoundPosition(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, SoundPosition(*slot));
    return 1;
}

int l_setSoundPosition(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    double seconds = lua_tonumber(L, 2);
    if (!std::isfinite(seconds) || seconds < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->position = seconds;
    slot->positionAt = DxNow();
    char line[64];
    std::snprintf(line, sizeof(line), "%u|%.3f", id, seconds);
    ::WorldRPG::JniBridge::PostGameLine(16, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSoundLength(lua_State* L) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(ElementIdOf(L, 1));
    if (!slot) return 0;
    lua_pushnumber(L, slot->length);
    return 1;
}

int l_setSoundSpeed(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float value = static_cast<float>(lua_tonumber(L, 2));
    if (!std::isfinite(value) || value < 0.0001f || value > 100.0f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float effectiveSpeed = 1.0f;
    bool hasPlayer = false;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        SoundSlot* slot = FindSound(id);
        if (!slot) {
            lua_pushboolean(L, 0);
            return 1;
        }
        slot->speed = value;
        effectiveSpeed = ComputeEffectiveSpeed(*slot);
        hasPlayer = slot->hasMediaPlayer;
    }
    if (hasPlayer) PostSoundSpeedLine(id, effectiveSpeed);
    lua_pushboolean(L, 1);
    return 1;
}
int l_getSoundSpeed(lua_State* L) { return GetSoundNumber(L, &SoundSlot::speed); }

int l_setSoundPan(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) { lua_pushboolean(L, 0); return 1; }
    const float pan = static_cast<float>(lua_tonumber(L, 2));
    if (!std::isfinite(pan) || pan < -1.0f || pan > 1.0f) { lua_pushboolean(L, 0); return 1; }
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        SoundSlot* slot = FindSound(id);
        if (slot) {
            slot->pan = pan;
            slot->lastSentPan = -99.0f;
            if (!slot->is3D) {
                slot->effectivePan = pan;
                if (slot->hasMediaPlayer) {
                    char line[64];
                    std::snprintf(line, sizeof(line), "%u|%.6f", id, pan);
                    ::WorldRPG::JniBridge::PostGameLine(18, line);
                }
            }
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    if (IsVoicePlayerTarget(id) && WorldRPG::Audio::VoiceRuntime::SetPlayerPan(id, pan)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_getSoundPan(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        const SoundSlot* slot = FindSound(id);
        if (slot) {
            lua_pushnumber(L, slot->pan);
            return 1;
        }
    }
    float voicePan = 0.0f;
    if (IsVoicePlayerTarget(id) && WorldRPG::Audio::VoiceRuntime::GetPlayerPan(id, voicePan)) {
        lua_pushnumber(L, voicePan);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_setSoundMaxDistance(lua_State* L) { return SetSoundNumber(L, &SoundSlot::maxDistance, 0.0f, 1.0e6f); }
int l_getSoundMaxDistance(lua_State* L) { return GetSoundNumber(L, &SoundSlot::maxDistance); }
int l_setSoundMinDistance(lua_State* L) { return SetSoundNumber(L, &SoundSlot::minDistance, 0.0f, 1.0e6f); }
int l_getSoundMinDistance(lua_State* L) { return GetSoundNumber(L, &SoundSlot::minDistance); }

int l_playSoundFrontEnd(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int id = static_cast<int>(lua_tointeger(L, 1));
    if (id < 0 || id > 101) {
        LUA_LOGE("playSoundFrontEnd: invalid sound ID. Valid sound IDs are 0 - 101.");
        lua_pushboolean(L, 0);
        return 1;
    }
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (g_nativeAudio.audioEngine && g_nativeAudio.reportFrontend) {
        WorldRPG::GameTasks::Run([id]() {
            if (g_nativeAudio.audioEngine && g_nativeAudio.reportFrontend) {
                g_nativeAudio.reportFrontend(g_nativeAudio.audioEngine, id, 0.0f, 1.0f);
            }
        });
        lua_pushboolean(L, 1);
        return 1;
    }
#endif
    if (!g_diagFrontEnd && !HasNativeAudioEngine()) {
        g_diagFrontEnd = true;
        LUA_LOGE("DIAG playSoundFrontEnd: CAudioEngine::ReportFrontendAudioEvent unavailable");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_getRadioChannelName(lua_State* L) {
    static const char* stations[] = {
        "Radio off", "Playback FM", "K-Rose", "K-DST", "Bounce FM", "SF-UR",
        "Radio Los Santos", "Radio X", "CSR 103.9", "K-Jah West", "Master Sounds 98.3", "WCTR", "User Track Player"
    };
    if (lua_isnumber(L, 1)) {
        const double value = lua_tonumber(L, 1);
        if (std::isfinite(value) && value > -1.0 && value < 13.0) {
            lua_pushstring(L, stations[static_cast<int>(value)]);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// --- Lot 1: 3D Spatial Audio & Core Streams ---

// CLuaAudioDefs::PlaySound3D: path, x, y, z, [loop = false], [throttled = true]
int l_playSound3D(lua_State* L) {
    const std::string path = SoundSource(L);
    if (path.empty() || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float x = static_cast<float>(lua_tonumber(L, 2));
    const float y = static_cast<float>(lua_tonumber(L, 3));
    const float z = static_cast<float>(lua_tonumber(L, 4));
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool loop = lua_isboolean(L, 5) ? (lua_toboolean(L, 5) != 0) : false;
    const bool throttled = lua_isboolean(L, 6) ? (lua_toboolean(L, 6) != 0) : true;

    uint32_t id = AllocSound(path, loop, true, x, y, z, throttled);
    if (!id) {
        lua_pushboolean(L, 0);
        return 1;
    }
    PushElement(L, id);
    return 1;
}

// CLuaAudioDefs::SetSoundPanEnabled / IsSoundPanEnabled
int l_setSoundPanningEnabled(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->panEnabled = (lua_toboolean(L, 2) != 0);
    lua_pushboolean(L, 1);
    return 1;
}

int l_isSoundPanningEnabled(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, slot->panEnabled ? 1 : 0);
    return 1;
}

// CLuaAudioDefs::GetSoundProperties / SetSoundProperties
int l_getSoundProperties(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, slot->sampleRate);
    lua_pushnumber(L, slot->tempo);
    lua_pushnumber(L, slot->pitch);
    lua_pushboolean(L, slot->reversed ? 1 : 0);
    return 4;
}

int l_setSoundProperties(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float sampleRate = static_cast<float>(lua_tonumber(L, 2));
    const float tempo = static_cast<float>(lua_tonumber(L, 3));
    const float pitch = static_cast<float>(lua_tonumber(L, 4));
    const bool reversed = lua_isboolean(L, 5) ? (lua_toboolean(L, 5) != 0) : false;
    if (!std::isfinite(sampleRate) || !std::isfinite(tempo) || !std::isfinite(pitch)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float effectiveSpeed = 1.0f;
    bool hasPlayer = false;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        SoundSlot* slot = FindSound(id);
        if (!slot) {
            lua_pushboolean(L, 0);
            return 1;
        }
        slot->sampleRate = sampleRate;
        slot->tempo = tempo;
        slot->pitch = pitch;
        slot->reversed = reversed;
        effectiveSpeed = ComputeEffectiveSpeed(*slot);
        hasPlayer = slot->hasMediaPlayer;
    }
    if (hasPlayer) PostSoundSpeedLine(id, effectiveSpeed);
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaAudioDefs::GetSoundBufferLength
int l_getSoundBufferLength(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, slot->bufferLength);
    return 1;
}

// CLuaAudioDefs::GetSoundBPM
int l_getSoundBPM(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (slot->bpm > 0.0f) {
        lua_pushnumber(L, slot->bpm);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaAudioDefs::GetSoundLevelData (dwLeft, dwRight in range 0 - 32768)
int l_getSoundLevelData(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot || slot->paused) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float vol = std::clamp(slot->is3D ? slot->effectiveVolume : slot->volume, 0.0f, 1.0f);
    const float pan = std::clamp(slot->is3D ? slot->effectivePan : slot->pan, -1.0f, 1.0f);
    const float leftGain = std::clamp(vol * (1.0f - std::max(0.0f, pan)), 0.0f, 1.0f);
    const float rightGain = std::clamp(vol * (1.0f + std::min(0.0f, pan)), 0.0f, 1.0f);
    int left = 0;
    int right = 0;
    if (slot->liveLeftLevel >= 0 && slot->liveRightLevel >= 0) {
        left = std::clamp(static_cast<int>(slot->liveLeftLevel * leftGain), 0, 32768);
        right = std::clamp(static_cast<int>(slot->liveRightLevel * rightGain), 0, 32768);
    } else {
        left = static_cast<int>(leftGain * 32768.0f);
        right = static_cast<int>(rightGain * 32768.0f);
    }
    lua_pushinteger(L, left);
    lua_pushinteger(L, right);
    return 2;
}

// --- Lot 2: Sound Analysis, DSP Effects & Debug Visualizer ---

static const char* kKnownEffects[] = {
    "chorus", "compressor", "distortion", "echo", "flanger", "gargle", "i3dl2reverb", "parameq", "reverb"
};

static std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool IsKnownEffect(const std::string& name) {
    for (const char* fx : kKnownEffects) {
        if (name == fx) return true;
    }
    return false;
}

int l_getSoundFFTData(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int length = static_cast<int>(lua_tointeger(L, 2));
    const int bands = lua_isnumber(L, 3) ? static_cast<int>(lua_tointeger(L, 3)) : 0;
    if (length < 2 || length > 16384) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot || slot->paused) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int count = (bands > 0) ? bands : (length / 2 + 1);
    const float gain = std::clamp(slot->is3D ? slot->effectiveVolume : slot->volume, 0.0f, 1.0f);
    lua_createtable(L, count, 0);
    const size_t srcSize = slot->liveFft.size();
    for (int i = 0; i < count; ++i) {
        double sample = 0.0;
        if (srcSize > 0) {
            const size_t srcIdx = std::min(srcSize - 1,
                static_cast<size_t>((static_cast<uint64_t>(i) * srcSize) / static_cast<uint64_t>(count)));
            sample = static_cast<double>(slot->liveFft[srcIdx] * gain);
        }
        lua_pushinteger(L, i);
        lua_pushnumber(L, sample);
        lua_settable(L, -3);
    }
    return 1;
}

int l_getSoundWaveData(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int length = static_cast<int>(lua_tointeger(L, 2));
    if (length <= 0 || length > 16384) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot || slot->paused) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float gain = std::clamp(slot->is3D ? slot->effectiveVolume : slot->volume, 0.0f, 1.0f);
    const size_t srcSize = slot->liveWave.size();
    lua_createtable(L, length, 0);
    for (int i = 0; i < length; ++i) {
        double sample = 0.0;
        if (srcSize > 0) {
            const size_t srcIdx = std::min(srcSize - 1,
                static_cast<size_t>((static_cast<uint64_t>(i) * srcSize) / static_cast<uint64_t>(length)));
            sample = static_cast<double>(slot->liveWave[srcIdx] * gain);
        }
        lua_pushinteger(L, i);
        lua_pushnumber(L, sample);
        lua_settable(L, -3);
    }
    return 1;
}

int l_getSoundMetaTags(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const char* format = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (format && format[0] != '\0') {
        const std::string key = ToLower(format);
        auto it = slot->metaTags.find(key);
        if (it != slot->metaTags.end()) {
            lua_pushstring(L, it->second.c_str());
            return 1;
        }
        lua_pushstring(L, "");
        return 1;
    }
    static const char* kStandardFields[] = {
        "title", "artist", "album", "genre", "year", "comment",
        "track", "composer", "copyright", "subtitle", "album_artist",
        "stream_name", "stream_title"
    };
    lua_newtable(L);
    for (const char* field : kStandardFields) {
        auto it = slot->metaTags.find(field);
        if (it != slot->metaTags.end() && !it->second.empty()) {
            lua_pushstring(L, it->second.c_str());
            lua_setfield(L, -2, field);
        }
    }
    return 1;
}

int l_setSoundEffectEnabled(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isstring(L, 2) || lua_type(L, 3) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string effect = ToLower(lua_tostring(L, 2));
    if (!IsKnownEffect(effect)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool enable = lua_toboolean(L, 3) != 0;
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->fxEffects[effect] = enable;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSoundEffects(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_newtable(L);
    for (const char* fx : kKnownEffects) {
        auto it = slot->fxEffects.find(fx);
        const bool enabled = (it != slot->fxEffects.end()) ? it->second : false;
        lua_pushboolean(L, enabled ? 1 : 0);
        lua_setfield(L, -2, fx);
    }
    return 1;
}

int l_setSoundEffectParameter(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isstring(L, 2) || !lua_isstring(L, 3) || !lua_isnumber(L, 4)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string effect = ToLower(lua_tostring(L, 2));
    const std::string param = lua_tostring(L, 3);
    const double val = lua_tonumber(L, 4);
    if (!std::isfinite(val) || !IsKnownEffect(effect)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    auto fxIt = slot->fxEffects.find(effect);
    if (fxIt == slot->fxEffects.end() || !fxIt->second) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->fxParams[effect][param] = val;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSoundEffectParameters(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isstring(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string effect = ToLower(lua_tostring(L, 2));
    if (!IsKnownEffect(effect)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_newtable(L);
    auto it = slot->fxParams.find(effect);
    if (it != slot->fxParams.end()) {
        for (const auto& [k, v] : it->second) {
            lua_pushnumber(L, v);
            lua_setfield(L, -2, k.c_str());
        }
    }
    return 1;
}

int l_showSound(lua_State* L) {
    if (lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_showSound = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int l_isShowSoundEnabled(lua_State* L) {
    lua_pushboolean(L, g_showSound ? 1 : 0);
    return 1;
}

// --- Lot 3: World & Ambient Audio, SFX & Radio Channels ---

static bool IsValidAudioContainer(const std::string& name) {
    static const char* kContainers[] = {
        "feet", "genrl", "pain_a", "script", "spc_ea", "spc_fa", "spc_ga", "spc_na", "spc_pa", "radio"
    };
    for (const char* c : kContainers) {
        if (name == c) return true;
    }
    return false;
}

int l_setRadioChannel(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int channel = static_cast<int>(lua_tointeger(L, 1));
    if (channel < 0 || channel > 12) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint8_t prev = g_radioChannel;
    g_radioChannel = static_cast<uint8_t>(channel);
    if (prev != g_radioChannel) {
        lua_pushinteger(L, g_radioChannel);
        ActiveVmFireElementEvent(L, "onClientPlayerRadioSwitch", ServerWorld::LocalPlayer(), 1);
    }
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (g_nativeAudio.audioEngine) {
        WorldRPG::GameTasks::Run([channel]() {
            if (!g_nativeAudio.audioEngine) return;
            if (channel == 0) {
                if (g_nativeAudio.stopRadio) g_nativeAudio.stopRadio(g_nativeAudio.audioEngine, nullptr, 0);
            } else if (g_nativeAudio.retuneRadio) {
                g_nativeAudio.retuneRadio(g_nativeAudio.audioEngine, static_cast<int8_t>(channel));
            } else if (g_nativeAudio.startRadio) {
                g_nativeAudio.startRadio(g_nativeAudio.audioEngine, static_cast<int8_t>(channel), 0);
            }
        });
    }
#endif
    lua_pushboolean(L, 1);
    return 1;
}

int l_getRadioChannel(lua_State* L) {
    lua_pushinteger(L, g_radioChannel);
    return 1;
}

// getRadioPlaybackState / setRadioPlaybackState (CLuaRadioPlaybackDefs.cpp:12-150).
// Desktop reads and writes CAERadioTrackManager through
// CAERadioTrackManagerSA::Get/SetPlaybackState. The phone has no hook on the
// radio track manager yet (the radio channel above is only stored), so both
// take desktop's path for a missing manager and return false. setRadioPlaybackState
// checks its table first, in desktop's order, so a bad table fails the same way.
constexpr int kRadioQueueSize = 5;  // SRadioPlaybackState::QUEUE_SIZE (CAERadioTrackManager.h:16)
constexpr int kMinNativeTrackIndex = -128;
constexpr int kMaxNativeTrackIndex = 127;

bool ReadRadioIntegerField(lua_State* L, int tableIndex, const char* name, int& value) {
    lua_getfield(L, tableIndex, name);
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    value = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return true;
}

void NoteRadioManagerMissing(const char* function) {
    static bool logged = false;
    if (logged) return;
    logged = true;
    LUA_LOGE("DIAG %s: CAERadioTrackManager is not hooked on this client", function);
}

int l_getRadioPlaybackState(lua_State* L) {
    NoteRadioManagerMissing("getRadioPlaybackState");
    lua_pushboolean(L, 0);
    return 1;
}

int l_setRadioPlaybackState(lua_State* L) {
    if (!lua_istable(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int station = 0, mode = 0, trackId = 0, trackType = 0, trackIndex = 0, position = 0, length = 0, flags = 0;
    if (!ReadRadioIntegerField(L, 1, "station", station) || !ReadRadioIntegerField(L, 1, "trackId", trackId) ||
        !ReadRadioIntegerField(L, 1, "trackType", trackType) || !ReadRadioIntegerField(L, 1, "trackIndex", trackIndex) ||
        !ReadRadioIntegerField(L, 1, "position", position) || !ReadRadioIntegerField(L, 1, "length", length) ||
        !ReadRadioIntegerField(L, 1, "flags", flags)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    ReadRadioIntegerField(L, 1, "mode", mode);
    if (station < 0 || station >= 13 || trackId < 0 || trackType < 0 || trackType > 7 || trackIndex < kMinNativeTrackIndex ||
        trackIndex > kMaxNativeTrackIndex || position < 0 || length < -1 || flags < 0 || flags > 255) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_getfield(L, 1, "queue");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
        return 1;
    }
    for (int i = 0; i < kRadioQueueSize; ++i) {
        lua_rawgeti(L, -1, i + 1);
        int id = 0, type = 0, index = 0;
        if (!lua_istable(L, -1) || !ReadRadioIntegerField(L, -1, "id", id) || !ReadRadioIntegerField(L, -1, "type", type) ||
            !ReadRadioIntegerField(L, -1, "index", index) || id < -1 || type < 0 || type > 7 ||
            index < kMinNativeTrackIndex || index > kMaxNativeTrackIndex) {
            lua_pop(L, 2);
            lua_pushboolean(L, 0);
            return 1;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    NoteRadioManagerMissing("setRadioPlaybackState");
    lua_pushboolean(L, 0);
    return 1;
}

int l_setAmbientSoundEnabled(lua_State* L) {
    const char* typeStr = lua_isstring(L, 1) ? lua_tostring(L, 1) : "general";
    const int boolIdx = lua_isstring(L, 1) ? 2 : 1;
    if (lua_type(L, boolIdx) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool enabled = lua_toboolean(L, boolIdx) != 0;
    const std::string type = ToLower(typeStr);
    if (type == "general") {
        g_ambientGeneral = enabled;
    } else if (type == "gunfire") {
        g_ambientGunfire = enabled;
    } else {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_isAmbientSoundEnabled(lua_State* L) {
    const char* typeStr = lua_isstring(L, 1) ? lua_tostring(L, 1) : "general";
    const std::string type = ToLower(typeStr);
    if (type == "general") {
        lua_pushboolean(L, g_ambientGeneral ? 1 : 0);
    } else if (type == "gunfire") {
        lua_pushboolean(L, g_ambientGunfire ? 1 : 0);
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

int l_resetAmbientSounds(lua_State* L) {
    g_ambientGeneral = true;
    g_ambientGunfire = true;
    lua_pushboolean(L, 1);
    return 1;
}

int l_setWorldSoundEnabled(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int group = static_cast<int>(lua_tointeger(L, 1));
    int index = -1;
    bool enabled = false;
    if (lua_type(L, 2) == LUA_TBOOLEAN) {
        enabled = lua_toboolean(L, 2) != 0;
    } else if (lua_isnumber(L, 2) && lua_type(L, 3) == LUA_TBOOLEAN) {
        index = static_cast<int>(lua_tointeger(L, 2));
        enabled = lua_toboolean(L, 3) != 0;
    } else {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (group < 0 || group > 49) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (enabled) {
        g_disabledWorldSounds.erase({group, index});
        if (index == -1) {
            for (auto it = g_disabledWorldSounds.begin(); it != g_disabledWorldSounds.end(); ) {
                if (it->first == group) it = g_disabledWorldSounds.erase(it);
                else ++it;
            }
        }
    } else {
        g_disabledWorldSounds.insert({group, index});
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_isWorldSoundEnabled(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int group = static_cast<int>(lua_tointeger(L, 1));
    const int index = lua_isnumber(L, 2) ? static_cast<int>(lua_tointeger(L, 2)) : -1;
    if (group < 0 || group > 49) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (g_disabledWorldSounds.count({group, -1}) > 0 || g_disabledWorldSounds.count({group, index}) > 0) {
        lua_pushboolean(L, 0);
    } else {
        lua_pushboolean(L, 1);
    }
    return 1;
}

int l_resetWorldSounds(lua_State* L) {
    g_disabledWorldSounds.clear();
    lua_pushboolean(L, 1);
    return 1;
}

int RadioStationNameToBank(const std::string& lower) {
    static const char* kStations[] = {
        "adverts", "ambience", "beats", "ch", "co", "cr", "cutscene", "ds",
        "hc", "mh", "mr", "nj", "re", "rg", "tk"
    };
    for (int i = 0; i < static_cast<int>(std::size(kStations)); ++i) {
        if (lower == kStations[i]) return i;
    }
    return -1;
}

bool TryResolveSfxAsset(const std::string& container, int bankIndex, int audioIndex, std::string& outPath) {
    static const char* kExts[] = {".ogg", ".wav", ".mp3"};
    for (const char* ext : kExts) {
        const std::string rel = "audio/sfx/" + container + "/" + std::to_string(bankIndex) + "/" +
                                std::to_string(audioIndex) + ext;
        std::filesystem::path resolved;
        if (ResolveFilePath(rel, resolved)) {
            outPath = resolved.string();
            return true;
        }
    }
    return false;
}

int l_getSFXStatus(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        lua_pushnil(L);
        return 1;
    }
    const std::string container = ToLower(lua_tostring(L, 1));
    if (!IsValidAudioContainer(container)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, HasNativeAudioEngine() ? 1 : 0);
    return 1;
}

int PlaySfxCommon(lua_State* L, bool is3D) {
    if (!lua_isstring(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string container = ToLower(lua_tostring(L, 1));
    if (!IsValidAudioContainer(container)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int bankIndex = -1;
    if (lua_isnumber(L, 2)) {
        bankIndex = static_cast<int>(lua_tointeger(L, 2));
    } else if (container == "radio" && lua_isstring(L, 2)) {
        bankIndex = RadioStationNameToBank(ToLower(lua_tostring(L, 2)));
    }
    if (bankIndex < 0 || !lua_isnumber(L, 3)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int audioIndex = static_cast<int>(lua_tointeger(L, 3));
    if (audioIndex < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool loop = false;
    if (is3D) {
        if (!lua_isnumber(L, 4) || !lua_isnumber(L, 5) || !lua_isnumber(L, 6)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        x = static_cast<float>(lua_tonumber(L, 4));
        y = static_cast<float>(lua_tonumber(L, 5));
        z = static_cast<float>(lua_tonumber(L, 6));
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        loop = lua_isboolean(L, 7) ? (lua_toboolean(L, 7) != 0) : false;
    } else {
        loop = lua_isboolean(L, 4) ? (lua_toboolean(L, 4) != 0) : false;
    }

    std::string assetPath;
    const bool hasAsset = TryResolveSfxAsset(container, bankIndex, audioIndex, assetPath);
    if (!hasAsset && !HasNativeAudioEngine()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!hasAsset && !g_diagSfxPak) {
        g_diagSfxPak = true;
        LUA_LOGI("playSFX(%s, %d, %d): using managed SFX element (no extracted OGG/WAV found)",
                 container.c_str(), bankIndex, audioIndex);
    }
    const uint32_t id = AllocSound(assetPath, loop, is3D, x, y, z, true, hasAsset, true);
    if (!id) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // Notify onClientWorldSound (CClientGame.cpp:7503) for SFX playback.
    const uint32_t src = ServerWorld::LocalPlayer() ? ServerWorld::LocalPlayer() : ClientRootElement();
    AudioNoteWorldSound(src, bankIndex, audioIndex, is3D ? x : 0.0f, is3D ? y : 0.0f, is3D ? z : 0.0f);
    PushElement(L, id);
    return 1;
}

int l_playSFX(lua_State* L) { return PlaySfxCommon(L, false); }
int l_playSFX3D(lua_State* L) { return PlaySfxCommon(L, true); }

bool IsValidMissionAudioEventId(int eventId) {
    return (eventId >= kMissionAudioScriptBankFirst && eventId <= kMissionAudioScriptBankLast) ||
           (eventId >= kMissionAudioScriptSlotFirst && eventId <= kMissionAudioScriptSlotLast);
}

bool MissionSlotOwnedByCurrent(const MissionAudioSlot& slot) {
    const std::string& owner = ActiveVmOwner();
    const uint16_t netId = ActiveVmOwnerNet();
    const uint32_t gen = ActiveVmOwnerGeneration();
    if (slot.owner != owner) return false;
    if (slot.ownerNetId && netId && slot.ownerNetId != netId) return false;
    if (slot.ownerGeneration && gen && slot.ownerGeneration != gen) return false;
    return true;
}

int FindMissionSlotByHandle(uint32_t handle) {
    if (!handle) return -1;
    for (int i = 0; i < static_cast<int>(kMissionAudioSlotCount); ++i) {
        if (g_missionSlots[i].inUse && g_missionSlots[i].handle == handle) return i;
    }
    return -1;
}

void ClearNativeMissionSlot(uint8_t slotIndex) {
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (g_nativeAudio.audioEngine && g_nativeAudio.clearMissionAudio) {
        WorldRPG::GameTasks::Run([slotIndex]() {
            if (g_nativeAudio.audioEngine && g_nativeAudio.clearMissionAudio) {
                g_nativeAudio.clearMissionAudio(g_nativeAudio.audioEngine, slotIndex);
            }
        });
    }
#else
    (void)slotIndex;
#endif
}

// CLuaAudioDefs::PlayMissionPassedTune (CLuaAudioDefs.cpp:2519-2525,
// CStaticFunctionDefinitions.cpp:4748-4754): valid tunes are 1 and 2, mapped to beat tracks 11 and 12.
int l_playMissionPassedTune(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int tune = static_cast<int>(lua_tointeger(L, 1));
    if (tune < 1 || tune > 2) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!HasNativeAudioEngine()) {
        lua_pushboolean(L, 0);
        return 1;
    }
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (g_nativeAudio.audioEngine && g_nativeAudio.preloadBeatTrack && g_nativeAudio.playPreloadedBeatTrack) {
        const short trackId = static_cast<short>(tune + 10);
        WorldRPG::GameTasks::Run([trackId]() {
            if (g_nativeAudio.audioEngine && g_nativeAudio.preloadBeatTrack && g_nativeAudio.playPreloadedBeatTrack) {
                g_nativeAudio.preloadBeatTrack(g_nativeAudio.audioEngine, trackId);
                g_nativeAudio.playPreloadedBeatTrack(g_nativeAudio.audioEngine, 1);
            }
        });
    }
#endif
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaAudioDefs::RequestMissionAudio (CLuaAudioDefs.cpp:173-232)
int l_requestMissionAudio(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int eventId = static_cast<int>(lua_tointeger(L, 1));
    if (!IsValidMissionAudioEventId(eventId)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int preferredSlot = -1;
    if (lua_isnumber(L, 2)) {
        const int s = static_cast<int>(lua_tointeger(L, 2));
        if (s < 1 || s > static_cast<int>(kMissionAudioSlotCount)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        preferredSlot = s - 1;
    }
    if (!HasNativeAudioEngine()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint32_t handle = 0;
    int chosenSlot = -1;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        if (preferredSlot >= 0) {
            if (!g_missionSlots[preferredSlot].inUse) chosenSlot = preferredSlot;
        } else {
            for (int i = 0; i < static_cast<int>(kMissionAudioSlotCount); ++i) {
                if (!g_missionSlots[i].inUse) {
                    chosenSlot = i;
                    break;
                }
            }
        }
        if (chosenSlot < 0) {
            lua_pushboolean(L, 0);
            return 1;
        }
        auto& slot = g_missionSlots[chosenSlot];
        slot = MissionAudioSlot{};
        slot.inUse = true;
        slot.handle = handle = g_nextMissionHandle++;
        slot.eventId = eventId;
        slot.loaded = true;  // host mock ready immediately; Android queries native loading status
        slot.playing = false;
        slot.finished = false;
        slot.owner = ActiveVmOwner();
        slot.ownerNetId = ActiveVmOwnerNet();
        slot.ownerGeneration = ActiveVmOwnerGeneration();
    }
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (g_nativeAudio.audioEngine && g_nativeAudio.preloadMissionAudio) {
        const uint8_t slotIndex = static_cast<uint8_t>(chosenSlot);
        WorldRPG::GameTasks::Run([slotIndex, eventId]() {
            if (g_nativeAudio.audioEngine && g_nativeAudio.preloadMissionAudio) {
                g_nativeAudio.preloadMissionAudio(g_nativeAudio.audioEngine, slotIndex, eventId);
            }
        });
    }
#endif
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
}

int l_isMissionAudioLoaded(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
    int slotIdx = -1;
    bool loaded = false;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        slotIdx = FindMissionSlotByHandle(handle);
        if (slotIdx < 0 || !MissionSlotOwnedByCurrent(g_missionSlots[slotIdx])) {
            lua_pushboolean(L, 0);
            return 1;
        }
        loaded = g_missionSlots[slotIdx].loaded;
    }
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (g_nativeAudio.audioEngine && g_nativeAudio.getMissionAudioLoadingStatus) {
        int8_t status = 0;
        const uint8_t idx = static_cast<uint8_t>(slotIdx);
        WorldRPG::GameTasks::RunQuery([idx, &status]() {
            if (g_nativeAudio.audioEngine && g_nativeAudio.getMissionAudioLoadingStatus) {
                status = g_nativeAudio.getMissionAudioLoadingStatus(g_nativeAudio.audioEngine, idx);
            }
        });
        loaded = (status == 1);
    }
#endif
    lua_pushboolean(L, loaded ? 1 : 0);
    return 1;
}

int l_playMissionAudio(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
    int slotIdx = -1;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        slotIdx = FindMissionSlotByHandle(handle);
        if (slotIdx < 0 || !MissionSlotOwnedByCurrent(g_missionSlots[slotIdx])) {
            lua_pushboolean(L, 0);
            return 1;
        }
        g_missionSlots[slotIdx].playing = true;
        g_missionSlots[slotIdx].finished = true;  // host mock marks sample played cleanly
    }
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (g_nativeAudio.audioEngine && g_nativeAudio.playLoadedMissionAudio) {
        const uint8_t idx = static_cast<uint8_t>(slotIdx);
        WorldRPG::GameTasks::Run([idx]() {
            if (g_nativeAudio.audioEngine && g_nativeAudio.playLoadedMissionAudio) {
                g_nativeAudio.playLoadedMissionAudio(g_nativeAudio.audioEngine, idx);
            }
        });
    }
#endif
    lua_pushboolean(L, 1);
    return 1;
}

int l_isMissionAudioFinished(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
    int slotIdx = -1;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        slotIdx = FindMissionSlotByHandle(handle);
        if (slotIdx < 0 || !MissionSlotOwnedByCurrent(g_missionSlots[slotIdx])) {
            lua_pushboolean(L, 0);
            return 1;
        }
        finished = g_missionSlots[slotIdx].finished;
    }
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (g_nativeAudio.audioEngine && g_nativeAudio.isMissionAudioSampleFinished) {
        const uint8_t idx = static_cast<uint8_t>(slotIdx);
        WorldRPG::GameTasks::RunQuery([idx, &finished]() {
            if (g_nativeAudio.audioEngine && g_nativeAudio.isMissionAudioSampleFinished) {
                finished = g_nativeAudio.isMissionAudioSampleFinished(g_nativeAudio.audioEngine, idx);
            }
        });
    }
#endif
    lua_pushboolean(L, finished ? 1 : 0);
    return 1;
}

int l_releaseMissionAudio(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
    int slotIdx = -1;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        slotIdx = FindMissionSlotByHandle(handle);
        if (slotIdx < 0 || !MissionSlotOwnedByCurrent(g_missionSlots[slotIdx])) {
            lua_pushboolean(L, 0);
            return 1;
        }
        g_missionSlots[slotIdx] = MissionAudioSlot{};
    }
    ClearNativeMissionSlot(static_cast<uint8_t>(slotIdx));
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaAudioDefs::ReportPositionMissionAudioEvent (CLuaAudioDefs.cpp:421-432)
int l_reportPositionMissionAudioEvent(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float x = static_cast<float>(lua_tonumber(L, 1));
    const float y = static_cast<float>(lua_tonumber(L, 2));
    const float z = static_cast<float>(lua_tonumber(L, 3));
    const int eventId = static_cast<int>(lua_tointeger(L, 4));
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        eventId < kMissionAudioEventMin || eventId > kMissionAudioEventMax) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!HasNativeAudioEngine()) {
        lua_pushboolean(L, 0);
        return 1;
    }
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (g_nativeAudio.audioEngine && g_nativeAudio.reportMissionAudioPos) {
        const uint16_t ev = static_cast<uint16_t>(eventId);
        WorldRPG::GameTasks::Run([ev, x, y, z]() {
            if (g_nativeAudio.audioEngine && g_nativeAudio.reportMissionAudioPos) {
                CVector3 pos{x, y, z};
                g_nativeAudio.reportMissionAudioPos(g_nativeAudio.audioEngine, ev, &pos);
            }
        });
    }
#endif
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaAudioDefs::ReportVehicleMissionAudioEvent (CLuaAudioDefs.cpp:2933-2941)
int l_reportVehicleMissionAudioEvent(lua_State* L) {
    const uint32_t vehicleId = ElementIdOf(L, 1);
    if (!vehicleId || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* kind = WorldRPG::ServerWorld::ElementKindName(vehicleId);
    if (!kind || std::string_view(kind) != "vehicle") {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int eventId = static_cast<int>(lua_tointeger(L, 2));
    if (eventId < kMissionAudioEventMin || eventId > kMissionAudioEventMax) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!HasNativeAudioEngine()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    const bool hasPose = WorldRPG::ServerWorld::ElementPose(vehicleId, vx, vy, vz);
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (hasPose && g_nativeAudio.audioEngine && g_nativeAudio.reportMissionAudioPos) {
        const uint16_t ev = static_cast<uint16_t>(eventId);
        WorldRPG::GameTasks::Run([ev, vx, vy, vz]() {
            if (g_nativeAudio.audioEngine && g_nativeAudio.reportMissionAudioPos) {
                CVector3 pos{vx, vy, vz};
                g_nativeAudio.reportMissionAudioPos(g_nativeAudio.audioEngine, ev, &pos);
            }
        });
    }
#else
    (void)hasPose;
#endif
    lua_pushboolean(L, 1);
    return 1;
}

// Element position hooks so getElementPosition/setElementPosition work on sound3D elements.
int l_soundGetElementPosition(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (id) {
        std::lock_guard<std::mutex> lock(g_soundMu);
        if (const SoundSlot* slot = FindSound(id)) {
            lua_pushnumber(L, slot->posX);
            lua_pushnumber(L, slot->posY);
            lua_pushnumber(L, slot->posZ);
            return 3;
        }
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

int l_soundSetElementPosition(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (id && lua_isnumber(L, 2) && lua_isnumber(L, 3) && lua_isnumber(L, 4)) {
        const float x = static_cast<float>(lua_tonumber(L, 2));
        const float y = static_cast<float>(lua_tonumber(L, 3));
        const float z = static_cast<float>(lua_tonumber(L, 4));
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
            std::lock_guard<std::mutex> lock(g_soundMu);
            if (SoundSlot* slot = FindSound(id)) {
                slot->posX = x;
                slot->posY = y;
                slot->posZ = z;
                lua_pushboolean(L, 1);
                return 1;
            }
        }
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

}  // namespace

bool AudioSoundAlive(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    return FindSound(id) != nullptr;
}

bool AudioStopSound(uint32_t id) {
    bool hasPlayer = false;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        SoundSlot* slot = FindSound(id);
        if (!slot) return false;
        hasPlayer = slot->hasMediaPlayer;
        *slot = SoundSlot{};
    }
    if (hasPlayer) {
        const std::string text = std::to_string(id);
        ::WorldRPG::JniBridge::PostGameLine(12, text.c_str());
    }
    return true;
}

void AudioNoteSoundEnded(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    if (SoundSlot* slot = FindSound(id)) slot->ended = true;
}

std::vector<uint32_t> AudioCollectEndedSounds() {
    std::vector<uint32_t> ended;
    std::lock_guard<std::mutex> lock(g_soundMu);
    for (auto& slot : g_sounds) {
        if (!slot.alive || !slot.ended) continue;
        ended.push_back(slot.id);
        slot = SoundSlot{};
    }
    return ended;
}

void AudioNoteSoundLength(uint32_t id, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return;
    std::lock_guard<std::mutex> lock(g_soundMu);
    if (SoundSlot* slot = FindSound(id)) {
        slot->length = seconds;
    }
}

void AudioNoteSoundStarted(uint32_t id, const char* reason) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) return;
    SoundLifecycleEvent ev;
    ev.kind = SoundLifecycleEvent::Kind::Started;
    ev.id = id;
    ev.reasonOrName = (reason && reason[0]) ? reason : "play";
    g_soundEvents.push_back(std::move(ev));
}

void AudioNoteSoundStream(uint32_t id, bool success, double length, const char* streamName, const char* errorMsg) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) return;
    if (success && std::isfinite(length) && length >= 0.0) slot->length = length;
    if (streamName && streamName[0]) slot->metaTags["stream_name"] = streamName;
    SoundLifecycleEvent ev;
    ev.kind = SoundLifecycleEvent::Kind::Stream;
    ev.id = id;
    ev.success = success;
    ev.length = (std::isfinite(length) && length >= 0.0) ? length : 0.0;
    ev.reasonOrName = streamName ? streamName : "";
    ev.errorMessage = errorMsg ? errorMsg : "";
    g_soundEvents.push_back(std::move(ev));
}

void AudioNoteSoundChangedMeta(uint32_t id, const char* streamTitle) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) return;
    const std::string title = streamTitle ? streamTitle : "";
    slot->metaTags["stream_title"] = title;
    SoundLifecycleEvent ev;
    ev.kind = SoundLifecycleEvent::Kind::ChangedMeta;
    ev.id = id;
    ev.reasonOrName = title;
    g_soundEvents.push_back(std::move(ev));
}

void AudioNoteSoundFinishedDownload(uint32_t id, double length) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) return;
    if (std::isfinite(length) && length >= 0.0) slot->length = length;
    SoundLifecycleEvent ev;
    ev.kind = SoundLifecycleEvent::Kind::FinishedDownload;
    ev.id = id;
    ev.length = (std::isfinite(length) && length >= 0.0) ? length : 0.0;
    g_soundEvents.push_back(std::move(ev));
}

void AudioNoteSoundBeat(uint32_t id, double bpm) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) return;
    SoundLifecycleEvent ev;
    ev.kind = SoundLifecycleEvent::Kind::Beat;
    ev.id = id;
    ev.length = (std::isfinite(bpm) && bpm > 0.0) ? bpm : 0.0;
    g_soundEvents.push_back(std::move(ev));
}

void AudioNoteSoundAnalysis(uint32_t id, int leftLevel, int rightLevel,
                            const float* fft, int fftCount,
                            const float* wave, int waveCount) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) return;
    if (leftLevel >= 0 && rightLevel >= 0) {
        slot->liveLeftLevel = std::clamp(leftLevel, 0, 32768);
        slot->liveRightLevel = std::clamp(rightLevel, 0, 32768);
    }
    if (fft && fftCount > 0) {
        const int n = std::min(fftCount, 2048);
        slot->liveFft.assign(fft, fft + n);
    }
    if (wave && waveCount > 0) {
        const int n = std::min(waveCount, 2048);
        slot->liveWave.assign(wave, wave + n);
    }
}

void AudioUpdateSpatial3D(float listenerX, float listenerY, float listenerZ,
                          float rightX, float rightY, float rightZ) {
    struct MixPush {
        uint32_t id;
        bool pushVol;
        float vol;
        bool pushPan;
        float pan;
    };
    std::vector<MixPush> pushes;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        for (auto& slot : g_sounds) {
            if (!slot.alive || !slot.is3D) continue;
            float effVol = 0.0f;
            float effPan = 0.0f;
            ComputeSpatial3DMix(slot, listenerX, listenerY, listenerZ, rightX, rightY, rightZ, effVol, effPan);
            slot.effectiveVolume = effVol;
            slot.effectivePan = effPan;
            if (!slot.hasMediaPlayer || slot.paused) continue;
            const bool needVol = (slot.lastSentVolume < 0.0f) || (std::fabs(effVol - slot.lastSentVolume) > 0.01f);
            const bool needPan = (slot.lastSentPan < -2.0f) || (std::fabs(effPan - slot.lastSentPan) > 0.01f);
            if (needVol || needPan) {
                if (needVol) slot.lastSentVolume = effVol;
                if (needPan) slot.lastSentPan = effPan;
                pushes.push_back({slot.id, needVol, effVol, needPan, effPan});
            }
        }
    }
    for (const auto& p : pushes) {
        char line[64];
        if (p.pushVol) {
            std::snprintf(line, sizeof(line), "%u|%.3f", p.id, p.vol);
            ::WorldRPG::JniBridge::PostGameLine(13, line);
        }
        if (p.pushPan) {
            std::snprintf(line, sizeof(line), "%u|%.6f", p.id, p.pan);
            ::WorldRPG::JniBridge::PostGameLine(18, line);
        }
    }
}

bool AudioGetEffectiveMix(uint32_t id, float& effectiveVol, float& effectivePan) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) return false;
    effectiveVol = slot->is3D ? slot->effectiveVolume : slot->volume;
    effectivePan = slot->is3D ? slot->effectivePan : slot->pan;
    return true;
}

std::vector<SoundLifecycleEvent> AudioCollectEvents() {
    std::vector<SoundLifecycleEvent> out;
    std::lock_guard<std::mutex> lock(g_soundMu);
    out.swap(g_soundEvents);
    for (auto& slot : g_sounds) {
        if (!slot.alive || !slot.ended) continue;
        SoundLifecycleEvent ev;
        ev.kind = SoundLifecycleEvent::Kind::Stopped;
        ev.id = slot.id;
        ev.reasonOrName = "finished";
        out.push_back(std::move(ev));
        slot = SoundSlot{};
    }
    return out;
}

void AudioPlayFrontEndSound(int soundId) {
    if (soundId < 0 || soundId > 101) return;
#ifdef __ANDROID__
    ProbeNativeAudio();
    if (g_nativeAudio.audioEngine && g_nativeAudio.reportFrontend) {
        WorldRPG::GameTasks::Run([soundId]() {
            if (g_nativeAudio.audioEngine && g_nativeAudio.reportFrontend) {
                g_nativeAudio.reportFrontend(g_nativeAudio.audioEngine, soundId, 0.0f, 1.0f);
            }
        });
        return;
    }
#endif
    (void)soundId;
}

void AudioSetNativeBackendForTests(bool enabled) {
    g_hostNativeAudioMock.store(enabled, std::memory_order_relaxed);
}

void AudioResetSession() {
    AudioStopOwnedSounds();
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        g_soundEvents.clear();
        for (int i = 0; i < static_cast<int>(kMissionAudioSlotCount); ++i) {
            if (g_missionSlots[i].inUse) {
                g_missionSlots[i] = MissionAudioSlot{};
                ClearNativeMissionSlot(static_cast<uint8_t>(i));
            }
        }
    }
    g_showSound = false;
    g_radioChannel = 0;
    g_ambientGeneral = true;
    g_ambientGunfire = true;
    g_disabledWorldSounds.clear();
}

void AudioStopOwnedSounds(const std::string* owner, uint16_t netId, uint32_t generation) {
    std::vector<uint32_t> stopped;
    std::vector<uint8_t> clearedMission;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        for (auto& slot : g_sounds) {
            if (!slot.alive) continue;
            if (owner && (slot.owner != *owner || (netId && slot.ownerNetId != netId) ||
                          (generation && slot.ownerGeneration != generation))) continue;
            if (slot.hasMediaPlayer) stopped.push_back(slot.id);
            slot = SoundSlot{};
        }
        for (int i = 0; i < static_cast<int>(kMissionAudioSlotCount); ++i) {
            auto& ms = g_missionSlots[i];
            if (!ms.inUse) continue;
            if (owner && (ms.owner != *owner || (netId && ms.ownerNetId != netId) ||
                          (generation && ms.ownerGeneration != generation))) continue;
            ms = MissionAudioSlot{};
            clearedMission.push_back(static_cast<uint8_t>(i));
        }
    }
    for (uint32_t id : stopped) {
        char line[32];
        std::snprintf(line, sizeof(line), "%u", id);
        ::WorldRPG::JniBridge::PostGameLine(12, line);
    }
    for (uint8_t idx : clearedMission) ClearNativeMissionSlot(idx);
}

void InstallAudioLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"playSound", l_playSound},
        {"playSound3D", l_playSound3D},
        {"stopSound", l_stopSound},
        {"setSoundVolume", l_setSoundVolume},
        {"getSoundVolume", l_getSoundVolume},
        {"setSoundLooped", l_setSoundLooped},
        {"isSoundLooped", l_isSoundLooped},
        {"setSoundPaused", l_setSoundPaused},
        {"isSoundPaused", l_isSoundPaused},
        {"getSoundPosition", l_getSoundPosition},
        {"setSoundPosition", l_setSoundPosition},
        {"getSoundLength", l_getSoundLength},
        {"setSoundSpeed", l_setSoundSpeed},
        {"getSoundSpeed", l_getSoundSpeed},
        {"setSoundPan", l_setSoundPan},
        {"getSoundPan", l_getSoundPan},
        {"setSoundMinDistance", l_setSoundMinDistance},
        {"getSoundMinDistance", l_getSoundMinDistance},
        {"setSoundMaxDistance", l_setSoundMaxDistance},
        {"getSoundMaxDistance", l_getSoundMaxDistance},
        {"playSoundFrontEnd", l_playSoundFrontEnd},
        {"getRadioChannelName", l_getRadioChannelName},

        // Lot 1 additions
        {"setSoundPanningEnabled", l_setSoundPanningEnabled},
        {"isSoundPanningEnabled", l_isSoundPanningEnabled},
        {"getSoundProperties", l_getSoundProperties},
        {"setSoundProperties", l_setSoundProperties},
        {"getSoundBufferLength", l_getSoundBufferLength},
        {"getSoundBPM", l_getSoundBPM},
        {"getSoundLevelData", l_getSoundLevelData},

        // Lot 2 additions
        {"getSoundFFTData", l_getSoundFFTData},
        {"getSoundWaveData", l_getSoundWaveData},
        {"getSoundMetaTags", l_getSoundMetaTags},
        {"setSoundEffectEnabled", l_setSoundEffectEnabled},
        {"getSoundEffects", l_getSoundEffects},
        {"setSoundEffectParameter", l_setSoundEffectParameter},
        {"getSoundEffectParameters", l_getSoundEffectParameters},
        {"showSound", l_showSound},
        {"isShowSoundEnabled", l_isShowSoundEnabled},

        // Lot 3 additions
        {"setRadioChannel", l_setRadioChannel},
        {"getRadioChannel", l_getRadioChannel},
        {"getRadioPlaybackState", l_getRadioPlaybackState},
        {"setRadioPlaybackState", l_setRadioPlaybackState},
        {"setAmbientSoundEnabled", l_setAmbientSoundEnabled},
        {"isAmbientSoundEnabled", l_isAmbientSoundEnabled},
        {"resetAmbientSounds", l_resetAmbientSounds},
        {"setWorldSoundEnabled", l_setWorldSoundEnabled},
        {"isWorldSoundEnabled", l_isWorldSoundEnabled},
        {"resetWorldSounds", l_resetWorldSounds},
        {"playSFX", l_playSFX},
        {"playSFX3D", l_playSFX3D},
        {"getSFXStatus", l_getSFXStatus},

        // Lot 4 additions
        {"playMissionPassedTune", l_playMissionPassedTune},
        {"requestMissionAudio", l_requestMissionAudio},
        {"isMissionAudioLoaded", l_isMissionAudioLoaded},
        {"playMissionAudio", l_playMissionAudio},
        {"isMissionAudioFinished", l_isMissionAudioFinished},
        {"releaseMissionAudio", l_releaseMissionAudio},
        {"reportPositionMissionAudioEvent", l_reportPositionMissionAudioEvent},
        {"reportVehicleMissionAudioEvent", l_reportVehicleMissionAudioEvent},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_getglobal(L, "getElementPosition");
    if (lua_isfunction(L, -1)) {
        lua_pushcclosure(L, l_soundGetElementPosition, 1);
        lua_setglobal(L, "getElementPosition");
    } else {
        lua_pop(L, 1);
    }
    lua_getglobal(L, "setElementPosition");
    if (lua_isfunction(L, -1)) {
        lua_pushcclosure(L, l_soundSetElementPosition, 1);
        lua_setglobal(L, "setElementPosition");
    } else {
        lua_pop(L, 1);
    }
}

bool AudioNoteWorldSound(uint32_t sourceId, int group, int index, float x, float y, float z) {
    lua_State* L = ActiveVmState();
    if (!L) return true;
    const uint32_t src = sourceId ? sourceId : ClientRootElement();
    lua_pushinteger(L, group);
    lua_pushinteger(L, index);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    return ActiveVmFireElementEvent(L, "onClientWorldSound", src, 5);
}

}  // namespace WorldRPG::Script
