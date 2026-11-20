#pragma once
// Resource-owned 2DFX state, ported from CClient2DFXManager (Neon desktop).
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
struct lua_State;
namespace WorldRPG::ModelEffects {
struct CVector {
    float fX=0, fY=0, fZ=0;
    CVector() = default;
    CVector(float x,float y,float z): fX(x),fY(y),fZ(z) {}
    bool operator==(const CVector&) const = default;
};
struct CResource { std::string name; uint16_t netId=0; uint32_t generation=0; };
enum class e2dEffectType : std::uint8_t
{
    LIGHT = 0,
    PARTICLE = 1,
    UNKNOWN = 2,
    ATTRACTOR = 3,
    SUN_GLARE = 4,
    FURNITURE = 5,
    ENEX = 6,
    ROADSIGN = 7,
    TRIGGER_POINT = 8,
    COVER_POINT = 9,
    ESCALATOR = 10,
    NONE = 11,
};

enum class e2dCoronaFlashType : std::uint8_t
{
    DEFAULT = 0,
    RANDOM,
    RANDOM_WHEN_WET,
    ANIM_SPEED_4X,
    ANIM_SPEED_2X,
    ANIM_SPEED_1X,
    WARNLIGHT,
    TRAFFICLIGHT,
    TRAINCROSSING,
    BRIDGE,
    ONLY_RAIN,
    ON5_OFF5,
    ON6_OFF4,
    ON4_OFF6,
};

enum class e2dEffectProperty : std::uint8_t
{
    POSITION,
    FAR_CLIP_DISTANCE,
    LIGHT_RANGE,
    CORONA_SIZE,
    SHADOW_SIZE,
    SHADOW_MULT,
    FLASH_TYPE,
    CORONA_REFLECTION,
    FLARE_TYPE,
    SHADOW_DISTANCE,
    OFFSET,
    COLOR,
    CORONA_NAME,
    SHADOW_NAME,
    FLAGS,
    PARTICLE_NAME,
    SIZE,
    ROTATION,
    TEXT_1,
    TEXT_2,
    TEXT_3,
    TEXT_4,
    BOTTOM,
    TOP,
    END,
    DIRECTION,
};

struct S2DFXData
{
    bool operator==(const S2DFXData&) const = default;
    CVector position{};
    float drawDistance{};
    float lightRange{};
    float coronaSize{};
    float shadowSize{};
    std::uint8_t shadowMultiplier{};
    std::int8_t shadowDistance{};
    e2dCoronaFlashType flashType{e2dCoronaFlashType::DEFAULT};
    bool coronaReflection{};
    std::uint8_t flareType{};
    CVector offset{};
    std::uint32_t color{0xFFFFFFFFu};
    std::uint16_t flags{};
    std::string coronaName;
    std::string shadowName;
    std::string particleName;
    CVector size{};
    CVector rotation{};
    std::string text[4];
    CVector bottom{};
    CVector top{};
    CVector end{};
    std::uint8_t direction{};
};


struct Effect {
    e2dEffectType type=e2dEffectType::NONE;
    S2DFXData data;
    bool readable=true;
    bool operator==(const Effect&) const = default;
};
// All backend calls occur on the game thread on Android. Host tests supply an
// explicit backend; without one, no model or successful mutation is fabricated.
struct Backend {
    virtual ~Backend() = default;
    virtual bool Capture(uint32_t model, bool& loaded, std::vector<Effect>& originals)=0;
    virtual bool Commit(uint32_t model, const std::vector<Effect>& effective)=0;
    virtual bool Supports(e2dEffectType type) const=0;
};
class CClient2DFXManager {
public:
    static CClient2DFXManager& GetSingleton();
    bool IsValidModel(uint32_t model) const;
    bool IsModelLoaded(uint32_t model) const;
    uint32_t GetCount(uint32_t model,bool includeCustom=true) const;
    e2dEffectType GetType(uint32_t model,uint32_t index) const;
    bool GetData(uint32_t model,uint32_t index,S2DFXData& out) const;
    bool Add(CResource*,uint32_t,e2dEffectType,const S2DFXData&);
    bool Remove(CResource*,uint32_t,uint32_t);
    bool Restore(CResource*,uint32_t,uint32_t);
    bool ResetModel(CResource*,uint32_t);
    bool SetProperty(CResource*,uint32_t,uint32_t,e2dEffectProperty,const S2DFXData&);
    bool ResetProperty(CResource*,uint32_t,uint32_t,e2dEffectProperty);
    void ReleaseResource(CResource*);
    static bool IsPropertyValid(e2dEffectType,e2dEffectProperty);
    static void CopyProperty(S2DFXData&,const S2DFXData&,e2dEffectProperty);
};
CResource* CurrentOwner();
void InstallModelEffectsLua(lua_State*);
void DropResource(const std::string&,uint16_t,uint32_t);
void Reset();
Backend* NativeBackend();
#ifndef __ANDROID__
void SetTestBackend(Backend*);
#endif
} // namespace WorldRPG::ModelEffects
