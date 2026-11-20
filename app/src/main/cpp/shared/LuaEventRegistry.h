#pragma once

// The client event list of desktop CEvents (Client/mods/deathmatch/logic/CEvents.cpp).
// An event exists when it is built in or a running resource added it with addEvent.

#include <cstdint>
#include <string>

namespace WorldRPG::Script::Events {

// CEvents::Exists.
bool Exists(const std::string& name);

// CEvents::AddEvent (CEvents.cpp:28-58) for the resource instance that is running.
// False when the event exists with a different allowRemoteTrigger.
bool Add(const std::string& name, bool allowRemoteTrigger, const std::string& owner, uint16_t netId,
         uint32_t generation);

// What CPacketHandler::Packet_LuaEvent (CPacketHandler.cpp:5226-5244) checks before the call.
enum class Remote { NotAdded, NotRemote, Allowed };
Remote RemoteState(const std::string& name);

// CEvents::RemoveAllEvents(CLuaMain*) (CEvents.cpp:103-131): the resource instance leaves every
// event, and an event no instance holds is removed. netId or generation 0 matches any.
void DropOwner(const std::string& owner, uint16_t netId, uint32_t generation);

// A new VM: only the built-in events remain.
void Reset();

}  // namespace WorldRPG::Script::Events
