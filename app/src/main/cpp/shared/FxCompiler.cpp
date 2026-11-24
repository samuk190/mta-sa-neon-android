#include "FxCompiler.h"

#include "../third_party/hlslparser/Engine.h"
#include "../third_party/hlslparser/GLSLGenerator.h"
#include "../third_party/hlslparser/HLSLParser.h"
#include "../third_party/hlslparser/HLSLTree.h"
#undef assert

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>
#include <string_view>

namespace WorldRPG::Fx {
namespace {

constexpr int kGlNearest = 0x2600, kGlLinear = 0x2601;
constexpr int kGlRepeat = 0x2901, kGlClampToEdge = 0x812F, kGlMirroredRepeat = 0x8370;

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string StripComments(const std::string& source) {
    std::string result = source;
    bool block = false, line = false, quote = false;
    for (size_t i = 0; i < result.size(); ++i) {
        const char c = source[i], next = i + 1 < source.size() ? source[i + 1] : 0;
        if (block) {
            if (c == '*' && next == '/') {
                result[i] = result[i + 1] = ' ';
                ++i;
                block = false;
            } else if (c != '\n') {
                result[i] = ' ';
            }
        } else if (line) {
            if (c == '\n') line = false;
            else result[i] = ' ';
        } else if (!quote && c == '/' && (next == '*' || next == '/')) {
            block = next == '*';
            line = next == '/';
            result[i] = result[i + 1] = ' ';
            ++i;
        } else if (c == '"' && (!i || source[i - 1] != '\\')) {
            quote = !quote;
        }
    }
    return result;
}

bool ExpandMacros(std::string& source, const std::map<std::string, std::string>& macros, std::string& diagnostic) {
    auto definitions = macros;
    std::istringstream lines(source);
    std::string line, expanded;
    static const std::regex define(R"(^\s*#\s*define\s+(\w+)\s+(.*?)\s*$)");
    while (std::getline(lines, line)) {
        std::smatch match;
        if (std::regex_match(line, match, define)) {
            if (!definitions.count(match[1])) definitions[match[1]] = match[2];
        } else if (line.find('#') != std::string::npos) {
            diagnostic = "FX preprocessor directive not supported";
            return false;
        } else {
            expanded += line + '\n';
        }
    }
    source = expanded;
    auto append = [&](std::string& out, std::string_view piece) {
        if (piece.size() > kMaxSourceBytes - out.size()) {
            diagnostic = "expanded shader source exceeds the 512 KiB limit";
            return false;
        }
        out.append(piece.data(), piece.size());
        return true;
    };
    if (source.size() > kMaxSourceBytes) {
        diagnostic = "expanded shader source exceeds the 512 KiB limit";
        return false;
    }
    static const std::regex identifierName("[A-Za-z_][A-Za-z_0-9]*");
    for (const auto& [name, value] : definitions) {
        if (!std::regex_match(name, identifierName) || value.size() > 256) {
            diagnostic = "unsupported or invalid FX source";
            return false;
        }
        const std::regex identifier("\\b" + name + "\\b");
        std::string replaced;
        size_t end = 0;
        const std::string_view current(source);
        for (auto it = std::sregex_iterator(source.begin(), source.end(), identifier); it != std::sregex_iterator(); ++it) {
            if (!append(replaced, current.substr(end, it->position() - end)) || !append(replaced, value)) return false;
            end = it->position() + it->length();
        }
        if (!append(replaced, current.substr(end))) return false;
        source = std::move(replaced);
    }
    return true;
}

// `name [: SEMANTIC] < string key = "value"; ... >` on global declarations.
// The block is removed (hlslparser has no annotations) and its strings kept.
void TakeAnnotations(std::string& source, Effect& out) {
    static const std::regex block(R"(\b(\w+)\s*(?::\s*\w+\s*)?(<\s*(?:string|float|int|bool|half)\b[^<>]*>))");
    static const std::regex entry(R"re(\bstring\s+(\w+)\s*=\s*"([^"]*)"\s*;)re");
    std::string result;
    size_t end = 0;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), block); it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        const std::string body = m[2];
        auto& map = out.annotations[Key(m[1])];
        for (auto e = std::sregex_iterator(body.begin(), body.end(), entry); e != std::sregex_iterator(); ++e)
            map[Key((*e)[1])] = (*e)[2];
        const size_t blockStart = m.position(2);
        result.append(source, end, blockStart - end);
        end = blockStart + m.length(2);
    }
    result.append(source, end, std::string::npos);
    source = std::move(result);
}

bool SamplerValue(const std::string& key, const std::string& value, SamplerState& state) {
    int filter = -1, wrap = -1;
    if (value == "linear" || value == "anisotropic" || value == "pyramidalquad" || value == "gaussianquad") filter = kGlLinear;
    else if (value == "point" || value == "none") filter = kGlNearest;
    if (value == "clamp") wrap = kGlClampToEdge;
    else if (value == "wrap") wrap = kGlRepeat;
    else if (value == "mirror") wrap = kGlMirroredRepeat;
    if (key == "maxanisotropy" || key == "maxmiplevel")
        return std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (key == "mipmaplodbias") return true;  // no mip chain on the tile path; GTA keeps its own
    if (key == "minfilter") { if (filter < 0) return false; state.minFilter = filter; return true; }
    if (key == "magfilter") { if (filter < 0) return false; state.magFilter = filter; return true; }
    if (key == "mipfilter") return filter >= 0;
    if (key == "addressu") { if (wrap < 0) return false; state.wrapS = wrap; return true; }
    if (key == "addressv") { if (wrap < 0) return false; state.wrapT = wrap; return true; }
    if (key == "addressw") return wrap >= 0;
    if (key == "srgbtexture") return value == "false" || value == "0";
    return false;
}

bool TakeSamplers(std::string& source, Effect& out, std::string& diagnostic) {
    static const std::regex sampler(R"(\bsampler(?:2D)?\s+(\w+)\s*=\s*sampler_state\s*\{([^{}]*)\}\s*;)", std::regex::icase);
    static const std::regex texture(R"(\bTexture\s*=\s*(?:<\s*(\w+)\s*>|\(\s*(\w+)\s*\))\s*;)", std::regex::icase);
    static const std::regex assignment(R"(\b(\w+)\s*=\s*(\w+)\s*;)");
    std::smatch match;
    while (std::regex_search(source, match, sampler)) {
        const std::string body = match[2];
        std::smatch t;
        if (!std::regex_search(body, t, texture)) {
            diagnostic = "sampler without a texture";
            return false;
        }
        Sampler s;
        s.name = Key(match[1]);
        s.texture = Key(t[1].matched ? t[1].str() : t[2].str());
        const std::string rest = std::regex_replace(body, texture, "");
        for (auto it = std::sregex_iterator(rest.begin(), rest.end(), assignment); it != std::sregex_iterator(); ++it) {
            if (!SamplerValue(Lower((*it)[1]), Lower((*it)[2]), s.state)) {
                diagnostic = "unsupported sampler state";
                return false;
            }
        }
        if (std::regex_replace(rest, assignment, "").find_first_not_of(" \t\r\n") != std::string::npos) {
            diagnostic = "unsupported sampler state";
            return false;
        }
        out.samplers[s.name] = s;
        source.replace(match.position(), match.length(), "sampler2D " + match[1].str() + ";");
    }
    source = std::regex_replace(source, std::regex(R"(\btexture(?:2D)?\s+\w+\s*;)"), "");
    return true;
}

int BlendFactor(const std::string& v) {
    if (v == "zero") return 0;
    if (v == "one") return 1;
    if (v == "srccolor") return 0x0300;
    if (v == "invsrccolor") return 0x0301;
    if (v == "srcalpha") return 0x0302;
    if (v == "invsrcalpha") return 0x0303;
    if (v == "destalpha") return 0x0304;
    if (v == "invdestalpha") return 0x0305;
    if (v == "destcolor") return 0x0306;
    if (v == "invdestcolor") return 0x0307;
    if (v == "srcalphasat") return 0x0308;
    return -1;
}

int Bool(const std::string& v) {
    if (v == "true" || v == "1") return 1;
    if (v == "false" || v == "0") return 0;
    return -1;
}

// One render state of a pass. False for a state that changes the draw and has no port.
bool PassValue(const std::string& key, const std::string& value, PassState& s) {
    if (key == "alphablendenable") return (s.alphaBlend = Bool(value)) >= 0;
    if (key == "srcblend") return (s.srcBlend = BlendFactor(value)) >= 0;
    if (key == "destblend") return (s.dstBlend = BlendFactor(value)) >= 0;
    if (key == "zenable") return (s.zEnable = Bool(value)) >= 0;
    if (key == "zwriteenable") return (s.zWrite = Bool(value)) >= 0;
    if (key == "alphatestenable") return (s.alphaTest = Bool(value)) >= 0;
    if (key == "fogenable") return (s.fog = Bool(value)) >= 0;
    if (key == "cullmode") {
        s.cull = value == "none" || value == "1" ? 0 : value == "cw" || value == "2" ? 1 : value == "ccw" || value == "3" ? 2 : -1;
        return s.cull >= 0;
    }
    if (key == "alpharef") {
        char* end = nullptr;
        const long v = std::strtol(value.c_str(), &end, 0);
        if (!end || *end || v < 0 || v > 255) return false;
        s.alphaRef = static_cast<int>(v);
        return true;
    }
    if (key == "alphafunc") return value == "greater" || value == "greaterequal" || value == "always";
    if (key == "blendop") return value == "add";
    if (key == "lighting" || key == "specularenable" || key == "colorvertex" || key == "normalizenormals")
        return Bool(value) >= 0;  // fixed-function lighting; GTA's own lighting stays
    return false;
}

bool ParseTechniques(std::string& source, Effect& out, std::string& diagnostic) {
    static const std::regex start(R"(\btechnique(?:9|10|11)?\s+(\w+)\s*\{)");
    static const std::regex passStart(R"(\bpass\s*(\w*)\s*\{)");
    static const std::regex pixel(R"(PixelShader\s*=\s*compile\s+ps_\w+\s+(\w+)\s*\(\s*\)\s*;)", std::regex::icase);
    static const std::regex vertex(R"(VertexShader\s*=\s*compile\s+vs_\w+\s+(\w+)\s*\(\s*\)\s*;)", std::regex::icase);
    static const std::regex shaders(R"((?:PixelShader|VertexShader)\s*=\s*compile\s+\w+\s+\w+\s*\(\s*\)\s*;|VertexShader\s*=\s*(?:NULL|0)\s*;)", std::regex::icase);
    static const std::regex state(R"(\b(\w+)\s*=\s*([A-Za-z0-9_.]+)\s*;)");
    std::smatch match;
    size_t from = 0;
    while (from < source.size()) {
        if (!std::regex_search(source.cbegin() + from, source.cend(), match, start)) break;
        const size_t first = from + match.position(), bodyStart = first + match.length();
        size_t end = bodyStart;
        unsigned depth = 1;
        for (; end < source.size() && depth; ++end) {
            if (source[end] == '{') ++depth;
            else if (source[end] == '}') --depth;
        }
        if (depth) {
            diagnostic = "unterminated technique";
            return false;
        }
        Technique technique;
        technique.name = match[1];
        std::string body = source.substr(bodyStart, end - bodyStart - 1);
        std::smatch p;
        bool ok = true;
        while (ok && std::regex_search(body, p, passStart)) {
            const size_t open = p.position() + p.length();
            size_t close = open;
            unsigned d = 1;
            for (; close < body.size() && d; ++close) {
                if (body[close] == '{') ++d;
                else if (body[close] == '}') --d;
            }
            const std::string passBody = body.substr(open, close - open - 1);
            body.erase(0, close);
            Pass pass;
            std::smatch s;
            if (std::regex_search(passBody, s, pixel)) pass.pixelEntry = s[1];
            if (std::regex_search(passBody, s, vertex)) pass.vertexEntry = s[1];
            const std::string states = std::regex_replace(passBody, shaders, "");
            for (auto it = std::sregex_iterator(states.begin(), states.end(), state); it != std::sregex_iterator(); ++it)
                if (!PassValue(Lower((*it)[1]), Lower((*it)[2]), pass.state)) ok = false;
            if (std::regex_replace(states, state, "").find_first_not_of(" \t\r\n") != std::string::npos) ok = false;
            if (pass.pixelEntry.empty()) ok = false;
            technique.passes.push_back(pass);
        }
        // A technique with a state that has no port is skipped, as desktop skips
        // techniques the device cannot validate; the next one is tried.
        if (ok && !technique.passes.empty()) out.techniques.push_back(technique);
        else if (diagnostic.empty()) diagnostic = "unsupported FX pass state";
        std::fill(source.begin() + first, source.begin() + end, ' ');
        from = end;
    }
    if (out.techniques.empty()) {
        if (diagnostic.empty()) diagnostic = "no usable technique";
        return false;
    }
    diagnostic.clear();
    return true;
}

unsigned Width(M4::HLSLBaseType type) {
    using namespace M4;
    switch (type) {
        case HLSLBaseType_Float: case HLSLBaseType_Half: case HLSLBaseType_Int: case HLSLBaseType_Bool: return 1;
        case HLSLBaseType_Float2: case HLSLBaseType_Half2: return 2;
        case HLSLBaseType_Float3: case HLSLBaseType_Half3: return 3;
        case HLSLBaseType_Float4: case HLSLBaseType_Half4: return 4;
        case HLSLBaseType_Float4x4: return 16;
        default: return 0;
    }
}

bool Constant(M4::HLSLTree& tree, M4::HLSLExpression* expression, std::vector<float>& result, unsigned depth = 0) {
    using namespace M4;
    if (!expression || depth > 32) return false;
    if (expression->nodeType == HLSLNodeType_LiteralExpression) {
        auto* literal = static_cast<HLSLLiteralExpression*>(expression);
        switch (expression->expressionType.baseType) {
            case HLSLBaseType_Float: case HLSLBaseType_Half: result = {literal->fValue}; return std::isfinite(literal->fValue);
            case HLSLBaseType_Int: result = {float(literal->iValue)}; return true;
            case HLSLBaseType_Bool: result = {float(literal->bValue)}; return true;
            default: return false;
        }
    }
    if (expression->nodeType == HLSLNodeType_IdentifierExpression) {
        auto* declaration = tree.FindGlobalDeclaration(static_cast<HLSLIdentifierExpression*>(expression)->name);
        return declaration && (declaration->type.flags & HLSLTypeFlag_Const) && Constant(tree, declaration->assignment, result, depth + 1);
    }
    if (expression->nodeType == HLSLNodeType_ConstructorExpression) {
        auto* constructor = static_cast<HLSLConstructorExpression*>(expression);
        for (auto* arg = constructor->argument; arg; arg = arg->nextExpression) {
            std::vector<float> values;
            if (!Constant(tree, arg, values, depth + 1) || result.size() + values.size() > 16) return false;
            result.insert(result.end(), values.begin(), values.end());
        }
        const unsigned width = Width(constructor->type.baseType);
        if (result.size() == 1 && width <= 4) result.resize(width, result[0]);
        return width && result.size() == width;
    }
    if (expression->nodeType == HLSLNodeType_UnaryExpression) {
        auto* unary = static_cast<HLSLUnaryExpression*>(expression);
        if (!Constant(tree, unary->expression, result, depth + 1)) return false;
        if (unary->unaryOp == HLSLUnaryOp_Negative) {
            for (float& value : result) value = -value;
            return true;
        }
        return unary->unaryOp == HLSLUnaryOp_Positive;
    }
    if (expression->nodeType == HLSLNodeType_BinaryExpression) {
        auto* binary = static_cast<HLSLBinaryExpression*>(expression);
        std::vector<float> a, b;
        if (!Constant(tree, binary->expression1, a, depth + 1) || !Constant(tree, binary->expression2, b, depth + 1)) return false;
        if (a.size() == 1) a.resize(b.size(), a[0]);
        if (b.size() == 1) b.resize(a.size(), b[0]);
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            float value;
            switch (binary->binaryOp) {
                case HLSLBinaryOp_Add: value = a[i] + b[i]; break;
                case HLSLBinaryOp_Sub: value = a[i] - b[i]; break;
                case HLSLBinaryOp_Mul: value = a[i] * b[i]; break;
                case HLSLBinaryOp_Div: if (!b[i]) return false; value = a[i] / b[i]; break;
                default: return false;
            }
            if (!std::isfinite(value)) return false;
            result.push_back(value);
        }
        return true;
    }
    return false;
}

