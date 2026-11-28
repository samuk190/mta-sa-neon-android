#pragma once
// Game-thread streamer for server and client objects (WorldObjectFeed -> CObject).

#include <cstdint>

namespace WorldRPG::Game::WorldObjects {

// Once per game frame from WorldRPG_NoStoryScript.
void Pulse(float cameraX, float cameraY, float cameraZ);
// The CObject of a streamed-in object element, or null. Game thread.
void* EntityOf(uint32_t id);
// The element of a CObject this streamer created, or 0xFFFFFFFF. Game thread.
uint32_t ElementOf(void* entity);
// Damages/breaks a streamed-in CObject via CObject::ObjectDamage. Game thread.
bool BreakObject(uint32_t id);
// Destroys every streamed object (session end). Game thread.
void Reset();

}  // namespace WorldRPG::Game::WorldObjects
