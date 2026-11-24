#include "FxWorld.h"

#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace WorldRPG::FxWorld {
namespace {

// Vertex stage for a pass without a vertex shader: desktop uses the
// fixed-function pipeline there. Position through GTA's matrices, the vertex
// colour times the material colour as COLOR0, texture coordinates as is.
constexpr const char* kDefaultVertex = R"(
float4x4 gMtaWorldViewProjection : WORLDVIEWPROJECTION;
float4 gMtaMaterialDiffuse;  // bound to MaterialDiffuse in Build
struct MtaWorldVertex { float3 Position:POSITION; float4 Diffuse:COLOR0; float2 TexCoord:TEXCOORD0; };
struct MtaWorldOutput { float4 Position:POSITION; float4 Diffuse:COLOR0; float4 Specular:COLOR1; float2 TexCoord:TEXCOORD0; float2 TexCoord1:TEXCOORD1; };
MtaWorldOutput MtaWorldVS(MtaWorldVertex v) {
    MtaWorldOutput o;
    o.Position = mul(float4(v.Position, 1), gMtaWorldViewProjection);
    o.Diffuse = v.Diffuse * gMtaMaterialDiffuse;
    o.Specular = float4(0, 0, 0, 0);
    o.TexCoord = v.TexCoord;
    o.TexCoord1 = v.TexCoord;
    return o;
}
)";

// The GTA values a stage reads, under reserved mta_ names so they cannot collide
// with script identifiers. The client copies each from GTA's program of the same
// draw (the uniform of the name without the prefix) before drawing with ours.
const std::map<std::string, std::string>& GtaUniforms() {
    static const std::map<std::string, std::string> uniforms = {
        {"mta_ProjMatrix", "uniform highp mat4 mta_ProjMatrix;"},
        {"mta_ViewMatrix", "uniform highp mat4 mta_ViewMatrix;"},
        {"mta_ObjMatrix", "uniform highp mat4 mta_ObjMatrix;"},
        {"mta_CameraPosition", "uniform highp vec3 mta_CameraPosition;"},
        {"mta_AmbientLightColor", "uniform lowp vec3 mta_AmbientLightColor;"},
        {"mta_DirLightDiffuseColor", "uniform lowp vec3 mta_DirLightDiffuseColor;"},
        {"mta_DirLightDirection", "uniform highp vec3 mta_DirLightDirection;"},
        {"mta_MaterialDiffuse", "uniform lowp vec4 mta_MaterialDiffuse;"},
        {"mta_MaterialAmbient", "uniform lowp vec4 mta_MaterialAmbient;"},
        {"mta_MaterialEmissive", "uniform lowp vec4 mta_MaterialEmissive;"},
        {"mta_FogColor", "uniform lowp vec3 mta_FogColor;"},
        {"mta_FogDistances", "uniform highp vec3 mta_FogDistances;"},
    };
    return uniforms;
}

// GLSL helpers some bindings need.
constexpr const char* kRigidInverse =
    "mat4 mtaRigidInverse(mat4 m) { mat3 r = mat3(m[0].xyz, m[1].xyz, m[2].xyz);"
    " mat3 t = mat3(vec3(r[0].x, r[1].x, r[2].x), vec3(r[0].y, r[1].y, r[2].y), vec3(r[0].z, r[1].z, r[2].z));"
    " vec3 p = -(t * m[3].xyz); return mat4(vec4(t[0], 0.0), vec4(t[1], 0.0), vec4(t[2], 0.0), vec4(p, 1.0)); }\n";

int Width(const std::string& type) {
    if (type == "float" || type == "int" || type == "bool") return 1;
    if (type.size() == 4 && (type.rfind("vec", 0) == 0)) return type[3] - '0';
    if (type.size() == 5 && (type.rfind("ivec", 0) == 0 || type.rfind("bvec", 0) == 0)) return type[4] - '0';
    if (type == "mat4") return 16;
    if (type == "mat3") return 9;
    if (type == "mat2") return 4;
    return 0;
}

