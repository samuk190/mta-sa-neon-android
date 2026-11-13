#include "LuaJsonApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ResourceLoader.h"
#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace WorldRPG::Script {

namespace {

// json-c tokenizer default depth (JSON_TOKENER_DEFAULT_DEPTH): deeper input fails on desktop too.
constexpr int kMaxParseDepth = 32;
// Writer guard only: desktop has no depth limit, but a cyclic table is written as "^T^n" first.
constexpr int kMaxWriteDepth = 256;
constexpr double kMaxExactInteger = 9007199254740992.0;  // 2^53

// Same test as LuaVMStub's l_isElement: an id the phone knows as an element.
bool IsKnownElement(uint32_t id) {
    if (id == 0) return false;
    if (id == ServerWorld::LocalPlayer() || id == ServerWorld::LastResourceRoot()) return true;
    float x = 0, y = 0, z = 0;
    return ServerWorld::ElementPose(id, x, y, z) || Game::CopyRemotePose(id, x, y, z) || ServerWorld::ActorHosted(id);
}

// Element tables are {id = <id>, element = true} (LuaApiExtra.h).
bool IsElementTable(lua_State* L, int index) {
    if (!lua_istable(L, index)) return false;
    lua_getfield(L, index, "element");
    const bool element = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return element;
}

// ---------------------------------------------------------------------------
// Decoding: JSON text -> Node tree -> Lua values.

struct Node {
    enum class Kind : uint8_t { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;                // String
    std::vector<Node> items;         // Array
    std::vector<std::string> keys;   // Object: keys[i] names values[i]
    std::vector<Node> values;        // Object
};

class Parser {
public:
    explicit Parser(std::string_view text) : p_(text.data()), end_(text.data() + text.size()) {}

    bool ParseDocument(Node& out) {
        SkipSpace();
        if (!ParseValue(out, 0)) return false;
        SkipSpace();
        return p_ == end_;
    }

private:
    void SkipSpace() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
    }

    bool Literal(const char* word) {
        const size_t n = std::strlen(word);
        if (static_cast<size_t>(end_ - p_) < n || std::strncmp(p_, word, n) != 0) return false;
        p_ += n;
        return true;
    }

    bool ParseValue(Node& out, int depth) {
        if (p_ >= end_) return false;
        switch (*p_) {
            case '{': return ParseObject(out, depth);
            case '[': return ParseArray(out, depth);
            case '"':
                out.kind = Node::Kind::String;
                return ParseString(out.text);
            case 't':
                out.kind = Node::Kind::Bool;
                out.boolean = true;
                return Literal("true");
            case 'f':
                out.kind = Node::Kind::Bool;
                out.boolean = false;
                return Literal("false");
            case 'n':
                out.kind = Node::Kind::Null;
                return Literal("null");
            default:
                return ParseNumber(out);
        }
    }

    bool ParseObject(Node& out, int depth) {
        if (depth >= kMaxParseDepth) return false;
        out.kind = Node::Kind::Object;
        ++p_;
        SkipSpace();
        if (p_ < end_ && *p_ == '}') {
            ++p_;
            return true;
        }
        while (true) {
            SkipSpace();
            if (p_ >= end_ || *p_ != '"') return false;
            std::string key;
            if (!ParseString(key)) return false;
            SkipSpace();
            if (p_ >= end_ || *p_ != ':') return false;
            ++p_;
            SkipSpace();
            Node child;
            if (!ParseValue(child, depth + 1)) return false;
            out.keys.push_back(std::move(key));
            out.values.push_back(std::move(child));
            SkipSpace();
            if (p_ >= end_) return false;
            if (*p_ == ',') {
                ++p_;
                continue;
            }
            if (*p_ == '}') {
                ++p_;
                return true;
            }
            return false;
        }
    }

