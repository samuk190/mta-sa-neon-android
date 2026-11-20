#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
struct lua_State;
namespace WorldRPG::DxShader {
struct Program;
struct Transform {
    std::array<float,3> rotation{}, rotationCenter{};
    bool rotationScreen=false;
    std::array<float,2> perspectiveCenter{};
    bool perspectiveScreen=false;
};
struct Image {
    unsigned width=0,height=0;
    std::vector<uint32_t> pixels;
};
struct Tile {
    float x=0,y=0,width=0,height=0,u=0,v=0,usize=1,vsize=1;
    float rotation=0,offsetX=0,offsetY=0;
    uint32_t color=0xffffffff;
};
// A real ES2 program compiled in an isolated EGL context. Unsupported FX
// techniques fail creation, rather than producing an inert shader element.
std::shared_ptr<Program> Create(const std::string&,const std::map<std::string,std::string>&,
                                std::string& technique,std::string& diagnostic);
bool SetNumbers(Program&,const std::string&,const std::vector<float>&,bool boolean=false);
bool SetTexture(Program&,const std::string&,Image);
void MaterialSize(Program&,unsigned&,unsigned&);
bool SetTessellation(Program&,unsigned,unsigned);
bool SetTransform(Program&,const Transform&);
bool Render(Program&,const Tile&,unsigned width,unsigned height,Image&);
void InstallShaderLua(lua_State*);
void Reset();
bool Exists(uint32_t);
bool Size(uint32_t,unsigned&,unsigned&);
bool Release(uint32_t);
void DropResource(const std::string&,uint16_t,uint32_t);
bool DrawImage(uint32_t,const Tile&,bool section,bool postGUI);
} // namespace WorldRPG::DxShader
