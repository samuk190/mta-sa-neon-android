#include "shared/DxShader.h"
#include "shared/ModelEffects.h"
#include "shared/LuaDx3DApi.h"
#include "shared/LuaVMStub.h"
#include "shared/LuaDxApi.h"
#include "gtasa/GameTasks.h"
#include "stubs/test_bitmaps.h"
#include "lua.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
namespace FX=WorldRPG::ModelEffects;
namespace DX=WorldRPG::DxShader;
namespace D3=WorldRPG::Dx3D;
unsigned checks=0;
uint32_t finalizerShaderId=0;
int NoteFinalizerShader(lua_State* L) {
    lua_getfield(L,1,"id");finalizerShaderId=uint32_t(lua_tointeger(L,-1));return 0;
}
bool failNextSurface=false,blockSurfaceless=false;
extern "C" EGLSurface __real_eglCreatePbufferSurface(EGLDisplay,EGLConfig,const EGLint*);
extern "C" EGLBoolean __real_eglMakeCurrent(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
extern "C" EGLSurface __wrap_eglCreatePbufferSurface(EGLDisplay display,EGLConfig config,const EGLint* attributes) {
    if(failNextSurface){failNextSurface=false;return EGL_NO_SURFACE;}
    return __real_eglCreatePbufferSurface(display,config,attributes);
}
extern "C" EGLBoolean __wrap_eglMakeCurrent(EGLDisplay display,EGLSurface draw,EGLSurface read,EGLContext context) {
    if(blockSurfaceless&&context!=EGL_NO_CONTEXT&&(draw==EGL_NO_SURFACE||read==EGL_NO_SURFACE))return EGL_FALSE;
    return __real_eglMakeCurrent(display,draw,read,context);
}
void Check(bool value,const char* what) {
    if(!value){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}++checks;
}
struct Backend:FX::Backend {
    std::vector<FX::Effect> baseline,effective;
    bool fail=false,loaded=true;
    bool Capture(uint32_t id,bool& isLoaded,std::vector<FX::Effect>& out) override {if(id!=100)return false;isLoaded=loaded;out=baseline;return true;}
    bool Commit(uint32_t id,const std::vector<FX::Effect>& out) override {if(fail||id!=100)return false;effective=out;return true;}
    bool Supports(FX::e2dEffectType type) const override {return type==FX::e2dEffectType::LIGHT||type==FX::e2dEffectType::SUN_GLARE;}
};
void Effects() {
    Backend backend;FX::S2DFXData data;data.position={1,2,3};data.lightRange=20;
    backend.baseline.push_back({FX::e2dEffectType::LIGHT,data});backend.effective=backend.baseline;
    FX::SetTestBackend(&backend);auto& manager=FX::CClient2DFXManager::GetSingleton();
    FX::CResource a{"a",1,1},b{"b",2,1};
    Check(manager.IsValidModel(100)&&!manager.IsValidModel(101),"real backend model validation");
    auto first=data;first.lightRange=30;auto second=data;second.lightRange=40;
    Check(manager.SetProperty(&a,100,0,FX::e2dEffectProperty::LIGHT_RANGE,first),"first resource property");
    Check(manager.SetProperty(&b,100,0,FX::e2dEffectProperty::LIGHT_RANGE,second),"second resource property");
    FX::S2DFXData out;Check(manager.GetData(100,0,out)&&out.lightRange==40,"last writer wins");
    Check(manager.ResetProperty(&b,100,0,FX::e2dEffectProperty::LIGHT_RANGE),"reset own property");
    Check(manager.GetData(100,0,out)&&out.lightRange==30,"previous writer restored");
    backend.fail=true;Check(!manager.SetProperty(&a,100,0,FX::e2dEffectProperty::LIGHT_RANGE,second),"failed native commit rejected");
    Check(manager.GetData(100,0,out)&&out.lightRange==30,"failed commit keeps state");backend.fail=false;
    Check(manager.Remove(&a,100,0)&&manager.Remove(&b,100,0),"two resource removals");
    Check(manager.Restore(&a,100,0)&&manager.GetType(100,0)==FX::e2dEffectType::UNKNOWN,"one restore keeps other removal");
    Check(manager.Restore(&b,100,0)&&manager.GetType(100,0)==FX::e2dEffectType::LIGHT,"last restore revives effect");
    Check(manager.Add(&a,100,FX::e2dEffectType::SUN_GLARE,data),"custom native effect");
    Check(manager.GetCount(100,false)==1&&manager.GetCount(100,true)==2,"original and custom count");
    Check(!manager.Remove(&b,100,1),"custom ownership enforced");
    Check(!manager.Add(&a,100,FX::e2dEffectType::PARTICLE,data),"unsupported native type rejected");
    manager.ReleaseResource(&a);Check(manager.GetCount(100,true)==1,"resource cleanup removes custom");
    Check(manager.GetData(100,0,out)&&out.lightRange==20,"resource cleanup restores original");
    Check(backend.effective==backend.baseline,"native baseline restored");
    backend.loaded=false;Check(!manager.Add(&a,100,FX::e2dEffectType::LIGHT,data),"unloaded add rejected");
    lua_State* L=luaL_newstate();luaL_openlibs(L);FX::InstallModelEffectsLua(L);
    Check(luaL_dostring(L,"assert(getModel2DFXCount(100)==1); assert(getModel2DFXType(100,0)=='light')")==LUA_OK,"2DFX Lua query bindings");
    lua_close(L);FX::SetTestBackend(nullptr);
}
void Geometry() {
    for(unsigned i=1;i<=4;++i){auto edges=D3::SphereLines(i);Check(edges.size()==12u*(1u<<(2*i)),"sphere edge count");for(auto [a,b]:edges){Check(std::abs(D3::Dot(a,a)-1)<0.0001,"sphere radius");Check(D3::Dot(a-b,a-b)>0,"no zero length edges");}}
    D3::Draw line;line.vertices={{{0,0,0},0xffffffff,0,0},{{0,0,2},0xffffffff,0,0}};line.width=4;
    auto quad=D3::LineQuad(line,{0,-10,0});Check(quad.size()==6,"billboard line triangles");
    Check(std::abs(D3::Dot(quad[0].position-quad[1].position,quad[0].position-quad[1].position)-16)<0.0001,"line world width");
    line.flipUV=true;quad=D3::LineQuad(line,{0,-10,0});Check(quad[0].u==0&&quad[0].v==1&&quad[2].u==1&&quad[2].v==1,"flipped material UVs");
    Check(D3::LineQuad(line,{0,0,5}).empty(),"degenerate camera alignment");
    lua_State* L=luaL_newstate();luaL_openlibs(L);D3::InstallDx3DLua(L);D3::SetTestAvailability(true);D3::BeginFrame();
    Check(luaL_dostring(L,"assert(dxDrawLine3D(0,0,0,1,2,3)); assert(not dxDrawLine3D(0,0,0,1,2,3,nil,1,'postgui')); assert(not pcall(dxDrawWiredSphere,0,0,0,1,nil,1,5))")==LUA_OK,"3D Lua validation and stage rejection");
    Check(luaL_dostring(L,"assert(not dxDrawWiredSphere(3e38,0,0,3e38))")==LUA_OK,"derived sphere overflow rejected before native queue");
    D3::EndFrame();Check(D3::Frame()->size()==1,"3D immutable frame publication");
    auto prior=D3::Frame();D3::BeginFrame();D3::EndFrame();Check(D3::Frame()->empty()&&prior->size()==1,"previous renderer snapshot remains valid");
    D3::SetTestAvailability(false);Check(luaL_dostring(L,"assert(not dxDrawLine3D(0,0,0,1,2,3))")==LUA_OK,"unavailable renderer returns false");lua_close(L);
}
void Shaders() {
    const std::string source=R"(
float4 tint;
float4 PS(float2 uv:TEXCOORD0):COLOR0 { return tint; }
technique Painted { pass P { PixelShader=compile ps_2_0 PS(); } }
)";
    std::string technique,error;auto shader=DX::Create(source,{},technique,error);
    if(!shader)std::fprintf(stderr,"Shader diagnostic: %s\n",error.c_str());
    Check(bool(shader),"real HLSL translation and GLES link");Check(technique=="Painted","selected technique name");
    Check(DX::SetNumbers(*shader,"TINT",{1,0,0,1}),"case insensitive float4 uniform");
    Check(!DX::SetNumbers(*shader,"missing",{1}),"unknown uniform rejected");
    Check(!DX::SetNumbers(*shader,"tint",{1,2}),"incorrect uniform shape rejected");
    DX::Tile tile;tile.x=8;tile.y=8;tile.width=16;tile.height=8;DX::Image first,second;
    Check(DX::Render(*shader,tile,32,32,first),"shader image render");
    Check(first.pixels[12*32+12]==0xffff0000,"shader produces red pixels");Check(first.pixels[0]==0,"shader outside tile transparent");
    Check(DX::SetNumbers(*shader,"tint",{0,1,0,1})&&DX::Render(*shader,tile,32,32,second),"update uniform and render");
    Check(second.pixels[12*32+12]==0xff00ff00&&first.pixels[12*32+12]==0xffff0000,"draw snapshot keeps prior values");
    Check(DX::SetTessellation(*shader,8,8)&&!DX::SetTessellation(*shader,65,1),"real tessellation and limit");
    DX::Transform transform;transform.rotation[2]=90;Check(DX::SetTransform(*shader,transform)&&DX::Render(*shader,tile,32,32,second),"transform applied to tessellated shader");
    Check(second.pixels[5*32+16]==0xff00ff00&&second.pixels[12*32+9]==0,"rotation changes coverage");
    std::string textured=R"(texture image; sampler imageSampler=sampler_state { Texture=<image>; MinFilter=Linear; AddressU=Clamp; AddressV=Clamp; };
float4 PS(float2 uv:TEXCOORD0):COLOR0 {return tex2D(imageSampler,uv);}
technique Textured {pass P {PixelShader=compile ps_2_0 PS();}})";
    auto sampler=DX::Create(textured,{},technique,error);if(!sampler)std::fprintf(stderr,"Sampler diagnostic: %s\n",error.c_str());
    Check(bool(sampler),"legacy sampler translated");
    Check(DX::SetTexture(*sampler,"image",{1,1,{0xff123456}})&&DX::Render(*sampler,tile,32,32,second),"texture alias binds actual pixels");
    Check(second.pixels[12*32+12]==0xff123456,"texture sampled by GPU");
    unsigned materialWidth=0,materialHeight=0;
    Check(DX::SetTexture(*sampler,"image",{2,1,{0xff123456,0xff123456}}),"texture dimensions updated");
    DX::MaterialSize(*sampler,materialWidth,materialHeight);Check(materialWidth==2&&materialHeight==1,"size mirrors first declared texture");
    Check(!DX::Create("float4 PS():COLOR0{return broken;} technique T{pass P{PixelShader=compile ps_2_0 PS();}}",{},technique,error),"invalid HLSL rejected");
    Check(!DX::Create("technique T{pass P{PixelShader=compile ps_2_0 PS()",{},technique,error),"truncated FX terminates");
    Check(!DX::Create("#include <unknown.fx>\n"+source,{},technique,error),"unsupported preprocessor rejected");
    auto fallback=DX::Create("float4 PS():COLOR0{return float4(CHANNEL,0,0,1);}"
        "technique Unsupported{pass P{AlphaBlendEnable=true;PixelShader=compile ps_2_0 PS();}}"
        "technique Supported{pass P{PixelShader=compile ps_2_0 PS();}}",{{"CHANNEL","1"}},technique,error);
    Check(bool(fallback)&&technique=="Supported","macro and technique fallback");
    std::string curved=R"(
