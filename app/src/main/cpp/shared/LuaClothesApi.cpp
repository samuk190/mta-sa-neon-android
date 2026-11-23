#include "LuaClothesApi.h"

#include "LuaVMStub.h"

#include "lua.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace WorldRPG::Script {
namespace {

#include "LuaClothesData.inc"

struct ExtraCloth { std::string texture; std::string model; int type; };
std::vector<ExtraCloth> g_extraClothes;
bool g_diagClothes = false;

bool SameText(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

int ExtraCount(int type) {
    int count = 0;
    for (const ExtraCloth& cloth : g_extraClothes)
        if (cloth.type == type) ++count;
    return count;
}

int l_getClothesByTypeIndex(lua_State* L) {
    // CStaticFunctionDefinitions::GetClothesByTypeIndex (2759-2773).
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int type = static_cast<int>(lua_tointeger(L, 1));
    const int index = static_cast<int>(lua_tointeger(L, 2));
    if (type < 0 || type >= static_cast<int>(sizeof(kClothesCount) / sizeof(kClothesCount[0])) || index < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (index < kClothesCount[type]) {
        for (const Cloth& cloth : kClothes) {
            if (cloth.type == type && cloth.index == index) {
                lua_pushstring(L, cloth.texture);
                lua_pushstring(L, cloth.model);
                return 2;
            }
        }
    }
    int extraIndex = kClothesCount[type];
    for (const ExtraCloth& cloth : g_extraClothes) {
        if (cloth.type != type) continue;
        if (extraIndex == index) {
            lua_pushstring(L, cloth.texture.c_str());
            lua_pushstring(L, cloth.model.c_str());
            return 2;
        }
        ++extraIndex;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_getTypeIndexFromClothes(lua_State* L) {
    // CStaticFunctionDefinitions::GetTypeIndexFromClothes (2775-2794). An empty model matches the texture only.
    const char* texture = lua_tostring(L, 1);
    if (!texture) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* model = lua_tostring(L, 2);
    if (model && model[0] == '\0') model = nullptr;
    for (const Cloth& cloth : kClothes) {
        if (std::strcmp(cloth.texture, texture) != 0) continue;
        if (model && std::strcmp(cloth.model, model) != 0) continue;
        lua_pushinteger(L, cloth.type);
        lua_pushinteger(L, cloth.index);
        return 2;
    }
    for (const ExtraCloth& cloth : g_extraClothes) {
        if (!SameText(cloth.texture.c_str(), texture)) continue;
        if (model && !SameText(cloth.model.c_str(), model)) continue;
        int index = kClothesCount[cloth.type];
        for (const ExtraCloth& earlier : g_extraClothes) {
            if (&earlier == &cloth) break;
            if (earlier.type == cloth.type) ++index;
        }
        lua_pushinteger(L, cloth.type);
        lua_pushinteger(L, index);
        return 2;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_addClothingModel(lua_State* L) {
    // CClientPlayerClothes::AddClothingModel (550-579). The txd and dff file check is not hooked.
    const char* texture = lua_tostring(L, 1);
    const char* model = lua_tostring(L, 2);
    if (!texture || !model || !lua_isnumber(L, 3)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int type = static_cast<int>(lua_tointeger(L, 3));
    if (type < 0 || type >= static_cast<int>(sizeof(kClothesCount) / sizeof(kClothesCount[0])) || texture[0] == '\0' ||
        model[0] == '\0') {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (const Cloth& cloth : kClothes) {
        if (cloth.type == type && SameText(cloth.texture, texture) && SameText(cloth.model, model)) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    for (const ExtraCloth& cloth : g_extraClothes) {
        if (cloth.type == type && SameText(cloth.texture.c_str(), texture) && SameText(cloth.model.c_str(), model)) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    g_extraClothes.push_back({texture, model, type});
    if (!g_diagClothes) {
        g_diagClothes = true;
        LUA_LOGE("DIAG addClothingModel: the clothes txd and dff check is not hooked; the pair is stored");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_removeClothingModel(lua_State* L) {
    const char* texture = lua_tostring(L, 1);
    const char* model = lua_tostring(L, 2);
    if (!texture || !model || !lua_isnumber(L, 3)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int type = static_cast<int>(lua_tointeger(L, 3));
    for (auto it = g_extraClothes.begin(); it != g_extraClothes.end(); ++it) {
        if (it->type == type && SameText(it->texture.c_str(), texture) && SameText(it->model.c_str(), model)) {
            g_extraClothes.erase(it);
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

}  // namespace

void InstallClothesLua(lua_State* L) {
    lua_pushcfunction(L, l_getClothesByTypeIndex);
    lua_setglobal(L, "getClothesByTypeIndex");
    lua_pushcfunction(L, l_getTypeIndexFromClothes);
    lua_setglobal(L, "getTypeIndexFromClothes");
    lua_pushcfunction(L, l_addClothingModel);
    lua_setglobal(L, "addClothingModel");
    lua_pushcfunction(L, l_removeClothingModel);
    lua_setglobal(L, "removeClothingModel");
}

}  // namespace WorldRPG::Script
