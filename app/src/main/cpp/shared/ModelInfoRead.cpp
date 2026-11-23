#include "ModelInfoRead.h"

#include "../gtasa/GameTasks.h"

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace WorldRPG::Models {
namespace {

constexpr std::size_t kTxdOffset = 0x24;
constexpr std::size_t kFlagsOffset = 0x2c;
constexpr std::size_t kLodOffset = 0x38;
constexpr std::size_t kDamageVirtual = 0x18;

template <typename T>
T ReadAt(const void* model, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const unsigned char*>(model) + offset, sizeof(T));
    return value;
}

const char* kIdeNames[] = {
    "is_road",
    "draw_last",
    "additive",
    "ignore_lighting",
    "no_zbuffer_write",
    "dont_receive_shadows",
    "is_glass_type_1",
    "is_glass_type_2",
    "is_garage_door",
    "is_damagable",
    "is_tree",
    "is_palm",
    "does_not_collide_with_flyer",
    "is_tag",
    "disable_backface_culling",
    "is_breakable_statue",
    "is_crane",
};

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'A' && c <= 'Z') ch = static_cast<char>(c + 32);
    }
    return out;
}

unsigned SpecialType(std::uint16_t flags) { return (flags >> 11) & 0xFu; }

bool Bit(std::uint16_t flags, unsigned bit) { return ((flags >> bit) & 1u) != 0; }

#if defined(__ANDROID__)
bool InstructionsMatch() {
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return false;
    const auto* init = static_cast<const std::uint32_t*>(dlsym(lib, "_ZN14CBaseModelInfo4InitEv"));
    const auto* setTxd =
        static_cast<const std::uint32_t*>(dlsym(lib, "_ZN14CBaseModelInfo16SetTexDictionaryEPKcS1_"));
    auto** models = static_cast<void**>(dlsym(lib, "_ZN10CModelInfo16ms_modelInfoPtrsE"));
    dlclose(lib);
    // CBaseModelInfo::Init 0x45b3d8 and SetTexDictionary 0x45b4b4.
    const bool initOk = init && init[0] == 0x32103fe8u && init[9] == 0xb900380au && init[10] == 0x7900580bu;
    const bool txdOk = setTxd && setTxd[11] == 0x79004a60u;
    return initOk && txdOk && models;
}

void** ModelTable() {
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return nullptr;
    auto** models = static_cast<void**>(dlsym(lib, "_ZN10CModelInfo16ms_modelInfoPtrsE"));
    dlclose(lib);
    return models;
}
#endif

}  // namespace

bool IdeFlagFromName(std::string_view text, IdeFlag& flag) {
    const std::string lower = LowerAscii(text);
    for (std::size_t i = 0; i < sizeof(kIdeNames) / sizeof(kIdeNames[0]); ++i) {
        if (text == kIdeNames[i] || lower == kIdeNames[i]) {
            flag = static_cast<IdeFlag>(i);
            return true;
        }
    }
    return false;
}

bool ReadStockModelView(const void* model, StockModelView& out) {
    if (!model) return false;
    out.lod = ReadAt<float>(model, kLodOffset);
    out.flags = ReadAt<std::uint16_t>(model, kFlagsOffset);
    out.txd = ReadAt<std::uint16_t>(model, kTxdOffset);
    out.damageAtomic = false;
    const auto* vptr = ReadAt<const unsigned char*>(model, 0);
    if (!vptr) return true;
    using DamageFn = void* (*)(void*);
    const auto damage = ReadAt<DamageFn>(vptr, kDamageVirtual);
    if (damage) out.damageAtomic = damage(const_cast<void*>(model)) != nullptr;
    return true;
}