    bool ParseArray(Node& out, int depth) {
        if (depth >= kMaxParseDepth) return false;
        out.kind = Node::Kind::Array;
        ++p_;
        SkipSpace();
        if (p_ < end_ && *p_ == ']') {
            ++p_;
            return true;
        }
        while (true) {
            SkipSpace();
            Node child;
            if (!ParseValue(child, depth + 1)) return false;
            out.items.push_back(std::move(child));
            SkipSpace();
            if (p_ >= end_) return false;
            if (*p_ == ',') {
                ++p_;
                continue;
            }
            if (*p_ == ']') {
                ++p_;
                return true;
            }
            return false;
        }
    }

    bool ParseNumber(Node& out) {
        const char* start = p_;
        if (p_ < end_ && *p_ == '-') ++p_;
        if (p_ >= end_ || !IsDigit(*p_)) return false;
        if (*p_ == '0') {
            ++p_;
        } else {
            while (p_ < end_ && IsDigit(*p_)) ++p_;
        }
        if (p_ < end_ && *p_ == '.') {
            ++p_;
            if (p_ >= end_ || !IsDigit(*p_)) return false;
            while (p_ < end_ && IsDigit(*p_)) ++p_;
        }
        if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
            ++p_;
            if (p_ < end_ && (*p_ == '+' || *p_ == '-')) ++p_;
            if (p_ >= end_ || !IsDigit(*p_)) return false;
            while (p_ < end_ && IsDigit(*p_)) ++p_;
        }
        const std::string token(start, p_);
        out.kind = Node::Kind::Number;
        out.number = std::strtod(token.c_str(), nullptr);
        return true;
    }

    static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

    static int HexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool ReadHex4(uint32_t& out) {
        if (end_ - p_ < 4) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = HexValue(p_[i]);
            if (digit < 0) return false;
            out = (out << 4) | static_cast<uint32_t>(digit);
        }
        p_ += 4;
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    // p_ is on the opening quote.
    bool ParseString(std::string& out) {
        ++p_;
        out.clear();
        while (p_ < end_) {
            const unsigned char c = static_cast<unsigned char>(*p_++);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (p_ >= end_) return false;
            const char escape = *p_++;
            switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!ReadHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        uint32_t low = 0;
                        if (end_ - p_ < 2 || p_[0] != '\\' || p_[1] != 'u') return false;
                        p_ += 2;
                        if (!ReadHex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;  // unterminated
    }

    const char* p_;
    const char* end_;
};

// Pushes Node values as Lua values. Tables are numbered in the order desktop
// ReadFromJSON* registers them (object or array, before its children), so "^T^n"
// refers to the same table. The numbered tables live in a helper table at knownIndex.
class LuaBuilder {
public:
    LuaBuilder(lua_State* L, int knownIndex) : L_(L), known_(knownIndex) {}

    void Push(const Node& node) {
        switch (node.kind) {
            case Node::Kind::Null: lua_pushnil(L_); return;
            case Node::Kind::Bool: lua_pushboolean(L_, node.boolean ? 1 : 0); return;
            case Node::Kind::Number: PushNumber(node.number); return;
            case Node::Kind::String: PushString(node.text); return;
            case Node::Kind::Array:
                lua_newtable(L_);
                Register();
                for (size_t i = 0; i < node.items.size(); ++i) {
                    Push(node.items[i]);
                    if (lua_isnil(L_, -1)) lua_pop(L_, 1);
                    else lua_rawseti(L_, -2, static_cast<lua_Integer>(i + 1));
                }
                return;
            case Node::Kind::Object:
                lua_newtable(L_);
                Register();
                for (size_t i = 0; i < node.keys.size(); ++i) {
                    lua_pushlstring(L_, node.keys[i].data(), node.keys[i].size());
                    Push(node.values[i]);
                    lua_settable(L_, -3);  // a nil value simply leaves the key unset
                }
                return;
        }
    }

private:
    // The table on top of the stack gets the next table number (it is left on the stack).
    void Register() {
        lua_pushvalue(L_, -1);
        lua_rawseti(L_, known_, static_cast<lua_Integer>(++tables_));
    }

    // Desktop: integral values are stored as numbers that print as integers (Lua 5.1 has one number type).
    void PushNumber(double value) {
        if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) <= kMaxExactInteger) {
            lua_pushinteger(L_, static_cast<lua_Integer>(value));
        } else {
            lua_pushnumber(L_, value);
        }
    }

