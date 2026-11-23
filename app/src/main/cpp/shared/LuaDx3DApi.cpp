#include "LuaDx3DApi.h"
#include "LuaDxApi.h"
#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "lua.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <strings.h>
#include <limits>
#include <mutex>
#include <set>
#include <tuple>
namespace WorldRPG::Dx3D {
float Dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 Cross(Vec3 a,Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
Vec3 Normalized(Vec3 v) { float n=std::sqrt(Dot(v,v)); return n>0?v*(1/n):Vec3{}; }
std::vector<Vertex> LineQuad(const Draw& draw,Vec3 camera) {
    if (draw.vertices.size()!=2) return {};
    const Vec3 a=draw.vertices[0].position,b=draw.vertices[1].position,delta=b-a;
    if (!(Dot(delta,delta)>0)) return {};
    Vec3 facing=draw.faceSet?draw.face:camera;
    Vec3 closest=a+delta*(Dot(delta,facing-a)/Dot(delta,delta));
    const Vec3 shift=Normalized(Cross(delta,closest-facing))*(draw.width*0.5f);
    if (!(Dot(shift,shift)>0)) return {};
    const uint32_t color=draw.vertices[0].color;
    const float u0=draw.u,u1=draw.u+draw.usize,v0=draw.v,v1=draw.v+draw.vsize;
    Vertex a1{a-shift,color,u0,v0},a2{a+shift,color,u1,v0};
    Vertex b1{b-shift,color,u0,v1},b2{b+shift,color,u1,v1};
    if (draw.flipUV) { a1.u=a2.u=b1.u=b2.u=u0; a1.v=v1; a2.v=v0; b1.u=b2.u=u1; b1.v=v1; b2.v=v0; }
    return {a1,a2,b1,a2,b2,b1};
}
std::vector<std::pair<Vec3,Vec3>> SphereLines(unsigned iterations) {
    if (iterations<1 || iterations>4) return {};
    static std::mutex cacheMutex;
    static std::array<std::vector<std::pair<Vec3,Vec3>>,4> cache;
    std::lock_guard lock(cacheMutex);
    auto& cached=cache[iterations-1];
    if(!cached.empty())return cached;
    // CGraphics.cpp:3120-3218: normalized octahedron, four-way subdivision,
    // then unique edges (the initial ring is not an icosahedron).
    std::array<Vec3,6> p={Vec3{0,0,1},{0,0,-1},{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
    for (auto& v:p) v=Normalized(v);
    constexpr unsigned indices[]={0,3,4,0,4,5,0,5,2,0,2,3,1,4,3,1,5,4,1,2,5,1,3,2};
    struct Face { Vec3 a,b,c; };
    std::vector<Face> faces;
    for (unsigned i=0;i<24;i+=3) faces.push_back({p[indices[i]],p[indices[i+1]],p[indices[i+2]]});
    while (iterations--) {
        const size_t count=faces.size();
        for (size_t i=0;i<count;i++) {
            const auto f=faces[i];
            Vec3 a=Normalized((f.a+f.b)*0.5f),b=Normalized((f.b+f.c)*0.5f),c=Normalized((f.c+f.a)*0.5f);
            faces[i]={a,b,c}; faces.push_back({f.a,a,c}); faces.push_back({f.b,a,b}); faces.push_back({f.c,b,c});
        }
    }
    std::vector<std::pair<Vec3,Vec3>> edges;
    auto add=[&](Vec3 a,Vec3 b) {
        for (const auto& [c,d]:edges)
            if ((Dot(a-c,a-c)<0.00001f && Dot(b-d,b-d)<0.00001f) ||
                (Dot(a-d,a-d)<0.00001f && Dot(b-c,b-c)<0.00001f)) return;
        edges.emplace_back(a,b);
    };
    for (const auto& f:faces) { add(f.a,f.b); add(f.b,f.c); add(f.c,f.a); }
    cached=edges;return edges;
}
namespace {
std::mutex mutex;
std::vector<Draw> pending;
size_t vertexCount=0;
std::shared_ptr<const std::vector<Draw>> published=std::make_shared<const std::vector<Draw>>();
#ifndef __ANDROID__
bool testAvailable=false;
#endif
bool Available() {
#ifdef __ANDROID__
    return NativeReady();
#else
    return testAvailable;
#endif
}
bool Number(lua_State* L,int index,float& value,float fallback=0,bool optional=false) {
    if (optional && lua_isnoneornil(L,index)) { value=fallback; return true; }
    if (!lua_isnumber(L,index)) return false;
    const double n=lua_tonumber(L,index);
    if (!std::isfinite(n) || std::abs(n)>std::numeric_limits<float>::max()) return false;
    value=static_cast<float>(n); return true;
}
bool Vector(lua_State* L,int index,Vec3& v) { return Number(L,index,v.x)&&Number(L,index+1,v.y)&&Number(L,index+2,v.z); }
bool ReadColor(lua_State* L,int index,uint32_t& color) {
    if (lua_isnoneornil(L,index)) { color=0xffffffffu; return true; }
    if (!lua_isnumber(L,index)) return false;
    const double value=lua_tonumber(L,index); if (!std::isfinite(value)) return false;
    double n=std::fmod(std::trunc(value),4294967296.0); if (n<0) n+=4294967296.0;
    color=static_cast<uint32_t>(n); return true;
}
bool Stage(lua_State* L,int index,int& value) {
    if (lua_isnoneornil(L,index)) { value=1; return true; }
    if (lua_isboolean(L,index)) { value=lua_toboolean(L,index)?2:1; return true; }
    if (lua_type(L,index)!=LUA_TSTRING) return false;
    const char* s=lua_tostring(L,index);
    if (!std::strcmp(s,"prefx")) value=0;
    else if (!std::strcmp(s,"postfx")) value=1;
    else if (!std::strcmp(s,"postgui")) value=2;
    else return false;
    return true;
}
bool Append(Draw draw) {
    // Canvas GUI is a separate Android layer; native 3D is currently pre-GUI.
    // Reject an unavailable stage instead of claiming correct queue ordering.
    if (draw.stage!=1 || !Available()) return false;
    for(const auto& v:draw.vertices) {
        if(!std::isfinite(v.position.x)||!std::isfinite(v.position.y)||!std::isfinite(v.position.z)||
           !std::isfinite(v.u)||!std::isfinite(v.v))return false;
    }
    draw.owner=Script::ActiveVmOwner(); draw.netId=Script::ActiveVmOwnerNet(); draw.generation=Script::ActiveVmOwnerGeneration();
    std::lock_guard lock(mutex);
    if (pending.size()>=4096 || vertexCount+draw.vertices.size()>200000) return false;
    vertexCount+=draw.vertices.size(); pending.push_back(std::move(draw)); return true;
}
int Result(lua_State* L,bool value) { lua_pushboolean(L,value); return 1; }
int DxDrawLine3D(lua_State* L) {
    Draw draw; draw.kind=Draw::Line; draw.vertices.resize(2);
    bool valid=Vector(L,1,draw.vertices[0].position)&&Vector(L,4,draw.vertices[1].position)&&
        ReadColor(L,7,draw.vertices[0].color)&&Number(L,8,draw.width,1,true)&&Stage(L,9,draw.stage);
    draw.width/=75.f; // CLine3DBatcher::AddLine3D, desktop line width is world-scaled.
    return Result(L,valid && Append(std::move(draw)));
}
bool Material(lua_State* L,int index,std::string& file,unsigned& width,unsigned& height) {
    return Script::DxResolveMaterial(L,index,file,width,height);
}
int MaterialLine(lua_State* L,bool section) {
    Draw draw; draw.kind=Draw::MaterialLine; draw.vertices.resize(2);
    if (!Vector(L,1,draw.vertices[0].position)||!Vector(L,4,draw.vertices[1].position)) return Result(L,false);
    int index=7;
    if (section) {
        if (!Number(L,index++,draw.u)||!Number(L,index++,draw.v)||!Number(L,index++,draw.usize)||!Number(L,index++,draw.vsize)) return Result(L,false);
    }
    if (lua_isboolean(L,index)) draw.flipUV=lua_toboolean(L,index++)!=0;
    unsigned w=0,h=0;
    if (!Material(L,index++,draw.material,w,h)||!Number(L,index++,draw.width)||
        !ReadColor(L,index++,draw.vertices[0].color)) return Result(L,false);
    // ReadIfNextIsEnumString leaves numeric facing coordinates in place when
    // the optional stage is omitted (CLuaDrawingDefs.cpp:246-258,296-308).
    int stage=1;
    if (lua_isboolean(L,index) || (lua_type(L,index)==LUA_TSTRING && Stage(L,index,stage))) {
        if(!Stage(L,index++,draw.stage))return Result(L,false);
    }
    if (lua_isnumber(L,index)&&lua_isnumber(L,index+1)&&lua_isnumber(L,index+2)) {
        if (!Vector(L,index,draw.face)) return Result(L,false);draw.faceSet=true;
    }
    if (section) { if (!w||!h) return Result(L,false); draw.u/=w; draw.usize/=w; draw.v/=h; draw.vsize/=h; }
    return Result(L,Append(std::move(draw)));
}
int DxDrawMaterialLine3D(lua_State* L) { return MaterialLine(L,false); }
int DxDrawMaterialSectionLine3D(lua_State* L) { return MaterialLine(L,true); }
bool PrimitiveSize(size_t n,int type) {
    if (!n) return false;
    switch(type) { case 2:return n%2==0; case 3:return n>=2; case 4:return n>=3&&n%3==0;case 5:case 6:return n>=3;default:return true; }
}
// Return an error object to the outer handler so lua_error does not skip C++ destructors.
int Primitive(lua_State* L,bool material) {
    constexpr const char* names[]={"pointlist","linelist","linestrip","trianglelist","trianglestrip","trianglefan"};
    Draw draw;
    const char* name=lua_type(L,1)==LUA_TSTRING?lua_tostring(L,1):"";
    for (int i=0;i<6;i++) if (!strcasecmp(name,names[i])) draw.primitive=i+1;
    bool known=false; for (const char* n:names) known=known||!strcasecmp(name,n);
    int index=2; unsigned w=0,h=0;
    if (!known || (material && !Material(L,index++,draw.material,w,h)) || !Stage(L,index++,draw.stage)) {
        lua_pushliteral(L,"Bad argument @ 3D primitive [primitive, material or stage]"); return -1;
    }
    for (;lua_istable(L,index);index++) {
        std::vector<double> values;
        lua_pushnil(L);
        while (lua_next(L,index)) {
            if (lua_type(L,-1)==LUA_TNUMBER) values.push_back(lua_tonumber(L,-1));
            lua_pop(L,1);
        }
        const size_t minimum=material?5:3;
        bool valid=values.size()==minimum || values.size()==minimum+1;
        for (double n:values) valid=valid&&std::isfinite(n)&&std::abs(n)<=std::numeric_limits<float>::max();
        if (!valid || draw.vertices.size()>=65536) { lua_pushliteral(L,"Bad argument @ 3D primitive [invalid vertex table]"); return -1; }
        Vertex v{{float(values[0]),float(values[1]),float(values[2])}};
        const bool color=values.size()==minimum+1;
        if (color) { double n=std::fmod(std::trunc(values[3]),4294967296.0); if(n<0)n+=4294967296.0; v.color=uint32_t(n); }
        if (material) { v.u=values[color?4:3]; v.v=values[color?5:4]; }
        draw.vertices.push_back(v);
    }
    return Result(L,PrimitiveSize(draw.vertices.size(),draw.primitive) && Append(std::move(draw)));
}
int DxDrawPrimitive3D(lua_State* L) { int result=Primitive(L,false); return result<0?lua_error(L):result; }
int DxDrawMaterialPrimitive3D(lua_State* L) { int result=Primitive(L,true); return result<0?lua_error(L):result; }
int DxDrawWiredSphere(lua_State* L) {
    Vec3 position; float radius=0,width=1,iterationValue=1; uint32_t color=0x40ff0000u;
    if (!Vector(L,1,position)||!Number(L,4,radius)||(!lua_isnoneornil(L,5)&&!ReadColor(L,5,color))||
        !Number(L,6,width,1,true)||!Number(L,7,iterationValue,1,true)||iterationValue<1||iterationValue>4||std::floor(iterationValue)!=iterationValue) {
        return luaL_error(L,"Bad argument @ dxDrawWiredSphere [finite geometry, iterations 1-4 required]");
    }
    auto lines=SphereLines(static_cast<unsigned>(iterationValue));
    if (!Available()) return Result(L,false);
    // One primitive batch keeps a sphere atomic with respect to the frame budget.
    Draw draw; draw.kind=Draw::Line; draw.width=width/75.f;
    for (const auto& [a,b]:lines) { draw.vertices.push_back({a*radius+position,color}); draw.vertices.push_back({b*radius+position,color}); }
    return Result(L,Append(std::move(draw)));
}
int BuildModel(lua_State* L) {
    Draw draw; draw.kind=Draw::Model; float model=0;
    if (!Number(L,1,model)||model<0||model>65535||std::floor(model)!=model || !Vector(L,2,draw.position)||!Vector(L,5,draw.rotation)) {
        lua_pushliteral(L,"Bad argument @ dxDrawModel3D [invalid model or transform]"); return -1;
    }
    draw.model=static_cast<uint32_t>(model);
    int index=8;
    if (lua_istable(L,index)) {
        for (unsigned i=0;i<3;i++) {
            lua_rawgeti(L,index,i+1); float value=1;
            bool ok=Number(L,-1,value); lua_pop(L,1);
            if (!ok) return Result(L,false);
            if(i==0)draw.scale.x=value; else if(i==1)draw.scale.y=value;else draw.scale.z=value;
        }
        ++index;
    } else if (lua_isnil(L,index)) ++index;
    if (!Number(L,index,draw.lighting,0,true) || draw.lighting!=0) return Result(L,false);
    if (!Available() || !NativeModelReady(draw.model)) return Result(L,false);
    return Result(L,Append(std::move(draw)));
}
int DxDrawModel3D(lua_State* L) {int result=BuildModel(L);return result<0?lua_error(L):result;}
}
// Draws queued by timers before render also belong to the next publication.
void BeginFrame() {}
void EndFrame() { std::lock_guard lock(mutex); published=std::make_shared<const std::vector<Draw>>(std::move(pending)); pending.clear(); vertexCount=0; }
std::shared_ptr<const std::vector<Draw>> Frame() { std::lock_guard lock(mutex); return published; }
void DropResource(const std::string& owner,uint16_t net,uint32_t generation) {
    auto mine=[&](const Draw& d){return d.owner==owner&&(!net||!d.netId||d.netId==net)&&(!generation||!d.generation||d.generation==generation);};
    std::lock_guard lock(mutex);
    pending.erase(std::remove_if(pending.begin(),pending.end(),mine),pending.end());
    vertexCount=0; for(const auto& d:pending) vertexCount+=d.vertices.size();
    auto next=std::make_shared<std::vector<Draw>>(*published);
    next->erase(std::remove_if(next->begin(),next->end(),mine),next->end()); published=std::move(next);
}
#ifndef __ANDROID__
void SetTestAvailability(bool value) { testAvailable=value; }
#endif
void Reset() {
    {std::lock_guard lock(mutex);pending.clear();vertexCount=0;published=std::make_shared<const std::vector<Draw>>();}
}
void InstallDx3DLua(lua_State* L) {
    Reset();
    const struct {const char* name;lua_CFunction fn;} binds[] = {
        {"dxDrawLine3D",DxDrawLine3D},{"dxDrawMaterialLine3D",DxDrawMaterialLine3D},
        {"dxDrawMaterialSectionLine3D",DxDrawMaterialSectionLine3D},{"dxDrawPrimitive3D",DxDrawPrimitive3D},
        {"dxDrawMaterialPrimitive3D",DxDrawMaterialPrimitive3D},{"dxDrawWiredSphere",DxDrawWiredSphere},{"dxDrawModel3D",DxDrawModel3D}};
    for (const auto& binding:binds) { lua_pushcfunction(L,binding.fn); lua_setglobal(L,binding.name); }
}
} // namespace WorldRPG::Dx3D
