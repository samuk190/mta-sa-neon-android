// Host checks for the resource lifecycle. Built as neon_resource_tests; needs no device or server.
//   - RESOURCE_STOP (83) body parsing and PLAYER_RESOURCE_START (108) bytes.
//   - RESOURCE_START descriptor sets the ServerWorld resource root.
//   - fromJSON / toJSON against the desktop rules; with a path argument, the real catalog.json.
//   - RESOURCE_STOP: onClientResourceStop runs for its own resource, its handlers and timers
//     are dropped, and another resource keeps running.
//   - GUI elements (desktop CLuaGUIDefs): a hidden window and its children are not drawn,
//     relative placement, alpha inheritance, destroyElement, and the DX font scale.
//   - fetchRemote options-table statusCode, abort, and https (no HttpDownloader stats change).
//   - NormVectorBytesMatchVehicleSync and one sent packet in getNetworkUsageData.
// Usage: neon_resource_tests [path/to/catalog.json]

#include "HttpDownloader.h"
#include "LuaVMStub.h"
#include "MtaCipher.h"
#include "MtaOfflineCodec.h"
#include "MtaReliabilityCodec.h"
#include "ResourceFiles.h"
#include "ResourceLoader.h"
#include "ResourcePackets.h"
#include "ServerWorld.h"
#include "net/raknet/RakNetWrapper.h"

#include <zlib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* expr, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL line %d: %s\n", line, expr);
    }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

// Runs a chunk in the test VM. A failed assert reports its message here.
bool RunLua(WorldRPG::Script::LuaVMStub& vm, const char* name, const std::string& code) {
    const WorldRPG::Script::LuaExecResult result = vm.executeBuffer(code.data(), code.size(), name);
    ++g_checks;
    if (!result.success) {
        ++g_failures;
        std::printf("FAIL lua %s: %s\n", name, result.errorMessage.c_str());
    }
    return result.success;
}

// RESOURCE_CLIENT_SCRIPTS (packet 95) body after the game id, as the server writes it for a
// mobile peer: uint16 net id, uint16 count, then per item uint32 length and the item
// (uint32 big-endian original length + zlib data).
std::vector<uint8_t> NoCacheScriptBody(uint16_t netId, const std::string& source) {
    uLongf packedLength = compressBound(source.size());
    std::vector<uint8_t> packed(packedLength);
    compress2(packed.data(), &packedLength, reinterpret_cast<const Bytef*>(source.data()), source.size(),
              Z_DEFAULT_COMPRESSION);
    packed.resize(packedLength);

    const uint32_t original = static_cast<uint32_t>(source.size());
    std::vector<uint8_t> item = {static_cast<uint8_t>(original >> 24), static_cast<uint8_t>(original >> 16),
                                 static_cast<uint8_t>(original >> 8), static_cast<uint8_t>(original)};
    item.insert(item.end(), packed.begin(), packed.end());

    std::vector<uint8_t> body = {static_cast<uint8_t>(netId & 0xFF), static_cast<uint8_t>(netId >> 8), 1, 0};
    const uint32_t length = static_cast<uint32_t>(item.size());
    for (int i = 0; i < 4; ++i) body.push_back(static_cast<uint8_t>(length >> (8 * i)));
    body.insert(body.end(), item.begin(), item.end());
    return body;
}

void CheckPacketLayouts() {
    // CResourceStopPacket::Write writes BitStream.Write(m_usID).
    WorldRPG::Net::BitStreamWriter serverStop;
    serverStop.Write(static_cast<uint16_t>(0x0102));
    uint16_t netId = 0;
    CHECK(WorldRPG::Script::ResourcePackets::ParseResourceStop(serverStop.GetData(), serverStop.GetByteLength(),
                                                               serverStop.GetBitLength(), netId));
    CHECK(netId == 0x0102);
    CHECK(!WorldRPG::Script::ResourcePackets::ParseResourceStop(serverStop.GetData(), 1, 8, netId));

    // Game id 108, then uint16 net id and uint32 start counter, little-endian as the server reads them.
    WorldRPG::Net::BitStreamWriter playerStart;
    WorldRPG::Script::ResourcePackets::BuildPlayerResourceStart(playerStart, 0x0102, 0x03040506);
    const uint8_t expected[] = {108, 0x02, 0x01, 0x06, 0x05, 0x04, 0x03};
    CHECK(playerStart.GetByteLength() == sizeof(expected));
    CHECK(std::memcmp(playerStart.GetData(), expected, sizeof(expected)) == 0);
}

