#include "LuaSpatialApi.h"
#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "LuaFieldApi.h"
#include "LuaUtilApi.h"
#include "ResourceLoader.h"
#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"
#include "lua.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>

namespace WorldRPG::Script {
namespace {
struct LocalShape { Spatial::Element element; std::string owner; uint16_t netId=0; uint32_t generation=0; };
std::map<uint32_t,LocalShape> localShapes;
std::map<uint32_t,uint32_t> markerCols;
Spatial::World world;
bool pulsing=false;
int Fail(lua_State* L) { lua_pushboolean(L,0); return 1; }
int WarnFail(lua_State* L, const std::string& message) {
    LUA_LOGI("WARNING: %s", message.c_str());
    lua_pushboolean(L, 0);
    return 1;
}
bool IsElementArg(lua_State* L, int index) {
    if (!lua_istable(L, index)) return false;
    lua_getfield(L, index, "element");
    const bool element = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return element;
}
std::string ElementTypeName(lua_State* L, int index) {
    const int top = lua_gettop(L);
    lua_getglobal(L, "getElementType");
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        return "element";
    }
    lua_pushvalue(L, index);
    if (lua_pcall(L, 1, 1, 0) != 0) {
        lua_settop(L, top);
        return "element";
    }
    const char* name = lua_tostring(L, -1);
    std::string out = name && name[0] ? name : "element";
    lua_settop(L, top);
    return out;
}
// CScriptArgReader::ResolveErrorGotArgumentTypeAndValue. Element tables stand in for userdata.
std::string ScriptGot(lua_State* L, int index) {
    const int type = lua_type(L, index);
    if (type == LUA_TTABLE && IsElementArg(L, index)) {
        const uint32_t id = ElementIdOf(L, index);
        if (!KnownClientElement(id)) return "destroyed element";
        return ElementTypeName(L, index);
    }
    const char* name = "none";
    switch (type) {
        case LUA_TNIL: name = "nil"; break;
        case LUA_TBOOLEAN: name = "boolean"; break;
        case LUA_TNUMBER: name = "number"; break;
        case LUA_TSTRING: name = "string"; break;
        case LUA_TTABLE: name = "table"; break;
        case LUA_TFUNCTION: name = "function"; break;
        case LUA_TUSERDATA: name = "userdata"; break;
        case LUA_TTHREAD: name = "thread"; break;
        case LUA_TLIGHTUSERDATA: name = "light userdata"; break;
        default: break;
    }
    // lua_tolstring converts numbers only. nil, bool, and table stay unquoted.
    const char* value = (type == LUA_TNUMBER || type == LUA_TSTRING) ? lua_tostring(L, index) : nullptr;
    if (!value || !value[0]) return name;
    std::string out = name;
    out += " '";
    out += value;
    out += "'";
    return out;
}
// CLuaFunctionParser::ReadParameterAsString.
std::string ParserGot(lua_State* L, int index) {
    const int type = lua_type(L, index);
    if (type == LUA_TTABLE && IsElementArg(L, index)) {
        const uint32_t id = ElementIdOf(L, index);
        if (!KnownClientElement(id)) return "destroyed element";
        return ElementTypeName(L, index);
    }
    if (type == LUA_TNONE) return "none";
    if (type == LUA_TNIL) return "nil";
    if (type == LUA_TBOOLEAN) return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
    if (type == LUA_TNUMBER) {
        const char* text = lua_tostring(L, index);
        return std::string("number (") + (text ? text : "0") + ")";
    }
    if (type == LUA_TSTRING) {
        size_t length = 0;
        const char* text = lua_tolstring(L, index, &length);
        std::string value = text ? std::string(text, length) : std::string();
        if (std::any_of(value.begin(), value.end(), [](unsigned char ch) { return !std::isprint(ch); })) return "string";
        if (value.size() > 10) {
            value.resize(10);
            value[7] = value[8] = value[9] = '.';
        }
        return "string (\"" + value + "\")";
    }
    if (type == LUA_TTABLE) return "table";
    if (type == LUA_TFUNCTION) return "function";
    if (type == LUA_TTHREAD) return "coroutine";
    return "userdata";
}
std::string BadArgument(const char* function, const std::string& detail) {
    return std::string("Bad argument @ '") + function + "' [" + detail + "]";
}
std::string BadUsage(const char* function, const char* detail) {
    return std::string("Bad usage @ '") + function + "' [" + detail + "]";
}
struct Args {
    lua_State* L;
    int index = 1;
    bool bad = false;
    std::string detail;
    void Fail(const std::string& text) {
        if (!bad) {
            bad = true;
            detail = text;
        }
    }
    void Type(const char* expected, int at) { Fail(std::string("Expected ") + expected + " at argument " + std::to_string(at) + ", got " + ScriptGot(L, at)); }
    void Custom(const char* text) { Fail(text); }
};
bool ReadNumber(lua_State* L, Args& args, float& out) {
    const int at = args.index;
    const int type = lua_type(L, at);
    if (type == LUA_TNUMBER || type == LUA_TSTRING) {
        if (!lua_isnumber(L, at)) {
            args.Custom("Expected number, got non-convertible string");
            ++args.index;
            return false;
        }
        const lua_Number number = lua_tonumber(L, at);
        ++args.index;
        if (std::isnan(number)) {
            args.Custom("Expected number, got NaN");
            return false;
        }
        // createColSphere already rejects non-finite input. Desktop ReadNumber keeps inf.
        if (!std::isfinite(number)) {
            args.Custom("Expected number, got inf");
            return false;
        }
        if (std::abs(number) > std::numeric_limits<float>::max()) {
            args.Custom("Expected number within the destination type range, got out-of-range number");
            return false;
        }
        out = static_cast<float>(number);
        return !args.bad;
    }
    args.Type("number", at);
    ++args.index;
    return false;
}
bool ReadUint(lua_State* L, Args& args, uint32_t& out) {
    const int index = args.index;
    float unused = 0.f;
    if (!ReadNumber(L, args, unused)) return false;
    const lua_Number number = lua_tonumber(L, index);
    if (number < -std::numeric_limits<float>::epsilon()) {
        args.Custom("Expected positive value, got negative");
        return false;
    }
    if (number > static_cast<lua_Number>(std::numeric_limits<uint32_t>::max())) {
        args.Custom("Expected number within the destination type range, got out-of-range number");
        return false;
    }
    out = static_cast<uint32_t>(number);
    return !args.bad;
}
bool ReadUintDefault(lua_State* L, Args& args, uint32_t& out, uint32_t fallback) {
    const int type = lua_type(L, args.index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        out = fallback;
        ++args.index;
        return true;
    }
    return ReadUint(L, args, out);
}
bool ReadVecParts(lua_State* L, Args& args, Spatial::Vec& out, int count, const char* expected) {
    const int at = args.index;
    if (lua_istable(L, at)) {
        if (IsElementArg(L, at)) {
            args.Type(expected, at);
            ++args.index;
            return false;
        }
        const char* keys[] = {"x", "y", "z"};
        float* parts[] = {&out.x, &out.y, &out.z};
        for (int i = 0; i < count; ++i) {
            lua_getfield(L, at, keys[i]);
            const lua_Number value = lua_tonumber(L, -1);
            const bool ok = lua_isnumber(L, -1) && std::isfinite(value) &&
                            std::abs(value) <= std::numeric_limits<float>::max();
            if (ok) *parts[i] = static_cast<float>(value);
            lua_pop(L, 1);
            if (!ok) {
                args.Type(expected, at);
                ++args.index;
                return false;
            }
        }
        ++args.index;
        return true;
    }
    if (count == 2) return ReadNumber(L, args, out.x) && ReadNumber(L, args, out.y);
    return ReadNumber(L, args, out.x) && ReadNumber(L, args, out.y) && ReadNumber(L, args, out.z);
}
bool NextIsVec2(lua_State* L, int index) {
    if (lua_istable(L, index) && !IsElementArg(L, index)) return true;
    return (lua_type(L, index) == LUA_TNUMBER || lua_type(L, index) == LUA_TSTRING) &&
           (lua_type(L, index + 1) == LUA_TNUMBER || lua_type(L, index + 1) == LUA_TSTRING);
}
// Server colshapes are addressed as {id=n} in the host tests, the same way
// isElementWithinColShape already reads them. Local shapes must still be alive.
bool AsShape(lua_State* L, int index, Spatial::Element& out) {
    if (!lua_istable(L, index)) return false;
    const uint32_t id = ElementIdOf(L, index);
    if (!id || !CopySpatialShape(id, out) || !out.shape) return false;
    if (localShapes.find(id) != localShapes.end() && !KnownClientElement(id)) return false;
    return true;
}
void Collect(bool native = true);
bool CommitShape(Spatial::Element shape) {
    if (!shape.shape) return false;
    const uint32_t id = shape.id;
    shape.bounds = Spatial::ShapeBounds(shape.position, *shape.shape);
    if (auto it = localShapes.find(id); it != localShapes.end()) {
        shape.generation = it->second.element.generation;
        shape.shapeRevision = it->second.element.shapeRevision + 1;
        it->second.element = std::move(shape);
    } else if (!ServerWorld::UpdateColshape(id, [&](Spatial::Geometry& geometry) {
                   geometry = *shape.shape;
                   return true;
               })) {
        return false;
    }
    // CStaticFunctionDefinitions::RefreshColShapeColliders runs hit detection immediately.
    Collect(false);
    world.Refresh(id);
    PulseSpatial();
    return true;
}
bool g_showCollision = false;
bool g_diagShow = false;
bool Number(lua_State* L,int i,float& out) {
    if(!lua_isnumber(L,i)) return false;
    out=static_cast<float>(lua_tonumber(L,i)); return std::isfinite(out);
}
bool Vec(lua_State* L,int& i,Spatial::Vec& out) {
    if(lua_istable(L,i)) {
        const char* keys[]={"x","y","z"}; float* v[]={&out.x,&out.y,&out.z};
        for(unsigned k=0;k<3;++k) { lua_getfield(L,i,keys[k]); bool ok=Number(L,-1,*v[k]); lua_pop(L,1); if(!ok) return false; }
        ++i; return true;
    }
    if(!Number(L,i,out.x)||!Number(L,i+1,out.y)||!Number(L,i+2,out.z)) return false;
    i+=3; return true;
}
void Collect(bool native) {
    std::vector<Spatial::Element> scene;
    ServerWorld::CopySpatialElements(scene);
    for(auto& e:scene) {
        float x=0,y=0,z=0,h=0;
        if(e.id==ServerWorld::LocalPlayer()) { Game::CopyPose(x,y,z,h); e.position={x,y,z}; e.bounds.center=e.position; }
        else if(!e.shape && Game::CopyRemotePose(e.id,x,y,z)) { e.position={x,y,z}; e.bounds.center=e.position; }
    }
    if(native) Game::CopySpatialScene(scene);
    else for(auto& e:scene) if(!e.shape) {
        const auto* old=world.Find(e.id);
        if(old && old->generation==e.generation) { e.position=old->position; e.bounds=old->bounds; }
    }
    for(auto& e:scene) ApplySpatialArea(e.id,e.generation,e.interior,e.dimension);
    for(const auto& [id,shape]:localShapes) scene.push_back(shape.element);
    world.Sync(scene);
}
bool Current(uint32_t id,uint64_t generation) {
    if(auto it=localShapes.find(id);it!=localShapes.end()) return it->second.element.generation==generation;
    if(ServerWorld::ElementGeneration(id)==generation && generation) return true;
    return Game::ScriptPedAlive(id) && generation==id;
}
void Dispatch(const Spatial::Transition& t) {
    if(!Current(t.shape,t.shapeGeneration)||!Current(t.entity,t.entityGeneration)) return;
    if(t.hit) ActiveVmFireSpatialEvent("onClientColShapeHit",t.shape,t.entity,t.matchingDimension);
    else ActiveVmFireSpatialEvent("onClientColShapeLeave",t.shape,t.entity,t.matchingDimension);
    // A handler can destroy its source or stop the owning resource.
    if(!Current(t.shape,t.shapeGeneration)||!Current(t.entity,t.entityGeneration)) return;
    Collect(false);
    const auto* s=world.Find(t.shape); const auto* e=world.Find(t.entity);
    if(s&&e) {
        if(t.hit) ActiveVmFireSpatialEvent("onClientElementColShapeHit",t.entity,t.shape,s->dimension==e->dimension);
        else ActiveVmFireSpatialEvent("onClientElementColShapeLeave",t.entity,t.shape,s->dimension==e->dimension);
    }
}
int Create(lua_State* L,bool cuboid) {
    int i=1; Spatial::Element e; Spatial::Geometry g;
    if(!Vec(L,i,e.position)) return Fail(L);
    if(cuboid) {
        g.type=Spatial::ShapeType::Cuboid;
        if(!Vec(L,i,g.size)) return Fail(L);
        if(g.size.x<0) g.size.x=.1f;
        if(g.size.y<0) g.size.y=.1f; // desktop preserves negative Z
    } else {
        g.type=Spatial::ShapeType::Sphere;
        if(!Number(L,i,g.radius)) return Fail(L);
        if(g.radius<0) g.radius=.1f;
    }
    const std::string owner=ActiveVmOwner(); ResourceLoader::ResourceView caller;
    if(owner.empty()||!ResourceLoader::CopyCallerResource(ActiveVmOwnerNet(),ActiveVmOwnerGeneration(),owner,caller)||!caller.root) return Fail(L);
    e.id=CreateResourceElement("colshape"); if(!e.id) return Fail(L);
    e.generation=e.id; e.type="colshape"; e.shape=g; e.bounds=Spatial::ShapeBounds(e.position,g);
    localShapes.emplace(e.id,LocalShape{e,owner,ActiveVmOwnerNet(),ActiveVmOwnerGeneration()});
    // CStaticFunctionDefinitions.cpp:7598-7612 intentionally does not refresh on creation.
    Collect(false); PushElement(L,e.id); return 1;
}
int CreateColSphere(lua_State* L) { return Create(L,false); }
int CreateColCuboid(lua_State* L) { return Create(L,true); }
int IsElementWithinColShape(lua_State* L) {
    Spatial::Element shape;
    uint32_t e=ElementIdOf(L,1),s=ElementIdOf(L,2);
    if(!KnownClientElement(e)||!CopySpatialShape(s,shape)) { lua_pushnil(L); return 1; }
    Collect(false); lua_pushboolean(L,world.Within(e,s)); return 1;
}
int GetElementsWithinColShape(lua_State* L) {
    Spatial::Element shape; uint32_t id=ElementIdOf(L,1);
    if(!CopySpatialShape(id,shape)) return Fail(L);
    std::string type;
    if(!lua_isnoneornil(L,2)) { if(!lua_isstring(L,2)) return Fail(L); type=lua_tostring(L,2); }
    Collect(false); lua_newtable(L); int i=0;
    for(uint32_t e:world.Colliders(id,type)) { PushElement(L,e); lua_rawseti(L,-2,++i); }
    return 1;
}
int GetElementsWithinRange(lua_State* L) {
    int i=1; Spatial::Vec pos; float radius=0;
    if(!Vec(L,i,pos)||!Number(L,i++,radius)) return Fail(L);
    std::optional<std::string> type; std::optional<uint16_t> interior,dimension;
    if(!lua_isnoneornil(L,i)) { if(!lua_isstring(L,i)) return Fail(L); type=lua_tostring(L,i); } ++i;
    for(auto* value:{&interior,&dimension}) {
        if(!lua_isnoneornil(L,i)) { float n=0; if(!Number(L,i,n)||n<0||n>65535) return Fail(L); *value=static_cast<uint16_t>(n); }
        ++i;
    }
    Collect(); lua_newtable(L); i=0;
    for(uint32_t id:world.Range(pos,radius,type,interior,dimension)) { PushElement(L,id); lua_rawseti(L,-2,++i); }
    return 1;
}
int GetElementsByType(lua_State* L) {
    const char* type=lua_tostring(L,1);
    if(!type || std::string(type)!="colshape") {
        int n=lua_gettop(L); lua_pushvalue(L,lua_upvalueindex(1)); lua_insert(L,1); lua_call(L,n,LUA_MULTRET); return lua_gettop(L);
    }
    uint32_t root=ClientRootElement();
    if(lua_istable(L,2)) { root=ElementIdOf(L,2); if(!KnownClientElement(root)) return Fail(L); }
    Collect(false); std::vector<Spatial::Element> shapes;
    ServerWorld::CopySpatialElements(shapes);
    for(const auto& [id,shape]:localShapes) shapes.push_back(shape.element);
    lua_newtable(L); int i=0;
    for(const auto& e:shapes) if(e.shape && ClientDescendantOf(e.id,root)) { PushElement(L,e.id); lua_rawseti(L,-2,++i); }
    return 1;
}
// Adds the colshape branch to the existing position setter. Other element
// backends retain their previous behavior and are not counted as native parity.
int SetElementPosition(lua_State* L) {
    const uint32_t id=ElementIdOf(L,1); Spatial::Element shape;
    if(!KnownClientElement(id)) return Fail(L);
    if(!CopySpatialShape(id,shape)) {
        int n=lua_gettop(L); lua_pushvalue(L,lua_upvalueindex(1)); lua_insert(L,1); lua_call(L,n,LUA_MULTRET); return lua_gettop(L);
    }
    int i=2; Spatial::Vec position;
    if(!Vec(L,i,position)) return Fail(L);
    if(!lua_isnoneornil(L,i)&&!lua_isboolean(L,i)) return Fail(L);
    if(shape.position!=position) {
        if(auto local=localShapes.find(id);local!=localShapes.end()) {
            // CClientColPolygon::SetPosition moves every point by the same XY delta.
            auto& element=local->second.element;
            if(element.shape && element.shape->type==Spatial::ShapeType::Polygon) {
                const float dx=position.x-element.position.x, dy=position.y-element.position.y;
                for(auto& point:element.shape->points) { point.x+=dx; point.y+=dy; }
            }
            element.position=position;
            element.bounds=Spatial::ShapeBounds(element.position,*element.shape);
        } else ServerWorld::NoteElementPosition(id,position.x,position.y,position.z);
        Collect(false); world.Refresh(id);
        PulseSpatial();
    }
    lua_pushboolean(L,1); return 1;
}
int Spawn(lua_State* L, Spatial::Vec position, Spatial::Geometry geometry) {
    const std::string owner = ActiveVmOwner();
    ResourceLoader::ResourceView caller;
    if (owner.empty() || !ResourceLoader::CopyCallerResource(ActiveVmOwnerNet(), ActiveVmOwnerGeneration(), owner, caller) || !caller.root)
        return Fail(L);
    Spatial::Element element;
    element.id = CreateResourceElement("colshape");
    if (!element.id) return Fail(L);
    element.generation = element.id;
    element.type = "colshape";
    element.position = position;
    element.shape = geometry;
    element.bounds = Spatial::ShapeBounds(position, geometry);
    localShapes.emplace(element.id, LocalShape{element, owner, ActiveVmOwnerNet(), ActiveVmOwnerGeneration()});
    // Creation does not refresh colliders (CStaticFunctionDefinitions.cpp:7590-7635).
    Collect(false);
    PushElement(L, element.id);
    return 1;
}
int CreateFailed(lua_State* L, const char* function, const Args& args, bool bare) {
    if (bare) return WarnFail(L, std::string("Bad argument @ '") + function + "'");
    return WarnFail(L, BadArgument(function, args.detail));
}
int CreateColCircle(lua_State* L) {
    // CLuaColShapeDefs::CreateColCircle (109-150). Z is SPATIAL_2D_Z.
    Args args{L};
    Spatial::Vec position{};
    float radius = 0.1f;
    ReadVecParts(L, args, position, 2, "vector2");
    ReadNumber(L, args, radius);
    if (args.bad) return CreateFailed(L, "createColCircle", args, false);
    if (radius < 0.f) radius = 0.1f;
    position.z = 0.f;
    Spatial::Geometry geometry;
    geometry.type = Spatial::ShapeType::Circle;
    geometry.radius = radius;
    return Spawn(L, position, geometry);
}
int CreateColRectangle(lua_State* L) {
    // CLuaColShapeDefs::CreateColRectangle (241-286).
    Args args{L};
    Spatial::Vec position{};
    Spatial::Vec size{};
    ReadVecParts(L, args, position, 2, "vector2");
    ReadVecParts(L, args, size, 2, "vector2");
    if (args.bad) return CreateFailed(L, "createColRectangle", args, false);
    if (size.x < 0.f) size.x = 0.1f;
    if (size.y < 0.f) size.y = 0.1f;
    position.z = 0.f;
    Spatial::Geometry geometry;
    geometry.type = Spatial::ShapeType::Rectangle;
    geometry.size = size;
    return Spawn(L, position, geometry);
}
int CreateColTube(lua_State* L) {
    // CLuaColShapeDefs::CreateColTube (339-386). Arg errors use LogBadType, not the full message.
    Args args{L};
    Spatial::Vec position{};
    float radius = 0.1f;
    float height = 0.1f;
    ReadVecParts(L, args, position, 3, "vector3");
    ReadNumber(L, args, radius);
    ReadNumber(L, args, height);
    if (args.bad) return CreateFailed(L, "createColTube", args, true);
    if (radius < 0.f) radius = 0.1f;
    if (height < 0.f) height = 0.1f;
    Spatial::Geometry geometry;
    geometry.type = Spatial::ShapeType::Tube;
    geometry.radius = radius;
    geometry.height = height;
    return Spawn(L, position, geometry);
}
int CreateColPolygon(lua_State* L) {
    // CLuaColShapeDefs::CreateColPolygon (288-337). At least three points, then any further vector2.
    Args args{L};
    Spatial::Vec center{};
    ReadVecParts(L, args, center, 2, "vector2");
    std::vector<Spatial::Vec> points;
    for (int count = 0; count < 3 || NextIsVec2(L, args.index); ++count) {
        Spatial::Vec point{};
        if (!ReadVecParts(L, args, point, 2, "vector2")) break;
        points.push_back(point);
    }
    if (args.bad) return CreateFailed(L, "createColPolygon", args, false);
    center.z = 0.f;
    Spatial::Geometry geometry;
    geometry.type = Spatial::ShapeType::Polygon;
    geometry.points = std::move(points);
    geometry.floor = std::numeric_limits<float>::lowest();
    geometry.ceiling = std::numeric_limits<float>::max();
    return Spawn(L, center, geometry);
}
int ShapeMiss(lua_State* L, const char* function, bool hard) {
    const std::string message = BadArgument(function, "Expected colshape at argument 1, got " + ScriptGot(L, 1));
    if (hard) return luaL_error(L, "%s", message.c_str());
    return WarnFail(L, message);
}
int GetColShapeType(lua_State* L) {
    // CLuaColShapeDefs::GetColShapeType (84-107). The enum value, not the name.
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "getColShapeType", false);
    lua_pushnumber(L, static_cast<lua_Number>(static_cast<unsigned>(shape.shape->type)));
    return 1;
}
int IsInsideColShape(lua_State* L) {
    // CLuaColShapeDefs::IsInsideColShape (388-412) and DoHitDetection with a zero entity radius.
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "isInsideColShape", false);
    Args args{L};
    args.index = 2;
    Spatial::Vec point{};
    ReadVecParts(L, args, point, 3, "vector3");
    if (args.bad) return WarnFail(L, BadArgument("isInsideColShape", args.detail));
    lua_pushboolean(L, Spatial::Hit(shape.position, *shape.shape, point) ? 1 : 0);
    return 1;
}
bool RadiusShape(Spatial::ShapeType type) {
    return type == Spatial::ShapeType::Circle || type == Spatial::ShapeType::Sphere || type == Spatial::ShapeType::Tube;
}
int GetColShapeRadius(lua_State* L) {
    // CLuaColShapeDefs::GetColShapeRadius (414-433).
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "getColShapeRadius", true);
    if (!RadiusShape(shape.shape->type)) return luaL_error(L, "%s", BadUsage("getColShapeRadius", "ColShape must be Circle, Sphere or Tube").c_str());
    lua_pushnumber(L, shape.shape->radius);
    return 1;
}
int SetColShapeRadius(lua_State* L) {
    // CLuaColShapeDefs::SetColShapeRadius (435-455) and CStaticFunctionDefinitions::SetColShapeRadius (7658-7680).
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "setColShapeRadius", true);
    Args args{L};
    args.index = 2;
    float radius = 0.f;
    ReadNumber(L, args, radius);
    if (args.bad) return luaL_error(L, "%s", BadArgument("setColShapeRadius", args.detail).c_str());
    if (!RadiusShape(shape.shape->type)) return luaL_error(L, "%s", BadUsage("setColShapeRadius", "ColShape must be Circle, Sphere or Tube").c_str());
    if (radius < 0.f) radius = 0.f;
    shape.shape->radius = radius;
    if (!CommitShape(shape)) return Fail(L);
    lua_pushboolean(L, 1);
    return 1;
}
int GetColShapeSize(lua_State* L) {
    // CLuaColShapeDefs::GetColShapeSize (457-494).
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "getColShapeSize", true);
    const Spatial::Geometry& geometry = *shape.shape;
    if (geometry.type == Spatial::ShapeType::Rectangle) {
        lua_pushnumber(L, geometry.size.x);
        lua_pushnumber(L, geometry.size.y);
        return 2;
    }
    if (geometry.type == Spatial::ShapeType::Cuboid) {
        lua_pushnumber(L, geometry.size.x);
        lua_pushnumber(L, geometry.size.y);
        lua_pushnumber(L, geometry.size.z);
        return 3;
    }
    if (geometry.type == Spatial::ShapeType::Tube) {
        lua_pushnumber(L, geometry.height);
        return 1;
    }
    return luaL_error(L, "%s", BadUsage("getColShapeSize", "ColShape must be Rectangle, Cuboid or Tube").c_str());
}
int SetColShapeSize(lua_State* L) {
    // CLuaColShapeDefs::SetColShapeSize (532-578) and CStaticFunctionDefinitions::SetColShapeSize (7683-7715).
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "setColShapeSize", true);
    Args args{L};
    args.index = 2;
    Spatial::Vec size{};
    if (shape.shape->type == Spatial::ShapeType::Rectangle) ReadVecParts(L, args, size, 2, "vector2");
    else if (shape.shape->type == Spatial::ShapeType::Cuboid) ReadVecParts(L, args, size, 3, "vector3");
    else if (shape.shape->type == Spatial::ShapeType::Tube) ReadNumber(L, args, size.x);
    else return luaL_error(L, "%s", BadUsage("setColShapeSize", "ColShape must be Rectangle, Cuboid or Tube").c_str());
    if (args.bad) return luaL_error(L, "%s", BadArgument("setColShapeSize", args.detail).c_str());
    if (size.x < 0.f) size.x = 0.f;
    if (size.y < 0.f) size.y = 0.f;
    if (size.z < 0.f) size.z = 0.f;
    if (shape.shape->type == Spatial::ShapeType::Tube) shape.shape->height = size.x;
    else shape.shape->size = size;
    if (!CommitShape(shape)) return Fail(L);
    lua_pushboolean(L, 1);
    return 1;
}
int GetColPolygonPoints(lua_State* L) {
    // CLuaColShapeDefs::GetColPolygonPoints (580-618). Each point is {x, y}.
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "getColPolygonPoints", true);
    if (shape.shape->type != Spatial::ShapeType::Polygon)
        return luaL_error(L, "%s", BadUsage("getColPolygonPoints", "ColShape must be Polygon").c_str());
    lua_newtable(L);
    int index = 0;
    for (const Spatial::Vec& point : shape.shape->points) {
        lua_pushnumber(L, ++index);
        lua_newtable(L);
        lua_pushnumber(L, 1);
        lua_pushnumber(L, point.x);
        lua_settable(L, -3);
        lua_pushnumber(L, 2);
        lua_pushnumber(L, point.y);
        lua_settable(L, -3);
        lua_settable(L, -3);
    }
    return 1;
}
int GetColPolygonPointPosition(lua_State* L) {
    // CLuaColShapeDefs::GetColPolygonPointPosition (650-680). Lua indexes are 1-based.
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "getColPolygonPointPosition", true);
    Args args{L};
    args.index = 2;
    uint32_t index = 0;
    ReadUint(L, args, index);
    if (args.bad) return luaL_error(L, "%s", BadArgument("getColPolygonPointPosition", args.detail).c_str());
    if (shape.shape->type != Spatial::ShapeType::Polygon)
        return luaL_error(L, "%s", BadUsage("getColPolygonPointPosition", "ColShape must be Polygon").c_str());
    if (index == 0 || index - 1 >= shape.shape->points.size()) {
        LUA_LOGI("WARNING: Invalid point index");
        lua_pushboolean(L, 0);
        return 1;
    }
    const Spatial::Vec& point = shape.shape->points[index - 1];
    lua_pushnumber(L, point.x);
    lua_pushnumber(L, point.y);
    return 2;
}
int SetColPolygonPointPosition(lua_State* L) {
    // CLuaColShapeDefs::SetColPolygonPointPosition (714-745).
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "setColPolygonPointPosition", true);
    Args args{L};
    args.index = 2;
    uint32_t index = 0;
    Spatial::Vec point{};
    ReadUint(L, args, index);
    ReadVecParts(L, args, point, 2, "vector2");
    if (args.bad) return luaL_error(L, "%s", BadArgument("setColPolygonPointPosition", args.detail).c_str());
    if (shape.shape->type != Spatial::ShapeType::Polygon)
        return luaL_error(L, "%s", BadUsage("setColPolygonPointPosition", "ColShape must be Polygon").c_str());
    if (index == 0 || index - 1 >= shape.shape->points.size()) {
        LUA_LOGI("WARNING: Invalid point index");
        lua_pushboolean(L, 0);
        return 1;
    }
    shape.shape->points[index - 1] = point;
    if (!CommitShape(shape)) return Fail(L);
    lua_pushboolean(L, 1);
    return 1;
}
int AddColPolygonPoint(lua_State* L) {
    // CLuaColShapeDefs::AddColPolygonPoint (747-782). Index 0 appends.
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "addColPolygonPoint", true);
    Args args{L};
    args.index = 2;
    Spatial::Vec point{};
    uint32_t index = 0;
    ReadVecParts(L, args, point, 2, "vector2");
    ReadUintDefault(L, args, index, 0);
    if (args.bad) return luaL_error(L, "%s", BadArgument("addColPolygonPoint", args.detail).c_str());
    if (shape.shape->type != Spatial::ShapeType::Polygon)
        return luaL_error(L, "%s", BadUsage("addColPolygonPoint", "ColShape must be Polygon").c_str());
    if (index == 0) shape.shape->points.push_back(point);
    else if (index - 1 > shape.shape->points.size()) {
        LUA_LOGI("WARNING: Invalid point index");
        lua_pushboolean(L, 0);
        return 1;
    } else {
        shape.shape->points.insert(shape.shape->points.begin() + static_cast<std::ptrdiff_t>(index - 1), point);
    }
    if (!CommitShape(shape)) return Fail(L);
    lua_pushboolean(L, 1);
    return 1;
}
int RemoveColPolygonPoint(lua_State* L) {
    // CLuaColShapeDefs::RemoveColPolygonPoint (784-818). The last three points stay.
    Spatial::Element shape;
    if (!AsShape(L, 1, shape)) return ShapeMiss(L, "removeColPolygonPoint", true);
    Args args{L};
    args.index = 2;
    uint32_t index = 0;
    ReadUint(L, args, index);
    if (args.bad) return luaL_error(L, "%s", BadArgument("removeColPolygonPoint", args.detail).c_str());
    if (shape.shape->type != Spatial::ShapeType::Polygon)
        return luaL_error(L, "%s", BadUsage("removeColPolygonPoint", "ColShape must be Polygon").c_str());
    if (shape.shape->points.size() <= 3) {
        LUA_LOGI("WARNING: Can't remove the last 3 points");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (index == 0 || index - 1 >= shape.shape->points.size()) {
        LUA_LOGI("WARNING: Invalid point index");
        lua_pushboolean(L, 0);
        return 1;
    }
    shape.shape->points.erase(shape.shape->points.begin() + static_cast<std::ptrdiff_t>(index - 1));
    if (!CommitShape(shape)) return Fail(L);
    lua_pushboolean(L, 1);
    return 1;
}
bool ReadLimit(lua_State* L, const char* function, int index, float reset, float& out) {
    // ArgumentParser<variant<bool, float>>. Any boolean restores the open end.
    if (lua_type(L, index) == LUA_TBOOLEAN) {
        out = reset;
        return true;
    }
    if (lua_isnumber(L, index)) {
        const lua_Number number = lua_tonumber(L, index);
        if (std::isnan(number))
            luaL_error(L, "%s", BadArgument(function, "Expected number at argument " + std::to_string(index) + ", got NaN").c_str());
        if (!std::isfinite(number))
            luaL_error(L, "%s", BadArgument(function, "Expected number at argument " + std::to_string(index) + ", got inf").c_str());
        out = static_cast<float>(number);
        return true;
    }
    luaL_error(L, "%s", BadArgument(function, "Expected boolean/number at argument " + std::to_string(index) + ", got " + ParserGot(L, index)).c_str());
    return false;
}
bool RequirePolygon(lua_State* L, const char* function, Spatial::Element& out) {
    // UserDataCast<CClientColPolygon> reports the base name "colshape" for every other element.
    if (AsShape(L, 1, out) && out.shape->type == Spatial::ShapeType::Polygon) return true;
    luaL_error(L, "%s", BadArgument(function, "Expected colshape at argument 1, got " + ParserGot(L, 1)).c_str());
    return false;
}
int GetColPolygonHeight(lua_State* L) {
    // CLuaColShapeDefs::GetColPolygonHeight (820-825).
    Spatial::Element shape;
    if (!RequirePolygon(L, "getColPolygonHeight", shape)) return 0;
    lua_pushnumber(L, shape.shape->floor);
    lua_pushnumber(L, shape.shape->ceiling);
    return 2;
}
int SetColPolygonHeight(lua_State* L) {
    // CLuaColShapeDefs::SetColPolygonHeight (827-852). An unchanged span returns false and does not refresh.
    Spatial::Element shape;
    if (!RequirePolygon(L, "setColPolygonHeight", shape)) return 0;
    float floor = 0.f;
    float ceiling = 0.f;
    if (!ReadLimit(L, "setColPolygonHeight", 2, std::numeric_limits<float>::lowest(), floor)) return 0;
    if (!ReadLimit(L, "setColPolygonHeight", 3, std::numeric_limits<float>::max(), ceiling)) return 0;
    if (floor > ceiling) std::swap(floor, ceiling);
    if (shape.shape->floor == floor && shape.shape->ceiling == ceiling) {
        lua_pushboolean(L, 0);
        return 1;
    }
    shape.shape->floor = floor;
    shape.shape->ceiling = ceiling;
    if (!CommitShape(shape)) return Fail(L);
    lua_pushboolean(L, 1);
    return 1;
}
int ShowCol(lua_State* L) {
    // CLuaColShapeDefs::SetShowCollision (854-861). Development mode gates the write.
    if (lua_type(L, 1) != LUA_TBOOLEAN)
        return luaL_error(L, "%s", BadArgument("showCol", "Expected boolean at argument 1, got " + ParserGot(L, 1)).c_str());
    if (!DevelopmentMode()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_showCollision = lua_toboolean(L, 1) != 0;
    if (g_showCollision && !g_diagShow) {
        g_diagShow = true;
        LUA_LOGE("DIAG showCol: CClientColShape::DebugRender is not hooked");
    }
    lua_pushboolean(L, 1);
    return 1;
}
int IsShowCollisionsEnabled(lua_State* L) {
    // CLuaColShapeDefs::IsShowCollisionsEnabled (863-866).
    (void)L;
    lua_pushboolean(L, g_showCollision ? 1 : 0);
    return 1;
}
}
bool SpatialShapeAlive(uint32_t id) { Spatial::Element e; return CopySpatialShape(id,e); }
bool CopySpatialShape(uint32_t id,Spatial::Element& out) {
    if(auto it=localShapes.find(id);it!=localShapes.end()) { out=it->second.element; return true; }
    return ServerWorld::CopyColshape(id,out);
}
bool SpatialSetArea(uint32_t id,unsigned value,bool dimension) {
    Spatial::Element e; if(!CopySpatialShape(id,e)) return false;
    if(auto it=localShapes.find(id);it!=localShapes.end()) {
        if(dimension) it->second.element.dimension=value; else it->second.element.interior=value;
    } else if(dimension) ServerWorld::NoteElementDimension(id,value); else ServerWorld::NoteElementInterior(id,value);
    Collect(false); return true;
}
void ForgetMarkerCol(uint32_t shape) {
    for (auto it = markerCols.begin(); it != markerCols.end();) {
        if (it->second == shape) it = markerCols.erase(it);
        else ++it;
    }
}
uint32_t RetainMarkerColShapeImpl(uint32_t marker, int kind, float x, float y, float z, float size) {
    // CClientMarker::CreateOfType (CClientMarker.cpp 466-532).
    Spatial::Geometry geometry;
    if (kind == 0) {
        geometry.type = Spatial::ShapeType::Circle;
        geometry.radius = size;
    } else if (kind == 2) {
        geometry.type = Spatial::ShapeType::Tube;
        geometry.radius = size;
        geometry.height = size;
    } else {
        geometry.type = Spatial::ShapeType::Sphere;
        geometry.radius = size;
    }
    uint32_t id = 0;
    bool created = false;
    if (const auto found = markerCols.find(marker); found != markerCols.end() && localShapes.contains(found->second))
        id = found->second;
    else {
        id = CreateResourceElement("colshape");
        if (!id) return 0;
        SetClientElementDestroyable(id, false);
        markerCols[marker] = id;
        created = true;
    }
    LocalShape& shape = localShapes[id];
    const bool moved = created || !shape.element.shape || shape.element.position.x != x || shape.element.position.y != y ||
                       shape.element.position.z != z || shape.element.shape->type != geometry.type ||
                       shape.element.shape->radius != geometry.radius || shape.element.shape->height != geometry.height;
    if (shape.owner.empty()) {
        shape.owner = ActiveVmOwner();
        shape.netId = ActiveVmOwnerNet();
        shape.generation = ActiveVmOwnerGeneration();
    }
    shape.element.id = id;
    shape.element.type = "colshape";
    shape.element.generation = shape.element.generation ? shape.element.generation : id;
    if (moved) shape.element.shapeRevision++;
    shape.element.position = {x, y, z};
    shape.element.shape = geometry;
    shape.element.bounds = Spatial::ShapeBounds(shape.element.position, geometry);
    if (moved) {
        Collect(false);
        world.Refresh(id);
        PulseSpatial();
    }
    return id;
}
bool ElementInsideShapeImpl(uint32_t entity, uint32_t shape) {
    Collect(false);
    return world.Within(entity, shape);
}
void SpatialRelease(uint32_t id) {
    localShapes.erase(id);
    world.Remove(id);
    ForgetMarkerCol(id);
}
void SpatialDropResource(const std::string& owner,uint16_t net,uint32_t generation) {
    std::vector<uint32_t> ids;
    for(const auto& [id,s]:localShapes) if(s.owner==owner&&(!net||s.netId==net)&&(!generation||s.generation==generation)) ids.push_back(id);
    for(uint32_t id:ids) {
        if (!DestroyClientElement(id)) DestroyClientElementForced(id);
    }
}
uint32_t RetainMarkerColShape(uint32_t marker, int kind, float x, float y, float z, float size) {
    return RetainMarkerColShapeImpl(marker, kind, x, y, z, size);
}
void ReleaseMarkerColShape(uint32_t marker) {
    const auto found = markerCols.find(marker);
    if (found == markerCols.end()) return;
    const uint32_t id = found->second;
    markerCols.erase(found);
    if (!DestroyClientElement(id)) DestroyClientElementForced(id);
}
bool ElementInsideShape(uint32_t entity, uint32_t shape) { return ElementInsideShapeImpl(entity, shape); }
void PulseSpatial() {
    const bool nested=pulsing;
    if(localShapes.empty()) {
        std::vector<Spatial::Element> scene; ServerWorld::CopySpatialElements(scene);
        if(std::none_of(scene.begin(),scene.end(),[](const auto& e){return e.shape.has_value();})) {
            world.Sync(scene); world.Pulse([](const auto&){}); return;
        }
    }
    Collect(!nested);
    pulsing=true; world.Pulse(Dispatch); pulsing=nested;
}
void ResetSpatial() { localShapes.clear(); markerCols.clear(); world.Clear(); pulsing=false; g_showCollision=false; }
void InstallSpatialLua(lua_State* L) {
    ResetSpatial();
    const struct { const char* name; lua_CFunction fn; } binds[]={
        {"getElementsWithinRange",GetElementsWithinRange},{"createColSphere",CreateColSphere},
        {"createColCuboid",CreateColCuboid},{"createColCircle",CreateColCircle},
        {"createColRectangle",CreateColRectangle},{"createColPolygon",CreateColPolygon},
        {"createColTube",CreateColTube},{"isElementWithinColShape",IsElementWithinColShape},
        {"getElementsWithinColShape",GetElementsWithinColShape},{"getColShapeType",GetColShapeType},
        {"isInsideColShape",IsInsideColShape},{"getColShapeRadius",GetColShapeRadius},
        {"setColShapeRadius",SetColShapeRadius},{"getColShapeSize",GetColShapeSize},
        {"setColShapeSize",SetColShapeSize},{"getColPolygonPoints",GetColPolygonPoints},
        {"getColPolygonPointPosition",GetColPolygonPointPosition},{"setColPolygonPointPosition",SetColPolygonPointPosition},
        {"addColPolygonPoint",AddColPolygonPoint},{"removeColPolygonPoint",RemoveColPolygonPoint},
        {"getColPolygonHeight",GetColPolygonHeight},{"setColPolygonHeight",SetColPolygonHeight},
        {"showCol",ShowCol},{"isShowCollisionsEnabled",IsShowCollisionsEnabled},
    };
    for(const auto& b:binds) { lua_pushcfunction(L,b.fn); lua_setglobal(L,b.name); }
    lua_getglobal(L,"setElementPosition"); lua_pushcclosure(L,SetElementPosition,1); lua_setglobal(L,"setElementPosition");
    lua_getglobal(L,"getElementsByType"); lua_pushcclosure(L,GetElementsByType,1); lua_setglobal(L,"getElementsByType");
}
}
