#include "LuaXmlApi.h"

#include "LuaVMStub.h"

#include "lua.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace WorldRPG::Script {
namespace {

namespace fs = std::filesystem;

struct XmlNode {
    int id = 0;
    int parent = 0;
    int root = 0;
    std::string name;
    std::string value;
    std::string path;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<int> children;
    bool alive = true;
};

std::map<int, XmlNode> g_nodes;
int g_next = 1;

XmlNode* NodeOf(lua_State* L, int index = 1) {
    if (!lua_isnumber(L, index)) return nullptr;
    const auto found = g_nodes.find(static_cast<int>(lua_tointeger(L, index)));
    if (found == g_nodes.end() || !found->second.alive) return nullptr;
    return &found->second;
}

int AddNode(XmlNode node) {
    node.id = g_next++;
    if (node.root == 0) node.root = node.id;
    const int id = node.id;
    g_nodes.emplace(id, std::move(node));
    return id;
}

void KillTree(int id) {
    const auto found = g_nodes.find(id);
    if (found == g_nodes.end() || !found->second.alive) return;
    found->second.alive = false;
    for (int child : found->second.children) KillTree(child);
}

fs::path Root() {
#if defined(__ANDROID__)
    return "/data/data/com.worldrpg.neonlauncher/files/lua";
#else
    return "/tmp/neon-lua-files";
#endif
}

bool Resolve(const char* text, fs::path& out) {
    if (!text || text[0] == '\0' || text[0] == '/' || text[0] == '\\') return false;
    const std::string raw(text);
    if (raw.find("..") != std::string::npos) return false;
    std::error_code error;
    fs::create_directories(Root(), error);
    out = Root() / raw;
    const std::string rootText = Root().string();
    return out.string().compare(0, rootText.size(), rootText) == 0;
}

void SkipSpace(const std::string& text, std::size_t& i) {
    while (i < text.size()) {
        if (text.compare(i, 4, "<!--") == 0) {
            const std::size_t end = text.find("-->", i + 4);
            if (end == std::string::npos) { i = text.size(); return; }
            i = end + 3;
            continue;
        }
        if (text.compare(i, 2, "<?") == 0) {
            const std::size_t end = text.find("?>", i + 2);
            if (end == std::string::npos) { i = text.size(); return; }
            i = end + 2;
            continue;
        }
        if (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r') { ++i; continue; }
        break;
    }
}

std::string Unescape(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') { out.push_back(text[i]); continue; }
        if (text.compare(i, 4, "&lt;") == 0) { out.push_back('<'); i += 3; }
        else if (text.compare(i, 4, "&gt;") == 0) { out.push_back('>'); i += 3; }
        else if (text.compare(i, 5, "&amp;") == 0) { out.push_back('&'); i += 4; }
        else if (text.compare(i, 6, "&quot;") == 0) { out.push_back('"'); i += 5; }
        else out.push_back(text[i]);
    }
    return out;
}

std::string Escape(const std::string& text) {
    std::string out;
    for (char ch : text) {
        if (ch == '&') out += "&amp;";
        else if (ch == '<') out += "&lt;";
        else if (ch == '>') out += "&gt;";
        else if (ch == '"') out += "&quot;";
        else out.push_back(ch);
    }
    return out;
}

bool ParseNode(const std::string& text, std::size_t& i, int parent, int root, int& created);

bool ParseChildren(const std::string& text, std::size_t& i, int parent, int root) {
    std::string textValue;
    while (i < text.size()) {
        SkipSpace(text, i);
        if (i >= text.size()) return false;
        if (text.compare(i, 2, "</") == 0) {
            g_nodes[parent].value = Unescape(textValue);
            return true;
        }
        if (text[i] != '<') {
            const std::size_t start = i;
            while (i < text.size() && text[i] != '<') ++i;
            textValue.append(text, start, i - start);
            continue;
        }
        int child = 0;
        if (!ParseNode(text, i, parent, root, child)) return false;
        g_nodes[parent].children.push_back(child);
    }
    return false;
}

bool ParseNode(const std::string& text, std::size_t& i, int parent, int root, int& created) {
    SkipSpace(text, i);
    if (i >= text.size() || text[i] != '<') return false;
    ++i;
    const std::size_t nameStart = i;
    while (i < text.size() && text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '/' && text[i] != '>') ++i;
    if (i == nameStart) return false;
    XmlNode node;
    node.parent = parent;
    node.root = root;
    node.name = text.substr(nameStart, i - nameStart);
    while (i < text.size() && text[i] != '>' && text[i] != '/') {
        SkipSpace(text, i);
        if (i < text.size() && (text[i] == '>' || text[i] == '/')) break;
        const std::size_t attrStart = i;
        while (i < text.size() && text[i] != '=' && text[i] != ' ' && text[i] != '>') ++i;
        const std::string attr = text.substr(attrStart, i - attrStart);
        SkipSpace(text, i);
        if (i >= text.size() || text[i] != '=') return false;
        ++i;
        SkipSpace(text, i);
        if (i >= text.size() || (text[i] != '"' && text[i] != '\'')) return false;
        const char quote = text[i++];
        const std::size_t valueStart = i;
        while (i < text.size() && text[i] != quote) ++i;
        if (i >= text.size()) return false;
        node.attrs.emplace_back(attr, Unescape(text.substr(valueStart, i - valueStart)));
        ++i;
    }
    const bool empty = i < text.size() && text[i] == '/';
    if (empty) ++i;
    if (i >= text.size() || text[i] != '>') return false;
    ++i;
    created = AddNode(std::move(node));
    if (root == 0) g_nodes[created].root = created;
    if (empty) return true;
    if (!ParseChildren(text, i, created, g_nodes[created].root)) {
        KillTree(created);
        return false;
    }
    SkipSpace(text, i);
    if (text.compare(i, 2, "</") != 0 ||
        text.compare(i + 2, g_nodes[created].name.size(), g_nodes[created].name) != 0) {
        KillTree(created);
        return false;
    }
    i += 2 + g_nodes[created].name.size();
    SkipSpace(text, i);
    if (i >= text.size() || text[i] != '>') return false;
    ++i;
    return true;
}

std::string Serialize(int id) {
    const XmlNode& node = g_nodes[id];
    std::string out = "<" + node.name;
    for (const auto& attr : node.attrs) out += " " + attr.first + "=\"" + Escape(attr.second) + "\"";
    if (node.children.empty() && node.value.empty()) return out + "/>";
    out.push_back('>');
    out += Escape(node.value);
    for (int child : node.children)
        if (g_nodes[child].alive) out += Serialize(child);
    out += "</" + node.name + ">";
    return out;
}

int l_xmlLoadString(lua_State* L) {
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::size_t index = 0;
    int created = 0;
    if (!ParseNode(std::string(text, length), index, 0, 0, created)) {
        if (created) KillTree(g_nodes[created].root ? g_nodes[created].root : created);
        LUA_LOGE("xmlLoadString: unable to load XML string");
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, created);
    return 1;
}

int l_xmlCreateFile(lua_State* L) {
    fs::path path;
    const char* name = lua_tostring(L, 2);
    if (!Resolve(lua_tostring(L, 1), path) || !name || name[0] == '\0') {
        lua_pushboolean(L, 0);
        return 1;
    }
    XmlNode node;
    node.name = name;
    node.path = path.string();
    lua_pushinteger(L, AddNode(std::move(node)));
    return 1;
}

int l_xmlLoadFile(lua_State* L) {
    fs::path path;
    if (!Resolve(lua_tostring(L, 1), path)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    lua_pushlstring(L, text.data(), text.size());
    lua_remove(L, 1);
    const int results = l_xmlLoadString(L);
    if (results == 1 && lua_isnumber(L, -1)) g_nodes[static_cast<int>(lua_tointeger(L, -1))].path = path.string();
    return results;
}

int l_xmlCreateChild(lua_State* L) {
    XmlNode* parent = NodeOf(L);
    const char* name = lua_tostring(L, 2);
    if (!parent || !name || name[0] == '\0') {
        lua_pushboolean(L, 0);
        return 1;
    }
    XmlNode child;
    child.parent = parent->id;
    child.root = parent->root;
    child.name = name;
    const int id = AddNode(std::move(child));
    g_nodes[parent->id].children.push_back(id);
    lua_pushinteger(L, id);
    return 1;
}

int l_xmlDestroyNode(lua_State* L) {
    XmlNode* node = NodeOf(L);
    if (!node) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (node->parent != 0) {
        auto& siblings = g_nodes[node->parent].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), node->id), siblings.end());
    }
    KillTree(node->id);
    lua_pushboolean(L, 1);
    return 1;
}