void CheckJson(WorldRPG::Script::LuaVMStub& vm, const char* catalogPath) {
    RunLua(vm, "json-arrays", R"LUA(
        local a, b, c, d = fromJSON('[ 1, "a", null, true ]')
        assert(a == 1 and b == "a" and c == nil and d == true, "array values")
        assert(select('#', fromJSON('[ 1, "a", null, true ]')) == 4, "one return value per element")
        assert(select('#', fromJSON('[]')) == 0, "empty array returns no values")
        assert(fromJSON('nope') == nil and fromJSON('') == nil and fromJSON('  "x"') == nil, "not an object or array")
        assert(fromJSON('{"a":1') == nil, "unterminated object")
        assert(fromJSON('[1,]') == nil, "trailing comma")
        assert(fromJSON('[1] x') == nil, "trailing text")
    )LUA");

    RunLua(vm, "json-objects", R"LUA(
        local o = fromJSON('{"format": 1, "list": [1, 2.5, "x"], "nested": {"k": false}, "big": 1e3}')
        assert(math.type(o.format) == "integer" and o.format == 1, "integral number")
        assert(o.list[2] == 2.5 and o.list[3] == "x", "array inside object")
        assert(o.nested.k == false, "false value")
        assert(o.big == 1000, "exponent")
        local u = fromJSON('["caf\\u00e9"]')
        assert(u == "caf\195\169", "unicode escape")
        local s = fromJSON('["\\ud83d\\ude00"]')
        assert(s == "\240\159\152\128", "surrogate pair")
    )LUA");

    RunLua(vm, "json-elements", R"LUA(
        local e = fromJSON('[ "^E^777" ]')
        assert(type(e) == "table" and e.element == true and e.id == 777, "known element id")
        assert(fromJSON('[ "^E^1" ]') == nil, "unknown element is nil")
        assert(fromJSON('[ "^Q^1" ]') == "^Q^1", "other ^x^ text stays a string")
    )LUA");

    RunLua(vm, "json-writer", R"LUA(
        assert(toJSON({1, 2}) == '[ [ 1, 2 ] ]', "spaced array, wrapped")
        assert(toJSON({1, 2}, true) == '[[1,2]]', "compact array, wrapped")
        assert(toJSON({a = 1}, true) == '[{"a":1}]', "compact object")
        assert(toJSON({}) == '[ [ ] ]', "empty table is an empty array")
        assert(toJSON({x = {}}) == '[ { "x": [ ] } ]', "nested empty array")
        assert(toJSON("x") == '[ "x" ]', "string")
        assert(toJSON(true, true) == '[true]', "boolean")
        assert(toJSON(nil) == nil, "nil argument gives nil")
        assert(toJSON(1, true, 5) == '[1]', "extra argument is ignored")
        assert(toJSON(1, "yes") == nil, "non-boolean compact flag gives nil")
        assert(toJSON(1.5, true) == '[1.5]', "fraction")
        assert(toJSON(50.0, true) == '[50]', "integral float is an integer")
        assert(toJSON("a/b", true) == '["a\\/b"]', "slash is escaped as json-c does")
        assert(toJSON("line\n", true) == '["line\\n"]', "newline escape")
        assert(toJSON("^E^5", true) == '["^E~5"]', "element-like text is escaped")
        assert(toJSON({id = 777, element = true}, true) == '[]', "element is not written by toJSON")
        assert(toJSON({1, 2, {x = 1}}, true) == '[[1,2,{"x":1}]]', "mixed nesting")
    )LUA");

    RunLua(vm, "json-round-trip", R"LUA(
        local c = {}
        c.self = c
        assert(toJSON(c, true) == '[{"self":"^T^0"}]', "cycle becomes a table reference")
        local back = fromJSON(toJSON(c, true))
        assert(back.self == back, "table reference reads back as the same table")
        local t = {name = "x", list = {1, 2, 3}}
        local r = fromJSON(toJSON(t, true))
        assert(r.name == "x" and r.list[3] == 3, "round trip")
    )LUA");

    if (catalogPath) {
        // samp-map-loader/models.lua: fromJSON(readFile("catalog.json")), then catalog.models[...].
        std::string code = std::string("local f = assert(io.open('") + catalogPath + "', 'rb'))\n" +
            R"LUA(
            local text = f:read('a')
            f:close()
            local cat = fromJSON(text)
            assert(type(cat) == 'table' and cat.format == 1 and type(cat.models) == 'table', 'catalog format')
            local count = 0
            for _ in pairs(cat.models) do count = count + 1 end
            assert(count == cat.modelCount, 'model count: ' .. count .. ' vs ' .. tostring(cat.modelCount))
            local m = cat.models['11682']
            assert(m.dff == 'CutsceneCouch1.dff' and m.txd == 'csmafcouch', 'model names')
            assert(m.animation == nil and m.supported == true and m.flags == 0, 'null, boolean and integer fields')
            assert(m.drawDistance == 50 and math.type(m.drawDistance) == 'integer', 'float literal 50.0')
            _G.catalogModels = count
        )LUA";
        RunLua(vm, "catalog", code);
    }
}

