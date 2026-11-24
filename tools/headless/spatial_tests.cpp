#include "stubs/test_ped_queries.h"
#include "shared/SpatialWorld.h"
#include "shared/LuaVMStub.h"
#include "shared/LuaSpatialApi.h"
#include "shared/LuaApiExtra.h"
#include "lua.hpp"
#include "shared/ResourceFiles.h"
#include "shared/ResourceLoader.h"
#include "shared/ServerWorld.h"
#include "shared/PacketHandler.h"
#include "shared/MtaIds.h"
#include "gtasa/GameEngine.h"
#include <zlib.h>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
using namespace WorldRPG;
namespace S=WorldRPG::Spatial;
unsigned checks=0;
void Check(bool yes,const char* label) { ++checks; if(!yes) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); } }
void Lua(Script::LuaVMStub& vm,const std::string& script) {
    auto r=vm.executeBuffer(script.data(),script.size(),"spatial-test");
    if(!r.success) std::fprintf(stderr,"%s\n",r.errorMessage.c_str());
    Check(r.success,"Lua spatial behavior");
}
void Bits(Net::BitStreamWriter& w,uint32_t n,size_t count) {
    uint8_t raw[]={uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)}; w.WriteBits(raw,count);
}
void Position(Net::BitStreamWriter& w,float x,float y,float z) {
    Bits(w,uint32_t(int32_t(x*1024))&0xffffff,24); Bits(w,uint32_t(int32_t(y*1024))&0xffffff,24); w.Write(z);
}
void Header(Net::BitStreamWriter& w,uint32_t id) {
    w.WriteCompressed(1,4); Bits(w,id,17); w.Write(uint8_t(15)); Bits(w,0,17); w.Write(uint8_t(0)); w.WriteCompressed(0,2);
    Bits(w,0,1); Bits(w,1,1); w.WriteCompressed(0,2); w.WriteCompressed(0,2);
}
void Shape(uint32_t id,unsigned type,bool enabled=true,bool autoEvents=true) {
    Net::BitStreamWriter w; Header(w,id); Bits(w,type,3); Position(w,100,100,10); Bits(w,enabled,1); Bits(w,autoEvents,1);
    switch(type) {
        case 0:case 2:w.Write(1.f);break;
        case 1:Position(w,2,2,2);break;
        case 3:Bits(w,2*1024,24);Bits(w,2*1024,24);break;
        case 5:w.Write(1.f);w.Write(2.f);break;
        case 4:
            w.WriteCompressed(4,4);
            for(auto p:{S::Vec{99,99,0},S::Vec{101,99,0},S::Vec{101,101,0},S::Vec{99,101,0}}) {
                Bits(w,uint32_t(p.x*1024),24); Bits(w,uint32_t(p.y*1024),24);
            }
            w.Write(9.f);w.Write(11.f);break;
    }
    ServerWorld::OnEntityAdd(w.GetData(),w.GetByteLength(),w.GetBitLength());
    S::Element shape; Check(Script::CopySpatialShape(id,shape),"ENTITY_ADD retains colshape");
    Check(unsigned(shape.shape->type)==type && shape.shape->enabled==enabled && shape.shape->autoEvents==autoEvents,"wire shape flags/type");
}
template<class Write> bool Rpc(Sync::PacketHandler& h,uint8_t fn,uint32_t id,Write write) {
    Net::BitStreamWriter w; w.Write(MtaIds::Packet::LuaElementRpc);w.Write(fn);Bits(w,id,17);write(w);
    return h.handlePacket(99,w.GetData(),w.GetByteLength(),w.GetBitLength());
}
std::vector<uint8_t> ScriptPacket(uint16_t net,const std::string& source) {
    uLongf size=compressBound(source.size()); std::vector<uint8_t> packed(size);
    compress2(packed.data(),&size,reinterpret_cast<const Bytef*>(source.data()),source.size(),Z_DEFAULT_COMPRESSION);packed.resize(size);
    std::vector<uint8_t> out={uint8_t(net),uint8_t(net>>8),1,0};
    uint32_t n=uint32_t(size+4); for(unsigned i=0;i<4;++i) out.push_back(uint8_t(n>>(i*8)));
    n=source.size(); for(int i=3;i>=0;--i) out.push_back(uint8_t(n>>(i*8)));
    out.insert(out.end(),packed.begin(),packed.end()); return out;
}
void Resource(uint16_t net,uint32_t root,const char* name,const std::string& script) {
    Net::ResourceStartInfo info; info.name=name; info.resourceId=net; info.entityId=root; info.dynamicEntityId=root+1;
    info.noClientCacheScripts=1; info.startCounter=1;
    Script::ActiveVmOnResourceStart(info);
    auto data=ScriptPacket(net,script);
    Check(Script::ActiveVmFeedScripts(data.data(),data.size(),data.size()*8),"resource scripts accepted");
    Script::ActiveVmPulseResources();
}
void GeometryChecks() {
    S::Geometry g; g.radius=1;
    Check(S::Hit({},g,{1,0,0})&&!S::Hit({},g,{1.001f,0,0}),"sphere inclusive boundary");
    g.type=S::ShapeType::Circle; Check(S::Hit({},g,{0,0,1000}),"circle ignores Z");
    g.type=S::ShapeType::Cuboid;g.size={2,3,4};
    Check(S::Hit({},g,{2,3,4})&&!S::Hit({},g,{2.001f,3,4}),"cuboid corner and point-only collision");
    g.size.z=-1;Check(!S::Hit({},g,{}),"negative cuboid height is empty");
    g.type=S::ShapeType::Rectangle;Check(S::Hit({},g,{1,1,1000}),"rectangle ignores height");
    g.type=S::ShapeType::Tube;g.height=2;
    Check(S::Hit({},g,{1,0,2})&&!S::Hit({},g,{0,0,2.001f}),"tube radial and height boundaries");
    g.type=S::ShapeType::Polygon;g.points={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};g.floor=-1;g.ceiling=1;
    Check(S::Hit({},g,{0,0,1})&&!S::Hit({},g,{0,0,1.01f})&&!S::Hit({},g,{2,0,0}),"polygon points and absolute height");
    g.points.clear();Check(!S::Hit({},g,{}),"empty polygon");
    S::World world; S::Element e; e.id=1;e.generation=1;e.type="object";e.position={1,1,500};e.bounds={e.position,2};
    world.Sync({e});
    Check(world.Range({},.5f,{}, {}, {}).size()==1,"unfiltered range uses XY bounding boxes, including model radius");
    Check(world.Range({},.5f,std::string("object"),{},{}).empty(),"type argument adds exact 3D point filtering");
    Check(world.Range({},.5f,std::string(""),{},{}).size()==1,"empty type leaves broadphase query");
    Check(world.Range({},.5f,{},0,{}).empty(),"interior zero still enables exact 3D check");
    Check(world.Range({},-3,{}, {},{}).size()==1,"negative range radius uses absolute broadphase");
    Check(world.Range({},12001,{}, {},{}).empty(),"desktop spatial radius limit");
    Check(!S::ValidSphere({{12001,0,0},0})&&!S::ValidSphere({{},std::numeric_limits<float>::quiet_NaN()}),"invalid spatial bounds");
    S::Element shape;shape.id=2;shape.generation=1;shape.type="colshape";shape.shape=S::Geometry{};
    shape.shape->radius=10;shape.bounds=S::ShapeBounds({},*shape.shape);e.position={};e.bounds={e.position,0};e.type="player";
    world.Sync({shape,e}); unsigned events=0;world.Pulse([&](const auto& t){++events;Check(world.Within(t.entity,t.shape),"membership is committed before hit");});
    world.Pulse([&](auto){++events;});Check(events==1,"hit once while staying inside");
    world.Remove(1);Check(!world.Within(1,2),"destructor unlinks both sides without leave");
    ++e.generation;world.Sync({shape,e});world.Pulse([&](auto){++events;});Check(events==2,"reused ID can enter again");
    shape.shape->autoEvents=false;world.Clear();world.Sync({shape,e});events=0;world.Pulse([&](auto){++events;});
    Check(events==0&&world.Within(1,2),"auto-call false stores collision without hit event");
    e.position={20,0,0};world.Sync({shape,e});world.Pulse([&](const auto& t){++events;Check(!t.hit,"leave transition");});
    Check(events==1,"desktop leave still fires with auto-call false");
    world.Clear();e.type="object";e.position={};e.bounds={e.position,0};shape.shape->autoEvents=true;
    world.Sync({e});world.Pulse([](auto){});world.Sync({e,shape});world.Pulse([](auto){});
    Check(!world.Within(1,2),"new shape does not immediately refresh a stationary object");
    world.Refresh(2);world.Pulse([](auto){});Check(world.Within(1,2),"explicit shape refresh detects stationary objects");

}
void DriftVehicleChecks() {
    // World RPG ENTITY_ADD response to /driftcar supra, 2026-11-20.
    // Current header, baseline 24-bit X/Y, model 401 with custom handling.
    constexpr uint8_t packet[] = {
        0xf1, 0x16, 0x02, 0x01, 0x37, 0x00, 0x80, 0x30, 0x78, 0x60, 0x02, 0x01, 0x80, 0x0a, 0x00, 0x5d,
        0xc0, 0xa5, 0x70, 0x04, 0x82, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x22, 0x03, 0xa0, 0xe2, 0x9a,
        0xc4, 0xd0, 0x9a, 0xc4, 0xd1, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0x80, 0x00, 0x02,
        0x0a, 0x1a, 0x4a, 0x59, 0x01, 0x9a, 0xd2, 0xb1, 0x00, 0x90, 0x40, 0x00, 0x32, 0x11, 0x00, 0x30,
        0x16, 0x91, 0x40, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x33, 0x73, 0x33, 0x0f, 0x66, 0xa6,
        0x66, 0x6f, 0x92, 0xc5, 0x2b, 0x91, 0xcf, 0xd4, 0x94, 0x01, 0x40, 0x00, 0x25, 0x10, 0x40, 0x00,
        0x08, 0x10, 0x40, 0x00, 0x23, 0x10, 0xc0, 0x00, 0x10, 0x10, 0x73, 0x73, 0x03, 0x0f, 0xc0, 0x00,
        0x06, 0x88, 0x53, 0x53, 0x2b, 0x27, 0xf1, 0xf8, 0x5e, 0xa7, 0xc6, 0x66, 0x76, 0x67, 0xf3, 0x53,
        0x23, 0x27, 0xc0, 0x00, 0x00, 0x00, 0x05, 0x2b, 0x91, 0xe7, 0xd1, 0xf8, 0x5e, 0xb7, 0xa0, 0x00,
        0x00, 0x07, 0xf3, 0x53, 0x33, 0x27, 0xc0, 0x00, 0x00, 0x07, 0xe0, 0x20, 0x00, 0x00, 0x00, 0x20,
        0x00, 0x00, 0x17, 0x03, 0xd0, 0xa7, 0xc0, 0x00,
    };
    ServerWorld::ConfigureEntityAdd(true, true);
    ServerWorld::OnEntityAdd(packet, sizeof(packet), sizeof(packet) * 8);
    ServerWorld::ActorSpawn vehicle;
    Check(ServerWorld::CopyVisual(534, vehicle) && vehicle.model == 401 && vehicle.health == 1000.f &&
          vehicle.x == 368.f && vehicle.y == -2036.5f, "current ENTITY_ADD stores the Supra");
    Sync::PacketHandler packetHandler;
    ServerWorld::SetSyncTimeContext(534, 17);
    auto sendVehicleVelocity = [&](uint8_t context) {
        VehicleSync::UnoccupiedUpdate update;
        update.vehicleId = 534;
        update.timeContext = context;
        update.syncVelocity = true;
        update.engineOn = true;
        Net::BitStreamWriter w;
        w.Write(uint8_t(56));
        Check(VehicleSync::WriteUnoccupiedEntry(w, update), "encode packet 56 delta");
        return packetHandler.handlePacket(99, w.GetData(), w.GetByteLength(), w.GetBitLength());
    };
    Check(sendVehicleVelocity(17), "read the NPC vehicle delta");
    const auto observed = Headless::TakeUnoccupiedUpdate();
    Check(observed && observed->vehicleId == 534 && !observed->syncPosition && !observed->syncRotation &&
        observed->syncVelocity && observed->velocity.x == 0 && observed->engineOn, "vehicle stop and engine state reach game thread");
    Check(sendVehicleVelocity(18) && !Headless::TakeUnoccupiedUpdate(), "stale vehicle context is parsed and discarded");


    ServerWorld::ResetSession();
    ServerWorld::ConfigureEntityAdd(true, true);
    Net::BitStreamWriter baseline;
    for (size_t bit = 0; bit < sizeof(packet) * 8; ++bit) {
        // Strip call propagation and sync time context as a baseline server does.
        if (bit == 66 || (bit >= 79 && bit < 87)) continue;
        Bits(baseline, (packet[bit / 8] >> (7 - bit % 8)) & 1u, 1);
    }
    ServerWorld::OnEntityAdd(baseline.GetData(), baseline.GetByteLength(), baseline.GetBitLength());
    Check(ServerWorld::CopyVisual(534, vehicle) && vehicle.model == 401 && vehicle.x == 368.f,
          "standard join also accepts a server with baseline ENTITY_ADD headers");
    ServerWorld::ResetSession();
}

