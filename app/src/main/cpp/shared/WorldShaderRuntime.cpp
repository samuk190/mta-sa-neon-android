#include "WorldShaderRuntime.h"

#include "FxCompiler.h"
#include "FxWorld.h"
#include "WorldShaderMatches.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace WorldRPG::WorldShaderRuntime {
namespace {

constexpr GLuint kAttrPosition = 0;
constexpr GLuint kAttrNormal = 1;
constexpr GLuint kAttrGlobalColor = 2;
constexpr GLuint kAttrTexCoord0 = 3;

struct CachedPass {
    GLuint program = 0;
    bool compressedUv = false;
    bool failed = false;
    std::vector<GLint> uniformLocs;
    std::vector<GLint> samplerLocs;
    std::vector<GLint> objectSamplerLocs;
    std::vector<GLint> engineUniformLocs;
};

struct CachedTexture {
    GLuint texId = 0;
    unsigned width = 0;
    unsigned height = 0;
    uint64_t revision = 0;
};

struct CachedShader {
    uint64_t effectRevision = 0;
    std::shared_ptr<const FxWorld::Effect> effect;
    std::vector<CachedPass> normalPasses;
    std::vector<CachedPass> compressedPasses;
    std::map<std::string, CachedTexture> textures;
};

std::mutex g_cacheMutex;
std::unordered_map<uint32_t, CachedShader> g_cache;
std::vector<uint32_t> g_pendingInvalidations;
bool g_pendingReset = false;

std::mutex g_visibleMutex;
std::set<std::string> g_visibleTextures;

std::atomic<bool> g_nativeHookInstalled{false};

std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

void DeleteCachedShaderGl(CachedShader& s) {
    for (auto& p : s.normalPasses) {
        if (p.program) glDeleteProgram(p.program);
    }
    for (auto& p : s.compressedPasses) {
        if (p.program) glDeleteProgram(p.program);
    }
    for (auto& [k, tex] : s.textures) {
        (void)k;
        if (tex.texId) glDeleteTextures(1, &tex.texId);
    }
    s.normalPasses.clear();
    s.compressedPasses.clear();
    s.textures.clear();
}

void DrainPendingGlDeletes() {
    std::vector<uint32_t> ids;
    bool doReset = false;
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        doReset = g_pendingReset;
        g_pendingReset = false;
        ids.swap(g_pendingInvalidations);
    }
    if (doReset) {
        for (auto& [id, cs] : g_cache) {
            (void)id;
            DeleteCachedShaderGl(cs);
        }
        g_cache.clear();
        return;
    }
    for (uint32_t id : ids) {
        auto it = g_cache.find(id);
        if (it != g_cache.end()) {
            DeleteCachedShaderGl(it->second);
            g_cache.erase(it);
        }
    }
}

