#pragma once
// Objects that belong in GTA's world: server objects (ENTITY_ADD type 3) and
// client objects (createObject). The VM and network side publish the current
// state; the game thread (gtasa/WorldObjects.cpp) streams CObjects in and out
// from it, as desktop CClientObject does through CClientStreamer.

#include <cstdint>
#include <vector>

namespace WorldRPG::WorldObjects {

struct State {
    uint32_t id = 0;
    int model = -1;
    float x = 0, y = 0, z = 0;
    float rx = 0, ry = 0, rz = 0;  // radians, CMatrix::SetRotate order
    float scaleX = 1.f, scaleY = 1.f, scaleZ = 1.f;
    uint16_t dimension = 0;
    uint8_t interior = 0;
    bool collisions = true;
    bool visible = true;
    bool frozen = false;
    bool allDimensions = false;  // visibleInAllDimensions (dimension -1)
    uint8_t alpha = 255;         // 0: hidden, collision kept
};

enum class Source { Server = 0, Client = 1 };

// Replaces every object of one source. Any thread. Copy lists the server source
// first, so a client state for the same id (a script moved a server object) wins.
void Publish(Source source, std::vector<State> states);

// The game thread's copy of both sources, and a revision that changes on every Publish.
uint64_t Revision();
void Copy(std::vector<State>& out);

// New session: both sources empty.
void Reset();

}  // namespace WorldRPG::WorldObjects