bool Has(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

// Screen is the default 1920x1080 (no surface size is set in the host build).
void CheckGui(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "gui-create", R"LUA(
        win = guiCreateWindow(10, 20, 300, 200, "Hello Window", false)
        lab = guiCreateLabel(5, 40, 100, 20, "Child label", false, win)
        btn = guiCreateButton(0.5, 0.5, 0.25, 0.1, "OK", true)
        assert(getElementType(win) == "gui-window" and getElementType(lab) == "gui-label", 'types')
        assert(getElementType(btn) == "gui-button", 'button type')
        assert(isElement(win) and isElement(lab) and isElement(btn), 'gui elements are elements')
        assert(guiGetVisible(win) == true and guiGetVisible(lab) == true, 'visible on create')
        assert(guiGetText(win) == "Hello Window" and guiGetText(lab) == "Child label", 'text')
        assert(guiGetEnabled(btn) == true and guiGetAlpha(win) == 1, 'enabled and opaque on create')
        local x, y = guiGetPosition(btn, true)
        assert(x == 0.5 and y == 0.5, 'relative position kept')
        local px, py = guiGetPosition(btn, false)
        assert(px == 960 and py == 540, 'absolute position of relative element: ' .. px .. ' ' .. py)
        local w, h = guiGetSize(btn, false)
        assert(w == 480 and h == 108, 'absolute size of relative element')
        assert(showCursor(true) == true and isCursorShowing() == true, 'cursor flag')
    )LUA");

    std::string frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "R 10.0 20.0 300.0 200.0 "));
    CHECK(Has(frame, "Hello Window"));
    CHECK(Has(frame, "T 15.0 60.0 115.0 80.0 1.00 4294967295 0 Child label"));
    CHECK(Has(frame, "R 960.0 540.0 480.0 108.0 "));

    // A window hidden by the script is no longer drawn, and neither is its child.
    RunLua(vm, "gui-hide", "guiSetVisible(win, false)\nassert(guiGetVisible(win) == false)");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(!Has(frame, "Hello Window"));
    CHECK(!Has(frame, "Child label"));
    CHECK(Has(frame, "R 960.0 540.0 480.0 108.0 "));  // the button is a separate element

    // Shown again: the window and its child come back, with the window alpha inherited.
    RunLua(vm, "gui-show", "guiSetVisible(win, true)\nguiSetAlpha(win, 0.5)");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "Hello Window"));
    CHECK(Has(frame, "Child label"));
    CHECK(Has(frame, "2147483647 0 Child label"));  // white text at alpha 0.5 (127 of 255)
    RunLua(vm, "gui-alpha-reset", "guiSetAlpha(win, 1)");

    // Font scale: desktop pricedown is 30 px, so the draw line carries scale 2.
    RunLua(vm, "dx-font", R"LUA(
        assert(dxGetFontHeight(1, "default") == 15 and dxGetFontHeight(1, "pricedown") == 30)
        assert(dxGetFontHeight(2) == 30, 'default height at scale 2')
        -- dx calls are only valid in onClientRender: draw once from a render handler.
        local function once()
            dxDrawText("BIG", 0, 0, 10, 10, tocolor(255, 255, 255, 255), 1, "pricedown")
            dxDrawText("SMALL", 0, 0, 10, 10, tocolor(255, 255, 255, 255), 1, "default")
            removeEventHandler("onClientRender", root, once)
        end
        addEventHandler("onClientRender", root, once)
    )LUA");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "T 0.0 0.0 10.0 10.0 2.00 "));
    CHECK(Has(frame, "T 0.0 0.0 10.0 10.0 1.00 "));

    // destroyElement removes the element and its children; the element is no longer an element.
    RunLua(vm, "gui-destroy", "destroyElement(win)\nassert(not isElement(win) and not isElement(lab), 'children destroyed')");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(!Has(frame, "Hello Window"));
    CHECK(!Has(frame, "Child label"));
    CHECK(Has(frame, "R 960.0 540.0 480.0 108.0 "));
    RunLua(vm, "gui-cleanup", "destroyElement(btn)\nassert(not isElement(btn), 'button destroyed')");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(!Has(frame, "R 960.0 540.0"));
}