bool ReadIdeFlag(const StockModelView& view, IdeFlag flag) {
    // SetBaseModelInfoFlags / SetAtomicModelInfoFlags (0x5564c0 / 0x556530)
    // write these bits of the halfword at +0x2c: 1 draw-last, 2 additive,
    // 3 no z-buffer, 4 no shadows, 6 backface culling, 8 road, 10 no flyer
    // collision. Bits 11-14 are the special type (mask 0xffff87ff).
    const std::uint16_t flags = view.flags;
    const unsigned special = SpecialType(flags);
    switch (flag) {
        case IdeFlag::IsRoad:
            return Bit(flags, 8);
        case IdeFlag::DrawLast:
            return Bit(flags, 1);
        case IdeFlag::Additive:
        case IdeFlag::IgnoreLighting:
            return Bit(flags, 2);
        case IdeFlag::NoZBufferWrite:
            return Bit(flags, 3);
        case IdeFlag::DontReceiveShadows:
            return Bit(flags, 4);
        case IdeFlag::IsGlassType1:
            return special == 4;
        case IdeFlag::IsGlassType2:
            return special == 5;
        case IdeFlag::IsGarageDoor:
            return special == 7;
        case IdeFlag::IsTree:
            return special == 1;
        case IdeFlag::IsPalm:
            return special == 2;
        case IdeFlag::IsTag:
            return special == 6;
        case IdeFlag::IsBreakableStatue:
            return special == 11;
        case IdeFlag::IsCrane:
            return special == 9;
        case IdeFlag::IsDamagable:
            return view.damageAtomic;
        case IdeFlag::DoesNotCollideWithFlyer:
            return Bit(flags, 10);
        case IdeFlag::DisableBackfaceCulling:
            return !Bit(flags, 6);
    }
    return false;
}

bool ApplyLodDistance(float& lod, LodOverride& saved, float distance, bool extended) {
    if (!(distance > 0.f)) return false;
    if (!extended && distance > 325.f) distance = 325.f;
    if (!saved.saved) {
        saved.original = lod;
        saved.saved = true;
    }
    lod = distance;
    return true;
}

bool RestoreLodDistance(float& lod, LodOverride& saved) {
    if (!saved.saved) return false;
    lod = saved.original;
    saved = {};
    return true;
}

LiveModelStatus ReadLiveStockModel(std::uint16_t modelId, StockModelView& out) {
    if (modelId >= kStockModelCount) return LiveModelStatus::Missing;
#if !defined(__ANDROID__)
    (void)out;
    return LiveModelStatus::Unavailable;
#else
    static std::once_flag once;
    static bool abiOk = false;
    std::call_once(once, [] { abiOk = InstructionsMatch(); });
    if (!abiOk) return LiveModelStatus::Unavailable;
    LiveModelStatus status = LiveModelStatus::Unavailable;
    const bool ran = GameTasks::Run([&] {
        void** models = ModelTable();
        if (!models) {
            status = LiveModelStatus::Unavailable;
            return;
        }
        void* model = models[modelId];
        if (!model || !ReadStockModelView(model, out)) {
            status = LiveModelStatus::Missing;
            return;
        }
        status = LiveModelStatus::Ok;
    });
    if (!ran) return LiveModelStatus::Unavailable;
    return status;
#endif
}

namespace {
std::unordered_map<std::uint16_t, LodOverride> g_lodOverrides;
}

bool WriteLiveLodDistance(std::uint16_t modelId, float distance, bool extended) {
    if (modelId >= kStockModelCount || !(distance > 0.f)) return false;
#if !defined(__ANDROID__)
    (void)extended;
    return false;
#else
    bool wrote = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abiOk = false;
        std::call_once(once, [] { abiOk = InstructionsMatch(); });
        if (!abiOk) return;
        void** models = ModelTable();
        if (!models || !models[modelId]) return;
        float lod = ReadAt<float>(models[modelId], kLodOffset);
        LodOverride saved = g_lodOverrides.count(modelId) ? g_lodOverrides[modelId] : LodOverride{};
        if (!ApplyLodDistance(lod, saved, distance, extended)) return;
        std::memcpy(static_cast<unsigned char*>(models[modelId]) + kLodOffset, &lod, sizeof lod);
        g_lodOverrides[modelId] = saved;
        wrote = true;
    });
    return ran && wrote;
#endif
}

void RememberFlags(FlagRecord& record) {
    if (!record.haveOriginal) {
        record.original = record.flags;
        record.haveOriginal = true;
    }
}

void SetBit(std::uint16_t& flags, unsigned bit, bool on) {
    const std::uint16_t mask = static_cast<std::uint16_t>(1u << bit);
    if (on) flags = static_cast<std::uint16_t>(flags | mask);
    else flags = static_cast<std::uint16_t>(flags & ~mask);
}

