// Host replacement for WorldRPG::JniBridge (jni/JNI_OnLoad.cpp is Android-only).
// Game lines that the Android client would show in the GameActivity are printed
// to stdout instead, so chat/progress text from the server stays visible.
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <utility>
#include "test_bitmaps.h"

namespace WorldRPG::JniBridge {
namespace {
struct Bitmap { std::vector<uint32_t> pixels;unsigned width,height; };
std::map<std::string,Bitmap> bitmaps;
bool testBitmaps=false;
std::mutex gameLinesMutex;
std::vector<std::pair<int,std::string>> gameLines;
}
// Every kind-20 control line since the last call, for the control toggle tests.
std::vector<std::string> TakeControlLines(){
    std::lock_guard<std::mutex> lock(gameLinesMutex);
    std::vector<std::string> out;
    for(auto& line:gameLines) if(line.first==20) out.push_back(line.second);
    gameLines.clear();
    return out;
}
void EnableTestBitmaps(bool enabled){testBitmaps=enabled;bitmaps.clear();}
size_t TestBitmapCount(){return bitmaps.size();}
void PostGameLine(int kind, const char* text) {
    if (text && kind == 20) {
        std::lock_guard<std::mutex> lock(gameLinesMutex);
        if (gameLines.size() < 256) gameLines.emplace_back(kind, text);
    }
    if (text && *text) std::printf("[GAMELINE kind=%d] %s\n", kind, text);
}
// No Activity on the host: the caller falls back to its fixed-width estimate.
float MeasureText(const std::string&, float, const std::string&, bool) { return -1.f; }
float FontHeight(const std::string&, float, bool) { return -1.f; }
// No Android bitmap codecs on the host.
bool LoadPixels(const std::string& name, std::vector<uint32_t>& out, unsigned& width, unsigned& height) {
    const auto it=bitmaps.find(name);if(!testBitmaps||it==bitmaps.end())return false;
    out=it->second.pixels;width=it->second.width;height=it->second.height;return true;
}
bool DecodePixels(const char*, size_t, std::vector<uint32_t>&, unsigned&, unsigned&) { return false; }
bool EncodePixels(const std::vector<uint32_t>&, unsigned, unsigned, bool, int, std::string&) { return false; }
bool StorePixels(const std::string& name, const std::vector<uint32_t>& pixels, unsigned width, unsigned height) {
    if(!testBitmaps||!width||!height||pixels.size()!=size_t(width)*height)return false;
    bitmaps[name]={pixels,width,height};return true;
}
void CopyScreen(const std::string&, unsigned, unsigned) {}
// No SVG renderer on the host: the document is kept without a picture.
int RenderSvg(const std::string&, const std::string&, unsigned, unsigned) { return -1; }
} // namespace WorldRPG::JniBridge
