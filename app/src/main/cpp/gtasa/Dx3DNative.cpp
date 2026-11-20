#include "../shared/LuaDx3DApi.h"
#include "GameTasks.h"
#include "GameEngine.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#ifdef __ANDROID__
#include <dlfcn.h>
#endif
namespace WorldRPG::JniBridge { bool LoadPixels(const std::string&,std::vector<uint32_t>&,unsigned&,unsigned&); }
namespace WorldRPG::Dx3D {
#ifdef __ANDROID__
namespace {
template<class T> T Read(const void* p,size_t off) { T value{}; std::memcpy(&value,static_cast<const uint8_t*>(p)+off,sizeof value); return value; }
struct NativeVertex { Vec3 position,normal; uint8_t r,g,b,a; float u,v; };
struct Matrix { Vec3 right; uint32_t flags=0; Vec3 up; uint32_t pad1=0; Vec3 at;uint32_t pad2=0; Vec3 position;uint32_t pad3=0; };
static_assert(sizeof(NativeVertex)==36 && sizeof(Matrix)==64);
struct Api {
    void* scene=nullptr;
    void** models=nullptr;
    void* (*transform)(NativeVertex*,unsigned,Matrix*,unsigned)=nullptr;
    int (*render)(int)=nullptr;
    int (*end)()=nullptr;
    int (*stateSet)(int,void*)=nullptr;
    int (*stateGet)(int,void*)=nullptr;
    void* (*rasterCreate)(int,int,int,int)=nullptr;
    void* (*rasterLock)(void*,uint8_t,int)=nullptr;
    void* (*rasterUnlock)(void*)=nullptr;
    int (*rasterDestroy)(void*)=nullptr;
    Matrix* (*frameLtm)(void*)=nullptr;
    void* (*frameTransform)(void*,const Matrix*,int)=nullptr;
    int (*atomicDestroy)(void*)=nullptr;
    int (*clumpDestroy)(void*)=nullptr;
    int (*frameDestroy)(void*)=nullptr;
    void* (*clumpRender)(void*)=nullptr;
    void (*addRef)(void*)=nullptr;
    void (*removeRef)(void*)=nullptr;
    std::array<void*,6> acceptedTypes{};
    std::array<void*,2> rejectedTypes{};
} api;
using EndCallback=void* (*)(void*);
std::map<void*,EndCallback> callbacks;
std::mutex callbackMutex;
void Set(int state,uintptr_t value) { api.stateSet(state,reinterpret_cast<void*>(value)); }
struct SavedStates {
    static constexpr std::array<int,10> names={1,2,6,7,8,9,10,11,12,20};
    std::array<uintptr_t,10> values{};
    SavedStates() { for(size_t i=0;i<names.size();i++) api.stateGet(names[i],&values[i]); }
    ~SavedStates() { for(size_t i=0;i<names.size();i++) Set(names[i],values[i]); }
};
void Vertices(const std::vector<Vertex>& input,int primitive) {
    if (input.empty()) return;
    std::vector<NativeVertex> vertices; vertices.reserve(input.size());
    for (const auto& v:input) vertices.push_back({v.position,{0,0,1},uint8_t(v.color>>16),uint8_t(v.color>>8),uint8_t(v.color),uint8_t(v.color>>24),v.u,v.v});
    constexpr int types[]={0,6,1,2,3,4,5};
    if (primitive<1 || primitive>6) return;
    if (api.transform(vertices.data(),vertices.size(),nullptr,1|8|16)) { api.render(types[primitive]); api.end(); }
}
void* Raster(const std::string& source) {
    if (source.empty()) return nullptr;
    std::vector<uint32_t> pixels; unsigned width=0,height=0;
    if (!JniBridge::LoadPixels(source,pixels,width,height) || !width || !height || width>4096 || height>4096 || pixels.size()!=size_t(width)*height) return nullptr;
    void* raster=api.rasterCreate(width,height,32,0x504); if (!raster) return nullptr;
    auto* data=static_cast<uint8_t*>(api.rasterLock(raster,0,1|4));
    // RwRasterCreate 0x272ea8 stores width/height at +0x18/+0x1c;
    // _rwOpenGLRasterLock loads stride +0x24 at 0x23f42c and 0x23f698.
    const int stride=Read<int>(raster,0x24);
    if (!data || stride<int(width*4) || stride>int(width*4+65536)) {
        if(data)api.rasterUnlock(raster); api.rasterDestroy(raster); return nullptr;
    }
    for(unsigned y=0;y<height;y++) for(unsigned x=0;x<width;x++) {
        uint32_t p=pixels[size_t(y)*width+x]; uint8_t* out=data+size_t(y)*stride+x*4;
        out[0]=p>>16;out[1]=p>>8;out[2]=p;out[3]=p>>24;
    }
    api.rasterUnlock(raster); return raster;
}
bool ModelUsable(uint32_t id) {
    if (!api.models || id>=20000 || !api.models[id]) return false;
    void* model=api.models[id]; if (!Read<void*>(model,0x40)) return false;
    void* vtable=Read<void*>(model,0);
    bool accepted=false;
    // All native model-info vtables have these slots (GetRwModelType is +0x50).
    // Compare known type-method pointers instead of guessing a PC model enum.
    for(unsigned i=0;i<10;i++) {
        void* method=Read<void*>(vtable,i*sizeof(void*));
        for(void* bad:api.rejectedTypes) if(bad && method==bad) return false;
        for(void* good:api.acceptedTypes) accepted=accepted || (good && method==good);
    }
    return accepted;
}
void ModelDraw(const Draw& d) {
    if (!ModelUsable(d.model)) return;
    void* info=api.models[d.model]; api.addRef(info);
    // CEntity::CreateRwObject at 0x4cb2d0 / 0x4cb2fc calls vtable +0x58.
    auto create=Read<void* (*)(void*)>(Read<void*>(info,0),0x58);
    void* object=create(info);
    if (!object) { api.removeRef(info); return; }
    void* frame=Read<void*>(object,8);
    constexpr float rad=3.14159265358979323846f/180.f;
    float sx=std::sin(d.rotation.x*rad),cx=std::cos(d.rotation.x*rad);
    float sy=std::sin(d.rotation.y*rad),cy=std::cos(d.rotation.y*rad);
    float sz=std::sin(d.rotation.z*rad),cz=std::cos(d.rotation.z*rad);
    Matrix matrix;
    matrix.right={cz*cy*d.scale.x,sz*cy*d.scale.x,-sy*d.scale.x};
    matrix.up={(cz*sy*sx-sz*cx)*d.scale.y,(sz*sy*sx+cz*cx)*d.scale.y,cy*sx*d.scale.y};
    matrix.at={(cz*sy*cx+sz*sx)*d.scale.z,(sz*sy*cx-cz*sx)*d.scale.z,cy*cx*d.scale.z};
    matrix.position=d.position;
    if (frame) {
        api.frameTransform(frame,&matrix,0);
        if (*static_cast<uint8_t*>(object)==1) {
            // CEntity::Render 0x4ccfdc reads atomic render callback at +0x70.
            auto render=Read<void* (*)(void*)>(object,0x70); if(render)render(object);
        } else if (*static_cast<uint8_t*>(object)==2) api.clumpRender(object);
    }
    if (*static_cast<uint8_t*>(object)==1) { api.atomicDestroy(object); if(frame)api.frameDestroy(frame); }
    else if (*static_cast<uint8_t*>(object)==2) api.clumpDestroy(object);
    api.removeRef(info);
}
void Render(void* camera) {
    auto batch=Frame(); if (batch->empty()) return;
    SavedStates saved;
    struct Materials {
        std::map<std::string,void*> rasters;
        ~Materials(){Set(1,0);for(auto& [source,raster]:rasters)if(raster)api.rasterDestroy(raster);}
        void* Get(const std::string& source) {
            if(source.empty())return nullptr;
            auto [it,inserted]=rasters.try_emplace(source,nullptr);
            if(inserted)it->second=Raster(source);return it->second;
        }
    } materials;
    Set(1,0);Set(2,1);Set(6,1);Set(7,2);Set(8,0);Set(9,2);Set(10,5);Set(11,6);Set(12,1);Set(20,1);
    Vec3 position{};
    void* frame=Read<void*>(camera,8);
    if (frame) if (Matrix* ltm=api.frameLtm(frame)) position=ltm->position;
    for(const auto& draw:*batch) {
        if(draw.kind==Draw::Model) { Set(1,0);Set(8,1);ModelDraw(draw);Set(8,0);continue; }
        void* raster=materials.Get(draw.material);
        if(!draw.material.empty() && !raster) continue;
        Set(1,reinterpret_cast<uintptr_t>(raster));
        if(draw.kind==Draw::Line || draw.kind==Draw::MaterialLine) {
            std::vector<Vertex> triangles;triangles.reserve(draw.vertices.size()*3);
            for(size_t i=0;i+1<draw.vertices.size();i+=2) {
                Draw line;line.width=draw.width;line.face=draw.face;line.faceSet=draw.faceSet;
                line.flipUV=draw.flipUV;line.u=draw.u;line.v=draw.v;line.usize=draw.usize;line.vsize=draw.vsize;
                line.vertices={draw.vertices[i],draw.vertices[i+1]};
                auto quad=LineQuad(line,position);triangles.insert(triangles.end(),quad.begin(),quad.end());
            }
            Vertices(triangles,4);
        } else Vertices(draw.vertices,draw.primitive);
    }
}
void* EndCamera(void* camera) {
    EndCallback original=nullptr;
    {std::lock_guard lock(callbackMutex);auto found=callbacks.find(camera);if(found!=callbacks.end())original=found->second;}
    if (api.scene && Read<void*>(api.scene,8)==camera) {
        try { Render(camera); } catch(...) { ENGINE_LOGE("DIAG DX 3D: draw batch failed; camera still ends normally"); }
    }
    return original?original(camera):camera;
}
bool ReadyOnGame() {
    static bool incompatible=false;
    if(incompatible)return false;
    if (!api.scene) {
        void* lib=dlopen("libGTASA.so",RTLD_NOW|RTLD_NOLOAD|RTLD_GLOBAL);if(!lib)return false;
        auto sym=[&](const char* name){void* address=dlsym(lib,name);if(!address)ENGINE_LOGE("DIAG DX 3D: missing native %s",name);return address;};
        void* createCamera=sym("_Z14RwCameraCreatev");
        void* endCamera=sym("_Z17RwCameraEndUpdateP8RwCamera");
        void* createRaster=sym("_Z14RwRasterCreateiiii");
        void* createEntity=sym("_ZN7CEntity14CreateRwObjectEv");
        void* renderEntity=sym("_ZN7CEntity6RenderEv");
        // Callback read/write and model/atomic slots are checked against the
        // exact load/store instructions cited above. Never use PC addresses.
        const bool verified=createCamera&&endCamera&&createRaster&&createEntity&&renderEntity&&
            Read<uint32_t>(endCamera,0)==0xf9401c01 && Read<uint32_t>(createCamera,0x68)==0x3d800e60 &&
            Read<uint32_t>(createRaster,0x74)==0x29035a77 && Read<uint32_t>(createEntity,0x38)==0xf9402d08 &&
            Read<uint32_t>(renderEntity,0xb8)==0xf9403808;
        if(!verified){incompatible=true;dlclose(lib);ENGINE_LOGE("DIAG DX 3D: unverified camera/RW ABI; disabled");return false;}
#define DX_SYM(field,name) api.field=reinterpret_cast<decltype(api.field)>(sym(name))
        DX_SYM(models,"_ZN10CModelInfo16ms_modelInfoPtrsE");
        DX_SYM(transform,"_Z15RwIm3DTransformP18RxObjSpace3DVertexjP11RwMatrixTagj");
        DX_SYM(render,"_Z21RwIm3DRenderPrimitive15RwPrimitiveType");DX_SYM(end,"_Z9RwIm3DEndv");
        DX_SYM(stateSet,"_Z16RwRenderStateSet13RwRenderStatePv");DX_SYM(stateGet,"_Z16RwRenderStateGet13RwRenderStatePv");
        DX_SYM(rasterCreate,"_Z14RwRasterCreateiiii");DX_SYM(rasterLock,"_Z12RwRasterLockP8RwRasterhi");
        DX_SYM(rasterUnlock,"_Z14RwRasterUnlockP8RwRaster");DX_SYM(rasterDestroy,"_Z15RwRasterDestroyP8RwRaster");
        DX_SYM(frameLtm,"_Z13RwFrameGetLTMP7RwFrame");DX_SYM(frameTransform,"_Z16RwFrameTransformP7RwFramePK11RwMatrixTag15RwOpCombineType");
        DX_SYM(atomicDestroy,"_Z15RpAtomicDestroyP8RpAtomic");DX_SYM(clumpDestroy,"_Z14RpClumpDestroyP7RpClump");
        DX_SYM(frameDestroy,"_Z14RwFrameDestroyP7RwFrame");DX_SYM(clumpRender,"_Z13RpClumpRenderP7RpClump");
        DX_SYM(addRef,"_ZN14CBaseModelInfo6AddRefEv");DX_SYM(removeRef,"_ZN14CBaseModelInfo9RemoveRefEv");
#undef DX_SYM
        const char* good[]={"_ZN16CAtomicModelInfo12GetModelTypeEv","_ZN17CLodTimeModelInfo12GetModelTypeEv","_ZN14CTimeModelInfo12GetModelTypeEv",
            "_ZN19CLodAtomicModelInfo12GetModelTypeEv","_ZN16CWeaponModelInfo12GetModelTypeEv","_ZN15CClumpModelInfo12GetModelTypeEv"};
        for(size_t i=0;i<6;i++)api.acceptedTypes[i]=sym(good[i]);
        api.rejectedTypes={sym("_ZN13CPedModelInfo12GetModelTypeEv"),sym("_ZN17CVehicleModelInfo12GetModelTypeEv")};
        void* scene=sym("Scene");dlclose(lib);
        if(!scene||!api.models||!api.transform||!api.render||!api.end||!api.stateGet||!api.stateSet||!api.rasterCreate||!api.rasterLock||
            !api.rasterUnlock||!api.rasterDestroy||!api.frameLtm||!api.frameTransform||!api.atomicDestroy||!api.clumpDestroy||
            !api.frameDestroy||!api.clumpRender||!api.addRef||!api.removeRef){incompatible=true;return false;}
        api.scene=scene;
    }
    void* camera=Read<void*>(api.scene,8);if(!camera||*static_cast<uint8_t*>(camera)!=4)return false;
    auto current=Read<EndCallback>(camera,0x38);
    if(current!=&EndCamera) {
        if(!current)return false;
        {std::lock_guard lock(callbackMutex);callbacks[camera]=current;}
        // RwCameraCreate at 0x26d4bc stores begin/end at +0x30/+0x38;
        // RwCameraEndUpdate 0x26cf48 loads and calls the +0x38 entry.
        EndCallback replacement=&EndCamera;
        std::memcpy(static_cast<uint8_t*>(camera)+0x38,&replacement,sizeof replacement);
    }
    return true;
}
}
bool NativeReady() {
    static std::atomic<bool> available{false};
    static std::once_flag subscribed;
    std::call_once(subscribed,[]{
        // Refresh/reinstall the camera callback once per game pulse. A Lua
        // frame can contain thousands of lines; it must not wait a game pulse
        // for every individual Append. False also covers a replaced camera.
        GameTasks::Defer([]{available.store(ReadyOnGame(),std::memory_order_release);return false;});
    });
    return available.load(std::memory_order_acquire);
}
bool NativeModelReady(uint32_t id) {bool ready=false;if(!GameTasks::Run([&]{ready=ReadyOnGame()&&ModelUsable(id);}))return false;return ready;}
#else
bool NativeReady() {return false;}
bool NativeModelReady(uint32_t) {return false;}
#endif
} // namespace WorldRPG::Dx3D
