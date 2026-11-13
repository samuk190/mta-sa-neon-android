#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#define LUA_LOG_TAG "WorldRPGNeon"
#define LUA_LOGI(...) __android_log_print(ANDROID_LOG_INFO, LUA_LOG_TAG, __VA_ARGS__)
#define LUA_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LUA_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LUA_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define LUA_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Net {
struct ResourceStartInfo;
}

namespace WorldRPG::Script {

/// Lua script execution result
struct LuaExecResult {
    bool success = false;
    std::string errorMessage;
    int returnCode = 0;
};

/// Callback for Lua print/output redirection
using LuaOutputCallback = std::function<void(const char* message)>;

/// Stub Lua VM interface for server-side resource script execution.
/// Provides the API surface that will be backed by an actual Lua 5.1/5.3
/// interpreter in a future phase. Currently logs all calls for protocol
/// validation and integration testing without executing scripts.
class LuaVMStub {
public:
    LuaVMStub();
    ~LuaVMStub();

    LuaVMStub(const LuaVMStub&) = delete;
    LuaVMStub& operator=(const LuaVMStub&) = delete;

    /// Initialize the Lua VM (stub: logs initialization)
    bool initialize();

    /// Check if VM is initialized
    bool isReady() const;

    /// Load and execute a Lua script from memory buffer
    LuaExecResult executeBuffer(const char* code, size_t length, const char* chunkName = "script");

    /// Load and execute a Lua script from file path
    LuaExecResult executeFile(const char* filePath);

    /// Register a C function callable from Lua scripts
    void registerFunction(const char* name, std::function<int()> handler);

    /// Set output callback for Lua print() redirection
    void setOutputCallback(LuaOutputCallback callback);

    /// Get list of registered MTA client-side functions (stub)
    std::vector<std::string> getRegisteredFunctions() const;

    /// Reset the VM state (clear globals, loaded scripts)
    void reset();

    /// Shutdown and release resources
    void shutdown();

    /// Physical pixels the client scripts see from guiGetScreenSize.
    void setScreenSize(int width, int height);

    /// Delivers a CHAT_ECHO into onClientChatMessage when a resource
    /// registered one. Returns false when the Java chat should show it.
    bool onChatMessage(const char* text, int red, int green, int blue);

    /// Runs timers and onClientRender. The result is one frame of
    /// "R x y w h argb" and "T x y size argb text" commands.
    std::string pumpDx();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

// RESOURCE_START (82): records the resource and queues its client files and scripts.
void ActiveVmOnResourceStart(const WorldRPG::Net::ResourceStartInfo& info);
// RESOURCE_CLIENT_SCRIPTS (95): holds the no-cache scripts until the resource can start.
bool ActiveVmFeedScripts(const uint8_t* body, size_t length, size_t bits);
// Runs resources that became ready (downloads finished, scripts received). Call on the
// VM thread after each network pulse. Returns true when the chat hook became active.
bool ActiveVmPulseResources();
// New session: drops every resource and restarts the VM with a clean state.
void ActiveVmResetSession();
bool ActiveVmOnChat(const char* text, int red, int green, int blue);
std::string ActiveVmPump();
void ActiveVmSetScreen(int width, int height);
// Screen size scripts see (guiGetScreenSize), and the resource whose code is running.
void ActiveVmScreenSize(int& width, int& height);
const std::string& ActiveVmOwner();
bool ActiveVmChatHooked();
bool ActiveVmOnServerEvent(const uint8_t* body, size_t length, size_t bits);
// RESOURCE_STOP (83): removes the resource and stops its scripts (desktop Packet_ResourceStop).
void ActiveVmOnResourceStop(const uint8_t* body, size_t length, size_t bits);

} // namespace WorldRPG::Script