void CheckStopLifecycle(WorldRPG::Script::LuaVMStub& vm) {
    // Resource "demo" (net id 5, root 0x1234) and "other" (net id 6, root 0x2222). Both start
    // from one no-cache script each (packet 95).
    WorldRPG::Net::ResourceStartInfo demo;
    demo.name = "demo";
    demo.resourceId = 5;
    demo.entityId = 0x1234;
    demo.dynamicEntityId = 0x1235;
    demo.noClientCacheScripts = 1;
    demo.startCounter = 3;
    WorldRPG::Net::ResourceStartInfo other = demo;
    other.name = "other";
    other.resourceId = 6;
    other.entityId = 0x2222;
    other.dynamicEntityId = 0x2223;
    other.startCounter = 1;

    WorldRPG::Script::ActiveVmOnResourceStart(demo);
    WorldRPG::Script::ActiveVmOnResourceStart(other);

    const std::string demoScript = R"LUA(
        _G.demoStarts = (_G.demoStarts or 0) + 1
        addEventHandler("onClientResourceStart", root, function() _G.demoStarted = true end)
        addEventHandler("onClientRender", root, function() _G.demoRenders = (_G.demoRenders or 0) + 1 end)
        addEventHandler("onClientResourceStop", root, function(res) _G.demoStopped = res end)
        setTimer(function() _G.demoTicks = (_G.demoTicks or 0) + 1 end, 1, 0)
        _G.demoWin = guiCreateWindow(0, 0, 50, 50, "demo window", false)
    )LUA";
    const std::string otherScript = R"LUA(
        addEventHandler("onClientRender", root, function() _G.otherRenders = (_G.otherRenders or 0) + 1 end)
    )LUA";
    const std::vector<uint8_t> demoBody = NoCacheScriptBody(5, demoScript);
    const std::vector<uint8_t> otherBody = NoCacheScriptBody(6, otherScript);
    CHECK(WorldRPG::Script::ActiveVmFeedScripts(demoBody.data(), demoBody.size(), demoBody.size() * 8));
    CHECK(WorldRPG::Script::ActiveVmFeedScripts(otherBody.data(), otherBody.size(), otherBody.size() * 8));

    WorldRPG::Script::ActiveVmPulseResources();
    RunLua(vm, "after-start", "assert(_G.demoStarted == true and _G.demoStarts == 1, 'demo started once')");

    WorldRPG::Script::ActiveVmPump();
    WorldRPG::Script::ActiveVmPump();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "before-stop",
           "assert(_G.demoRenders == 3 and _G.otherRenders == 3, 'both resources render')\n"
           "assert(_G.demoTicks and _G.demoTicks >= 1, 'demo timer fired')\n"
           "_G.snapTicks, _G.snapRenders = _G.demoTicks, _G.demoRenders");

    // RESOURCE_STOP for net id 5: CResourceStopPacket writes the id as a uint16.
    WorldRPG::Net::BitStreamWriter stop;
    stop.Write(static_cast<uint16_t>(5));
    WorldRPG::Script::ActiveVmOnResourceStop(stop.GetData(), stop.GetByteLength(), stop.GetBitLength());
    RunLua(vm, "stop-event",
           "assert(_G.demoStopped ~= nil and _G.demoStopped.id == 0x1234, 'onClientResourceStop got the root')");
    RunLua(vm, "stop-gui", "assert(not isElement(_G.demoWin), 'stopped resource GUI elements are destroyed')");

    // The stopped resource's handlers and timers are gone; the other resource still runs.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    WorldRPG::Script::ActiveVmPump();
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "after-stop",
           "assert(_G.demoRenders == _G.snapRenders, 'demo render handler dropped')\n"
           "assert(_G.demoTicks == _G.snapTicks, 'demo timer dropped')\n"
           "assert(_G.otherRenders == 5, 'other resource keeps rendering')");

    // A second RESOURCE_STOP for the same id is unknown and does nothing.
    WorldRPG::Script::ActiveVmOnResourceStop(stop.GetData(), stop.GetByteLength(), stop.GetBitLength());
    RunLua(vm, "unknown-stop", "assert(_G.otherRenders == 5, 'unknown stop changes nothing')");

    // Too short a body is rejected without touching the VM.
    const uint8_t oneByte[] = {0x05};
    WorldRPG::Script::ActiveVmOnResourceStop(oneByte, sizeof(oneByte), 8);
    RunLua(vm, "short-stop", "assert(_G.otherRenders == 5, 'short body changes nothing')");
}

