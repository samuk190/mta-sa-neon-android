#pragma once
// World shaders (engineApplyShaderToWorldTexture): a D3D9 effect rewritten as
// GLSL ES 1.00 programs the client runs on GTA's render thread in place of
// GTA's own for a replaced texture. They read GTA's vertex attributes
// (Position, Normal, GlobalColor, TexCoord0, bound at GTA's locations) and
// GTA's per-draw values (matrices, camera, lights, material colours) under
// reserved mta_ names that the client copies from GTA's program of that draw.
// Engine values desktop binds by semantic or annotation
// (CRenderItem.EffectParameters.cpp) become those; script parameters and
// textures stay uniforms the client uploads.

#include "FxCompiler.h"

#include <string>
#include <vector>

namespace WorldRPG::FxWorld {

// A uniform the client supplies (a script parameter, or TIME).
struct Uniform {
    std::string key;      // parameter Key (dxSetShaderValue name)
    std::string glsl;     // uniform name in the GLSL
    std::string type;     // GLSL type: float, vec2..4, mat4, int, bool, ...
    int count = 1;        // array length
    std::vector<float> defaults;
    bool time = false;    // TIME semantic: seconds, set every frame
};

// A sampler the client binds: a script texture (not the object's own texture).
struct Sampler {
    std::string glsl;      // sampler uniform name in the GLSL
    std::string texture;   // texture parameter Key
    Fx::SamplerState state;
};

struct Pass {
    // GTA stores texture coordinates either as floats or as shorts scaled by
    // 512 (its own shaders use TexCoord0 / 512.0); one vertex program each.
    std::string vertex;
    std::string vertexCompressedUv;
    std::string fragment;
    Fx::PassState state;
};

struct Effect {
    std::string technique;
    std::vector<Pass> passes;
    std::vector<Uniform> uniforms;
    std::vector<Sampler> samplers;
    // Samplers that read the mesh's own texture (textureState "0,Texture"): unit 0 as GTA bound it.
    std::vector<std::string> objectSamplers;
    // GTA values the programs read: (uniform in our GLSL, GTA's uniform of the same draw).
    std::vector<std::pair<std::string, std::string>> engineUniforms;
};

// Builds the first technique that translates. diagnostic says why none did.
bool Build(const Fx::Effect& effect, Effect& out, std::string& diagnostic);

}  // namespace WorldRPG::FxWorld
