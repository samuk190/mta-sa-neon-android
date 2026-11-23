#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
struct lua_State;
namespace WorldRPG::Dx3D {
struct Vec3 {
    float x=0,y=0,z=0;
    Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec3 operator*(float s) const { return {x*s,y*s,z*s}; }
};
float Dot(Vec3,Vec3);
Vec3 Cross(Vec3,Vec3);
Vec3 Normalized(Vec3);
struct Vertex { Vec3 position; uint32_t color=0xffffffffu; float u=0,v=0; };
struct Draw {
    enum Kind { Primitive, Line, MaterialLine, Model } kind=Primitive;
    int primitive=4,stage=1;
    std::vector<Vertex> vertices;
    std::string material;
    float width=1;
    bool flipUV=false,faceSet=false;
    Vec3 face;
    float u=0,v=0,usize=1,vsize=1;
    uint32_t model=0;
    Vec3 position,rotation,scale{1,1,1};
    float lighting=0;
    std::string owner;
    uint16_t netId=0;
    uint32_t generation=0;
};
// Desktop CLine3DBatcher / CMaterialLine3DBatcher geometry. Line width is
// converted from desktop's width/75 units by the Lua line handler.
std::vector<Vertex> LineQuad(const Draw&,Vec3 camera);
std::vector<std::pair<Vec3,Vec3>> SphereLines(unsigned iterations);
void InstallDx3DLua(lua_State*);
void Reset();
void BeginFrame();
void EndFrame();
void DropResource(const std::string&,uint16_t,uint32_t);
std::shared_ptr<const std::vector<Draw>> Frame();
bool NativeReady();
bool NativeModelReady(uint32_t);
#ifndef __ANDROID__
void SetTestAvailability(bool);
#endif
} // namespace WorldRPG::Dx3D
