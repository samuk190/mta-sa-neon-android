// Immediate particle calls. Desktop CLuaEffectDefs.cpp 78-653 and
// CStaticFunctionDefinitions.cpp 8119-8209. Each FxAdd* returns true after the
// manager call. CFx is not hooked, so a parsed call returns true and logs once.

#include "LuaFxApi.h"

#include "LuaVMStub.h"

#include "lua.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace WorldRPG::Script {
namespace {

bool g_diag = false;

const char* TypeName(int type) {
    switch (type) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TNUMBER: return "number";
        case LUA_TSTRING: return "string";
        case LUA_TTABLE: return "table";
        default: return type == LUA_TNONE ? "none" : "userdata";
    }
}

struct Args {
    lua_State* L = nullptr;
    const char* function = "";
    int index = 1;
    bool ok = true;

    void Fail(const char* category, const char* message) {
        if (!ok) return;
        ok = false;
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s @ '%s' [%s]", category, function, message);
        LUA_LOGI("WARNING: %s", buf);
    }

    void Type(const char* expected) {
        if (!ok) return;
        const int type = lua_type(L, index);
        const char* value = (type == LUA_TNUMBER || type == LUA_TSTRING) ? lua_tostring(L, index) : nullptr;
        char buf[256];
        if (value && value[0] != '\0') {
            std::snprintf(buf, sizeof(buf), "Expected %s at argument %d, got %s '%.64s'", expected, index, TypeName(type), value);
        } else {
            std::snprintf(buf, sizeof(buf), "Expected %s at argument %d, got %s", expected, index, TypeName(type));
        }
        Fail("Bad argument", buf);
        ++index;
    }

    bool NumberHere() const {
        const int type = lua_type(L, index);
        return (type == LUA_TNUMBER || type == LUA_TSTRING) && lua_isnumber(L, index);
    }

    bool TakeNumber(float& out, bool hasDefault, float fallback) {
        if (!ok) return false;
        const int type = lua_type(L, index);
        if (hasDefault && (type == LUA_TNONE || type == LUA_TNIL)) {
            out = fallback;
            ++index;
            return true;
        }
        if (type != LUA_TNUMBER && type != LUA_TSTRING) {
            Type("number");
            return false;
        }
        if (!lua_isnumber(L, index)) {
            Fail("Bad argument", "Expected number, got non-convertible string");
            ++index;
            return false;
        }
        const lua_Number number = lua_tonumber(L, index);
        if (std::isnan(number)) {
            Fail("Bad argument", "Expected number, got NaN");
            ++index;
            return false;
        }
        if (!std::isfinite(number)) {
            Fail("Bad argument", "Expected number within the supported range");
            ++index;
            return false;
        }
        out = static_cast<float>(number);
        ++index;
        return true;
    }

    bool Vec() {
        float x = 0.f, y = 0.f, z = 0.f;
        if (!ok) return false;
        const int type = lua_type(L, index);
        if (type != LUA_TNUMBER && type != LUA_TSTRING) {
            Type("vector3");
            return false;
        }
        return TakeNumber(x, false, 0.f) && TakeNumber(y, false, 0.f) && TakeNumber(z, false, 0.f);
    }

    bool VecDefault() {
        if (!ok) return false;
        const int type = lua_type(L, index);
        if (type == LUA_TNONE || type == LUA_TNIL) {
            ++index;
            return true;
        }
        if (!NumberHere()) {
            Type("vector3");
            return false;
        }
        float x = 0.f, y = 0.f, z = 0.f;
        return TakeNumber(x, true, 0.f) && TakeNumber(y, true, 0.f) && TakeNumber(z, true, 0.f);
    }

    bool BoolDefault(bool& out, bool fallback) {
        if (!ok) return false;
        const int type = lua_type(L, index);
        if (type == LUA_TNONE || type == LUA_TNIL) {
            out = fallback;
            ++index;
            return true;
        }
        if (type != LUA_TBOOLEAN) {
            Type("bool");
            return false;
        }
        out = lua_toboolean(L, index) != 0;
        ++index;
        return true;
    }
};

void Note() {
    if (g_diag) return;
    g_diag = true;
    LUA_LOGE("DIAG fxAdd: CFxManager particle calls are not hooked; a parsed call returns true");
}

int Done(lua_State* L, const Args& args) {
    if (!args.ok) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Note();
    lua_pushboolean(L, 1);
    return 1;
}

int BloodLike(lua_State* L, const char* name) {
    Args args{L, name};
    float count = 1.f, brightness = 1.f;
    args.Vec();
    args.Vec();
    args.TakeNumber(count, true, 1.f);
    args.TakeNumber(brightness, true, 1.f);
    return Done(L, args);
}

int DirOnly(lua_State* L, const char* name) {
    Args args{L, name};
    args.Vec();
    args.Vec();
    return Done(L, args);
}

int PosOnly(lua_State* L, const char* name) {
    Args args{L, name};
    args.Vec();
    return Done(L, args);
}

int Colored(lua_State* L, const char* name) {
    Args args{L, name};
    float r = 0.f, g = 0.f, b = 0.f, a = 0.f, scale = 0.f, count = 0.f;
    args.Vec();
    args.TakeNumber(r, true, 255.f);
    args.TakeNumber(g, true, 0.f);
    args.TakeNumber(b, true, 0.f);
    args.TakeNumber(a, true, 255.f);
    args.TakeNumber(scale, true, 1.f);
    args.TakeNumber(count, true, 1.f);
    return Done(L, args);
}