    void PushString(const std::string& text) {
        if (text.size() > 3 && text[0] == '^' && text[2] == '^' && text[1] != '^') {
            const char* digits = text.c_str() + 3;
            switch (text[1]) {
                case 'E': {  // element id
                    const uint32_t id = static_cast<uint32_t>(std::strtoul(digits, nullptr, 10));
                    if (IsKnownElement(id)) PushElement(L_, id);
                    else lua_pushnil(L_);
                    return;
                }
                case 'R': {  // resource: the phone represents a resource by its root element
                    const uint32_t root = ResourceLoader::RootElementOf(text.substr(3));
                    if (root != 0) PushElement(L_, root);
                    else lua_pushnil(L_);
                    return;
                }
                case 'T': {  // table reference
                    const unsigned long index = std::strtoul(digits, nullptr, 10);
                    if (index < tables_) {
                        lua_rawgeti(L_, known_, static_cast<lua_Integer>(index + 1));
                    } else {
                        LUA_LOGE("fromJSON: invalid table reference '%s'", text.c_str());
                        lua_pushnil(L_);
                    }
                    return;
                }
                default:
                    break;  // any other "^x^" text is an ordinary string
            }
        }
        lua_pushlstring(L_, text.data(), text.size());
    }

    lua_State* L_;
    int known_;
    unsigned long tables_ = 0;
};

// fromJSON(text): a top-level array returns one value per element (desktop
// CLuaArguments::ReadFromJSONString); a top-level object returns one table. Anything
// else, or invalid JSON, returns nil.
int FromJson(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        lua_pushnil(L);
        return 1;
    }
    size_t length = 0;
    const char* raw = lua_tolstring(L, 1, &length);
    const std::string_view text(raw, length);
    const size_t first = text.find_first_not_of(" \t\r\n\v\f");
    if (first == std::string_view::npos || (text[first] != '[' && text[first] != '{')) {
        lua_pushnil(L);
        return 1;
    }
    Node root;
    if (!Parser(text).ParseDocument(root)) {
        LUA_LOGE("fromJSON: input is not valid JSON");
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    const int known = lua_gettop(L);
    LuaBuilder builder(L, known);
    if (root.kind == Node::Kind::Array) {
        luaL_checkstack(L, static_cast<int>(root.items.size()) + 4, "fromJSON");
        for (const Node& item : root.items) builder.Push(item);
        lua_remove(L, known);
        return static_cast<int>(root.items.size());
    }
    builder.Push(root);
    lua_remove(L, known);
    return 1;
}

// ---------------------------------------------------------------------------
// Encoding: Lua values -> JSON text.

class JsonWriter {
public:
    JsonWriter(lua_State* L, bool compact) : L_(L), compact_(compact) {}

    // Desktop WriteToJSONArray: the argument is wrapped in an array, so toJSON(v) is "[ v ]".
    // A value that cannot be written is left out, which gives "[ ]".
    std::string WriteWrapped(int index) {
        out_ = "[";
        const size_t mark = out_.size();
        if (!compact_) out_ += ' ';
        if (!WriteValue(index)) out_.resize(mark);
        out_ += compact_ ? "]" : " ]";
        return out_;
    }

private:
    // Returns false, with nothing appended, when the value has no JSON form.
    bool WriteValue(int index) {
        index = lua_absindex(L_, index);
        switch (lua_type(L_, index)) {
            case LUA_TNIL:
                out_ += "0";  // desktop: nil is written as the integer 0
                return true;
            case LUA_TBOOLEAN:
                out_ += lua_toboolean(L_, index) ? "true" : "false";
                return true;
            case LUA_TNUMBER:
                AppendNumber(lua_tonumber(L_, index));
                return true;
            case LUA_TSTRING: {
                size_t length = 0;
                const char* raw = lua_tolstring(L_, index, &length);
                std::string text(raw, length);
                // Desktop: a string that looks like an element, resource or table tag is escaped
                // ("^E^5" -> "^E~5") so it cannot be read back as one.
                if (text.size() > 3 && text[0] == '^' && text[2] == '^' && text[1] != '^') text[2] = '~';
                AppendJsonString(text);
                return true;
            }
            case LUA_TTABLE:
                return WriteTable(index);
            default:
                LUA_LOGE("toJSON: unsupported value type '%s'", lua_typename(L_, lua_type(L_, index)));
                return false;
        }
    }

