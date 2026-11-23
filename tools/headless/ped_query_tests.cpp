#include "shared/PedQueries.h"
#include "shared/ServerWorld.h"
#include "shared/PacketHandler.h"
#include "shared/MtaIds.h"
#include "shared/LuaVMStub.h"
#include "gtasa/GameEngine.h"
#include "stubs/test_ped_queries.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace WorldRPG;
namespace Q = WorldRPG::PedQueries;
unsigned checks = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
void Bits(Net::BitStreamWriter& w, uint32_t n, size_t count) {
    const uint8_t raw[] = {uint8_t(n), uint8_t(n >> 8), uint8_t(n >> 16), uint8_t(n >> 24)};
    w.WriteBits(raw, count);
}
void PedAdd(uint32_t id) {
    Net::BitStreamWriter w;
    w.WriteCompressed(1,4);
    Bits(w,id,17); w.Write(uint8_t(14)); Bits(w,0,17); w.Write(uint8_t(0)); w.WriteCompressed(0,2);
    Bits(w,0,1); Bits(w,1,1); w.WriteCompressed(0,2); w.WriteCompressed(0,2);
    Bits(w,100*1024,24); Bits(w,50*1024,24); w.Write(12.f); w.WriteCompressed(61,2);
    Bits(w,32768,16); w.Write(uint8_t(100)); w.Write(uint8_t(0)); Bits(w,0,1);
    Bits(w,0,1); Bits(w,0,1); Bits(w,0,1); Bits(w,0,1);
    w.WriteCompressed(0,1); w.Write(uint8_t(0)); w.Write(uint8_t(0));
    w.Write(uint8_t(2)); w.Write(uint8_t(22)); w.Write(uint16_t(170));
    w.Write(uint8_t(255)); w.Write(uint8_t(2)); Bits(w,0,1);
    ServerWorld::OnEntityAdd(w.GetData(),w.GetByteLength(),w.GetBitLength());
    Check(std::string(ServerWorld::ElementKindName(id)) == "ped", "ENTITY_ADD registered a ped");
}
void Rpc(Sync::PacketHandler& handler, uint8_t fn, uint32_t id, unsigned weapon = 0,
         unsigned ammo = 0, unsigned clip = 0, bool current = false) {
    Net::BitStreamWriter w;
    w.Write(MtaIds::Packet::LuaElementRpc); w.Write(fn); Bits(w,id,17);
    if (fn == MtaIds::Rpc::GiveWeapon || fn == MtaIds::Rpc::TakeWeapon || fn == MtaIds::Rpc::SetWeaponAmmo) {
        Bits(w,weapon,6); w.WriteCompressed(ammo,2);
        if (fn == MtaIds::Rpc::GiveWeapon) Bits(w,current,1);
        if (fn == MtaIds::Rpc::SetWeaponAmmo) w.WriteCompressed(clip,2);
    } else if (fn == MtaIds::Rpc::SetWeaponSlot) Bits(w,weapon,4);
    Check(handler.handlePacket(99,w.GetData(),w.GetByteLength(),w.GetBitLength()), "weapon RPC parsed");
}
void Lua(Script::LuaVMStub& vm, const char* script) {
    auto r = vm.executeBuffer(script,std::char_traits<char>::length(script),"ped-queries");
    if (!r.success) std::fprintf(stderr,"%s\n",r.errorMessage.c_str());
    Check(r.success,"Lua query behavior");
}
int main() {
    ServerWorld::ResetSession(); ServerWorld::SetLocalPlayer(1);
    ServerWorld::NotePlayerSkin(2,7); PedAdd(900);
    Script::LuaVMStub vm; Check(vm.initialize(),"VM initializes");
    Lua(vm,R"(
        p={id=900}; other={id=2}
        assert(getPedWeapon(p)==22 and getPedWeaponSlot(p)==2)
        assert(getPedTotalAmmo(p)==170 and getPedAmmoInClip(p)==false)
        assert(getPedWeapon(p,13)==0 and getPedTotalAmmo(p,13)==false)
        assert(getPedWeapon(p,nil)==22 and getPedWeapon(p,'2')==22)
        assert(getPedWeapon(p,255)==22 and getPedWeapon(p,256)==0)
        assert(getPedWeapon(p,-1)==false and getPedWeapon(p,{})==false)
        assert(getPedWeapon(p,0/0)==false and getPedWeapon(p,1/0)==false)
        assert(getPedWeapon({id=999})==false and getPedWeapon(1)==false)
        assert(isPedDead({id=999})==nil and isPedDead(false)==nil)
        assert(isPedDead(p)==false and isPedDead(localPlayer)==false)
        assert(getPedTotalAmmo(other)==false and getPedAmmoInClip(other)==false)
    )");
    Q::Snapshot state; Check(Q::Copy(900,state),"copy initial ped state");
    const auto generation = state.generation;
    state.streamed=true; state.currentSlot=5; state.weapons[5]={31,70000,65538};
    Headless::SetPedQueryNative(900,state);
    Lua(vm,R"(
        assert(getPedWeapon(p)==31 and getPedWeaponSlot(p)==5)
        assert(getPedTotalAmmo(p)==4464 and getPedAmmoInClip(p)==2)
        assert(getPedTotalAmmo(p,0)==1 and getPedTotalAmmo(p,1)==1)
        assert(getPedTotalAmmo(p,10)==1 and getPedTotalAmmo(p,11)==1 and getPedTotalAmmo(p,12)==1)
        assert(getPedTotalAmmo(p,3)==0 and getPedAmmoInClip(p,3)==0)
    )");
    for (int task=211;task<=219;++task) {
        state.taskType=task; state.health=100;
        Headless::SetPedQueryNative(900,state);
        Lua(vm,(std::string("assert(isPedDead(p)==")+(task>=212&&task<=218?"true)":"false)")).c_str());
    }
    state.taskType=0; state.health=0; Headless::SetPedQueryNative(900,state);
    Lua(vm,"assert(isPedDead(p)==false)"); // zero health alone is insufficient
    Q::Wasted(2); Q::Snapshot player; Q::Copy(2,player); player.taskType=0; player.health=0;
    Headless::SetPedQueryNative(2,player); Lua(vm,"assert(isPedDead(other)==true)");
    player.health=100; Headless::SetPedQueryNative(2,player);
    Lua(vm,"assert(isPedDead(other)==false)"); // stale network death after revival
    Q::Health(2,100); Headless::ClearPedQueryNative(); Lua(vm,"assert(isPedDead(other)==false)");
    // Real packet dispatch updates the same client inventory queried by Lua.
    Sync::PacketHandler handler;
    Rpc(handler,MtaIds::Rpc::GiveWeapon,900,31,90,0,true);
    Lua(vm,"assert(getPedWeapon(p)==31 and getPedTotalAmmo(p)==90 and getPedWeaponSlot(p)==5)");
    Rpc(handler,MtaIds::Rpc::GiveWeapon,900,31,20);
    Lua(vm,"assert(getPedTotalAmmo(p,5)==110)");
    Rpc(handler,MtaIds::Rpc::TakeWeapon,900,31,40);
    Lua(vm,"assert(getPedTotalAmmo(p,5)==70)");
    Rpc(handler,MtaIds::Rpc::SetWeaponAmmo,900,31,600,12);
    Lua(vm,"assert(getPedTotalAmmo(p,5)==600 and getPedAmmoInClip(p,5)==false)");
    Rpc(handler,MtaIds::Rpc::TakeWeapon,900,31,1000);
    Lua(vm,"assert(getPedTotalAmmo(p,5)==false)");
    Rpc(handler,MtaIds::Rpc::SetWeaponSlot,900,2);
    Lua(vm,"assert(getPedWeaponSlot(p)==2 and getPedWeapon(p)==22)");
    Rpc(handler,MtaIds::Rpc::SetWeaponAmmo,900,22,65530,0);
    Rpc(handler,MtaIds::Rpc::GiveWeapon,900,22,20);
    Lua(vm,"assert(getPedTotalAmmo(p,2)==14)"); // unsigned-short cache wrap
    Rpc(handler,MtaIds::Rpc::GiveWeapon,900,36,65000);
    Lua(vm,"assert(getPedTotalAmmo(p,7)==9999)"); // GiveWeapon RPC clamp
    Rpc(handler,MtaIds::Rpc::SetWeaponSlot,900,15);
    Lua(vm,"assert(getPedWeaponSlot(p)==2)");
    Rpc(handler,MtaIds::Rpc::GiveWeapon,2,31,99,0,true);
    Lua(vm,"assert(getPedWeapon(other)==31 and getPedTotalAmmo(other)==false)");
    Rpc(handler,MtaIds::Rpc::TakeAllWeapons,900);
    Lua(vm,"assert(getPedWeapon(p)==0 and getPedWeaponSlot(p)==0 and getPedTotalAmmo(p,2)==false)");
    // A consumed/reloaded native inventory is preserved on stream-out. The
    // offscreen RPCs then update the client cache before model creation.
    PedAdd(901); Q::Copy(901,state); state.streamed=true; state.weapons[2]={22,123,7};
    Q::StreamOut(901,state); Q::Snapshot saved; Q::Copy(901,saved);
    Check(saved.cachedAmmo[2]==123,"stream-out saves actual remaining ammo");
    Game::PedRpcOp give; give.kind=Game::PedRpcOp::Kind::GiveWeapon; give.weapon=22; give.ammo=20;
    state.weapons[2].total=100;
    Q::StreamOut(901,state); Q::WeaponRpc(901,give); Q::Copy(901,saved);
    Check(saved.cachedAmmo[2]==120,"offscreen give adds to actual stream-out ammo");
    give.weapon=24; Q::WeaponRpc(901,give); Q::CopyRestore(901,saved);
    Check(saved.weapons[2].type==24 && saved.weapons[2].total==140,
          "offscreen weapon replacement restores client ammo instead of replaying native give");
    // Entity deletion/reuse rejects an old GTA body's sample by generation.
    Net::BitStreamWriter remove; Bits(remove,900,17);
    ServerWorld::OnEntityRemove(remove.GetData(),remove.GetByteLength(),remove.GetBitLength());
    Lua(vm,"assert(getPedWeapon(p)==false and isPedDead(p)==nil)");
    PedAdd(900); Q::Copy(900,saved); Check(saved.generation!=generation,"ID reuse gets a new generation");
    Check(!Q::Current(900,generation) && Q::Current(900,saved.generation),"old RPC generation is rejected");
    state.generation=generation; state.weapons[2].total=999; Q::StreamOut(900,state);
    Lua(vm,"assert(getPedTotalAmmo(p)==170)");
    Q::Wasted(900); Lua(vm,"assert(isPedDead(p)==true)");
    Q::Health(900,100,false); Lua(vm,"assert(isPedDead(p)==true)"); // LockHealth does not clear m_bDead
    Q::Health(900,50);
    Lua(vm,"assert(isPedDead(p)==false)");
    Q::Spawn(2); Lua(vm,"assert(isPedDead(other)==false and getPedWeapon(other)==0)");
    ServerWorld::OnPlayerQuit(2); Check(!Q::Copy(2,saved),"player quit forgets state");
    ServerWorld::ResetSession(); Check(!Q::Copy(900,saved),"session reset forgets state");
    std::printf("ped query tests: %u checks passed\n",checks);
}
