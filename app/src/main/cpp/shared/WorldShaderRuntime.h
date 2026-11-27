#pragma once

// Runtime GLES2 executor for MTA world shaders (engineApplyShaderToWorldTexture).
// Connects WorldShaders::Resolve + DxShader::CopyWorldSnapshot (FxWorld::Effect)
// to GTA's active GLES2 draw state on the render thread, and tracks visible world
// texture names for engineGetVisibleTextureNames.

#include "DxShader.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace WorldRPG::WorldShaderRuntime {

// Optional GTA engine uniform values when a caller supplies them directly instead of
// (or in addition to) reading them from an active GTA GLES2 program.
struct EngineUniformOverride {
    bool hasMatrices = false;
    float projMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float viewMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float objMatrix[16]  = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float cameraPos[3] = {0, 0, 0};
    float ambientLight[3] = {1, 1, 1};
    float dirLightDiffuse[3] = {1, 1, 1};
    float dirLightDir[3] = {0, 0, -1};
    float materialDiffuse[4] = {1, 1, 1, 1};
    float materialAmbient[4] = {1, 1, 1, 1};
    float materialEmissive[4] = {0, 0, 0, 0};
    float fogColor[3] = {0.5f, 0.5f, 0.5f};
    float fogDistances[3] = {100.f, 1000.f, 0.f};
};

// Executes any matching base and/or layered world shaders for a draw call inside the
// caller's active GLES2 context. Returns the number of shader passes executed (0 if no
// world shader matched or none could compile, meaning the caller should run GTA's own draw).
int DrawWithWorldShaders(const std::string& textureName,
                         uint32_t entityId,
                         int entityType,
                         bool compressedUv,
                         float timeSeconds,
                         uint32_t objectTexture,
                         const EngineUniformOverride* uniformOverride,
                         const std::function<void()>& emitDrawCall);

// Track visible texture names observed during world rendering (for engineGetVisibleTextureNames).
void RecordVisibleTexture(const std::string& textureName);
void BeginVisibleTextureFrame();
std::vector<std::string> VisibleTextureNames(const std::string& wildcardPattern);

// Hook status reported by gtasa/WorldShaderHook.cpp on Android.
void SetNativeDrawHookInstalled(bool installed);
bool IsNativeDrawHookInstalled();

// Cache management (called when a shader is released or Lua VM resets).
void InvalidateShader(uint32_t shaderId);
void Reset();

// Self-contained offscreen EGL/GLES2 verification helper used by host tests to prove
// dxCreateShader + dxSetShaderValue + engineApplyShaderToWorldTexture -> DrawWithWorldShaders
// renders real pixels through FxWorld::Effect.
bool RenderTestWorldQuad(const std::string& textureName,
                         uint32_t entityId,
                         int entityType,
                         float timeSeconds,
                         unsigned width,
                         unsigned height,
                         uint32_t objectPixelRgba,
                         DxShader::Image& outImage,
                         int* outPassesExecuted = nullptr);

}  // namespace WorldRPG::WorldShaderRuntime