int FxAddBlood(lua_State* L) { return BloodLike(L, "fxAddBlood"); }
int FxAddWood(lua_State* L) { return BloodLike(L, "fxAddWood"); }
int FxAddTyreBurst(lua_State* L) { return DirOnly(L, "fxAddTyreBurst"); }
int FxAddPunchImpact(lua_State* L) { return DirOnly(L, "fxAddPunchImpact"); }
int FxAddTankFire(lua_State* L) { return DirOnly(L, "fxAddTankFire"); }
int FxAddWaterHydrant(lua_State* L) { return PosOnly(L, "fxAddWaterHydrant"); }
int FxAddWaterSplash(lua_State* L) { return PosOnly(L, "fxAddWaterSplash"); }
int FxAddBulletSplash(lua_State* L) { return PosOnly(L, "fxAddBulletSplash"); }
int FxAddFootSplash(lua_State* L) { return PosOnly(L, "fxAddFootSplash"); }
int FxAddDebris(lua_State* L) { return Colored(L, "fxAddDebris"); }
int FxAddGlass(lua_State* L) { return Colored(L, "fxAddGlass"); }

int FxAddSparks(lua_State* L) {
    Args args{L, "fxAddSparks"};
    float force = 0.f, count = 0.f, spread = 0.f, life = 0.f;
    bool blur = false;
    args.Vec();
    args.Vec();
    args.TakeNumber(force, true, 1.f);
    args.TakeNumber(count, true, 1.f);
    args.VecDefault();
    args.BoolDefault(blur, false);
    args.TakeNumber(spread, true, 1.f);
    args.TakeNumber(life, true, 1.f);
    (void)force;
    (void)count;
    (void)spread;
    (void)life;
    (void)blur;
    return Done(L, args);
}

int FxAddBulletImpact(lua_State* L) {
    Args args{L, "fxAddBulletImpact"};
    float smoke = 0.f, sparks = 0.f, intensity = 0.f;
    args.Vec();
    args.Vec();
    args.TakeNumber(smoke, true, 1.f);
    args.TakeNumber(sparks, true, 1.f);
    args.TakeNumber(intensity, true, 1.f);
    (void)smoke;
    (void)sparks;
    (void)intensity;
    return Done(L, args);
}

int FxAddGunshot(lua_State* L) {
    Args args{L, "fxAddGunshot"};
    bool sparks = true;
    args.Vec();
    args.Vec();
    args.BoolDefault(sparks, true);
    (void)sparks;
    return Done(L, args);
}

const char* kParticles[] = {"blood",        "boat_splash", "bubble", "car_debris", "collision_smoke", "gunshell", "sand",  "sand2",
                            "huge_smoke",    "smoke",       "spark",  "spark2",     "splash",          "wake",     "water_splash",
                            "wheel_dirt",    "glass"};

bool ParticleName(const char* name) {
    if (!name) return false;
    for (const char* known : kParticles) {
        if (std::strcmp(name, known) == 0) return true;
    }
    return false;
}

int FxCreateParticle(lua_State* L) {
    // ArgumentParser<FxCreateParticle>. Colors are divided by 255 before CFx::AddParticle.
    if (lua_type(L, 1) != LUA_TSTRING || !ParticleName(lua_tostring(L, 1))) {
        return luaL_error(L, "Bad argument @ 'fxCreateParticle' [Expected fx-particle at argument 1, got %s]", TypeName(lua_type(L, 1)));
    }
    Args args{L, "fxCreateParticle", 2};
    float red = 0.f, green = 0.f, blue = 0.f, alpha = 0.f;
    args.Vec();
    args.Vec();
    if (!args.TakeNumber(red, false, 0.f) || !args.TakeNumber(green, false, 0.f) || !args.TakeNumber(blue, false, 0.f) ||
        !args.TakeNumber(alpha, false, 0.f)) {
        return luaL_error(L, "Bad argument @ 'fxCreateParticle' [Expected number]");
    }
    bool randomColor = false, randomSize = false;
    float count = 1.f, brightness = 1.f, size = 0.3f, life = 1.f;
    if (!args.BoolDefault(randomColor, false) || !args.TakeNumber(count, true, 1.f) || count < 0.f || !args.TakeNumber(brightness, true, 1.f) ||
        !args.TakeNumber(size, true, 0.3f) || !args.BoolDefault(randomSize, false) || !args.TakeNumber(life, true, 1.f)) {
        return luaL_error(L, "Bad argument @ 'fxCreateParticle' [Expected optional particle argument]");
    }
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
    (void)randomColor;
    (void)randomSize;
    (void)count;
    (void)brightness;
    (void)size;
    (void)life;
    Note();
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    const struct {
        const char* name;
        lua_CFunction fn;
    } binds[] = {
        {"fxAddBlood", FxAddBlood},
        {"fxAddWood", FxAddWood},
        {"fxAddSparks", FxAddSparks},
        {"fxAddTyreBurst", FxAddTyreBurst},
        {"fxAddBulletImpact", FxAddBulletImpact},
        {"fxAddPunchImpact", FxAddPunchImpact},
        {"fxAddDebris", FxAddDebris},
        {"fxAddGlass", FxAddGlass},
        {"fxAddWaterHydrant", FxAddWaterHydrant},
        {"fxAddGunshot", FxAddGunshot},
        {"fxAddTankFire", FxAddTankFire},
        {"fxAddWaterSplash", FxAddWaterSplash},
        {"fxAddBulletSplash", FxAddBulletSplash},
        {"fxAddFootSplash", FxAddFootSplash},
        {"fxCreateParticle", FxCreateParticle},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace

void InstallFxLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
