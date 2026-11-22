#pragma once
#include <filesystem>
#include <string_view>

struct lua_State;
namespace WorldRPG::Script {
void InstallFileLua(lua_State* L);
bool ResolveFilePath(std::string_view text, std::filesystem::path& out);
bool ResolveModelPath(std::string_view text, std::filesystem::path& out);
}
