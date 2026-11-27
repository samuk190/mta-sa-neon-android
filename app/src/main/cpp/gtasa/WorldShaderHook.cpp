#include "WorldShaderHook.h"

#include "../shared/WorldShaderRuntime.h"

#include <android/log.h>
#include <dlfcn.h>

#define WSH_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WorldRPG_WorldShader", __VA_ARGS__)

namespace WorldRPG::Gta {

void InstallWorldShaderHook(void* gtasaHandle) {
    if (!gtasaHandle) {
        WorldShaderRuntime::SetNativeDrawHookInstalled(false);
        return;
    }
    // Probe libGTASA.so GLES draw/texture dispatch symbols safely via dlsym.
    // Raw ARM64 inline trampolines are NOT installed without verified libGTASA 2.10
    // disassembly (see .cursor/skills/mta-sync-engine-safety/SKILL.md).
    void* drawElements = dlsym(gtasaHandle, "_Z17emu_glDrawElementsjijPKv");
    if (!drawElements) drawElements = dlsym(gtasaHandle, "emu_glDrawElements");
    void* texSet = dlsym(gtasaHandle, "_Z14emu_TextureSetP9RwTexture");
    if (!texSet) texSet = dlsym(gtasaHandle, "emu_TextureSet");
    WSH_LOGI("DIAG WorldShaderHook probe: emu_glDrawElements=%p emu_TextureSet=%p (runtime executor ready; inline draw trampoline awaits libGTASA 2.10 disassembly)",
             drawElements, texSet);
    WorldShaderRuntime::SetNativeDrawHookInstalled(false);
}

}  // namespace WorldRPG::Gta