int main() {
    GeometryChecks();DriftVehicleChecks();ServerWorld::ResetSession();Script::ResourceLoader::Reset();ServerWorld::SetLocalPlayer(1);
    Game::ApplyServerSpawn(100.25f,100.25f,10); ServerWorld::NotePlayerSkin(2,7);ServerWorld::NoteElementPosition(2,100.5f,100,10);ServerWorld::NoteElementDimension(2,3);
    Script::ActiveVmSetResourceLua(1);  // the checks run in the 5.3 VM with the resource
    Script::LuaVMStub vm;Check(vm.initialize(),"VM initializes");
    Lua(vm,"assert(createColSphere(0,0,0,1)==false and isElementWithinColShape({}, {})==nil and getElementsWithinColShape({})==false)");
    Resource(41,4000,"spatial",R"LUA(
        _G.s=createColSphere(100,100,10,1)
        _G.b=createColCuboid(100,100,10,-1,-1,-1)
        assert(isElement(s) and getElementType(s)=='colshape')
        assert(getElementParent(s).id==4001 and #getElementsWithinColShape(s)==0)
        assert(isElementWithinColShape(localPlayer,s)==false)
        assert(setElementInterior(s,99) and setElementDimension(s,0))
        _G.hits=0;_G.leaves=0;_G.entityHits=0;_G.entityLeaves=0;_G.matching={}
        addEventHandler('onClientColShapeHit',s,function(e,matching)
            _G.hits=_G.hits+1;_G.matching[e.id]=matching
            _G.insideDuringHit=isElementWithinColShape(e,source)
        end)
        addEventHandler('onClientColShapeLeave',s,function(e,matching)
            _G.leaves=_G.leaves+1;_G.outsideDuringLeave=not isElementWithinColShape(e,source)
        end)
        addEventHandler('onClientElementColShapeHit',root,function(shape) if shape.id==s.id then _G.entityHits=_G.entityHits+1 end end)
        addEventHandler('onClientElementColShapeLeave',root,function(shape) if shape.id==s.id then _G.entityLeaves=_G.entityLeaves+1 end end)
        _G.started=true
    )LUA");
    Lua(vm,"assert(started and hits==0 and #getElementsByType('colshape')==2)");
    vm.pumpDx();
    Lua(vm,R"(
        assert(hits==2 and entityHits==2 and insideDuringHit and matching[1]==true and matching[2]==false)
        assert(#getElementsWithinColShape(s,'player')==2 and #getElementsWithinColShape(s,'ped')==0)
        assert(#getElementsWithinColShape(b)==0 and #getElementsByType('colshape',{id=4000},true)==2)
        assert(getElementInterior(s)==99 and getElementDimension(s)==0)
        assert(#getElementsWithinRange(100,100,10,1,'player',nil,3)==1)
        assert(#getElementsWithinRange(100,100,10,1,'player',nil,0)==1)
        assert(#getElementsWithinRange(100,100,1000,1)==4)
        assert(#getElementsWithinRange(100,100,1000,1,'player')==0)
        assert(getElementsWithinRange(0,0,0,0/0)==false and getElementsWithinRange(0,0,0,1,true)==false)
        assert(getElementsWithinRange(0,0,0,1,nil,-1)==false)
        assert(createColSphere(0,0,0,1/0)==false and createColCuboid(0,0,0,1,1,{})==false)
    )");
    Resource(43,6000,"spatial-api",R"LUA(
        local circle=createColCircle(10,20,-5)
        local x,y,z=getElementPosition(circle)
        assert(x==10 and y==20 and z==0 and getColShapeType(circle)==0)
        assert(math.abs(getColShapeRadius(circle)-0.1)<1e-6)
        assert(setColShapeRadius(circle,4) and getColShapeRadius(circle)==4)
        assert(isInsideColShape(circle,10,20,500)==true)
        assert(isInsideColShape(circle,15,20,0)==false)
        assert(setColShapeRadius(circle,-2) and getColShapeRadius(circle)==0)
        local rect=createColRectangle(0,0,-1,2)
        local sx,sy=getColShapeSize(rect)
        assert(getColShapeType(rect)==3 and math.abs(sx-0.1)<1e-6 and sy==2)
        assert(setColShapeSize(rect,3,-4))
        sx,sy=getColShapeSize(rect)
        assert(sx==3 and sy==0 and isInsideColShape(rect,1,0,999)==true and isInsideColShape(rect,1,1,0)==false)
        local tube=createColTube(0,0,5,2,-3)
        assert(getColShapeType(tube)==5 and getColShapeRadius(tube)==2 and math.abs(getColShapeSize(tube)-0.1)<1e-6)
        assert(isInsideColShape(tube,0,0,5.05)==true and isInsideColShape(tube,0,0,5.2)==false)
        assert(setColShapeSize(tube,-1) and getColShapeSize(tube)==0)
        local box=createColCuboid(0,0,0,2,3,-4)
        local bx,by,bz=getColShapeSize(box)
        assert(getColShapeType(box)==1 and bx==2 and by==3 and bz==-4)
        assert(setColShapeSize(box,-5,2,-3))
        bx,by,bz=getColShapeSize(box)
        assert(bx==0 and by==2 and bz==0)
        local poly=createColPolygon(0,0,-1,-1,1,-1,1,1,-1,1)
        assert(getColShapeType(poly)==4 and isInsideColShape(poly,0,0,0)==true and isInsideColShape(poly,3,0,0)==false)
        local pts=getColPolygonPoints(poly)
        assert(#pts==4 and pts[1][1]==-1 and pts[1][2]==-1)
        local px,py=getColPolygonPointPosition(poly,2)
        assert(px==1 and py==-1 and setColPolygonPointPosition(poly,2,2,-1))
        px,py=getColPolygonPointPosition(poly,2)
        assert(px==2 and py==-1 and addColPolygonPoint(poly,0,2) and #getColPolygonPoints(poly)==5)
        assert(addColPolygonPoint(poly,-2,-2,1) and getColPolygonPointPosition(poly,1)==-2)
        assert(removeColPolygonPoint(poly,1) and #getColPolygonPoints(poly)==5)
        assert(getColPolygonPointPosition(poly,99)==false and removeColPolygonPoint(poly,0)==false)
        assert(not pcall(getColPolygonPointPosition, poly, 0/0) and not pcall(getColPolygonPointPosition, poly, math.huge))
        assert(createColRectangle(0, 0, 1e100, 2) == false)
        while #getColPolygonPoints(poly)>3 do assert(removeColPolygonPoint(poly,1)) end
        assert(removeColPolygonPoint(poly,1)==false)
        local tall=createColPolygon(0,0,-1,-1,1,-1,1,1,-1,1)
        local floor,ceil=getColPolygonHeight(tall)
        assert(floor<-1e37 and ceil>1e37 and setColPolygonHeight(tall,2,1)==true)
        floor,ceil=getColPolygonHeight(tall)
        assert(floor==1 and ceil==2 and setColPolygonHeight(tall,1,2)==false)
        assert(isInsideColShape(tall,0,0,1.5)==true and isInsideColShape(tall,0,0,3)==false)
        assert(setColPolygonHeight(tall,false,true)==true)
        local moved=createColPolygon(0,0,-1,-1,1,-1,1,1)
        assert(setElementPosition(moved,10,5,0))
        local mx,my=getColPolygonPointPosition(moved,1)
        assert(mx==9 and my==4 and isInsideColShape(moved,10.9,4.1,0)==true and isInsideColShape(moved,0,0,0)==false)
        local near=createColSphere(100,100,10,0.01)
        assert(isElementWithinColShape(localPlayer,near)==false and setColShapeRadius(near,5))
        assert(isElementWithinColShape(localPlayer,near)==true)
        assert(showCol(true)==false and isShowCollisionsEnabled()==false and setDevelopmentMode(true))
        assert(showCol(true) and isShowCollisionsEnabled())
        assert(showCol(false)==true and isShowCollisionsEnabled()==false and setDevelopmentMode(false))
        assert(createColPolygon(0,0,1,1)==false and getColShapeType(nil)==false and isInsideColShape('nope',0,0,0)==false)
        local ok,err=pcall(getColShapeRadius,rect)
        assert(not ok and string.find(err,'Circle, Sphere or Tube',1,true))
        ok,err=pcall(showCol,1)
        assert(not ok and string.find(err,'boolean',1,true))
        ok,err=pcall(getColPolygonHeight,circle)
        assert(not ok and string.find(err,'colshape',1,true))
        local marker=createMarker(1,2,3)
        local mx,my,mz=getElementPosition(marker)
        assert(isElement(marker) and getElementType(marker)=='marker' and mx==1 and my==2 and mz==3)
        assert(getMarkerType(marker)=='checkpoint' and getMarkerSize(marker)==4 and getMarkerIcon(marker)=='none')
        assert(getMarkerTarget(marker)==false and getMarkerTargetArrowProperties(marker)==false)
        local mr,mg,mb,ma=getMarkerColor(marker)
        assert(mr==0 and mg==0 and mb==255 and ma==255)
        assert(setMarkerColor(marker,10,20,30,40))
        mr,mg,mb,ma=getMarkerColor(marker)
        assert(mr==10 and mg==20 and mb==30 and ma==255)
        assert(setMarkerTarget(marker,4,5,6) and getMarkerIcon(marker)=='arrow')
        local tx,ty,tz=getMarkerTarget(marker)
        assert(tx==4 and ty==5 and tz==6)
        local ar,ag,ab,aa,as=getMarkerTargetArrowProperties(marker)
        assert(ar==255 and ag==64 and ab==64 and aa==255 and as==2.5)
        assert(setMarkerTargetArrowProperties(marker,1,2,3,4,8))
        ar,ag,ab,aa,as=getMarkerTargetArrowProperties(marker)
        assert(ar==1 and ag==2 and ab==3 and aa==4 and as==8)
        assert(setMarkerSize(marker,8))
        assert(select(5,getMarkerTargetArrowProperties(marker))==5)
        assert(setMarkerType(marker,'ring') and getMarkerType(marker)=='ring' and getMarkerTarget(marker)==4)
        assert(getMarkerTargetArrowProperties(marker)==false)
        assert(setMarkerType(marker,'corona') and getMarkerIcon(marker)==false)
        assert(isCoronaReflectionEnabled(marker)==false and setCoronaReflectionEnabled(marker,true))
        assert(isCoronaReflectionEnabled(marker)==true)
        assert(setMarkerType(marker,'cylinder') and isCoronaReflectionEnabled(marker)==false)
        assert(setMarkerIcon(marker,'arrow')==false)
        assert(createMarker(0,0,0,'nope')==false)
        assert(setMarkerType(localPlayer,'corona')==true and setMarkerSize(localPlayer,1)==false)
        assert(setElementPosition(marker,8,8,8))
        mx,my,mz=getElementPosition(marker)
        assert(mx==8 and my==8 and mz==8)
        local arrow=createMarker(0,0,0,'arrow',2,9,9,9,9,true)
        mr,mg,mb,ma=getMarkerColor(arrow)
        assert(getMarkerType(arrow)=='arrow' and ma==9)
        local before=getMarkerCount()
        local doomed=createMarker(0,0,0,'cylinder')
        assert(getMarkerCount()==before+1 and destroyElement(doomed) and getMarkerCount()==before)
        local stats=getMarkerLimitStats()
        assert(stats.streamedMarkerLimit==4096 and stats.marker3DLimit==4096 and stats.checkpointLimit==4096)
        assert(stats.directionArrowLimit==4096 and stats.streamedMarkers==0 and stats.allocated3DMarkers==0)
        assert(stats.marker3DProcessLimit==0 and stats.activeCheckpoints==0 and stats.checkpointProcessLimit==0)
        assert(renderScriptImportantArea(0,0,0,1,1)==false and renderScriptImportantArea(0,0,0,-1,1)==false)
        local blip=createBlip(9,8,7)
        local br,bg,bb,ba=getBlipColor(blip)
        assert(getElementType(blip)=='blip' and getBlipIcon(blip)==0 and getBlipSize(blip)==2)
        assert(br==255 and bg==0 and bb==0 and ba==255 and getBlipOrdering(blip)==0 and getBlipVisibleDistance(blip)==16383)
        local bx,by,bz=getElementPosition(blip)
        assert(bx==9 and by==8 and bz==7)
        assert(setBlipIcon(blip,63) and getBlipIcon(blip)==63 and setBlipIcon(blip,64)==false)
        assert(setBlipSize(blip,30) and getBlipSize(blip)==25)
        assert(setBlipColor(blip,1,2,3,4))
        br,bg,bb,ba=getBlipColor(blip)
        assert(br==1 and bg==2 and bb==3 and ba==4)
        assert(setBlipOrdering(blip,-5) and getBlipOrdering(blip)==-5)
        assert(setBlipVisibleDistance(blip,100) and getBlipVisibleDistance(blip)==100)
        local attached=createBlipAttachedTo(localPlayer,1,3)
        assert(getBlipIcon(attached)==1 and getBlipSize(attached)==3)
        local ax,ay,az=getElementPosition(attached)
        local px,py,pz=getElementPosition(localPlayer)
        assert(ax==px and ay==py and az==pz)
        assert(createBlip(0,0,0,64)==false)
        local area=createRadarArea(10,20,-4,6)
        local aw,ah=getRadarAreaSize(area)
        local ar,ag,ab,aa=getRadarAreaColor(area)
        assert(getElementType(area)=='radararea' and aw==-4 and ah==6 and ar==255 and ag==0 and ab==0 and aa==255)
        assert(isRadarAreaFlashing(area)==false and isInsideRadarArea(area,10,20)==true)
        assert(isInsideRadarArea(area,8,22)==true and isInsideRadarArea(area,5,22)==false)
        assert(setRadarAreaFlashing(area,true) and isRadarAreaFlashing(area))
        assert(setRadarAreaColor(area,1,2,3) )
        ar,ag,ab,aa=getRadarAreaColor(area)
        assert(ar==1 and ag==2 and ab==3 and aa==255)
        assert(setRadarAreaSize(area,2,-2) and isInsideRadarArea(area,11,19)==true and isInsideRadarArea(area,13,19)==false)
        assert(setElementPosition(area,0,0,9))
        local ax,ay,az=getElementPosition(area)
        assert(ax==0 and ay==0 and az==0)
        local health=createPickup(1,2,3,0,75)
        assert(getElementType(health)=='pickup' and getPickupType(health)==0 and getPickupAmount(health)==75)
        assert(getPickupWeapon(health)==0 and getPickupAmmo(health)==0)
        assert(createPickup(0,0,0,0,101)==false and createPickup(0,0,0,2,19,30000,10)==false)
        local gun=createPickup(0,0,0,2,22,30000,10000)
        assert(getPickupType(gun)==2 and getPickupWeapon(gun)==22 and getPickupAmmo(gun)==9999 and getPickupAmount(gun)==0)
        assert(setPickupType(gun,1,40) and getPickupType(gun)==1 and getPickupAmount(gun)==40 and getPickupWeapon(gun)==22)
        assert(setPickupType(localPlayer,0,10)==false)
        local box=createPickup(0,0,0,3,1337)
        assert(isElement(box) and getPickupType(box)==3)
        assert(createPickup(0,0,0,3,0)==false and createPickup(0,0,0,3,400)==false)
        local lamp=createLight(1,2,3,4)
        local lr,lg,lb=getLightColor(lamp)
        local ldx,ldy,ldz=getLightDirection(lamp)
        assert(getElementType(lamp)=='light' and getLightType(lamp)==1 and getLightRadius(lamp)==3)
        assert(lr==255 and lg==0 and lb==0 and ldx==0 and ldy==0 and ldz==0)
        assert(setLightRadius(lamp,8) and setLightColor(lamp,9,8,7) and setLightDirection(lamp,1,0,0))
        lr,lg,lb=getLightColor(lamp)
        ldx,ldy,ldz=getLightDirection(lamp)
        assert(getLightRadius(lamp)==8 and lr==9 and lg==8 and lb==7 and ldx==1 and ldy==0 and ldz==0)
        assert(createLight(1,0,0)==false)
        local spot=createSearchLight(1,2,3,4,5,6,0.5,1.5)
        local ssx,ssy,ssz=getSearchLightStartPosition(spot)
        local sex,sey,sez=getSearchLightEndPosition(spot)
        local scr,scg,scb,sca=getSearchLightColor(spot)
        assert(getElementType(spot)=='searchlight' and ssx==1 and ssy==2 and ssz==3 and sex==4 and sey==5 and sez==6)
        assert(getSearchLightStartRadius(spot)==0.5 and getSearchLightEndRadius(spot)==1.5)
        assert(scr==200 and scg==200 and scb==255 and sca==0)
        assert(setSearchLightStartPosition(spot,0,0,1) and setSearchLightEndRadius(spot,3))
        assert(getSearchLightStartPosition(spot)==0 and getSearchLightEndRadius(spot)==3)
        setSearchLightColor(spot,tocolor(1,2,3,4))
        scr,scg,scb,sca=getSearchLightColor(spot)
        assert(scr==1 and scg==2 and scb==3 and sca==4)
        assert(setElementPosition(spot,9,9,9)==true)
        local spx,spy,spz=getElementPosition(spot)
        assert(spx==0 and spy==0 and spz==1)
        local bird=createBird(0,0,10)
        local vel=getBirdVelocity(bird)
        assert(getElementType(bird)=='bird' and math.abs(getBirdSize(bird)-0.55)<1e-5 and math.abs(vel.y-5.3)<1e-5 and getBirdWingBeatTime(bird)==400)
        local br,bg,bb,wr,wg,wb=getBirdColors(bird)
        assert(br==210 and bg==210 and bb==210 and wr==235 and wg==235 and wb==235)
        assert(isBirdShootable(bird) and isBirdMovementEnabled(bird) and isBirdCurvedFlightEnabled(bird)==false)
        assert(setBirdSize(bird,2) and setBirdSize(bird,21)==false and getBirdSize(bird)==2)
        assert(setBirdWingBeatTime(bird,0)==false and setBirdWingBeatTime(bird,500) and getBirdWingBeatTime(bird)==500)
        assert(setBirdColors(bird,-10,300,1,0,0,0))
        br,bg,bb=getBirdColors(bird)
        assert(br==0 and bg==255 and bb==1)
        local water=createBird(0,0,0,{preset='water',speed=2})
        assert(getBirdSize(water)==1 and getBirdVelocity(water).y==2 and getBirdRenderDistance(water)==90)
        assert(processBirdGunShot(0,0,0,0,0,0)==false)
        addEventHandler('onClientBirdShot',bird,function() cancelEvent() end)
        assert(processBirdGunShot(0,0,0,0,0,20,24) and isElement(bird))
        removeEventHandler('onClientBirdShot',bird,function() end)
        destroyElement(bird)
        local doomed=createBird(0,0,10)
        assert(processBirdGunShot(0,0,0,0,0,20) and not isElement(doomed))
        assert(createBird(0,0,0,{size=0})==false)
        local rope=createRope(1,2,3)
        assert(getElementType(rope)=='rope' and getRopeType(rope)=='swat')
        assert(getRopeDuration(rope)==0 and getRopeRemainingTime(rope)==0 and getRopeWinchHeight(rope)==0.5)
        assert(getRopeLength(rope)==0 and getRopeFixedNode(rope)==0 and isRopeOnGround(rope)==false and isRopePhysicsEnabled(rope))
        assert(getRopeHolder(rope)==false and getRopeCarriedElement(rope)==false and isRopeActive(rope)==false)
        assert(getRopePositionAt(rope,0.5)==false)
        local obj=createObject(1337,0,0,0)
        assert(setRopeHolder(rope,obj) and getRopeHolder(rope).id==obj.id)
        assert(setRopeHolder(rope,localPlayer)==false)
        assert(attachElementToRope(rope,obj) and getRopeCarriedElement(rope).id==obj.id)
        assert(detachElementFromRope(rope) and getRopeCarriedElement(rope)==false)
        assert(setRopeType(rope,'harness') and getRopeType(rope)=='harness' and setRopeType(rope,'nope')==false)
        assert(setRopeDuration(rope,1000) and getRopeDuration(rope)==1000 and getRopeRemainingTime(rope)==1000)
        assert(setRopeLength(rope,4) and getRopeLength(rope)==4 and setRopeFixedNode(rope,31)==false and setRopeFixedNode(rope,3))
        assert(getRopeFixedNode(rope)==3 and setRopeWinchHeight(rope,0)==false and setRopeOnGround(rope,true) and isRopeOnGround(rope))
        assert(createRope(0,0)==false and createRope(0,0,0,{type='nope'})==false)
        local short=createRope(0,0,1,{type='swat',duration=5,length=-2,winchHeight=1})
        assert(getRopeLength(short)==0 and getRopeDuration(short)==5)
        local off=getRopeHolderOffset(rope)
        assert(setRopeHolderOffset(rope,1,2,3) and getRopeHolderOffset(rope).x==1 and getRopeTopVelocity(rope).x==0)
        local bad, why = createNativeUI('nope', {})
        assert(bad==false and why=='invalid-kind-or-arguments')
        bad, why = createNativeUI('text', {foo=1})
        assert(bad==false and why=='invalid-option:foo')
        bad, why = createNativeUI('text', {content='Hello'})
        assert(bad==false and why=='invalid-text')
        bad, why = createNativeUI('menu', {})
        assert(bad==false and why=='invalid-rows')
        local ui = createNativeUI('text', {name='greet', content='Hello', x=10})
        assert(type(ui)=='number' and ui>=1)
        bad, why = createNativeUI('text', {name='greet', content='Again'})
        assert(bad==false and why=='duplicate-name')
        local state = getNativeUIState(ui)
        assert(state.kind=='text' and state.available==true and state.value==0 and state.selected==0)
        assert(updateNativeUI(ui, {content='Bye'})==true)
        local shown, serr = showNativeText(ui, 'nope', 1000)
        assert(shown==false and serr=='invalid-channel')
        assert(showNativeText(ui, 'objective', 1000)==true)
        bad, why = clearNativeText('nope')
        assert(bad==false and why=='invalid-channel' and clearNativeText('objective')==true)
        bad, why = destroyNativeUI(999999)
        assert(bad==false and why=='invalid-handle')
        assert(destroyNativeUI(ui)==true)
        bad, why = getNativeUIState(ui)
        assert(bad==false and why=='invalid-handle-or-arguments')
        assert(releaseNativeUI()==true)
        local okm,errm=pcall(getMarkerTargetArrowProperties,localPlayer)
        assert(not okm and string.find(errm,'marker',1,true))
        _G.apiOk=true
    )LUA");
    Lua(vm,"assert(apiOk)");
    vm.pumpDx();Lua(vm,"assert(hits==2 and leaves==0)");
    Game::ApplyServerSpawn(110,110,10);vm.pumpDx();Lua(vm,"assert(leaves==1 and entityLeaves==1 and outsideDuringLeave)");
    Lua(vm,"assert(setElementPosition(s,120,120,10))");Lua(vm,"assert(leaves==2 and entityLeaves==2 and #getElementsWithinColShape(s)==0)");
    Lua(vm,"local x,y,z=getElementPosition(s);assert(x==120 and y==120 and z==10)");
    for(unsigned type=0;type<6;++type) Shape(900+type,type);
    {
        Net::BitStreamWriter w;
        w.WriteCompressed(1,4); Bits(w,920,17); w.Write(uint8_t(13)); Bits(w,0,17); w.Write(uint8_t(0)); w.WriteCompressed(0,2);
        Bits(w,0,1); Bits(w,1,1); w.WriteCompressed(0,2); w.WriteCompressed(0,2);
        w.WriteCompressed(5,2); for (char ch : {'A','l','p','h','a'}) w.Write(uint8_t(ch));
        w.Write(uint8_t(1)); w.Write(uint8_t(2)); w.Write(uint8_t(3)); Bits(w,1,1); w.Write(uint32_t(2));
        Bits(w,1,17); Bits(w,2,17);
        ServerWorld::OnEntityAdd(w.GetData(), w.GetByteLength(), w.GetBitLength());
    }
    Lua(vm,R"LUA(
        assert(getTeamName({id=920})=='Alpha' and getTeamFromName('Alpha').id==920 and getTeamFromName('alpha')==false)
        local r,g,b=getTeamColor({id=920})
        assert(r==1 and g==2 and b==3 and getTeamFriendlyFire({id=920})==true and countPlayersInTeam({id=920})==2)
        assert(#getPlayersInTeam({id=920})==2 and getPlayersInTeam({id=920})[1].id==1)
        assert(getTeamName(localPlayer)==false)
    )LUA");
    Lua(vm,R"LUA(
        assert(getColShapeType({id=900})==0 and getColShapeRadius({id=900})==1)
        assert(getColShapeType({id=901})==1)
        local x,y,z=getColShapeSize({id=901})
        assert(x==2 and y==2 and z==2 and getColShapeRadius({id=902})==1 and getColShapeType({id=902})==2)
        local rx,ry=getColShapeSize({id=903})
        assert(getColShapeType({id=903})==3 and rx==2 and ry==2)
        local pts=getColPolygonPoints({id=904})
        local floor,ceil=getColPolygonHeight({id=904})
        assert(getColShapeType({id=904})==4 and #pts==4 and pts[1][1]==99 and pts[1][2]==99 and floor==9 and ceil==11)
        assert(getColShapeType({id=905})==5 and getColShapeRadius({id=905})==1 and getColShapeSize({id=905})==2)
        assert(setColShapeRadius({id=900},50) and getColShapeRadius({id=900})==50)
        assert(setColShapeRadius({id=900},1) and getColShapeRadius({id=900})==1)
    )LUA");
    Game::ApplyServerSpawn(100.25f,100.25f,10);vm.pumpDx();
    Lua(vm,"for id=900,905 do assert(isElementWithinColShape(localPlayer,{id=id})==true,'shape '..id) end");
    Sync::PacketHandler handler;
    Check(Rpc(handler,MtaIds::Rpc::SetColshapeRadius,902,[](auto& w){w.Write(.1f);}),"radius RPC");
    vm.pumpDx();Lua(vm,"assert(isElementWithinColShape(localPlayer,{id=902})==false)");
    Check(!Rpc(handler,MtaIds::Rpc::SetColshapeRadius,902,[](auto&){}),"truncated radius RPC is rejected");
    Check(Rpc(handler,MtaIds::Rpc::SetColshapeSize,901,[](auto& w){w.Write(-1.f);w.Write(-1.f);w.Write(-1.f);}),"size RPC");
    Check(Rpc(handler,MtaIds::Rpc::SetColshapeSize,905,[](auto& w){w.Write(.1f);w.Write(999.f);w.Write(999.f);}),"tube size uses X for height");
    Check(Rpc(handler,MtaIds::Rpc::SetColPolygonHeight,904,[](auto& w){w.Write(20.f);w.Write(21.f);}),"polygon height RPC");
    vm.pumpDx();Lua(vm,"assert(not isElementWithinColShape(localPlayer,{id=901}) and not isElementWithinColShape(localPlayer,{id=904}))");
    Check(Rpc(handler,MtaIds::Rpc::SetElementPosition,900,[](auto& w){w.Write(120.f);w.Write(120.f);w.Write(0.f);w.Write(uint8_t(0));}),"colshape position RPC");
    vm.pumpDx();Lua(vm,"assert(not isElementWithinColShape(localPlayer,{id=900}))");
    Check(Rpc(handler,MtaIds::Rpc::RemoveColPolygonPoint,904,[](auto& w){w.Write(uint32_t(0));}),"remove polygon point RPC");
    S::Element polygon;Script::CopySpatialShape(904,polygon);Check(polygon.shape->points.size()==3,"polygon has three remaining points");
    auto revision=polygon.shapeRevision;
    Check(Rpc(handler,MtaIds::Rpc::RemoveColPolygonPoint,904,[](auto& w){w.Write(uint32_t(0));}),"minimum-size removal is consumed");
    Script::CopySpatialShape(904,polygon);Check(polygon.shape->points.size()==3 && polygon.shapeRevision==revision,"minimum three points and no false refresh");
    auto point=[](auto& w){Bits(w,98*1024,24);Bits(w,98*1024,24);};
    Check(Rpc(handler,MtaIds::Rpc::AddColPolygonPoint,904,point),"append polygon point RPC without index");
    Check(Rpc(handler,MtaIds::Rpc::AddColPolygonPoint,904,[&](auto& w){point(w);w.Write(uint32_t(1));}),"insert polygon point RPC with index");
    Check(Rpc(handler,MtaIds::Rpc::UpdateColPolygonPoint,904,[&](auto& w){point(w);w.Write(uint32_t(0));}),"update polygon point RPC");
    Script::CopySpatialShape(904,polygon);Check(polygon.shape->points.size()==5 && polygon.shape->points[0].x==98,"polygon mutation payload order");
    Check(!Rpc(handler,MtaIds::Rpc::UpdateColPolygonPoint,904,point),"update without index is malformed");
    Check(Rpc(handler,MtaIds::Rpc::SetElementInterior,903,[](auto& w){w.Write(uint8_t(7));w.Write(uint8_t(1));w.Write(200.f);w.Write(200.f);w.Write(0.f);}),"colshape interior and position RPC");
    Lua(vm,"assert(getElementInterior({id=903})==7 and getElementPosition({id=903})==200)");
    Lua(vm,"assert(setElementInterior(localPlayer,4) and #getElementsWithinRange(100,100,10,1,'player',4,0)==1)");
    Check(Rpc(handler,MtaIds::Rpc::SetElementInterior,1,[](auto& w){w.Write(uint8_t(7));w.Write(uint8_t(0));}),"player interior RPC supersedes Lua value");
    Lua(vm,"assert(getElementInterior(localPlayer)==7 and #getElementsWithinRange(100,100,10,1,'player',7,0)==1)");
    // Removal/reuse must not carry collision history into a fresh shape.
    Net::BitStreamWriter remove;Bits(remove,903,17);ServerWorld::OnEntityRemove(remove.GetData(),remove.GetByteLength(),remove.GetBitLength());
    Lua(vm,"assert(isElementWithinColShape(localPlayer,{id=903})==nil)");Shape(903,3);
    Lua(vm,"assert(isElementWithinColShape(localPlayer,{id=903})==false)");vm.pumpDx();Lua(vm,"assert(isElementWithinColShape(localPlayer,{id=903})==true)");
    Shape(910,2,false);vm.pumpDx();Lua(vm,"assert(isElementWithinColShape(localPlayer,{id=910})==false)");
    Resource(42,5000,"spatial-other",R"LUA(
        _G.otherShape=createColSphere(100,100,10,2)
        _G.movingShape=createColSphere(100,100,10,2);_G.order={}
        addEventHandler('onClientColShapeHit',movingShape,function()
            table.insert(_G.order,'hitShape');setElementPosition(source,300,300,10)
        end)
        addEventHandler('onClientColShapeLeave',movingShape,function() table.insert(_G.order,'leaveShape') end)
        addEventHandler('onClientElementColShapeHit',root,function(shape) if shape.id==movingShape.id then table.insert(_G.order,'hitElement') end end)
        addEventHandler('onClientElementColShapeLeave',root,function(shape) if shape.id==movingShape.id then table.insert(_G.order,'leaveElement') end end)
        _G.doomed=createColSphere(100,100,10,2);_G.doomedElementHits=0
        addEventHandler('onClientColShapeHit',doomed,function() destroyElement(doomed) end)
        addEventHandler('onClientElementColShapeHit',root,function(shape) if shape.id==doomed.id then _G.doomedElementHits=_G.doomedElementHits+1 end end)
    )LUA");
    vm.pumpDx();Lua(vm,"assert(not isElement(doomed) and doomedElementHits==0 and isElement(otherShape))");
    Lua(vm,"assert(table.concat(order,',')=='hitShape,leaveShape,leaveElement,hitElement')");
    Lua(vm,"assert(setElementParent(s,{id=5001}))");
    Net::BitStreamWriter stop;stop.Write(uint16_t(41));Script::ActiveVmOnResourceStop(stop.GetData(),stop.GetByteLength(),stop.GetBitLength());
    Lua(vm,"assert(not isElement(s) and not isElement(b) and isElement(otherShape) and leaves==2)");
    ServerWorld::NoteVehicle(9000, 400);
    ServerWorld::NotePlayerSkin(50, 7);
    Resource(44, 7000, "element-tree", R"LUA(
        local dummy = createElement('coin')
        local obj = createObject(1337, 0, 0, 0)
        local low = createObject(1337, 1, 0, 0, 0, 0, 0, true)
        local child = createObject(1337, 2, 0, 0)
        assert(obj and low and child and isElementLowLOD(obj) == false and isElementLowLOD(low) == true, 'object lod flag')
        assert(getLowLODElement(obj) == nil and getLowLODElement(low) == nil, 'unlinked lod is nil')
        assert(setLowLODElement(obj, low) == true and getLowLODElement(obj).id == low.id, 'object lod link')
        assert(setLowLODElement(low, obj) == false and setLowLODElement(obj, obj) == false, 'lod target must be low')
        assert(setLowLODElement(obj) == true and getLowLODElement(obj) == nil, 'clear object lod')
        assert(setLowLODElement(obj) == false, 'clearing an empty object link fails')
        local building = createBuilding(1337, 0, 0, 0)
        local lodBuilding = createBuilding(1338, 3, 0, 0)
        assert(isElementLowLOD(building) == false and setLowLODElement(building, lodBuilding) == true, 'building lod')
        assert(isElementLowLOD(lodBuilding) == true and getLowLODElement(building).id == lodBuilding.id, 'building back link')
        assert(setLowLODElement(building, obj) == false, 'lod types must match')
        assert(setLowLODElement(building) == true and setLowLODElement(building) == true, 'building clear stays true')
        local bad, err = pcall(setLowLODElement, 'nope')
        assert(not bad and string.find(err, 'element', 1, true), 'setLowLODElement raises')
        assert(setElementParent(child, obj) == true, 'parent for propagation')
        assert(setElementVelocity(obj, 1, 2, 3) == true, 'object velocity')
        local vx, vy, vz = getElementVelocity(child)
        assert(vx == 1 and vy == 2 and vz == 3, 'velocity reaches the child')
        vx, vy, vz = getElementVelocity(obj)
        assert(vx == 1 and vy == 2 and vz == 3, 'stored velocity')
        assert(setElementVelocity(dummy) == false and setElementAngularVelocity(dummy, 1, 0, 0) == false, 'dummy motion')
        assert(setElementAngularVelocity(obj, 4, 5, 6) == true, 'turn')
        local tx, ty, tz = getElementAngularVelocity(obj)
        assert(tx == 4 and ty == 5 and tz == 6 and getElementAngularVelocity(dummy) == false, 'turn read')
        local vehicle = {id = 9000, element = true}
        assert(setElementVelocity(vehicle, 9, 8, 7) == true, 'vehicle velocity')
        assert(setElementAngularVelocity(vehicle, 1, 1, 1) == true, 'vehicle turn')
        assert(setVehicleFrozen(vehicle, true) == true, 'freeze')
        vx, vy, vz = getElementVelocity(vehicle)
        assert(vx == 0 and vy == 0 and vz == 0, 'frozen move speed is zero')
        assert(setElementVelocity(vehicle, 4, 4, 4) == true, 'frozen move write is accepted')
        assert(setElementAngularVelocity(vehicle, 3, 3, 3) == true, 'frozen turn write is accepted')
        tx, ty, tz = getElementAngularVelocity(vehicle)
        assert(tx == 1 and ty == 1 and tz == 1, 'frozen turn keeps the stored speed')
        assert(setVehicleFrozen(vehicle, false) == true, 'thaw')
        vx, vy, vz = getElementVelocity(vehicle)
        assert(vx == 9 and vy == 8 and vz == 7, 'thaw restores the stored move speed')
        assert(getElementBoundingBox(obj) == false and getElementRadius(obj) == false, 'no model bounds')
        assert(getElementBoundingBox(building) == false and getElementRadius(building) == false, 'building radius is not queried')
        assert(getElementDistanceFromCentreOfMassToBaseOfModel(obj) == 0, 'unstreamed centre distance')
        assert(getElementDistanceFromCentreOfMassToBaseOfModel(dummy) == false, 'dummy centre distance')
        assert(isElementOnScreen(obj) == false and isElementOnScreen('x') == nil, 'on screen')
        assert(isElementWaitingForGroundToLoad(localPlayer) == false and isElementWaitingForGroundToLoad(dummy) == false, 'ground')
        local marker = createMarker(100.25, 100.25, 10)
        local shape = getElementColShape(marker)
        assert(isElement(shape) and getElementType(shape) == 'colshape', 'marker collision')
        assert(destroyElement(shape) == false and isElement(shape), 'marker collision is not script-destructible')
        assert(isElementWithinMarker(localPlayer, marker) == true, 'player inside marker')
        assert(isElementWithinMarker(localPlayer, obj) == nil and getElementColShape(obj) == false, 'colshape query')
        local matrix = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {5, 6, 7, 1}}
        assert(setElementMatrix(obj, matrix) == true, 'matrix')
        local read = getElementMatrix(obj)
        assert(read[1][1] == 1 and read[4][1] == 5 and read[4][2] == 6 and read[4][3] == 7 and read[4][4] == 1, 'matrix read')
        local px, py, pz = getElementPosition(obj)
        assert(px == 5 and py == 6 and pz == 7, 'matrix position')
        assert(setElementMatrix(obj, {}) == false, 'bad matrix')
        assert(setElementAttachedOffsets(obj, 1, 2, 3, 4, 5, 6) == true, 'offsets')
        local ox, oy, oz, rx, ry, rz = getElementAttachedOffsets(obj)
        assert(ox == 1 and oy == 2 and oz == 3 and rx == 4 and ry == 5 and rz == 6, 'offset read')
        local ped = createPed(7, 0, 0, 3)
        assert(setElementOnFire(ped, true) == true and isElementOnFire(ped) == true, 'ped fire')
        assert(setElementOnFire(ped, false) == true and isElementOnFire(ped) == false, 'ped fire clear')
        local remote = {id = 50, element = true}
        assert(setElementOnFire(remote, true) == false and isElementOnFire(remote) == false, 'remote player fire')
        assert(setElementOnFire(obj, true) == false and isElementOnFire(obj) == false, 'object fire')
        bad, err = pcall(setElementOnFire, ped, 1)
        assert(not bad and string.find(err, 'boolean', 1, true), 'fire bool')
        bad, err = pcall(isElementOnFire, 'nope')
        assert(not bad and string.find(err, 'element', 1, true), 'fire element')
        local token = acquireElementStreamingLease(obj)
        assert(type(token) == 'number' and token > 0, 'lease')
        assert(releaseElementStreamingLease(token) == true and releaseElementStreamingLease(token) == false, 'lease release')
        assert(acquireElementStreamingLease(dummy) == false, 'dummy is not streamable')
        assert(acquireElementStreamingLease(localPlayer) ~= false, 'player lease')
        _G.elementTreeOk = true
    )LUA");
    Lua(vm, "assert(elementTreeOk, 'element tree script')");
    Check(Rpc(handler, MtaIds::Rpc::SetElementPosition, 1, [](auto& w) {
        w.Write(2229.5f); w.Write(-1722.f); w.Write(14.5f); w.Write(uint8_t(7));
    }), "server teleport of the local player");
    Lua(vm, "local x,y,z = getElementPosition(localPlayer); assert(x == 2229.5 and y == -1722 and z == 14.5)");
    Check(Rpc(handler, MtaIds::Rpc::SetElementInterior, 1, [](auto& w) {
        w.Write(uint8_t(0)); w.Write(uint8_t(0));
    }), "server return to the exterior");
    Check(ServerWorld::InteriorOf(1) == 0, "exterior applies to the local player");
    lua_State* L=Script::ActiveVmState();lua_getglobal(L,"otherShape");
    uint32_t localId=Script::ElementIdOf(L,-1);lua_pop(L,1);
    vm.shutdown();Check(!Script::SpatialShapeAlive(localId),"VM shutdown releases local colshapes");
    Check(Script::SpatialShapeAlive(903),"server geometry survives VM shutdown");
    ServerWorld::ResetSession();Check(!Script::SpatialShapeAlive(903),"new session forgets server geometry");
    std::printf("spatial tests: %u checks passed\n",checks);
}
