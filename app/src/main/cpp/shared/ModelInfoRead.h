#pragma once

#include <cstdint>
#include <string_view>

namespace WorldRPG::Models {

// Stock GTA SA file-id split. CFileIDRuntimeSA.cpp STOCK_LAYOUT: DFF/model
// slots are [0, 20000) and TXD slots start at 20000. CModelInfo::Initialise
// clears 20000 pointers (memset size 0x27100). The phone does not relocate
// that table.
constexpr std::uint32_t kStockModelCount = 20000;
constexpr std::uint32_t kStockTxdBase = 20000;
constexpr std::uint32_t kStockTxdCount = 5000;

// libGTASA 2.10 arm64 CBaseModelInfo, from CBaseModelInfo::Init (0x45b3d8)
// and SetTexDictionary (0x45b4b4):
//   +0x24 uint16 texture dictionary (strh in SetTexDictionary+0x2c, 0x79004a60)
//   +0x2c uint16 flags, init stores 0xC0 (strh Init+0x28, 0x7900580b)
//   +0x38 float unscaled LOD, init stores 2000.0f (str Init+0x24, 0xb900380a)
// AsDamageAtomicModelInfoPtr is the virtual at vptr+0x18 (vtable slot after
// the two destructors and AsAtomicModelInfoPtr). The base function returns null.
struct StockModelView {
    float lod = 0.f;
    std::uint16_t flags = 0;
    std::uint16_t txd = 0;
    bool damageAtomic = false;
};

enum class IdeFlag {
    IsRoad,
    DrawLast,
    Additive,
    IgnoreLighting,
    NoZBufferWrite,
    DontReceiveShadows,
    IsGlassType1,
    IsGlassType2,
    IsGarageDoor,
    IsDamagable,
    IsTree,
    IsPalm,
    DoesNotCollideWithFlyer,
    IsTag,
    DisableBackfaceCulling,
    IsBreakableStatue,
    IsCrane,
};

bool IdeFlagFromName(std::string_view text, IdeFlag& flag);

// Reads one model object. A null vtable makes is_damagable false.
bool ReadStockModelView(const void* model, StockModelView& out);
bool ReadIdeFlag(const StockModelView& view, IdeFlag flag);

enum class LiveModelStatus { Ok, Missing, Unavailable };

// Ok: view is filled. Missing: the slot is empty. Unavailable: libGTASA is
// absent or its CBaseModelInfo instructions do not match the layout above.
LiveModelStatus ReadLiveStockModel(std::uint16_t modelId, StockModelView& out);

// First successful call keeps the previous distance. Later calls replace only
// the live value. Without extendedLod, values above 325 are stored as 325
// (CModelInfoSA::SetLODDistance, CModelInfoSA.cpp:1008-1024). There is no
// Project2DFX preload lease and no extended-LOD preference on the phone, so
// ModelChanging and the preference reapply have nothing to change.
struct LodOverride {
    bool saved = false;
    float original = 0.f;
};

bool ApplyLodDistance(float& lod, LodOverride& saved, float distance, bool extended);
bool RestoreLodDistance(float& lod, LodOverride& saved);

// False when the model slot cannot be written, including the host build.
bool WriteLiveLodDistance(std::uint16_t modelId, float distance, bool extended);
bool ResetLiveLodDistance(std::uint16_t modelId);

// CModelInfoSA::SetFlags / SetIdeFlags / SetIdeFlag / the reset compare
// (CModelInfoSA.cpp:559-709, CLuaEngineDefs.cpp:3470-3490). SetIdeFlag does
// not record an original. Bulk writes do, and they keep bIsColLoaded (bit 7).
struct FlagRecord {
    std::uint16_t flags = 0;
    bool haveOriginal = false;
    std::uint16_t original = 0;
};

void SetRawFlags(FlagRecord& record, std::uint16_t incoming);
void SetIdeWord(FlagRecord& record, std::uint32_t ideFlags, int modelType);
void SetOneIdeFlag(FlagRecord& record, IdeFlag flag, bool state);
bool ResetRecordedFlags(FlagRecord& record);

// CModelInfoSA::SetTextureDictionaryID / ResetTextureDictionaryID
// (CModelInfoSA.cpp:857-895). refs is the number of RemoveRef/AddRef calls.
struct TxdRecord {
    std::uint16_t id = 0;
    std::uint16_t refs = 0;
    bool rwObject = false;
    bool haveDefault = false;
    std::uint16_t original = 0;
};

struct TxdTransfer {
    std::uint16_t from = 0;
    std::uint16_t to = 0;
    std::size_t refs = 0;
};

TxdTransfer SetTxdId(TxdRecord& record, std::uint16_t next);
bool ResetTxdId(TxdRecord& record, TxdTransfer& transfer);

// Live writes. False when the model cannot be updated.
bool WriteLiveRawFlags(std::uint16_t modelId, std::uint16_t incoming);
bool WriteLiveIdeFlags(std::uint16_t modelId, std::uint32_t ideFlags);
bool WriteLiveIdeFlag(std::uint16_t modelId, IdeFlag flag, bool state);
enum class FlagReset { Missing, Unchanged, Changed };

FlagReset ResetLiveFlags(std::uint16_t modelId);
bool WriteLiveTxdId(std::uint16_t modelId, std::uint16_t txdId);
bool ResetLiveTxdId(std::uint16_t modelId);
int FindLiveTxdSlot(const char* name);

// CModelInfoSA::GetTime / SetTime (CModelInfoSA.cpp:924-956). A model whose
// GetModelType is not TIME (3) is incompatible: get reports 0, 24 and set
// fails. Time bytes come from the GetTimeInfo virtual (vptr+0x30): CTimeModelInfo
// at +0x48, CLodTimeModelInfo at +0x4c.
struct VisibleTime {
    int on = 0;
    int off = 24;
    bool saved = false;
    int originalOn = 0;
    int originalOff = 24;
};

bool HoursInRange(int on, int off);
bool ApplyVisibleTime(VisibleTime& time, int on, int off);

enum class TimeQuery { Missing, Incompatible, Ok };

TimeQuery ReadLiveVisibleTime(std::uint16_t modelId, int& on, int& off);
bool WriteLiveVisibleTime(std::uint16_t modelId, int on, int off);

}  // namespace WorldRPG::Models
