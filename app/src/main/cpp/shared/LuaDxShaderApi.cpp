#include "DxShader.h"
#include "LuaDxApi.h"
#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "lua.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

namespace WorldRPG::JniBridge {
bool LoadPixels(const std::string&,std::vector<uint32_t>&,unsigned&,unsigned&);
bool StorePixels(const std::string&,const std::vector<uint32_t>&,unsigned,unsigned);
}
namespace WorldRPG::DxShader {
namespace {
struct Element {
    std::shared_ptr<Program> program;
    std::string owner;
    uint16_t net=0;
    uint32_t generation=0;
};
std::map<uint32_t,Element> elements;
uint32_t nextId=0x38000000,nextBitmap=0x39000000;
bool Number(lua_State* L,int index,float& value,float fallback=0,bool optional=false) {
    if(optional && lua_isnoneornil(L,index)){value=fallback;return true;}
    if(!lua_isnumber(L,index))return false;
    double v=lua_tonumber(L,index);if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max())return false;
    value=v;return true;
}
int Result(lua_State* L,bool result){lua_pushboolean(L,result);return 1;}
Element* Find(lua_State* L){auto it=elements.find(Script::ElementIdOf(L,1));return it==elements.end()?nullptr:&it->second;}
int CreateLua(lua_State* L) {
    if(lua_type(L,1)!=LUA_TSTRING)return Result(L,false);
    size_t size;const char* input=lua_tolstring(L,1,&size);if(size>512*1024)return Result(L,false);
    std::string source(input,size);
    if(source.find('\n')==std::string::npos && source.find('{')==std::string::npos) {
        std::string file=Script::ActiveVmResourceFile(source);std::ifstream stream(file,std::ios::binary);
        if(!stream)return Result(L,false);
        source.resize(512*1024+1);stream.read(source.data(),source.size());source.resize(stream.gcount());
        if(source.size()>512*1024)return Result(L,false);
    }
    int index=2;std::map<std::string,std::string> macros;
    if(lua_istable(L,index)) {
        lua_pushnil(L);while(lua_next(L,index)) {
            if(lua_type(L,-2)!=LUA_TSTRING || (lua_type(L,-1)!=LUA_TSTRING && lua_type(L,-1)!=LUA_TNUMBER)) {
                lua_pop(L,2);return Result(L,false);
            }
            macros[lua_tostring(L,-2)]=lua_tostring(L,-1);lua_pop(L,1);
            if(macros.size()>64){lua_pop(L,1);return Result(L,false);}
        }
        ++index;
    }
    float priority,distance;
    if(!Number(L,index,priority,0,true)||!Number(L,index+1,distance,0,true)||distance<0 ||
       (!lua_isnoneornil(L,index+2)&&!lua_isboolean(L,index+2)) ||
       (!lua_isnoneornil(L,index+3)&&lua_type(L,index+3)!=LUA_TSTRING))return Result(L,false);
    // Priority/distance/layering and element-type masks are world-texture
    // application options. This backend exposes DX image shaders only.
    if(priority!=0 || distance!=0 || lua_toboolean(L,index+2))return Result(L,false);
    std::string technique,diagnostic;
    auto program=Create(source,macros,technique,diagnostic);
    if(!program){LUA_LOGI("dxCreateShader: %s",diagnostic.c_str());return Result(L,false);}
    if(elements.size()>=128)return Result(L,false);
    const uint32_t id=++nextId;
    elements[id]={std::move(program),Script::ActiveVmOwner(),Script::ActiveVmOwnerNet(),Script::ActiveVmOwnerGeneration()};
    Script::PushElement(L,id);lua_pushlstring(L,technique.data(),technique.size());return 2;
}
int ValueLua(lua_State* L) {
    Element* element=Find(L);if(!element||lua_type(L,2)!=LUA_TSTRING)return Result(L,false);
    std::string name=lua_tostring(L,2);
    if(lua_isboolean(L,3))return Result(L,SetNumbers(*element->program,name,{float(lua_toboolean(L,3))},true));
    if(lua_istable(L,3) && Script::ElementIdOf(L,3)) {
        std::string source;if(!Script::DxTextureFile(Script::ElementIdOf(L,3),source))return Result(L,false);
        Image image;if(!JniBridge::LoadPixels(source,image.pixels,image.width,image.height))return Result(L,false);
        return Result(L,SetTexture(*element->program,name,std::move(image)));
    }
    std::vector<float> values;
    if(lua_istable(L,3)) {
        const size_t count=lua_rawlen(L,3);if(!count||count>16)return Result(L,false);
        for(size_t i=1;i<=count;++i){lua_rawgeti(L,3,i);float v;bool ok=Number(L,-1,v);lua_pop(L,1);if(!ok)return Result(L,false);values.push_back(v);}
    } else {
        for(int index=3;index<=lua_gettop(L);++index){float v;if(values.size()==16||!Number(L,index,v))return Result(L,false);values.push_back(v);}
    }
    return Result(L,SetNumbers(*element->program,name,values));
}
int TessellationLua(lua_State* L) {
    auto* element=Find(L);float x,y;
    if(!element||!Number(L,2,x)||!Number(L,3,y)||x!=std::floor(x)||y!=std::floor(y)||x<1||y<1||x>64||y>64)return Result(L,false);
    return Result(L,SetTessellation(*element->program,unsigned(x),unsigned(y)));
}
int TransformLua(lua_State* L) {
    auto* element=Find(L);if(!element)return Result(L,false);
    Transform t;
    for(int i=0;i<3;++i)if(!Number(L,2+i,t.rotation[i],0,true)||!Number(L,5+i,t.rotationCenter[i],0,true))return Result(L,false);
    if(!lua_isnoneornil(L,8)&&!lua_isboolean(L,8))return Result(L,false);t.rotationScreen=lua_toboolean(L,8);
    for(int i=0;i<2;++i)if(!Number(L,9+i,t.perspectiveCenter[i],0,true))return Result(L,false);
    if(!lua_isnoneornil(L,11)&&!lua_isboolean(L,11))return Result(L,false);t.perspectiveScreen=lua_toboolean(L,11);
    return Result(L,SetTransform(*element->program,t));
}
}
void Reset(){elements.clear();}
bool Exists(uint32_t id){return elements.count(id);}
bool Size(uint32_t id,unsigned& width,unsigned& height) {
    auto it=elements.find(id);if(it==elements.end())return false;
    MaterialSize(*it->second.program,width,height);return true;
}
bool Release(uint32_t id){return elements.erase(id)>0;}
void DropResource(const std::string& owner,uint16_t net,uint32_t generation) {
    for(auto it=elements.begin();it!=elements.end();) {
        auto& e=it->second;
        if(e.owner==owner && (!net||!e.net||e.net==net)&&(!generation||!e.generation||e.generation==generation))it=elements.erase(it);else ++it;
    }
}
bool DrawImage(uint32_t id,const Tile& original,bool section,bool postGUI) {
    auto it=elements.find(id);if(it==elements.end())return false;
    // The bitmap spans the viewport, including transparent uncovered pixels.
    // Canvas SRC would clear the destination outside the shader geometry too.
    // Refuse overwrite until a separate fragment coverage mask is available.
    if(Script::DxIsOverwriteBlend())return false;
    Tile tile=original;
    if(section){unsigned width,height;MaterialSize(*it->second.program,width,height);tile.u/=width;tile.v/=height;tile.usize/=width;tile.vsize/=height;}
    int width=0,height=0;Script::DxDestinationSize(width,height);if(width<=0||height<=0)return false;
    Image image;if(!Render(*it->second.program,tile,width,height,image))return false;
    const uint32_t bitmap=++nextBitmap;const std::string name="tx:"+std::to_string(bitmap);
    if(!JniBridge::StorePixels(name,image.pixels,image.width,image.height))return false;
    char header[256];std::snprintf(header,sizeof(header),"I 0 0 %u %u 0 0 -1 -1 4294967295 0 0 0 ",image.width,image.height);
    Script::DxAppendDraw(std::string(header)+name+'\n',postGUI);
    // D is emitted at the next frame boundary, after retained target commands.
    Script::DxFreeOverlayBitmap(bitmap);return true;
}
void InstallShaderLua(lua_State* L) {
    Reset();
    const struct{const char* name;lua_CFunction function;} binds[] = {
        {"dxCreateShader",CreateLua},{"dxSetShaderValue",ValueLua},
        {"dxSetShaderTessellation",TessellationLua},{"dxSetShaderTransform",TransformLua}};
    for(auto binding:binds){lua_pushcfunction(L,binding.function);lua_setglobal(L,binding.name);}
}
} // namespace WorldRPG::DxShader