GLuint CompileStage(GLenum type, const std::string& src) {
    GLuint sh = glCreateShader(type);
    if (!sh) return 0;
    const char* ptr = src.c_str();
    glShaderSource(sh, 1, &ptr, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

CachedPass CompilePass(const FxWorld::Effect& effect, const FxWorld::Pass& pass, bool compressedUv) {
    CachedPass cp;
    cp.compressedUv = compressedUv;
    const std::string& vsSrc = compressedUv ? pass.vertexCompressedUv : pass.vertex;
    GLuint vs = CompileStage(GL_VERTEX_SHADER, vsSrc);
    if (!vs) {
        cp.failed = true;
        return cp;
    }
    GLuint fs = CompileStage(GL_FRAGMENT_SHADER, pass.fragment);
    if (!fs) {
        glDeleteShader(vs);
        cp.failed = true;
        return cp;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, kAttrPosition, "Position");
    glBindAttribLocation(prog, kAttrNormal, "Normal");
    glBindAttribLocation(prog, kAttrGlobalColor, "GlobalColor");
    glBindAttribLocation(prog, kAttrTexCoord0, "TexCoord0");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(prog);
        cp.failed = true;
        return cp;
    }
    cp.program = prog;
    cp.uniformLocs.reserve(effect.uniforms.size());
    for (const auto& u : effect.uniforms) cp.uniformLocs.push_back(glGetUniformLocation(prog, u.glsl.c_str()));
    cp.samplerLocs.reserve(effect.samplers.size());
    for (const auto& s : effect.samplers) cp.samplerLocs.push_back(glGetUniformLocation(prog, s.glsl.c_str()));
    cp.objectSamplerLocs.reserve(effect.objectSamplers.size());
    for (const auto& os : effect.objectSamplers) cp.objectSamplerLocs.push_back(glGetUniformLocation(prog, os.c_str()));
    cp.engineUniformLocs.reserve(effect.engineUniforms.size());
    for (const auto& eu : effect.engineUniforms) cp.engineUniformLocs.push_back(glGetUniformLocation(prog, eu.first.c_str()));
    return cp;
}

void UploadUniformValue(GLint loc, const FxWorld::Uniform& u, const std::vector<float>& vals) {
    if (loc < 0 || vals.empty()) return;
    const GLsizei count = std::max(1, u.count);
    auto padded = [&](size_t need) {
        std::vector<float> buf(need, 0.0f);
        const size_t n = std::min(need, vals.size());
        std::memcpy(buf.data(), vals.data(), n * sizeof(float));
        return buf;
    };
    if (u.type == "float") {
        auto b = padded(static_cast<size_t>(count));
        glUniform1fv(loc, count, b.data());
    } else if (u.type == "vec2") {
        auto b = padded(static_cast<size_t>(count) * 2);
        glUniform2fv(loc, count, b.data());
    } else if (u.type == "vec3") {
        auto b = padded(static_cast<size_t>(count) * 3);
        glUniform3fv(loc, count, b.data());
    } else if (u.type == "vec4") {
        auto b = padded(static_cast<size_t>(count) * 4);
        glUniform4fv(loc, count, b.data());
    } else if (u.type == "mat2") {
        auto b = padded(static_cast<size_t>(count) * 4);
        glUniformMatrix2fv(loc, count, GL_FALSE, b.data());
    } else if (u.type == "mat3") {
        auto b = padded(static_cast<size_t>(count) * 9);
        glUniformMatrix3fv(loc, count, GL_FALSE, b.data());
    } else if (u.type == "mat4") {
        auto b = padded(static_cast<size_t>(count) * 16);
        glUniformMatrix4fv(loc, count, GL_FALSE, b.data());
    } else if (u.type == "int" || u.type == "bool") {
        std::vector<GLint> iv(static_cast<size_t>(count), 0);
        for (size_t i = 0; i < iv.size() && i < vals.size(); ++i) iv[i] = static_cast<GLint>(vals[i]);
        glUniform1iv(loc, count, iv.data());
    } else if (u.type == "ivec2" || u.type == "bvec2") {
        std::vector<GLint> iv(static_cast<size_t>(count) * 2, 0);
        for (size_t i = 0; i < iv.size() && i < vals.size(); ++i) iv[i] = static_cast<GLint>(vals[i]);
        glUniform2iv(loc, count, iv.data());
    } else if (u.type == "ivec3" || u.type == "bvec3") {
        std::vector<GLint> iv(static_cast<size_t>(count) * 3, 0);
        for (size_t i = 0; i < iv.size() && i < vals.size(); ++i) iv[i] = static_cast<GLint>(vals[i]);
        glUniform3iv(loc, count, iv.data());
    } else if (u.type == "ivec4" || u.type == "bvec4") {
        std::vector<GLint> iv(static_cast<size_t>(count) * 4, 0);
        for (size_t i = 0; i < iv.size() && i < vals.size(); ++i) iv[i] = static_cast<GLint>(vals[i]);
        glUniform4iv(loc, count, iv.data());
    }
}

void UploadEngineUniform(GLint dstLoc,
                         const std::string& mtaName,
                         const std::string& gtaName,
                         GLuint gtaProgram,
                         const EngineUniformOverride* overrideVals) {
    if (dstLoc < 0) return;
    if (gtaProgram != 0) {
        const GLint srcLoc = glGetUniformLocation(gtaProgram, gtaName.c_str());
        if (srcLoc >= 0) {
            float buf[16]{};
            glGetUniformfv(gtaProgram, srcLoc, buf);
            if (mtaName == "mta_ProjMatrix" || mtaName == "mta_ViewMatrix" || mtaName == "mta_ObjMatrix") {
                glUniformMatrix4fv(dstLoc, 1, GL_FALSE, buf);
            } else if (mtaName == "mta_MaterialDiffuse" || mtaName == "mta_MaterialAmbient" || mtaName == "mta_MaterialEmissive") {
                glUniform4fv(dstLoc, 1, buf);
            } else {
                glUniform3fv(dstLoc, 1, buf);
            }
            return;
        }
    }
    EngineUniformOverride fallback;
    const EngineUniformOverride& src = overrideVals ? *overrideVals : fallback;
    if (mtaName == "mta_ProjMatrix") glUniformMatrix4fv(dstLoc, 1, GL_FALSE, src.projMatrix);
    else if (mtaName == "mta_ViewMatrix") glUniformMatrix4fv(dstLoc, 1, GL_FALSE, src.viewMatrix);
    else if (mtaName == "mta_ObjMatrix") glUniformMatrix4fv(dstLoc, 1, GL_FALSE, src.objMatrix);
    else if (mtaName == "mta_CameraPosition") glUniform3fv(dstLoc, 1, src.cameraPos);
    else if (mtaName == "mta_AmbientLightColor") glUniform3fv(dstLoc, 1, src.ambientLight);
    else if (mtaName == "mta_DirLightDiffuseColor") glUniform3fv(dstLoc, 1, src.dirLightDiffuse);
    else if (mtaName == "mta_DirLightDirection") glUniform3fv(dstLoc, 1, src.dirLightDir);
    else if (mtaName == "mta_MaterialDiffuse") glUniform4fv(dstLoc, 1, src.materialDiffuse);
    else if (mtaName == "mta_MaterialAmbient") glUniform4fv(dstLoc, 1, src.materialAmbient);
    else if (mtaName == "mta_MaterialEmissive") glUniform4fv(dstLoc, 1, src.materialEmissive);
    else if (mtaName == "mta_FogColor") glUniform3fv(dstLoc, 1, src.fogColor);
    else if (mtaName == "mta_FogDistances") glUniform3fv(dstLoc, 1, src.fogDistances);
}

GLuint EnsureScriptTexture(CachedShader& cached,
                           const std::string& texKey,
                           const DxShader::WorldShaderSnapshot& snap,
                           const Fx::SamplerState& state) {
    auto texIt = snap.textures.find(texKey);
    if (texIt == snap.textures.end() || texIt->second.width == 0 || texIt->second.height == 0) return 0;
    auto& entry = cached.textures[texKey];
    if (!entry.texId) glGenTextures(1, &entry.texId);
    glBindTexture(GL_TEXTURE_2D, entry.texId);
    if (entry.revision != snap.revision || entry.width != texIt->second.width || entry.height != texIt->second.height) {
        const DxShader::Image& img = texIt->second;
        std::vector<uint8_t> rgba(static_cast<size_t>(img.width) * img.height * 4, 0);
        for (size_t i = 0; i < img.pixels.size() && i < static_cast<size_t>(img.width) * img.height; ++i) {
            const uint32_t argb = img.pixels[i];
            rgba[i * 4 + 0] = static_cast<uint8_t>((argb >> 16) & 0xFFu);
            rgba[i * 4 + 1] = static_cast<uint8_t>((argb >> 8) & 0xFFu);
            rgba[i * 4 + 2] = static_cast<uint8_t>(argb & 0xFFu);
            rgba[i * 4 + 3] = static_cast<uint8_t>((argb >> 24) & 0xFFu);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(img.width), static_cast<GLsizei>(img.height),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        entry.width = img.width;
        entry.height = img.height;
        entry.revision = snap.revision;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, state.minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, state.magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, state.wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, state.wrapT);
    return entry.texId;
}

void ApplyPassState(const Fx::PassState& st) {
    if (st.alphaBlend == 1) {
        glEnable(GL_BLEND);
        const GLenum src = st.srcBlend >= 0 ? static_cast<GLenum>(st.srcBlend) : GL_SRC_ALPHA;
        const GLenum dst = st.dstBlend >= 0 ? static_cast<GLenum>(st.dstBlend) : GL_ONE_MINUS_SRC_ALPHA;
        glBlendFunc(src, dst);
    } else if (st.alphaBlend == 0) {
        glDisable(GL_BLEND);
    } else if (st.srcBlend >= 0 || st.dstBlend >= 0) {
        const GLenum src = st.srcBlend >= 0 ? static_cast<GLenum>(st.srcBlend) : GL_SRC_ALPHA;
        const GLenum dst = st.dstBlend >= 0 ? static_cast<GLenum>(st.dstBlend) : GL_ONE_MINUS_SRC_ALPHA;
        glBlendFunc(src, dst);
    }
    if (st.cull == 0) {
        glDisable(GL_CULL_FACE);
    } else if (st.cull > 0) {
        glEnable(GL_CULL_FACE);
        glCullFace(static_cast<GLenum>(st.cull));
    }
    if (st.zEnable == 1) glEnable(GL_DEPTH_TEST);
    else if (st.zEnable == 0) glDisable(GL_DEPTH_TEST);
    if (st.zWrite == 1) glDepthMask(GL_TRUE);
    else if (st.zWrite == 0) glDepthMask(GL_FALSE);
}

int ExecuteShader(uint32_t shaderId,
                  bool compressedUv,
                  GLuint gtaProgram,
                  float timeSeconds,
                  GLuint objectTexture,
                  const EngineUniformOverride* uniformOverride,
                  const std::function<void()>& emitDrawCall) {
    DxShader::WorldShaderSnapshot snap;
    if (!DxShader::CopyWorldSnapshot(shaderId, snap) || !snap.effect || snap.effect->passes.empty()) return 0;

    auto& cached = g_cache[shaderId];
    if (cached.effect != snap.effect) {
        DeleteCachedShaderGl(cached);
        cached.effect = snap.effect;
        cached.effectRevision = snap.revision;
    }

    auto& passVec = compressedUv ? cached.compressedPasses : cached.normalPasses;
    if (passVec.size() != snap.effect->passes.size()) {
        passVec.resize(snap.effect->passes.size());
    }

    int passesRun = 0;
    for (size_t i = 0; i < snap.effect->passes.size(); ++i) {
        auto& cp = passVec[i];
        if (!cp.program && !cp.failed) {
            cp = CompilePass(*snap.effect, snap.effect->passes[i], compressedUv);
        }
        if (cp.failed || !cp.program) continue;

        glUseProgram(cp.program);

        // Bind object texture on unit 0 for objectSamplers.
        if (objectTexture != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, objectTexture);
        }
        for (GLint loc : cp.objectSamplerLocs) {
            if (loc >= 0) glUniform1i(loc, 0);
        }

        // Bind script samplers on texture units 1..N.
        for (size_t s = 0; s < snap.effect->samplers.size(); ++s) {
            const GLint loc = cp.samplerLocs[s];
            if (loc < 0) continue;
            const GLint unit = static_cast<GLint>(s + 1);
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
            const GLuint texId = EnsureScriptTexture(cached, snap.effect->samplers[s].texture, snap, snap.effect->samplers[s].state);
            glBindTexture(GL_TEXTURE_2D, texId);
            glUniform1i(loc, unit);
        }
        glActiveTexture(GL_TEXTURE0);

        // Upload script uniforms + TIME.
        for (size_t u = 0; u < snap.effect->uniforms.size(); ++u) {
            const auto& udesc = snap.effect->uniforms[u];
            const GLint loc = cp.uniformLocs[u];
            if (loc < 0) continue;
            if (udesc.time) {
                glUniform1f(loc, timeSeconds);
                continue;
            }
            auto it = snap.numbers.find(udesc.key);
            if (it != snap.numbers.end() && !it->second.empty()) {
                UploadUniformValue(loc, udesc, it->second);
            } else if (!udesc.defaults.empty()) {
                UploadUniformValue(loc, udesc, udesc.defaults);
            }
        }

        // Copy GTA engine uniforms (mta_ProjMatrix, mta_MaterialDiffuse, etc.).
        for (size_t e = 0; e < snap.effect->engineUniforms.size(); ++e) {
            UploadEngineUniform(cp.engineUniformLocs[e],
                                snap.effect->engineUniforms[e].first,
                                snap.effect->engineUniforms[e].second,
                                gtaProgram,
                                uniformOverride);
        }

        ApplyPassState(snap.effect->passes[i].state);
        emitDrawCall();
        ++passesRun;
    }
    return passesRun;
}

}  // namespace

