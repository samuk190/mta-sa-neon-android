#pragma once

// Client-side MTA API bindings that do not need the GTA engine: chat output, element
// data, element helpers and no-op natives for engine features the phone does not
// expose yet. InstallExtraBindings runs at the end of installDxBindings, so a
// binding here replaces a core binding or an "unimplemented" stub of the same name.

#include <cstdint>

struct lua_State;

namespace WorldRPG::ServerWorld { struct ElementDataChange; }

namespace WorldRPG::Script {

void InstallExtraBindings(lua_State* L);
// Applies a server custom data change to the script store and fires
// onClientElementDataChange (CClientEntity::SetCustomData / DeleteCustomData).
void ApplyServerElementData(lua_State* L, const ServerWorld::ElementDataChange& change);
// Forgets the values setElementData stored for an element that is gone, so a reused id starts clean.
// L is the primary state.
void DropClientElementData(lua_State* L, uint32_t id);

// Element tables are {id = <id>, element = true}, as every element binding expects.
void PushElement(lua_State* L, uint32_t id);
uint32_t ElementIdOf(lua_State* L, int index);

// CPlayerRPCs::SetPlayerMoney (CPlayerRPCs.cpp:29-42): the value getPlayerMoney reports. VM thread only.
void RpcSetPlayerMoney(int32_t amount, bool instant);
long long ScriptPlayerMoney();

// CInputRPCs::ShowChat (CInputRPCs.cpp:293-304): the state isChatVisible reports and the chat view. VM thread only.
void RpcSetChatVisible(bool visible, bool inputBlocked);

} // namespace WorldRPG::Script
