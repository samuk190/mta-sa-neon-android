#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct lua_State;
namespace WorldRPG::Script {
void InstallUtilLua(lua_State* L);
// CClientGame::GetDevelopmentMode. showCol refuses to change while this is false.
bool DevelopmentMode();
// CClientVariables default allow_tray_notifications (CClientVariables.cpp:355).
bool TrayNotificationsEnabled();

// One client command handler to call (desktop CRegisteredCommands::ProcessCommand).
struct CommandCall {
    // Serial of the handler. A registry ref can be reused after a removal, a serial cannot.
    uint64_t id = 0;
    int ref = 0;
    // The name as it was registered, which is what the handler receives.
    std::string name;
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};
// The handlers registered for this command, in registration order.
void CommandHandlersCollect(const char* name, std::vector<CommandCall>& out);
// False when the handler was removed after CommandHandlersCollect.
bool CommandHandlerLive(uint64_t id);
// CRegisteredCommands::CleanUpForVM: a stopped resource loses its command handlers.
// netId or generation 0 matches any.
void CommandHandlersDropResource(lua_State* L, const std::string& owner, uint16_t netId, uint32_t generation);
}