void SetSpecial(std::uint16_t& flags, unsigned type) {
    flags = static_cast<std::uint16_t>((flags & 0x87FFu) | ((type & 0xFu) << 11));
}

void SetSpecialIf(std::uint16_t& flags, unsigned type, bool state) {
    const unsigned current = (flags >> 11) & 0xFu;
    if (state) SetSpecial(flags, type);
    else if (current == type) SetSpecial(flags, 0);
}

void SetRawFlags(FlagRecord& record, std::uint16_t incoming) {
    RememberFlags(record);
    incoming = static_cast<std::uint16_t>(incoming & 0xFF7Fu);
    incoming = static_cast<std::uint16_t>(incoming | (record.flags & 0x80u));
    record.flags = incoming;
}

void SetIdeWord(FlagRecord& record, std::uint32_t ideFlags, int modelType) {
    RememberFlags(record);
    const auto bit = [ideFlags](unsigned index) { return ((ideFlags >> index) & 1u) != 0; };
    std::uint16_t flags = static_cast<std::uint16_t>(record.flags & 0x80u);
    SetBit(flags, 6, true);
    if (bit(2)) {
        SetBit(flags, 1, true);
        SetBit(flags, 2, true);
    }
    SetBit(flags, 2, bit(3));
    SetBit(flags, 3, bit(6));
    SetBit(flags, 4, bit(7));
    SetBit(flags, 6, !bit(21));
    if (modelType == 1 || modelType == 3) {
        SetBit(flags, 8, bit(0));
        SetBit(flags, 10, bit(15));
        unsigned special = 0;
        if (bit(19)) special = 9;
        else if (bit(23)) special = 10;
        else if (bit(22)) special = 11;
        else if (bit(20)) special = 6;
        else if (bit(14)) special = 2;
        else if (bit(13)) special = 1;
        else if (bit(11)) special = 7;
        else if (bit(10)) special = 5;
        else if (bit(9)) special = 4;
        SetSpecial(flags, special);
    } else if (modelType == 5) {
        SetBit(flags, 2, bit(5));
    }
    record.flags = flags;
}

void SetOneIdeFlag(FlagRecord& record, IdeFlag flag, bool state) {
    std::uint16_t flags = record.flags;
    switch (flag) {
        case IdeFlag::IsRoad:
            SetBit(flags, 8, state);
            break;
        case IdeFlag::DrawLast:
            SetBit(flags, 1, state);
            if (state) SetBit(flags, 2, true);
            break;
        case IdeFlag::Additive:
        case IdeFlag::IgnoreLighting:
            SetBit(flags, 2, state);
            break;
        case IdeFlag::NoZBufferWrite:
            SetBit(flags, 3, state);
            break;
        case IdeFlag::DontReceiveShadows:
            SetBit(flags, 4, state);
            break;
        case IdeFlag::IsGlassType1:
            SetSpecialIf(flags, 4, state);
            break;
        case IdeFlag::IsGlassType2:
            SetSpecialIf(flags, 5, state);
            break;
        case IdeFlag::IsGarageDoor:
            SetSpecialIf(flags, 7, state);
            break;
        case IdeFlag::IsTree:
            SetSpecialIf(flags, 1, state);
            break;
        case IdeFlag::IsPalm:
            SetSpecialIf(flags, 2, state);
            break;
        case IdeFlag::IsTag:
            SetSpecialIf(flags, 6, state);
            break;
        case IdeFlag::IsBreakableStatue:
            SetSpecialIf(flags, 11, state);
            break;
        case IdeFlag::IsCrane:
            SetSpecialIf(flags, 9, state);
            break;
        case IdeFlag::IsDamagable:
            break;
        case IdeFlag::DoesNotCollideWithFlyer:
            SetBit(flags, 10, state);
            break;
        case IdeFlag::DisableBackfaceCulling:
            SetBit(flags, 6, !state);
            break;
    }
    record.flags = flags;
}