    bool WriteTable(int index) {
        if (IsElementTable(L_, index)) {
            // Desktop: elements are only written for serialisation, not for toJSON.
            LUA_LOGE("toJSON: elements are not allowed, only tables, strings, numbers and booleans");
            return false;
        }
        if (depth_ >= kMaxWriteDepth) {
            LUA_LOGE("toJSON: table nested too deep");
            return false;
        }
        const void* identity = lua_topointer(L_, index);
        if (auto known = known_.find(identity); known != known_.end()) {
            AppendJsonString("^T^" + std::to_string(known->second));
            return true;
        }
        known_.emplace(identity, known_.size());
        ++depth_;
        const bool written = WriteTableBody(index);
        --depth_;
        return written;
    }

    // Copies the pairs into a helper table first, so the nested writes cannot disturb lua_next.
    bool WriteTableBody(int index) {
        lua_newtable(L_);
        const int pairs = lua_gettop(L_);
        int count = 0;
        lua_pushnil(L_);
        while (lua_next(L_, index) != 0) {
            lua_pushvalue(L_, -2);
            lua_rawseti(L_, pairs, 2 * count + 1);
            lua_pushvalue(L_, -1);
            lua_rawseti(L_, pairs, 2 * count + 2);
            lua_pop(L_, 1);
            ++count;
        }

        // Desktop WriteTableToJSONObject: an array when every key is a number and the sorted keys run 1..n.
        std::vector<std::pair<uint32_t, int>> order;  // (key, pair index)
        bool isArray = true;
        for (int i = 0; i < count && isArray; ++i) {
            lua_rawgeti(L_, pairs, 2 * i + 1);
            if (lua_type(L_, -1) == LUA_TNUMBER) order.emplace_back(ArrayKey(lua_tonumber(L_, -1)), i);
            else isArray = false;
            lua_pop(L_, 1);
        }
        if (isArray && !order.empty()) {
            std::sort(order.begin(), order.end());
            isArray = order.front().first == 1 && order.back().first == order.size();
        }

        if (isArray) {
            WriteArray(pairs, order);
        } else {
            WriteObject(pairs, count);
        }
        lua_settop(L_, pairs - 1);
        return true;
    }

    static uint32_t ArrayKey(double key) {
        return (key >= 0.0 && key < 4294967296.0) ? static_cast<uint32_t>(key) : 0u;
    }

    void WriteArray(int pairs, const std::vector<std::pair<uint32_t, int>>& order) {
        out_ += '[';
        for (size_t i = 0; i < order.size(); ++i) {
            const size_t mark = out_.size();
            if (i > 0) out_ += ',';
            if (!compact_) out_ += ' ';
            lua_rawgeti(L_, pairs, 2 * order[i].second + 2);
            const bool ok = WriteValue(lua_gettop(L_));
            lua_pop(L_, 1);
            if (!ok) {
                out_.resize(mark);
                break;
            }
        }
        out_ += compact_ ? "]" : " ]";
    }