int l_xmlFindChild(lua_State* L) {
    XmlNode* node = NodeOf(L);
    const char* name = lua_tostring(L, 2);
    if (!node || !name || !lua_isnumber(L, 3)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int seen = 0;
    const int wanted = static_cast<int>(lua_tointeger(L, 3));
    for (int child : node->children) {
        XmlNode& item = g_nodes[child];
        if (!item.alive || item.name != name) continue;
        if (seen++ == wanted) {
            lua_pushinteger(L, child);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_xmlNodeGetChildren(lua_State* L) {
    XmlNode* node = NodeOf(L);
    if (!node) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (lua_isnumber(L, 2) && lua_tointeger(L, 2) != -1) {
        const int index = static_cast<int>(lua_tointeger(L, 2));
        int seen = 0;
        for (int child : node->children) {
            if (!g_nodes[child].alive) continue;
            if (seen++ == index) {
                lua_pushinteger(L, child);
                return 1;
            }
        }
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_newtable(L);
    int index = 0;
    for (int child : node->children) {
        if (!g_nodes[child].alive) continue;
        lua_pushinteger(L, ++index);
        lua_pushinteger(L, child);
        lua_settable(L, -3);
    }
    return 1;
}

int l_xmlNodeGetParent(lua_State* L) {
    XmlNode* node = NodeOf(L);
    if (!node || node->parent == 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, node->parent);
    return 1;
}

int l_xmlNodeGetName(lua_State* L) {
    XmlNode* node = NodeOf(L);
    if (!node) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, node->name.c_str());
    return 1;
}

int l_xmlNodeSetName(lua_State* L) {
    XmlNode* node = NodeOf(L);
    const char* name = lua_tostring(L, 2);
    if (!node || !name || name[0] == '\0') {
        lua_pushboolean(L, 0);
        return 1;
    }
    node->name = name;
    lua_pushboolean(L, 1);
    return 1;
}

int l_xmlNodeGetValue(lua_State* L) {
    XmlNode* node = NodeOf(L);
    if (!node) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushstring(L, node->value.c_str());
    return 1;
}

int l_xmlNodeSetValue(lua_State* L) {
    XmlNode* node = NodeOf(L);
    const char* value = lua_tostring(L, 2);
    if (!node || !value) {
        lua_pushboolean(L, 0);
        return 1;
    }
    node->value = value;
    lua_pushboolean(L, 1);
    return 1;
}

int l_xmlNodeGetAttribute(lua_State* L) {
    XmlNode* node = NodeOf(L);
    const char* name = lua_tostring(L, 2);
    if (!node || !name) {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (const auto& attr : node->attrs) {
        if (attr.first == name) {
            lua_pushstring(L, attr.second.c_str());
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_xmlNodeGetAttributes(lua_State* L) {
    XmlNode* node = NodeOf(L);
    if (!node) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_newtable(L);
    for (const auto& attr : node->attrs) {
        lua_pushstring(L, attr.second.c_str());
        lua_setfield(L, -2, attr.first.c_str());
    }
    return 1;
}

int l_xmlNodeSetAttribute(lua_State* L) {
    XmlNode* node = NodeOf(L);
    const char* name = lua_tostring(L, 2);
    if (!node || !name) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool remove = lua_isnoneornil(L, 3);
    const char* value = remove ? "" : lua_tostring(L, 3);
    if (!remove && !value) {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (auto it = node->attrs.begin(); it != node->attrs.end(); ++it) {
        if (it->first == name) {
            if (remove) node->attrs.erase(it);
            else it->second = value;
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    if (!remove) node->attrs.emplace_back(name, value);
    lua_pushboolean(L, 1);
    return 1;
}

bool WriteNode(int id, const fs::path& path) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << Serialize(id);
    return static_cast<bool>(output);
}

int l_xmlSaveFile(lua_State* L) {
    XmlNode* node = NodeOf(L);
    if (!node) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const XmlNode& root = g_nodes[node->root];
    fs::path path;
    if (lua_isstring(L, 2)) {
        if (!Resolve(lua_tostring(L, 2), path)) {
            lua_pushboolean(L, 0);
            return 1;
        }
    } else if (!root.path.empty()) {
        path = root.path;
    } else {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, WriteNode(node->root, path) ? 1 : 0);
    return 1;
}

int l_xmlCopyFile(lua_State* L) {
    XmlNode* node = NodeOf(L);
    fs::path path;
    if (!node || !Resolve(lua_tostring(L, 2), path)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, WriteNode(node->root, path) ? 1 : 0);
    return 1;
}

int l_xmlUnloadFile(lua_State* L) {
    XmlNode* node = NodeOf(L);
    if (!node) {
        lua_pushboolean(L, 0);
        return 1;
    }
    KillTree(node->root);
    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

int XmlParseString(const std::string& text) {
    std::size_t index = 0;
    int created = 0;
    if (!ParseNode(text, index, 0, 0, created)) {
        if (created) KillTree(g_nodes[created].root ? g_nodes[created].root : created);
        return 0;
    }
    return created;
}

bool XmlNodeString(lua_State* L, int index, std::string& out) {
    const XmlNode* node = NodeOf(L, index);
    if (!node) return false;
    out = Serialize(node->id);
    return true;
}

bool XmlValidateString(const std::string& text) {
    // Parsing is synchronous and invokes no Lua callbacks. Discard every node
    // made by this validation, including dead nodes left by a failed parse.
    const int first = g_next;
    const bool valid = XmlParseString(text) != 0;
    g_nodes.erase(g_nodes.lower_bound(first), g_nodes.end());
    return valid;
}

void InstallXmlLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"xmlCreateFile", l_xmlCreateFile},
        {"xmlLoadFile", l_xmlLoadFile},
        {"xmlLoadString", l_xmlLoadString},
        {"xmlCopyFile", l_xmlCopyFile},
        {"xmlSaveFile", l_xmlSaveFile},
        {"xmlUnloadFile", l_xmlUnloadFile},
        {"xmlCreateChild", l_xmlCreateChild},
        {"xmlDestroyNode", l_xmlDestroyNode},
        {"xmlFindChild", l_xmlFindChild},
        // CLuaCompatibilityDefs.cpp:66. Same function as xmlFindChild.
        {"xmlNodeFindChild", l_xmlFindChild},
        {"xmlNodeGetChildren", l_xmlNodeGetChildren},
        {"xmlNodeGetParent", l_xmlNodeGetParent},
        {"xmlNodeGetValue", l_xmlNodeGetValue},
        {"xmlNodeGetAttributes", l_xmlNodeGetAttributes},
        {"xmlNodeGetAttribute", l_xmlNodeGetAttribute},
        {"xmlNodeGetName", l_xmlNodeGetName},
        {"xmlNodeSetValue", l_xmlNodeSetValue},
        {"xmlNodeSetAttribute", l_xmlNodeSetAttribute},
        {"xmlNodeSetName", l_xmlNodeSetName},
        {"xmlNodeGetSubNodes", l_xmlNodeGetChildren},
        {"xmlNodeFindSubNode", l_xmlFindChild},
        {"xmlCreateSubNode", l_xmlCreateChild},
        {"xmlFindSubNode", l_xmlFindChild},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
