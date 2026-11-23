#include "SpatialNative.h"
#include <cmath>
#include <cstring>
#ifdef __ANDROID__
#include <dlfcn.h>
#include <android/log.h>
#endif
namespace WorldRPG::SpatialNative {
void ModelBounds(std::vector<Spatial::Element>& elements) {
#ifdef __ANDROID__
    static void** models=nullptr;
    static bool ready=[] {
        void* h=dlopen("libGTASA.so",RTLD_NOLOAD|RTLD_GLOBAL);
        auto* box=h?static_cast<const uint32_t*>(dlsym(h,"_ZN15CClumpModelInfo14GetBoundingBoxEv")):nullptr;
        auto* center=h?static_cast<const uint32_t*>(dlsym(h,"_ZNK7CEntity14GetBoundCentreER7CVector")):nullptr;
        auto* sphere=h?static_cast<const uint32_t*>(dlsym(h,"_ZN10CColSphere3SetEfRK7CVectorhhh")):nullptr;
        models=h?static_cast<void**>(dlsym(h,"_ZN10CModelInfo16ms_modelInfoPtrsE")):nullptr;
        // libGTASA 2.10: model+0x30 -> CColModel; its sphere center+0x18,
        // radius+0x24. GetBoundCentre 0x4cba98/0x4cba9c; CColSphere::Set
        // 0x3a4d0c stores radius at sphere+0xc. Read only; never mutate it.
        bool ok=box&&center&&sphere&&models&&box[0]==0xf9401800&&center[0x2c/4]==0xf9401929&&
            center[0x30/4]==0x91006122&&sphere[0]==0xbd000c00;
        if(!ok) __android_log_print(ANDROID_LOG_INFO,"WorldRPGNeon","DIAG spatial model bounds unavailable: using element points");
        return ok;
    }();
    if(!ready) return;
    for(auto& e:elements) {
        if(e.model<0||e.model>=20000||e.shape||e.type=="marker") continue;
        auto* model=static_cast<const unsigned char*>(models[e.model]);
        if(!model) continue;
        const unsigned char* col=nullptr;
        std::memcpy(&col,model+0x30,sizeof(col));
        if(!col) continue;
        float v[4]; std::memcpy(v,col+0x18,sizeof(v));
        Spatial::Sphere b{{e.position.x+v[0],e.position.y+v[1],e.position.z+v[2]},v[3]};
        if(Spatial::ValidSphere(b)) e.bounds=b;
    }
#else
    (void)elements;
#endif
}
}