// dxDrawImage writes one "I" line per call. With no resource running, the call draws nothing
// and rectangles still do. A resource resolves the PNG through the file cache.
void CheckDxImage(WorldRPG::Script::LuaVMStub& vm) {
    WorldRPG::Net::HttpDownloader::SetResourceCacheRoot("/tmp/neon-dx-cache");

    RunLua(vm, "dx-image-noscript", R"LUA(
        local function once()
            dxDrawRectangle(1, 2, 3, 4, tocolor(255, 255, 255, 255))
            dxDrawText("plain", 0, 0, 10, 10)
            _G.imageWithoutScript = dxDrawImage(1, 2, 3, 4, "radar/radar00.png")
            _G.sectionWithoutScript = dxDrawImageSection(1, 2, 3, 4, 0, 0, 8, 8, "radar/radar00.png")
            removeEventHandler("onClientRender", root, once)
        end
        addEventHandler("onClientRender", root, once)
    )LUA");
    std::string frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "R 1.0 2.0 3.0 4.0 "));
    CHECK(Has(frame, "plain"));
    CHECK(frame.find("\nI ") == std::string::npos);
    CHECK(frame.rfind("I ", 0) != 0);
    RunLua(vm, "dx-image-noscript-result",
           "assert(_G.imageWithoutScript == false and _G.sectionWithoutScript == false, 'no resource draws no image')");

    WorldRPG::Net::ResourceStartInfo info;
    info.name = "radarres";
    info.resourceId = 9;
    info.entityId = 0x4444;
    info.dynamicEntityId = 0x4445;
    info.noClientCacheScripts = 1;
    info.startCounter = 1;
    WorldRPG::Script::ActiveVmOnResourceStart(info);
    const std::string script = R"LUA(
        addEventHandler("onClientRender", root, function()
            if _G.radarDrew then return end
            _G.radarDrew = true
            dxDrawRectangle(8, 9, 10, 11, tocolor(0, 0, 0, 255))
            dxDrawText("keep", 0, 0, 10, 10)
            _G.fullImage = dxDrawImage(1, 2, 3, 4, "radar/radar00.png", 0, 0, 0, tocolor(255, 255, 255, 255))
            _G.sectionImage = dxDrawImageSection(5, 6, 7, 8, 9, 10, 11, 12, "radar/radar01.png", 90, 1, 2, tocolor(255, 0, 0, 255))
            _G.escapedImage = dxDrawImage(1, 2, 3, 4, "../secret.png")
            _G.pedRotation = getPedRotation(localPlayer)
            _G.velX, _G.velY, _G.velZ = getElementVelocity(localPlayer)
            _G.pedArmor = getPedArmor(localPlayer)
        end)
    )LUA";
    const std::vector<uint8_t> body = NoCacheScriptBody(9, script);
    CHECK(WorldRPG::Script::ActiveVmFeedScripts(body.data(), body.size(), body.size() * 8));
    WorldRPG::Script::ActiveVmPulseResources();
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "R 8.0 9.0 10.0 11.0 "));
    CHECK(Has(frame, "keep"));
    CHECK(Has(frame, "I 1.0 2.0 3.0 4.0 0.0 0.0 -1.0 -1.0 4294967295 0.0 0.0 0.0 /tmp/neon-dx-cache/radarres/radar/radar00.png"));
    CHECK(Has(frame, "I 5.0 6.0 7.0 8.0 9.0 10.0 11.0 12.0 4294901760 90.0 1.0 2.0 /tmp/neon-dx-cache/radarres/radar/radar01.png"));
    CHECK(!Has(frame, "secret"));
    RunLua(vm, "dx-image-resource", R"LUA(
        assert(_G.fullImage == true and _G.sectionImage == true, 'resource image calls draw')
        assert(_G.escapedImage == false, 'parent path is rejected')
        assert(type(_G.pedRotation) == 'number' and _G.pedRotation >= 0 and _G.pedRotation < 360, 'heading in degrees')
        assert(_G.velX == 0 and _G.velY == 0 and _G.velZ == 0, 'local velocity')
        assert(_G.pedArmor == 0, 'armor sample')
    )LUA");
}

// Polls the game pulse that delivers fetchRemote completions. A failed probe is not a
// recorded check; the final RunLua reports the assertion that is still false.
bool WaitRemote(WorldRPG::Script::LuaVMStub& vm, const char* name, const std::string& assertCode, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    // One beat so a localhost callback can land before the first probe. executeBuffer logs
    // every failed probe, and the completion is usually ready within this window.
    WorldRPG::Script::ActiveVmPulseResources();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    while (std::chrono::steady_clock::now() < deadline) {
        WorldRPG::Script::ActiveVmPulseResources();
        const WorldRPG::Script::LuaExecResult result = vm.executeBuffer(assertCode.data(), assertCode.size(), name);
        if (result.success) {
            ++g_checks;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    WorldRPG::Script::ActiveVmPulseResources();
    return RunLua(vm, name, assertCode);
}

bool SameHttpStats(const WorldRPG::Net::HttpDownloader::Stats& before, const WorldRPG::Net::HttpDownloader::Stats& after) {
    return before.queued == after.queued && before.skippedDuplicate == after.skippedDuplicate &&
           before.cached == after.cached && before.downloaded == after.downloaded &&
           before.md5Verified == after.md5Verified && before.crcVerified == after.crcVerified &&
           before.mismatches == after.mismatches && before.failed == after.failed && before.bytes == after.bytes &&
           before.dropped == after.dropped && before.failures.size() == after.failures.size() &&
           before.samples.size() == after.samples.size();
}

// Local HTTP peer for the fetchRemote checks. Hold waits until Release before the response,
// so abort can run while the client is blocked in the read.
class LoopbackHttp {
public:
    enum class Mode { Respond, Hold };

    explicit LoopbackHttp(Mode mode) : mode_(mode) {
        listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_ < 0) return;
        int yes = 1;
        ::setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listen_, 4) != 0) {
            ::close(listen_);
            listen_ = -1;
            return;
        }
        socklen_t len = sizeof(addr);
        if (::getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            ::close(listen_);
            listen_ = -1;
            return;
        }
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { Serve(); });
    }

    ~LoopbackHttp() {
        stop_.store(true);
        Release();
        if (listen_ >= 0) ::shutdown(listen_, SHUT_RDWR);
        if (thread_.joinable()) thread_.join();
        if (listen_ >= 0) {
            ::close(listen_);
            listen_ = -1;
        }
    }

    uint16_t port() const { return port_; }
    bool ok() const { return port_ != 0; }

    bool WaitAccepted(int timeoutMs) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            if (accepted_.load() > 0) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return accepted_.load() > 0;
    }

    void Release() {
        release_.store(true);
        releaseCv_.notify_all();
    }