thread_local std::string* g_errors = nullptr;
void CollectError(const char* line) {
    if (g_errors && g_errors->size() < 1024) g_errors->append(line);
}

}  // namespace

std::string Key(std::string name) { return Lower(std::move(name)); }

bool Parse(const std::string& original, const std::map<std::string, std::string>& macros, Effect& out,
           std::string& diagnostic) {
    diagnostic = "unsupported or invalid FX source";
    if (original.size() > kMaxSourceBytes || original.find('\0') != std::string::npos) return false;
    std::string source = StripComments(original);
    if (!ExpandMacros(source, macros, diagnostic)) return false;
    TakeAnnotations(source, out);
    {
        std::smatch first;
        static const std::regex texture(R"(\b(?:texture|sampler2D)\s+(\w+)\b)");
        if (std::regex_search(source, first, texture)) out.firstTexture = Key(first[1]);
    }
    if (!TakeSamplers(source, out, diagnostic)) return false;
    diagnostic.clear();
    if (!ParseTechniques(source, out, diagnostic)) return false;
    // Semantics of global parameters (desktop binds engine values by semantic).
    static const std::regex declared(R"(^\s*(?:(?:static|uniform|const|shared|extern)\s+)*\w+\s+(\w+)\s*:\s*(\w+)\s*[;=])");
    std::istringstream lines(source);
    std::string line;
    while (std::getline(lines, line)) {
        std::smatch m;
        if (std::regex_search(line, m, declared)) {
            std::string semantic = m[2];
            for (char& c : semantic) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            out.semantics[Key(m[1])] = semantic;
        }
    }
    out.hlsl = std::move(source);
    return true;
}