float4x4 gWorldViewProjection;
struct Input {float3 Position:POSITION;float2 UV:TEXCOORD0;};
struct Output {float4 Position:POSITION;float2 UV:TEXCOORD0;};
Output VS(Input input){Output o;input.Position.y-=sin(input.UV.x*3.14159265)*8; o.Position=mul(float4(input.Position,1),gWorldViewProjection);o.UV=input.UV;return o;}
float4 PS(float2 uv:TEXCOORD0):COLOR0{return float4(1,1,0,1);}
technique Curved{pass P{VertexShader=compile vs_2_0 VS();PixelShader=compile ps_2_0 PS();}}
)";
    auto custom=DX::Create(curved,{},technique,error);if(!custom)std::fprintf(stderr,"Custom VS diagnostic: %s\n",error.c_str());
    Check(bool(custom),"custom HLSL vertex shader");
    Check(DX::Render(*custom,tile,32,32,first)&&DX::SetTessellation(*custom,8,1)&&DX::Render(*custom,tile,32,32,second),"custom VS with tessellation");
    Check(first.pixels[3*32+16]==0&&second.pixels[3*32+16]==0xffffff00,"tessellation changes curved coverage");
    auto initialized=DX::Create("const float amount=0.5;float4 tint=float4(amount*2,0,0,1);"
        "float4 PS():COLOR0{return tint;}technique T{pass P{PixelShader=compile ps_2_0 PS();}}",{},technique,error);
    if(!initialized)std::fprintf(stderr,"Initializer diagnostic: %s\n",error.c_str());
    Check(bool(initialized)&&DX::Render(*initialized,tile,32,32,first),"uniform initializer and const expression");
    Check(first.pixels[12*32+12]==0xffff0000,"default uniform values preserved");
    WorldRPG::Script::LuaVMStub vm;Check(vm.initialize(),"actual VM initialized");
    const char* script=R"LUA(
        local shader,technique=dxCreateShader([[float4 color;float4 PS():COLOR0{return color;}
            technique LuaTest{pass P{PixelShader=compile ps_2_0 PS();}}]])
        assert(isElement(shader) and getElementType(shader)=='shader' and technique=='LuaTest')
        assert(dxSetShaderValue(shader,'color',1,0.5,0,1))
        assert(dxSetShaderValue(shader,'COLOR',{0,1,0,1}))
        assert(not dxSetShaderValue(shader,'invalid',1))
        assert(not dxSetShaderValue(shader,'color',0/0,0,0,1))
        assert(dxSetShaderTessellation(shader,4,4) and not dxSetShaderTessellation(shader,0,1))
        assert(dxSetShaderTransform(shader,0,0,45,0,0,0,false,0,0,false))
        assert(not dxDrawImage(0,0,16,16,shader)) -- No Android bitmap bridge on host.
        assert(destroyElement(shader) and not isElement(shader))
        assert(not dxSetShaderValue(shader,'color',1,0,0,1))
    )LUA";
    auto result=vm.executeBuffer(script,std::strlen(script),"dx-shader-integration");
    if(!result.success)std::fprintf(stderr,"Lua diagnostic: %s\n",result.errorMessage.c_str());
    Check(result.success,"shader Lua API, type, image failure and destruction");vm.shutdown();
}
void ContextPreservation() {
    auto platform=reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display=platform(0x31DD,nullptr,nullptr);EGLint major,minor;Check(eglInitialize(display,&major,&minor),"external display");
    EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLConfig config;EGLint count;Check(eglChooseConfig(display,attributes,&config,1,&count)&&count==1,"external config");
    EGLint surfaceAttributes[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE},contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttributes);EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttributes);
    Check(eglMakeCurrent(display,surface,surface,context),"external context current");glViewport(1,2,3,4);
    std::printf("GLES pixel-test renderer: %s\n",glGetString(GL_RENDERER));
    std::string name,error;auto shader=DX::Create("float4 PS():COLOR0{return float4(1,0,0,1);}technique T{pass P{PixelShader=compile ps_2_0 PS();}}",{},name,error);
    Check(shader&&eglGetCurrentContext()==context,"creation restores caller EGL context");
    DX::Tile tile;tile.width=tile.height=4;DX::Image image;Check(DX::Render(*shader,tile,4,4,image),"isolated shader rendering");
    GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
    Check(eglGetCurrentContext()==context&&eglGetCurrentSurface(EGL_DRAW)==surface&&viewport[0]==1&&viewport[1]==2&&viewport[2]==3&&viewport[3]==4,"render preserves caller surfaces and GL state");
    shader.reset();Check(eglGetCurrentContext()==context,"destruction restores caller context");
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);
}
void DeferredCleanup() {
    int calls=0;WorldRPG::GameTasks::Defer([&]{return ++calls==2;});
    WorldRPG::GameTasks::Service();Check(calls==1,"cleanup retries after failed commit");
    WorldRPG::GameTasks::Service();Check(calls==2,"cleanup eventually finishes");
    WorldRPG::GameTasks::Service();Check(calls==2,"finished cleanup runs once");
}
void ReviewCase(const std::string& which) {
    std::string name,error;
    DX::Tile tile;tile.x=tile.y=8;tile.width=tile.height=16;DX::Image image;
    if(which=="egl") {
        failNextSurface=true;blockSurfaceless=true;
        const std::string source="float4 PS():COLOR0{return float4(1,0,0,1);}technique T{pass P{PixelShader=compile ps_2_0 PS();}}";
        Check(!DX::Create(source,{},name,error),"initial EGL surface failure returns false");
        Check(bool(DX::Create(source,{},name,error)),"EGL retries after partial initialization failure");
        blockSurfaceless=false;
    } else if(which=="long-source") {
        auto shader=DX::Create(std::string(200000,' ')+"float4 PS():COLOR0{return float4(1,0,0,1);}technique T{pass P{PixelShader=compile ps_2_0 PS();}}",{},name,error);
        Check(bool(shader),"large leading whitespace does not overflow native parser stack");
    } else if(which=="shutdown") {
        WorldRPG::Script::LuaVMStub vm;Check(vm.initialize(),"shutdown VM");
        WorldRPG::JniBridge::EnableTestBitmaps(true);D3::SetTestAvailability(true);
        finalizerShaderId=0;auto* state=WorldRPG::Script::ActiveVmState();
        lua_pushcfunction(state,NoteFinalizerShader);lua_setglobal(state,"noteFinalizerShader");
        const char* setup=R"LUA(
            shutdownShader=dxCreateShader([[float4 PS():COLOR0{return float4(1,0,0,1);} technique T{pass P{PixelShader=compile ps_2_0 PS();}}]])
            shutdownTexture=dxCreateTexture(4,4)
            shutdownSvg=svgCreate(4,4)
            assert(shutdownShader and shutdownTexture and shutdownSvg)
            closingObject=setmetatable({}, {__gc=function()
                local shader=dxCreateShader([[float4 PS():COLOR0{return float4(1,0,0,1);} technique T{pass P{PixelShader=compile ps_2_0 PS();}}]])
                assert(shader);noteFinalizerShader(shader)
            end})
            addEventHandler('onClientRender',root,function() assert(dxDrawLine3D(0,0,0,0,0,1)) end)
        )LUA";
        Check(vm.executeBuffer(setup,std::strlen(setup),"shutdown-setup").success,"shutdown resources created");
        auto* L=WorldRPG::Script::ActiveVmState();
        auto id=[&](const char* global){lua_getglobal(L,global);lua_getfield(L,-1,"id");auto value=uint32_t(lua_tointeger(L,-1));lua_pop(L,2);return value;};
        auto shaderId=id("shutdownShader"),textureId=id("shutdownTexture"),svgId=id("shutdownSvg");vm.pumpDx();
        Check(DX::Exists(shaderId)&&D3::Frame()->size()==1,"native state exists before shutdown");
        vm.shutdown();
        Check(!DX::Exists(shaderId),"shutdown releases shader programs without a replacement VM");
        Check(D3::Frame()->empty(),"shutdown clears published 3D batches");
        Check(!WorldRPG::Script::DxElementType(textureId),"shutdown invalidates DX texture handles");
        Check(!WorldRPG::Script::DxElementType(svgId),"shutdown invalidates SVG handles");
        Check(finalizerShaderId!=0,"Lua close finalizer exercised");
        Check(!DX::Exists(finalizerShaderId),"finalizers cannot leave late DX resources alive");
        D3::SetTestAvailability(false);WorldRPG::JniBridge::EnableTestBitmaps(false);
    } else if(which=="material-facing") {
        WorldRPG::Script::LuaVMStub vm;Check(vm.initialize(),"material facing VM");
        WorldRPG::JniBridge::EnableTestBitmaps(true);D3::SetTestAvailability(true);
        const char* setup=R"LUA(
            local texture=dxCreateTexture(4,4);assert(texture)
            assert(dxDrawMaterialLine3D(0,0,0,0,0,2,texture,1,4294967295,2,3,4))
            assert(dxDrawMaterialSectionLine3D(0,0,0,0,0,2,0,0,4,4,texture,1,4294967295,5,6,7))
            assert(dxDrawMaterialLine3D(0,0,0,0,0,2,texture,1,4294967295,'postfx',8,9,10))
        )LUA";
        Check(vm.executeBuffer(setup,std::strlen(setup),"material-facing").success,"face-toward vector works without a stage placeholder");
        D3::EndFrame();auto frame=D3::Frame();Check(frame->size()==3,"all material line calls queued");
        Check((*frame)[0].faceSet&&(*frame)[0].face.x==2&&(*frame)[0].face.z==4,"plain line keeps all facing coordinates");
        Check((*frame)[1].faceSet&&(*frame)[1].face.x==5&&(*frame)[1].usize==1,"section line keeps facing coordinates and UVs");
        Check((*frame)[2].faceSet&&(*frame)[2].face.z==10,"explicit postfx still works");
        vm.shutdown();D3::SetTestAvailability(false);WorldRPG::JniBridge::EnableTestBitmaps(false);
    } else if(which=="macro-budget") {
        std::map<std::string,std::string> macros;
        for(char c='A';c<'G';++c){std::string next(1,c+1);macros[std::string(1,c)]=next+"+"+next+"+"+next+"+"+next+"+"+next+"+"+next+"+"+next+"+"+next;}
        macros["G"]="0.01";
        auto shader=DX::Create("A",macros,name,error);
        Check(!shader&&error.find("limit")!=std::string::npos,"macro expansion respects the shader source budget before parsing");
    } else if(which=="projection") {
        auto shader=DX::Create("float4x4 gWorldViewProjection=float4x4(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1);"
            "float4 PS():COLOR0{return float4(1,0,0,1);}technique T{pass P{PixelShader=compile ps_2_0 PS();}}",{},name,error);
        Check(shader&&DX::Render(*shader,tile,32,32,image),"initialized standard matrix renders");
        Check(image.pixels[12*32+12]==0xffff0000,"common projection overrides effect default");
    } else if(which=="integer") {
        auto small=DX::Create("int count=1;float4 PS():COLOR0{return float4(float(count),0,0,1);}"
            "technique T{pass P{PixelShader=compile ps_2_0 PS();}}",{},name,error);
        Check(small&&DX::Render(*small,tile,32,32,image),"integer uniform links with matching VS/PS precision");
        Check(image.pixels[12*32+12]==0xffff0000,"integer default reaches fragment shader");
        auto shader=DX::Create("int count=2147483647;float4 PS():COLOR0{return float4(float(count),0,0,1);}"
            "technique T{pass P{PixelShader=compile ps_2_0 PS();}}",{},name,error);
        if(shader)DX::Render(*shader,tile,32,32,image); // Exercise conversion if a regression accepts an unsafe default.
        Check(!shader&&!error.empty(),"unrepresentable integer default rejected before cast");
    } else if(which=="overflow") {
        auto shader=DX::Create("float4 PS():COLOR0{return float4(1,0,0,1);}technique T{pass P{PixelShader=compile ps_2_0 PS();}}",{},name,error);
        DX::Transform transform;transform.rotationCenter[0]=3e38f;
        Check(shader&&DX::SetTransform(*shader,transform),"finite transform input");
        Check(!DX::Render(*shader,tile,32,32,image),"derived shader matrix overflow rejected");
    } else if(which=="overwrite") {
        WorldRPG::Script::LuaVMStub vm;Check(vm.initialize(),"overwrite VM");vm.setScreenSize(32,32);
        WorldRPG::JniBridge::EnableTestBitmaps(true);
        const char* setup=R"LUA(
            shader=dxCreateShader([[float4 PS():COLOR0{return float4(1,0,0,1);} technique T{pass P{PixelShader=compile ps_2_0 PS();}}]])
            assert(shader)
            overwrite=true
            addEventHandler('onClientRender',root,function()
                dxSetBlendMode(overwrite and 'overwrite' or 'blend')
                accepted=dxDrawImage(8,8,16,16,shader)
                dxSetBlendMode('blend')
            end)
        )LUA";
        Check(vm.executeBuffer(setup,std::strlen(setup),"overwrite-setup").success,"overwrite shader setup");
        const std::string rejected=vm.pumpDx();
        const char* assertRejected="assert(accepted==false);overwrite=false";
        Check(vm.executeBuffer(assertRejected,std::strlen(assertRejected),"overwrite-rejected").success,"unsafe full-viewport overwrite rejected");
        Check(rejected.find("I ")==std::string::npos&&WorldRPG::JniBridge::TestBitmapCount()==0,"rejected overwrite allocates no bitmap or command");
        const std::string accepted=vm.pumpDx();
        const char* assertAccepted="assert(accepted==true)";
        Check(vm.executeBuffer(assertAccepted,std::strlen(assertAccepted),"blend-accepted").success,"normal shader image still accepted");
        Check(accepted.find("I 0 0 32 32 ")!=std::string::npos&&WorldRPG::JniBridge::TestBitmapCount()==1,"normal shader reaches bitmap bridge and draw queue");
        vm.shutdown();WorldRPG::JniBridge::EnableTestBitmaps(false);
    }
}
int main(int argc,char** argv){
    if(argc==2){ReviewCase(argv[1]);return 0;}
    ReviewCase("egl");Effects();Geometry();Shaders();ContextPreservation();DeferredCleanup();
    ReviewCase("projection");ReviewCase("integer");ReviewCase("overflow");ReviewCase("overwrite");ReviewCase("long-source");
    ReviewCase("shutdown");ReviewCase("material-facing");ReviewCase("macro-budget");
    std::printf("DX conversion checks: %u passed\n",checks);
}