bool ResetRecordedFlags(FlagRecord& record) {
    const std::uint16_t original = record.haveOriginal ? record.original : record.flags;
    if (original == record.flags) return false;
    SetRawFlags(record, original);
    return true;
}

TxdTransfer SetTxdId(TxdRecord& record, std::uint16_t next) {
    if (!record.haveDefault) {
        record.original = record.id;
        record.haveDefault = true;
    }
    TxdTransfer transfer;
    transfer.from = record.id;
    transfer.to = next;
    transfer.refs = static_cast<std::size_t>(record.refs) + (record.rwObject ? 1u : 0u);
    record.id = next;
    return transfer;
}

bool ResetTxdId(TxdRecord& record, TxdTransfer& transfer) {
    if (!record.haveDefault) return false;
    const std::uint16_t original = record.original;
    transfer = SetTxdId(record, original);
    record.haveDefault = false;
    return true;
}

bool ResetLiveLodDistance(std::uint16_t modelId) {
    if (modelId >= kStockModelCount) return false;
#if !defined(__ANDROID__)
    return false;
#else
    bool restored = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abiOk = false;
        std::call_once(once, [] { abiOk = InstructionsMatch(); });
        if (!abiOk) return;
        auto found = g_lodOverrides.find(modelId);
        if (found == g_lodOverrides.end() || !found->second.saved) return;
        void** models = ModelTable();
        if (!models || !models[modelId]) return;
        float lod = 0.f;
        LodOverride saved = found->second;
        if (!RestoreLodDistance(lod, saved)) return;
        std::memcpy(static_cast<unsigned char*>(models[modelId]) + kLodOffset, &lod, sizeof lod);
        g_lodOverrides.erase(found);
        restored = true;
    });
    return ran && restored;
#endif
}

namespace {
std::unordered_map<std::uint16_t, FlagRecord> g_flagRecords;
std::unordered_map<std::uint16_t, TxdRecord> g_txdRecords;
}

bool WriteLiveRawFlags(std::uint16_t modelId, std::uint16_t incoming) {
    if (modelId >= kStockModelCount) return false;
#if !defined(__ANDROID__)
    (void)incoming;
    return false;
#else
    bool wrote = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abiOk = false;
        std::call_once(once, [] { abiOk = InstructionsMatch(); });
        if (!abiOk) return;
        void** models = ModelTable();
        if (!models || !models[modelId]) return;
        FlagRecord record = g_flagRecords.count(modelId) ? g_flagRecords[modelId] : FlagRecord{};
        record.flags = ReadAt<std::uint16_t>(models[modelId], kFlagsOffset);
        SetRawFlags(record, incoming);
        std::memcpy(static_cast<unsigned char*>(models[modelId]) + kFlagsOffset, &record.flags, sizeof record.flags);
        g_flagRecords[modelId] = record;
        wrote = true;
    });
    return ran && wrote;
#endif
}

bool WriteLiveIdeFlags(std::uint16_t modelId, std::uint32_t ideFlags) {
    if (modelId >= kStockModelCount) return false;
#if !defined(__ANDROID__)
    (void)ideFlags;
    return false;
#else
    bool wrote = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abiOk = false;
        std::call_once(once, [] { abiOk = InstructionsMatch(); });
        if (!abiOk) return;
        void** models = ModelTable();
        if (!models || !models[modelId]) return;
        void* model = models[modelId];
        FlagRecord record = g_flagRecords.count(modelId) ? g_flagRecords[modelId] : FlagRecord{};
        record.flags = ReadAt<std::uint16_t>(model, kFlagsOffset);
        int modelType = 0;
        const auto* vptr = ReadAt<const unsigned char*>(model, 0);
        if (vptr) {
            using TypeFn = int (*)(void*);
            const auto getType = ReadAt<TypeFn>(vptr, 0x28);
            if (getType) modelType = getType(model);
        }
        SetIdeWord(record, ideFlags, modelType);
        std::memcpy(static_cast<unsigned char*>(model) + kFlagsOffset, &record.flags, sizeof record.flags);
        g_flagRecords[modelId] = record;
        wrote = true;
    });
    return ran && wrote;
#endif
}

