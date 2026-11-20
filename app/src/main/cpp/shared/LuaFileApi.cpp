#include "LuaFileApi.h"
#include "ResourceLoader.h"

#include "LuaVMStub.h"
#include "net/HttpDownloader.h"

#include "lua.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace WorldRPG::Script {
namespace {

namespace fs = std::filesystem;

struct OpenFile {
    std::fstream stream;
    std::string path;
    bool readOnly = false;
    std::streamoff pos = 0;
};

std::map<int, OpenFile> g_files;
int g_nextFile = 1;

fs::path Root() {
#if defined(__ANDROID__)
    return "/data/data/com.worldrpg.neonlauncher/files/lua";
#else
    return "/tmp/neon-lua-files";
#endif
}

// Resolve symlinks before comparing path components, including for paths that
// do not exist yet and may be created by a file operation.
bool PathWithin(const fs::path& root, const fs::path& relative, fs::path& out) {
    if (root.empty() || relative.is_absolute()) return false;
    std::error_code error;
    const fs::path absoluteRoot = fs::absolute(root, error);
    if (error) return false;
    const fs::path canonicalRoot = fs::weakly_canonical(absoluteRoot, error);
    if (error) return false;
    const fs::path candidate = fs::weakly_canonical(canonicalRoot / relative, error);
    if (error) return false;
    const auto common = std::mismatch(canonicalRoot.begin(), canonicalRoot.end(), candidate.begin(), candidate.end());
    if (common.first != canonicalRoot.end()) return false;
    out = candidate;
    return true;
}

enum class PathAccess { Read, Write, Asset, Model };

bool Resolve(std::string_view text, PathAccess access, fs::path& out, bool* readOnly = nullptr) {
    if (text.empty() || text.find('\0') != std::string_view::npos) return false;
    std::string raw(text);
    std::replace(raw.begin(), raw.end(), '\\', '/');
    const fs::path original(raw);
    if (original.is_absolute()) return false;
    for (const auto& part : original)
        if (part == "..") return false;

    std::string resource = ActiveVmOwner();
    fs::path relative = original;
    const bool explicitResource = raw.front() == ':';
    if (explicitResource) {
        const auto slash = raw.find('/');
        if (access == PathAccess::Write || slash == std::string::npos || slash < 2 || slash + 1 == raw.size()) return false;
        resource = raw.substr(1, slash - 1);
        if (resource == "." || resource == ".." || resource.find(':') != std::string::npos) return false;
        relative = fs::path(raw.substr(slash + 1));
        if (relative.is_absolute()) return false;
    }

    if (access != PathAccess::Write) {
        std::error_code error;
        if (access == PathAccess::Read && !explicitResource && PathWithin(Root(), relative, out) &&
            fs::exists(out, error))
            return true;
        const fs::path cache = WorldRPG::Net::HttpDownloader::GetResourceCacheRoot();
        if (!cache.empty() && !resource.empty() && PathWithin(cache, fs::path(resource) / relative, out) &&
            fs::exists(out, error)) {
            if (access != PathAccess::Model ||
                ResourceLoader::IsFileReady(resource, relative.lexically_normal().generic_string(),
                                            explicitResource ? 0 : ActiveVmOwnerGeneration())) {
                if (readOnly) *readOnly = true;
                return true;
            }
        }
        if (explicitResource) return false;
        if (!cache.empty() && PathWithin(cache.parent_path() / "game_assets", relative, out) &&
            fs::exists(out, error)) {
            if (readOnly) *readOnly = true;
            return true;
        }
    }
    if ((access == PathAccess::Asset || access == PathAccess::Model) && !resource.empty()) return false;
    return PathWithin(Root(), relative, out);
}

bool Resolve(lua_State* L, int index, fs::path& out, PathAccess access = PathAccess::Read, bool* readOnly = nullptr) {
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    return text && Resolve(std::string_view(text, length), access, out, readOnly);
}

OpenFile* FindFile(lua_State* L) {
    if (!lua_isnumber(L, 1)) return nullptr;
    const auto found = g_files.find(static_cast<int>(lua_tointeger(L, 1)));
    return found == g_files.end() ? nullptr : &found->second;
}

void Place(OpenFile& file) {
    file.stream.clear();
    file.stream.seekg(file.pos);
    if (!file.readOnly) file.stream.seekp(file.pos);
}

int l_fileExists(lua_State* L) {
    fs::path path;
    std::error_code error;
    lua_pushboolean(L, Resolve(L, 1, path) && fs::is_regular_file(path, error) ? 1 : 0);
    return 1;
}

int l_fileCreate(lua_State* L) {
    fs::path path;
    if (!Resolve(L, 1, path, PathAccess::Write)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    {
        std::ofstream created(path, std::ios::binary | std::ios::trunc);
        if (!created) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    OpenFile opened;
    opened.path = path.string();
    opened.stream.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!opened.stream) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int id = g_nextFile++;
    g_files.emplace(id, std::move(opened));
    lua_pushinteger(L, id);
    return 1;
}

int l_fileOpen(lua_State* L) {
    fs::path path;
    bool cached = false;
    std::error_code error;
    if (!Resolve(L, 1, path, PathAccess::Read, &cached) || !fs::is_regular_file(path, error)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool readOnly = cached || lua_toboolean(L, 2) != 0;
    OpenFile opened;
    opened.path = path.string();
    opened.readOnly = readOnly;
    opened.stream.open(path, readOnly ? std::ios::in | std::ios::binary
                                      : std::ios::in | std::ios::out | std::ios::binary);
    if (!opened.stream) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int id = g_nextFile++;
    g_files.emplace(id, std::move(opened));
    lua_pushinteger(L, id);
    return 1;
}

int l_fileClose(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int id = static_cast<int>(lua_tointeger(L, 1));
    const auto found = g_files.find(id);
    if (found == g_files.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    found->second.stream.close();
    g_files.erase(found);
    lua_pushboolean(L, 1);
    return 1;
}

int l_fileFlush(lua_State* L) {
    OpenFile* file = FindFile(L);
    if (!file) {
        lua_pushboolean(L, 0);
        return 1;
    }
    file->stream.flush();
    lua_pushboolean(L, file->stream.good() || file->stream.eof() ? 1 : 0);
    return 1;
}

int l_fileRead(lua_State* L) {
    OpenFile* file = FindFile(L);
    if (!file || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int count = static_cast<int>(lua_tointeger(L, 2));
    if (count <= 0 || count > (1 << 20)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::string bytes(static_cast<std::size_t>(count), '\0');
    Place(*file);
    file->stream.read(bytes.data(), count);
    const auto got = file->stream.gcount();
    file->pos += got;
    bytes.resize(static_cast<std::size_t>(got));
    lua_pushlstring(L, bytes.data(), bytes.size());
    return 1;
}

int l_fileWrite(lua_State* L) {
    OpenFile* file = FindFile(L);
    if (!file || file->readOnly) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::size_t written = 0;
    const int top = lua_gettop(L);
    for (int i = 2; i <= top; ++i) {
        size_t length = 0;
        const char* text = lua_tolstring(L, i, &length);
        if (!text) {
            lua_pushboolean(L, 0);
            return 1;
        }
        Place(*file);
        file->stream.write(text, static_cast<std::streamsize>(length));
        if (!file->stream) {
            lua_pushboolean(L, 0);
            return 1;
        }
        file->pos += static_cast<std::streamoff>(length);
        written += length;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(written));
    return 1;
}

int l_fileGetPos(lua_State* L) {
    OpenFile* file = FindFile(L);
    if (!file) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(file->pos));
    return 1;
}

int l_fileSetPos(lua_State* L) {
    OpenFile* file = FindFile(L);
    if (!file || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto offset = static_cast<std::streamoff>(lua_tointeger(L, 2));
    if (offset < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    file->pos = offset;
    Place(*file);
    lua_pushinteger(L, static_cast<lua_Integer>(file->pos));
    return 1;
}

int l_fileGetSize(lua_State* L) {
    OpenFile* file = FindFile(L);
    if (!file) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::error_code error;
    const auto size = fs::file_size(file->path, error);
    if (error) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(size));
    return 1;
}

int l_fileGetPath(lua_State* L) {
    OpenFile* file = FindFile(L);
    if (!file) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, file->path.c_str());
    return 1;
}

int l_fileIsEOF(lua_State* L) {
    OpenFile* file = FindFile(L);
    if (!file) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::error_code error;
    const auto size = fs::file_size(file->path, error);
    lua_pushboolean(L, !error && file->pos >= static_cast<std::streamoff>(size) ? 1 : 0);
    return 1;
}

int l_fileDelete(lua_State* L) {
    fs::path path;
    std::error_code error;
    if (!Resolve(L, 1, path, PathAccess::Write) || !fs::is_regular_file(path, error)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, fs::remove(path, error) ? 1 : 0);
    return 1;
}

int l_fileCopy(lua_State* L) {
    fs::path from;
    fs::path to;
    if (!Resolve(L, 1, from) || !Resolve(L, 2, to, PathAccess::Write)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool overwrite = !lua_isnoneornil(L, 3) && lua_toboolean(L, 3) != 0;
    std::error_code error;
    if (fs::exists(to, error) && !overwrite) {
        lua_pushboolean(L, 0);
        return 1;
    }
    fs::create_directories(to.parent_path(), error);
    fs::copy_file(from, to, overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none, error);
    lua_pushboolean(L, !error ? 1 : 0);
    return 1;
}

int l_fileRename(lua_State* L) {
    fs::path from;
    fs::path to;
    if (!Resolve(L, 1, from, PathAccess::Write) || !Resolve(L, 2, to, PathAccess::Write)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::error_code error;
    fs::rename(from, to, error);
    lua_pushboolean(L, !error ? 1 : 0);
    return 1;
}

int l_fileGetHash(lua_State* L) {
    OpenFile* file = FindFile(L);
    const char* algorithm = lua_tostring(L, 2);
    if (!file || !algorithm || (std::strcmp(algorithm, "md5") != 0 && std::strcmp(algorithm, "sha256") != 0)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    file->stream.flush();
    std::ifstream input(file->path, std::ios::binary);
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    lua_getglobal(L, algorithm);
    lua_pushlstring(L, bytes.data(), bytes.size());
    if (lua_pcall(L, 1, 1, 0) != 0) {
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
    }
    return 1;
}

int l_fileGetContents(lua_State* L) {
    fs::path path;
    std::error_code error;
    if (!Resolve(L, 1, path) || !fs::is_regular_file(path, error)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    lua_pushlstring(L, bytes.data(), bytes.size());
    return 1;
}

int l_pathIsFile(lua_State* L) {
    fs::path path;
    std::error_code error;
    lua_pushboolean(L, Resolve(L, 1, path) && fs::is_regular_file(path, error) ? 1 : 0);
    return 1;
}

int l_pathIsDirectory(lua_State* L) {
    fs::path path;
    std::error_code error;
    lua_pushboolean(L, Resolve(L, 1, path) && fs::is_directory(path, error) ? 1 : 0);
    return 1;
}

int l_pathListDir(lua_State* L) {
    fs::path path;
    if (!Resolve(L, 1, path)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::error_code error;
    if (!fs::is_directory(path, error)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_newtable(L);
    int index = 0;
    const fs::directory_iterator end;
    for (fs::directory_iterator it(path, error); !error && it != end; it.increment(error)) {
        lua_pushstring(L, it->path().filename().string().c_str());
        lua_rawseti(L, -2, ++index);
    }
    if (error) {
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
    }
    return 1;
}

}  // namespace

bool ResolveFilePath(std::string_view text, fs::path& out) {
    std::error_code error;
    return Resolve(text, PathAccess::Asset, out) && fs::is_regular_file(out, error);
}

bool ResolveModelPath(std::string_view text, fs::path& out) {
    std::error_code error;
    return Resolve(text, PathAccess::Model, out) && fs::is_regular_file(out, error);
}

void InstallFileLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"fileCreate", l_fileCreate},
        {"fileOpen", l_fileOpen},
        {"fileExists", l_fileExists},
        {"fileClose", l_fileClose},
        {"fileFlush", l_fileFlush},
        {"fileRead", l_fileRead},
        {"fileWrite", l_fileWrite},
        {"fileGetPos", l_fileGetPos},
        {"fileSetPos", l_fileSetPos},
        {"fileGetSize", l_fileGetSize},
        {"fileGetPath", l_fileGetPath},
        {"fileIsEOF", l_fileIsEOF},
        {"fileDelete", l_fileDelete},
        {"fileCopy", l_fileCopy},
        {"fileRename", l_fileRename},
        {"fileGetContents", l_fileGetContents},
        {"fileGetHash", l_fileGetHash},
        {"pathIsFile", l_pathIsFile},
        {"pathIsDirectory", l_pathIsDirectory},
        {"pathListDir", l_pathListDir},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
