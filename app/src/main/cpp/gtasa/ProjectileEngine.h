#pragma once
#include <cstdint>
#include <string>
namespace WorldRPG::Projectiles {
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Request {
    uint32_t creator = 0, target = 0;
    int weapon = 16;
    Vec3 origin, velocity;
    uint8_t originMask = 7;
    float force = 1;
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};
struct Snapshot {
    uint32_t id = 0, creator = 0, target = 0, counter = 0;
    int weapon = 0;
    float force = 0;
    Vec3 position;
};
using ResolveEntity = void* (*)(uint32_t);
// Only the game thread resolves native handles, creates/removes physics objects
// or reads the engine pool. Lua sees copies of the previous frame's state.
void Pump(ResolveEntity resolve);
uint32_t Create(const Request& request);
bool Copy(uint32_t id, Snapshot& out);
bool Release(uint32_t id);
bool SetCounter(uint32_t id, uint32_t milliseconds);
void DropOwner(const std::string& owner, uint16_t netId, uint32_t generation);
void Reset();
}
