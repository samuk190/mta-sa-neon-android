#pragma once

// Client fetchRemote queue. Desktop references:
//   CLuaNetworkDefs::FetchRemote / GetRemoteRequests / GetRemoteRequestInfo /
//   AbortRemoteRequest / GetServerIp
//   (Client/mods/deathmatch/logic/luadefs/CLuaNetworkDefs.cpp:18-280)
//   CRemoteCalls::Call, GetDownloadModeForQueueName, CRemoteCall::DownloadFinishedCallback,
//   CancelDownload, GetDownloadStatus
//   (Client/mods/deathmatch/logic/CRemoteCalls.cpp:28-264)
//
// Workers never touch Lua. Completions are delivered from ResourceLoader::Pump,
// the game pulse that already drains HttpDownloader::TakeCompletions.
// This queue does not use HttpDownloader.

#include <string>

struct lua_State;

namespace WorldRPG::Net {

// Live RakNet address. jni/JNI_OnLoad.cpp defines the strong function and calls
// RakNetWrapper::GetConnectedServer on the wrapper nativeConnect started.
// RemoteCalls.cpp defines a weak fallback that returns "" when that file is not
// linked (the headless executable owns a different RakNetWrapper).
std::string QueryConnectedServer(bool includePort);

namespace RemoteCalls {

void BindState(lua_State* L);
void DeliverCompletions();
void AbandonAll();

int LuaFetchRemote(lua_State* L);
int LuaGetRemoteRequests(lua_State* L);
int LuaGetRemoteRequestInfo(lua_State* L);
int LuaAbortRemoteRequest(lua_State* L);
int LuaGetServerIp(lua_State* L);

}  // namespace RemoteCalls
}  // namespace WorldRPG::Net