void RecordVisibleTexture(const std::string& textureName) {
    if (textureName.empty()) return;
    std::lock_guard<std::mutex> lock(g_visibleMutex);
    if (g_visibleTextures.size() < 4096) g_visibleTextures.insert(textureName);
}

void BeginVisibleTextureFrame() {
    std::lock_guard<std::mutex> lock(g_visibleMutex);
    g_visibleTextures.clear();
}

std::vector<std::string> VisibleTextureNames(const std::string& wildcardPattern) {
    std::lock_guard<std::mutex> lock(g_visibleMutex);
    std::vector<std::string> out;
    const std::string pat = Lower(wildcardPattern.empty() ? "*" : wildcardPattern);
    for (const auto& name : g_visibleTextures) {
        const std::string low = Lower(name);
        if (WorldShaders::WildcardMatch(pat.c_str(), low.c_str())) out.push_back(name);
    }
    return out;
}

void SetNativeDrawHookInstalled(bool installed) { g_nativeHookInstalled.store(installed); }
bool IsNativeDrawHookInstalled() { return g_nativeHookInstalled.load(); }

void InvalidateShader(uint32_t shaderId) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_pendingInvalidations.push_back(shaderId);
}

void Reset() {
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_pendingReset = true;
        g_pendingInvalidations.clear();
    }
    std::lock_guard<std::mutex> lock(g_visibleMutex);
    g_visibleTextures.clear();
}

