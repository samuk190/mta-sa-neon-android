#include "ModelEffects.h"
#include "LuaVMStub.h"
#include "../gtasa/GameTasks.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

namespace WorldRPG::ModelEffects {
namespace {
struct Override { CResource* owner; S2DFXData value; };
struct Original {
    std::map<e2dEffectProperty,std::vector<Override>> properties;
    std::set<CResource*> removers;
};
struct Custom { CResource* owner; Effect baseline, current; };
struct Model {
    std::vector<Effect> baseline;
    std::map<uint32_t,Original> originals;
    std::vector<Custom> customs;
};
std::recursive_mutex stateMutex;
std::map<uint32_t,Model> models;
uint64_t epoch=0;
std::map<std::tuple<uint64_t,std::string,uint16_t,uint32_t>,std::unique_ptr<CResource>> owners;
std::set<CResource*> cleanupOwners;
bool cleanupScheduled=false;
#ifndef __ANDROID__
Backend* testBackend=nullptr;
#endif
Backend* BackendForCalls() {
#ifdef __ANDROID__
    return NativeBackend();
#else
    return testBackend;
#endif
}
template<class T,class F> T OnGame(T fallback,F&& action) {
    T result=fallback;
    auto run=[&] { std::lock_guard lock(stateMutex); if (Backend* b=BackendForCalls()) result=action(*b); };
#ifdef __ANDROID__
    if (!GameTasks::Run(run)) return fallback;
#else
    run();
#endif
    return result;
}
std::vector<Effect> Effective(const Model& model) {
    auto effects=model.baseline;
    for (const auto& [index,original]:model.originals) {
        if (index>=effects.size()) continue;
        if (!original.removers.empty()) { effects[index].type=e2dEffectType::UNKNOWN; continue; }
        for (const auto& [property,stack]:original.properties)
            if (!stack.empty()) CClient2DFXManager::CopyProperty(effects[index].data,stack.back().value,property);
    }
    for (const auto& custom:model.customs) effects.push_back(custom.current);
    return effects;
}
bool Capture(Backend& backend,uint32_t id,Model& state,bool& loaded) {
    std::vector<Effect> baseline;
    if (!backend.Capture(id,loaded,baseline)) return false;
    auto found=models.find(id);
    if (found==models.end()) state.baseline=std::move(baseline);
    else state=found->second;
    return true;
}
bool Publish(Backend& backend,uint32_t id,Model&& next) {
    if (!backend.Commit(id,Effective(next))) return false;
    bool active=!next.customs.empty();
    for (const auto& [index,original]:next.originals) {
        active=active || !original.removers.empty();
        for (const auto& [property,stack]:original.properties) active=active || !stack.empty();
    }
    if (active) models[id]=std::move(next);
    else models.erase(id);
    return true;
}
bool MutateProperty(Backend& backend,CResource* owner,uint32_t id,uint32_t index,
                    e2dEffectProperty property,const S2DFXData* value) {
    if (!owner) return false;
    Model next; bool loaded=false;
    if (!Capture(backend,id,next,loaded)) return false;
    auto effective=Effective(next);
    if (index>=effective.size() || !backend.Supports(effective[index].type) ||
        !CClient2DFXManager::IsPropertyValid(effective[index].type,property)) return false;
    if (index>=next.baseline.size()) {
        auto& custom=next.customs[index-next.baseline.size()];
        if (custom.owner!=owner) return false;
        CClient2DFXManager::CopyProperty(custom.current.data,value?*value:custom.baseline.data,property);
    } else {
        auto& original=next.originals[index];
        if (!original.removers.empty()) return false;
        auto& stack=original.properties[property];
        auto end=std::remove_if(stack.begin(),stack.end(),[&](const Override& entry){return entry.owner==owner;});
        const bool existed=end!=stack.end();
        stack.erase(end,stack.end());
        if (value) stack.push_back({owner,*value});
        else if (!existed) return false;
    }
    return Publish(backend,id,std::move(next));
}
}
CResource* CurrentOwner() {
    const std::string name=Script::ActiveVmOwner();
    if (name.empty()) return nullptr;
    const auto net=Script::ActiveVmOwnerNet(); const auto generation=Script::ActiveVmOwnerGeneration();
    std::lock_guard lock(stateMutex);
    auto key=std::make_tuple(epoch,name,net,generation);
    auto& owner=owners[key];
    if (!owner) owner=std::make_unique<CResource>(CResource{name,net,generation});
    return owner.get();
}
CClient2DFXManager& CClient2DFXManager::GetSingleton() { static CClient2DFXManager instance; return instance; }
bool CClient2DFXManager::IsValidModel(uint32_t id) const {
    return OnGame(false,[&](Backend& b){bool loaded=false; std::vector<Effect> base; return b.Capture(id,loaded,base);});
}
bool CClient2DFXManager::IsModelLoaded(uint32_t id) const {
    return OnGame(false,[&](Backend& b){bool loaded=false; std::vector<Effect> base; return b.Capture(id,loaded,base)&&loaded;});
}
uint32_t CClient2DFXManager::GetCount(uint32_t id,bool custom) const {
    return OnGame(uint32_t{0},[&](Backend& b){Model state; bool loaded=false;
        if (!Capture(b,id,state,loaded)) return uint32_t{0};
        return static_cast<uint32_t>(state.baseline.size()+(custom?state.customs.size():0));});
}
e2dEffectType CClient2DFXManager::GetType(uint32_t id,uint32_t index) const {
    return OnGame(e2dEffectType::NONE,[&](Backend& b){Model state; bool loaded=false;
        if (!Capture(b,id,state,loaded)) return e2dEffectType::NONE;
        auto effects=Effective(state); return index<effects.size()?effects[index].type:e2dEffectType::NONE;});
}
bool CClient2DFXManager::GetData(uint32_t id,uint32_t index,S2DFXData& out) const {
    return OnGame(false,[&](Backend& b){Model state; bool loaded=false;
        if (!Capture(b,id,state,loaded)) return false;
        auto effects=Effective(state);
        if (index>=effects.size() || !effects[index].readable || effects[index].type==e2dEffectType::UNKNOWN || effects[index].type==e2dEffectType::NONE) return false;
        out=effects[index].data; return true;});
}
bool CClient2DFXManager::Add(CResource* owner,uint32_t id,e2dEffectType type,const S2DFXData& data) {
    return OnGame(false,[&](Backend& b){Model next; bool loaded=false;
        if (!owner || !b.Supports(type) || !Capture(b,id,next,loaded) || !loaded ||
            next.baseline.size()+next.customs.size()>=255) return false;
        Effect effect{type,data}; next.customs.push_back({owner,effect,effect});
        return Publish(b,id,std::move(next));});
}
bool CClient2DFXManager::Remove(CResource* owner,uint32_t id,uint32_t index) {
    return OnGame(false,[&](Backend& b){Model next; bool loaded=false;
        if (!owner || !Capture(b,id,next,loaded)) return false;
        auto effects=Effective(next);
        if (index>=effects.size()) return false;
        if (index>=next.baseline.size()) {
            const auto custom=index-next.baseline.size();
            if (next.customs[custom].owner!=owner) return false;
            next.customs.erase(next.customs.begin()+custom);
        } else {
            if (!b.Supports(next.baseline[index].type)) return false;
            auto& removers=next.originals[index].removers;
            if (!removers.insert(owner).second) return true;
        }
        return Publish(b,id,std::move(next));});
}
bool CClient2DFXManager::Restore(CResource* owner,uint32_t id,uint32_t index) {
    return OnGame(false,[&](Backend& b){Model next; bool loaded=false;
        if (!owner || !Capture(b,id,next,loaded) || index>=next.baseline.size()) return false;
        if (!next.originals[index].removers.erase(owner)) return false;
        return Publish(b,id,std::move(next));});
}
bool CClient2DFXManager::SetProperty(CResource* owner,uint32_t id,uint32_t index,e2dEffectProperty p,const S2DFXData& data) {
    return OnGame(false,[&](Backend& b){return MutateProperty(b,owner,id,index,p,&data);});
}
bool CClient2DFXManager::ResetProperty(CResource* owner,uint32_t id,uint32_t index,e2dEffectProperty p) {
    return OnGame(false,[&](Backend& b){return MutateProperty(b,owner,id,index,p,nullptr);});
}
bool CClient2DFXManager::ResetModel(CResource* owner,uint32_t id) {
    return OnGame(false,[&](Backend& b){Model next; bool loaded=false;
        if (!owner || !Capture(b,id,next,loaded)) return false;
        bool changed=false;
        auto end=std::remove_if(next.customs.begin(),next.customs.end(),[&](const Custom& c){return c.owner==owner;});
        changed=end!=next.customs.end(); next.customs.erase(end,next.customs.end());
        for (auto& [index,original]:next.originals) {
            changed=original.removers.erase(owner)>0 || changed;
            for (auto& [property,stack]:original.properties) {
                auto last=std::remove_if(stack.begin(),stack.end(),[&](const Override& entry){return entry.owner==owner;});
                changed=last!=stack.end() || changed; stack.erase(last,stack.end());
            }
        }
        return changed && Publish(b,id,std::move(next));});
}
void CClient2DFXManager::ReleaseResource(CResource* owner) {
    // One dispatch for the whole resource: queuing a synchronous game task for
    // each model would stall resource stop for one game pulse per model.
    OnGame(false,[&](Backend&) {
        std::vector<uint32_t> ids;
        for (const auto& [id,state]:models) ids.push_back(id);
        for (auto id:ids) ResetModel(owner,id);
        return true;
    });
}
void DropResource(const std::string& name,uint16_t net,uint32_t generation) {
    std::vector<CResource*> doomed;
    { std::lock_guard lock(stateMutex);
      for (const auto& [key,owner]:owners)
        if (owner->name==name && (!net || !owner->netId || owner->netId==net) &&
            (!generation || !owner->generation || owner->generation==generation)) doomed.push_back(owner.get()); }
    for (auto* owner:doomed) CClient2DFXManager::GetSingleton().ReleaseResource(owner);
    {
        std::lock_guard lock(stateMutex);cleanupOwners.insert(doomed.begin(),doomed.end());
        if(!cleanupScheduled && !cleanupOwners.empty()) {
            cleanupScheduled=true;
            GameTasks::Defer([] {
                std::lock_guard lock(stateMutex);
                for(auto* owner:cleanupOwners) CClient2DFXManager::GetSingleton().ReleaseResource(owner);
                bool pending=false;
                for(const auto& [id,model]:models) {
                    for(const auto& c:model.customs) pending=pending||cleanupOwners.count(c.owner);
                    for(const auto& [index,original]:model.originals) {
                        for(auto* owner:original.removers) pending=pending||cleanupOwners.count(owner);
                        for(const auto& [property,stack]:original.properties)
                            for(const auto& entry:stack) pending=pending||cleanupOwners.count(entry.owner);
                    }
                }
                if(pending)return false;
                cleanupOwners.clear();cleanupScheduled=false;return true;
            });
        }
    }
}
void Reset() {
    std::vector<CResource*> all;
    { std::lock_guard lock(stateMutex); for (const auto& [key,owner]:owners) all.push_back(owner.get()); }
    for (auto* owner:all) DropResource(owner->name,owner->netId,owner->generation);
    std::lock_guard lock(stateMutex);
    // A game-thread timeout must not free owner identities still referenced by
    // pending overrides. A new VM uses distinct identities even in that case.
    if (models.empty()) {cleanupOwners.clear();owners.clear();}
    else LUA_LOGI("DIAG model 2DFX reset: native cleanup still pending");
    ++epoch;
}
#ifndef __ANDROID__
void SetTestBackend(Backend* backend) { Reset(); std::lock_guard lock(stateMutex); testBackend=backend; }
#endif

bool CClient2DFXManager::IsPropertyValid(e2dEffectType type, e2dEffectProperty property)
{
    if (property == e2dEffectProperty::POSITION)
        return type != e2dEffectType::NONE && type != e2dEffectType::UNKNOWN;
    switch (type)
    {
        case e2dEffectType::LIGHT:
            return property >= e2dEffectProperty::FAR_CLIP_DISTANCE && property <= e2dEffectProperty::FLAGS;
        case e2dEffectType::PARTICLE:
            return property == e2dEffectProperty::PARTICLE_NAME;
        case e2dEffectType::ROADSIGN:
            return property == e2dEffectProperty::COLOR || property == e2dEffectProperty::FLAGS || property == e2dEffectProperty::SIZE ||
                   property == e2dEffectProperty::ROTATION || (property >= e2dEffectProperty::TEXT_1 && property <= e2dEffectProperty::TEXT_4);
        case e2dEffectType::ESCALATOR:
            return property >= e2dEffectProperty::BOTTOM && property <= e2dEffectProperty::DIRECTION;
        default:
            return false;
    }
}

void CClient2DFXManager::CopyProperty(S2DFXData& destination, const S2DFXData& source, e2dEffectProperty property)
{
    switch (property)
    {
        case e2dEffectProperty::POSITION: destination.position = source.position; break;
        case e2dEffectProperty::FAR_CLIP_DISTANCE: destination.drawDistance = source.drawDistance; break;
        case e2dEffectProperty::LIGHT_RANGE: destination.lightRange = source.lightRange; break;
        case e2dEffectProperty::CORONA_SIZE: destination.coronaSize = source.coronaSize; break;
        case e2dEffectProperty::SHADOW_SIZE: destination.shadowSize = source.shadowSize; break;
        case e2dEffectProperty::SHADOW_MULT: destination.shadowMultiplier = source.shadowMultiplier; break;
        case e2dEffectProperty::FLASH_TYPE: destination.flashType = source.flashType; break;
        case e2dEffectProperty::CORONA_REFLECTION: destination.coronaReflection = source.coronaReflection; break;
        case e2dEffectProperty::FLARE_TYPE: destination.flareType = source.flareType; break;
        case e2dEffectProperty::SHADOW_DISTANCE: destination.shadowDistance = source.shadowDistance; break;
        case e2dEffectProperty::OFFSET: destination.offset = source.offset; break;
        case e2dEffectProperty::COLOR: destination.color = source.color; break;
        case e2dEffectProperty::CORONA_NAME: destination.coronaName = source.coronaName; break;
        case e2dEffectProperty::SHADOW_NAME: destination.shadowName = source.shadowName; break;
        case e2dEffectProperty::FLAGS: destination.flags = source.flags; break;
        case e2dEffectProperty::PARTICLE_NAME: destination.particleName = source.particleName; break;
        case e2dEffectProperty::SIZE: destination.size = source.size; break;
        case e2dEffectProperty::ROTATION: destination.rotation = source.rotation; break;
        case e2dEffectProperty::TEXT_1: destination.text[0] = source.text[0]; break;
        case e2dEffectProperty::TEXT_2: destination.text[1] = source.text[1]; break;
        case e2dEffectProperty::TEXT_3: destination.text[2] = source.text[2]; break;
        case e2dEffectProperty::TEXT_4: destination.text[3] = source.text[3]; break;
        case e2dEffectProperty::BOTTOM: destination.bottom = source.bottom; break;
        case e2dEffectProperty::TOP: destination.top = source.top; break;
        case e2dEffectProperty::END: destination.end = source.end; break;
        case e2dEffectProperty::DIRECTION: destination.direction = source.direction; break;
    }
}

} // namespace WorldRPG::ModelEffects