// expr has `width` float components; give it GLSL type `type` (extend or swizzle).
std::string Fit(const std::string& expr, int width, const std::string& type, const char* extend = "0.0", const char* w = "1.0") {
    const int want = Width(type);
    if (want == width || type.rfind("mat", 0) == 0) return expr;
    static const char* swizzle[] = {"", "x", "xy", "xyz", "xyzw"};
    if (want < width && want >= 1 && want <= 4) return "(" + expr + ")." + swizzle[want];
    std::string out = type + "(" + expr;
    for (int i = width; i < want; ++i) out += std::string(", ") + (i == 3 ? w : extend);
    return out + ")";
}

// The GTA value a parameter stands for (semantic, or materialState/lightState/
// textureState annotation), as an expression of `type`. Empty: not an engine value.
std::string EngineValue(const std::string& semantic, const std::map<std::string, std::string>* annotations,
                        const std::string& type, bool& time, bool& rigidInverse) {
    std::string s = semantic;
    if (s.size() > 11 && s.compare(s.size() - 11, 11, "_MAIN_SCENE") == 0) s.resize(s.size() - 11);
    if (s == "WORLD") return "mta_ObjMatrix";
    if (s == "VIEW") return "mta_ViewMatrix";
    if (s == "PROJECTION") return "mta_ProjMatrix";
    if (s == "WORLDVIEW") return "(mta_ViewMatrix * mta_ObjMatrix)";
    if (s == "VIEWPROJECTION") return "(mta_ProjMatrix * mta_ViewMatrix)";
    if (s == "WORLDVIEWPROJECTION") return "(mta_ProjMatrix * mta_ViewMatrix * mta_ObjMatrix)";
    if (s == "WORLDINVERSETRANSPOSE") return "mta_ObjMatrix";  // rigid transforms: the same for normals
    if (s == "VIEWINVERSE") { rigidInverse = true; return "mtaRigidInverse(mta_ViewMatrix)"; }
    if (s == "VIEWINVERSETRANSPOSE") return "mta_ViewMatrix";  // rigid: (V^-1)^T is V
    if (s == "CAMERAPOSITION") return Fit("mta_CameraPosition", 3, type);
    if (s == "CAMERADIRECTION")  // GL view looks down -Z: the third row of the rotation, negated
        return Fit("(-vec3(mta_ViewMatrix[0].z, mta_ViewMatrix[1].z, mta_ViewMatrix[2].z))", 3, type, "0.0", "0.0");
    if (s == "LIGHTDIFFUSE") return Fit("mta_DirLightDiffuseColor", 3, type);
    if (s == "LIGHTAMBIENT") return Fit("mta_AmbientLightColor", 3, type);
    if (s == "LIGHTSPECULAR") return Fit("vec3(0.0)", 3, type);
    if (s == "LIGHTDIRECTION") return Fit("mta_DirLightDirection", 3, type, "0.0", "0.0");
    if (s == "TIME") { time = true; return {}; }
    if (annotations) {
        auto value = [&](const char* name) -> std::string {
            const auto found = annotations->find(name);
            return found == annotations->end() ? std::string() : found->second;
        };
        const std::string material = Fx::Key(value("materialstate"));
        if (material == "diffuse") return Fit("mta_MaterialDiffuse", 4, type);
        if (material == "ambient") return Fit("mta_MaterialAmbient", 4, type);
        if (material == "emissive") return Fit("mta_MaterialEmissive", 4, type);
        if (material == "specular") return Fit("vec4(0.0)", 4, type);
        if (material == "power") return Fit("0.0", 1, type);
        const std::string light = Fx::Key(value("lightstate"));
        if (light.find("diffuse") != std::string::npos) return Fit("mta_DirLightDiffuseColor", 3, type);
        if (light.find("ambient") != std::string::npos) return Fit("mta_AmbientLightColor", 3, type);
        if (light.find("direction") != std::string::npos) return Fit("mta_DirLightDirection", 3, type, "0.0", "0.0");
        const std::string renderState = Fx::Key(value("renderstate"));
        if (renderState == "fogcolor") return Fit("mta_FogColor", 3, type);
        if (renderState == "fogenable") return Fit("1.0", 1, type);
        if (renderState == "fogstart") return Fit("mta_FogDistances.x", 1, type);
        if (renderState == "fogend") return Fit("mta_FogDistances.y", 1, type);
    }
    return {};
}