int DrawWithWorldShaders(const std::string& textureName,
                         uint32_t entityId,
                         int entityType,
                         bool compressedUv,
                         float timeSeconds,
                         uint32_t objectTexture,
                         const EngineUniformOverride* uniformOverride,
                         const std::function<void()>& emitDrawCall) {
    if (!textureName.empty()) RecordVisibleTexture(textureName);
    DrainPendingGlDeletes();
    if (!WorldShaders::Active() || !emitDrawCall) return 0;

    const WorldShaders::Layers layers = WorldShaders::Resolve(textureName, entityId, entityType);
    if (layers.base == 0 && layers.layers.empty()) return 0;

    // Save caller GLES2 state so GTA's renderer resumes cleanly after our passes.
    GLint prevProgram = 0;
    GLint prevActiveTex = GL_TEXTURE0;
    GLint prevTex0 = 0;
    GLboolean prevBlend = GL_FALSE;
    GLint prevBlendSrcRgb = GL_ONE, prevBlendDstRgb = GL_ZERO;
    GLint prevBlendSrcA = GL_ONE, prevBlendDstA = GL_ZERO;
    GLboolean prevCull = GL_FALSE;
    GLint prevCullMode = GL_BACK;
    GLboolean prevDepth = GL_FALSE;
    GLboolean prevDepthMask = GL_TRUE;

    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0);
    prevBlend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstA);
    prevCull = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);
    prevDepth = glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    const GLuint gtaProg = static_cast<GLuint>(prevProgram);
    const GLuint objTex = objectTexture ? static_cast<GLuint>(objectTexture) : static_cast<GLuint>(prevTex0);

    int totalPasses = 0;
    if (layers.base != 0) {
        totalPasses += ExecuteShader(layers.base, compressedUv, gtaProg, timeSeconds, objTex, uniformOverride, emitDrawCall);
    } else if (!layers.layers.empty()) {
        // Layered shader draws on top of GTA's original pass.
        emitDrawCall();
    }

    for (uint32_t layerId : layers.layers) {
        totalPasses += ExecuteShader(layerId, compressedUv, gtaProg, timeSeconds, objTex, uniformOverride, emitDrawCall);
    }

    // Restore caller GLES2 state.
    glUseProgram(gtaProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex0));
    glActiveTexture(static_cast<GLenum>(prevActiveTex));
    if (prevBlend) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    glBlendFuncSeparate(static_cast<GLenum>(prevBlendSrcRgb), static_cast<GLenum>(prevBlendDstRgb),
                        static_cast<GLenum>(prevBlendSrcA), static_cast<GLenum>(prevBlendDstA));
    if (prevCull) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);
    glCullFace(static_cast<GLenum>(prevCullMode));
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthMask(prevDepthMask);

    return totalPasses;
}