private:
    void Serve() {
        while (!stop_.load()) {
            pollfd pfd{};
            pfd.fd = listen_;
            pfd.events = POLLIN;
            if (::poll(&pfd, 1, 50) <= 0) continue;
            const int client = ::accept(listen_, nullptr, nullptr);
            if (client < 0) continue;
            std::string request;
            char buf[1024];
            while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192 && !stop_.load()) {
                pollfd in{};
                in.fd = client;
                in.events = POLLIN;
                if (::poll(&in, 1, 200) <= 0) continue;
                const ssize_t n = ::recv(client, buf, sizeof(buf), 0);
                if (n <= 0) break;
                request.append(buf, static_cast<size_t>(n));
            }
            if (mode_ == Mode::Hold) {
                accepted_.fetch_add(1);
                std::unique_lock<std::mutex> lock(releaseMu_);
                releaseCv_.wait(lock, [this] { return release_.load() || stop_.load(); });
            }
            if (!stop_.load()) {
                const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 7\r\nConnection: close\r\n\r\nok-body";
                ::send(client, response, sizeof(response) - 1, MSG_NOSIGNAL);
            }
            ::close(client);
        }
    }

    Mode mode_;
    int listen_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<bool> release_{false};
    std::atomic<int> accepted_{0};
    std::mutex releaseMu_;
    std::condition_variable releaseCv_;
    std::thread thread_;
};

uint16_t ClosedTcpPort() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return 0;
    }
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::close(fd);
        return 0;
    }
    const uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

// Enough of an MTA peer for RakNetWrapper::SendPacket to pass isConnected().
// The handshake bytes are unchanged; this only answers OPEN and the connection request.
class LoopbackMta {
public:
    LoopbackMta() {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        socklen_t len = sizeof(addr);
        if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        port_ = ntohs(addr.sin_port);
        key_ = {1, 2, 3, 4, 5, 6, 7, 8};
        thread_ = std::thread([this] { Serve(); });
    }

    ~LoopbackMta() {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    uint16_t port() const { return port_; }
    bool ok() const { return port_ != 0; }
    int datagrams() const { return datagrams_.load(); }
    int opened() const { return opened_.load(); }
    int frames() const { return frames_.load(); }
    int accepts() const { return accepts_.load(); }

private:
    static std::vector<uint8_t> OpenReply(const std::vector<uint8_t>& key) {
        using WorldRPG::Net::Mta::OfflineMagic;
        std::vector<uint8_t> reply;
        reply.push_back(0x0a);
        reply.insert(reply.end(), OfflineMagic.begin(), OfflineMagic.end());
        reply.insert(reply.end(), 16, 0);
        reply.push_back(0xed);
        reply.push_back(0x80);
        reply.push_back(1);
        reply.push_back(1);
        reply.push_back(0x6e);
        reply.push_back(static_cast<uint8_t>(key.size()));
        reply.insert(reply.end(), key.begin(), key.end());
        reply.push_back(0x1a);
        reply.push_back(0);
        reply.push_back(0xde);
        return reply;
    }

    void Serve() {
        using namespace WorldRPG::Net::Mta;
        std::optional<Cipher> cipher;
        uint8_t selector = 1;
        uint32_t nextNumber = 0;
        bool sentAccept = false;
        const std::vector<uint8_t> reply = OpenReply(key_);
        while (!stop_.load()) {
            pollfd pfd{};
            pfd.fd = fd_;
            pfd.events = POLLIN;
            if (::poll(&pfd, 1, 20) <= 0) continue;
            uint8_t buf[2048];
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            const ssize_t n = ::recvfrom(fd_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n <= 0) continue;
            datagrams_.fetch_add(1);
            if (buf[0] == 0x09) {
                ::sendto(fd_, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&from), fromLen);
                opened_.fetch_add(1);
                continue;
            }
            if (!cipher) cipher = Cipher::Create(key_);
            if (!cipher) continue;
            const auto plain = cipher->Open(std::span<const uint8_t>(buf, static_cast<size_t>(n)));
            const auto* bytes = std::get_if<Bytes>(&plain);
            if (!bytes) continue;
            const auto decoded = DecodeFrame(*bytes);
            const auto* frame = std::get_if<ReliableFrame>(&decoded);
            if (!frame) continue;
            frames_.fetch_add(1);
            ReliableFrame out;
            out.remoteTime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                        std::chrono::steady_clock::now().time_since_epoch())
                                                        .count());
            for (const ReliableMessage& message : frame->messages) {
                if (message.reliability >= 2) out.acknowledgments.push_back(AckRange{message.number, message.number});
            }
            // EncodeFrame rejects acknowledgments without ackTime. The client stamps remoteTime
            // on the connection request; that value is the ack time it expects.
            if (!out.acknowledgments.empty()) out.ackTime = frame->remoteTime.value_or(0);
            const bool includeAccept = !sentAccept;
            if (includeAccept) {
                ReliableMessage accept;
                accept.number = nextNumber++;
                accept.reliability = 2;
                accept.bitLength = 15 * 8;
                accept.payload.assign(15, 0);
                accept.payload[0] = 14;
                out.messages.push_back(std::move(accept));
            }
            const auto encoded = EncodeFrame(out);
            const auto* wire = std::get_if<Bytes>(&encoded);
            if (!wire) continue;
            const Bytes sealed = cipher->Seal(*wire, selector++);
            if (::sendto(fd_, sealed.data(), sealed.size(), 0, reinterpret_cast<sockaddr*>(&from), fromLen) < 0) continue;
            if (includeAccept) {
                sentAccept = true;
                accepts_.fetch_add(1);
            }
        }
    }

    int fd_ = -1;
    uint16_t port_ = 0;
    std::vector<uint8_t> key_;
    std::atomic<bool> stop_{false};
    std::atomic<int> datagrams_{0};
    std::atomic<int> opened_{0};
    std::atomic<int> frames_{0};
    std::atomic<int> accepts_{0};
    std::thread thread_;
};

