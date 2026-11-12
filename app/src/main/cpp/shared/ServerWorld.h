#pragma once

#include <cstdint>
#include <string>

namespace WorldRPG::ServerWorld {

void SetLocalPlayer(uint32_t id);
uint32_t LocalPlayer();
void NotePlayerSkin(uint32_t id, int model);

// -1 until PLAYER_LIST or ENTITY_ADD names a model.
int ModelOf(uint32_t id);

bool GetData(uint32_t id, const char* key, double& number, std::string& text, bool& isNumber);

void OnEntityAdd(const uint8_t* body, size_t length, size_t bits);
void OnEntityRemove(const uint8_t* body, size_t length, size_t bits);
void OnCustomData(const uint8_t* body, size_t length, size_t bits);
void OnPlayerList(const uint8_t* body, size_t length, size_t bits);
void OnResourceStart(const uint8_t* body, size_t length, size_t bits);
uint32_t LastResourceRoot();

struct ActorSpawn {
    uint32_t id = 0;
    int kind = 0;
    int model = -1;
    float x = 0, y = 0, z = 0, heading = 0;
    bool hosted = false;
};
int CopyActors(ActorSpawn* out, int capacity);
void SetActorHost(uint32_t id, bool host);
bool ActorHosted(uint32_t id);
bool ElementPose(uint32_t id, float& x, float& y, float& z);
const char* ElementKindName(uint32_t id);

// Radar blips and the F11 snapshot. Call on the game thread.
void Tick(float playerX, float playerY, float playerZ);
std::string MapSnapshot(float playerX, float playerY, float playerHeading);

} // namespace WorldRPG::ServerWorld
