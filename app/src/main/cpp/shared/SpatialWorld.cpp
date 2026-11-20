#include "SpatialWorld.h"
#include <algorithm>
#include <cmath>

namespace WorldRPG::Spatial {
bool ValidSphere(const Sphere& s) {
    return std::isfinite(s.center.x+s.center.y+s.center.z+s.radius) &&
        s.radius>=-12000 && s.radius<=12000 && s.center.x*s.center.x+s.center.y*s.center.y<=12000.f*12000.f;
}
Sphere ShapeBounds(const Vec& p,const Geometry& g) {
    Sphere b{p,g.radius};
    if (g.type==ShapeType::Cuboid || g.type==ShapeType::Rectangle) {
        b.center.x+=g.size.x*.5f; b.center.y+=g.size.y*.5f;
        b.radius=std::max(g.size.x,g.size.y)*.5f;
        if(g.type==ShapeType::Cuboid) { b.center.z+=g.size.z*.5f; b.radius=std::max(b.radius,g.size.z*.5f); }
    } else if(g.type==ShapeType::Tube) {
        b.center.z+=g.height*.5f; b.radius=std::max(g.radius,g.height*.5f);
    } else if(g.type==ShapeType::Polygon) {
        b.radius=0;
        for(const auto& v:g.points) b.radius=std::max(b.radius,std::hypot(v.x-p.x,v.y-p.y));
    }
    return b;
}
bool Hit(const Vec& p,const Geometry& g,const Vec& v) {
    const float dx=v.x-p.x,dy=v.y-p.y,dz=v.z-p.z;
    switch(g.type) {
        case ShapeType::Circle: return dx*dx+dy*dy<=g.radius*g.radius;
        case ShapeType::Sphere: return dx*dx+dy*dy+dz*dz<=g.radius*g.radius;
        case ShapeType::Cuboid: return v.x>=p.x && v.x<=p.x+g.size.x && v.y>=p.y && v.y<=p.y+g.size.y && v.z>=p.z && v.z<=p.z+g.size.z;
        case ShapeType::Rectangle: return v.x>=p.x && v.x<=p.x+g.size.x && v.y>=p.y && v.y<=p.y+g.size.y;
        case ShapeType::Tube: return dx*dx+dy*dy<=g.radius*g.radius && v.z>=p.z && v.z<=p.z+g.height;
        case ShapeType::Polygon: {
            if(g.points.empty() || v.z<g.floor || v.z>g.ceiling) return false;
            const auto b=ShapeBounds(p,g);
            if(std::hypot(dx,dy)>b.radius) return false;
            bool inside=false;
            size_t j=g.points.size()-1;
            for(size_t i=0;i<g.points.size();j=i++) {
                const auto& a=g.points[i]; const auto& b=g.points[j];
                if((a.y<v.y && b.y>=v.y)||(b.y<v.y && a.y>=v.y))
                    if(a.x+(v.y-a.y)/(b.y-a.y)*(b.x-a.x)<v.x) inside=!inside;
            }
            return inside;
        }
    }
    return false;
}
bool ColliderType(const std::string& t) {
    return t=="player"||t=="ped"||t=="vehicle"||t=="object"||t=="marker"||t=="pickup"||t=="weapon"||t=="projectile";
}
void World::Remove(uint32_t id) {
    elements_.erase(id); previous_.erase(id); refresh_.erase(id);
    std::erase_if(collisions_,[id](auto pair){return pair.first==id || pair.second==id;});
}
void World::Clear() { elements_.clear(); previous_.clear(); collisions_.clear(); refresh_.clear(); }
void World::Sync(const std::vector<Element>& input) {
    std::set<uint32_t> alive;
    for(const auto& e:input) {
        alive.insert(e.id);
        auto old=elements_.find(e.id);
        if(old!=elements_.end()) {
            if(old->second.generation!=e.generation) Remove(e.id);
            else if(old->second.shapeRevision!=e.shapeRevision) refresh_.insert(e.id);
        }
        elements_[e.id]=e;
    }
    std::vector<uint32_t> gone;
    for(const auto& [id,e]:elements_) if(!alive.contains(id)) gone.push_back(id);
    for(uint32_t id:gone) Remove(id); // destructors unlink without leave events
}
const Element* World::Find(uint32_t id) const { auto it=elements_.find(id); return it==elements_.end()?nullptr:&it->second; }
bool World::Within(uint32_t e,uint32_t s) const { return collisions_.contains({s,e}); }
std::vector<uint32_t> World::Colliders(uint32_t s,const std::string& type) const {
    std::vector<uint32_t> result;
    for(const auto& [shape,id]:collisions_) if(shape==s) {
        const auto* e=Find(id);
        if(e && (type.empty() || e->type==type)) result.push_back(id);
    }
    return result;
}
std::vector<uint32_t> World::Range(Vec p,float r,const std::optional<std::string>& type,std::optional<uint16_t> interior,std::optional<uint16_t> dimension) const {
    std::vector<uint32_t> result;
    if(!ValidSphere({p,r})) return result;
    const bool typed=type && !type->empty(), filtered=typed || interior || dimension;
    const float range=std::fabs(r),rr=r*r;
    for(const auto& [id,e]:elements_) {
        if(!ValidSphere(e.bounds)) continue;
        const auto& b=e.bounds;
        const float br=std::fabs(b.radius);
        if(b.center.x-br>p.x+range || b.center.x+br<p.x-range || b.center.y-br>p.y+range || b.center.y+br<p.y-range) continue;
        if(typed && e.type!=*type) continue;
        if(interior && *interior!=e.interior) continue;
        if(dimension && *dimension!=e.dimension) continue;
        if(filtered) {
            float x=e.position.x-p.x,y=e.position.y-p.y,z=e.position.z-p.z;
            if(x*x+y*y+z*z>rr) continue;
        }
        result.push_back(id);
    }
    return result;
}
void World::Refresh(uint32_t id) { refresh_.insert(id); }
void World::Pulse(const std::function<void(const Transition&)>& dispatch) {
    // Snapshot IDs: callbacks may destroy/reparent elements, including the source.
    std::vector<std::pair<uint32_t,uint32_t>> pairs;
    for(const auto& [sid,s]:elements_) if(s.shape && s.shape->enabled) {
        const bool shapeMoved=refresh_.contains(sid)||(previous_.contains(sid) && previous_.at(sid)!=s.position);
        for(const auto& [eid,e]:elements_) if(ColliderType(e.type)) {
            const bool dynamic=e.type=="ped"||e.type=="player"||e.type=="vehicle"||e.type=="projectile";
            if(shapeMoved || dynamic || !previous_.contains(eid) || previous_.at(eid)!=e.position) {
                const float r=std::fabs(s.bounds.radius);
                if(!Within(eid,sid) && (std::fabs(e.position.x-s.bounds.center.x)>r || std::fabs(e.position.y-s.bounds.center.y)>r)) continue;
                pairs.emplace_back(sid,eid);
            }
        }
    }
    refresh_.clear();
    for(const auto& [id,e]:elements_) previous_[id]=e.position;
    for(auto [sid,eid]:pairs) {
        const auto* s=Find(sid); const auto* e=Find(eid);
        if(!s||!e||!s->shape||!s->shape->enabled) continue;
        const bool old=Within(eid,sid);
        if(!old && !ValidSphere({e->position,0})) continue;
        const bool hit=ValidSphere(s->bounds)&&Hit(s->position,*s->shape,e->position);
        if(hit==old) continue;
        Transition t{sid,eid,s->generation,e->generation,hit,s->dimension==e->dimension};
        const bool emit=!hit || s->shape->autoEvents;
        if(hit) collisions_.insert({sid,eid}); else collisions_.erase({sid,eid});
        if(emit) dispatch(t); // membership is visible inside both events
    }
}
}