void CheckNetworkCalls(WorldRPG::Script::LuaVMStub& vm) {
    CHECK(WorldRPG::Net::NormVectorBytesMatchVehicleSync());

    {
        LoopbackHttp server(LoopbackHttp::Mode::Respond);
        CHECK(server.ok());
        if (server.ok()) {
            const std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/ok";
            RunLua(vm, "fetch-ok-start",
                   "_G.okCalls = 0\n"
                   "local req = fetchRemote(\"" +
                       url +
                       "\", {queueName = \"ok\", connectionAttempts = 1, connectTimeout = 2000}, "
                       "function(body, info)\n"
                       "  _G.okCalls = _G.okCalls + 1\n"
                       "  _G.okBody = body\n"
                       "  _G.okCode = info.statusCode\n"
                       "  _G.okSuccess = info.success\n"
                       "end)\n"
                       "assert(type(req) == \"table\" and req.remoteRequest == true and type(req.id) == \"number\", \"handle\")");
            WaitRemote(vm, "fetch-ok",
                       "assert(_G.okCalls == 1 and _G.okCode == 200 and _G.okSuccess == true and _G.okBody == \"ok-body\", "
                       "\"status \" .. tostring(_G.okCode) .. \" body \" .. tostring(_G.okBody))",
                       3000);
        }
    }

    {
        const uint16_t closed = ClosedTcpPort();
        CHECK(closed != 0);
        if (closed != 0) {
            const std::string url = "http://127.0.0.1:" + std::to_string(closed) + "/no";
            RunLua(vm, "fetch-refused-start",
                   "_G.refusedCalls = 0\n"
                   "fetchRemote(\"" +
                       url +
                       "\", {queueName = \"refused\", connectionAttempts = 1, connectTimeout = 1000}, "
                       "function(body, info)\n"
                       "  _G.refusedCalls = _G.refusedCalls + 1\n"
                       "  _G.refusedCode = info.statusCode\n"
                       "  _G.refusedSuccess = info.success\n"
                       "end)");
            WaitRemote(vm, "fetch-refused",
                       "assert(_G.refusedCalls == 1 and _G.refusedSuccess == false and _G.refusedCode ~= 200, "
                       "\"refused statusCode \" .. tostring(_G.refusedCode))",
                       3000);
        }
    }

    {
        LoopbackHttp server(LoopbackHttp::Mode::Hold);
        CHECK(server.ok());
        if (server.ok()) {
            const std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/slow";
            RunLua(vm, "fetch-abort-start",
                   "_G.abortCalls = 0\n"
                   "_G.abortReq = fetchRemote(\"" +
                       url +
                       "\", {queueName = \"abortq\", connectionAttempts = 1, connectTimeout = 4000}, "
                       "function()\n"
                       "  _G.abortCalls = _G.abortCalls + 1\n"
                       "end)\n"
                       "assert(type(_G.abortReq) == \"table\" and _G.abortReq.remoteRequest == true, \"abort handle\")\n"
                       "local info = getRemoteRequestInfo(_G.abortReq)\n"
                       "assert(type(info) == \"table\" and info.resource == false, \"resource is false\")\n"
                       "local listed = false\n"
                       "for _, item in ipairs(getRemoteRequests()) do\n"
                       "  if item.id == _G.abortReq.id then listed = true end\n"
                       "end\n"
                       "assert(listed, \"live request is listed\")");
            CHECK(server.WaitAccepted(3000));
            RunLua(vm, "fetch-abort",
                   "assert(abortRemoteRequest(_G.abortReq) == true, \"abort\")\n"
                   "local still = false\n"
                   "for _, item in ipairs(getRemoteRequests()) do\n"
                   "  if item.id == _G.abortReq.id then still = true end\n"
                   "end\n"
                   "assert(still == false, \"aborted request leaves the list\")");
            server.Release();
            for (int i = 0; i < 25; ++i) {
                WorldRPG::Script::ActiveVmPulseResources();
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            RunLua(vm, "fetch-abort-quiet", "assert(_G.abortCalls == 0, \"late completion ran the callback\")");
        }
    }

    {
        const auto before = WorldRPG::Net::HttpDownloader::GetStats();
        RunLua(vm, "fetch-https-start",
               "_G.httpsCalls = 0\n"
               "fetchRemote(\"https://127.0.0.1/no\", {queueName = \"httpsq\", connectionAttempts = 1, connectTimeout = 1000}, "
               "function(body, info)\n"
               "  _G.httpsCalls = _G.httpsCalls + 1\n"
               "  _G.httpsCode = info.statusCode\n"
               "  _G.httpsSuccess = info.success\n"
               "end)");
        WaitRemote(vm, "fetch-https",
                   "assert(_G.httpsCalls == 1 and _G.httpsSuccess == false and _G.httpsCode == 1, "
                   "\"https statusCode \" .. tostring(_G.httpsCode))",
                   1000);
        const auto after = WorldRPG::Net::HttpDownloader::GetStats();
        CHECK(SameHttpStats(before, after));
    }

    RunLua(vm, "get-server-ip",
           "assert(type(getServerIp()) == \"string\", \"getServerIp\")\n"
           "assert(type(getServerIp(true)) == \"string\", \"getServerIp with port\")");

    {
        LoopbackMta peer;
        CHECK(peer.ok());
        if (peer.ok()) {
            WorldRPG::Net::RakNetWrapper net;
            const bool started = net.StartNetwork("127.0.0.1", peer.port());
            CHECK(started);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (started && !net.IsConnected() && std::chrono::steady_clock::now() < deadline) {
                net.DoPulse();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!net.IsConnected()) {
                std::printf("FAIL mta loopback not connected (datagrams=%d opened=%d frames=%d accepts=%d)\n",
                            peer.datagrams(), peer.opened(), peer.frames(), peer.accepts());
            }
            CHECK(net.IsConnected());
            if (net.IsConnected()) {
                RunLua(vm, "usage-before",
                       "_G.usageBefore = {}\n"
                       "local count = getNetworkUsageData().out.count\n"
                       "for id = 0, 255 do _G.usageBefore[id] = count[id] end");
                // sendBuf[0] is the wire id. A packet id other than USER_PACKET_ENUM (99)
                // is stored as that id, so one send of 42 increments out.count[42].
                WorldRPG::Net::BitStreamWriter stream;
                stream.Write(static_cast<uint8_t>(0x11));
                CHECK(net.SendPacket(42, stream, WorldRPG::Net::PacketPriority::HIGH,
                                     WorldRPG::Net::PacketReliability::UNRELIABLE));
                RunLua(vm, "usage-after",
                       "local count = getNetworkUsageData().out.count\n"
                       "local raised = -1\n"
                       "for id = 0, 255 do\n"
                       "  local delta = count[id] - _G.usageBefore[id]\n"
                       "  if delta == 1 then\n"
                       "    assert(raised < 0, \"two wire ids increased\")\n"
                       "    raised = id\n"
                       "  else\n"
                       "    assert(delta == 0, \"id \" .. id .. \" changed by \" .. tostring(delta))\n"
                       "  end\n"
                       "end\n"
                       "assert(raised == 42, \"wire id \" .. tostring(raised))");
            }
            net.Shutdown();
        }
    }

    WorldRPG::Script::ResourceLoader::Reset();
}

} // namespace

int main(int argc, char** argv) {
    CheckPacketLayouts();

    // RESOURCE_START sets the root the phone reports for the resource (shim layout included).
    WorldRPG::Net::ResourceStartInfo root;
    root.name = "rootcheck";
    root.entityId = 0x0ABC;
    root.dynamicEntityId = 0x0ABD;
    WorldRPG::ServerWorld::OnResourceStart(root);
    CHECK(WorldRPG::ServerWorld::LastResourceRoot() == 0x0ABC);

    WorldRPG::Script::LuaVMStub vm;
    CHECK(vm.initialize());
    WorldRPG::ServerWorld::SetLocalPlayer(777);

    CheckJson(vm, argc > 1 ? argv[1] : nullptr);
    CheckGui(vm);
    CheckStopLifecycle(vm);
    CheckDxImage(vm);
    CheckNetworkCalls(vm);

    if (argc > 1) {
        RunLua(vm, "catalog-count", "assert(_G.catalogModels == 1435, 'catalog holds 1435 models')");
    }
    std::printf("neon_resource_tests: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