bool RenderTestWorldQuad(const std::string& textureName,
                         uint32_t entityId,
                         int entityType,
                         float timeSeconds,
                         unsigned width,
                         unsigned height,
                         uint32_t objectPixelRgba,
                         DxShader::Image& outImage,
                         int* outPassesExecuted) {
    if (width == 0 || height == 0) return false;
#ifndef __ANDROID__
    auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display = platform ? platform(0x31DD, nullptr, nullptr) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) return false;
    const EGLint cfgAttrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg = nullptr;
    EGLint count = 0;
    if (!eglChooseConfig(display, cfgAttrs, &cfg, 1, &count) || count < 1) return false;
    const EGLint surfAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(display, cfg, surfAttrs);
    if (surf == EGL_NO_SURFACE) return false;
    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLContext ctx = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ctxAttrs);
    if (ctx == EGL_NO_CONTEXT) {
        eglDestroySurface(display, surf);
        return false;
    }

    EGLDisplay oldDisp = eglGetCurrentDisplay();
    EGLContext oldCtx = eglGetCurrentContext();
    EGLSurface oldDraw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface oldRead = eglGetCurrentSurface(EGL_READ);

    if (!eglMakeCurrent(display, surf, surf, ctx)) {
        eglDestroyContext(display, ctx);
        eglDestroySurface(display, surf);
        return false;
    }

    GLuint fbo = 0, colorTex = 0, objTex = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    glGenTextures(1, &objTex);
    glBindTexture(GL_TEXTURE_2D, objTex);
    const uint8_t objPx[4] = {
        static_cast<uint8_t>((objectPixelRgba >> 24) & 0xFFu),
        static_cast<uint8_t>((objectPixelRgba >> 16) & 0xFFu),
        static_cast<uint8_t>((objectPixelRgba >> 8) & 0xFFu),
        static_cast<uint8_t>(objectPixelRgba & 0xFFu),
    };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, objPx);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Build a minimal GTA-style base program that carries the standard GTA uniforms.
    const char* gtaVs =
        "attribute vec3 Position;\n"
        "attribute vec3 Normal;\n"
        "attribute vec4 GlobalColor;\n"
        "attribute vec2 TexCoord0;\n"
        "uniform mat4 ProjMatrix;\n"
        "uniform mat4 ViewMatrix;\n"
        "uniform mat4 ObjMatrix;\n"
        "uniform vec4 MaterialDiffuse;\n"
        "varying vec2 vUv;\n"
        "varying vec4 vCol;\n"
        "void main() {\n"
        "  gl_Position = ProjMatrix * ViewMatrix * ObjMatrix * vec4(Position, 1.0);\n"
        "  vUv = TexCoord0;\n"
        "  vCol = GlobalColor * MaterialDiffuse + vec4(Normal * 0.0, 0.0);\n"
        "}\n";
    const char* gtaFs =
        "precision mediump float;\n"
        "uniform sampler2D Diffuse;\n"
        "varying vec2 vUv;\n"
        "varying vec4 vCol;\n"
        "void main() { gl_FragColor = texture2D(Diffuse, vUv) * vCol; }\n";
    GLuint gvs = CompileStage(GL_VERTEX_SHADER, gtaVs);
    GLuint gfs = CompileStage(GL_FRAGMENT_SHADER, gtaFs);
    GLuint gtaProg = glCreateProgram();
    glAttachShader(gtaProg, gvs);
    glAttachShader(gtaProg, gfs);
    glBindAttribLocation(gtaProg, kAttrPosition, "Position");
    glBindAttribLocation(gtaProg, kAttrNormal, "Normal");
    glBindAttribLocation(gtaProg, kAttrGlobalColor, "GlobalColor");
    glBindAttribLocation(gtaProg, kAttrTexCoord0, "TexCoord0");
    glLinkProgram(gtaProg);
    glDeleteShader(gvs);
    glDeleteShader(gfs);

    const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUseProgram(gtaProg);
    glUniformMatrix4fv(glGetUniformLocation(gtaProg, "ProjMatrix"), 1, GL_FALSE, ident);
    glUniformMatrix4fv(glGetUniformLocation(gtaProg, "ViewMatrix"), 1, GL_FALSE, ident);
    glUniformMatrix4fv(glGetUniformLocation(gtaProg, "ObjMatrix"), 1, GL_FALSE, ident);
    glUniform4f(glGetUniformLocation(gtaProg, "MaterialDiffuse"), 1.f, 1.f, 1.f, 1.f);
    glUniform1i(glGetUniformLocation(gtaProg, "Diffuse"), 0);

    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    struct QuadVert { float pos[3]; float norm[3]; float col[4]; float uv[2]; };
    const QuadVert verts[4] = {
        {{-1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}, {0.f, 0.f}},
        {{ 1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}, {1.f, 0.f}},
        {{-1.f,  1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}, {0.f, 1.f}},
        {{ 1.f,  1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}, {1.f, 1.f}},
    };
    glEnableVertexAttribArray(kAttrPosition);
    glEnableVertexAttribArray(kAttrNormal);
    glEnableVertexAttribArray(kAttrGlobalColor);
    glEnableVertexAttribArray(kAttrTexCoord0);
    glVertexAttribPointer(kAttrPosition, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVert), &verts[0].pos[0]);
    glVertexAttribPointer(kAttrNormal, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVert), &verts[0].norm[0]);
    glVertexAttribPointer(kAttrGlobalColor, 4, GL_FLOAT, GL_FALSE, sizeof(QuadVert), &verts[0].col[0]);
    glVertexAttribPointer(kAttrTexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVert), &verts[0].uv[0]);

    EngineUniformOverride ov;
    const int passes = DrawWithWorldShaders(textureName, entityId, entityType, false, timeSeconds, objTex, &ov, []() {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    });
    if (outPassesExecuted) *outPassesExecuted = passes;

    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 0);
    glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    outImage.width = width;
    outImage.height = height;
    outImage.pixels.resize(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < outImage.pixels.size(); ++i) {
        outImage.pixels[i] = (static_cast<uint32_t>(rgba[i * 4 + 3]) << 24) |
                             (static_cast<uint32_t>(rgba[i * 4 + 0]) << 16) |
                             (static_cast<uint32_t>(rgba[i * 4 + 1]) << 8) |
                             static_cast<uint32_t>(rgba[i * 4 + 2]);
    }

    // Tear down cached GL programs created inside this temporary test context before destroying ctx.
    for (auto& [id, cs] : g_cache) {
        (void)id;
        DeleteCachedShaderGl(cs);
    }
    g_cache.clear();
    glDeleteProgram(gtaProg);
    glDeleteTextures(1, &objTex);
    glDeleteTextures(1, &colorTex);
    glDeleteFramebuffers(1, &fbo);

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (oldDisp != EGL_NO_DISPLAY) eglMakeCurrent(oldDisp, oldDraw, oldRead, oldCtx);
    eglDestroyContext(display, ctx);
    eglDestroySurface(display, surf);
    return passes > 0;
}

}  // namespace WorldRPG::WorldShaderRuntime