bool WriteLiveIdeFlag(std::uint16_t modelId, IdeFlag flag, bool state) {
    if (modelId >= kStockModelCount) return false;
#if !defined(__ANDROID__)
    (void)flag;
    (void)state;
    return false;
#else
    bool wrote = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abiOk = false;
        std::call_once(once, [] { abiOk = InstructionsMatch(); });
        if (!abiOk) return;
        void** models = ModelTable();
        if (!models || !models[modelId]) return;
        FlagRecord record = g_flagRecords.count(modelId) ? g_flagRecords[modelId] : FlagRecord{};
        record.flags = ReadAt<std::uint16_t>(models[modelId], kFlagsOffset);
        SetOneIdeFlag(record, flag, state);
        std::memcpy(static_cast<unsigned char*>(models[modelId]) + kFlagsOffset, &record.flags, sizeof record.flags);
        g_flagRecords[modelId] = record;
        wrote = true;
    });
    return ran && wrote;
#endif
}

FlagReset ResetLiveFlags(std::uint16_t modelId) {
    if (modelId >= kStockModelCount) return FlagReset::Missing;
#if !defined(__ANDROID__)
    return FlagReset::Missing;
#else
    FlagReset result = FlagReset::Missing;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abiOk = false;
        std::call_once(once, [] { abiOk = InstructionsMatch(); });
        if (!abiOk) return;
        void** models = ModelTable();
        if (!models || !models[modelId]) return;
        FlagRecord record = g_flagRecords.count(modelId) ? g_flagRecords[modelId] : FlagRecord{};
        record.flags = ReadAt<std::uint16_t>(models[modelId], kFlagsOffset);
        if (!ResetRecordedFlags(record)) {
            result = FlagReset::Unchanged;
            return;
        }
        std::memcpy(static_cast<unsigned char*>(models[modelId]) + kFlagsOffset, &record.flags, sizeof record.flags);
        g_flagRecords[modelId] = record;
        result = FlagReset::Changed;
    });
    return ran ? result : FlagReset::Missing;
#endif
}

bool WriteLiveTxdId(std::uint16_t modelId, std::uint16_t txdId) {
    if (modelId >= kStockModelCount) return false;
#if !defined(__ANDROID__)
    (void)txdId;
    return false;
#else
    bool wrote = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abiOk = false;
        std::call_once(once, [] { abiOk = InstructionsMatch(); });
        if (!abiOk) return;
        void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
        if (!lib) return;
        using RefFn = void (*)(int);
        auto removeRef = reinterpret_cast<RefFn>(dlsym(lib, "_ZN9CTxdStore9RemoveRefEi"));
        auto addRef = reinterpret_cast<RefFn>(dlsym(lib, "_ZN9CTxdStore6AddRefEi"));
        dlclose(lib);
        void** models = ModelTable();
        if (!models || !models[modelId] || !removeRef || !addRef) return;
        void* model = models[modelId];
        TxdRecord record = g_txdRecords.count(modelId) ? g_txdRecords[modelId] : TxdRecord{};
        record.id = ReadAt<std::uint16_t>(model, kTxdOffset);
        record.refs = ReadAt<std::uint16_t>(model, 0x22);
        record.rwObject = ReadAt<void*>(model, 0x40) != nullptr;
        const TxdTransfer transfer = SetTxdId(record, txdId);
        for (std::size_t i = 0; i < transfer.refs; ++i) removeRef(static_cast<int>(transfer.from));
        std::memcpy(static_cast<unsigned char*>(model) + kTxdOffset, &record.id, sizeof record.id);
        for (std::size_t i = 0; i < transfer.refs; ++i) addRef(static_cast<int>(transfer.to));
        g_txdRecords[modelId] = record;
        wrote = true;
    });
    return ran && wrote;
#endif
}

bool ResetLiveTxdId(std::uint16_t modelId) {
    if (modelId >= kStockModelCount) return false;
#if !defined(__ANDROID__)
    return false;
#else
    bool wrote = false;
    const bool ran = GameTasks::Run([&] {
        void** models = ModelTable();
        if (!models || !models[modelId]) return;
        auto found = g_txdRecords.find(modelId);
        if (found == g_txdRecords.end() || !found->second.haveDefault) {
            wrote = true;
            return;
        }
        const std::uint16_t original = found->second.original;
        if (!WriteLiveTxdId(modelId, original)) return;
        g_txdRecords[modelId].haveDefault = false;
        wrote = true;
    });
    return ran && wrote;
#endif
}

