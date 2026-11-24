#pragma once
// D3D9 effect (.fx) front end shared by the 2D shader path (DxShader) and the
// world shader path (FxWorld): comments, macros, annotations, sampler_state
// blocks, techniques and passes, and HLSL -> GLSL ES 1.00 translation through
// hlslparser. Nothing here needs a GL context.

#include <map>
#include <string>
#include <vector>

namespace WorldRPG::Fx {

constexpr size_t kMaxSourceBytes = 512 * 1024;

// Lower-case name, the key every parameter lookup uses (desktop is case-insensitive).
std::string Key(std::string name);

// GL enums kept as plain ints so this header needs no GL headers.
struct SamplerState {
    int minFilter = 0x2601;  // GL_LINEAR
    int magFilter = 0x2601;
    int wrapS = 0x812F;      // GL_CLAMP_TO_EDGE
    int wrapT = 0x812F;
};

struct Sampler {
    std::string name;     // sampler variable (Key)
    std::string texture;  // texture parameter it reads (Key)
    SamplerState state;
};

// The pass render states desktop applies around the draw (D3DRS_*). Unset
// fields keep the device's current state.
struct PassState {
    int alphaBlend = -1;          // -1 unset, 0 off, 1 on
    int srcBlend = -1, dstBlend = -1;  // GL blend factors
    int zEnable = -1, zWrite = -1;
    int cull = -1;                // 0 none, 1 cw, 2 ccw (D3DCULL)
    int alphaTest = -1;
    int alphaRef = -1;            // 0..255
    int fog = -1;
};

struct Pass {
    std::string vertexEntry;  // empty: fixed-function vertex stage
    std::string pixelEntry;
    PassState state;
};

struct Technique {
    std::string name;
    std::vector<Pass> passes;
};

struct Effect {
    std::string hlsl;  // annotations, sampler_state and technique blocks removed
    std::vector<Technique> techniques;
    std::map<std::string, Sampler> samplers;                               // by sampler Key
    std::map<std::string, std::map<std::string, std::string>> annotations;  // param Key -> name -> string value
    std::map<std::string, std::string> semantics;                           // param Key -> upper-case semantic
    std::string firstTexture;  // first texture or sampler2D declared (Key); the material size follows it
};

// Parses an effect. diagnostic names the first construct that is not supported.
bool Parse(const std::string& source, const std::map<std::string, std::string>& macros, Effect& out,
           std::string& diagnostic);

struct TranslateOptions {
    bool vertex = false;
};
// HLSL entry point -> GLSL ES 1.00. Global initialisers become defaults
// (param Key -> values) and are removed from the GLSL. The #extension line
// hlslparser emits for derivatives is moved to the top, where GLSL needs it.
bool Translate(const std::string& hlsl, const std::string& entry, const TranslateOptions& options,
               std::string& glsl, std::map<std::string, std::vector<float>>& defaults, std::string& diagnostic);

}  // namespace WorldRPG::Fx
