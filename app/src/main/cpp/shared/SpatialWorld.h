#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace WorldRPG::Spatial {
struct Vec { float x=0,y=0,z=0; bool operator==(const Vec&) const = default; };
enum class ShapeType : uint8_t { Circle=0, Cuboid=1, Sphere=2, Rectangle=3, Polygon=4, Tube=5 };
struct Geometry {
    ShapeType type=ShapeType::Sphere;
    bool enabled=true, autoEvents=true;
    float radius=0,height=0;
    Vec size;
    std::vector<Vec> points; // polygon world XY, as sent by the desktop
    float floor=-3.402823466e38F, ceiling=3.402823466e38F;
};
struct Sphere { Vec center; float radius=0; };
struct Element {
    uint32_t id=0; uint64_t generation=0, shapeRevision=0;
    std::string type;
    Vec position;
    uint16_t dimension=0; uint8_t interior=0;
    int model=-1;
    Sphere bounds;
    std::optional<Geometry> shape;
};
bool ValidSphere(const Sphere& sphere);
Sphere ShapeBounds(const Vec& position,const Geometry& shape);
bool Hit(const Vec& position,const Geometry& shape,const Vec& point);
bool ColliderType(const std::string& type);
struct Transition { uint32_t shape=0,entity=0; uint64_t shapeGeneration=0,entityGeneration=0; bool hit=false,matchingDimension=false; };
// VM-owned collision history; server/network threads only publish snapshots.
class World {
public:
    void Sync(const std::vector<Element>& elements);
    void Remove(uint32_t id);
    void Clear();
    const Element* Find(uint32_t id) const;
    bool Within(uint32_t entity,uint32_t shape) const;
    std::vector<uint32_t> Colliders(uint32_t shape,const std::string& type) const;
    std::vector<uint32_t> Range(Vec position,float radius,const std::optional<std::string>& type,
        std::optional<uint16_t> interior,std::optional<uint16_t> dimension) const;
    // Called after a colshape moves; creation alone does not refresh colliders.
    void Refresh(uint32_t shape);
    void Pulse(const std::function<void(const Transition&)>& dispatch);
private:
    std::map<uint32_t,Element> elements_;
    std::map<uint32_t,Vec> previous_;
    std::set<std::pair<uint32_t,uint32_t>> collisions_; // shape, entity
    std::set<uint32_t> refresh_;
};
}
