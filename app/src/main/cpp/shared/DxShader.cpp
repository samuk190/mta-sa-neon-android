#include "DxShader.h"
#include "../third_party/hlslparser/HLSLParser.h"
#include "../third_party/hlslparser/GLSLGenerator.h"
#undef assert
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <regex>
#include <sstream>
#include <string_view>

namespace WorldRPG::DxShader {
namespace {
std::recursive_mutex gpuMutex;
constexpr size_t MaxSourceBytes=512*1024;
std::string Key(std::string name) {
    for(char& c:name)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return name;
}
bool IntegerUniform(GLenum type) {
    return type==GL_INT||type==GL_BOOL||(type>=GL_INT_VEC2&&type<=GL_BOOL_VEC4);
}
bool ValidIntegerValue(float value) {
    return std::isfinite(value)&&double(value)>=-2147483648.0&&double(value)<=2147483647.0;
}
struct Device {
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLContext context=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE;
    bool Initialize() {
        if (context!=EGL_NO_CONTEXT && surface!=EGL_NO_SURFACE) return true;
#ifndef __ANDROID__
        auto platform=reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        display=platform?platform(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */,nullptr,nullptr):eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
        display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
        EGLint major=0,minor=0;
        if (display==EGL_NO_DISPLAY || !eglInitialize(display,&major,&minor)) return false;
        const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
        EGLConfig config; EGLint count=0;
        if (!eglChooseConfig(display,attributes,&config,1,&count) || count!=1) return false;
        const EGLint surfaceAttributes[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
        const EGLSurface createdSurface=eglCreatePbufferSurface(display,config,surfaceAttributes);
        if(createdSurface==EGL_NO_SURFACE)return false;
        const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
        eglBindAPI(EGL_OPENGL_ES_API);
        const EGLContext createdContext=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttributes);
        if(createdContext==EGL_NO_CONTEXT){eglDestroySurface(display,createdSurface);return false;}
        surface=createdSurface;context=createdContext;return true;
    }
};
// Process lifetime: eglTerminate on Android's shared default display would
// invalidate GTA's context too. Individual programs/textures are still freed.
Device& Gpu() { static auto* device=new Device; return *device; }
struct Context {
    std::lock_guard<std::recursive_mutex> lock{gpuMutex};
    EGLDisplay oldDisplay=eglGetCurrentDisplay();
    EGLContext oldContext=eglGetCurrentContext();
    EGLSurface oldDraw=eglGetCurrentSurface(EGL_DRAW),oldRead=eglGetCurrentSurface(EGL_READ);
    EGLenum oldAPI=eglQueryAPI();
    bool valid=false;
    Context() {
        auto& d=Gpu();
        if (d.Initialize() && eglBindAPI(EGL_OPENGL_ES_API)) valid=eglMakeCurrent(d.display,d.surface,d.surface,d.context)==EGL_TRUE;
    }
    ~Context() {
        if (valid) eglMakeCurrent(Gpu().display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        eglBindAPI(oldAPI);
        if (oldDisplay!=EGL_NO_DISPLAY) eglMakeCurrent(oldDisplay,oldDraw,oldRead,oldContext);
    }
};
std::string StripComments(const std::string& source) {
    std::string result=source;
    bool block=false,line=false,quote=false;
    for (size_t i=0;i<result.size();++i) {
        char c=source[i],next=i+1<source.size()?source[i+1]:0;
        if (block) { if (c=='*' && next=='/') { result[i]=result[i+1]=' '; ++i; block=false; } else if(c!='\n') result[i]=' '; }
        else if (line) { if(c=='\n') line=false; else result[i]=' '; }
        else if (!quote && c=='/' && (next=='*'||next=='/')) { block=next=='*'; line=next=='/'; result[i]=result[i+1]=' '; ++i; }
        else if(c=='"' && (!i||source[i-1]!='\\')) quote=!quote;
    }
    return result;
}
struct Technique { std::string name,body; };
bool Techniques(std::string& source,std::vector<Technique>& techniques) {
    std::regex start(R"(\btechnique(?:9)?\s+(\w+)\s*\{)");
    std::smatch match;
    size_t from=0;
    while (from<source.size()) {
        auto begin=source.cbegin()+from;
        if (!std::regex_search(begin,source.cend(),match,start)) break;
        size_t first=from+match.position(),body=first+match.length(),end=body;
        unsigned depth=1;
        for (;end<source.size() && depth;++end) { if(source[end]=='{') ++depth; else if(source[end]=='}') --depth; }
        if (depth) return false;
        techniques.push_back({match[1],source.substr(body,end-body-1)});
        std::fill(source.begin()+first,source.begin()+end,' '); from=end;
    }
    return !techniques.empty();
}
unsigned Width(M4::HLSLBaseType type) {
    using namespace M4;
    switch(type){case HLSLBaseType_Float:case HLSLBaseType_Half:case HLSLBaseType_Int:case HLSLBaseType_Bool:return 1;
    case HLSLBaseType_Float2:case HLSLBaseType_Half2:return 2;case HLSLBaseType_Float3:case HLSLBaseType_Half3:return 3;
    case HLSLBaseType_Float4:case HLSLBaseType_Half4:return 4;case HLSLBaseType_Float4x4:return 16;default:return 0;}
}
bool Constant(M4::HLSLTree& tree,M4::HLSLExpression* expression,std::vector<float>& result,unsigned depth=0) {
    using namespace M4;
    if(!expression||depth>32)return false;
    if(expression->nodeType==HLSLNodeType_LiteralExpression) {
        auto* literal=static_cast<HLSLLiteralExpression*>(expression);
        switch(expression->expressionType.baseType) {
        case HLSLBaseType_Float:case HLSLBaseType_Half:result={literal->fValue};return std::isfinite(literal->fValue);
        case HLSLBaseType_Int:result={float(literal->iValue)};return true;
        case HLSLBaseType_Bool:result={float(literal->bValue)};return true;default:return false;}
    }
    if(expression->nodeType==HLSLNodeType_IdentifierExpression) {
        auto* declaration=tree.FindGlobalDeclaration(static_cast<HLSLIdentifierExpression*>(expression)->name);
        return declaration && (declaration->type.flags&HLSLTypeFlag_Const) && Constant(tree,declaration->assignment,result,depth+1);
    }
    if(expression->nodeType==HLSLNodeType_ConstructorExpression) {
        auto* constructor=static_cast<HLSLConstructorExpression*>(expression);
        for(auto* arg=constructor->argument;arg;arg=arg->nextExpression) {
            std::vector<float> values;if(!Constant(tree,arg,values,depth+1)||result.size()+values.size()>16)return false;
            result.insert(result.end(),values.begin(),values.end());
        }
        const unsigned width=Width(constructor->type.baseType);
        if(result.size()==1&&width<=4)result.resize(width,result[0]);
        return width && result.size()==width;
    }
    if(expression->nodeType==HLSLNodeType_UnaryExpression) {
        auto* unary=static_cast<HLSLUnaryExpression*>(expression);
        if(!Constant(tree,unary->expression,result,depth+1))return false;
        if(unary->unaryOp==HLSLUnaryOp_Negative){for(float& value:result)value=-value;return true;}
        return unary->unaryOp==HLSLUnaryOp_Positive;
    }
    if(expression->nodeType==HLSLNodeType_BinaryExpression) {
        auto* binary=static_cast<HLSLBinaryExpression*>(expression);std::vector<float> a,b;
        if(!Constant(tree,binary->expression1,a,depth+1)||!Constant(tree,binary->expression2,b,depth+1))return false;
        if(a.size()==1)a.resize(b.size(),a[0]);if(b.size()==1)b.resize(a.size(),b[0]);if(a.size()!=b.size())return false;
        for(size_t i=0;i<a.size();++i){float value;
            switch(binary->binaryOp){case HLSLBinaryOp_Add:value=a[i]+b[i];break;case HLSLBinaryOp_Sub:value=a[i]-b[i];break;
            case HLSLBinaryOp_Mul:value=a[i]*b[i];break;case HLSLBinaryOp_Div:if(!b[i])return false;value=a[i]/b[i];break;default:return false;}
            if(!std::isfinite(value))return false;result.push_back(value);
        }return true;
    }
    return false;
}
bool Translate(const std::string& source,const std::string& entry,bool vertex,std::string& output,
               std::map<std::string,std::vector<float>>& defaults) {
    M4::Allocator allocator;
    M4::HLSLParser parser(&allocator,"mobile.fx",source.c_str(),source.size());
    M4::HLSLTree tree(&allocator);
    if (!parser.Parse(&tree)) return false;
    for(auto* statement=tree.GetRoot()->statement;statement;statement=statement->nextStatement) {
        if(statement->nodeType!=M4::HLSLNodeType_Declaration)continue;
        for(auto* d=static_cast<M4::HLSLDeclaration*>(statement);d;d=d->nextDeclaration) {
            if(d->type.flags&M4::HLSLTypeFlag_Const)d->type.flags|=M4::HLSLTypeFlag_Static;
            if(!d->assignment||(d->type.flags&M4::HLSLTypeFlag_Static))continue;
            std::vector<float> values;if(!Constant(tree,d->assignment,values))return false;
            if(values.size()==1 && Width(d->type.baseType)<=4)values.resize(Width(d->type.baseType),values[0]);
            defaults[Key(d->name)]=values;d->assignment=nullptr;
        }
    }
    M4::GLSLGenerator generator;
    M4::GLSLGenerator::Options options;
    options.flags=M4::GLSLGenerator::Flag_PackMatrixRowMajor;
    if (!generator.Generate(&tree,vertex?M4::GLSLGenerator::Target_VertexShader:M4::GLSLGenerator::Target_FragmentShader,
                            M4::GLSLGenerator::Version_100_ES,entry.c_str(),options)) return false;
    output=generator.GetResult();
    // ES100 defaults to different integer precision in VS and PS. The same
    // reflected uniform must have matching precision in both stages to link.
    output.insert(output.find('\n')+1,"precision highp int;\n");
    return true;
}
GLuint Compile(GLenum type,const std::string& source,std::string& error) {
    GLuint shader=glCreateShader(type); const char* data=source.c_str();
    glShaderSource(shader,1,&data,nullptr); glCompileShader(shader);
    GLint success=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&success);
    if (!success) {
        char log[4096]{}; glGetShaderInfoLog(shader,sizeof(log),nullptr,log); error=log;
        glDeleteShader(shader); return 0;
    }
    return shader;
}
using Matrix=std::array<float,16>;
Matrix Identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
// Row-vector matrices, uploaded as column-major transpose for GLSL mul(v,m).
Matrix Multiply(const Matrix& a,const Matrix& b) {
    Matrix out{};
    for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) for(unsigned k=0;k<4;++k) out[r*4+c]+=a[r*4+k]*b[k*4+c];
    return out;
}
Matrix Translation(float x,float y,float z) { auto m=Identity(); m[12]=x;m[13]=y;m[14]=z;return m; }
struct Vertex { float x,y,z,u,v,r,g,b,a; };
}
struct Program {
    GLuint id=0;
    struct Uniform { GLint location=-1; GLenum type=0; GLint count=0; std::vector<float> numbers; Image image; };
    std::map<std::string,Uniform> uniforms;
    std::map<std::string,std::vector<std::string>> aliases;
    std::string firstTexture;
    unsigned tessX=1,tessY=1;
    Transform transform;
    ~Program() { Context context; if(context.valid && id) glDeleteProgram(id); }
};
std::shared_ptr<Program> Create(const std::string& original,const std::map<std::string,std::string>& macros,
                               std::string& technique,std::string& diagnostic) {
    diagnostic="unsupported or invalid FX source";
    if (original.size()>MaxSourceBytes || original.find('\0')!=std::string::npos) return {};
    std::string source=StripComments(original);
    auto definitions=macros;
    std::istringstream lines(source); std::string line,expanded;
    std::regex define(R"(^\s*#\s*define\s+(\w+)\s+(.*?)\s*$)");
    while (std::getline(lines,line)) {
        std::smatch match;
        if (std::regex_match(line,match,define)) { if(!definitions.count(match[1])) definitions[match[1]]=match[2]; }
        else if(line.find('#')!=std::string::npos) { diagnostic="FX preprocessor directive not supported"; return {}; }
        else expanded+=line+'\n';
    }
    source=expanded;
    auto appendSource=[&](std::string& out,std::string_view piece) {
        if(piece.size()>MaxSourceBytes-out.size()) {
            diagnostic="expanded shader source exceeds the 512 KiB limit";return false;
        }
        out.append(piece.data(),piece.size());return true;
    };
    if(source.size()>MaxSourceBytes){diagnostic="expanded shader source exceeds the 512 KiB limit";return {};}
    for (const auto& [name,value]:definitions) {
        if(!std::regex_match(name,std::regex("[A-Za-z_][A-Za-z_0-9]*")) || value.size()>256) return {};
        const std::regex identifier("\\b"+name+"\\b");
        std::string replaced;size_t end=0;const std::string_view current(source);
        for(auto it=std::sregex_iterator(source.begin(),source.end(),identifier);it!=std::sregex_iterator();++it) {
            if(!appendSource(replaced,current.substr(end,it->position()-end)) || !appendSource(replaced,value))return {};
            end=it->position()+it->length();
        }
        if(!appendSource(replaced,current.substr(end)))return {};
        source=std::move(replaced);
    }
    std::vector<Technique> techniques;
    if (!Techniques(source,techniques)) return {};
    // DX9 effect texture objects are bound to GLES sampler uniforms. Preserve
    // the original texture parameter name as an alias for dxSetShaderValue.
    std::map<std::string,std::vector<std::string>> aliases;
    std::smatch firstTexture;
    const bool hasTexture=std::regex_search(source,firstTexture,std::regex(R"(\b(?:texture|sampler2D)\s+(\w+)\b)"));
    const std::string sizeParameter=hasTexture?Key(firstTexture[1]):std::string{};
    std::regex sampler(R"(\bsampler(?:2D)?\s+(\w+)\s*=\s*sampler_state\s*\{([^{}]*)\}\s*;)",std::regex::icase);
    std::smatch match;
    while (std::regex_search(source,match,sampler)) {
        std::string body=match[2]; std::smatch texture;
        if (!std::regex_search(body,texture,std::regex(R"(Texture\s*=\s*<\s*(\w+)\s*>\s*;)",std::regex::icase))) return {};
        // Filtering/addressing other than the implemented linear/clamp path is
        // rejected. Silently dropping FX render state changes is not safe.
        std::string remainder=std::regex_replace(body,std::regex(R"(Texture\s*=\s*<\s*\w+\s*>\s*;)",std::regex::icase),"");
        remainder=std::regex_replace(remainder,std::regex(R"(\b(?:MinFilter|MagFilter|MipFilter)\s*=\s*Linear\s*;|\bAddress[UV]\s*=\s*Clamp\s*;)",std::regex::icase),"");
        if(remainder.find_first_not_of(" \t\r\n")!=std::string::npos) { diagnostic="unsupported sampler state"; return {}; }
        aliases[Key(texture[1])].push_back(Key(match[1]));
        source.replace(match.position(),match.length(),"sampler2D "+match[1].str()+";");
    }
    source=std::regex_replace(source,std::regex(R"(\btexture\s+\w+\s*;)"),"");
    if (source.find("gWorldViewProjection")==std::string::npos) source="float4x4 gWorldViewProjection;\n"+source;
    const char* defaultVertex=R"(
struct MobileVertex { float3 Position:POSITION; float2 TexCoord:TEXCOORD0; float4 Color:COLOR0; };
struct MobileOutput { float4 Position:POSITION; float2 TexCoord:TEXCOORD0; float4 Color:COLOR0; };
MobileOutput MobileDefaultVS(MobileVertex input) {
 MobileOutput output; output.Position=mul(float4(input.Position,1),gWorldViewProjection);
 output.TexCoord=input.TexCoord; output.Color=input.Color; return output;
}
)";
    source+=defaultVertex;
    Context context;
    if (!context.valid) { diagnostic="EGL shader context unavailable"; return {}; }
    for (const auto& candidate:techniques) {
        std::string body=candidate.body;
        const std::regex pass(R"(\bpass\b)");
        if (std::distance(std::sregex_iterator(body.begin(),body.end(),pass),std::sregex_iterator())!=1) continue;
        std::smatch pixel,vertex;
        if (!std::regex_search(body,pixel,std::regex(R"(PixelShader\s*=\s*compile\s+ps_\w+\s+(\w+)\s*\(\s*\)\s*;)",std::regex::icase))) continue;
        std::string ps=pixel[1],vs="MobileDefaultVS";
        if(std::regex_search(body,vertex,std::regex(R"(VertexShader\s*=\s*compile\s+vs_\w+\s+(\w+)\s*\(\s*\)\s*;)",std::regex::icase))) vs=vertex[1];
        body=std::regex_replace(body,std::regex(R"((?:PixelShader|VertexShader)\s*=\s*compile\s+\w+\s+\w+\s*\(\s*\)\s*;|VertexShader\s*=\s*(?:NULL|0)\s*;|\bpass\s*\w*\s*[{}]|[{}])",std::regex::icase),"");
        if(body.find_first_not_of(" \t\r\n")!=std::string::npos) { diagnostic="unsupported FX pass state"; continue; }
        std::string vertexSource,pixelSource;std::map<std::string,std::vector<float>> defaults;
        if(!Translate(source,vs,true,vertexSource,defaults)||!Translate(source,ps,false,pixelSource,defaults)) continue;
        GLuint v=Compile(GL_VERTEX_SHADER,vertexSource,diagnostic);
        if(!v) continue;
        GLuint f=Compile(GL_FRAGMENT_SHADER,pixelSource,diagnostic);
        if(!f) { glDeleteShader(v); continue; }
        GLuint id=glCreateProgram(); glAttachShader(id,v); glAttachShader(id,f); glLinkProgram(id);
        glDeleteShader(v);glDeleteShader(f);
        GLint success=0;glGetProgramiv(id,GL_LINK_STATUS,&success);
        if(!success) { char log[4096]{};glGetProgramInfoLog(id,sizeof(log),nullptr,log);diagnostic=log;glDeleteProgram(id);continue; }
        GLint attributeCount=0;glGetProgramiv(id,GL_ACTIVE_ATTRIBUTES,&attributeCount);
        bool supported=true;
        for(GLint i=0;i<attributeCount;++i) {
            char name[256]{};GLint size;GLenum type;glGetActiveAttrib(id,i,sizeof(name),nullptr,&size,&type,name);
            const std::string key=name;
            supported=supported && size==1 &&
                (((key=="POSITION"||key=="POSITION0")&&(type==GL_FLOAT_VEC3||type==GL_FLOAT_VEC4)) ||
                 (key=="TEXCOORD0"&&type==GL_FLOAT_VEC2) || (key=="COLOR0"&&type==GL_FLOAT_VEC4));
        }
        if(!supported){diagnostic="unsupported vertex input semantic";glDeleteProgram(id);continue;}
        auto result=std::make_shared<Program>();result->id=id;result->aliases=aliases;result->firstTexture=sizeParameter;
        GLint count=0;glGetProgramiv(id,GL_ACTIVE_UNIFORMS,&count);
        bool supportedUniforms=true;
        for(GLint i=0;i<count;++i) {
            char name[256]{};GLint size;GLenum type;glGetActiveUniform(id,i,sizeof(name),nullptr,&size,&type,name);
            switch(type) {
            case GL_FLOAT:case GL_FLOAT_VEC2:case GL_FLOAT_VEC3:case GL_FLOAT_VEC4:
            case GL_FLOAT_MAT2:case GL_FLOAT_MAT3:case GL_FLOAT_MAT4:case GL_INT:
            case GL_INT_VEC2:case GL_INT_VEC3:case GL_INT_VEC4:case GL_BOOL:
            case GL_BOOL_VEC2:case GL_BOOL_VEC3:case GL_BOOL_VEC4:break;
            case GL_SAMPLER_2D:supportedUniforms=supportedUniforms&&size==1;break;
            default:supportedUniforms=false;
            }
            std::string key=name;auto bracket=key.find("[0]");if(bracket!=std::string::npos) key.resize(bracket);
            if(IntegerUniform(type))for(float value:defaults[Key(key)])
                supportedUniforms=supportedUniforms&&ValidIntegerValue(value);
            result->uniforms[Key(key)]={glGetUniformLocation(id,name),type,size,defaults[Key(key)],{}};
        }
        if(!supportedUniforms){diagnostic="unsupported shader uniform type, sampler array or integer default";continue;}
        technique=candidate.name;diagnostic.clear();return result;
    }
    return {};
}
namespace {
Program::Uniform* Find(Program& p,const std::string& name) {
    const auto key=Key(name);auto alias=p.aliases.find(key);
    if(alias!=p.aliases.end())for(const auto& sampler:alias->second){auto it=p.uniforms.find(sampler);if(it!=p.uniforms.end())return &it->second;}
    auto it=p.uniforms.find(key);
    return it==p.uniforms.end()?nullptr:&it->second;
}
unsigned Components(GLenum type) {
    switch(type) { case GL_FLOAT:case GL_INT:case GL_BOOL:return 1;case GL_FLOAT_VEC2:case GL_INT_VEC2:case GL_BOOL_VEC2:return 2;
    case GL_FLOAT_VEC3:case GL_INT_VEC3:case GL_BOOL_VEC3:return 3;case GL_FLOAT_VEC4:case GL_INT_VEC4:case GL_BOOL_VEC4:return 4;
    case GL_FLOAT_MAT2:return 4;case GL_FLOAT_MAT3:return 9;case GL_FLOAT_MAT4:return 16;default:return 0; }
}
void SetUniform(const Program::Uniform& u,const std::vector<float>& v) {
    if(v.empty()) return;
    switch(u.type) {
    case GL_FLOAT:glUniform1fv(u.location,v.size(),v.data());break;
    case GL_FLOAT_VEC2:glUniform2fv(u.location,v.size()/2,v.data());break;
    case GL_FLOAT_VEC3:glUniform3fv(u.location,v.size()/3,v.data());break;
    case GL_FLOAT_VEC4:glUniform4fv(u.location,v.size()/4,v.data());break;
    case GL_FLOAT_MAT2:glUniformMatrix2fv(u.location,v.size()/4,GL_FALSE,v.data());break;
    case GL_FLOAT_MAT3:glUniformMatrix3fv(u.location,v.size()/9,GL_FALSE,v.data());break;
    case GL_FLOAT_MAT4:glUniformMatrix4fv(u.location,v.size()/16,GL_FALSE,v.data());break;
    default: { std::vector<GLint> ints;for(float n:v) ints.push_back(static_cast<GLint>(n));
        unsigned c=Components(u.type);if(c==1)glUniform1iv(u.location,ints.size(),ints.data());if(c==2)glUniform2iv(u.location,ints.size()/2,ints.data());
        if(c==3)glUniform3iv(u.location,ints.size()/3,ints.data());if(c==4)glUniform4iv(u.location,ints.size()/4,ints.data()); }
    }
}
}
bool SetNumbers(Program& p,const std::string& name,const std::vector<float>& values,bool boolean) {
    std::lock_guard lock(gpuMutex);auto* u=Find(p,name);if(!u) return false;
    unsigned components=Components(u->type);
    if(!components || values.empty() || values.size()>16 || values.size()%components || values.size()>components*unsigned(u->count))return false;
    for(float value:values)if(!std::isfinite(value) || (IntegerUniform(u->type) && !ValidIntegerValue(value)))return false;
    if(boolean && u->type!=GL_BOOL) return false;
    u->numbers=values;return true;
}
bool SetTexture(Program& p,const std::string& name,Image image) {
    std::lock_guard lock(gpuMutex);
    if(!image.width || !image.height ||
       image.width>4096 || image.height>4096 || image.pixels.size()!=size_t(image.width)*image.height)return false;
    const auto key=Key(name);const auto alias=p.aliases.find(key);
    const std::vector<std::string> names=alias==p.aliases.end()?std::vector<std::string>{key}:alias->second;
    bool found=false;
    for(const auto& key:names) {
        auto it=p.uniforms.find(key);if(it==p.uniforms.end())continue;
        if(it->second.type!=GL_SAMPLER_2D||it->second.count!=1)return false;found=true;
    }
    if(!found)return false;
    for(const auto& key:names) {auto it=p.uniforms.find(key);if(it!=p.uniforms.end())it->second.image=image;}
    return true;
}
bool SetTessellation(Program& p,unsigned x,unsigned y) {
    std::lock_guard lock(gpuMutex);if(!x||!y||x>64||y>64)return false;p.tessX=x;p.tessY=y;return true;
}
void MaterialSize(Program& p,unsigned& width,unsigned& height) {
    std::lock_guard lock(gpuMutex);auto* u=Find(p,p.firstTexture);
    width=u&&u->image.width?u->image.width:1;height=u&&u->image.height?u->image.height:1;
}
bool SetTransform(Program& p,const Transform& t) {
    std::lock_guard lock(gpuMutex);
    for(float v:t.rotation)if(!std::isfinite(v))return false;
    for(float v:t.rotationCenter)if(!std::isfinite(v))return false;
    for(float v:t.perspectiveCenter)if(!std::isfinite(v))return false;
    p.transform=t;return true;
}
bool Render(Program& p,const Tile& tile,unsigned width,unsigned height,Image& output) {
    if(!width||!height||width>4096||height>4096||uint64_t(width)*height>8*1024*1024) return false;
    for(float value:{tile.x,tile.y,tile.width,tile.height,tile.u,tile.v,tile.usize,tile.vsize,tile.rotation,tile.offsetX,tile.offsetY})
        if(!std::isfinite(value))return false;
    Context context;if(!context.valid)return false;
    while(glGetError()!=GL_NO_ERROR) {}
    GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);if(width>unsigned(limit)||height>unsigned(limit))return false;
    GLuint color=0,fbo=0,vbo=0;std::vector<GLuint> textures;
    struct Cleanup { GLuint& color;GLuint& fbo;GLuint& vbo;std::vector<GLuint>& textures;
        ~Cleanup(){glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteBuffers(1,&vbo);glDeleteTextures(1,&color);if(!textures.empty())glDeleteTextures(textures.size(),textures.data());} } cleanup{color,fbo,vbo,textures};
    glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
    glViewport(0,0,width,height);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glUseProgram(p.id);
    const auto& t=p.transform;
    const float radians=3.14159265358979323846f/180;
    // D3DX yaw=rotation.x, pitch=rotation.y, roll=rotation.z (desktop tile batcher).
    float yaw=t.rotation[0]*radians,pitch=t.rotation[1]*radians,roll=t.rotation[2]*radians;
    auto rx=Identity(),ry=Identity(),rz=Identity();
    rx[5]=rx[10]=std::cos(pitch);rx[6]=std::sin(pitch);rx[9]=-rx[6];
    ry[0]=ry[10]=std::cos(yaw);ry[8]=std::sin(yaw);ry[2]=-ry[8];
    rz[0]=rz[5]=std::cos(roll);rz[1]=std::sin(roll);rz[4]=-rz[1];
    float cx=(t.rotationScreen?width/2.f:tile.x+tile.width/2)+t.rotationCenter[0]*width/2;
    float cy=(t.rotationScreen?height/2.f:tile.y+tile.height/2)+t.rotationCenter[1]*height/2,cz=t.rotationCenter[2]*500;
    auto world=Multiply(Multiply(Translation(-cx,-cy,-cz),Multiply(Multiply(rz,rx),ry)),Translation(cx,cy,cz));
    float px=t.perspectiveCenter[0]*width/2,py=t.perspectiveCenter[1]*height/2;
    if(!t.perspectiveScreen) { px+=tile.x+tile.width/2-width/2.f;py+=tile.y+tile.height/2-height/2.f; }
    world=Multiply(world,Translation(-px,-py,0));auto view=Translation(0,0,1000);
    Matrix projection{};projection[0]=2000.f/width;projection[5]=-2000.f/height;projection[10]=10000.f/9900;
    projection[11]=1;projection[12]=-1000;projection[13]=1000;projection[14]=-10000.f/99;
    auto from=Translation(px*2/width,-py*2/height,0),to=Translation(-px*2/width,py*2/height,0);
    projection=Multiply(Multiply(to,projection),from);
    auto combined=Multiply(Multiply(world,view),projection);
    for(const auto* matrix:{&world,&view,&projection,&combined})for(float value:*matrix)if(!std::isfinite(value))return false;
    auto setMatrix=[&](const char* name,const Matrix& matrix){auto* u=Find(p,name);if(u && u->type==GL_FLOAT_MAT4)SetUniform(*u,{matrix.begin(),matrix.end()});};
    GLint unit=0,maxUnits=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&maxUnits);
    for(const auto& [name,u]:p.uniforms) {
        if(u.type==GL_SAMPLER_2D) {
            if(unit>=maxUnits)return false;
            GLuint texture;glGenTextures(1,&texture);textures.push_back(texture);glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,texture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            std::vector<uint8_t> rgba;for(uint32_t c:u.image.pixels) { rgba.push_back(c>>16);rgba.push_back(c>>8);rgba.push_back(c);rgba.push_back(c>>24); }
            const uint8_t white[]={255,255,255,255};
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,u.image.width?u.image.width:1,u.image.height?u.image.height:1,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.empty()?white:rgba.data());glUniform1i(u.location,unit++);
        } else SetUniform(u,u.numbers);
    }
    // CTileBatcher.cpp:217-222 applies custom values before common handles.
    // Effect initializers and Lua values must not replace the active camera.
    setMatrix("gWorld",world);setMatrix("gView",view);setMatrix("gProjection",projection);setMatrix("gWorldViewProjection",combined);
    std::vector<Vertex> vertices;vertices.reserve(p.tessX*p.tessY*6);
    auto vertex=[&](float x,float y){ float vx=tile.x+x*tile.width,vy=tile.y+y*tile.height;
        float ox=tile.x+tile.width/2+tile.offsetX,oy=tile.y+tile.height/2+tile.offsetY,s=std::sin(tile.rotation*radians),c=std::cos(tile.rotation*radians);
        return Vertex{ox+(vx-ox)*c-(vy-oy)*s,oy+(vx-ox)*s+(vy-oy)*c,0,tile.u+x*tile.usize,tile.v+y*tile.vsize,
            float((tile.color>>16)&255)/255,float((tile.color>>8)&255)/255,float(tile.color&255)/255,float(tile.color>>24)/255};};
    for(unsigned y=0;y<p.tessY;++y)for(unsigned x=0;x<p.tessX;++x) {
        auto a=vertex(float(x)/p.tessX,float(y)/p.tessY),b=vertex(float(x+1)/p.tessX,float(y)/p.tessY);
        auto c=vertex(float(x)/p.tessX,float(y+1)/p.tessY),d=vertex(float(x+1)/p.tessX,float(y+1)/p.tessY);
        vertices.insert(vertices.end(),{a,b,c,b,d,c});
    }
    for(const auto& v:vertices)for(float value:{v.x,v.y,v.z,v.u,v.v})if(!std::isfinite(value))return false;
    glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STREAM_DRAW);
    auto attribute=[&](const char* name,int count,size_t offset){GLint location=glGetAttribLocation(p.id,name);if(location>=0){glEnableVertexAttribArray(location);glVertexAttribPointer(location,count,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offset));}};
    attribute("POSITION",3,0);attribute("POSITION0",3,0);attribute("TEXCOORD0",2,3*sizeof(float));attribute("COLOR0",4,5*sizeof(float));
    glDrawArrays(GL_TRIANGLES,0,vertices.size());
    std::vector<uint8_t> rgba(size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    if(glGetError()!=GL_NO_ERROR)return false;
    output.width=width;output.height=height;output.pixels.resize(size_t(width)*height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {size_t i=(size_t(height-1-y)*width+x)*4;output.pixels[size_t(y)*width+x]=(uint32_t(rgba[i+3])<<24)|(rgba[i]<<16)|(rgba[i+1]<<8)|rgba[i+2];}
    return true;
}
} // namespace WorldRPG::DxShader
