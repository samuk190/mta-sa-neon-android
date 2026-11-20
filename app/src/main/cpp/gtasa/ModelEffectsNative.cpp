#include "../shared/ModelEffects.h"
#include "GameEngine.h"
#include "../hook/MemoryPatcher.h"
#include <array>
#include <cstring>
#include <map>
#include <mutex>
#ifdef __ANDROID__
#include <dlfcn.h>
#endif
namespace WorldRPG::ModelEffects {
#ifdef __ANDROID__
namespace {
// libGTASA 2.10 arm64: Get2dEffect 0x45b5dc-0x45b6b8 uses 64-byte
// effects, model count +0x27 and store index +0x28. Init2dEffects stores
// count at 0x45b5d4; Add2dEffect loads/stores it at 0x45b6d8/e0.
struct Light {
    uint8_t r,g,b,a;
    float distance,range,corona,shadow;
    uint16_t flags;
    uint8_t flash,reflection,flare,multiplier;
    int8_t shadowDistance,ox,oy,oz;
    uint8_t pad[2];
    void* coronaTexture;
    void* shadowTexture;
};
struct NativeEffect { CVector position; uint8_t type,pad[3]; std::array<uint8_t,48> payload; };
static_assert(sizeof(Light)==48 && offsetof(Light,coronaTexture)==32);
static_assert(sizeof(NativeEffect)==64 && offsetof(NativeEffect,payload)==16);
template<class T> T Read(const void* p,size_t offset) { T value{}; std::memcpy(&value,static_cast<const uint8_t*>(p)+offset,sizeof value); return value; }
struct Api {
    void** models=nullptr;
    NativeEffect* (*original)(void*,int)=nullptr;
    void* (*readTexture)(const char*,const char*)=nullptr;
    int (*destroyTexture)(void*)=nullptr;
    void (*pushTxd)()=nullptr;
    void (*popTxd)()=nullptr;
    int (*findTxd)(const char*)=nullptr;
    void (*setTxd)(int,const char*)=nullptr;
    void (*addRef)(void*)=nullptr;
    void (*removeRef)(void*)=nullptr;
    int32_t* pluginOffset=nullptr;
    void* (*effectAtomic)(void*)=nullptr;
    void* (*store)()=nullptr;
} api;
struct State {
    void* model=nullptr;
    unsigned baselineCount=0;
    std::vector<NativeEffect*> effects;
    std::vector<std::unique_ptr<NativeEffect>> owned;
    std::vector<void*> textures;
    ~State() {
        if (api.destroyTexture) for (void* texture:textures) api.destroyTexture(texture);
        if (model && api.removeRef) api.removeRef(model);
    }
};
std::recursive_mutex nativeMutex;
std::map<void*,std::unique_ptr<State>> states;
NativeEffect* HookGet(void* model,int index) {
    std::lock_guard lock(nativeMutex);
    auto found=states.find(model);
    if (found!=states.end() && index>=0 && static_cast<size_t>(index)<found->second->effects.size())
        return found->second->effects[index];
    return api.original(model,index);
}
void* Model(uint32_t id) { return api.models && id<20000 ? api.models[id]:nullptr; }
NativeEffect* Original(void* model,unsigned index,unsigned count) {
    // Same selection as Get2dEffect; use the original count so custom entries
    // do not move the boundary between the native store and geometry plugin.
    void* vtable=Read<void*>(model,0);
    auto getType=Read<int (*)(void*)>(vtable,0x50);
    const int type=getType(model);
    void* object=Read<void*>(model,0x40);
    void* atomic=type==1?object:(type==2 && object?api.effectAtomic(object):nullptr);
    void* geometry=atomic?Read<void*>(atomic,0x30):nullptr;
    const int32_t offset=*api.pluginOffset;
    void* plugin=geometry && offset>=0 && offset<4096?Read<void*>(geometry,offset):nullptr;
    const unsigned pluginCount=plugin?Read<uint32_t>(plugin,0):0;
    if (pluginCount>count) return nullptr;
    const unsigned stored=count-pluginCount;
    if (index>=stored) return plugin?reinterpret_cast<NativeEffect*>(static_cast<uint8_t*>(plugin)+4)+(index-stored):nullptr;
    const int16_t start=Read<int16_t>(model,0x28);
    void* store=api.store();
    return start>=0 && store?reinterpret_cast<NativeEffect*>(static_cast<uint8_t*>(store)+8)+(start+index):nullptr;
}
std::string TextureName(void* texture) {
    // RwTextureSetName 0x27401c adds +0x20; 0x274078 terminates at +0x3f.
    return texture?std::string(static_cast<char*>(texture)+32,strnlen(static_cast<char*>(texture)+32,32)):std::string{};
}
Effect Decode(const NativeEffect& native) {
    Effect out; out.type=static_cast<e2dEffectType>(native.type); out.data.position=native.position;
    if (out.type==e2dEffectType::LIGHT) {
        Light light; std::memcpy(&light,native.payload.data(),sizeof light);
        auto& d=out.data;
        d.drawDistance=light.distance; d.lightRange=light.range; d.coronaSize=light.corona; d.shadowSize=light.shadow;
        d.flags=light.flags; d.flashType=static_cast<e2dCoronaFlashType>(light.flash); d.coronaReflection=light.reflection;
        d.flareType=light.flare; d.shadowMultiplier=light.multiplier; d.shadowDistance=light.shadowDistance;
        d.offset=CVector(light.ox,light.oy,light.oz);
        d.color=(uint32_t(light.a)<<24)|(uint32_t(light.r)<<16)|(uint32_t(light.g)<<8)|light.b;
        d.coronaName=TextureName(light.coronaTexture); d.shadowName=TextureName(light.shadowTexture);
    } else if (out.type==e2dEffectType::PARTICLE) {
        out.data.particleName=std::string(reinterpret_cast<const char*>(native.payload.data()),strnlen(reinterpret_cast<const char*>(native.payload.data()),24));
    } else if (out.type==e2dEffectType::ESCALATOR) {
        out.data.bottom=Read<CVector>(native.payload.data(),0); out.data.top=Read<CVector>(native.payload.data(),12);
        out.data.end=Read<CVector>(native.payload.data(),24); out.data.direction=native.payload[36];
    } else if (out.type!=e2dEffectType::SUN_GLARE) out.readable=false;
    return out;
}
void* LoadTexture(const std::string& name,State& state) {
    if (name.empty()) return nullptr;
    api.pushTxd();
    const int slot=api.findTxd("particle");
    if (slot>=0) api.setTxd(slot,nullptr);
    void* result=api.readTexture(name.c_str(),nullptr);
    api.popTxd();
    if (result) state.textures.push_back(result);
    return result;
}
class Native final:public Backend {
public:
    bool Capture(uint32_t id,bool& loaded,std::vector<Effect>& originals) override {
        std::lock_guard lock(nativeMutex);
        void* model=Model(id); if (!model) return false;
        loaded=Read<void*>(model,0x40)!=nullptr;
        const auto found=states.find(model);
        unsigned count=found==states.end()?Read<uint8_t>(model,0x27):found->second->baselineCount;
        originals.clear(); originals.reserve(count);
        for (unsigned i=0;i<count;i++) {
            NativeEffect* effect=Original(model,i,count); if (!effect) return false;
            originals.push_back(Decode(*effect));
        }
        return true;
    }
    bool Supports(e2dEffectType type) const override {
        // Other effects own persistent FX/road-sign/escalator bodies. Their
        // refresh/destruction hooks must be ported before mutating live models.
        return type==e2dEffectType::LIGHT || type==e2dEffectType::SUN_GLARE;
    }
    bool Commit(uint32_t id,const std::vector<Effect>& effective) override {
        std::lock_guard lock(nativeMutex);
        void* model=Model(id); if (!model || !Read<void*>(model,0x40) || effective.size()>255) return false;
        auto previous=states.find(model);
        unsigned baseCount=previous==states.end()?Read<uint8_t>(model,0x27):previous->second->baselineCount;
        if (effective.size()<baseCount) return false;
        std::vector<Effect> originalData;
        for (unsigned i=0;i<baseCount;i++) {
            NativeEffect* base=Original(model,i,baseCount); if (!base) return false;
            originalData.push_back(Decode(*base));
        }
        if (effective==originalData) {
            // Return to the native getter and release the retained model only
            // after the last resource's override/removal/custom effect leaves.
            // Get2dEffect +0x18 loads count; Init2dEffects +8 stores count +0x27.
            *(static_cast<uint8_t*>(model)+0x27)=static_cast<uint8_t>(baseCount);
            states.erase(model);
            return true;
        }
        auto next=std::make_unique<State>(); next->baselineCount=baseCount;
        for (unsigned i=0;i<effective.size();i++) {
            const auto& effect=effective[i];
            NativeEffect* base=i<baseCount?Original(model,i,baseCount):nullptr;
            if (base && effect==Decode(*base)) {
                next->effects.push_back(base); continue;
            }
            if((base&&!Supports(static_cast<e2dEffectType>(base->type))) ||
               (effect.type!=e2dEffectType::UNKNOWN&&!Supports(effect.type)))return false;
            auto native=std::make_unique<NativeEffect>();
            if (base) *native=*base;
            native->type=static_cast<uint8_t>(effect.type); native->position=effect.data.position;
            if (effect.type==e2dEffectType::LIGHT) {
                const auto& d=effect.data; Light light{};
                light.r=d.color>>16; light.g=d.color>>8; light.b=d.color; light.a=d.color>>24;
                light.distance=d.drawDistance; light.range=d.lightRange; light.corona=d.coronaSize; light.shadow=d.shadowSize;
                light.flags=d.flags; light.flash=static_cast<uint8_t>(d.flashType); light.reflection=d.coronaReflection;
                light.flare=d.flareType; light.multiplier=d.shadowMultiplier; light.shadowDistance=d.shadowDistance;
                light.ox=static_cast<int8_t>(d.offset.fX); light.oy=static_cast<int8_t>(d.offset.fY); light.oz=static_cast<int8_t>(d.offset.fZ);
                light.coronaTexture=LoadTexture(d.coronaName,*next); light.shadowTexture=LoadTexture(d.shadowName,*next);
                if ((!d.coronaName.empty() && !light.coronaTexture) || (!d.shadowName.empty() && !light.shadowTexture)) return false;
                std::memcpy(native->payload.data(),&light,sizeof light);
            }
            next->effects.push_back(native.get()); next->owned.push_back(std::move(native));
        }
        // All mutations are on the game thread. Publish only after every texture
        // is available; a failure leaves the previous effects and count intact.
        api.addRef(model); next->model=model; // Keep baseline geometry/textures alive while overridden.
        // Get2dEffect +0x18 loads count; Init2dEffects +8 stores count +0x27.
        *(static_cast<uint8_t*>(model)+0x27)=static_cast<uint8_t>(effective.size());
        states[model]=std::move(next);
        return true;
    }
};
bool Ready() {
    if (api.original) return true;
    void* lib=dlopen("libGTASA.so",RTLD_NOW|RTLD_NOLOAD|RTLD_GLOBAL); if (!lib) return false;
    auto sym=[&](const char* name){void* address=dlsym(lib,name);if(!address)ENGINE_LOGE("DIAG model 2DFX: missing native %s",name);return address;};
    void* get=sym("_ZN14CBaseModelInfo11Get2dEffectEi");
    void* init=sym("_ZN14CBaseModelInfo13Init2dEffectsEv");
    void* textureName=sym("_Z16RwTextureSetNameP9RwTexturePKc");
    const bool verified=get && init && textureName && Read<uint32_t>(get,0)==0xf81d0ff5 &&
        Read<uint32_t>(get,4)==0xa9014ff4 && Read<uint32_t>(get,8)==0xa9027bfd &&
        Read<uint32_t>(get,12)==0x910083fd && Read<uint32_t>(get,0x18)==0x39409e95 && Read<uint32_t>(init,8)==0x39009c1f &&
        Read<uint32_t>(textureName,0x1c)==0x91008260;
    if (!verified) { ENGINE_LOGE("DIAG model 2DFX: unverified mobile ABI; no native changes"); dlclose(lib); return false; }
    api.models=static_cast<void**>(sym("_ZN10CModelInfo16ms_modelInfoPtrsE"));
    api.pluginOffset=static_cast<int32_t*>(sym("g2dEffectPluginOffset"));
    api.effectAtomic=reinterpret_cast<decltype(api.effectAtomic)>(sym("_Z17Get2DEffectAtomicP7RpClump"));
    api.store=reinterpret_cast<decltype(api.store)>(sym("_ZN10CModelInfo16Get2dEffectStoreEv"));
    api.readTexture=reinterpret_cast<decltype(api.readTexture)>(sym("_Z13RwTextureReadPKcS0_"));
    api.destroyTexture=reinterpret_cast<decltype(api.destroyTexture)>(sym("_Z16RwTextureDestroyP9RwTexture"));
    api.pushTxd=reinterpret_cast<decltype(api.pushTxd)>(sym("_ZN9CTxdStore14PushCurrentTxdEv"));
    api.popTxd=reinterpret_cast<decltype(api.popTxd)>(sym("_ZN9CTxdStore13PopCurrentTxdEv"));
    api.findTxd=reinterpret_cast<decltype(api.findTxd)>(sym("_ZN9CTxdStore11FindTxdSlotEPKc"));
    api.setTxd=reinterpret_cast<decltype(api.setTxd)>(sym("_ZN9CTxdStore13SetCurrentTxdEiPKc"));
    api.addRef=reinterpret_cast<decltype(api.addRef)>(sym("_ZN14CBaseModelInfo6AddRefEv"));
    api.removeRef=reinterpret_cast<decltype(api.removeRef)>(sym("_ZN14CBaseModelInfo9RemoveRefEv"));
    if (!api.models || !api.pluginOffset || !api.effectAtomic || !api.store || !api.readTexture || !api.destroyTexture ||
        !api.pushTxd || !api.popTxd || !api.findTxd || !api.setTxd || !api.addRef || !api.removeRef) { dlclose(lib); return false; }
    static Hook::MemoryPatcher patcher;
    // Verified first 16 bytes are stack/register saves; no PC-relative instruction.
    const auto hook=patcher.installHook(reinterpret_cast<uintptr_t>(get),reinterpret_cast<uintptr_t>(&HookGet));
    if (!hook.success) { dlclose(lib); return false; }
    api.original=reinterpret_cast<decltype(api.original)>(hook.trampolineAddress);
    dlclose(lib); return true;
}
}
Backend* NativeBackend() { static Native native; return Ready()?&native:nullptr; }
#else
Backend* NativeBackend() { return nullptr; }
#endif
} // namespace WorldRPG::ModelEffects