    void WriteObject(int pairs, int count) {
        out_ += '{';
        bool any = false;
        for (int i = 0; i < count; ++i) {
            const size_t mark = out_.size();
            if (any) out_ += ',';
            if (!compact_) out_ += ' ';
            lua_rawgeti(L_, pairs, 2 * i + 1);
            std::string key;
            const bool keyOk = KeyText(lua_gettop(L_), key);
            lua_pop(L_, 1);
            if (!keyOk) {
                out_.resize(mark);
                break;
            }
            AppendJsonString(key);
            out_ += compact_ ? ":" : ": ";
            lua_rawgeti(L_, pairs, 2 * i + 2);
            const bool ok = WriteValue(lua_gettop(L_));
            lua_pop(L_, 1);
            if (!ok) {
                out_.resize(mark);
                break;
            }
            any = true;
        }
        out_ += compact_ ? "}" : " }";
    }

    // Desktop CLuaArgument::WriteToString, used for object keys.
    bool KeyText(int index, std::string& key) {
        switch (lua_type(L_, index)) {
            case LUA_TSTRING: {
                size_t length = 0;
                const char* raw = lua_tolstring(L_, index, &length);
                key.assign(raw, length);
                return true;
            }
            case LUA_TBOOLEAN:
                key = lua_toboolean(L_, index) ? "true" : "false";
                return true;
            case LUA_TNUMBER: {
                const double value = lua_tonumber(L_, index);
                char buffer[64];
                if (IsJsonInt(value)) std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(value));
                else std::snprintf(buffer, sizeof(buffer), "%f", value);
                key = buffer;
                return true;
            }
            case LUA_TTABLE:
                if (!IsElementTable(L_, index)) return false;
                key = "#E#" + std::to_string(ElementIdOf(L_, index));
                return true;
            default:
                return false;
        }
    }

    static bool IsJsonInt(double value) {
        return std::isfinite(value) && value == std::floor(value) && std::fabs(value) <= 2147483647.0;
    }

    void AppendNumber(double value) {
        char buffer[64];
        if (IsJsonInt(value)) std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(value));
        else if (std::isfinite(value)) std::snprintf(buffer, sizeof(buffer), "%.17g", value);
        else std::snprintf(buffer, sizeof(buffer), "null");
        out_ += buffer;
    }

    // json-c escaping: \b \n \r \t \f \" \\ and "/" are escaped, other controls as \u00xx.
    void AppendJsonString(const std::string& text) {
        static const char kHex[] = "0123456789abcdef";
        out_ += '"';
        for (unsigned char c : text) {
            switch (c) {
                case '\b': out_ += "\\b"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                case '\f': out_ += "\\f"; break;
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '/': out_ += "\\/"; break;
                default:
                    if (c < 0x20) {
                        out_ += "\\u00";
                        out_ += kHex[c >> 4];
                        out_ += kHex[c & 0x0F];
                    } else {
                        out_ += static_cast<char>(c);
                    }
            }
        }
        out_ += '"';
    }

    lua_State* L_;
    bool compact_;
    std::string out_;
    std::unordered_map<const void*, size_t> known_;
    int depth_ = 0;
};

// toJSON(value [, compact]): nil for a nil value or a non-boolean compact flag.
int ToJson(lua_State* L) {
    if (lua_isnoneornil(L, 1)) {
        lua_pushnil(L);
        return 1;
    }
    bool compact = false;
    if (!lua_isnoneornil(L, 2)) {
        if (!lua_isboolean(L, 2)) {
            lua_pushnil(L);
            return 1;
        }
        compact = lua_toboolean(L, 2) != 0;
    }
    JsonWriter writer(L, compact);
    const std::string json = writer.WriteWrapped(1);
    lua_pushlstring(L, json.data(), json.size());
    return 1;
}

} // namespace

void InstallJsonBindings(lua_State* L) {
    lua_pushcfunction(L, FromJson);
    lua_setglobal(L, "fromJSON");
    lua_pushcfunction(L, ToJson);
    lua_setglobal(L, "toJSON");
}

} // namespace WorldRPG::Script