// The object's own texture: textureState "0,Texture" (mta-helper.fx gTexture0).
bool ObjectTexture(const std::map<std::string, std::string>* annotations) {
    if (!annotations) return false;
    const auto found = annotations->find("texturestate");
    if (found == annotations->end()) return false;
    std::string v = Fx::Key(found->second);
    v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return c == ' '; }), v.end());
    return v == "0,texture";
}

struct Rewrite {
    const Fx::Effect& effect;
    Effect& out;
    std::set<std::string> uniformKeys;  // already listed in out.uniforms
    std::set<std::string> samplerNames;
};

// Vertex inputs (by semantic) -> GTA attributes.
bool Attribute(const std::string& semantic, const std::string& type, bool compressedUv, std::string& define, std::string& declare) {
    std::string s = semantic;
    while (!s.empty() && s.back() >= '0' && s.back() <= '9') s.pop_back();
    const std::string index = semantic.substr(s.size());
    if (s == "POSITION" && (index.empty() || index == "0")) {
        declare = "attribute highp vec3 Position;";
        define = Fit("Position", 3, type);
    } else if (s == "NORMAL" && (index.empty() || index == "0")) {
        declare = "attribute highp vec3 Normal;";
        define = Fit("Normal", 3, type, "0.0", "0.0");
    } else if (s == "COLOR" && (index.empty() || index == "0")) {
        declare = "attribute lowp vec4 GlobalColor;";
        define = Fit("GlobalColor", 4, type);
    } else if (s == "TEXCOORD" && (index.empty() || index == "0")) {
        declare = "attribute highp vec2 TexCoord0;";
        define = Fit(compressedUv ? "(TexCoord0 / 512.0)" : "TexCoord0", 2, type);
    } else {
        // Inputs GTA's vertex buffers do not carry (TEXCOORD1+, TANGENT, BINORMAL,
        // BLENDWEIGHT, COLOR1): zero, as an unset D3D9 vertex element reads.
        declare.clear();
        define = Fit(Width(type) == 1 ? "0.0" : "vec" + std::to_string(Width(type)) + "(0.0)", Width(type), type);
    }
    return true;
}