bool HoursInRange(int on, int off) { return on >= 0 && on <= 24 && off >= 0 && off <= 24; }

bool ApplyVisibleTime(VisibleTime& time, int on, int off) {
    if (!HoursInRange(on, off)) return false;
    if (!time.saved) {
        time.originalOn = time.on;
        time.originalOff = time.off;
        time.saved = true;
    }
    time.on = on;
    time.off = off;
    return true;
}

TimeQuery ReadLiveVisibleTime(std::uint16_t modelId, int& on, int& off) {
    if (modelId >= kStockModelCount) return TimeQuery::Missing;
#if !defined(__ANDROID__)
    (void)on;
    (void)off;
    return TimeQuery::Missing;
#else
    TimeQuery result = TimeQuery::Missing;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abiOk = false;
        std::call_once(once, [] { abiOk = InstructionsMatch(); });
        if (!abiOk) return;
        void** models = ModelTable();
        if (!models || !models[modelId]) return;
        void* model = models[modelId];
        const auto* vptr = ReadAt<const unsigned char*>(model, 0);
        if (!vptr) return;
        using TypeFn = int (*)(void*);
        using InfoFn = unsigned char* (*)(void*);
        const auto getType = ReadAt<TypeFn>(vptr, 0x28);
        const auto getInfo = ReadAt<InfoFn>(vptr, 0x30);
        if (!getType || getType(model) != 3 || !getInfo) {
            result = TimeQuery::Incompatible;
            return;
        }
        unsigned char* info = getInfo(model);
        if (!info) {
            result = TimeQuery::Incompatible;
            return;
        }
        on = info[0];
        off = info[1];
        result = TimeQuery::Ok;
    });
    return ran ? result : TimeQuery::Missing;
#endif
}

bool WriteLiveVisibleTime(std::uint16_t modelId, int hourOn, int hourOff) {
    if (modelId >= kStockModelCount || !HoursInRange(hourOn, hourOff)) return false;
#if !defined(__ANDROID__)
    return false;
#else
    bool wrote = false;
    const bool ran = GameTasks::Run([&] {
        static std::once_flag once;
        static bool abiOk = false;
        std::call_once(once, [] { abiOk = InstructionsMatch(); });
        if (!abiOk) return;
        void** models = ModelTable();
        if (!models || !models[modelId]) return;
        void* model = models[modelId];
        const auto* vptr = ReadAt<const unsigned char*>(model, 0);
        if (!vptr) return;
        using TypeFn = int (*)(void*);
        using InfoFn = unsigned char* (*)(void*);
        const auto getType = ReadAt<TypeFn>(vptr, 0x28);
        const auto getInfo = ReadAt<InfoFn>(vptr, 0x30);
        if (!getType || getType(model) != 3 || !getInfo) return;
        unsigned char* info = getInfo(model);
        if (!info) return;
        static std::unordered_map<std::uint16_t, VisibleTime> saved;
        VisibleTime time = saved.count(modelId) ? saved[modelId] : VisibleTime{};
        time.on = info[0];
        time.off = info[1];
        if (!ApplyVisibleTime(time, hourOn, hourOff)) return;
        info[0] = static_cast<unsigned char>(time.on);
        info[1] = static_cast<unsigned char>(time.off);
        saved[modelId] = time;
        wrote = true;
    });
    return ran && wrote;
#endif
}

int FindLiveTxdSlot(const char* name) {
    if (!name) return -2;
#if !defined(__ANDROID__)
    return -2;
#else
    int slot = -1;
    const bool ran = GameTasks::Run([&] {
        void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
        if (!lib) return;
        using FindFn = int (*)(const char*);
        auto find = reinterpret_cast<FindFn>(dlsym(lib, "_ZN9CTxdStore11FindTxdSlotEPKc"));
        dlclose(lib);
        if (find) slot = find(name);
    });
    return ran ? slot : -1;
#endif
}

}  // namespace WorldRPG::Models
