#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace WorldRPG::Models {

// libGTASA 2.10 arm64 SurfaceInfos_c. GetTyreGrip multiplies the id by 12 and
// reads the signed byte at this+0x90. GetBulletFx loads the flag word at
// record+2. 179 records follow the 144-byte adhesion table.
constexpr int kSurfaceCount = 179;
constexpr int kSurfaceStride = 12;
constexpr int kSurfaceIdLimit = 179;

enum class SurfaceProperty {
    Audio,
    StepWater,
    StepDust,
    Climbing,
    Bullet,
    Shoot,
    See,
    Skid,
    Tyre,
    Wet,
    Adhesion,
    Wheel,
    Fraction,
    Stairs,
    Rough,
    Steep,
    Glass,
    Pavement,
    Soft,
    Foot,
};

enum class SurfaceReadKind { None, Bool, Number, Text };

struct SurfaceRead {
    SurfaceReadKind kind = SurfaceReadKind::None;
    bool flag = false;
    int number = 0;
    const char* text = "";
};

bool SurfacePropertyFromName(const char* text, SurfaceProperty& property);
bool ReadSurfaceRecord(const unsigned char* record, SurfaceProperty property, SurfaceRead& out);
bool WriteSurfaceAudio(unsigned char* record, const char* name);
bool WriteSurfaceBool(unsigned char* record, SurfaceProperty property, bool enabled);
bool WriteSurfaceBullet(unsigned char* record, const char* name);
bool WriteSurfaceSkid(unsigned char* record, const char* name);
bool WriteSurfaceAdhesion(unsigned char* record, const char* name);
bool WriteSurfaceWheel(unsigned char* record, const char* name);
bool WriteSurfaceFraction(unsigned char* record, int value);
bool WriteSurfaceGrip(unsigned char* record, SurfaceProperty property, int value);
bool WriteSurfaceRoughness(unsigned char* record, int value);
void CopyOriginalSurface(int surfaceId, unsigned char* record);

bool ReadLiveSurface(int surfaceId, SurfaceProperty property, SurfaceRead& out);
bool WriteLiveSurfaceAudio(int surfaceId, const char* name);
bool WriteLiveSurfaceBool(int surfaceId, SurfaceProperty property, bool enabled);
bool WriteLiveSurfaceBullet(int surfaceId, const char* name);
bool WriteLiveSurfaceSkid(int surfaceId, const char* name);
bool WriteLiveSurfaceAdhesion(int surfaceId, const char* name);
bool WriteLiveSurfaceWheel(int surfaceId, const char* name);
bool WriteLiveSurfaceFraction(int surfaceId, int value);
bool WriteLiveSurfaceGrip(int surfaceId, SurfaceProperty property, int value);
bool WriteLiveSurfaceRoughness(int surfaceId, int value);
bool ResetLiveSurface(int surfaceId);
bool ResetLiveSurfaces();

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
void BindSurfaceLua(lua_State* L);
}