bool RewriteStage(Rewrite& r, std::string glsl, bool vertex, bool compressedUv, std::string& result, std::string& diagnostic) {
    std::istringstream lines(glsl);
    std::string line, body, header;
    std::set<std::string> declared;
    bool rigidInverse = false;
    static const std::regex attribute(R"(^\s*attribute\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+(\w+)\s*;\s*$)");
    static const std::regex uniform(R"(^\s*uniform\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;\s*$)");
    while (std::getline(lines, line)) {
        std::smatch m;
        if (vertex && std::regex_match(line, m, attribute)) {
            std::string define, declare;
            Attribute(m[2], m[1], compressedUv, define, declare);
            if (!declare.empty() && declared.insert(declare).second) header += declare + "\n";
            body += "#define " + m[2].str() + " " + define + "\n";
            continue;
        }
        if (std::regex_match(line, m, uniform)) {
            const std::string type = m[1], name = m[2], key = Fx::Key(name);
            const int count = m[3].matched ? std::stoi(m[3]) : 1;
            const auto annotation = r.effect.annotations.find(key);
            const auto* annotations = annotation == r.effect.annotations.end() ? nullptr : &annotation->second;
            if (type == "sampler2D") {
                const auto sampler = r.effect.samplers.find(key);
                const std::string texture = sampler == r.effect.samplers.end() ? key : sampler->second.texture;
                const auto textureAnnotation = r.effect.annotations.find(texture);
                if (ObjectTexture(textureAnnotation == r.effect.annotations.end() ? nullptr : &textureAnnotation->second)) {
                    // The texture GTA binds for the mesh (unit 0) is sampled here.
                    if (std::find(r.out.objectSamplers.begin(), r.out.objectSamplers.end(), name) == r.out.objectSamplers.end())
                        r.out.objectSamplers.push_back(name);
                    body += line + "\n";
                    continue;
                }
                if (r.samplerNames.insert(name).second) {
                    Sampler s;
                    s.glsl = name;
                    s.texture = texture;
                    if (sampler != r.effect.samplers.end()) s.state = sampler->second.state;
                    r.out.samplers.push_back(s);
                }
                body += line + "\n";
                continue;
            }
            const auto semantic = r.effect.semantics.find(key);
            bool time = false;
            const std::string engine = count == 1
                ? EngineValue(semantic == r.effect.semantics.end() ? std::string() : semantic->second, annotations, type, time, rigidInverse)
                : std::string();
            if (!engine.empty()) {
                body += "#define " + name + " " + engine + "\n";
                continue;
            }
            if (r.uniformKeys.insert(key).second) {
                Uniform u;
                u.key = key;
                u.glsl = name;
                u.type = type;
                u.count = count;
                u.time = time;
                r.out.uniforms.push_back(u);
            }
            body += line + "\n";
            continue;
        }
        body += line + "\n";
    }
    // GTA uniforms the stage now refers to.
    std::string gta;
    for (const auto& [name, declaration] : GtaUniforms()) {
        if (!std::regex_search(body, std::regex("\\b" + name + "\\b"))) continue;
        gta += declaration + "\n";
        const std::string engineName = name.substr(4);  // without mta_
        if (std::find_if(r.out.engineUniforms.begin(), r.out.engineUniforms.end(),
                         [&](const auto& e) { return e.first == name; }) == r.out.engineUniforms.end())
            r.out.engineUniforms.emplace_back(name, engineName);
    }
    // After #version, #extension and the precision lines the generator writes.
    size_t at = 0;
    for (int i = 0; i < 6; ++i) {
        const size_t next = body.find('\n', at);
        if (next == std::string::npos) break;
        const std::string first = body.substr(at, next - at);
        if (first.rfind("#version", 0) != 0 && first.rfind("#extension", 0) != 0 && first.rfind("precision", 0) != 0) break;
        at = next + 1;
    }
    body.insert(at, header + gta + (rigidInverse ? kRigidInverse : ""));
    result = std::move(body);
    (void)diagnostic;
    return true;
}

}  // namespace

bool Build(const Fx::Effect& effect, Effect& out, std::string& diagnostic) {
    const std::string hlsl = effect.hlsl + kDefaultVertex;
    Fx::Effect withDefault = effect;
    withDefault.semantics["gmtaworldviewprojection"] = "WORLDVIEWPROJECTION";
    withDefault.annotations["gmtamaterialdiffuse"]["materialstate"] = "Diffuse";
    std::string firstError;
    for (const auto& technique : effect.techniques) {
        Effect candidate;
        candidate.technique = technique.name;
        Rewrite rewrite{withDefault, candidate, {}, {}};
        std::map<std::string, std::vector<float>> defaults;
        bool ok = true;
        for (const auto& pass : technique.passes) {
            Pass built;
            built.state = pass.state;
            const std::string vertexEntry = pass.vertexEntry.empty() ? "MtaWorldVS" : pass.vertexEntry;
            std::string vs, fs, error;
            Fx::TranslateOptions vertexOptions;
            vertexOptions.vertex = true;
            if (!Fx::Translate(hlsl, vertexEntry, vertexOptions, vs, defaults, error) ||
                !Fx::Translate(hlsl, pass.pixelEntry, {}, fs, defaults, error) ||
                !RewriteStage(rewrite, vs, true, false, built.vertex, error) ||
                !RewriteStage(rewrite, vs, true, true, built.vertexCompressedUv, error) ||
                !RewriteStage(rewrite, fs, false, false, built.fragment, error)) {
                if (firstError.empty()) firstError = error;
                ok = false;
                break;
            }
            candidate.passes.push_back(std::move(built));
        }
        if (!ok || candidate.passes.empty()) continue;
        for (auto& u : candidate.uniforms) {
            const auto found = defaults.find(u.key);
            if (found != defaults.end()) u.defaults = found->second;
        }
        out = std::move(candidate);
        diagnostic.clear();
        return true;
    }
    diagnostic = firstError.empty() ? "no technique translates for world rendering" : firstError;
    return false;
}

}  // namespace WorldRPG::FxWorld