bool Translate(const std::string& hlsl, const std::string& entry, const TranslateOptions& options, std::string& glsl,
               std::map<std::string, std::vector<float>>& defaults, std::string& diagnostic) {
    std::string errors;
    g_errors = &errors;
    M4::Log_SetSink(CollectError);
    struct Restore {
        ~Restore() {
            M4::Log_SetSink(nullptr);
            g_errors = nullptr;
        }
    } restore;
    M4::Allocator allocator;
    M4::HLSLParser parser(&allocator, "mobile.fx", hlsl.c_str(), hlsl.size());
    M4::HLSLTree tree(&allocator);
    if (!parser.Parse(&tree)) {
        diagnostic = errors.empty() ? "HLSL parse failed" : errors;
        return false;
    }
    for (auto* statement = tree.GetRoot()->statement; statement; statement = statement->nextStatement) {
        if (statement->nodeType != M4::HLSLNodeType_Declaration) continue;
        for (auto* d = static_cast<M4::HLSLDeclaration*>(statement); d; d = d->nextDeclaration) {
            if (d->type.flags & M4::HLSLTypeFlag_Const) d->type.flags |= M4::HLSLTypeFlag_Static;
            if (!d->assignment || (d->type.flags & M4::HLSLTypeFlag_Static)) continue;
            std::vector<float> values;
            if (!Constant(tree, d->assignment, values)) {
                diagnostic = std::string("parameter initialiser is not a constant: ") + d->name;
                return false;
            }
            if (values.size() == 1 && Width(d->type.baseType) <= 4) values.resize(Width(d->type.baseType), values[0]);
            defaults[Key(d->name)] = values;
            d->assignment = nullptr;
        }
    }
    // Only what the entry point reaches: the other stage's functions (ddx in a
    // pixel shader, for one) must not reach this stage's GLSL.
    M4::PruneTree(&tree, entry.c_str());
    M4::GLSLGenerator generator;
    M4::GLSLGenerator::Options generatorOptions;
    generatorOptions.flags = M4::GLSLGenerator::Flag_PackMatrixRowMajor;
    if (!generator.Generate(&tree, options.vertex ? M4::GLSLGenerator::Target_VertexShader : M4::GLSLGenerator::Target_FragmentShader,
                            M4::GLSLGenerator::Version_100_ES, entry.c_str(), generatorOptions)) {
        diagnostic = errors.empty() ? "GLSL generation failed" : errors;
        return false;
    }
    glsl = generator.GetResult();
    // #extension must precede any non-preprocessor token.
    static const std::string extension = "#extension GL_OES_standard_derivatives : require\n";
    if (const size_t at = glsl.find(extension); at != std::string::npos) {
        glsl.erase(at, extension.size());
        glsl.insert(glsl.find('\n') + 1, extension);
    }
    // ES100 defaults to different integer precision in VS and PS. The same
    // reflected uniform must have matching precision in both stages to link.
    const size_t afterHeader = glsl.find('\n') + 1;
    const size_t afterExtension = glsl.compare(afterHeader, extension.size(), extension) == 0 ? afterHeader + extension.size() : afterHeader;
    glsl.insert(afterExtension, "precision highp int;\n");
    return true;
}

}  // namespace WorldRPG::Fx
