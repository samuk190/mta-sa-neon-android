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
#include "EngineModelLoader.h"
#include <filesystem>
#include <fstream>
#include "LuaEventRegistry.h"
#include "LuaFileApi.h"
#include "LuaKeyBindApi.h"
#include "LuaGuiApi.h"
#include "LuaMtaWindowApi.h"
#include "LuaVMStub.h"
#include "ModelNames.h"
#include "ModelInfoRead.h"
#include "ObjectGroups.h"
#include "SurfaceInfo.h"
#include "Streaming.h"
#include "EngineWorld.h"
#include "EngineRenderer.h"
#include "CullZones.h"
#include "LuaWeaponApi.h"
#include "LuaCryptApi.h"
#include "ClientWeapon.h"
#include "MtaCipher.h"
#include "MtaOfflineCodec.h"
#include "MtaReliabilityCodec.h"
#include "RemoteVehicle.h"
#include "PedQueries.h"
#include "gtasa/GameEngine.h"
#include "test_ped_queries.h"
#include "ResourceFiles.h"
#include "ResourceLoader.h"
#include "ResourcePackets.h"
#include "ServerWorld.h"
#include "entity/EntityAddTypes.h"
#include "net/raknet/RakNetWrapper.h"
#include "lua.hpp"
#include "Utf8.h"

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
#include <sys/stat.h>
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

// Desktop ENTITY_ADD object/weapon layout at peer 0x0030. A following sentinel
// detects target sub-field shifts; a one-bit truncation must fail atomically.
void WriteTestBits(WorldRPG::Net::BitStreamWriter& w, uint32_t value, size_t count) {
    uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                        static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    w.WriteBits(bytes, count);
}

void WriteWeaponBody(WorldRPG::Net::BitStreamWriter& w, uint8_t targetType, int targetKind = -1,
                     uint32_t owner = 903, bool changed = true) {
    auto bits = [&](uint32_t v, size_t n) { WriteTestBits(w, v, n); };
    bits(1024, 24); bits(2048, 24); w.Write(3.f); // SPositionSync
    bits(0, 16); bits(0, 16); bits(0, 16);       // rotation
    w.WriteCompressed(355u, 2); w.WriteCompressed(0u, 1); // model and alpha
    bits(0, 1); bits(0x1FFFF, 17);               // LOD
    bits(0, 1); bits(1, 1); bits(0, 1); bits(0, 1); // double sided, breakable, all dimensions, moving
    bits(1, 1); bits(1, 1); bits(1, 1);           // uniform, unit scale, frozen
    bits(2000, 11); bits(0, 1); bits(1, 1);      // health, broken, respawn
    bits(targetType, 3);
    if (targetType == 0) { w.Write(10.f); w.Write(-20.f); w.Write(30.f); }
    if (targetType == 1) {
        bits(903, 17);
        if (targetKind == 1 || targetKind == 14) w.Write(static_cast<uint8_t>(42));
        else if (targetKind == 2) bits(9, 4);
    }
    bits(changed ? 1 : 0, 1);
    if (changed) { bits(3456, 12); w.Write(0.75f); w.Write(80.f); w.Write(120.f); }
    for (int i = 0; i < 13; ++i) bits(i % 2, 1);
    bits(2, 4); w.Write(static_cast<uint16_t>(1234)); w.Write(static_cast<uint16_t>(27));
    bits(owner, 17);
}

void WriteWeaponHeader(WorldRPG::Net::BitStreamWriter& w, uint32_t id, uint32_t attachedTo = 0xFFFFFFFFu) {
    WriteTestBits(w, id, 17); w.Write(static_cast<uint8_t>(18)); WriteTestBits(w, 0, 17);
    w.Write(static_cast<uint8_t>(0)); w.WriteCompressed(0u, 2);
    WriteTestBits(w, attachedTo != 0xFFFFFFFFu, 1);
    if (attachedTo != 0xFFFFFFFFu) {
        WriteTestBits(w, attachedTo, 17);
        WriteTestBits(w, 1024, 24); WriteTestBits(w, static_cast<uint32_t>(-2048) & 0xFFFFFFu, 24); w.Write(3.f);
        WriteTestBits(w, 0, 16); WriteTestBits(w, 16384, 16); WriteTestBits(w, 32768, 16);
    }
    WriteTestBits(w, 1, 1); // collisions (no callProp at 0x0030)
    w.WriteCompressed(0u, 2); w.WriteCompressed(0u, 2); // custom count, name length (no syncTimeContext at 0x0030)
}

void CheckWeaponSnapshots(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG;
    RunLua(vm, "projectile-invalid-inputs", R"LUA(
        assert(type(createProjectile) == 'function', 'projectile bindings installed')
        assert(createProjectile() == false and createProjectile(localPlayer, 16) == false, 'no creation outside a resource')
        assert(getProjectileCreator({}) == false and getProjectileForce({}) == false, 'invalid projectile')
        assert(getProjectileType({}) == false and getProjectileTarget({}) == false, 'invalid projectile type/target')
        assert(getProjectileCounter({}) == false and setProjectileCounter({}, 10) == false, 'invalid projectile timer')
    )LUA");
    for (int targetKind : {-1, 1, 2, 14}) {
        Net::BitStreamWriter w;
        WriteWeaponBody(w, 1, targetKind);
        const auto resolve = +[](uint32_t) -> int { return 1; };
        // Select a noncapturing resolver for the type-specific width.
        Entity::ElementKindResolver resolver = resolve;
        if (targetKind == -1) resolver = +[](uint32_t) -> int { return -1; };
        if (targetKind == 2) resolver = +[](uint32_t) -> int { return 2; };
        if (targetKind == 14) resolver = +[](uint32_t) -> int { return 14; };
        Entity::EntityCommon common;
        common.id = 1001; common.type = 18;
        Entity::Visual visual;
        Net::BitStreamReader reader(w.GetData(), w.GetByteLength(), w.GetBitLength());
        CHECK(Entity::ReadEntityBodyObject(reader, common, visual, resolver));
        CHECK(reader.GetReadOffsetBits() == w.GetBitLength());
        CHECK(visual.weapon && visual.weapon->ammo == 1234 && visual.weapon->clipAmmo == 27);
        if (visual.weapon) {
            CHECK(visual.weapon->damage == 3456 && visual.weapon->accuracy == 0.75f);
            CHECK(visual.weapon->state == 2 && visual.weapon->flags[1] && !visual.weapon->flags[2]);
            CHECK(visual.weapon->subTarget == (targetKind == 2 ? 9 : (targetKind == 1 || targetKind == 14 ? 42 : 0)));
            CHECK(visual.weapon->owner == (targetKind == 1 ? 903u : 0xFFFFFFFFu));
        }
        // A one-bit-short body must not publish a partially read snapshot.
        Entity::Visual shortVisual;
        Net::BitStreamReader shortReader(w.GetData(), w.GetByteLength(), w.GetBitLength() - 1);
        CHECK(!Entity::ReadEntityBodyObject(shortReader, common, shortVisual, resolver));
        CHECK(!shortVisual.weapon);
    }
    ServerWorld::NotePlayerSkin(903, 7);
    Net::BitStreamWriter packet;
    packet.WriteCompressed(4u, 4);
    WriteWeaponHeader(packet, 1001); WriteWeaponBody(packet, 1, 1);
    WriteWeaponHeader(packet, 1002); WriteWeaponBody(packet, 0);
    WriteWeaponHeader(packet, 1003); WriteWeaponBody(packet, 2);
    WriteWeaponHeader(packet, 1004); WriteWeaponBody(packet, 2, -1, 777, false);
    ServerWorld::OnEntityAdd(packet.GetData(), packet.GetByteLength(), packet.GetBitLength());
    RunLua(vm, "weapon-snapshot-queries", R"LUA(
        local weapon = {id = 1001, element = true}
        assert(isElement(weapon) and getElementType(weapon) == 'weapon', 'weapon element type')
        assert(getWeaponAmmo(weapon) == 1234 and getWeaponClipAmmo(weapon) == 27, 'ammo survived the target sub-field')
        assert(getWeaponState(weapon) == 'reloading' and getWeaponOwner(weapon).id == 903, 'state and owner')
        assert(getWeaponTarget(weapon).id == 903, 'player target')
        local x, y, z = getWeaponTarget({id = 1002, element = true})
        assert(x == 10 and y == -20 and z == 30, 'vector target: following record stays aligned')
        assert(getWeaponTarget({id = 1003, element = true}) == nil, 'fixed target is nil')
        local localOwned = {id = 1004, element = true}
        assert(getWeaponOwner(localOwned).id == 777 and getWeaponAmmo(localOwned) == 1234, 'local owner and unchanged stats')
        assert(getWeaponAmmo() == false and getWeaponClipAmmo(903) == false, 'invalid weapon')
        assert(getWeaponState({id = 903}) == false and getWeaponOwner({}) == false, 'player is not a weapon')
        -- Server weapons are CClientWeapon AK-47s (CPacketHandler.cpp:3181); setters change the local copy.
        local fixed = {id = 1003, element = true}
        assert(getWeaponFiringRate(fixed) == math.floor((getOriginalWeaponProperty(30, 'std', 'anim_loop_stop') -
            getOriginalWeaponProperty(30, 'std', 'anim_loop_start')) * 1000), 'server weapon fire rate is the AK-47 STD rate')
        assert(setWeaponAmmo(fixed, 55) and getWeaponAmmo(fixed) == 55, 'server weapon ammo set locally')
        assert(setWeaponClipAmmo(fixed, 4) and getWeaponClipAmmo(fixed) == 4, 'server weapon clip set locally')
        assert(getWeaponProperty(fixed, 'std', 'damage') == 3456, 'changed stats from the packet')
        assert(getWeaponFlags(fixed, 'instant_reload') == true and getWeaponFlags(fixed, 'disable_model') == false, 'packet flags')
        assert(setWeaponState(fixed, 'ready') and getWeaponState(fixed) == 'ready', 'server weapon state set locally')
        assert(setWeaponAmmo(fixed, 1e20) == false and setWeaponClipAmmo(fixed, -1e20) == false, 'ammo out of int range')
        assert(setWeaponTarget(fixed, {id = 4242}) == false, 'unknown target element')
    )LUA");
    {
        // ENTITY_REMOVE and ENTITY_ADD of 1003 between two script calls: a new CClientWeapon.
        Net::BitStreamWriter gone;
        WriteTestBits(gone, 1003, 17);
        ServerWorld::OnEntityRemove(gone.GetData(), gone.GetByteLength(), gone.GetBitLength());
        Net::BitStreamWriter again;
        again.WriteCompressed(1u, 4);
        WriteWeaponHeader(again, 1003); WriteWeaponBody(again, 2, -1, 903, false);
        ServerWorld::OnEntityAdd(again.GetData(), again.GetByteLength(), again.GetBitLength());
    }
    RunLua(vm, "weapon-id-reuse", R"LUA(
        local fixed = {id = 1003, element = true}
        assert(getWeaponAmmo(fixed) == 1234 and getWeaponClipAmmo(fixed) == 27, 'reused id reads the new snapshot')
        assert(getWeaponState(fixed) == 'reloading', 'reused id state from the new snapshot')
    )LUA");
    ServerWorld::OnPlayerQuit(903);
    ServerWorld::NotePlayerSkin(903, 8); // ID reuse must not restore old references.
    RunLua(vm, "weapon-references-cleared", R"LUA(
        local weapon = {id = 1001, element = true}
        assert(getWeaponOwner(weapon) == false and getWeaponTarget(weapon) == nil, 'deleted owner/target stay cleared on reuse')
    )LUA");
    Net::BitStreamWriter removed;
    for (uint32_t id : {1001u, 1002u, 1003u, 1004u}) WriteTestBits(removed, id, 17);
    ServerWorld::OnEntityRemove(removed.GetData(), removed.GetByteLength(), removed.GetBitLength());
    RunLua(vm, "weapon-removed", "assert(getWeaponAmmo({id = 1001}) == false and getWeaponTarget({id = 1002}) == false)");
    ServerWorld::OnPlayerQuit(903);
}

// encodeString / decodeString / passwordHash / passwordVerify / generateKeyPair (CLuaCryptDefs.cpp).
void CheckCrypto(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "crypt-sync", R"LUA(
        assert(encodeString('base64', 'hello') == 'aGVsbG8=', 'base64')
        assert(decodeString('base64', 'aGVsbG8=') == 'hello', 'base64 decode')
        local url = encodeString('base64', '\251\255\254', {variant = 'url'})
        assert(url and not url:find('[+/]') and decodeString('base64', url, {variant = 'URL'}) == '\251\255\254', 'base64 url')
        assert(encodeString('base64', 'x', {variant = 'hex'}) == false, 'bad base64 variant')
        local b32 = encodeString('base32', 'hello world')
        assert(b32 and decodeString('base32', b32) == 'hello world', 'base32')
        local b32hex = encodeString('base32', 'hello', {variant = 'hex'})
        assert(b32hex and b32hex ~= encodeString('base32', 'hello') and decodeString('base32', b32hex, {variant = 'hex'}) == 'hello', 'base32 hex')

        local text = string.rep('zlib round trip ', 50)
        local gz = encodeString('zlib', text)
        assert(gz and gz:byte(1) == 0x1F and gz:byte(2) == 0x8B and #gz < #text, 'gzip by default')
        assert(decodeString('zlib', gz) == text, 'auto-detected gzip')
        local raw = encodeString('zlib', text, {format = 'raw', compression = 1, strategy = 'rle'})
        assert(raw and decodeString('zlib', raw, {format = 'raw'}) == text, 'raw deflate')
        assert(decodeString('zlib', encodeString('zlib', text, {format = 'zlib'})) == text, 'zlib header')
        assert(encodeString('zlib', text, {compression = 10}) == false, 'compression range')
        assert(encodeString('zlib', text, {format = 'nope'}) == false, 'bad format')
        assert(encodeString('zlib', text, {strategy = 'nope'}) == false, 'bad strategy')
        assert(decodeString('zlib', 'not compressed data') == false, 'zlib error')

        -- encodeString tea is the raw SharedUtil::TeaEncode; teaEncode is the same bytes in base64.
        local tea = encodeString('tea', 'secret message', {key = 'k3y'})
        assert(tea and base64Encode(tea) == teaEncode('secret message', 'k3y'), 'tea matches teaEncode')
        assert(decodeString('tea', tea, {key = 'k3y'}):sub(1, 14) == 'secret message', 'tea decode')
        assert(encodeString('tea', 'x', {}) == false and encodeString('tea', 'x') == false, 'tea needs key and table')

        local key = '0123456789abcdef'
        local cipher, iv = encodeString('aes128', 'aes payload', {key = key})
        assert(#cipher == #'aes payload' and #iv == 16, 'aes128 ctr: same size and a 16 byte iv')
        assert(decodeString('aes128', cipher, {key = key, iv = iv}) == 'aes payload', 'aes128 round trip')
        assert(encodeString('aes128', 'x', {key = 'short'}) == false, 'aes key length')
        assert(decodeString('aes128', cipher, {key = key, iv = 'short'}) == false, 'aes iv length')

        local private, public = generateKeyPair('rsa', {size = 1024})
        assert(type(private) == 'string' and type(public) == 'string' and #private > #public, 'rsa key pair (DER)')
        local sealed = encodeString('rsa', 'rsa payload', {key = public})
        assert(sealed and #sealed == 128 and decodeString('rsa', sealed, {key = private}) == 'rsa payload', 'rsa oaep round trip')
        assert(encodeString('rsa', 'x', {key = 'not a key'}) == false, 'bad rsa key')
        assert(encodeString('rsa', 'x', {}) == false, 'rsa needs key')
        assert(pcall(generateKeyPair, 'rsa', {}) == false, 'missing size is an error')
        assert(pcall(generateKeyPair, 'dsa', {size = 1024}) == false, 'unknown algorithm is an error')
        assert(generateKeyPair('rsa', {size = 8}) == false, 'too small key: warning and false')

        -- crypt_blowfish reference vector ("U*U", salt CCC...).
        local reference = '$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW'
        assert(passwordVerify('U*U', reference) == nil, '2a needs insecureBcrypt')
        assert(passwordVerify('U*U', reference, {insecureBcrypt = true}) == true, '2a reference verifies')
        assert(passwordVerify('U*V', reference, {insecureBcrypt = true}) == false, 'wrong password')
        local salted = passwordHash('U*U', 'bcrypt', {cost = 5, salt = 'CCCCCCCCCCCCCCCCCCCCC.'})
        assert(salted == '$2y$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW', 'custom salt hash: ' .. tostring(salted))
        local hash = passwordHash('pw', 'bcrypt', {cost = 4})
        assert(hash and #hash == 60 and hash:sub(1, 7) == '$2y$04$', 'random salt hash')
        assert(passwordHash('pw', 'bcrypt', {cost = 4}) ~= hash, 'salts differ')
        assert(passwordVerify('pw', hash) == true and passwordVerify('px', hash) == false, 'verify')
        assert(passwordVerify('pw', 'plain') == nil and passwordVerify('pw') == nil, 'unknown hash / bad args')
        assert(passwordHash('pw', 'bcrypt', {cost = 'abc'}) == false, 'bad cost')
        assert(passwordHash('pw', 'bcrypt', {cost = 99}) == false, 'cost out of range')
        assert(passwordHash('pw', 'md5', {}) == false and passwordHash('pw', 'bcrypt') == false, 'bad algorithm / missing table')

        _G.cryptAsync = {}
        assert(encodeString('base64', 'hi', function(r) _G.cryptAsync.b64 = r end) == true, 'async base64')
        assert(decodeString('zlib', 'bad', function(r) _G.cryptAsync.zlib = r end) == true, 'async zlib error')
        assert(encodeString('aes128', 'async aes', {key = key}, function(c, v) _G.cryptAsync.aes = {c, v} end) == true, 'async aes')
        assert(passwordHash('pw', 'bcrypt', {cost = 4}, function(h) _G.cryptAsync.hash = h end) == true, 'async hash')
        assert(passwordVerify('pw', hash, function(ok) _G.cryptAsync.verify = ok end) == true, 'async verify')
        assert(generateKeyPair('rsa', {size = 1024}, function(a, b) _G.cryptAsync.keys = {a, b} end) == true, 'async keys')
        assert(encodeString('base64', 'x', 5) == false, 'callback must be a function')
    )LUA");
    WorldRPG::Script::CryptWaitIdleForTests();
    WorldRPG::Script::CryptDeliverCompletions();
    RunLua(vm, "crypt-async", R"LUA(
        local r = _G.cryptAsync
        assert(r.b64 == 'aGk=', 'async base64 result')
        assert(r.zlib == false, 'async zlib error result')
        assert(decodeString('aes128', r.aes[1], {key = '0123456789abcdef', iv = r.aes[2]}) == 'async aes', 'async aes result')
        assert(type(r.hash) == 'string' and passwordVerify('pw', r.hash) == true, 'async hash result')
        assert(r.verify == true, 'async verify result')
        assert(type(r.keys[1]) == 'string' and decodeString('rsa', encodeString('rsa', 'k', {key = r.keys[2]}), {key = r.keys[1]}) == 'k', 'async keys result')
    )LUA");

    // A callback runs as the resource that asked (desktop: its own CLuaMain), and a stopped
    // resource's pending callback is dropped.
    auto start = [](const char* name, uint16_t netId, uint32_t entity) {
        WorldRPG::Net::ResourceStartInfo info;
        info.name = name;
        info.resourceId = netId;
        info.entityId = entity;
        info.dynamicEntityId = entity + 1;
        info.noClientCacheScripts = 1;
        info.startCounter = 1;
        WorldRPG::Script::ActiveVmOnResourceStart(info);
    };
    start("cryptowner", 90, 0x3900);
    start("cryptstopped", 91, 0x3910);
    const std::vector<uint8_t> ownerBody = NoCacheScriptBody(90, R"LUA(
        encodeString('base64', 'owner', function(r)
            local res = getThisResource()
            _G.cryptOwner = {result = r, name = res and getResourceName(res)}
        end)
    )LUA");
    const std::vector<uint8_t> stoppedBody = NoCacheScriptBody(91, R"LUA(
        encodeString('base64', 'late', function() _G.cryptStoppedRan = true end)
    )LUA");
    CHECK(WorldRPG::Script::ActiveVmFeedScripts(ownerBody.data(), ownerBody.size(), ownerBody.size() * 8));
    CHECK(WorldRPG::Script::ActiveVmFeedScripts(stoppedBody.data(), stoppedBody.size(), stoppedBody.size() * 8));
    WorldRPG::Script::ActiveVmPulseResources();
    WorldRPG::Script::CryptWaitIdleForTests();
    WorldRPG::Net::BitStreamWriter stop;
    stop.Write(static_cast<uint16_t>(91));
    WorldRPG::Script::ActiveVmOnResourceStop(stop.GetData(), stop.GetByteLength(), stop.GetBitLength());
    WorldRPG::Script::CryptDeliverCompletions();
    RunLua(vm, "crypt-callback-owner", R"LUA(
        assert(_G.cryptOwner and _G.cryptOwner.result == 'b3duZXI=', 'resource callback ran')
        assert(_G.cryptOwner.name == 'cryptowner', 'callback runs as its resource: ' .. tostring(_G.cryptOwner.name))
        assert(_G.cryptStoppedRan == nil, 'stopped resource callback dropped')
    )LUA");
    WorldRPG::Net::BitStreamWriter stopOwner;
    stopOwner.Write(static_cast<uint16_t>(90));
    WorldRPG::Script::ActiveVmOnResourceStop(stopOwner.GetData(), stopOwner.GetByteLength(), stopOwner.GetBitLength());
}

// createWeapon and the CClientWeapon element functions (CLuaWeaponDefs.cpp:150-695).
void CheckCustomWeapons(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG;
    RunLua(vm, "weapon-create-outside-resource", R"LUA(
        assert(createWeapon(31, 0, 0, 0) == false, 'createWeapon needs a resource')
        assert(fireWeapon({}) == false and setWeaponState() == false and getWeaponFlags({}, 'flags') == false, 'not a weapon')
    )LUA");
    Net::ResourceStartInfo info;
    info.name = "weapons";
    info.resourceId = 88;
    info.entityId = 0x3880;
    info.dynamicEntityId = 0x3881;
    info.noClientCacheScripts = 1;
    info.startCounter = 1;
    Script::ActiveVmOnResourceStart(info);
    const std::vector<uint8_t> body = NoCacheScriptBody(88, R"LUA(
        _G.cwBad = createWeapon(31) == false and createWeapon('nope', 1, 2, 3) == false
        _G.cw = createWeapon(31, 1, 2, 3)
        _G.cw2 = createWeapon('M4', 4, 5, 6)
        _G.cwRocket = createWeapon(35, 0, 0, 0)
    )LUA");
    CHECK(Script::ActiveVmFeedScripts(body.data(), body.size(), body.size() * 8));
    Script::ActiveVmPulseResources();
    RunLua(vm, "weapon-create", R"LUA(
        local w = _G.cw
        assert(_G.cwBad, 'bad createWeapon arguments')
        assert(isElement(w) and getElementType(w) == 'weapon' and isElement(_G.cw2), 'weapon elements')
        assert(getWeaponState(w) == 'ready' and getWeaponOwner(w) == false and getWeaponTarget(w) == nil, 'constructor state')
        assert(getWeaponAmmo(w) == 9999, 'total ammo 9999')
        assert(getWeaponClipAmmo(w) == getOriginalWeaponProperty(31, 'std', 'maximum_clip_ammo'), 'full STD clip')
        _G.cwRate = math.floor((getOriginalWeaponProperty(31, 'std', 'anim_loop_stop') -
            getOriginalWeaponProperty(31, 'std', 'anim_loop_start')) * 1000)
        assert(getWeaponFiringRate(w) == _G.cwRate, 'GetWeaponFireTime of the STD stat')
        local a, b, c, d, e, f, g, h = getWeaponFlags(w, 'flags')
        assert(a and b and c and d and e and f and not g and not h, 'constructor line-of-sight flags')
        for _, name in ipairs({'disable_model', 'instant_reload', 'shoot_if_out_of_range', 'shoot_if_blocked'}) do
            assert(getWeaponFlags(w, name) == false, name .. ' starts false')
        end
        assert(getWeaponFlags(w, 'bogus') == false and setWeaponFlags(w, 'bogus', true) == false, 'unknown flag')
        assert(setWeaponFlags(w, 'flags', false, false, true, true, true, true, true, false), 'set all flags')
        a, b, c, d, e, f, g, h = getWeaponFlags(w, 'flags')
        assert(not a and not b and c and d and e and f and g and not h, 'flags round trip')
        assert(setWeaponFlags(w, 'flags', true) == false, 'flags needs eight booleans')
        assert(setWeaponFlags(w, 'shoot_if_blocked', true) and getWeaponFlags(w, 'shoot_if_blocked'), 'single flag')

        assert(setWeaponState(w, 'firing') and getWeaponState(w) == 'firing', 'state')
        assert(setWeaponState(w, 'bad') == false and setWeaponState(w, 1) == false, 'state needs the enum string')
        assert(setWeaponState(w, 'ready'), 'back to ready')

        assert(setWeaponTarget(w, 10, 20, 30), 'vector target')
        local x, y, z = getWeaponTarget(w)
        assert(x == 10 and y == 20 and z == 30, 'vector target read back')
        assert(setWeaponTarget(w, nil) and getWeaponTarget(w) == nil, 'nil clears the target')
        assert(setWeaponTarget(w) == false and setWeaponTarget(w, 'x') == false, 'bad target argument')
        assert(setWeaponTarget(w, localPlayer) and getWeaponTarget(w).id == localPlayer.id, 'entity target')
        assert(setWeaponTarget(w, nil), 'clear again')

        assert(setWeaponFiringRate(w, 5) and getWeaponFiringRate(w) == 5, 'firing rate')
        assert(resetWeaponFiringRate(w) and getWeaponFiringRate(w) == _G.cwRate, 'reset firing rate')
        assert(setWeaponFiringRate(w, 'x') == false, 'firing rate needs a number')
        assert(setWeaponAmmo(w, 100) and getWeaponAmmo(w) == 100, 'ammo')
        assert(setWeaponClipAmmo(w, 3) and getWeaponClipAmmo(w) == 3, 'clip ammo')

        -- Fire (CClientWeapon.cpp:216-339): an instant-hit shot takes one round from the clip.
        assert(fireWeapon(w) and getWeaponClipAmmo(w) == 2, 'fixed target shot')
        assert(setWeaponTarget(w, 5000, 0, 0) and fireWeapon(w) and getWeaponClipAmmo(w) == 2, 'out of range: no shot')
        assert(setWeaponFlags(w, 'shoot_if_out_of_range', true) and fireWeapon(w) and getWeaponClipAmmo(w) == 1, 'shoot if out of range')
        local rocketClip = getWeaponClipAmmo(_G.cwRocket)
        assert(fireWeapon(_G.cwRocket) and getWeaponClipAmmo(_G.cwRocket) == rocketClip, 'rocket launcher is not fired by Fire')

        local damage = getOriginalWeaponProperty(31, 'std', 'damage')
        assert(getWeaponProperty(w, 'std', 'damage') == damage, 'per-weapon damage starts at STD')
        assert(setWeaponProperty(w, 'damage', 77) and getWeaponProperty(w, 'pro', 'damage') == 77, 'per-weapon damage')
        assert(getWeaponProperty(31, 'std', 'damage') == damage, 'type stats unchanged')
        assert(setWeaponProperty(w, 'fire_rotation', 0, 90, 0), 'fire rotation')
        local rx, ry, rz = getWeaponProperty(w, 'std', 'fire_rotation')
        assert(math.abs(rx) < 1e-4 and math.abs(ry - 90) < 1e-3 and math.abs(rz) < 1e-4, 'fire rotation in degrees')
        assert(setWeaponProperty(w, 'accuracy', 0.5) and math.abs(getWeaponProperty(w, 'std', 'accuracy') - 0.5) < 1e-6, 'accuracy')
        assert(setWeaponProperty(w, 'move_speed', 1) == false and getWeaponProperty(w, 'std', 'move_speed') == false, 'unsupported property')
        assert(setWeaponProperty(w, 'nonsense', 1) == false and getWeaponProperty(w, 9, 'damage') == false, 'bad property / skill')

        assert(setWeaponClipAmmo(w, 0) and setWeaponAmmo(w, 100) and setWeaponFlags(w, 'instant_reload', true), 'empty clip')
    )LUA");
    // CClientWeapon::DoPulse (CClientWeapon.cpp:151-176): instant reload refills from the total.
    Script::WeaponsPulse();
    RunLua(vm, "weapon-instant-reload", R"LUA(
        local clip = getOriginalWeaponProperty(31, 'std', 'maximum_clip_ammo')
        assert(getWeaponClipAmmo(_G.cw) == clip and getWeaponAmmo(_G.cw) == 100 - clip, 'instant reload')
        assert(setWeaponClipAmmo(_G.cw, 0) and setWeaponFlags(_G.cw, 'instant_reload', false), 'empty again')
    )LUA");
    Script::WeaponsPulse();
    RunLua(vm, "weapon-timed-reload", R"LUA(
        assert(getWeaponState(_G.cw) == 'reloading' and getWeaponClipAmmo(_G.cw) == 0, 'reload starts')
        assert(destroyElement(_G.cw) and not isElement(_G.cw), 'destroyElement releases the weapon')
        assert(getWeaponAmmo(_G.cw) == false, 'destroyed weapon')
    )LUA");

    // The timed parts of DoPulse and Fire, with a clock the test controls.
    Weapons::Stat stat;
    stat.known = true;
    stat.maximumClipAmmo = 30;
    stat.animLoopStart = 0.2f;
    stat.animLoopStop = 0.3f;
    stat.weaponRange = 100.f;
    stat.flags = Weapons::kFlagReload;
    Weapons::Weapon weapon;
    Weapons::Initialize(weapon, 1, 30, stat, Weapons::Vec3{}, 1000);
    Weapons::Hooks hooks;
    CHECK(weapon.fireRate == 100 && weapon.ammoInClip == 30);
    weapon.state = Weapons::State::Firing;
    CHECK(!Weapons::DoPulse(weapon, true, 0, 1050, hooks));  // fire timer not elapsed
    CHECK(Weapons::DoPulse(weapon, true, 0, 1100, hooks) && weapon.ammoInClip == 29);
    CHECK(!Weapons::DoPulse(weapon, false, 7, 1300, hooks));  // remote weapon not owned by the local player
    weapon.owner = 7;
    CHECK(Weapons::DoPulse(weapon, false, 7, 1300, hooks) && weapon.ammoInClip == 28);
    weapon.ammoInClip = 0;
    weapon.ammoTotal = 60;
    CHECK(!Weapons::DoPulse(weapon, true, 0, 2000, hooks) && weapon.state == Weapons::State::Reloading);
    CHECK(!Weapons::DoPulse(weapon, true, 0, 2999, hooks) && weapon.ammoInClip == 0);
    CHECK(Weapons::DoPulse(weapon, true, 0, 3000, hooks));  // reload done, back to firing, fires at once
    CHECK(weapon.state == Weapons::State::Firing && weapon.ammoInClip == 29 && weapon.ammoTotal == 30);
    weapon.ammoInClip = 0;
    weapon.ammoTotal = 10;  // less than a clip: no reload, no shot
    CHECK(!Weapons::DoPulse(weapon, true, 0, 5000, hooks) && weapon.state == Weapons::State::Firing);
    CHECK(Weapons::WeaponReloadTime(stat) == 1000);
    stat.flags |= Weapons::kFlagTwinPistol;
    CHECK(Weapons::WeaponReloadTime(stat) == 2000);

    // RESOURCE_STOP destroys the resource's weapons with its dynamic element.
    Net::BitStreamWriter stop;
    stop.Write(static_cast<uint16_t>(88));
    Script::ActiveVmOnResourceStop(stop.GetData(), stop.GetByteLength(), stop.GetBitLength());
    RunLua(vm, "weapon-resource-stop", "assert(not isElement(_G.cw2) and not isElement(_G.cwRocket), 'weapons left with the resource')");
}

void CheckElementsGuiDx(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG;
    Net::BitStreamWriter packet;
    packet.WriteCompressed(4u, 4);
    WriteWeaponHeader(packet, 2001, 2002); WriteWeaponBody(packet, 2);
    WriteWeaponHeader(packet, 2002); WriteWeaponBody(packet, 2);
    WriteWeaponHeader(packet, 2003, 2002); WriteWeaponBody(packet, 2);
    WriteWeaponHeader(packet, 2004, 9999); WriteWeaponBody(packet, 2);
    ServerWorld::OnEntityAdd(packet.GetData(), packet.GetByteLength(), packet.GetBitLength());
    RunLua(vm, "element-attachments", R"LUA(
        local a, b = {id=2001}, {id=2002}
        assert(attachElements(a,b) == false and detachElements(a,b) == false, 'missing physical hook must not report a successful mutation')
        assert(isElementAttached(a) == true and getElementAttachedTo(a).id == 2002, 'forward attachment reference')
        local list = getAttachedElements(b)
        assert(#list == 2 and list[1].id == 2001 and list[2].id == 2003, 'attachment order')
        local x,y,z,rx,ry,rz = getElementAttachedOffsets(a)
        assert(x == 1 and y == -2 and z == 3 and rx == 0 and ry == 90 and rz == 180, 'six offsets, degrees')
        assert(isElementAttached(b) == nil and getElementAttachedTo(b) == false, 'unattached return types')
        assert(getElementAttachedTo({id=2004}) == false, 'unknown target is not a live attachment')
        assert(isElementAttached({}) == nil and getElementAttachedTo({}) == nil, 'bad argument nil')
        assert(getAttachedElements({}) == false and getElementAttachedOffsets({}) == false, 'bad argument false')
        assert(select('#',getElementAttachedOffsets(b)) == 6, 'unattached element also has six offsets')
    )LUA");
    Net::BitStreamWriter removed;
    WriteTestBits(removed, 2002, 17);
    ServerWorld::OnEntityRemove(removed.GetData(), removed.GetByteLength(), removed.GetBitLength());
    Net::BitStreamWriter replacement;
    replacement.WriteCompressed(1u, 4); WriteWeaponHeader(replacement, 2002); WriteWeaponBody(replacement, 2);
    ServerWorld::OnEntityAdd(replacement.GetData(), replacement.GetByteLength(), replacement.GetBitLength());
    RunLua(vm, "element-attachment-removal", R"LUA(
        assert(isElementAttached({id=2001}) == nil and getElementAttachedTo({id=2003}) == false, 'deleted parent clears references')
        assert(#getAttachedElements({id=2002}) == 0, 'reused ID inherits no children')
    )LUA");
    Net::BitStreamWriter cleanup;
    for (uint32_t id : {2001u,2002u,2003u,2004u}) WriteTestBits(cleanup,id,17);
    ServerWorld::OnEntityRemove(cleanup.GetData(),cleanup.GetByteLength(),cleanup.GetBitLength());

    RunLua(vm, "gui-unicode-caret-setup", R"LUA(
        _G.editUtf = guiCreateEdit(100,100,120,20,'aé🙂z',false)
        assert(guiEditGetCaretIndex(editUtf) == 0, 'initial caret matches CEGUI')
        assert(guiEditGetMaxLength(editUtf) == 4294967295, '64-bit CEGUI default capacity')
        assert(guiEditSetCaretIndex(editUtf, 100) and guiEditGetCaretIndex(editUtf) == 4, 'caret clamps by characters')
        assert(guiEditSetCaretIndex(editUtf, 2), 'move caret inside text')
    )LUA");
    lua_State* L = Script::ActiveVmState();
    lua_getglobal(L,"editUtf"); lua_getfield(L,-1,"id");
    const auto edit = static_cast<uint32_t>(lua_tointeger(L,-1)); lua_pop(L,2);
    Script::GuiSetFocused(edit);
    CHECK(Script::GuiEditAppend("Ω",2));
    RunLua(vm,"gui-caret-insert",R"LUA(
        assert(guiGetText(editUtf) == 'aéΩ🙂z' and guiEditGetCaretIndex(editUtf) == 3, 'insert at Unicode caret')
    )LUA");
    CHECK(Script::GuiEditErase());
    RunLua(vm,"gui-caret-erase",R"LUA(
        assert(guiGetText(editUtf) == 'aé🙂z' and guiEditGetCaretIndex(editUtf) == 2, 'erase before caret')
        assert(guiEditSetReadOnly(editUtf,true))
    )LUA");
    CHECK(!Script::GuiEditErase() && !Script::GuiEditAppend("x",1));
    RunLua(vm,"gui-unicode-limits",R"LUA(
        assert(guiGetText(editUtf) == 'aé🙂z', 'readonly blocks insertion and backspace')
        assert(guiEditSetReadOnly(editUtf,false) and guiEditSetMaxLength(editUtf,2))
        assert(guiGetText(editUtf) == 'aé', 'length cap does not cut UTF-8 bytes')
        assert(guiSetText(editUtf,'é🙂x') and guiGetText(editUtf) == 'é🙂', 'setText respects Unicode length')
        assert(guiEditSetMaxLength(editUtf,0) and guiGetText(editUtf) == '', 'zero length means empty')
    )LUA");
    CHECK(!Script::GuiEditAppend("x",1));
    RunLua(vm,"gui-unsigned-length",R"LUA(
        assert(guiEditSetMaxLength(editUtf,-1) and guiEditGetMaxLength(editUtf) == 4294967295, 'signed Lua length converts to desktop unsigned length')
    )LUA");
    const std::string typed = Utf8::FromUtf16(u"é🙂");
    Script::ActiveVmQueueScriptInput(typed.c_str(),0,"");
    vm.pumpDx();
    RunLua(vm,"gui-android-unicode-input",R"LUA(
        assert(guiGetText(editUtf) == 'é🙂' and guiEditGetCaretIndex(editUtf) == 2, 'UTF-16 input becomes UTF-8 characters')
    )LUA");
    Script::ActiveVmQueueScriptInput("",1,"");
    vm.pumpDx();
    RunLua(vm,"gui-android-unicode-backspace",R"LUA(
        assert(guiGetText(editUtf) == 'é' and guiEditGetCaretIndex(editUtf) == 1, 'one backspace erases the whole emoji')
    )LUA");
    RunLua(vm,"dx-gui-layers-setup",R"LUA(
        assert(guiEditSetMaxLength(editUtf,10) and guiSetText(editUtf,'é🙂x') and guiEditSetCaretIndex(editUtf,1))
        _G.layerLabel = guiCreateLabel(500,500,150,30,'camada-é🙂',false)
        _G.layerTarget = dxCreateRenderTarget(16,16,true)
        _G.layerTarget2 = dxCreateRenderTarget(16,16,true)
        assert(getElementAttachedTo(layerTarget) == false and #getAttachedElements(layerTarget) == 0, 'DX elements are valid attachment-query arguments')
        assert(select('#',getElementAttachedOffsets(layerTarget)) == 6, 'unattached DX element has offsets')
        _G.layerDraw = function()
            assert(dxDrawRectangle(301,302,3,4,4294967295,false) == true, 'rectangle success returns boolean')
            assert(dxDrawText('before-gui',310,311) == true, 'text success returns boolean')
            dxDrawLine(321,322,323,324,4294967295,1,false)
            dxSetBlendMode('add')
            dxDrawLine(421,422,423,424,4294967295,1,true)
            dxDrawCircle(450,450,10,0,90,4294967295,4294967295,3,1,true)
            dxDrawPrimitive('linelist',true,{550,550,4294967295},{560,560,4278190080})
            dxSetRenderTarget(layerTarget,true)
            dxDrawLine(1,2,3,4,4294967295,1,true)
            dxSetRenderTarget()
            dxDrawMaterialPrimitive('trianglelist',layerTarget,true,{650,650,0,0},{660,650,1,0},{650,660,0,1})
            assert(dxDrawRectangle(701,702,3,4,4294967295,true))
            assert(dxDrawText('after-gui-é🙂',710,711,720,730,4294967295,1,'default','left','top',false,false,true))
            assert(dxDrawImage(740,741,10,10,layerTarget,0,0,0,4294967295,true), 'live texture works outside resource scope')
            assert(dxDrawImageSection(750,751,10,10,0,0,4,4,layerTarget,0,0,0,4294967295,true))
            dxSetRenderTarget(layerTarget2,true)
            assert(dxDrawRectangle(801,802,3,4,4294967295,true))
            assert(dxDrawText('target-only',810,811,820,830,4294967295,1,'default','left','top',false,false,true))
            assert(dxDrawImage(840,841,10,10,layerTarget,0,0,0,4294967295,true))
            assert(dxDrawImageSection(850,851,10,10,0,0,4,4,layerTarget,0,0,0,4294967295,true))
            dxSetRenderTarget()
            dxSetBlendMode('overwrite')
        end
        assert(addEventHandler('onClientRender',root,layerDraw))
        assert(dxDrawLine(math.huge,0,1,1) == false and dxDrawCircle(0,0,math.huge) == false, 'infinite geometry rejected')
        assert(dxDrawCircle(0,0,3e38,0,90,4294967295,4294967295,3,100) == false, 'circle overflow rejected')
        assert(not pcall(dxDrawPrimitive,'linelist',false,{math.huge,0},{1,1}), 'infinite vertex rejected')
        assert(not pcall(dxDrawMaterialPrimitive,'linelist',layerTarget,false,{math.huge,0,0,0},{1,1,1,1}), 'infinite material vertex rejected')
        assert(not pcall(dxDrawPrimitive,'linelist',false,{0,0},{1}), 'malformed vertex after an allocated vertex is an error')
        assert(not pcall(dxDrawMaterialPrimitive,'trianglelist',layerTarget,false,{0,0,0,0},{1}), 'malformed material vertex after a valid vertex is an error')
        assert(dxDrawPrimitive('linelist',false,{0,0,'ignored'},{1,1,'2'}) == true, 'ReadNumberTable ignores strings as desktop does')
        assert(not pcall(dxDrawPrimitive,'linelist',false,{'0','0'},{1,1}), 'numeric strings are not vertex numbers')
        assert(dxDrawRectangle('bad',0,1,1) == false and dxDrawRectangle(math.huge,0,1,1) == false, 'bad rectangle returns false')
        assert(dxDrawText({},0,0) == false and dxDrawText('bad',math.huge,0) == false, 'bad text returns false')
        assert(dxDrawText('bad',0,0,0,0,4294967295,1,'default','left','top',false,false,'yes') == false, 'postGUI must be boolean')
        assert(dxDrawImage(math.huge,0,1,1,layerTarget) == false, 'bad image coordinates')
        assert(dxDrawImageSection(0,0,1,1,0,0,1,1,layerTarget,0,0,0,4294967295,'yes') == false, 'bad section postGUI')
    )LUA");
    const std::string frame = vm.pumpDx();
    const size_t pre = frame.find("L 321.0 322.0"), gui = frame.find("camada-é🙂"), post = frame.find("L 421.0 422.0");
    CHECK(pre != std::string::npos && gui != std::string::npos && post != std::string::npos && pre < gui && gui < post);
    for (const char* command : {"R 701.0 702.0", "after-gui-é🙂", "I 740.0 741.0", "I 750.0 751.0"}) {
        const size_t draw = frame.find(command);
        CHECK(draw != std::string::npos && draw > gui);
    }
    for (const char* command : {"R 801.0 802.0", "target-only", "I 840.0 841.0", "I 850.0 851.0", "before-gui"}) {
        const size_t draw = frame.find(command);
        CHECK(draw != std::string::npos && draw < gui);
    }
    CHECK(frame.find("B 1\nL 421.0 422.0") != std::string::npos);
    const size_t overwrite = frame.find("B 3\n");
    const size_t guiBlend = frame.find("B 0\n", overwrite);
    CHECK(overwrite != std::string::npos && guiBlend != std::string::npos && guiBlend < gui);
    CHECK(frame.find("L 1.0 2.0 3.0 4.0") < gui); // RT draws cannot escape to the screen's post-GUI queue.
    const size_t material = frame.find("M 4 3 650.0 650.0");
    CHECK(material != std::string::npos && material > gui);
    CHECK(frame.find("é|🙂x") != std::string::npos);
    CHECK(Utf8::ToUtf16("é🙂") == u"é🙂");
    CHECK(Utf8::FromUtf16(Utf8::ToUtf16("é🙂")) == "é🙂");
    CHECK(Utf8::FromUtf16(std::u16string{0xD800,u'x',0xDC00}) == "\xEF\xBF\xBDx\xEF\xBF\xBD");
    CHECK(Utf8::ToUtf16(std::string("\xC0\xAF",2)) == u"\uFFFD\uFFFD");
    CHECK(Utf8::ToUtf16(std::string("\xED\xA0\x80",3)) == u"\uFFFD\uFFFD\uFFFD");
    RunLua(vm,"dx-gui-layers-cleanup",R"LUA(
        assert(removeEventHandler('onClientRender',root,layerDraw))
        dxSetBlendMode('blend')
        assert(destroyElement(editUtf) and destroyElement(layerLabel) and destroyElement(layerTarget) and destroyElement(layerTarget2))
    )LUA");
}

void CheckDxTextLayout(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm,"dx-text-layout-setup",R"LUA(
        _G.textLayoutLabel = guiCreateLabel(1000,900,100,20,'layout-gui',false)
        _G.textLayoutDraw = function()
            assert(dxDrawText('minimal',10.25,20.75))
            assert(dxDrawText('a\nb\\n🙂',1.25,2.5,101.25,42.5,4278190081,2,0.5,'default','right','bottom',true,true,true,false,true,30,10,20))
            assert(dxDrawText('#FF0000colored',0,0,100,100,-1,1,'default','center','center',true,true,false,true,false,0,50,50,21))
        end
        assert(addEventHandler('onClientRender',root,textLayoutDraw))
        assert(dxDrawText('bad',0,0,0,0,-1,1,'default','middle') == false, 'horizontal alignment enum')
        assert(dxDrawText('bad',0,0,0,0,-1,1,'default','left','sideways') == false, 'vertical alignment enum')
        assert(dxDrawText('bad',0,0,0,0,-1,1,'default','left','top','yes') == false, 'clip is boolean')
        assert(dxDrawText('bad',0,0,0,0,-1,1,math.huge) == false, 'scale Y must be finite')
        assert(dxDrawText('fallback-font',0,0,0,0,-1,1,'123') == true, 'numeric font string falls back to default instead of changing scale Y')
        assert(dxDrawText('bad',0,0,0,0,-1,1,localPlayer) == false, 'font argument must be a live DX font')
        assert(dxDrawText('bad',0,0,0,0,-1,1,'default','left','top',false,false,false,false,false,math.huge) == false, 'rotation must be finite')
    )LUA");
    const std::string frame = vm.pumpDx();
    CHECK(frame.find("X 10.250 20.750 10.250 20.750 1.0000 1.0000 4294967295 0 0 0 0 0 0 0.000 10.250 20.750 0.000 minimal\n") != std::string::npos);
    const size_t draw = frame.find("X 1.250 2.500 101.250 42.500 2.0000 0.5000 4278190081 0 2 2 1 1 1 30.000 10.000 20.000 0.000 a\\nb\\\\n🙂\n");
    const size_t gui = frame.find("layout-gui");
    CHECK(draw != std::string::npos && gui != std::string::npos && gui < draw);
    CHECK(frame.find("\na\nb") == std::string::npos); // Text cannot inject new frame commands.
    CHECK(frame.find("X 0.000 0.000 100.000 100.000 1.0000 1.0000 4294967295 1 1 1 1 1 0 0.000 50.000 50.000 21.000 #FF0000colored\n") != std::string::npos);
    RunLua(vm,"dx-text-layout-cleanup",R"LUA(
        assert(removeEventHandler('onClientRender',root,textLayoutDraw))
        assert(destroyElement(textLayoutLabel))
    )LUA");
}

void CheckUiViewport(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG::Script;
    int oldWidth=0,oldHeight=0;
    ActiveVmScreenSize(oldWidth,oldHeight);
    vm.setScreenSize(832,384);
    RunLua(vm,"ui-dpi-setup",R"LUA(
        local w,h=guiGetScreenSize()
        assert(w==832 and h==384, 'scripts use the logical viewport')
        _G.dpiEdit=guiCreateEdit(100,100,200,40,'dpi',false)
        _G.dpiClicked=false
        _G.dpiClick=function(button,state,x,y)
            if state=='down' then assert(x==150 and y==120); dpiClicked=true end
        end
        assert(addEventHandler('onClientClick',root,dpiClick))
        showCursor(true)
    )LUA");
    ActiveVmQueueCursor(0,150,120);
    std::string frame=vm.pumpDx();
    CHECK(frame.rfind("C\nV 832 384\n",0)==0);
    CHECK(frame.find("K\n")!=std::string::npos);
    RunLua(vm,"ui-dpi-click",R"LUA(
        assert(dpiClicked, 'logical coordinates reach onClientClick')
        local x,y=getCursorPosition()
        assert(math.abs(x-150/832)<0.00001 and math.abs(y-120/384)<0.00001,
            'normalized cursor and CGUI edit hit test use the same coordinates')
    )LUA");
    vm.setScreenSize(1664,768);
    CHECK(vm.pumpDx().find("V 1664 768\n")!=std::string::npos);
    RunLua(vm,"ui-dpi-cleanup",R"LUA(
        assert(removeEventHandler('onClientClick',root,dpiClick))
        assert(destroyElement(dpiEdit))
        showCursor(false)
        guiSetInputEnabled(false)
    )LUA");
    vm.setScreenSize(oldWidth,oldHeight);
}

void CheckMemoInput(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG::Script;
    RunLua(vm,"memo-input-setup",R"LUA(
        _G.memoInput = guiCreateMemo(1100,100,160,80,'é🙂',false)
        _G.memoChanged, _G.memoAccepted, _G.memoFocused, _G.memoChars = 0,0,0,0
        assert(guiGetText(memoInput) == 'é🙂\n' and guiMemoGetCaretIndex(memoInput) == 0, 'CEGUI trailing newline and initial caret')
        assert(guiMemoSetCaretIndex(memoInput,-1) and guiMemoGetCaretIndex(memoInput) == 2, 'unsigned index clamps before final newline')
        assert(addEventHandler('onClientGUIChanged',memoInput,function() memoChanged = memoChanged + 1 end))
        assert(addEventHandler('onClientGUIAccepted',memoInput,function() memoAccepted = memoAccepted + 1 end))
        assert(addEventHandler('onClientGUIFocus',memoInput,function() memoFocused = memoFocused + 1 end))
        _G.memoCharHandler = function() memoChars = memoChars + 1 end
        assert(addEventHandler('onClientCharacter',root,memoCharHandler))
        assert(guiFocus(memoInput) and memoFocused == 1, 'memo focus event')
    )LUA");
    std::string frame = vm.pumpDx();
    CHECK(frame.find("K multiline\n") != std::string::npos);
    ActiveVmQueueScriptInput("Ω",0,"");
    vm.pumpDx();
    ActiveVmQueueScriptInput("",0,"enter");
    vm.pumpDx();
    ActiveVmQueueScriptInput("x\r\ny",0,"");
    frame = vm.pumpDx();
    CHECK(frame.find("T 1100.0 114.4") != std::string::npos && frame.find("T 1100.0 128.8") != std::string::npos);
    RunLua(vm,"memo-insert-lines",R"LUA(
        assert(guiGetText(memoInput) == 'é🙂Ω\nx\ny\n' and guiMemoGetCaretIndex(memoInput) == 7, 'memo preserves Unicode and CRLF becomes one newline')
        assert(memoChanged == 3 and memoAccepted == 0 and memoChars == 0, 'memo consumes text and Enter changes text without acceptance')
        assert(guiMemoSetReadOnly(memoInput,true))
    )LUA");
    ActiveVmQueueScriptInput("Z",1,"enter");
    vm.pumpDx();
    RunLua(vm,"memo-readonly",R"LUA(
        assert(guiGetText(memoInput) == 'é🙂Ω\nx\ny\n' and memoChanged == 3, 'readonly rejects insertion, backspace and Enter')
        assert(guiMemoSetReadOnly(memoInput,false) and guiMemoSetCaretIndex(memoInput,6))
    )LUA");
    ActiveVmQueueScriptInput("",1,"");
    vm.pumpDx();
    RunLua(vm,"memo-erase-line",R"LUA(
        assert(guiGetText(memoInput) == 'é🙂Ω\nxy\n' and guiMemoGetCaretIndex(memoInput) == 5, 'backspace joins lines at the caret')
        assert(guiSetText(memoInput,'') and guiGetText(memoInput) == '\n' and guiMemoGetCaretIndex(memoInput) == 0, 'empty memo keeps terminal newline')
        assert(guiSetVisible(memoInput,false))
    )LUA");
    frame = vm.pumpDx();
    CHECK(frame.find("K multiline\n") == std::string::npos);
    RunLua(vm,"memo-input-cleanup",R"LUA(
        assert(removeEventHandler('onClientCharacter',root,memoCharHandler))
        assert(destroyElement(memoInput))
    )LUA");
}

void CheckAudioQueries(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "audio-queries", R"LUA(
        local stations = {'Radio off', 'Playback FM', 'K-Rose', 'K-DST', 'Bounce FM', 'SF-UR',
            'Radio Los Santos', 'Radio X', 'CSR 103.9', 'K-Jah West', 'Master Sounds 98.3', 'WCTR', 'User Track Player'}
        for i, name in ipairs(stations) do assert(getRadioChannelName(i - 1) == name, 'station ' .. i) end
        assert(getRadioChannelName('2') == 'K-Rose' and getRadioChannelName(12.9) == 'User Track Player', 'number conversion')
        assert(getRadioChannelName(-0.5) == 'Radio off', 'integer truncation before range check')
        assert(getRadioChannelName() == false and getRadioChannelName(13) == false and getRadioChannelName(-1) == false, 'station bounds')
        assert(getRadioChannelName(0/0) == false and getRadioChannelName(math.huge) == false, 'nonfinite')
        assert(select('#', getSoundLength()) == 0 and getSoundLength({}) == nil, 'bad sound has no results, as on desktop')
        _G.querySound = playSound('http://example/audio-test.ogg')
        _G.otherQuerySound = playSound('http://example/other-test.ogg')
        assert(isElement(querySound) and isElement(otherQuerySound), 'independent sound handles')
        assert(getSoundLength(querySound) == 0 and getSoundLength(otherQuerySound) == 0, 'duration initially unknown')
        assert(setSoundPan(querySound, -1) and getSoundPan(querySound) == -1, 'left pan')
        assert(setSoundVolume(querySound, 0.5) and getSoundPan(querySound) == -1, 'volume preserves pan')
        assert(setSoundPan(querySound, 1) and getSoundPan(querySound) == 1, 'right pan')
        assert(setSoundPan(querySound, 2) == false and setSoundPan(querySound, 0/0) == false, 'bad pan')
        assert(setSoundVolume(querySound, 0/0) == false, 'bad volume')
    )LUA");
    lua_State* L = WorldRPG::Script::ActiveVmState();
    lua_getglobal(L, "querySound"); lua_getfield(L, -1, "id");
    const uint32_t id = static_cast<uint32_t>(lua_tointeger(L, -1));
    lua_pop(L, 2);
    WorldRPG::Script::ActiveVmNoteSoundLength(id, 123.456);
    WorldRPG::Script::ActiveVmNoteSoundLength(id, -1); // invalid callbacks cannot overwrite metadata.
    RunLua(vm, "audio-prepared", R"LUA(
        assert(math.abs(getSoundLength(querySound) - 123.456) < 0.00001, 'prepared duration in seconds')
        assert(getSoundLength(otherQuerySound) == 0, 'metadata belongs to one sound')
        assert(stopSound(querySound) and isElement(otherQuerySound), 'stopping one leaves the other')
        _G.reusedSound = playSound('http://example/reused.ogg')
        assert(getSoundLength(reusedSound) == 0 and getSoundPan(reusedSound) == 0, 'reused slot has fresh defaults')
        -- Lot 1: 3D Spatial Audio & Core Streams
        local s3d = playSound3D('http://example/3d.ogg', 100, 200, 300, false)
        assert(isElement(s3d) and getElementType(s3d) == 'sound', 'playSound3D creates sound element')
        assert(isSoundPanningEnabled(s3d) == true, 'default panning enabled')
        assert(setSoundPanningEnabled(s3d, false) == true and isSoundPanningEnabled(s3d) == false, 'disable pan')
        assert(setSoundPanningEnabled(s3d, true) == true and isSoundPanningEnabled(s3d) == true, 'enable pan')
        local rate, tempo, pitch, rev = getSoundProperties(s3d)
        assert(rate == 44100 and tempo == 0 and pitch == 0 and rev == false, 'default properties')
        assert(setSoundProperties(s3d, 48000, 10, 5, true) == true, 'set properties')
        rate, tempo, pitch, rev = getSoundProperties(s3d)
        assert(rate == 48000 and tempo == 10 and pitch == 5 and rev == true, 'read updated properties')
        assert(getSoundBufferLength(s3d) == 0, 'buffer length default')
        assert(getSoundBPM(s3d) == false, 'bpm unavailable default')
        local left, right = getSoundLevelData(s3d)
        assert(type(left) == 'number' and type(right) == 'number', 'sound level data')
        assert(stopSound(s3d) == true, 'stop 3d sound')

        -- Lot 2: Sound Analysis, DSP Effects & Debug Visualizer
        local sfxTest = playSound('http://example/sfx.ogg')
        local fft = getSoundFFTData(sfxTest, 128, 16)
        assert(type(fft) == 'table', 'fft data table')
        local wave = getSoundWaveData(sfxTest, 64)
        assert(type(wave) == 'table', 'wave data table')
        local meta = getSoundMetaTags(sfxTest)
        assert(type(meta) == 'table', 'meta tags table')
        local fxTable = getSoundEffects(sfxTest)
        assert(type(fxTable) == 'table' and fxTable.echo == false, 'fx effects table')
        assert(setSoundEffectEnabled(sfxTest, 'echo', true) == true, 'enable echo fx')
        fxTable = getSoundEffects(sfxTest)
        assert(fxTable.echo == true, 'echo enabled')
        assert(setSoundEffectParameter(sfxTest, 'echo', 'feedback', 30) == true, 'set fx param')
        local fxParams = getSoundEffectParameters(sfxTest, 'echo')
        assert(type(fxParams) == 'table' and fxParams.feedback == 30, 'read fx param')
        assert(showSound(true) == true and isShowSoundEnabled() == true, 'enable showSound')
        assert(showSound(false) == true and isShowSoundEnabled() == false, 'disable showSound')
        assert(stopSound(sfxTest) == true, 'stop sfxTest sound')

        -- Lot 3: World & Ambient Audio, SFX & Radio Channels
        assert(getRadioChannel() == 0, 'radio channel default')
        assert(setRadioChannel(5) == true and getRadioChannel() == 5, 'set radio channel')
        assert(setRadioChannel(20) == false, 'radio channel out of range')
        assert(isAmbientSoundEnabled('general') == true, 'ambient general enabled')
        assert(setAmbientSoundEnabled('general', false) == true and isAmbientSoundEnabled('general') == false, 'disable ambient')
        assert(resetAmbientSounds() == true and isAmbientSoundEnabled('general') == true, 'reset ambient sounds')
        assert(isWorldSoundEnabled(5, 2) == true, 'world sound enabled default')
        assert(setWorldSoundEnabled(5, 2, false) == true and isWorldSoundEnabled(5, 2) == false, 'disable world sound')
        assert(resetWorldSounds() == true and isWorldSoundEnabled(5, 2) == true, 'reset world sounds')
        assert(getSFXStatus('genrl') == false and getSFXStatus('invalid_container') == nil, 'SFX status')
        assert(playSFX('genrl', 1, 2) == false and playSFX3D('genrl', 1, 2, 10, 20, 30) == false,
            'SFX needs a GTA audio backend')
        assert(playMissionPassedTune(1) == false and requestMissionAudio(2001) == false, 'mission audio unavailable')
        assert(isMissionAudioLoaded(1) == false and isMissionAudioFinished(1) == false)
        assert(playMissionAudio(1) == false and releaseMissionAudio(1) == false)
        assert(reportPositionMissionAudioEvent(100, 200, 300, 1050) == false)
        assert(reportVehicleMissionAudioEvent(localPlayer, 1050) == false)
    )LUA");
    WorldRPG::Script::ActiveVmNoteSoundLength(id, 999);
    RunLua(vm, "audio-late-metadata", R"LUA(
        assert(getSoundLength(reusedSound) == 0, 'stale callback cannot modify new sound')
        _G.audioFinished = 0
        assert(addEventHandler('onClientSoundStopped', reusedSound, function(reason)
            assert(reason == 'finished' and source.id == reusedSound.id)
            _G.audioFinished = audioFinished + 1
        end))
    )LUA");
    lua_getglobal(L, "reusedSound");
    lua_getfield(L, -1, "id");
    const uint32_t reusedId = static_cast<uint32_t>(lua_tointeger(L, -1));
    lua_pop(L, 2);
    WorldRPG::Script::ActiveVmNoteSoundEnded(id);
    WorldRPG::Script::ActiveVmNoteSoundEnded(reusedId);
    WorldRPG::Script::ActiveVmNoteSoundEnded(reusedId);
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "audio-ended", R"LUA(
        assert(audioFinished == 1 and not isElement(reusedSound), 'completion fires once and releases the sound')
        assert(destroyElement(otherQuerySound) and not isElement(otherQuerySound), 'destroy releases a live sound')
    )LUA");
}

// Stock model lookup and command-key queries need no GTA native or MTA server.
// CLuaCompatibilityDefs.cpp:13-78. Old names are the same C functions as the modern names.
void CheckCompatibilityAliases(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "compatibility-aliases", R"LUA(
        local me = {id = 777, element = true}
        local function same(old, modern, ...)
            local a, b = {old(...)}, {modern(...)}
            assert(#a == #b, 'result count')
            for i = 1, #a do
                assert(a[i] == b[i], 'result ' .. i .. ' ' .. tostring(a[i]) .. ' ~= ' .. tostring(b[i]))
            end
        end
        same(getPlayerRotation, getPedRotation, me)
        same(getPlayerArmor, getPedArmor, me)
        same(isPlayerDead, isPedDead, me)
        same(getPlayerWeapon, getPedWeapon, me)
        same(getPlayerWeaponSlot, getPedWeaponSlot, me)
        same(getPlayerTotalAmmo, getPedTotalAmmo, me)
        same(getPlayerAmmoInClip, getPedAmmoInClip, me)
        same(getPlayerOccupiedVehicle, getPedOccupiedVehicle, me)
        same(isPlayerInVehicle, isPedInVehicle, me)
        same(isPlayerInVehicle, isPedInVehicle)
        same(getPlayerSkin, getElementModel, me)
        same(getPedSkin, getElementModel, me)
        same(getVehicleModel, getElementModel, me)
        same(getVehicleID, getElementModel, me)
        same(getObjectRotation, getElementRotation, me)
        same(getVehicleRotation, getElementRotation, me)
        same(setObjectRotation, setElementRotation, me, 0, 0, 90)
        same(setVehicleRotation, setElementRotation, me, 0, 0, 90)
        same(getVehicleIDFromName, getVehicleModelFromName, 'Infernus')
        same(getVehicleNameFromID, getVehicleNameFromModel, 411)
        same(getVehicleNameFromID, getVehicleNameFromModel, -1)
        assert(setCameraDrunkLevel(12) == true and getCameraDrunkLevel() == 12, 'drunk level')
        assert(setCameraShakeLevel(40) == true and getCameraShakeLevel() == 40 and getCameraDrunkLevel() == 40, 'shake writes drunk level')
        assert(setCameraShakeLevel(300) == false and getCameraShakeLevel() == 40, 'shake range')
        assert(setCameraDrunkLevel(0) == true and getCameraShakeLevel() == 0, 'shake restore')
        local xml = xmlLoadString('<meta><info name="x">hi</info></meta>')
        local found = xmlFindChild(xml, 'info', 0)
        assert(found ~= false and xmlNodeFindChild(xml, 'info', 0) == found, 'xmlNodeFindChild')
        assert(xmlNodeFindChild(xml, 'missing', 0) == false, 'missing child')
        assert(xmlUnloadFile(xml) == true, 'xml unload')
        local edit = guiCreateEdit(0, 0, 80, 20, 'ab', false)
        assert(guiEditSetCaratIndex(edit, 1) == true and guiEditGetCaretIndex(edit) == 1, 'edit carat')
        assert(destroyElement(edit) == true, 'edit destroy')
        local memo = guiCreateMemo(0, 0, 80, 40, 'ab', false)
        assert(guiMemoSetCaratIndex(memo, 1) == true and guiMemoGetCaretIndex(memo) == 1, 'memo carat')
        assert(destroyElement(memo) == true, 'memo destroy')
        same(canPlayerBeKnockedOffBike, canPedBeKnockedOffBike, me)
        same(getPlayerSimplestTask, getPedSimplestTask, me)
        same(isPlayerDoingTask, isPedDoingTask, me, 'TASK_SIMPLE_PLAYER_ON_FOOT')
        same(setPlayerCanBeKnockedOffBike, setPedCanBeKnockedOffBike, me, false)
        same(attachElementToElement, attachElements, me, me)
        same(detachElementFromElement, detachElements, me)
        same(getPlayerStat, getPedStat, me, 24)
        same(getPlayerTask, getPedTask, me, 'primary', 0)
        same(getPlayerTarget, getPedTarget, me)
        same(getControlState, getPedControlState, me, 'forwards')
        same(isPlayerOnGround, isPedOnGround, me)
        same(setControlState, setPedControlState, me, 'forwards', true)
        same(setPlayerRotation, setPedRotation, me, 90)
        same(setPlayerWeaponSlot, setPedWeaponSlot, me, 0)
        assert(setPedOnFire(me, true) == true and isPedOnFire(me) == true, 'local ped can be set on fire')
        assert(setPedOnFire(me, false) == true and isPedOnFire(me) == false, 'fire clears')
        assert(doesPedHaveJetPack(me) == false and doesPlayerHaveJetPack(me) == false, 'no jetpack stored')
        assert(isPedDucked(me) == false and isPlayerDucked(me) == false, 'not ducked')
        assert(isPedChoking(me) == false and isPlayerChoking(me) == false, 'not choking')
        assert(getPedContactElement(me) == false and getPlayerContactElement(me) == false, 'no contact')
        assert(getPedWeaponMuzzlePosition(me) == false, 'no weapon muzzle')
        local sx, sy, sz = getPedTargetStart(me)
        assert(sx == 0 and sy == 0 and sz == 0, 'no aim origin')
        assert(getPlayerTargetStart(me) == sx, 'alias start')
        local ex, ey, ez = getPedTargetEnd(me)
        assert(ex == 0 and ey == 0 and ez == 0, 'range not applied')
        assert(getPedTargetCollision(me) == false and getPlayerTargetCollision(me) == false, 'no line of sight')
        assert(isElementInWater(me) == false and isPedInWater(me) == false and isPlayerInWater(me) == false, 'not in water')
        assert(setPlayerSkin(me, 7) == true and getPlayerSkin(me) == 7, 'skin stored')
        assert(setPlayerSkin(me, 7) == false, 'same skin is false')
        assert(setPlayerSkin(me, 74) == false and getPlayerSkin(me) == 7, 'invalid ped model')
        assert(setVehicleModel(me) == false, 'player is not a vehicle')
        local settings = getBrowserSettings()
        assert(settings.RemoteEnabled == false and settings.RemoteJavascript == false and settings.PluginsEnabled == false, 'no web core')
        assert(createBrowser(1, 1, true) == false and isBrowserGPUEnabled() == false, 'create needs a web core')
        local ok, err = pcall(createBrowser, 0, 1, true)
        assert(not ok and err:find('at least 1x1'), 'browser size: ' .. tostring(err))
        ok, err = pcall(guiCreateBrowser, 0, 0, 8, 8, true, false, false)
        assert(not ok and err:find('Failed to create browser'), 'local gui browser: ' .. tostring(err))
        assert(guiCreateBrowser(0, 0, 8, 8, false, false, false) == false, 'remote gui browser')
        assert(isBrowserDomainBlocked('example.com') == nil, 'domain check needs web core')
        assert(setDiscordApplicationID('1234') == false and isDiscordRichPresenceConnected() == false, 'discord off')
        assert(getDiscordRichPresenceUserID() == '', 'no discord user')
        ok, err = pcall(setDiscordRichPresenceState, string.rep('a', 129))
        assert(not ok and err:find('less than/equal to 128'), 'state length: ' .. tostring(err))
        ok, err = pcall(setDiscordRichPresencePartySize, 3, 1)
        assert(not ok and err:find('Party size'), 'party size: ' .. tostring(err))
        assert(setDiscordRichPresenceButton(3, 'Go', 'https://example.com') == false, 'button index')
        assert(setPedWeaponSlot(me, 3) == true and getPedWeaponSlot(me) == 3, 'slot stored')
        assert(setPedWeaponSlot(me, -1) == false and setPedWeaponSlot(me, 13) == false, 'slot range')
        assert(setPedCanBeKnockedOffBike(me, true) == true and canPedBeKnockedOffBike(me) == true, 'knock flag true')
        assert(setPedCanBeKnockedOffBike(me, false) == true and canPlayerBeKnockedOffBike(me) == false, 'knock flag stored')
        assert(setControlState(me, 'jump', true) == true and getControlState(me, 'jump') == true, 'control stored')
        assert(setControlState(me, 'not_a_control', true) == false, 'unknown control')
        assert(getPedStat(me, 24) == 0 and getPedStat(me, 400) == false, 'stat id range')
        assert(getPedTask(me, 'primary', 0) == false and getPedSimplestTask(me) == false, 'no task manager')
        assert(isPedDoingTask(me, 'TASK_SIMPLE_PLAYER_ON_FOOT') == false and getPedTarget(me) == false, 'no task or target')
        assert(setPedRotation(me, 90) == false, 'local rotation is not hooked')
        assert(attachElements(me, me) == false and detachElements(me) == false, 'no physical attach')
    )LUA");
}

// CLuaPedDefs body flags, walking style, clothes, mission policy and weapon accuracy.
void CheckPedBodyState(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "ped-body-state", R"LUA(
        local me = {id = 777, element = true}
        assert(isPedHeadless(me) == false and setPedHeadless(me, true) == true and isPedHeadless(me) == true, 'headless stored')
        assert(setPedHeadless(me, 1) == false and isPedHeadless(me) == true, 'headless wants a bool')
        assert(isPedFrozen(me) == false and setPedFrozen(me, true) == true and isPedFrozen(me) == true, 'frozen stored')
        assert(setPedFrozen(me, false) == true and isPedFrozen(me) == false, 'frozen clears')
        assert(setPedFootBloodEnabled(me, true) == true and isPedFootBloodEnabled(me) == false, 'foot blood is not stored without a body')
        assert(setPedOxygenLevel(me, 50) == true and getPedOxygenLevel(me) == false, 'oxygen is not stored without a body')
        assert(setPedOxygenLevel(me, -1) == true and setPedOxygenLevel(me, 0/0) == false and setPedOxygenLevel(me) == false, 'oxygen accepts any finite number')
        assert(isPedBleeding(me) == false and setPedBleeding(me, true) == true and isPedBleeding(me) == true, 'bleeding stored')
        local ok, err = pcall(setPedBleeding, me, 1)
        assert(not ok and err:find('boolean'), 'bleeding type: ' .. tostring(err))
        assert(setPedArmor(me, 40) == true and getPedArmor(me) == 40 and getPlayerArmor(me) == 40, 'armor stored')
        ok, err = pcall(setPedArmor, me, -1)
        assert(not ok and err:find('greater than or equal to 0'), 'armor floor: ' .. tostring(err))
        ok, err = pcall(setPedArmor, me, 101)
        assert(not ok and err:find('less than or equal to 100'), 'armor ceiling: ' .. tostring(err))
        assert(getPedArmor(me) == 40, 'rejected armor is unchanged')
        assert(getPedFightingStyle(me) == 15, 'default grab kick')
        assert(setPedFightingStyle(me, 4) == false and getPedFightingStyle(me) == 15, 'player is not a local ped')
        assert(getPedWalkingStyle(me) == 0, 'default walk')
        assert(setPedWalkingStyle(me, 118) == true and getPedWalkingStyle(me) == 118, 'man walk')
        assert(setPedWalkingStyle(me, 1) == false and getPedWalkingStyle(me) == 118, 'invalid walk')
        assert(isPedUsingNativeWalkingStyle(me) == false, 'not native')
        assert(setPedUseNativeWalkingStyle(me, true) == true and isPedUsingNativeWalkingStyle(me) == true and getPedWalkingStyle(me) == 0, 'native resets style')
        assert(setPedWalkingStyle(me, 121) == true and isPedUsingNativeWalkingStyle(me) == false and getPedWalkingStyle(me) == 121, 'explicit style clears native')
        assert(isPedDoingGangDriveby(me) == false and setPedDoingGangDriveby(me, true) == true and isPedDoingGangDriveby(me) == true, 'gang flag')
        assert(getPedCameraRotation(me) == 0, 'camera starts at 0')
        assert(setPedCameraRotation(me, 90) == true and math.abs(getPedCameraRotation(me) - 90) < 0.02, 'camera degrees')
        assert(isPedWearingJetpack(me) == doesPedHaveJetPack(me), 'jetpack alias')
        assert(setPedMissionActor(me, true) == false and isPedMissionActor(me) == false, 'player is not a script ped')
        assert(getPedSuffersCriticalHits(me) == false and canPedBeDraggedOut(me) == false, 'player policy getters')
        assert(setPedStayInSamePlace(me, true) == false and getPedStayInSamePlace(me) == false, 'player stay')
        assert(setPedPhysicalProofs(me, true, true, true, true, true) == false, 'player proofs')
        local b, f, e, c, m = getPedPhysicalProofs(me)
        assert(b == false and f == false and e == false and c == false and m == false, 'empty proofs')
        assert(getPedWeaponAccuracy(me) == -1 and setPedWeaponAccuracy(me, 10) == false, 'unstreamed accuracy')
        assert(getPedWeaponShootingRate(me) == -1 and setPedWeaponShootingRate(me, 10) == false, 'unstreamed rate')
        local ped = createPed(7, 0, 0, 3)
        assert(type(ped) == 'table', 'script ped')
        _G.bodyPed = ped
        assert(setPedFightingStyle(ped, 4) == true and getPedFightingStyle(ped) == 4, 'style stored')
        assert(setPedFightingStyle(ped, 4) == false, 'same style is false')
        ok, err = pcall(setPedFightingStyle, ped, 3)
        assert(not ok and err:find('between 4 and 16'), 'style range: ' .. tostring(err))
        assert(setPedMissionActor(ped, true) == true and isPedMissionActor(ped) == true, 'mission actor')
        assert(setPedStoryProtected(ped, true) == true and isPedStoryProtected(ped) == true, 'story')
        assert(getPedSuffersCriticalHits(ped) == true and setPedSuffersCriticalHits(ped, false) == true and getPedSuffersCriticalHits(ped) == false, 'critical hits')
        assert(canPedBeDraggedOut(ped) == true and setPedCanBeDraggedOut(ped, false) == true and canPedBeDraggedOut(ped) == false, 'drag')
        assert(isPedOnlyDamagedByPlayer(ped) == false and setPedOnlyDamagedByPlayer(ped, true) == true and isPedOnlyDamagedByPlayer(ped) == true, 'only player')
        assert(getPedStayInSamePlace(ped) == false and setPedStayInSamePlace(ped, true) == true and getPedStayInSamePlace(ped) == true, 'stay')
        assert(isPedNeverTargeted(ped) == false and setPedNeverTargeted(ped, true) == true and isPedNeverTargeted(ped) == true, 'never targeted')
        assert(setPedPhysicalProofs(ped, true, false, true, false, true) == true, 'proofs set')
        b, f, e, c, m = getPedPhysicalProofs(ped)
        assert(b == true and f == false and e == true and c == false and m == true, 'proofs stored')
        assert(getPedClothes(ped, 0) == false, 'no clothes yet')
        assert(addPedClothes(ped, 'player_torso', 'torso', 0) == true, 'clothes accepted')
        local texture, model = getPedClothes(ped, 0)
        assert(texture == 'player_torso' and model == 'torso', 'clothes read back')
        assert(removePedClothes(ped, 0) == true and getPedClothes(ped, 0) == false, 'clothes cleared')
        assert(getPedClothes(ped, 17) == false, 'slot 17 is not stored on a script ped')
        assert(destroyElement(ped) == true, 'script ped removed')
        _G.bodyPed = nil
        assert(setPedArmor(me, 0) == true and setPedWalkingStyle(me, 0) == true and setPedHeadless(me, false) == true, 'player restored')
        assert(setPedBleeding(me, false) == true and setPedDoingGangDriveby(me, false) == true and setPedCameraRotation(me, 0) == true, 'player flags restored')
    )LUA");
    lua_State* L = WorldRPG::Script::ActiveVmState();
    lua_getglobal(L, "bodyPed");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "id");
        if (lua_isinteger(L, -1)) WorldRPG::Game::ReleaseScriptPed(static_cast<uint32_t>(lua_tointeger(L, -1)));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    WorldRPG::PedQueries::Snapshot native;
    native.generation = WorldRPG::PedQueries::Generation(777);
    native.taskType = -1;
    native.health = 100.f;
    WorldRPG::Headless::SetPedQueryNative(777, native);
    RunLua(vm, "ped-weapon-bytes", R"LUA(
        local me = {id = 777, element = true}
        assert(setPedWeaponAccuracy(me, 40) == true and getPedWeaponAccuracy(me) == 40, 'accuracy stored while streamed')
        assert(setPedWeaponAccuracy(me, 256) == false and setPedWeaponAccuracy(me, -1) == false and getPedWeaponAccuracy(me) == 40, 'accuracy range')
        assert(setPedWeaponShootingRate(me, 12) == true and getPedWeaponShootingRate(me) == 12, 'rate stored')
        assert(setPedWeaponShootingRate(me, 256) == false and getPedWeaponShootingRate(me) == 12, 'rate range')
        assert(setPedTaskSequence(me, {}) == false, 'empty sequence')
        assert(setPedTaskSequence(me, {{task = 'die'}}) == false, 'sequence factory')
        assert(setPedTaskSequence(me, {{task = 'go_to', x = 1, y = 2, z = 3}}) == false, 'go to factory')
        assert(setPedTaskSequence(me, {{task = 'drive_to', x = 1, y = 2, z = 3}}) == false, 'drive to needs the driver')
        assert(killPedTask(me, 'primary', 0) == false, 'primary task is not removed')
    )LUA");
    native.taskType = 218;
    native.health = 0.f;
    WorldRPG::Headless::SetPedQueryNative(777, native);
    RunLua(vm, "ped-weapon-bytes-dead", R"LUA(
        local me = {id = 777, element = true}
        assert(setPedWeaponAccuracy(me, 7) == false and setPedWeaponShootingRate(me, 7) == false, 'dead ped rejects the byte')
    )LUA");
    WorldRPG::Headless::ClearPedQueryNative();
    RunLua(vm, "ped-weapon-bytes-away", R"LUA(
        local me = {id = 777, element = true}
        assert(getPedWeaponAccuracy(me) == -1 and getPedWeaponShootingRate(me) == -1, 'unstreamed hides the byte')
        assert(getPlayerMoney() == 0 and getPlayerWantedLevel() == 0, 'money and wanted start at 0')
        assert(setPlayerMoney(40) == true and getPlayerMoney() == 40, 'money set')
        assert(setPlayerMoney(12, true) == true and getPlayerMoney() == 12, 'instant money')
        assert(givePlayerMoney(5) == true and takePlayerMoney(2) == true and getPlayerMoney() == 15, 'give and take')
        assert(setPlayerMoney(-4) == true and getPlayerMoney() == -4, 'negative money')
        assert(setPlayerMoney() == false and givePlayerMoney(0/0) == false and setPlayerMoney(12, 1) == false, 'bad money')
        assert(getPlayerMoney() == -4, 'bad money does not change the balance')
        assert(isPlayerMapForced() == false and forcePlayerMap(true) == true and isPlayerMapForced() == true, 'map forced')
        assert(forcePlayerMap(false) == true and isPlayerMapVisible() == false and getPlayerMapBoundingBox() == false, 'map stays hidden')
        assert(getPlayerMapOpacity() == 155, 'default map alpha')
        assert(forcePlayerMap(1) == false, 'map force wants a bool')
        assert(getPlayerScriptDebugLevel() == 0 and isPlayerCrosshairVisible() == false, 'debug level and crosshair')
        assert(getPlayerUserName() == false, 'no community username')
        local ped = createPed(7, 1, 2, 3)
        assert(setPedAnimation(ped) == true and setPedAnimation(ped, false) == true, 'empty animation kills')
        assert(setPedAnimation(ped, 'ped', 'idle') == false and getPedAnimation(ped) == false, 'named block is not loaded')
        assert(getPedAnimationProgress(ped) == -1 and getPedAnimationSpeed(ped) == -1 and getPedAnimationLength(ped) == -1, 'no anim task')
        assert(setPedAnimationProgress(ped) == true and setPedAnimationProgress(ped, 'idle', 0.5) == false, 'progress needs the association')
        assert(setPedAnimationSpeed(ped, 'idle', 1) == false and setPedAnimationSpeed(ped, 'idle', 11) == false and setPedAnimationSpeed(ped) == false, 'speed rejected')
        assert(isPedReloadingWeapon(ped) == false and isPedReloadingWeapon(me) == false, 'not reloading')
        assert(destroyElement(ped) == true, 'anim ped removed')
        local x, y = getPlayerHudComponentProperty('health', 'position')
        assert(x == 0 and y == 0, 'unset hud position')
        assert(setPlayerHudComponentProperty('health', 'position', 12, 34) == true, 'hud position')
        x, y = getPlayerHudComponentProperty('health', 'position')
        assert(x == 12 and y == 34, 'hud position stored')
        assert(setPlayerHudComponentProperty('radar', 'position', 1, 2) == false, 'radar position is rejected')
        assert(getPlayerHudComponentProperty('radar', 'position') == false, 'radar layout is not read')
        assert(setPlayerHudComponentProperty('health', 'fillColor', 16711680) == true, 'health color')
        local r, g, b, a = getPlayerHudComponentProperty('health', 'fillColor')
        assert(r == 255 and g == 0 and b == 0 and a == 0, 'color bytes')
        assert(setPlayerHudComponentProperty('health', 'drawPercentage', true) == true and getPlayerHudComponentProperty('health', 'drawPercentage') == true, 'percentage')
        assert(setPlayerHudComponentProperty('money', 'fontStyle', 'pricedown') == true and getPlayerHudComponentProperty('money', 'fontStyle') == 'pricedown', 'font')
        assert(setPlayerHudComponentProperty('money', 'fontStyle', 'nope') == false, 'unknown font')
        assert(getPlayerHudComponentProperty('health', 'fontStyle') == false, 'health is not text')
        assert(resetPlayerHudComponentProperty('health', 'position') == true, 'reset position')
        x, y = getPlayerHudComponentProperty('health', 'position')
        assert(x == 0 and y == 0, 'position cleared')
        assert(resetPlayerHudComponentProperty('crosshair', 'position') == false, 'crosshair cannot reset')
        assert(setPlayerHudComponentProperty('money', 'textSize', 1, 2) == false and resetPlayerHudComponentProperty('money', 'textSize') == false, 'text size is read only')
        local ped = createPed(7, 4, 5, 6)
        assert(setPedGoTo(ped, 1, 2, 3) == false and setPedNavigateTo(ped, 1, 2, 3) == false, 'unstreamed task')
        assert(setPedStandStill(ped) == false and setPedHandsUp(ped, 1000) == false and setPedJump(ped) == false, 'unstreamed simple tasks')
        assert(setPedTurnToFace(ped, me) == false and setPedKillOnFoot(ped, me) == false, 'unstreamed target tasks')
        assert(setPedAimAt(ped, 0, 0, 1) == false and setPedShootAt(ped, 0, 1, 1) == false, 'zero aim is rejected when it runs')
        assert(setPedFacialTalk(ped, -1) == false and stopPedFacialTalk(ped) == false, 'no facial task')
        assert(setPedEnterVehicle(ped) == false and setPedExitVehicle(ped) == false, 'no enter task')
        assert(getPedTaskSequenceProgress(ped) == -1 and isPedNativeTaskReady(ped) == false, 'no sequence')
        assert(isPedNativeAmbientCopWanderTask(ped) == false, 'no cop wander')
        local ok, err = pcall(setPedGoTo, ped, 'nope')
        assert(not ok and err:find('vector3', 1, true), 'goto vector: ' .. tostring(err))
        ok, err = pcall(setPedHandsUp, ped)
        assert(not ok, 'hands up needs a duration')
        assert(destroyElement(ped) == true, 'task ped removed')
        assert(acquireMissionText('') == false and acquireMissionText('BAD NAME') == false and acquireMissionText('TOOLONG1') == false, 'gxt names')
        assert(acquireMissionText('INTRO') == false and showMissionText('INTRO', 1000) == false, 'no mission text block')
        assert(clearMissionTexts() == false and clearMissionHelp() == false and releaseMissionText() == false, 'no mission lease')
        assert(killPed(me) == false, 'player is not a client ped')
        local cj = createPed(0, 0, 0, 3)
        assert(setPedStat(cj, 24, 50) == true and getPedStat(cj, 24) == 50, 'stat stored')
        assert(setPedStat(cj, 21, 10) == true and setPedStat(cj, 400, 1) == false and setPedStat(cj, 24, 1001) == false, 'stat range and cj muscle')
        assert(killPed(cj) == true and isPedDead(cj) == true and killPed(cj) == false, 'client ped dies once')
        assert(setPedAimTarget(cj, 1, 2, 3) == true, 'aim target accepted')
        assert(getElementBonePosition(cj, 8) == false and getElementBonePosition(cj, 400) == false, 'no bone entity')
        assert(getPedBonePosition(cj, 8) == false and getPedBonePosition(cj, 200) == false, 'ped bone')
        assert(updateElementRpHAnim(cj) == false and detonateSatchels() == false, 'no anim hierarchy or satchels')
        assert(removePedFromVehicle(cj) == false and removePedFromVehicle(me) == false, 'not in a vehicle')
        assert(destroyElement(cj) == true, 'cj removed')
        local other = createPed(7, 0, 0, 3)
        assert(setPedStat(other, 21, 10) == false and getPedStat(other, 21) == 0, 'fat stat needs cj')
        assert(destroyElement(other) == true, 'other ped removed')
        local cj = createPed(0, 0, 0, 3)
        local mate = createPed(7, 1, 0, 3)
        assert(getPedVoice(cj) == false and setPedVoice(cj, 'PED_TYPE_DISABLED') == true, 'disable speech')
        assert(getPedVoice(cj) == 'PED_TYPE_DISABLED' and resetPedVoice(cj) == false, 'voice shorts stay')
        assert(getPedVoice(cj) == 'PED_TYPE_DISABLED', 'speech stays disabled')
        assert(setPedVoice(cj, 'PED_TYPE_GEN', 'VOICE_GEN_BB') == true and getPedVoice(cj) == false, 'voice names are not read')
        local ok, err = pcall(playPedVoiceLine, cj, 400)
        assert(not ok and err:find('0-359', 1, true), 'speech range: ' .. tostring(err))
        assert(pcall(playPedVoiceLine, cj, 1), 'say returns no values')
        assert(getPedAnalogControlState(me, 'forwards') == 0 and getPedAnalogControlState(me, 'not_a_control') == 0, 'player analog is the controller')
        assert(setPedAnalogControlState(me, 'forwards', 1) == true and getPedAnalogControlState(me, 'forwards') == 0, 'player get ignores the pad')
        assert(getPedAnalogControlState(cj, 'forwards') == 0 and getPedAnalogControlState(cj, 'fire') == 0, 'script pad starts at 0')
        assert(getPedAnalogControlState(cj, 'not_a_control') == false, 'unknown script control')
        assert(setPedAnalogControlState(cj, 'forwards', 2) == true and getPedAnalogControlState(cj, 'forwards') == 1, 'analog clamps')
        assert(setPedAnalogControlState(cj, 'backwards', 0.25) == true, 'opposite analog')
        assert(getPedAnalogControlState(cj, 'forwards') == 0 and getPedAnalogControlState(cj, 'backwards') == 0.25, 'opposite cleared')
        assert(setPedAnalogControlState(cj, 'fire', 1) == false, 'fire is not analog')
        assert(givePedWeapon(me, 22) == false and givePedWeapon(cj, 'nope') == false and givePedWeapon(cj, 19) == false, 'weapon rejected')
        assert(givePedWeapon(cj, 'pistol', 5, true) == true and getPedWeapon(cj) == 22 and getPedTotalAmmo(cj) == 5, 'pistol alias')
        assert(givePedWeapon(cj, 22, 30, true) == true and getPedWeapon(cj) == 22 and getPedTotalAmmo(cj) == 35, 'script weapon cache')
        assert(setPedLookAt(cj, 1, 2, 3) == true and setPedLookAt(cj, 1, 2, 3, 3000, 500) == true, 'look at')
        assert(setPedLookAt(cj, 'nope') == false and setPedLookAt(cj, 1, 2, 3, cj) == false, 'look at arguments')
        ok, err = pcall(killPedTask, cj, 'primary', 4)
        assert(not ok and err:find('TASK_PRIORITY_DEFAULT', 1, true), 'default task: ' .. tostring(err))
        ok, err = pcall(killPedTask, cj, 'primary', 5)
        assert(not ok and err:find('Invalid task slot', 1, true), 'primary slot: ' .. tostring(err))
        ok, err = pcall(killPedTask, cj, 'nope', 0)
        assert(not ok and err:find('enum', 1, true), 'task type: ' .. tostring(err))
        assert(killPedTask(cj, 'secondary', 1) == false, 'secondary task is not removed')
        local carried, carryState = getPedCarriedObject(cj)
        assert(carried == false and carryState == 'released', 'not carrying')
        assert(setPedCarryObject(cj, cj) == false and pickUpPedObject(cj, cj) == false, 'carry rejected')
        assert(putDownPedObject(cj) == false and cancelPedCarryObject(cj) == false and getPedNoiseLevel(cj) == false, 'no cargo or noise')
        assert(warpPedIntoVehicle(cj) == false, 'warp needs a vehicle')
        ok, err = pcall(setPedDriveTo, cj)
        assert(not ok, 'drive to needs a vehicle')
        ok, err = pcall(setPedDriveBy, cj)
        assert(not ok, 'drive by needs a target')
        assert(setPedTaskSequence(cj, {{task = 'die'}}) == false, 'unstreamed sequence')
        assert(acquirePedNativeEventProfile(me, 'mission') == false and acquirePedNativeEventProfile(cj, 'nope') == false, 'event profile rejected')
        assert(acquirePedNativeEventProfile(cj, 'mission') == false, 'no event lease')
        assert(acquirePedNativeGroup({cj}, 'ambient-random') == false and acquirePedNativeGroup({cj, me}, 'ambient-random') == false, 'group rejected')
        assert(acquirePedNativeGroup({cj, mate}, 'nope') == false and acquirePedNativeGroup({cj, mate}, 'ambient-random') == false, 'no group lease')
        assert(releasePedNativeGroup(1) == false and isPedNativeGroupActive(1) == false and getPedNativeGroupDiagnostic(1) == false, 'group query')
        assert(validatePedNativeCouple(cj, mate) == false and acquirePedNativeCouple(cj, mate, 3) == false, 'couple rejected')
        assert(acquirePedNativeCouple(cj, mate, 1) == false and releasePedNativeCouple(1) == false, 'no couple lease')
        assert(getPedNativeVehicleResponse(cj, 1) == false and getPedNativeVehicleResponse(me, 1) == false, 'no vehicle response')
        assert(addPedNativeDamageResponseEvent(cj, mate, 99, 3, 1) == false and addPedNativeDamageResponseEvent(cj, mate, 22, 3, 1) == false, 'damage response')
        assert(addPedNativeGunAimedAtEvent(cj, mate, 1) == false and addPedNativeDamageEvent(cj, mate, 22, 3, 10, 1, 1) == false, 'native events')
        assert(restorePedNativeVehicleResponse(cj, {{type = 1, values = {}}}, 1) == false, 'restore response')
        assert(acquirePedNativePointArm(cj) == false and isPedNativePointArmActive(1) == false, 'point arm')
        assert(addPedNativeBikeJackTask(me, cj, me, 10, 0, true, 1) == false, 'bike jack needs a vehicle')
        assert(destroyElement(cj) == true and destroyElement(mate) == true, 'voice peds removed')
        local ok, err = pcall(setPlayerHudComponentProperty, 'nope', 'position', 0, 0)
        assert(not ok and err:find('hud-component', 1, true), 'bad component: ' .. tostring(err))
    )LUA");
}

void CheckLowRiskLuaQueries(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "valid-ped-models", R"LUA(
        local models = getValidPedModels()
        assert(#models == 310 and models[1] == 0 and models[#models] == 312, 'stock skin range')
        local present = {}
        for i, model in ipairs(models) do
            assert(type(model) == 'number' and not present[model], 'unique numeric models')
            assert(i == 1 or models[i - 1] < model, 'ascending model IDs')
            present[model] = true
        end
        assert(not present[74] and not present[149] and not present[208], 'missing stock skins')
        assert(present[73] and present[75] and present[148] and present[150] and present[207] and present[209], 'neighbors remain')
        for _, arg in ipairs({true, false, 'bad', 1, {}}) do
            local other = getValidPedModels(arg)
            assert(#other == #models, 'no allocated custom models')
            for i, model in ipairs(models) do assert(other[i] == model, 'same stock list') end
        end
        assert(#getValidPedModels(nil) == #models, 'nil takes the default')
        models[1] = -100
        assert(getValidPedModels()[1] == 0, 'fresh table per call')
    )LUA");

    RunLua(vm, "bound-keys-query", R"LUA(
        assert(getBoundKeys() == false and getBoundKeys(false) == false and getBoundKeys({}) == false, 'bad argument')
        assert(getBoundKeys('unbound-query-command') == false, 'unbound is false, not an empty table')
        local callback = function() end
        assert(bindKey('j', 'down', callback), 'function bind')
        assert(getBoundKeys('j') == false, 'function binds are not commands')
        assert(bindKey('j', 'down', 'queryCommand', 'one'), 'down bind')
        assert(bindKey('k', 'up', 'queryCommand', 'two'), 'up bind')
        local keys = getBoundKeys('QUERYCOMMAND')
        assert(keys.j == 'down' and keys.k == 'up', 'case-insensitive command and keyed table')
        keys.j = 'changed'
        assert(getBoundKeys('queryCommand').j == 'down', 'query does not expose storage')
        assert(bindKey('l', 'both', 'queryCommand'), 'both bind')
        assert(getBoundKeys('queryCommand').l == 'up', 'later state wins for both')
        assert(bindKey('j', 'up', 'queryCommand', 'three'), 'second state on same key')
        assert(getBoundKeys('queryCommand').j == 'up', 'later bind wins')
        assert(unbindKey('j', 'up', 'queryCommand'), 'remove later state')
        assert(getBoundKeys('queryCommand').j == 'down', 'remaining state is visible')
        assert(bindKey('m', 'down', 'fire'), 'command named like a GTA control')
        assert(bindKey('fire', 'down', callback), 'script callback for a control')
        assert(getBoundKeys('FIRE') == false, 'control lookup precedes commands; no physical control mappings')
        assert(bindKey('n', 'down', '123'), 'numeric command name')
        assert(getBoundKeys(123).n == 'down', 'ReadString accepts a number')
        assert(unbindKey('j', 'down', 'queryCommand') and unbindKey('j', 'down', callback), 'cleanup j')
        assert(unbindKey('k', 'up', 'queryCommand') and unbindKey('l', 'both', 'queryCommand'), 'cleanup k and l')
        assert(unbindKey('m', 'down', 'fire') and unbindKey('n', 'down', '123'), 'cleanup m and n')
        assert(unbindKey('fire', 'down', callback), 'cleanup control callback')
        assert(getBoundKeys('queryCommand') == false and getBoundKeys(123) == false, 'removed binds disappear')
    )LUA");
}

// Element, player, bit and vehicle helpers that do not need a GTA native.
void CheckHelpers(WorldRPG::Script::LuaVMStub& vm) {
    WorldRPG::Net::HttpDownloader::SetResourceCacheRoot("/tmp/neon-dx-cache");
    std::filesystem::create_directories("/tmp/neon-dx-cache/worldrpg_cars/models");
    std::ofstream("/tmp/neon-dx-cache/worldrpg_cars/models/testcar.txd") << "TXD";

    RunLua(vm, "helpers", R"LUA(
        local me = {id = 777, element = true}
        assert(isElement(me) == true, "local player is an element")
        assert(getPlayerName(me) == "Sam", "nick from the join")
        assert(getPlayerFromName("sam") == me or getPlayerFromName("sam").id == 777, "case-insensitive nick")
        assert(getPlayerFromNick("Sam").id == 777, "getPlayerFromNick")
        assert(getPlayerNametagText(me) == "Sam", "nametag starts as the nick")
        assert(setPlayerNametagText(me, "Tag") == true, "set nametag")
        assert(getPlayerNametagText(me) == "Tag", "nametag override")
        assert(setPlayerNametagText(me, "") == false, "empty nametag rejected")
        assert(getPlayerNametagText(me) == "Tag", "rejected text is not stored")
        assert(getPlayerTeam(me) == false, "no team is stored")
        assert(type(getPlayerPing(me)) == "number", "ping is a number")
        assert(destroyElement(me) == false and isElement(me) == true, "local player is not deleted")
        assert(destroyElement(false) == false and destroyElement({}) == false, "non-element")
        local root = getRootElement()
        assert(isElement(root) == true and destroyElement(root) == false and isElement(root) == true, "root stays")
        assert(destroyElement(root) == false, "root is still there")
        setElementData(me, "coin", 3)
        setElementData(me, "label", "x")
        local data = getAllElementData(me)
        assert(type(data) == "table" and data.coin == 3 and data.label == "x", "own element data")
        assert(getAllElementData(false) == false, "getAllElementData needs an element")
        assert(bitAnd(7, 3) == 3 and bitOr(1, 2, 4) == 7 and bitXor(7, 1) == 6, "bit and/or/xor")
        assert(bitNot(0) == 4294967295, "bitNot")
        assert(bitTest(1, 1) == true and bitTest(1, 2) == false, "bitTest")
        assert(bitLShift(1, 4) == 16 and bitRShift(16, 2) == 4, "shifts")
        assert(bitLRotate(1, 1) == 2 and bitRRotate(1, 1) == 2147483648, "rotates")
        assert(bitExtract(255, 0, 4) == 15 and bitReplace(255, 0, 0, 4) == 240, "extract and replace")
        assert(bitAnd(true) == false and bitTest(true) == nil, "bad bit arguments")
        assert(getVehicleType(400) == "Automobile" and getVehicleType(460) == "Plane", "vehicle type")
        assert(getVehicleType(481) == "BMX" and getVehicleType(520) == "Plane", "more types")
        assert(getVehicleType(700) == "" and getVehicleType("nope") == false, "bad vehicle type")
        local sound = playSound("http://example/login.mp3", false)
        assert(type(sound) == "table" and isElement(sound) == true, "playSound stays an element")
        assert(getElementType(sound) == "sound", "sound type")
        assert(setSoundVolume(sound, 0.65) == true, "setSoundVolume")
        local volume = getSoundVolume(sound)
        assert(type(volume) == "number" and math.abs(volume - 0.65) < 0.01, "getSoundVolume")
        assert(stopSound(sound) == true and isElement(sound) == false, "stopSound removes it")
        assert(stopSound(sound) == false, "stopSound on a stopped sound")
        local px, py, pz, lx, ly, lz, roll, fov = getCameraMatrix()
        assert(setCameraMatrix(10, 20, 30, 11, 21, 31, 0, 80) == true, "setCameraMatrix")
        px, py, pz, lx, ly, lz, roll, fov = getCameraMatrix()
        assert(px == 10 and ly == 21 and fov == 80, "matrix stored")
        assert(setCameraMatrix(1, 2, 3, 4, 5, 6, 0, 0) == true, "fov 0 becomes 70")
        px, py, pz, lx, ly, lz, roll, fov = getCameraMatrix()
        assert(fov == 70, "clamped fov")
        local objects, vehicles = getCameraClip()
        assert(objects == true and vehicles == true, "clip defaults")
        assert(setCameraClip(false, true) == true, "setCameraClip")
        objects, vehicles = getCameraClip()
        assert(objects == false and vehicles == true, "clip stored")
        assert(setCameraViewMode(2, 1) == true, "view mode")
        local vehicleMode, pedMode = getCameraViewMode()
        assert(vehicleMode == 2 and pedMode == 1, "view mode stored")
        assert(fadeCamera(true, 0) == true, "fadeCamera")
        assert(setCameraDrunkLevel(40) == true and getCameraDrunkLevel() == 40, "drunk level")
        assert(setCameraDrunkLevel(300) == false, "drunk level range")
        assert(setCameraGoggleEffect("nightvision") == true, "goggles")
        assert(getCameraGoggleEffect() == "nightvision", "goggle read")
        assert(setCameraGoggleEffect("nope") == false, "bad goggles")
        assert(shakeCamera(1.5) == true and resetShakeCamera() == true, "shake")
        assert(getElementType(getCamera()) == "camera" and isElement(getCamera()) == true, "camera element")
        assert(acquireScriptCamera() == false, "no resource cannot lease the camera")
        local timer = setTimer(function() end, 1000, 1)
        assert(isTimer(timer) == true, "live timer")
        assert(killTimer(timer) == true and isTimer(timer) == false, "killed timer")
        assert(isTimer("no") == false and killTimer(false) == false, "bad timer")
        assert(setElementAlpha(me, 40) == true and getElementAlpha(me) == 40, "alpha")
        assert(setElementAlpha(me, 300) == false, "alpha range")
        assert(setElementInterior(me, 3) == true and getElementInterior(me) == 3, "interior")
        assert(setElementInterior(me, 300) == false, "interior range")
        assert(setElementDimension(me, 9) == true and getElementDimension(me) == 9, "dimension")
        assert(setElementDimension(me, -1) == false and setElementDimension(me, 70000) == false, "dimension range")
        assert(isElementFrozen(me) == false, "not frozen")
        assert(setElementHealth(me, 50) == true and getElementHealth(me) == 50, "health")
        assert(type(getElementModel(me)) == "number", "model")
        local rx, ry, rz = getElementRotation(me)
        assert(type(rz) == "number" and rx == 0 and ry == 0, "rotation")
        assert(setGravity(0.01) == true and math.abs(getGravity() - 0.01) < 0.0001, "gravity")
        assert(setGravity(2) == false, "gravity range")
        assert(setGameSpeed(2) == true and getGameSpeed() == 2, "game speed")
        assert(setGameSpeed(11) == false, "game speed range")
        assert(setMinuteDuration(2000) == true and getMinuteDuration() == 2000, "minute")
        assert(setMinuteDuration(0) == false, "minute range")
        assert(setFarClipDistance(500) == true and getFarClipDistance() == 500, "far clip")
        assert(resetFarClipDistance() == true and getFarClipDistance() == 800, "far clip reset")
        assert(setFogDistance(80) == true and getFogDistance() == 80, "fog")
        assert(resetFogDistance() == true and getFogDistance() == 150, "fog reset")
        assert(setWeather(7) == true, "weather")
        local weather, blend = getWeather()
        assert(weather == 7 and blend == nil, "weather read")
        assert(setSoundLooped(sound, true) == false, "stopped sound does not loop")
        local live = playSound("http://example/loop.mp3", false)
        assert(isSoundLooped(live) == false, "not looped")
        assert(setSoundLooped(live, true) == true and isSoundLooped(live) == true, "loop")
        assert(setSoundPaused(live, true) == true and isSoundPaused(live) == true, "pause")
        assert(setSoundPaused(live, false) == true and isSoundPaused(live) == false, "resume")
        assert(setSoundPosition(live, 1.5) == true, "sound position")
        assert(getSoundPosition(live) >= 1.5, "sound position read")
        assert(playSoundFrontEnd(40) == true and playSoundFrontEnd(200) == false, "frontend sound")
        local held = setTimer(function() end, 5000, 2)
        assert(setTimerPaused(held, true) == true and isTimerPaused(held) == true, "timer pause")
        local left, repeats, delay = getTimerDetails(held)
        assert(type(left) == "number" and repeats == 2 and delay == 5000, "timer details")
        assert(resetTimer(held) == true, "timer reset")
        assert(type(getTimers(0)) == "table", "timer list")
        assert(getEasingValue(0.5, "Linear") == 0.5, "easing")
        local ix, iy, iz = interpolateBetween(0, 0, 0, 10, 0, 0, 0.5, "Linear")
        assert(ix == 5 and iy == 0 and iz == 0, "interpolate")
        assert(getRealTime().year >= 126, "real time")
        local parts = split("a,b", ",")
        assert(parts[1] == "a" and parts[2] == "b", "split")
        local cr, cg, cb = getColorFromString("#ff0000")
        assert(cr == 255 and cg == 0 and cb == 0, "color")
        assert(utfLen("é") == 1 and utfSub("éx", 2) == "x", "utf")
        assert(utfChar(65) == "A" and utfCode("A") == 65 and utfSeek("A", 1) == 1, "utf code")
        assert(getVersion().os == "Android", "version")
        assert(getWeaponNameFromID(22) == "Colt 45", "weapon name")
        assert(getWeaponIDFromName("AK-47") == 30 and getSlotFromWeapon(31) == 5, "weapon id")
        assert(setElementCollisionsEnabled(me, false) == true and getElementCollisionsEnabled(me) == false, "collisions")
        assert(setNearClipDistance(1) == true and getNearClipDistance() == 1, "near clip")
        assert(setCloudsEnabled(false) == true and getCloudsEnabled() == false, "clouds")
        assert(setBirdsEnabled(false) == true and getBirdsEnabled() == false, "birds")
        assert(setRainLevel(0.5) == true and getRainLevel() == 0.5, "rain")
        assert(setBlurLevel(10) == true and getBlurLevel() == 10, "blur")
        assert(setMoonSize(4) == true and getMoonSize() == 4, "moon")
        assert(setSunSize(2) == true and getSunSize() == 2, "sun size")
        assert(setSunColor(1, 2, 3) == true, "sun color")
        assert(setSkyGradient(1, 2, 3, 4, 5, 6, 7, 8) == true and resetSkyGradient() == true, "sky")
        assert(setWindVelocity(1, 0, 0) == true, "wind")
        assert(setWaveHeight(1) == true and getWaveHeight() == 1, "waves")
        assert(setFPSLimit(60) == true and getFPSLimit() == 60, "fps")
        assert(setFPSLimit(10) == true and getFPSLimit() == 10, "fps low")
        assert(setFPSLimit(-1) == false, "fps range")
        assert(md5("") == "d41d8cd98f00b204e9800998ecf8427e", "md5")
        assert(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256")
        assert(hash("md5", "a") == md5("a"), "hash")
        assert(base64Decode(base64Encode("hello")) == "hello", "base64")
        assert(teaDecode(teaEncode("1234", "key"), "key") == "1234", "tea")
        assert(gettok("a,b,c", 2, ",") == "b", "gettok")
        assert(pregFind("abc", "b") == true, "preg find")
        assert(pregReplace("a1b", "\\d", "x") == "axb", "preg replace")
        local matched = pregMatch("a1b2", "\\d")
        assert(matched[1] == "1" and matched[2] == "2", "preg match")
        local refId = ref("kept")
        assert(deref(refId) == "kept", "ref")
        local compiled, loadError = loadstring("return 7")
        assert(loadError == nil and compiled() == 7, "loadstring")
        assert(dofile("x") == false and loadfile("x") == false, "file loaders are disabled")
        assert(io == nil and package == nil and os.execute == nil and debug.getregistry == nil)
        assert(type(os.time()) == "number" and type(debug.traceback()) == "string", "safe standard Lua utilities")
        assert(load("return 11", "text test")() == 11)
        local binary = string.dump(function() return 7 end)
        local readerPart = binary
        assert(load(binary) == nil and load(function()
            local part = readerPart
            readerPart = nil
            return part
        end) == nil and loadstring(binary) == nil)
        assert(getBodyPartName(3) == "Torso" and getClothesTypeName(1) == "Hair", "names")
        assert(isOOPEnabled() == false and getLocalization().code == "en_US", "locale")
        assert(setWorldSpecialPropertyEnabled("hovercars", true) == true and isWorldSpecialPropertyEnabled("hovercars") == true, "world prop")
        assert(resetRainLevel() == true and resetBlurLevel() == true, "world reset")
        assert(setJetpackMaxHeight(40) == true and getJetpackMaxHeight() == 40, "jetpack height")
        assert(setJetpackMaxHeight(-21) == false, "jetpack range")
        assert(setElementDoubleSided(me, true) == true and isElementDoubleSided(me) == true, "double sided")
        assert(setSoundMaxDistance(live, 30) == true and getSoundMaxDistance(live) == 30, "sound distance")
        local created = fileCreate("note.txt")
        assert(type(created) == "number" and fileWrite(created, "hi") == 2 and fileGetPos(created) == 2, "file write")
        assert(fileSetPos(created, 0) == 0 and fileRead(created, 2) == "hi", "file read back")
        assert(fileClose(created) == true and fileGetContents("note.txt") == "hi", "file read")
        assert(fileExists("note.txt") == true and pathIsFile("note.txt") == true, "file exists")
        assert(fileDelete("note.txt") == true and fileExists("note.txt") == false, "file delete")
        assert(fileOpen("../secret") == false, "file sandbox")
        local dotted = fileCreate("a..b.txt")
        assert(type(dotted) == "number" and fileClose(dotted) == true and fileDelete("a..b.txt") == true, "dots in a file name")
        assert(fileExists("models/testcar.txd") == false, "relative path does not scan unrelated resources")
        local assetPath = ":worldrpg_cars/models/testcar.txd"
        local asset = fileOpen(assetPath)
        assert(type(asset) == "number" and fileRead(asset, 3) == "TXD", "explicit cached asset")
        assert(fileSetPos(asset, 0) == 0 and fileWrite(asset, "overwrite") == false and fileClose(asset))
        assert(fileCreate(assetPath) == false and fileDelete(assetPath) == false, "cache stays intact")
        assert(fileGetContents(assetPath) == "TXD")
        assert(fileExists(assetPath .. string.char(0) .. "suffix") == false, "embedded NUL is invalid")
        local matrix = getElementMatrix(me)
        assert(type(matrix) == "table" and type(matrix[4][1]) == "number" and matrix[4][4] == 1, "matrix")
        assert(setHeatHaze(10) == true and getHeatHaze() == 10, "heat haze")
        assert(resetHeatHaze() == true, "heat haze reset")
        assert(setColorFilter(1, 2, 3, 4, 5, 6, 7, 8) == true, "color filter")
        local fr = getColorFilter()
        assert(fr == 1, "color filter read")
        assert(setTrafficLightState(2) == true and getTrafficLightState() == 2, "lights")
        assert(setTrafficLightState("green", "red") == true and getTrafficLightState() == 0 and areTrafficLightsLocked() == true, "light colors")
        assert(setTrafficLightState("auto") == true and areTrafficLightsLocked() == false, "light auto")
        assert(setTimeFrozen(true) == true and isTimeFrozen() == true and resetTimeFrozen() == true, "frozen time")
        assert(setAircraftMaxVelocity(50) == true and getAircraftMaxVelocity() == 50, "aircraft speed")
        assert(setAircraftMaxVelocity(-1) == false, "aircraft range")
        assert(setCoronaReflectionsEnabled(1) == true and getCoronaReflectionsEnabled() == 1, "corona")
        assert(setCoronaReflectionsEnabled(3) == false, "corona range")
        assert(setWorldProperty("SpriteSize", 2) == true and getWorldProperty("SpriteSize") == 2, "world property")
        local ar, ag, ab = getWorldProperty("AmbientColor")
        assert(setWorldProperty("AmbientColor", 1, 2, 3) == true and ar ~= nil, "ambient before")
        ar, ag, ab = getWorldProperty("AmbientColor")
        assert(ar == 1 and ag == 2 and ab == 3, "ambient")
        assert(setWorldProperty("nope", 1) == false, "bad property")
        assert(setWeatherBlended(8) == true, "blend")
        local current, blend = getWeather()
        assert(blend == 8 and type(current) == "number", "blend read")
        assert(setGarageOpen(1, true) == true and isGarageOpen(1) == true, "garage")
        assert(setDynamicPedShadowsEnabled(false) == true and isDynamicPedShadowsEnabled() == false, "shadows")
        local xml = xmlLoadString('<meta><info name="x">hi</info></meta>')
        assert(xmlNodeGetName(xml) == "meta", "xml name")
        local info = xmlFindChild(xml, "info", 0)
        assert(xmlNodeGetAttribute(info, "name") == "x" and xmlNodeGetValue(info) == "hi", "xml child")
        local script = xmlCreateChild(xml, "script")
        assert(xmlNodeSetAttribute(script, "src", "a.lua") == true, "xml attr")
        assert(xmlNodeGetName(xmlNodeGetParent(script)) == "meta", "xml parent")
        assert(xmlSaveFile(xml, "meta.xml") == true and xmlLoadFile("meta.xml") ~= false, "xml file")
        assert(xmlUnloadFile(xml) == true and xmlNodeGetName(xml) == false, "xml unload")
        assert(setWaterLevel(5) == true and resetWaterLevel() == true, "water level")
        assert(setOcclusionRegion(0, 0, 10, 10) == 1 and resetOcclusionRegion() == true, "occlusion")
        assert(setDevelopmentMode(true) == true and getDevelopmentMode() == true, "dev mode")
        assert(setTransferBoxVisible(true) == true and isTransferBoxVisible() == true, "transfer")
        assert(setNeonClientSetting("a", "b") == true and getNeonClientSetting("a") == "b", "neon setting")
        assert(resetNeonClientSettings() == true and getNeonClientSetting("a") == false, "neon reset")
        assert(setClipboard("hi") == true, "clipboard")
        assert(type(getProcessMemoryStats()) == "table", "memory")
        assert(getKeyboardLayout() == "ltr" and isVoiceEnabled() == false, "client flags")
        assert(getUserdataType(me) == false, "userdata type")
        assert(setWeaponRenderEnabled(false) == true and isWeaponRenderEnabled() == false, "weapon render")
        local ran = false
        assert(addCommandHandler("hi", function() ran = true end) == true, "command add")
        assert(executeCommandHandler("hi", "there") == true and ran == true, "command run")
        assert(getCommandHandlers()[1][1] == "hi", "command list")
        assert(removeCommandHandler("hi") == true and executeCommandHandler("hi") == false, "command remove")
        local hashed = fileCreate("hash.txt")
        assert(fileWrite(hashed, "abc") == 3 and fileFlush(hashed) == true, "hash write")
        assert(fileGetHash(hashed, "md5") == md5("abc"), "file hash")
        assert(fileClose(hashed) == true and fileDelete("hash.txt") == true, "hash cleanup")
        local foliage = createFoliage(0, 0, 0, 1, 0, 0, 0, 1, 0, 3, 0.5)
        assert(isElement(foliage) == true and getElementType(foliage) == "foliage", "foliage element")
        assert(getFoliageSurface(foliage) == 3 and getFoliageDensity(foliage) == 0.5, "foliage values")
        assert(setFoliageSurface(foliage, 300) == false, "foliage surface range")
        assert(setFoliageDensity(foliage, 2) == true and getFoliageDensity(foliage) == 2, "foliage density")
        local first = getFoliageVertices(foliage)
        assert(first.x == 0 and first.y == 0 and first.z == 0, "foliage vertex")
        local effect = createEffect("prt_blood", 1, 2, 3)
        assert(getElementType(effect) == "effect", "effect element")
        assert(setEffectSpeed(effect, 2) == true and getEffectSpeed(effect) == 2, "effect speed")
        assert(setEffectDensity(effect, 0.25) == true and getEffectDensity(effect) == 0.25, "effect density")
        assert(destroyElement(foliage) == true and isElement(foliage) == false, "foliage destroy")
        assert(createFire(0, 0, 1, 2) == true and createFire("no") == false, "fire")
        local texture, model = getClothesByTypeIndex(0, 0)
        assert(texture == "player_torso" and model == "torso", "clothes index")
        local clothesType, clothesIndex = getTypeIndexFromClothes("foot", "feet")
        assert(clothesType == 3 and clothesIndex == 0, "clothes lookup")
        assert(getClothesByTypeIndex(0, 999) == false, "clothes range")
        clothesType, clothesIndex = getTypeIndexFromClothes("player_torso")
        assert(clothesType == 0 and clothesIndex == 0, "clothes texture")
        assert(getTypeIndexFromClothes("Player_Torso", "torso") == false, "clothes case")
        assert(getWeaponProperty(22, "std", "damage") == 25, "colt damage")
        assert(getWeaponProperty(22, "std", "weapon_range") == 35, "colt range")
        assert(getOriginalWeaponProperty(23, "poor", "damage") == 40, "silenced poor")
        assert(getWeaponProperty(32, "pro", "damage") == 20, "tec9 pro")
        assert(setWeaponProperty(22, "std", "damage", 99) == true, "weapon set")
        assert(getWeaponProperty(22, "std", "damage") == 99, "weapon override")
        assert(getOriginalWeaponProperty(22, "std", "damage") == 25, "weapon original")
        assert(getWeaponProperty(22, "std", "flag_aim_1st_person") == false, "colt aim flag")
        assert(setWeaponProperty(22, "std", "flag_aim_1st_person", true) == true, "colt flag set")
        assert(getWeaponProperty(22, "std", "flag_aim_1st_person") == true, "colt flag read")
        assert(getWeaponProperty(22, "std", "flags") == 12343, "colt flags word")
        assert(setWeaponProperty(22, "std", "flag_aim_1st_person", true) == false, "colt flag already set")
        assert(getOriginalWeaponProperty(22, "std", "flags") == 12339, "colt flags original")
        assert(getWeaponProperty(22, "std", "nope") == false and getWeaponProperty(0, "std", "damage") == false, "weapon range")
        assert(addClothingModel("customtex", "custommod", 0) == true, "add clothes")
        local addedType, addedIndex = getTypeIndexFromClothes("customtex", "custommod")
        assert(addedType == 0 and addedIndex == 68, "added clothes index")
        local addedTexture, addedModel = getClothesByTypeIndex(0, 68)
        assert(addedTexture == "customtex" and addedModel == "custommod", "added clothes read")
        assert(removeClothingModel("customtex", "custommod", 0) == true, "remove clothes")
        assert(getTypeIndexFromClothes("customtex", "custommod") == false, "removed clothes")
        local function onRender() end
        assert(addEventHandler("onClientRender", me, onRender) == true, "add handler")
        assert(addEventHandler("onClientRender", me, onRender) == false, "duplicate handler")
        local handlers = getEventHandlers("onClientRender", me)
        local found = false
        if type(handlers) == "table" then
            for _, fn in ipairs(handlers) do if fn == onRender then found = true end end
        end
        assert(found, "event handlers")
        local rootHandlers = getEventHandlers("onClientRender", root)
        local onRoot = false
        if type(rootHandlers) == "table" then
            for _, fn in ipairs(rootHandlers) do if fn == onRender then onRoot = true end end
        end
        assert(onRoot == false, "handler stays on the player")
        assert(getEventHandlers("onClientRender") == false, "event handlers need an element")
        local hit = false
        -- CEvents: a custom event exists only after addEvent (CStaticFunctionDefinitions.cpp:115-174).
        assert(addEventHandler("onTest", me, function() end) == false, "handler needs an added event")
        assert(triggerEvent("onTest", me) == nil, "trigger needs an added event")
        assert(addEvent() == false and addEvent("") == false and addEvent("onTest", 1) == false, "addEvent arguments")
        assert(addEvent("onTest") == true and addEvent("onTest", false) == true, "addEvent twice")
        assert(addEvent("onTest", true) == false, "remote flag cannot change")
        assert(addEvent("onRemoteTest", true) == true and addEvent("onRemoteTest") == false, "remote event")
        assert(addEvent("onClientRender") == true and addEvent("onClientRender", true) == false, "built-in event")
        addEventHandler("onTest", me, function(n) hit = n == 5 end)
        assert(triggerEvent("onTest", me, 5) == true and hit == true, "triggerEvent")
        assert(addEvent("onStop") and addEvent("onOuter") and addEvent("onBubble") and addEvent("onPlayerOnly"), "test events")
        addEventHandler("onStop", me, function() cancelEvent() end)
        assert(triggerEvent("onStop", me) == false and wasEventCancelled() == true, "trigger cancel")
        local nested = true
        addEventHandler("onOuter", me, function() nested = triggerEvent("onStop", me) end)
        assert(triggerEvent("onOuter", me) == true and nested == false, "nested cancel")
        local playerOnly = false
        local function onlyMe() playerOnly = true end
        assert(addEventHandler("onPlayerOnly", me, onlyMe) == true, "player handler")
        assert(triggerEvent("onPlayerOnly", root) == true and playerOnly == false, "element filter")
        assert(removeEventHandler("onPlayerOnly", root, onlyMe) == false, "remove is per element")
        assert(removeEventHandler("onPlayerOnly", me, onlyMe) == true, "remove on the element")
        local bubbled = false
        addEventHandler("onBubble", root, function() bubbled = true end)
        assert(triggerEvent("onBubble", me) == true and bubbled == true, "bubble to root")
        assert(triggerEvent("onTest") == nil, "trigger needs an element")
    )LUA");
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
        local bar = guiCreateProgressBar(0, 0, 100, 20, false)
        assert(guiProgressBarSetProgress(bar, 40) == true and guiProgressBarGetProgress(bar) == 40, 'progress')
        local a = guiCreateRadioButton(0, 0, 80, 20, 'A', false)
        local b = guiCreateRadioButton(0, 0, 80, 20, 'B', false)
        assert(guiRadioButtonSetSelected(a, true) == true and guiRadioButtonSetSelected(b, true) == true, 'radios')
        assert(guiRadioButtonGetSelected(a) == false and guiRadioButtonGetSelected(b) == true, 'one radio')
        local scroll = guiCreateScrollBar(0, 0, 100, 16, true, false)
        assert(guiScrollBarSetScrollPosition(scroll, 150) == true and guiScrollBarGetScrollPosition(scroll) == 100, 'scrollbar')
        local edit = guiCreateEdit(0, 0, 80, 20, 'ab', false)
        assert(guiEditSetMaxLength(edit, 2) == true and guiSetText(edit, 'abcd') == true and guiGetText(edit) == 'ab', 'edit length')
        assert(guiEditSetCaretIndex(edit, 1) == true and guiEditGetCaretIndex(edit) == 1, 'caret')
        assert(guiProgressBarSetProgress(bar, 40.9) == true and guiProgressBarGetProgress(bar) == 40, 'progress truncates')
        local dotted = guiCreateStaticImage(0, 0, 8, 8, 'a..b.png', false)
        assert(isElement(dotted) == true and guiCreateStaticImage(0, 0, 8, 8, '../x.png', false) == false, 'image path')
        assert(guiEditSetReadOnly(edit, true) == true and guiEditIsReadOnly(edit) == true, 'readonly')
        local memo = guiCreateMemo(0, 0, 80, 40, 'note', false)
        assert(guiMemoSetVerticalScrollPosition(memo, 10) == true and guiMemoGetVerticalScrollPosition(memo) == 10, 'memo')
        assert(guiWindowSetMovable(win, false) == true and guiWindowIsMovable(win) == false, 'window')
        assert(guiLabelSetColor(lab, 1, 2, 3) == true, 'label color')
        local cr, cg, cb = guiLabelGetColor(lab)
        assert(cr == 1 and cg == 2 and cb == 3, 'label color read')
        guiLabelSetColor(lab, 255, 255, 255)
        local panel = guiCreateTabPanel(0, 0, 100, 80, false)
        local tab = guiCreateTab('One', panel)
        assert(guiGetSelectedTab(panel).id == tab.id, 'first tab')
        local tab2 = guiCreateTab('Two', panel)
        assert(guiSetSelectedTab(panel, tab2) == true and guiGetSelectedTab(panel).id == tab2.id, 'select tab')
        assert(guiDeleteTab(tab, panel) == true and isElement(tab) == false, 'delete tab')
        assert(guiSetProperty(btn, 'AlwaysOnTop', 'True') == true and guiGetProperty(btn, 'AlwaysOnTop') == 'True', 'property')
        assert(guiMoveToBack(btn) == true, 'move back')
        local image = guiCreateStaticImage(0, 0, 10, 10, 'face.png', false)
        assert(isElement(image) == true and guiStaticImageGetNativeSize(image) == false, 'static image')
        assert(guiSetFont(lab, 'default-bold-small') == true and guiGetFont(lab) == 'default-bold-small', 'font')
        assert(guiGetCursorType() == 'arrow', 'cursor')
        assert(guiCreateFont('../x.ttf') == false and guiCreateFont('face.ttf', 12) == false, 'font file')
        local grid = guiCreateGridList(0, 0, 200, 100, false)
        assert(getElementType(grid) == 'gui-gridlist', 'grid type')
        assert(guiGridListAddColumn(grid, 'Name', 0.5) == 1 and guiGridListAddColumn(grid, 'Score', 0.3) == 2, 'columns')
        assert(guiGridListGetColumnCount(grid) == 2 and guiGridListGetColumnTitle(grid, 1) == 'Name', 'column title')
        assert(guiGridListGetColumnWidth(grid, 1, true) == 0.5 and guiGridListGetColumnWidth(grid, 1, false) == 100, 'column width')
        assert(guiGridListSetColumnWidth(grid, 1, 40, false) == true and guiGridListGetColumnWidth(grid, 1, false) == 40, 'absolute width')
        local row = guiGridListAddRow(grid, nil, 'B')
        assert(row == 0 and guiGridListGetItemText(grid, 0, 1) == 'B' and guiGridListGetItemText(grid, 9, 1) == '', 'skipped value')
        assert(guiGridListAddRow(grid, 'Sam', 3) == 1 and guiGridListGetItemText(grid, 1, 2) == '3', 'row text')
        assert(guiGridListSetItemData(grid, 1, 1, 'id') == true and guiGridListGetItemData(grid, 1, 1) == 'id', 'item data')
        assert(guiGridListGetItemData(grid, 0, 1) == nil, 'empty data')
        assert(guiGridListSetItemColor(grid, 1, 1, 1, 2, 3, 4) == true, 'item color')
        local cr, cg, cb, ca = guiGridListGetItemColor(grid, 1, 1)
        assert(cr == 1 and cg == 2 and cb == 3 and ca == 4, 'item color read')
        local sr, sc = guiGridListGetSelectedItem(grid)
        assert(sr == -1 and sc == 0 and guiGridListGetSelectionMode(grid) == 0, 'no selection')
        assert(guiGridListSetSelectedItem(grid, 1, 2) == true, 'select')
        sr, sc = guiGridListGetSelectedItem(grid)
        assert(sr == 1 and sc == 1 and guiGridListGetSelectedCount(grid) == 2, 'selected row')
        local picked = guiGridListGetSelectedItems(grid)
        assert(picked[1].row == 1 and picked[1].column == 0 and picked[2].column == 1, 'selected table')
        assert(guiGridListInsertRowAfter(grid, -1, 'Top', 1) == 0 and guiGridListGetItemText(grid, 0, 1) == 'Top', 'insert')
        assert(guiGridListGetItemText(grid, 2, 1) == 'Sam' and guiGridListGetRowCount(grid) == 3, 'shifted row')
        assert(guiGridListSetSelectionMode(grid, 2) == true and guiGridListGetSelectionMode(grid) == 2, 'cell mode')
        assert(guiGridListSetSelectedItem(grid, 2, 2, true) == true, 'select cell')
        sr, sc = guiGridListGetSelectedItem(grid)
        assert(sr == 2 and sc == 2 and guiGridListGetSelectedCount(grid) == 1, 'one cell')
        assert(guiGridListSetSelectionMode(grid, 1) == true, 'multi row')
        assert(guiGridListSetSelectedItem(grid, 0, 1, false) == true and guiGridListGetSelectedCount(grid) == 3, 'extra selection')
        assert(guiGridListSetSelectionMode(grid, 0) == true and guiGridListSetSelectedItem(grid, 1, 1, false) == true, 'single mode')
        assert(guiGridListGetSelectedCount(grid) == 2, 'single replaces')
        assert(guiGridListIsSortingEnabled(grid) == true and guiGridListSetSortingEnabled(grid, false) == true, 'sort flag')
        assert(guiGridListIsSortingEnabled(grid) == false and guiGridListGetItemText(grid, 0, 1) == 'Top', 'sort keeps order')
        assert(guiGridListSetVerticalScrollPosition(grid, 150) == true and guiGridListGetVerticalScrollPosition(grid) == 100, 'grid scroll')
        assert(guiGridListSetScrollBars(grid, true, false) == true and guiGridListAutoSizeColumn(grid, 1) == true, 'bars and autosize')
        assert(guiGridListRemoveRow(grid, 0) == true and guiGridListGetItemText(grid, 0, 1) == 'B', 'remove row')
        assert(guiGridListRemoveColumn(grid, 2) == true and guiGridListGetColumnCount(grid) == 1, 'remove column')
        assert(guiGridListSetItemText(grid, 9, 1, 'no', false, false) == true, 'missing row')
        assert(guiGridListRemoveRow(grid, 50) == true and guiGridListGetRowCount(grid) == 2, 'missing remove')
        assert(guiGridListClear(grid) == true and guiGridListGetRowCount(grid) == 0, 'clear')
    )LUA");

    std::string frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "R 10.0 20.0 300.0 200.0 "));
    CHECK(Has(frame, "Hello Window"));
    CHECK(Has(frame, "Y 15.0 60.0 115.0 80.0 4294967295 0 0 0 Child label"));
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
    CHECK(Has(frame, "2147483647 0 0 0 Child label"));  // white text at alpha 0.5 (127 of 255)
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
    CHECK(Has(frame, "X 0.000 0.000 10.000 10.000 2.0000 2.0000 "));
    CHECK(Has(frame, "X 0.000 0.000 10.000 10.000 1.0000 1.0000 "));

    // destroyElement removes the element and its children; the element is no longer an element.
    RunLua(vm, "gui-destroy", "destroyElement(win)\nassert(not isElement(win) and not isElement(lab), 'children destroyed')");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(!Has(frame, "Hello Window"));
    CHECK(!Has(frame, "Child label"));
    CHECK(Has(frame, "R 960.0 540.0 480.0 108.0 "));
    RunLua(vm, "gui-cleanup", "destroyElement(btn)\nassert(not isElement(btn), 'button destroyed')");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(!Has(frame, "R 960.0 540.0"));

    // A tap on a gui-edit focuses it, the frame asks for the soft keyboard ("K"), and
    // typed text stays in the edit (onClientCharacter does not also see it).
    RunLua(vm, "gui-edit", R"LUA(
        edit = guiCreateEdit(10, 10, 200, 40, "", false)
        assert(getElementType(edit) == "gui-edit" and guiGetText(edit) == "", "edit")
        assert(guiSetInputMode("no_binds") == true and guiGetInputMode() == "no_binds", "no_binds")
        assert(guiGetInputEnabled() == true, "capture on")
        assert(guiSetInputMode("nope") == false, "bad mode")
        assert(guiSetInputMode("no_binds_when_editing") == true, "restore mode")
        assert(guiGetInputEnabled() == false, "capture off without a focused edit")
        _G.changed, _G.accepted, _G.focused, _G.blurred = 0, 0, 0, 0
        _G.chars, _G.keys = "", ""
        addEventHandler("onClientGUIChanged", root, function() _G.changed = _G.changed + 1 end)
        addEventHandler("onClientGUIAccepted", root, function() _G.accepted = _G.accepted + 1 end)
        addEventHandler("onClientGUIFocus", root, function() _G.focused = _G.focused + 1 end)
        addEventHandler("onClientGUIBlur", root, function() _G.blurred = _G.blurred + 1 end)
        addEventHandler("onClientCharacter", root, function(c) _G.chars = _G.chars .. c end)
        addEventHandler("onClientKey", root, function(button, press)
            if press then _G.keys = _G.keys .. button .. "," end
        end)
        showCursor(true)
    )LUA");
    WorldRPG::Script::ActiveVmQueueCursor(0, 20.f, 25.f);
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "\nK\n"));
    CHECK(Has(frame, "|"));
    RunLua(vm, "gui-edit-focus", R"LUA(
        assert(_G.focused == 1 and guiGetInputEnabled() == true, "tap focused the edit")
        assert(guiFocus(edit) == true, "guiFocus")
    )LUA");
    WorldRPG::Script::ActiveVmQueueScriptInput("ab", 0, "");
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "gui-edit-type", R"LUA(
        assert(guiGetText(edit) == "ab" and _G.changed == 1, "typed into the edit")
        assert(_G.chars == "", "a focused edit does not emit onClientCharacter")
    )LUA");
    WorldRPG::Script::ActiveVmQueueScriptInput("", 1, "enter");
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "gui-edit-enter", R"LUA(
        assert(guiGetText(edit) == "a" and _G.accepted == 1, "backspace and enter")
        assert(guiEditSetMasked(edit, true) == true and guiEditIsMasked(edit) == true, "masked")
    )LUA");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "**|") == false);
    CHECK(Has(frame, "*|"));

    // DX edits (the login boxes) are not widgets. guiSetInputEnabled(true) opens the
    // keyboard and the keystrokes are onClientCharacter / onClientKey.
    RunLua(vm, "dx-capture", "guiBlur(edit)\nguiSetInputEnabled(true)");
    WorldRPG::Script::ActiveVmQueueScriptInput("Z", 0, "");
    WorldRPG::Script::ActiveVmQueueScriptInput("", 1, "");
    WorldRPG::Script::ActiveVmQueueScriptInput("", 0, "enter");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "\nK\n"));
    RunLua(vm, "dx-keys", R"LUA(
        assert(_G.chars == "Z", "dx character: " .. _G.chars)
        assert(_G.keys == "backspace,enter,", "dx keys: " .. _G.keys)
        assert(_G.blurred == 1, "blur")
        guiSetInputEnabled(false)
        destroyElement(edit)
    )LUA");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(!Has(frame, "\nK\n"));
}

// Client window state calls (desktop CLuaGUIDefs.cpp:642-700, 3631-3748, 4066-4086).
void CheckCoreClientCalls(WorldRPG::Script::LuaVMStub& vm) {
    // CLuaClientDefs ClearDebug / IsMTAWindowFocused, CLuaUtilDefs::DebugSleep,
    // CLuaFunctionDefs::SetWindowFlashing and CreateTrayNotification.
    RunLua(vm, "core-client-unfocused", R"LUA(
        assert(clearDebugBox() == true and clearDebugBox("extra") == true, "clear debug")
        assert(isMTAWindowFocused() == false, "host window is not focused yet")
        assert(isTrayNotificationEnabled() == true, "tray cvar default")
        assert(setWindowFlashing() == false and setWindowFlashing(1) == false, "flash needs a bool")
        assert(setWindowFlashing(true, -1) == false and setWindowFlashing(true, "no") == false, "flash count")
        assert(setWindowFlashing(true, 0/0) == false, "flash nan")
        assert(setWindowFlashing(true) == true and setWindowFlashing(false, 3) == true, "flash while unfocused")
        assert(setWindowFlashing(true, "4", "extra") == true, "flash ignores a spare argument")
        assert(createTrayNotification() == false and createTrayNotification("hi", "nope") == false, "tray arguments")
        assert(createTrayNotification("hi", "default", "x") == false, "tray sound")
        assert(createTrayNotification(12) == false and createTrayNotification("hi", "INFO", false) == false, "tray icon missing")
        assert(setDevelopmentMode(false) == true and debugSleep(0) == false, "sleep needs dev mode")
        assert(debugSleep() == false and debugSleep(true) == false and debugSleep("zz") == false, "sleep arguments")
        assert(debugSleep(-1) == false and debugSleep(0/0) == false, "sleep range")
        assert(setDevelopmentMode(true) == true and debugSleep(0) == true and debugSleep("0") == true, "sleep in dev mode")
        focusHits = {}
        assert(addEventHandler("onClientMTAFocusChange", root, function(state)
            focusHits[#focusHits + 1] = state
        end) == true, "focus handler")
    )LUA");
    WorldRPG::Script::MtaWindowSetFocused(true);
    RunLua(vm, "core-client-focus-pending",
           "assert(isMTAWindowFocused() == false and setWindowFlashing(true) == false, 'live focus blocks the flash')");
    vm.pumpDx();
    RunLua(vm, "core-client-focus-on",
           "assert(isMTAWindowFocused() == true and #focusHits == 1 and focusHits[1] == true, 'focus gained')");
    WorldRPG::Script::MtaWindowSetFocused(true);
    vm.pumpDx();
    RunLua(vm, "core-client-focus-same",
           "assert(#focusHits == 1, 'same focus does not fire again')");
    WorldRPG::Script::MtaWindowSetFocused(false);
    vm.pumpDx();
    RunLua(vm, "core-client-focus-off",
           "assert(isMTAWindowFocused() == false and #focusHits == 2 and focusHits[2] == false, 'focus lost')");
    WorldRPG::Script::MtaWindowSetFocused(true);
    WorldRPG::Script::MtaWindowSetFocused(false);
    vm.pumpDx();
    RunLua(vm, "core-client-focus-coalesced",
           "assert(#focusHits == 2 and setWindowFlashing(true) == true, 'cancelled focus change')");
}

void CheckMtaWindow(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "mta-window-state", R"LUA(
        assert(isChatBoxInputActive() == false and isConsoleActive() == false, 'chat input and console closed')
        assert(isMainMenuActive() == false and isTransferBoxActive() == false, 'menu and transfer box closed')
        assert(isMTAWindowActive() == false, 'no MTA window open')
        assert(isDebugViewActive() == false, 'debug view starts hidden')
        assert(setDebugViewActive() == false and setDebugViewActive(1) == false, 'debug view needs a boolean')
        assert(setDebugViewActive(true) == true and isDebugViewActive() == true, 'debug view on')
        assert(setDebugViewActive(false) == true and isDebugViewActive() == false, 'debug view off')
    )LUA");
    WorldRPG::Script::MtaWindowSetChatInputActive(true);
    RunLua(vm, "mta-window-chat-open",
           "assert(isChatBoxInputActive() == true and isMTAWindowActive() == true, 'chat input open')");
    WorldRPG::Script::MtaWindowSetChatInputActive(false);
    RunLua(vm, "mta-window-chat-closed",
           "assert(isChatBoxInputActive() == false and isMTAWindowActive() == false, 'chat input closed')");
    RunLua(vm, "chatbox-character-limit", R"LUA(
        assert(getChatboxCharacterLimit() == 96, 'default limit')
        assert(setChatboxCharacterLimit(120) == true and getChatboxCharacterLimit() == 120, 'limit set')
        assert(setChatboxCharacterLimit('64') == true and getChatboxCharacterLimit() == 64, 'numeric string')
        assert(setChatboxCharacterLimit(0) == true and getChatboxCharacterLimit() == 0, 'limit 0')
        assert(setChatboxCharacterLimit(255) == true and getChatboxCharacterLimit() == 255, 'limit max')
        local ok, err = pcall(setChatboxCharacterLimit, 256)
        assert(not ok and err:find('less than, or equal to 255 %(got: 256%)'), 'over max: ' .. tostring(err))
        ok, err = pcall(setChatboxCharacterLimit, -2)
        assert(not ok and err:find('greater than, or equal to 0 %(got: %-2%)'), 'negative: ' .. tostring(err))
        assert(not pcall(setChatboxCharacterLimit) and not pcall(setChatboxCharacterLimit, 'abc'), 'bad argument')
        assert(getChatboxCharacterLimit() == 255, 'rejected values keep the limit')
        assert(setChatboxCharacterLimit(-1) == true and getChatboxCharacterLimit() == 96, '-1 resets')
    )LUA");
    RunLua(vm, "chatbox-layout", R"LUA(
        local all = getChatboxLayout()
        local count = 0
        for _ in pairs(all) do count = count + 1 end
        assert(count == 21, 'layout holds 21 values, got ' .. count)
        assert(all.chat_lines == 10 and all.chat_font == 2 and all.chat_width == 1.5, 'numbers')
        assert(all.chat_use_cegui == false and all.chat_text_outline == 0 and all.chat_css_style_text == 0, 'only cegui is boolean')
        local c = all.chat_text_color
        assert(#c == 4 and c[1] == 172 and c[2] == 213 and c[3] == 254 and c[4] == 255, 'text colour')
        assert(#all.chat_scale == 2 and all.chat_scale[1] == 1 and all.chat_scale[2] == 1, 'scale')
        assert(getChatboxLayout('chat_lines') == 10 and getChatboxLayout('CHAT_LINES') == 10, 'one value, any case')
        assert(#getChatboxLayout('chat_input_color') == 4, 'one colour')
        assert(getChatboxLayout('text_scale') == 1 and getChatboxLayout('nope') == false, 'unknown name')
        assert(getChatboxLayout({}) == false and getChatboxLayout(nil) == false, 'bad argument')
    )LUA");
    // Occupancy (CLuaPedDefs.cpp:834-880, 1214-1238; CLuaVehicleDefs.cpp:692-777). The host has no
    // vehicle element, so only the on-foot and bad-argument results are checked here.
    {
        // A player or a vehicle that leaves takes its seats with it, so a reused id starts on foot.
        uint32_t vehicle = 0;
        uint8_t seat = 0;
        WorldRPG::RemoteVehicle::SetOccupant(50, 60, 1);
        WorldRPG::RemoteVehicle::SetOccupant(51, 60, 2);
        WorldRPG::RemoteVehicle::ForgetElement(50);
        CHECK(!WorldRPG::RemoteVehicle::FindOccupant(50, vehicle, seat));
        CHECK(WorldRPG::RemoteVehicle::FindOccupant(51, vehicle, seat) && vehicle == 60 && seat == 2);
        WorldRPG::RemoteVehicle::ForgetElement(60);
        CHECK(!WorldRPG::RemoteVehicle::FindOccupant(51, vehicle, seat));
    }
    RunLua(vm, "occupancy-on-foot", R"LUA(
        local me = {id = 777, element = true}
        assert(getPedOccupiedVehicle(me) == false, 'local player on foot')
        assert(getPedOccupiedVehicleSeat(me) == false, 'no seat on foot')
        assert(isPedInVehicle(me) == false, 'not in a vehicle')
        assert(isPedInVehicle() == nil and isPedInVehicle({}) == nil, 'bad ped is nil')
        assert(getPedOccupiedVehicle() == false and getPedOccupiedVehicleSeat(5) == false, 'bad ped is false')
        assert(getVehicleOccupant(me) == false and getVehicleOccupants() == false, 'not a vehicle')
        assert(getVehicleController({}) == false, 'not a vehicle')
    )LUA");
    WorldRPG::ServerWorld::NoteVehicle(880, 411);
    WorldRPG::ServerWorld::NoteVehicle(881, 520);
    WorldRPG::ServerWorld::NoteVehicle(882, 596);
    WorldRPG::ServerWorld::NoteVehicle(883, 420);
    WorldRPG::ServerWorld::NoteVehicle(884, 513);
    WorldRPG::ServerWorld::NoteVehicle(885, 537);
    WorldRPG::ServerWorld::NoteVehicle(886, 571);
    WorldRPG::ServerWorld::NoteVehicle(887, 487);
    WorldRPG::ServerWorld::NoteVehicle(888, 406);
    WorldRPG::ServerWorld::NoteVehicle(889, 432);
    WorldRPG::ServerWorld::NoteVehicle(890, 400);
    RunLua(vm, "vehicle-element-state", R"LUA(
        local me = {id = 777, element = true}
        local car = {id = 880, element = true}
        local hydra = {id = 881, element = true}
        local heli = {id = 887, element = true}
        local police = {id = 882, element = true}
        local taxi = {id = 883, element = true}
        local stunt = {id = 884, element = true}
        assert(isVehicleLocked() == nil and isVehicleLocked(me) == nil, "locked needs a vehicle")
        assert(setVehicleLocked(me, true) == false, "a player is not locked")
        assert(isVehicleLocked(car) == false and setVehicleLocked(car, true) == true, "lock stores")
        assert(isVehicleLocked(car) == true and getVehicleDoorLockMode(car) == 2, "lock mode follows the doors")
        assert(setVehicleDoorLockMode(car, 1) == true and isVehicleLocked(car) == false, "unlock")
        assert(setVehicleDoorLockMode(car, 0) == false and setVehicleDoorLockMode(car, 7) == false, "lock mode range")
        assert(getVehicleEngineState(car) == false and setVehicleEngineState(car, true) == true, "engine starts off")
        assert(getVehicleEngineState(car) == true, "engine stored")
        assert(getVehicleOverrideLights(car) == 0 and setVehicleOverrideLights(car, 2) == true, "lights")
        assert(getVehicleOverrideLights(car) == 2 and setVehicleOverrideLights(car, 3) == false, "lights stop at 2")
        assert(getVehiclePaintjob(car) == 3 and setVehiclePaintjob(car, 3) == false, "paintjob 3 is already set")
        assert(setVehiclePaintjob(car, 0) == true and getVehiclePaintjob(car) == 0, "paintjob stored")
        assert(setVehiclePaintjob(car, 4) == false, "paintjob 4 is rejected")
        assert(getVehiclePlateText(car) == false and setVehiclePlateText(car, "ABCD") == true, "empty plate")
        assert(getVehiclePlateText(car) == "ABCD" and setVehiclePlateText(car, "ABCD") == false, "same plate")
        assert(setVehiclePlateText(car, "123456789") == true and getVehiclePlateText(car) == "12345678", "plate is 8")
        assert(isVehicleDamageProof(car) == false and setVehicleDamageProof(car, true) == true, "damage proof")
        assert(isVehicleDamageProof(car) == true, "damage proof stored")
        assert(isVehicleFuelTankExplodable(car) == true and setVehicleFuelTankExplodable(car, false) == true, "tank")
        assert(isVehicleFuelTankExplodable(car) == false, "tank stored")
        assert(isVehicleFrozen(car) == false and setVehicleFrozen(car, true) == true and isVehicleFrozen(car) == true, "frozen")
        assert(getVehicleTyresCanBurst(car) == true and setVehicleTyresCanBurst(car, false) == true, "tyres")
        assert(getVehicleTyresCanBurst(car) == false, "tyres stored")
        assert(setVehicleDirtLevel(car, 5) == true and setVehicleDirtLevel(me, 1) == false, "dirt")
        assert(getVehicleLandingGearDown(hydra) == true, "hydra gear starts down")
        assert(setVehicleLandingGearDown(hydra, true) == false and setVehicleLandingGearDown(hydra, false) == true, "gear changes")
        assert(getVehicleLandingGearDown(hydra) == false and getVehicleLandingGearDown(car) == nil, "no gear on a car")
        assert(getVehicleLandingGearDown(stunt) == nil and setVehicleLandingGearDown(stunt, false) == true, "stunt setter only")
        assert(getVehicleSirensOn(police) == false and setVehicleSirensOn(police, false) == false, "sirens already off")
        assert(setVehicleSirensOn(police, true) == true and getVehicleSirensOn(police) == true, "sirens stored")
        assert(getVehicleSirensOn(car) == nil and setVehicleSirensOn(car, true) == false, "infernus has no sirens")
        assert(isVehicleTaxiLightOn(taxi) == false and isVehicleTaxiLightOn(car) == false, "taxi light starts off")
        assert(setVehicleTaxiLightOn(car, true) == false and setVehicleTaxiLightOn(taxi, true) == true, "only a taxi")
        assert(isVehicleTaxiLightOn(taxi) == true, "taxi light stored")
        assert(getVehicleDoorState(car, 0) == 0 and setVehicleDoorState(car, 0, 4) == true, "door")
        assert(getVehicleDoorState(car, 0) == 4 and getVehicleDoorState(car, 9) == 0, "door range")
        assert(setVehicleDoorState(stunt, 0, 1) == true, "stunt doors can be written")
        local kart = {id = 886, element = true}
        assert(setVehicleDoorState(kart, 0, 1) == false and getVehicleDoorState(kart, 2) == 4, "kart doors start missing")
        assert(getVehicleWheelStates(car) == 0, "wheels intact")
        assert(setVehicleWheelStates(car, 1) == true, "burst one wheel")
        local a, b, c, d = getVehicleWheelStates(car)
        assert(a == 1 and b == 0 and c == 0 and d == 0, "front left burst")
        assert(setVehicleLightState(car, 0, 1) == true and getVehicleLightState(car, 0) == 1, "light")
        assert(setVehicleLightState(car, 4, 1) == false, "light range")
        assert(setVehiclePanelState(car, 0, 2) == true and getVehiclePanelState(car, 0) == 2, "panel")
        assert(fixVehicle(car) == true and getVehicleDoorState(car, 0) == 0 and getVehicleLightState(car, 0) == 0, "fix")
        assert(isVehicleBlown(car) == false and blowVehicle(car) == true and isVehicleBlown(car) == true, "blown")
        assert(isVehicleOnGround(car) == false and isVehicleOnAllWheels(car) == false, "ground")
        assert(isVehicleWheelOnGround(car, "front_left") == true, "no body reports collided")
        assert(getVehicleWheelFrictionState(car, "rear_right") == 0, "friction")
        assert(getVehicleCurrentGear(car) == 0, "gear")
        assert(setVehicleTurnVelocity(car, 1, 2, 3) == true, "turn")
        local tx, ty, tz = getVehicleTurnVelocity(car)
        assert(tx == 1 and ty == 2 and tz == 3, "turn stored")
        local gx, gy, gz = getVehicleGravity(car)
        assert(gx == 0 and gy == 0 and gz == -1, "gravity default")
        assert(setVehicleGravity(car, 0, 0, -0.5) == true, "gravity set")
        gx, gy, gz = getVehicleGravity(car)
        assert(gz == -0.5, "gravity stored")
        local hr, hg, hb = getVehicleHeadLightColor(car)
        assert(hr == 255 and hg == 255 and hb == 255, "headlight default")
        assert(setVehicleHeadLightColor(car, 1, 2, 3) == true, "headlight set")
        hr, hg, hb = getVehicleHeadLightColor(car)
        assert(hr == 1 and hg == 2 and hb == 3, "headlight stored")
        assert(getHelicopterRotorSpeed(car) == false and getHelicopterRotorSpeed(heli) == 0, "rotor")
        assert(areVehicleLightsOn(car) == true, "override 2 forces lights")
        local train = {id = 885, element = true}
        assert(isTrainDerailed(car) == false and isTrainDerailed(train) == false, "train derailed")
        assert(isTrainDerailable(train) == true and getTrainDirection(train) == false, "train defaults")
        assert(getTrainSpeed(train) == 0 and getTrainPosition(train) == -1, "train motion")
        assert(setTrainDerailed(train, true) == true and getTrainPosition(train) == false, "derailed hides position")
        assert(setTrainPosition(train, 10) == false, "cannot position a derailed train")
        assert(setTrainDerailed(train, false) == true and setTrainPosition(train, 12) == true, "position")
        assert(getTrainPosition(train) == 12 and setTrainSpeed(train, 4) == true and getTrainSpeed(train) == 4, "speed")
        assert(setTrainDirection(car, true) == false, "a car is not a train")
        local c1, c2, c3, c4 = getVehicleColor(car)
        assert(c1 == 0 and c2 == 0 and c3 == 0 and c4 == 0, "palette default")
        assert(setVehicleColor(car, 10, 20, 30, 40) == true, "palette set")
        c1, c2, c3, c4 = getVehicleColor(car)
        assert(c1 == 10 and c4 == 40, "palette stored")
        assert(setVehicleColor(car, 1, 2) == false, "color arity")
        local v1, v2 = getVehicleVariant(car)
        assert(v1 == 255 and v2 == 255, "variant default")
        assert(setVehicleVariant(car, 1, 2) == true, "variant set")
        v1, v2 = getVehicleVariant(car)
        assert(v1 == 1 and v2 == 2, "variant stored")
        assert(setVehicleVariant(car) == false, "random variant is not hooked")
        assert(getVehicleWheelScale(car) == 1 and setVehicleWheelScale(car, 1.5) == true, "wheel scale")
        assert(getVehicleWheelScale(car) == 1.5, "wheel scale stored")
        assert(isVehicleSmokeTrailEnabled(car) == false, "smoke starts off")
        ok, err = pcall(setVehicleSmokeTrailEnabled, car, true)
        assert(ok == false and err:find("Invalid model ID", 1, true), "smoke model")
        assert(isVehicleNitroRecharging(car) == false and getVehicleNitroLevel(car) == false, "no nitro fitted")
        assert(setVehicleNitroLevel(car, 0.5) == false, "nitro level needs the upgrade")
        local dumper = {id = 888, element = true}
        local rhino = {id = 889, element = true}
        local trailer = {id = 890, element = true}
        assert(getVehicleAdjustableProperty(car) == false and getVehicleAdjustableProperty(dumper) == 0, "adjustable")
        assert(setVehicleAdjustableProperty(dumper, 5) == true and getVehicleAdjustableProperty(dumper) == 5, "adjustable stored")
        local th, tv = getVehicleTurretPosition(rhino)
        assert(th == 0 and tv == 0 and setVehicleTurretPosition(rhino, 0.2, 0.3) == true, "turret")
        th, tv = getVehicleTurretPosition(rhino)
        assert(math.abs(th - 0.2) < 0.001 and math.abs(tv - 0.3) < 0.001, "turret stored")
        assert(getVehicleDoorOpenRatio(car, 0) == 0 and setVehicleDoorOpenRatio(car, 0, 0.5) == true, "door ratio")
        assert(getVehicleDoorOpenRatio(car, 0) == 0.5 and getVehicleDoorOpenRatio(car, 6) == false, "door ratio range")
        assert(getHeliBladeCollisionsEnabled(heli) == true and setHeliBladeCollisionsEnabled(heli, false) == true, "blades")
        assert(getHeliBladeCollisionsEnabled(heli) == false, "blades stored")
        assert(setHelicopterRotorSpeed(heli, 0.25) == true and getVehicleRotorSpeed(heli) == 0.25, "heli rotor")
        assert(getVehicleRotorSpeed(car) == false and setVehicleRotorSpeed(hydra, 0.5) == true, "plane rotor")
        assert(getVehicleRotorSpeed(hydra) == 0.5, "plane rotor stored")
        assert(isVehicleWindowOpen(car, 0) == false and setVehicleWindowOpen(car, 0, true) == true, "window")
        assert(isVehicleWindowOpen(car, 0) == true and setVehicleWindowOpen(car, 7, true) == false, "window range")
        assert(isVehicleParkingBrakeEnabled(car) == false and setVehicleParkingBrake(car, true) == true, "brake")
        assert(isVehicleParkingBrakeEnabled(car) == true, "brake stored")
        assert(setVehicleDoorsUndamageable(car, true) == true, "doors undamageable")
        assert(setVehicleLoadCollisionFlag(car, true) == true, "load collision")
        assert(setVehiclePhysicalProofs(car, true, false, false, false, false) == true, "proofs")
        assert(setVehicleWheelsRotation(car, 1) == false, "wheel rotation needs the body")
        assert(getVehicleStraightLineDistance(car) == false and setVehicleStraightLineDistance(car, 10) == false, "distance")
        assert(attachTrailerToVehicle(car, trailer) == true and getVehicleTowedByVehicle(car).id == 890, "tow")
        assert(getVehicleTowingVehicle(trailer).id == 880 and attachTrailerToVehicle(car, trailer) == false, "already towed")
        assert(detachTrailerFromVehicle(car) == true and getVehicleTowedByVehicle(car) == false, "detach")
        assert(getVehicleUpgradeOnSlot(car, 10) == 0 and addVehicleUpgrade(car, 1086) == true, "radio")
        assert(getVehicleUpgradeOnSlot(car, 10) == 1086 and getVehicleUpgrades(car)[1] == 1086, "radio stored")
        assert(addVehicleUpgrade(train, 1086) == false and removeVehicleUpgrade(car, 1086) == true, "radio rules")
        assert(type(getVehicleComponents(car)) == "table" and getVehicleComponentPosition(car, "bonnet") == false, "components")
        assert(setVehicleComponentPosition(car, "bonnet", 0, 0, 0) == false, "component not on the clump")
        assert(createVehicle(411, 0, 0, 0) == false and requestVehicleRecording(1) == false, "no resource")
        assert(isVehicleRecordingLoaded(1) == false and isVehiclePlaybackActive(car) == false, "recording")
        assert(startVehiclePlayback(car, 1) == false and stopVehiclePlayback(car) == false, "playback")
        assert(addVehicleSirens(car, 1, 1) == true, "sirens added")
        local params = getVehicleSirenParams(car)
        assert(params.SirenCount == 1 and params.SirenType == 1 and params.Flags.UseRandomiser == true, "siren params")
        assert(setVehicleSirens(car, 1, 0, 1, 2, 3, 4, 5, 6) == true, "siren point")
        assert(getVehicleSirens(car)[1].x == 0 and getVehicleSirens(car)[1].Red == 3, "siren point stored")
        assert(addVehicleSirens(car, 0, 1) == false and removeVehicleSirens(car) == true, "siren range")
        assert(getVehicleSirenParams(car).SirenCount == 0, "sirens removed")
        assert(getVehicleRotorState(heli) == true and setVehicleRotorState(heli, false) == true, "rotor state")
        assert(getVehicleRotorState(heli) == false and setVehicleRotorState(car, false) == false, "car rotor")
        assert(getOriginalHandling(411) == false and getVehicleHandling(car) == false, "handling data")
        assert(setVehicleHandling(car, "mass", 1000) == false, "handling write")
        assert(getVehicleAudioSettings(car) == false and getVehicleModelAudioSettings(411) == false, "audio data")
        assert(getVehicleModelDummyPosition(411, "exhaust") == false, "dummy")
        assert(getVehicleModelDummyDefaultPosition(411, "exhaust") == false, "default dummy")
        assert(getVehicleDummyPosition(car, "exhaust") == false and setVehicleDummyPosition(car, "exhaust", 0, 0, 0) == false, "vehicle dummy")
        assert(resetVehicleDummyPositions(car) == false, "reset dummies")
        assert(getVehicleModelExhaustFumesPosition(411) == false and setVehicleModelExhaustFumesPosition(411, 0, 0, 0) == false, "exhaust")
        assert(getVehicleEntryPoints(car) == false and getVehicleCompatibleUpgrades(car) == false, "entry and upgrades")
        assert(getVehicleNativeAutoPilotDiagnostic(car) == false, "autopilot")
        assert(spawnVehicleFlyingComponent(car, 1) == false, "flying component")
        ok, err = pcall(getVehicleModelWheelSize, 411)
        assert(ok == false and err:find("Invalid model ID", 1, true), "wheel size")
        ok, err = pcall(setVehicleModelWheelSize, 411, "front_axle", 0)
        assert(ok == false and err:find("Invalid wheel size", 1, true), "wheel size zero")
        ok, err = pcall(spawnVehicleFlyingComponent, car, 0)
        assert(ok == false and err:find("Invalid component index", 1, true), "component index")
        ok, err = pcall(setVehicleModelAudioSetting, 411, "nope", 1)
        assert(ok == false and err:find("vehicle-audio-setting", 1, true), "audio property")
        local ok, err = pcall(getVehicleDoorLockMode)
        assert(ok == false and err:find("vehicle", 1, true), "door lock type")
        ok, err = pcall(setVehicleDoorLockMode, car, "nope")
        assert(ok == false and err:find("number", 1, true), "door lock number")
        ok, err = pcall(setVehicleTyresCanBurst, car)
        assert(ok == false and err:find("boolean", 1, true), "tyres bool")
    )LUA");
    // Key binds (CLuaFunctionDefs.Input.cpp 148-276, 420-655; CScriptKeyBinds.cpp).
    RunLua(vm, "bind-key", R"LUA(
        _G.keyLog = {}
        _G.onE = function(key, state, extra) keyLog[#keyLog + 1] = key .. ':' .. state .. ':' .. tostring(extra) end
        _G.onBoth = function(key, state) keyLog[#keyLog + 1] = 'both:' .. key .. ':' .. state end
        assert(bindKey() == false and bindKey('e') == false and bindKey('e', 'down') == false, 'bad arguments')
        assert(bindKey('nokey', 'down', onE) == false, 'unknown key')
        assert(bindKey('e', 'sideways', onE) == false, 'unknown state')
        assert(bindKey('E', 'down', onE, 'x') == true, 'key name is case-insensitive')
        assert(bindKey('F2', 'both', onBoth) == true, 'both states')
        assert(bindKey('fire', 'down', onBoth) == true, 'a control name binds')
        assert(#getFunctionsBoundToKey('e') == 1 and getFunctionsBoundToKey('e')[1] == onE, 'functions of e')
        assert(#getFunctionsBoundToKey('E') == 0, 'getter uses the table spelling')
        assert(#getFunctionsBoundToKey('F2') == 2 and #getFunctionsBoundToKey('F2', 'up') == 1, 'state filter')
        assert(getKeyBoundToFunction(onE) == 'e' and getKeyBoundToFunction(print) == false, 'key of function')
        assert(getKeyBoundToFunction('x') == false, 'needs a function')
        assert(getKeyState('e') == false and getKeyState('nokey') == false, 'nothing pressed')
        assert(bindKey('p', 'down', 'panel', 'open') == true and bindKey('p', 'down', 'panel', 'open') == true, 'command bind')
        assert(getCommandsBoundToKey('p').panel == 'open' and getKeyBoundToCommand('panel') == 'p', 'command getters')
        assert(getKeyBoundToCommand('nothing') == false, 'unbound command')
        addCommandHandler('panel', function(name, args) keyLog[#keyLog + 1] = 'cmd:' .. name .. ':' .. tostring(args) end)
        addEventHandler('onClientKey', root, function(key, press)
            _G.keySource = source
            if key == 'F3' then cancelEvent() end
        end)
        local cmd = function(name, a, b, c) _G.lastCmd = name .. '|' .. tostring(a) .. '|' .. tostring(b) .. '|' .. tostring(c) end
        assert(addCommandHandler('Greet', cmd, false) == true, 'command added')
        assert(addCommandHandler('greet', cmd, false) == false, 'same function is not added twice')
        addEventHandler('onClientConsole', root, function(text) _G.consoleText = text; _G.consoleSource = source end)
        assert(bindKey('F3', 'down', onBoth) == true, 'F3 bound')
        -- A bind removed by an earlier bind of the same key press is not called.
        _G.second = function() keyLog[#keyLog + 1] = 'second' end
        assert(bindKey('F4', 'down', function() unbindKey('F4', 'down', second) end) == true, 'F4 first')
        assert(bindKey('F4', 'down', second) == true, 'F4 second')
    )LUA");
    // The on-screen key list shows key binds only (not the "fire" control), in table order.
    CHECK(WorldRPG::Script::KeyBindsBoundKeys() == "e|p|F2|F3|F4");
    WorldRPG::Script::KeyBindsQueueKey("E", true);
    WorldRPG::Script::KeyBindsQueueKey("f2", true);
    WorldRPG::Script::KeyBindsQueueKey("f2", false);
    WorldRPG::Script::KeyBindsQueueKey("p", true);
    WorldRPG::Script::KeyBindsQueueKey("F3", true);
    WorldRPG::Script::KeyBindsQueueKey("F8", true);
    WorldRPG::Script::KeyBindsQueueKey("F4", true);
    // A chat command reaches the client handlers, one argument per word, then onClientConsole.
    WorldRPG::Script::ActiveVmQueueCommand("greet  one two");
    WorldRPG::Script::ActiveVmPump();
    {
        // The key-bound command is handed back for the server, as a typed command is sent.
        const std::vector<std::string> toServer = WorldRPG::Script::ActiveVmTakeServerCommands();
        CHECK(toServer.size() == 1 && toServer[0] == "panel open");
        CHECK(WorldRPG::Script::ActiveVmTakeServerCommands().empty());
    }
    RunLua(vm, "chat-command", R"LUA(
        assert(lastCmd == 'Greet|one|two|nil', 'arguments are split on spaces: ' .. tostring(lastCmd))
        assert(consoleText == 'greet  one two', 'onClientConsole text: ' .. tostring(consoleText))
        assert(consoleSource and consoleSource.id == 777, 'onClientConsole source is the local player')
        assert(keySource and keySource.id == root.id, 'onClientKey source is root')
    )LUA");
    WorldRPG::Script::ActiveVmQueueCommand("login secret");
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "chat-command-login", "assert(consoleText == 'login ***', 'login is censored: ' .. tostring(consoleText))");
    RunLua(vm, "bind-key-fired", R"LUA(
        local got = table.concat(keyLog, ' ')
        assert(got == 'e:down:x both:F2:down both:F2:up cmd:panel:open', 'binds fired: ' .. got)
        assert(getKeyState('e') == true and getKeyState('F2') == false, 'key state')
        assert(unbindKey('e', 'up') == false and unbindKey('e', 'down', print) == false, 'no such bind')
        assert(unbindKey('e', 'down', onE) == true and #getFunctionsBoundToKey('e') == 0, 'unbind function')
        assert(unbindKey('F2') == true and #getFunctionsBoundToKey('F2') == 0, 'unbind every state')
        assert(unbindKey('F2') == false and unbindKey('nokey') == false, 'nothing left')
        assert(unbindKey('p', 'down', 'panel') == true and getKeyBoundToCommand('panel') == false, 'unbind command')
        _G.keyLog = {}
    )LUA");
    WorldRPG::Script::KeyBindsQueueKey("e", false);
    WorldRPG::Script::KeyBindsQueueKey("f2", true);
    WorldRPG::Script::ActiveVmPump();
    CHECK(WorldRPG::Script::KeyBindsBoundKeys() == "F3|F4");
    RunLua(vm, "bind-key-removed", "assert(#keyLog == 0 and getKeyState('e') == false, 'removed binds do not fire')");
    RunLua(vm, "chatbox-limit-set", "assert(setChatboxCharacterLimit(40) == true)");
    WorldRPG::Script::MtaWindowResetSession();
    RunLua(vm, "chatbox-limit-session", "assert(getChatboxCharacterLimit() == 96, 'a new session resets the limit')");
}

// 2D drawing calls (desktop CLuaDrawingDefs.cpp 159-187, 443-509, 706-753, 800-923, 2044-2109).
void CheckDxDrawing(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "dx-draw-arguments", R"LUA(
        assert(dxDrawLine() == false and dxDrawLine(1, 2, 3) == false, 'line needs four numbers')
        assert(dxDrawLine(1, 2, 3, 4, 5, 'x') == false and dxDrawLine(1, 2, 3, 4, 5, 2, 1) == false, 'line bad width or postGUI')
        assert(dxDrawCircle(10, 10, 0) == false and dxDrawCircle(10, 10, 5, 90, 90) == false, 'empty circle')
        assert(dxDrawCircle(10, 10, 5, 0, 360, 1, 1, 2) == false, 'too few segments')
        assert(dxDrawCircle(10, 10, 5, 0, 360, 1, 1, 32, 0) == false, 'ratio 0')
        assert(dxDrawPrimitive('trianglelist', false, {0, 0}, {1, 1}) == false, 'two vertices are not a triangle')
        assert(dxDrawPrimitive('linelist', false, {0, 0}, {1, 1}, {2, 2}) == false, 'odd line list')
        assert(not pcall(dxDrawPrimitive, 'nope', false, {0, 0}) and not pcall(dxDrawPrimitive, 'pointlist'), 'primitive errors')
        assert(not pcall(dxDrawPrimitive, 'pointlist', false, {1}), 'vertex needs 2 or 3 numbers')
        assert(dxGetBlendMode() == 'blend' and dxSetBlendMode('nope') == false, 'blend mode default')
        assert(dxSetTestMode('no_mem') == true and dxSetTestMode('x') == false and dxSetTestMode() == true, 'test mode')
        local on, ratio = dxIsAspectRatioAdjustmentEnabled()
        assert(on == false and math.abs(ratio - 4 / 3) < 0.001, 'aspect ratio adjustment is off')
        assert(dxSetAspectRatioAdjustmentEnabled(true) == false, 'only inside onClientHUDRender')
        -- Host font: 7 px per byte at the 15 px default size.
        assert(dxGetTextWidth('abcd') == 28 and dxGetTextWidth('abcd', 2) == 56, 'text width')
        assert(dxGetTextWidth('ab\nabcdef') == 42, 'the longest line')
        assert(dxGetTextWidth('#FF0000ab', 1, 'default', true) == 14 and dxGetTextWidth('#FF0000ab') == 63, 'colour codes')
        assert(dxGetTextWidth() == false, 'text width needs a text')
        local w, h = dxGetTextSize('abcd')
        assert(w == 28 and h == 15, 'one line: ' .. w .. ' ' .. h)
        w, h = dxGetTextSize('ab\nabcdef', 0, 2)
        assert(w == 84 and h == 60, 'two lines at scale 2: ' .. w .. ' ' .. h)
        w, h = dxGetTextSize('aa bb cc', 40, 1, 1, 'default', true)
        assert(w == 35 and h == 30, 'word break at 40 px: ' .. w .. ' ' .. h)
        w, h = dxGetTextSize('aa bb cc', 40, 1, 1, 'default', false)
        assert(w == 56 and h == 15, 'no word break: ' .. w .. ' ' .. h)
        w, h = dxGetTextSize('ab', 0, 1, 3)
        assert(w == 14 and h == 45, 'separate scales: ' .. w .. ' ' .. h)
        w, h = dxGetTextSize('')
        assert(w == 0 and h == 0, 'empty text')
        -- Plain pixels: B, G, R, A per pixel, then width and height as 16-bit values.
        local plain = string.rep(string.char(1, 2, 3, 4), 6) .. string.char(3, 0, 2, 0)
        local pw, ph = dxGetPixelsSize(plain)
        assert(pw == 3 and ph == 2, 'plain pixels are 3 x 2')
        local png = string.char(0x89) .. 'PNG\r\n' .. string.char(0x1a) .. '\n' .. string.char(0, 0, 0, 13) .. 'IHDR'
            .. string.char(0, 0, 1, 0x2c, 0, 0, 0, 120, 8, 6, 0, 0, 0) .. string.rep('x', 8) .. string.char(0xAE, 0x42, 0x60, 0x82)
        pw, ph = dxGetPixelsSize(png)
        assert(dxGetPixelsFormat(png) == 'png' and pw == 300 and ph == 120, 'PNG size from the header')
        assert(dxGetPixelsSize('junk') == false and dxGetPixelsSize() == false, 'not pixels')
        assert(dxConvertPixels(plain, 'plain') == plain and dxConvertPixels(png, 'png') == png, 'same format is a copy')
        assert(dxConvertPixels('junk', 'png') == false and dxConvertPixels(plain, 'nope') == false, 'bad pixels or format')
        -- The host has no image codec, so a real conversion fails the way a codec error does.
        assert(dxConvertPixels(plain, 'png') == false and dxConvertPixels(plain, 'dds') == false, 'no codec on the host')
        assert(dxGetTexturePixels(localPlayer) == false and dxSetTexturePixels(localPlayer, plain) == false, 'needs a texture')
        _G.dxTest = function()
            dxDrawLine(1, 2, 30, 40, tocolor(255, 0, 0, 255), 3)
            dxSetBlendMode('add')
            dxDrawPrimitive('trianglelist', false, {0, 0, 4278190335}, {10, 0}, {0, 10, 4294901760})
            dxDrawCircle(100, 100, 10, 0, 360, 4294967295, 4278190080, 4)
            -- Left selected on purpose: the frame must return to the screen by itself.
            _G.leftRt = _G.leftRt or dxCreateRenderTarget(8, 8)
            dxSetRenderTarget(_G.leftRt)
        end
        addEventHandler('onClientRender', root, dxTest)
    )LUA");
    std::string frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "L 1.0 2.0 30.0 40.0 4294901760 3.00\n"));
    CHECK(Has(frame, "B 1\n"));
    CHECK(Has(frame, " 8 8 0 0\nG\n"));
    CHECK(Has(frame, "P 4 3 0.0 0.0 4278190335 10.0 0.0 4294967295 0.0 10.0 4294901760\n"));
    // Centre, then segments + 1 points: right, down, left, up, right again.
    CHECK(Has(frame, "P 6 6 100.0 100.0 4278190080 110.0 100.0 4294967295 100.0 110.0 4294967295 90.0 100.0 4294967295"));
    // The mode set in one frame is still active at the start of the next (desktop m_ActiveBlendMode).
    RunLua(vm, "dx-draw-stop", "removeEventHandler('onClientRender', root, dxTest); assert(dxGetBlendMode() == 'add')");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "B 1\n") && !Has(frame, "L 1.0"));
    RunLua(vm, "dx-blend-reset", "assert(dxSetBlendMode() == true and dxGetBlendMode() == 'blend')");
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(!Has(frame, "B 1\n"));
}

// Vector graphics (desktop CLuaVectorGraphicDefs.cpp). The host has no SVG renderer, so the
// element, its document, size and callback are checked, not the picture.
void CheckSvg(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "svg", R"LUA(
        local doc = '<?xml version="1.0"?>\n<!-- a comment -->\n<svg width="10" height="10"><rect x="1" y="1" width="8" height="8" fill="red"/></svg>'
        _G.svgCalls = 0
        _G.svg = svgCreate(64, 32, doc, function(element) _G.svgCalls = _G.svgCalls + 1; _G.svgSeen = element end)
        assert(svg and getElementType(svg) == 'svg' and isElement(svg), 'svg element')
        assert(_G.svgCalls == 0, 'the callback does not run inside svgCreate')
        local w, h = svgGetSize(svg)
        assert(w == 64 and h == 32, 'svg size')
        local mw, mh = dxGetMaterialSize(svg)
        assert(mw == 64 and mh == 32, 'an svg is a material')
        local node = svgGetDocumentXML(svg)
        assert(node and xmlNodeGetName(node) == 'svg' and xmlNodeGetAttribute(node, 'width') == '10', 'document root')
        assert(xmlNodeGetName(xmlNodeGetChildren(node, 0)) == 'rect', 'document child')
        assert(svgCreate(8, 8, '<svg><broken') == false, 'bad XML does not load')
        assert(svgCreate(8, 8, 'radar/none.svg') == false, 'missing file does not load')
        local blank = svgCreate(8, 8)
        assert(blank and svgGetDocumentXML(blank) == false, 'an svg without a document')
        assert(not pcall(svgCreate, 0, 8) and not pcall(svgCreate, 5000, 8) and not pcall(svgCreate), 'size errors')
        assert(not pcall(svgCreate, 0/0, 8) and not pcall(svgCreate, 0.5, 8), 'finite SVG sizes cannot truncate to zero')
        assert(not pcall(svgSetSize, svg, math.huge, 8) and not pcall(svgSetSize, svg, 0/0, 8), 'invalid resize preserves old size')
        local before = xmlCreateFile('svg-probe-before.xml','probe')
        local temporary = svgCreate(8,8,'<svg><g/></svg>')
        assert(temporary and destroyElement(temporary))
        local after = xmlCreateFile('svg-probe-after.xml','probe')
        for id = before + 1, after - 1 do assert(xmlNodeGetName(id) == false, 'SVG validation must not retain XML handles') end
        assert(xmlUnloadFile(before) and xmlUnloadFile(after))
        assert(not pcall(svgGetSize, localPlayer) and not pcall(svgSetSize, svg, 0, 0), 'argument errors')
        assert(svgSetSize(svg, 128, 96) == true, 'resize')
        w, h = svgGetSize(svg)
        assert(w == 128 and h == 96, 'new size')
        xmlNodeSetAttribute(node, 'width', '20')
        assert(svgSetDocumentXML(svg, node) == true, 'set document')
        assert(xmlNodeGetAttribute(svgGetDocumentXML(svg), 'width') == '20', 'document was replaced')
        assert(not pcall(svgSetDocumentXML, svg, 'text'), 'document must be an xml node')
    )LUA");
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "svg-callback", R"LUA(
        assert(_G.svgCalls == 1 and _G.svgSeen and _G.svgSeen.id == svg.id, 'one callback for the changes of one frame')
        assert(svgSetUpdateCallback(svg, false) == true and svgSetUpdateCallback(svg, true) == false, 'callback removed')
        assert(not pcall(svgSetUpdateCallback, svg, 5), 'callback must be a function or false')
        assert(svgSetSize(svg, 16, 16) == true, 'resize without a callback')
    )LUA");
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "svg-destroy", R"LUA(
        assert(_G.svgCalls == 1, 'a removed callback is not called')
        assert(destroyElement(svg) == true and not isElement(svg), 'svg destroyed')
        assert(not pcall(svgGetSize, svg), 'destroyed svg is not an svg')
    )LUA");
    // The overlay is told to free the bitmap of the destroyed svg.
    CHECK(Has(WorldRPG::Script::ActiveVmPump(), "D "));
}

void CheckStopLifecycle(WorldRPG::Script::LuaVMStub& vm) {
    const std::string fileName = "lifecycle_asset.ogg";
    for (const std::string name : {"demo", "other"}) {
        const std::filesystem::path path = WorldRPG::Net::HttpDownloader::CachedFilePath(name, fileName);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << name;
    }
    // Resource "demo" (net id 5, root 0x1234) and "other" (net id 6, root 0x2222). Both start
    // from one no-cache script each (packet 95).
    WorldRPG::Net::ResourceStartInfo demo;
    demo.name = "demo";
    demo.resourceId = 5;
    demo.entityId = 0x1234;
    demo.dynamicEntityId = 0x1235;
    demo.noClientCacheScripts = 1;
    demo.startCounter = 3;
    demo.exportedFunctions = {"ping", "missing"};
    WorldRPG::Net::ResourceStartInfo other = demo;
    other.name = "other";
    other.resourceId = 6;
    other.entityId = 0x2222;
    other.dynamicEntityId = 0x2223;
    other.startCounter = 1;

    WorldRPG::Script::ActiveVmOnResourceStart(demo);
    WorldRPG::Script::ActiveVmOnResourceStart(other);

    const std::string demoScript = R"LUA(
        function ping(n)
            return n + 1, nil, getResourceName(getThisResource()), getResourceName(sourceResource),
                fileGetContents("lifecycle_asset.ogg"), sourceResourceRoot.id
        end
        function hidden() return true end
        _G.demoStarts = (_G.demoStarts or 0) + 1
        addEventHandler("onClientResourceStart", root, function(started)
            _G.demoStarted = true
            if getResourceName(started) == "demo" then
                _G.demoStartArg = started
                _G.demoStartSource = source
            end
        end)
        addEventHandler("onClientRender", root, function()
            _G.demoRenders = (_G.demoRenders or 0) + 1
            _G.demoSelf = getThisResource()
        end)
        addEventHandler("onClientResourceStop", root, function(res)
            _G.demoStopped = res
            _G.demoStopSource = source
            _G.demoStopState = getResourceState(getThisResource())
        end)
        local created = createElement("coin")
        _G.demoCreated = created
        _G.demoParent = getElementParent(created)
        showCursor(true)
        local res = getThisResource()
        _G.demoRes = res
        _G.demoResName = getResourceName(res)
        _G.demoResRoot = getResourceRootElement(res)
        _G.demoDyn = getResourceDynamicElementRoot(res)
        local maps = 0
        for _, child in ipairs(getElementChildren(_G.demoResRoot)) do
            if getElementType(child) == "map" then maps = maps + 1 end
        end
        _G.demoMaps = maps
        _G.demoGui = getResourceGUIElement()
        _G.demoChild = guiCreateWindow(0, 0, 40, 40, "child", false, _G.demoGui)
        _G.demoExports = getResourceExportedFunctions(res)
        _G.demoState = getResourceState(res)
        assert(addEvent("onMine") == true, "resource event")
        addCommandHandler("democmd", function(name, args) _G.demoCmd = (getThisResource() == _G.demoRes) and args end)
        assert(bindKey("F5", "down", "queryLifecycle"), "demo query bind")
        assert(fileGetContents("lifecycle_asset.ogg") == "demo", "relative path uses current resource")
        assert(fileGetContents(":other/lifecycle_asset.ogg") == "other", "explicit path uses named resource")
        local demoText = createNativeUI("text", {name = "demoText", content = "Demo"})
        assert(demoText and showNativeText(demoText, "objective", 600000))
        _G.demoSound = playSound("lifecycle_asset.ogg")
        assert(isElement(_G.demoSound), "resource-relative audio path")
        addEventHandler("onMine", root, function() _G.demoSource = sourceResource == getThisResource() end)
        triggerEvent("onMine", root)
        setTimer(function() _G.demoTicks = (_G.demoTicks or 0) + 1 end, 1, 0)
        _G.demoWin = guiCreateWindow(0, 0, 50, 50, "demo window", false)
    )LUA";
    const std::string otherScript = R"LUA(
        assert(bindKey("F6", "up", "queryLifecycle"), "other query bind")
        local demo = getResourceFromName("demo")
        local n, empty, owner, caller, file, callerRoot = call(demo, "ping", 41)
        assert(n == 42 and empty == nil and owner == "demo" and caller == "other")
        assert(file == "demo" and callerRoot == 0x2222 and getResourceName(getThisResource()) == "other")
        assert(call(demo, "hidden") == false and call(demo, "missing") == false)
        _G.exportChecksRan = true
        _G.otherSound = playSound("http://example/other.ogg")
        local otherText = createNativeUI("text", {name = "otherText", content = "Other"})
        assert(otherText)
        addCommandHandler("otherui", function()
            _G.otherUiShown, _G.otherUiError = showNativeText(otherText, "objective", 600000)
        end)
        addEventHandler("onClientRender", root, function() _G.otherRenders = (_G.otherRenders or 0) + 1 end)
    )LUA";
    const std::vector<uint8_t> demoBody = NoCacheScriptBody(5, demoScript);
    const std::vector<uint8_t> otherBody = NoCacheScriptBody(6, otherScript);
    CHECK(WorldRPG::Script::ActiveVmFeedScripts(demoBody.data(), demoBody.size(), demoBody.size() * 8));
    CHECK(WorldRPG::Script::ActiveVmFeedScripts(otherBody.data(), otherBody.size(), otherBody.size() * 8));

    WorldRPG::Script::ActiveVmPulseResources();
    RunLua(vm, "after-start", R"LUA(
        assert(_G.demoStarted == true and _G.demoStarts == 1, "demo started once")
        assert(_G.exportChecksRan, "resource exports execute in the target environment")
        assert(_G.demoStartArg == _G.demoRes, "start passes the resource")
        assert(_G.demoStartSource.id == 0x1234, "start source is the root")
        assert(_G.demoResName == "demo" and _G.demoState == "running", "this resource")
        assert(_G.demoRes == getResourceFromName("demo"), "same resource handle")
        assert(isElement(_G.demoResRoot) and _G.demoResRoot.id == 0x1234, "resource root")
        assert(getElementType(_G.demoResRoot) == "resource", "resource type")
        assert(isElement(_G.demoDyn) and _G.demoDyn.id == 0x1235 and getElementType(_G.demoDyn) == "map", "dynamic root")
        assert(_G.demoParent.id == _G.demoDyn.id and _G.demoMaps == 1, "one dynamic map")
        assert(isElement(_G.demoGui) and getElementType(_G.demoGui) == "guiroot", "gui root")
        assert(isElement(_G.demoChild), "window parented to gui root")
        assert(_G.demoSource == true, "sourceResource")
        assert(_G.demoExports[1] == "ping", "exports")
        assert(getResourceName(getResourceFromName("other")) == "other", "resource by name")
        assert(getResourceFromName("missing") == false, "unknown resource")
        local names = {}
        for _, res in ipairs(getResources()) do names[getResourceName(res)] = true end
        assert(names.demo and names.other, "resource list")
        assert(getThisResource() == false, "core code has no resource")
        local keys = getBoundKeys("queryLifecycle")
        assert(keys.F5 == "down" and keys.F6 == "up", "query sees both resources")
        assert(isElement(_G.demoSound) and isElement(_G.otherSound), "resource sounds are alive")
    )LUA");

    WorldRPG::Net::ResourceStartInfo again = demo;
    again.resourceId = 7;
    again.entityId = 0x1236;
    again.dynamicEntityId = 0x1237;
    again.noClientCacheScripts = 1;
    again.exportedFunctions = {"pong"};
    WorldRPG::Script::ActiveVmOnResourceStart(again);
    const std::vector<uint8_t> againBody = NoCacheScriptBody(7, "showCursor(true)\n");
    CHECK(WorldRPG::Script::ActiveVmFeedScripts(againBody.data(), againBody.size(), againBody.size() * 8));
    WorldRPG::Script::ActiveVmPulseResources();

    WorldRPG::Script::ActiveVmPump();
    WorldRPG::Script::ActiveVmPump();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "before-stop",
           "assert(_G.demoRenders == 3 and _G.otherRenders == 3, 'both resources render')\n"
           "assert(_G.demoTicks and _G.demoTicks >= 1, 'demo timer fired')\n"
           "assert(_G.demoSelf == _G.demoRes, 'handler stays on the old resource')\n"
           "assert(getResourceFromName('demo') ~= _G.demoRes, 'name finds the new start')\n"
           "assert(getResourceExportedFunctions(getResourceFromName('demo'))[1] == 'pong', 'new exports')\n"
           "assert(getResourceState(_G.demoRes) == 'running', 'old one still running')\n"
           "assert(getResourceRootElement(_G.demoRes).id == 0x1234, 'old root')\n"
           "_G.snapTicks, _G.snapRenders = _G.demoTicks, _G.demoRenders");

    // A command handler runs as the resource that registered it, and leaves with it.
    CHECK(WorldRPG::Script::ActiveVmRunCommand("democmd", "hello"));
    RunLua(vm, "command-owner", "assert(_G.demoCmd == 'hello', 'command ran as its resource: ' .. tostring(_G.demoCmd))");

    CHECK(WorldRPG::Script::ActiveVmRunCommand("otherui", ""));
    RunLua(vm, "native-ui-owned", "assert(otherUiShown == false and otherUiError == 'channel-busy')");
    // RESOURCE_STOP for net id 5: CResourceStopPacket writes the id as a uint16.
    WorldRPG::Net::BitStreamWriter stop;
    stop.Write(static_cast<uint16_t>(5));
    WorldRPG::Script::ActiveVmOnResourceStop(stop.GetData(), stop.GetByteLength(), stop.GetBitLength());
    CHECK(WorldRPG::Script::ActiveVmRunCommand("otherui", ""));
    RunLua(vm, "native-ui-released", "assert(otherUiShown == true)");
    RunLua(vm, "stop-event",
           "assert(_G.demoStopped == _G.demoRes, 'stop passes the resource')\n"
           "assert(_G.demoStopSource.id == 0x1234, 'stop source is the root')\n"
           "assert(_G.demoStopState == 'stopping', 'stop state')\n"
           "assert(getResourceFromName('demo') ~= _G.demoRes, 'old handle is not the current demo')\n"
           "assert(not isElement(_G.demoResRoot), 'old root is gone')\n"
           "assert(not isElement(_G.demoCreated), 'client elements are destroyed')\n"
           "assert(not isElement(_G.demoGui) and not isElement(_G.demoChild), 'old gui is gone')\n"
           "assert(isCursorShowing() == true, 'the other start still holds the cursor')");
    RunLua(vm, "stop-gui", "assert(not isElement(_G.demoWin), 'stopped resource GUI elements are destroyed')");
    RunLua(vm, "stop-bound-keys", R"LUA(
        local keys = getBoundKeys("queryLifecycle")
        assert(keys.F5 == nil and keys.F6 == "up", "query drops only the stopped resource's binds")
        assert(not isElement(_G.demoSound) and isElement(_G.otherSound), "only stopped resource audio is released")
        assert(call(_G.demoRes, "ping", 1) == false, "stopped exports are unavailable")
    )LUA");

    // The stopped resource's handlers and timers are gone; the other resource still runs.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    WorldRPG::Script::ActiveVmPump();
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "after-stop",
           "assert(_G.demoRenders == _G.snapRenders, 'demo render handler dropped')\n"
           "assert(_G.demoTicks == _G.snapTicks, 'demo timer dropped')\n"
           "assert(_G.otherRenders == 5, 'other resource keeps rendering')");
    CHECK(!WorldRPG::Script::ActiveVmRunCommand("democmd", "again"));
    // CEvents::RemoveAllEvents(CLuaMain*): the stopped resource's event is gone, a core one stays.
    CHECK(!WorldRPG::Script::Events::Exists("onMine"));
    CHECK(WorldRPG::Script::Events::Exists("onTest"));
    CHECK(WorldRPG::Script::Events::RemoteState("onMine") == WorldRPG::Script::Events::Remote::NotAdded);
    CHECK(WorldRPG::Script::Events::RemoteState("onTest") == WorldRPG::Script::Events::Remote::NotRemote);
    CHECK(WorldRPG::Script::Events::RemoteState("onRemoteTest") == WorldRPG::Script::Events::Remote::Allowed);
    CHECK(WorldRPG::Script::Events::RemoteState("onClientRender") == WorldRPG::Script::Events::Remote::NotRemote);

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
            -- dxCreateTexture reads the size from the file; dxDrawImage accepts the element.
            _G.tex = dxCreateTexture("radar/tex.png", "argb", true, "clamp")
            _G.texType = _G.tex and getElementType(_G.tex)
            _G.texW, _G.texH = dxGetMaterialSize(_G.tex)
            _G.texDraw = dxDrawImage(20, 21, 22, 23, _G.tex)
            _G.texEdge = dxSetTextureEdge(_G.tex, "border", 255) and dxSetTextureEdge(_G.tex, "nope") == false
            _G.texMissing = dxCreateTexture("radar/none.png")
            _G.texBad = dxCreateTexture("radar/notimage.png")
            _G.texBlank = dxCreateTexture(64, 64)
            _G.texNoSize = dxGetMaterialSize(localPlayer)
            -- dxCreateFont: size 10 is 17 px high (floor(10 * 1.75)); scale multiplies it.
            _G.font = dxCreateFont("radar/font.ttf", 10, true)
            _G.fontType = _G.font and getElementType(_G.font)
            _G.fontH, _G.fontH2 = dxGetFontHeight(1, _G.font), dxGetFontHeight(2, _G.font)
            _G.fontW = dxGetTextWidth("abcd", 1, _G.font)
            _G.fontBad = dxCreateFont("radar/notimage.png")
            _G.fontMissing = dxCreateFont("radar/none.ttf")
            dxDrawText("styled", 0, 0, 50, 20, tocolor(255, 255, 255, 255), 2, _G.font)
            _G.fontLayerLabel = guiCreateLabel(0,100,100,20,'font-layer-gui',false)
            assert(dxDrawText('styled-post',0,0,50,20,4294967295,2,_G.font,'left','top',false,false,true))
            _G.matDraw = dxDrawMaterialPrimitive("trianglelist", _G.tex, false, {0, 0, 0, 0}, {10, 0, 4294901760, 1, 0}, {0, 10, 0, 1})
            _G.matShort = dxDrawMaterialPrimitive("trianglelist", "radar/tex.png", false, {0, 0, 0, 0})
            _G.matError = not pcall(dxDrawMaterialPrimitive, "trianglelist", localPlayer, false, {0, 0, 0, 0})
            -- Render target: a texture the next draw lines go into, then drawn like an image.
            _G.rt = dxCreateRenderTarget(64, 32, true)
            _G.rtType = _G.rt and getElementType(_G.rt)
            _G.rtW, _G.rtH = dxGetMaterialSize(_G.rt)
            _G.rtSet = dxSetRenderTarget(_G.rt, true)
            dxDrawRectangle(1, 1, 2, 2, tocolor(255, 255, 255, 255))
            _G.rtBack = dxSetRenderTarget()
            _G.rtDraw = dxDrawImage(40, 41, 64, 32, _G.rt)
            _G.rtBad = dxSetRenderTarget(_G.tex) == false and dxCreateRenderTarget(0, 10) == false
                and dxCreateRenderTarget(5000, 10) == false and dxCreateRenderTarget(8, 8, "argb") == false
            local plain = string.rep(string.char(1, 2, 3, 4), 6) .. string.char(3, 0, 2, 0)
            _G.pixBadSize = dxSetTexturePixels(_G.tex, plain) == false and dxSetTexturePixels(_G.tex, plain, 0, 0, 3, 3) == false
                and dxSetTexturePixels(_G.tex, plain, 299, 0, 3, 2) == false and dxSetTexturePixels(_G.tex, 'junk') == false
            _G.pixNoCodec = dxGetTexturePixels(_G.tex) == false and dxCreateTexture(plain) == nil
            -- An svg is drawn like any other texture.
            _G.svg2 = svgCreate(16, 16, '<svg width="4" height="4"/>')
            _G.svgDrew = dxDrawImage(50, 51, 16, 16, _G.svg2)
            -- Screen source: a texture; the copy of the game picture needs the phone.
            _G.screen = dxCreateScreenSource(320, 180)
            _G.screenOk = getElementType(_G.screen) == 'texture' and dxUpdateScreenSource(_G.screen) == true
                and dxUpdateScreenSource(_G.screen, true) == true and dxUpdateScreenSource(_G.tex) == false
                and dxCreateScreenSource(0, 1) == false and dxUpdateScreenSource() == false
            _G.screenDraw = dxDrawImage(60, 61, 320, 180, _G.screen)
            local status = dxGetStatus()
            _G.statusOk = status.TestMode == "none" and status.VideoCardName == "Android" and status.TotalPhysicalMemory > 0
                and status.SettingWindowed == false and status.SettingFOV == 70
            _G.pedRotation = getPedRotation(localPlayer)
            _G.velX, _G.velY, _G.velZ = getElementVelocity(localPlayer)
            _G.pedArmor = getPedArmor(localPlayer)
        end)
    )LUA";
    {
        // A PNG header for a 300 x 120 image, and a file that is not an image.
        ::mkdir("/tmp/neon-dx-cache", 0755);
        ::mkdir("/tmp/neon-dx-cache/radarres", 0755);
        ::mkdir("/tmp/neon-dx-cache/radarres/radar", 0755);
        const unsigned char png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                     0, 0, 1, 0x2c, 0, 0, 0, 120, 8, 6, 0, 0, 0};
        if (std::FILE* out = std::fopen("/tmp/neon-dx-cache/radarres/radar/tex.png", "wb")) {
            std::fwrite(png, 1, sizeof(png), out);
            std::fclose(out);
        }
        if (std::FILE* out = std::fopen("/tmp/neon-dx-cache/radarres/radar/font.ttf", "wb")) {
            const unsigned char ttf[] = {0, 1, 0, 0, 0, 0};
            std::fwrite(ttf, 1, sizeof(ttf), out);
            std::fclose(out);
        }
        if (std::FILE* out = std::fopen("/tmp/neon-dx-cache/radarres/radar/notimage.png", "wb")) {
            std::fputs("not an image", out);
            std::fclose(out);
        }
    }
    const std::vector<uint8_t> body = NoCacheScriptBody(9, script);
    CHECK(WorldRPG::Script::ActiveVmFeedScripts(body.data(), body.size(), body.size() * 8));
    WorldRPG::Script::ActiveVmPulseResources();
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(Has(frame, "R 8.0 9.0 10.0 11.0 "));
    CHECK(Has(frame, "keep"));
    CHECK(Has(frame, "I 1.0 2.0 3.0 4.0 0.0 0.0 -1.0 -1.0 4294967295 0.0 0.0 0.0 /tmp/neon-dx-cache/radarres/radar/radar00.png"));
    CHECK(Has(frame, "I 5.0 6.0 7.0 8.0 9.0 10.0 11.0 12.0 4294901760 90.0 1.0 2.0 /tmp/neon-dx-cache/radarres/radar/radar01.png"));
    CHECK(!Has(frame, "secret"));
    {
        // "G id 64 32 1 1", the rectangle, "G", then the target drawn as image "rt:<id>".
        const size_t select = frame.find(" 64 32 1 1\nR 1.0 1.0 2.0 2.0 4294967295\nG\n");
        CHECK(select != std::string::npos && frame.rfind("G ", select) != std::string::npos);
        CHECK(Has(frame, "I 40.0 41.0 64.0 32.0 0.0 0.0 -1.0 -1.0 4294967295 0.0 0.0 0.0 rt:"));
    }
    CHECK(Has(frame, "I 50.0 51.0 16.0 16.0 0.0 0.0 -1.0 -1.0 4294967295 0.0 0.0 0.0 tx:"));
    lua_State* fontState = WorldRPG::Script::ActiveVmState();
    lua_getglobal(fontState,"font"); lua_getfield(fontState,-1,"id");
    const auto fontId = static_cast<uint32_t>(lua_tointeger(fontState,-1)); lua_pop(fontState,2);
    const std::string fontSelection = "F 17.0 1 @font:" + std::to_string(fontId) + ":/tmp/neon-dx-cache/radarres/radar/font.ttf\n";
    CHECK(Has(frame, (fontSelection + "X 0.000 0.000 50.000 20.000 2.0000 2.0000 4294967295 0 0 0 0 0 0 0.000 25.000 10.000 0.000 styled\nF\n").c_str()));
    const size_t fontGui = frame.find("font-layer-gui");
    const size_t fontPost = frame.find(fontSelection + "X 0.000 0.000 50.000 20.000 2.0000 2.0000 4294967295 0 0 0 0 0 0 0.000 25.000 10.000 0.000 styled-post\nF\n");
    CHECK(fontGui != std::string::npos && fontPost != std::string::npos && fontGui < fontPost);
    CHECK(Has(frame, "M 4 3 0.0 0.0 4294967295 0.0000 0.0000 10.0 0.0 4294901760 1.0000 0.0000 0.0 10.0 4294967295 0.0000 1.0000 "
                     "/tmp/neon-dx-cache/radarres/radar/tex.png\n"));
    CHECK(Has(frame, "I 20.0 21.0 22.0 23.0 0.0 0.0 -1.0 -1.0 4294967295 0.0 0.0 0.0 /tmp/neon-dx-cache/radarres/radar/tex.png"));
    RunLua(vm, "dx-image-resource", R"LUA(
        assert(destroyElement(fontLayerLabel))
        assert(_G.fullImage == true and _G.sectionImage == true, 'resource image calls draw')
        assert(_G.escapedImage == false, 'parent path is rejected')
        assert(_G.texType == 'texture' and isElement(_G.tex), 'texture element: ' .. tostring(_G.texType))
        assert(_G.texW == 300 and _G.texH == 120, 'texture size from the PNG header')
        assert(_G.texDraw == true and _G.texEdge == true, 'texture draws and takes an edge')
        assert(_G.texMissing == false and _G.texBad == nil and _G.texBlank == nil, 'missing, bad and blank textures')
        assert(_G.texNoSize == false, 'material size needs a material')
        assert(_G.fontType == 'dx-font' and _G.fontH == 17 and _G.fontH2 == 34, 'font height: ' .. tostring(_G.fontH))
        assert(math.abs(_G.fontW - 4 * 7 * 17 / 15) < 0.01, 'font width on the host: ' .. tostring(_G.fontW))
        assert(_G.fontBad == false and _G.fontMissing == false, 'bad and missing fonts')
        assert(dxGetFontHeight() == 15 and dxGetFontHeight(2) == 30 and dxGetFontHeight('x') == false, 'default font height')
        assert(_G.matDraw == true and _G.matShort == false and _G.matError == true, 'material primitive')
        assert(_G.statusOk == true, 'dxGetStatus')
        assert(_G.screenOk == true and _G.screenDraw == true, 'screen source')
        assert(_G.svg2 and _G.svgDrew == true, 'an svg draws as an image')
        assert(_G.pixBadSize == true and _G.pixNoCodec == true, 'texture pixel calls: sizes checked, no codec on the host')
        assert(_G.rtType == 'texture' and _G.rtW == 64 and _G.rtH == 32, 'render target is a 64 x 32 texture')
        assert(_G.rtSet == true and _G.rtBack == true and _G.rtDraw == true and _G.rtBad == true, 'render target calls')
        _G.rtId = _G.rt.id
        assert(destroyElement(_G.rt) == true and dxSetRenderTarget(_G.rt) == false, 'destroyed render target')
        assert(destroyElement(_G.font) == true and dxGetFontHeight(1, _G.font) == 15, 'destroyed font is the default font')
        assert(destroyElement(_G.tex) == true and not isElement(_G.tex), 'texture destroyed')
        assert(dxGetMaterialSize(_G.tex) == false, 'destroyed texture has no size')
    )LUA");
    // The overlay is told to free the destroyed target's bitmap at the start of the next frame.
    frame = WorldRPG::Script::ActiveVmPump();
    CHECK(frame.rfind("D ", 0) == 0 || Has(frame, "\nD "));
    RunLua(vm, "dx-image-resource-done", R"LUA(
        assert(_G.rtId ~= nil, 'render target id kept for the check above')
        assert(type(_G.pedRotation) == 'number' and _G.pedRotation >= 0 and _G.pedRotation < 360, 'heading in degrees')
        assert(_G.velX == 0 and _G.velY == 0 and _G.velZ == 0, 'local velocity')
        assert(_G.pedArmor == 0, 'armor sample')
    )LUA");
}

void CheckGuiFonts(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG::Script;
    // CheckDxImage created this synthetic sfnt-header fixture. The host cannot
    // decode glyphs; Android validates the file through its real Typeface loader.
    WorldRPG::Net::ResourceStartInfo producer;
    producer.name = "guifonts";
    producer.resourceId = 75;
    producer.entityId = 0x7500;
    producer.dynamicEntityId = 0x7501;
    producer.noClientCacheScripts = 1;
    producer.startCounter = 1;
    ActiveVmOnResourceStart(producer);
    const std::string script = R"LUA(
        _G.guiFont = guiCreateFont(':radarres/radar/font.ttf',12)
        _G.guiFont2 = guiCreateFont(':radarres/radar/font.ttf',12)
        _G.guiFontDefault = guiCreateFont(':radarres/radar/font.ttf')
        _G.guiFontMin = guiCreateFont(':radarres/radar/font.ttf',0)
        _G.guiFontMax = guiCreateFont(':radarres/radar/font.ttf',-1)
        _G.guiFontMissing = guiCreateFont(':radarres/radar/missing.ttf')
        _G.guiFontBad = guiCreateFont(':radarres/radar/notimage.png')
        _G.guiFontEscape = guiCreateFont(':radarres/../secret.ttf')
        _G.guiFontLabel = guiCreateLabel(20,30,120,90,'#ff0000\né🙂\\n',false)
        _G.guiFontEdit = guiCreateEdit(20,130,120,30,'font-edit',false)
        _G.guiFontPanel = guiCreateTabPanel(20,180,120,80,false)
        _G.guiFontTab = guiCreateTab('font-tab',guiFontPanel)
        assert(guiSetFont(guiFontTab,guiFont))
        _G.guiFontSet = guiSetFont(guiFontLabel,guiFont) and guiSetFont(guiFontEdit,guiFont)
        guiLabelSetHorizontalAlign(guiFontLabel,'right',true)
        guiLabelSetVerticalAlign(guiFontLabel,'bottom')
        _G.guiFontName, _G.guiFontReturned = guiGetFont(guiFontLabel)
        _G.guiDxFont = dxCreateFont(':radarres/radar/font.ttf',12)
        addEventHandler('onClientRender',root,function()
            _G.guiFontRejectedByDx = dxDrawText('invalid-gui-font',0,0,100,20,-1,1,guiFont)==false
        end)
    )LUA";
    auto body = NoCacheScriptBody(75, script);
    CHECK(ActiveVmFeedScripts(body.data(),body.size(),body.size()*8));
    ActiveVmPulseResources();
    RunLua(vm,"gui-font-create",R"LUA(
        assert(guiFontSet and isElement(guiFont) and getElementType(guiFont)=='gui-font')
        assert(guiFontName=='guifonts*radarres*radar/font.ttf*12*0' and guiFontReturned.id==guiFont.id)
        assert(guiFont2.id~=guiFont.id and guiFontDefault and guiFontMin and guiFontMax)
        assert(guiFontMissing==false and guiFontBad==false and guiFontEscape==false)
        assert(guiCreateFont(':radarres/radar/font.ttf')==false, 'creation requires a resource owner')
    )LUA");
    RunLua(vm,"gui-font-metrics",R"LUA(
        assert(math.abs(guiLabelGetFontHeight(guiFontLabel)-19.2)<0.001, 'host estimate at 12 pt / 16 px')
        assert(guiSetText(guiFontLabel,'ab\nabcdef\nx'))
        assert(math.abs(guiLabelGetTextExtent(guiFontLabel)-6*7*16/15)<0.001, 'longest line in assigned font')
        assert(guiSetFont(guiFontLabel,guiFontDefault))
        local defaultHeight=guiLabelGetFontHeight(guiFontLabel)
        assert(guiSetFont(guiFontLabel,guiFontMax) and guiLabelGetFontHeight(guiFontLabel)>defaultHeight*10)
        assert(guiSetFont(guiFontLabel,guiFontMin) and guiLabelGetFontHeight(guiFontLabel)<defaultHeight)
        assert(guiSetFont(guiFontLabel,guiFont) and guiSetText(guiFontLabel,'#ff0000\né🙂\\n'))
    )LUA");
    RunLua(vm,"gui-font-validation",R"LUA(
        local old=guiGetFont(guiFontLabel)
        assert(guiSetFont(guiFontLabel,guiDxFont)==false, 'DX font is not a GUI font')
        assert(guiSetFont(guiFontLabel,localPlayer)==false and guiSetFont(guiFontLabel,'pricedown')==false)
        assert(guiSetFont(guiFontLabel,nil)==false and guiSetFont(guiFontLabel,123)==false)
        assert(guiGetFont(guiFontLabel)==old, 'failed assignment preserves font')
        assert(select('#',guiGetFont(guiFontLabel))==2 and select('#',guiGetFont(localPlayer))==1)
        assert(guiSetFont(guiFontLabel) and guiGetFont(guiFontLabel)=='default-normal')
        assert(select(2,guiGetFont(guiFontLabel))==nil)
        assert(guiSetFont(guiFontLabel,guiFont))
    )LUA");
    RunLua(vm,"gui-font-dx-metric-types",R"LUA(
        assert(dxGetFontHeight(1,guiFont)==false, 'GUI font must not silently measure as DX default')
        assert(dxGetTextWidth('x',1,guiFont)==false, 'DX text width requires a DX font')
        assert(not pcall(dxGetTextSize,'x',0,1,guiFont), 'DX size uses a typed font variant')
        assert(not pcall(dxGetTextSize,'',0,1,guiFont), 'font type is validated even for empty text')
    )LUA");
    std::string frame = ActiveVmPump();
    lua_State* L = ActiveVmState();
    lua_getglobal(L,"guiFont"); lua_getfield(L,-1,"id");
    const std::string guiFontId = std::to_string(lua_tointeger(L,-1));
    const std::string selection = "F 16.000 0 @font:" + guiFontId +
        ":/tmp/neon-dx-cache/radarres/radar/font.ttf\n";
    lua_pop(L,2);
    CHECK(Has(frame,(selection + "Y 20.0 30.0 140.0 120.0 4294967295 2 2 1 #ff0000\\né🙂\\\\n\nF\n").c_str()));
    CHECK(Has(frame,(selection + "R 20.0 130.0 120.0 30.0 ").c_str()));
    CHECK(Has(frame,"T 20.0 130.0 140.0 160.0 1.00 4294967295 0 font-edit\nF\n"));
    CHECK(Has(frame,(selection + "T 24.0 180.0 96.0 198.0 1.00 4294967295 0 font-tab\n").c_str()));
    RunLua(vm,"gui-font-dx-separation", "assert(guiFontRejectedByDx, 'GUI fonts cannot be passed as DX fonts')");

    WorldRPG::Net::ResourceStartInfo consumer = producer;
    consumer.name = "guifontconsumer";
    consumer.resourceId = 76;
    consumer.entityId = 0x7600;
    consumer.dynamicEntityId = 0x7601;
    ActiveVmOnResourceStart(consumer);
    body = NoCacheScriptBody(76,R"LUA(
        _G.guiFontConsumer=guiCreateLabel(200,30,120,50,'consumer',false)
        _G.guiFontNamed=guiCreateLabel(200,90,120,50,'named',false)
        _G.guiConsumerSet=guiSetFont(guiFontConsumer,guiFont) and guiSetFont(guiFontNamed,guiFontName)
    )LUA");
    CHECK(ActiveVmFeedScripts(body.data(),body.size(),body.size()*8));
    ActiveVmPulseResources();
    RunLua(vm,"gui-font-cross-resource",R"LUA(
        assert(guiConsumerSet and select(2,guiGetFont(guiFontConsumer)).id==guiFont.id)
        assert(guiGetFont(guiFontNamed)==guiFontName and select(2,guiGetFont(guiFontNamed))==nil)
        assert(guiSetVisible(guiFont, false)==false, 'font is a render element, not a widget')
    )LUA");
    // The file's resource does not own a font created by guifonts.
    WorldRPG::Net::BitStreamWriter fileStop;
    fileStop.Write(static_cast<uint16_t>(9));
    ActiveVmOnResourceStop(fileStop.GetData(),fileStop.GetByteLength(),fileStop.GetBitLength());
    RunLua(vm,"gui-font-file-owner-stop",R"LUA(
        assert(isElement(guiFont) and guiGetFont(guiFontConsumer)==guiFontName)
        assert(destroyElement(guiFont) and not isElement(guiFont))
        assert(guiGetFont(guiFontConsumer)=='default-normal' and select(2,guiGetFont(guiFontConsumer))==nil)
        assert(guiGetFont(guiFontNamed)=='default-normal', 'named users also release the removed font')
        assert(guiGetFont(guiFontEdit)=='default-normal' and isElement(guiFont2))
        assert(guiSetFont(guiFontConsumer,guiFont)==false)
        assert(guiSetFont(guiFontConsumer,guiFont2))
    )LUA");
    WorldRPG::Net::BitStreamWriter ownerStop;
    ownerStop.Write(static_cast<uint16_t>(75));
    ActiveVmOnResourceStop(ownerStop.GetData(),ownerStop.GetByteLength(),ownerStop.GetBitLength());
    RunLua(vm,"gui-font-owner-stop",R"LUA(
        assert(not isElement(guiFont2) and not isElement(guiFontDefault) and not isElement(guiFontMin) and not isElement(guiFontMax))
        assert(not isElement(guiFontEdit) and isElement(guiFontConsumer), 'other resource GUI survives')
        assert(guiGetFont(guiFontConsumer)=='default-normal' and select(2,guiGetFont(guiFontConsumer))==nil)
    )LUA");
    frame = ActiveVmPump();
    CHECK(Has(frame,("D " + guiFontId + "\n").c_str()));
    CHECK(Has(frame,"F 12.000 0 @gui:default-normal\nY 200.0 30.0"));
    CHECK(!Has(frame,"/radar/font.ttf"));
    WorldRPG::Net::BitStreamWriter consumerStop;
    consumerStop.Write(static_cast<uint16_t>(76));
    ActiveVmOnResourceStop(consumerStop.GetData(),consumerStop.GetByteLength(),consumerStop.GetBitLength());
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

void CheckModelDownloads(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG;

    LoopbackHttp server(LoopbackHttp::Mode::Respond);
    CHECK(server.ok());
    if (!server.ok()) return;
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port());
    Net::HttpDownloader::SetServerBase(base);

    Net::ResourceStartInfo model;
    model.name = "model-download-test";
    model.resourceId = 120;
    model.entityId = 0x7300;
    model.noClientCacheScripts = 1;
    Net::ResourceFile asset;
    asset.name = "model.dff";
    asset.clientFile = true;
    asset.type = Net::kResourceFileTypeClientFile;
    constexpr char response[] = "ok-body";
    asset.size = sizeof(response) - 1;
    asset.crc = crc32(0, reinterpret_cast<const unsigned char*>(response), asset.size);
    model.files = {asset};

    const std::filesystem::path path = Net::HttpDownloader::CachedFilePath(model.name, asset.name);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << "old";
    Script::ActiveVmOnResourceStart(model);
    const auto body = NoCacheScriptBody(model.resourceId, R"LUA(
        _G.modelDownloadEvents = 0
        addEventHandler("onClientFileDownloadComplete", resourceRoot, function(name, ok, requester)
            assert(name == "model.dff" and ok and requester == getThisResource())
            assert(source.id == resourceRoot.id and fileGetContents(name) == "ok-body")
            _G.modelDownloadEvents = _G.modelDownloadEvents + 1
        end)
        assert(downloadFile("undeclared.dff") == false)
        assert(downloadFile("./model.dff") and downloadFile("model.dff"))
        assert(_G.modelDownloadEvents == 0, "completion runs on the next resource pulse")
    )LUA");
    CHECK(Script::ActiveVmFeedScripts(body.data(), body.size(), body.size() * 8));
    Script::ActiveVmPulseResources();
    std::filesystem::path resolved;
    CHECK(!Script::ResolveModelPath(":model-download-test/model.dff", resolved));
    CHECK(Net::HttpDownloader::WaitIdle(3000));
    Script::ActiveVmPulseResources();
    CHECK(Script::ResolveModelPath(":model-download-test/model.dff", resolved) && resolved == path);
    RunLua(vm, "download-model", "assert(_G.modelDownloadEvents == 1)");

    LoopbackHttp oldServer(LoopbackHttp::Mode::Hold);
    CHECK(oldServer.ok());
    if (!oldServer.ok()) return;
    model.name = "model-download-restart-test";
    model.resourceId = 121;
    model.entityId = 0x7301;
    model.noClientCacheScripts = 0;
    model.files[0].autoDownload = true;
    const std::string restartPath = Net::HttpDownloader::CachedFilePath(model.name, asset.name);
    std::filesystem::remove(restartPath);
    Net::HttpDownloader::SetServerBase("http://127.0.0.1:" + std::to_string(oldServer.port()));
    model.files[0].crc ^= 1u;
    Script::ActiveVmOnResourceStart(model);
    CHECK(oldServer.WaitAccepted(3000));
    Net::HttpDownloader::SetServerBase(base);
    model.files[0].crc = asset.crc;
    Script::ActiveVmOnResourceStart(model);
    oldServer.Release();
    CHECK(Net::HttpDownloader::WaitIdle(3000));
    Script::ActiveVmPulseResources();
    CHECK(Script::ResourceLoader::IsFileReady(model.name, asset.name, 0));
    Script::ResourceLoader::ResourceView latest;
    CHECK(Script::ResourceLoader::CopyResource(model.name, latest) && latest.state == "running");

    // The host validates chunk boundaries before any native parser is called.
    constexpr unsigned char clump[] = {
        0x10, 0, 0, 0, 16, 0, 0, 0, 0xff, 0xff, 3, 0x18,
        0x01, 0, 0, 0, 4, 0, 0, 0, 0xff, 0xff, 3, 0x18,
        0, 0, 0, 0,
    };
    const auto fixture = path.parent_path() / "clump-fixture.dff";
    std::ofstream(fixture, std::ios::binary).write(reinterpret_cast<const char*>(clump), sizeof(clump));
    CHECK(Script::ReadDffClump(fixture.c_str()).size() == sizeof(clump));
    std::filesystem::resize_file(fixture, sizeof(clump) - 1);
    CHECK(Script::ReadDffClump(fixture.c_str()).empty());
    std::filesystem::remove(fixture);
    std::filesystem::remove(path);
    std::filesystem::remove(restartPath);
}

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

// ROADMAP_PEDS_SYNC phase 1: every field of the ENTITY_ADD ped body is kept, in the order of
// CPacketHandler::Packet_EntityAdd case PED (CPacketHandler.cpp 3997-4187), at the 0x0030 widths.
void CheckPedEntityBody() {
    using namespace WorldRPG;
    Net::BitStreamWriter w;
    auto bits = [&](uint32_t value, size_t count) {
        uint8_t raw[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16),
                          static_cast<uint8_t>(value >> 24)};
        w.WriteBits(raw, count);
    };
    bits(static_cast<uint32_t>(100 * 1024), 24);  // SPositionSync x = 100
    bits(static_cast<uint32_t>(-50 * 1024) & 0xFFFFFFu, 24);  // y = -50
    w.Write(12.5f);                                // z
    w.WriteCompressed(61u, 2);                     // model
    bits(32768, 16);                               // SPedRotationSync, ~0 rad
    w.Write(static_cast<uint8_t>(255));            // SPlayerHealthSync -> 255
    w.Write(static_cast<uint8_t>(102));            // SPlayerArmorSync -> 51
    bits(1, 1);                                    // in vehicle
    bits(321, 17);                                 // vehicle id
    bits(2, 4);                                    // seat
    bits(0, 1); bits(1, 1); bits(1, 1); bits(1, 1);  // jetpack, synced, headless, frozen
    w.WriteCompressed(55u, 1);                     // alpha raw -> 200
    w.Write(static_cast<uint8_t>(54));             // move anim
    w.Write(static_cast<uint8_t>(1));              // one clothes item
    w.Write(static_cast<uint8_t>(5)); w.WriteBytes(reinterpret_cast<const uint8_t*>("vestw"), 5);
    w.Write(static_cast<uint8_t>(4)); w.WriteBytes(reinterpret_cast<const uint8_t*>("vest"), 4);
    w.Write(static_cast<uint8_t>(0));              // clothes type
    w.Write(static_cast<uint8_t>(2)); w.Write(static_cast<uint8_t>(22)); w.Write(static_cast<uint16_t>(170));  // slot 2: colt, 170
    w.Write(static_cast<uint8_t>(0xFF));           // end of weapons
    w.Write(static_cast<uint8_t>(2));              // current slot
    bits(1, 1);                                    // animation present
    w.Write(static_cast<uint16_t>(3)); w.WriteBytes(reinterpret_cast<const uint8_t*>("ped"), 3);
    w.Write(static_cast<uint16_t>(9)); w.WriteBytes(reinterpret_cast<const uint8_t*>("WALK_civi"), 9);
    w.Write(static_cast<int32_t>(-1));
    bits(1, 1); bits(1, 1); bits(0, 1); bits(0, 1);  // looped, updatePosition, interruptable, freezeLastFrame
    w.Write(static_cast<int32_t>(250));
    bits(1, 1);                                    // taskRestore
    w.Write(400.f); w.Write(1.5f);                 // elapsed, speed

    Net::BitStreamReader r(w.GetData(), w.GetByteLength(), w.GetBitLength());
    Entity::EntityCommon common;
    common.id = 900;
    common.type = Entity::kTypePed;
    common.collisions = false;
    common.dimension = 7;
    Entity::Visual visual;
    CHECK(Entity::ReadEntityBodyPed(r, common, visual));
    CHECK(r.GetRemainingBits() < 8);
    CHECK(visual.model == 61 && visual.x == 100.f && visual.y == -50.f && visual.z == 12.5f);
    CHECK(visual.ped != nullptr);
    if (!visual.ped) return;
    const Entity::PedSpawn& p = *visual.ped;
    CHECK(p.health == 255.f && p.armor == 51.f && p.synced);
    CHECK(p.vehicleId == 321 && p.seat == 2);
    CHECK(!p.hasJetpack && p.headless && p.frozen);
    CHECK(p.alpha == 200 && p.moveAnim == 54);
    CHECK(p.clothes.size() == 1 && p.clothes[0].texture == "vestw" && p.clothes[0].model == "vest" && p.clothes[0].type == 0);
    CHECK(p.weapons.size() == 1 && p.weapons[0].slot == 2 && p.weapons[0].type == 22 && p.weapons[0].totalAmmo == 170);
    CHECK(p.currentSlot == 2 && !p.collisions && p.dimension == 7);
    CHECK(p.animation.present && p.animation.block == "ped" && p.animation.name == "WALK_civi");
    CHECK(p.animation.time == -1 && p.animation.looped && p.animation.updatePosition && !p.animation.interruptable);
    CHECK(p.animation.blendTime == 250 && p.animation.taskRestore && p.animation.elapsedTime == 400.f && p.animation.speed == 1.5f);
}

// PLAYER_LIST as the mobile peer receives it (device capture 2026-11-14): an 8-byte prefix before the records.
void CheckPlayerListCapture() {
    const char* hex = "030002002d310000b906804454d85b5d595b17d85cdcdd5b98d85bdc18cf402f40000012000030000000000000000000000040601f00000000";
    std::vector<uint8_t> body;
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) body.push_back(static_cast<uint8_t>(std::stoi(std::string(hex + i, 2), nullptr, 16)));
    WorldRPG::ServerWorld::OnPlayerList(body.data(), body.size(), 456);
    uint32_t id = 0;
    CHECK(WorldRPG::ServerWorld::FindPlayerByNick("Samuel_assuncaopc", id));
    CHECK(id != 0 && WorldRPG::ServerWorld::ModelOf(id) >= 0 && WorldRPG::ServerWorld::ModelOf(id) <= 312);
    std::printf("player list capture: id=%u model=%d\n", id, WorldRPG::ServerWorld::ModelOf(id));
}

// CLuaEngineDefs.cpp:2192-2270 and CModelNames::InitializeMaps (CModelNames.cpp:29362-29382).
void CheckEngineModelNames(WorldRPG::Script::LuaVMStub& vm) {
    CHECK(WorldRPG::Models::ModelNameFromId(400) == std::string("Landstalker"));
    CHECK(WorldRPG::Models::ModelIdFromName("landstal") == WorldRPG::Models::kInvalidModelId);
    CHECK(WorldRPG::Models::ModelIdFromName("Landstalker") == 400u);
    CHECK(std::string(WorldRPG::Models::ModelNameFromId(334)) == "Night_Stick");
    CHECK(WorldRPG::Models::ModelIdFromName("Night_Strick") == 334u);
    CHECK(WorldRPG::Models::ModelNameFromId(611)[0] == '\0');
    CHECK(WorldRPG::Models::ModelIdFromName("Street Clean Trailer") == WorldRPG::Models::kInvalidModelId);
    CHECK(WorldRPG::Models::ModelIdFromName("Monster") == WorldRPG::Models::kInvalidModelId);
    CHECK(WorldRPG::Models::ModelIdFromName("Monster 1") == 444u);

    // ARM64 CBaseModelInfo fixture: LOD at +0x38, flags at +0x2c, TXD at +0x24,
    // AsDamageAtomicModelInfoPtr at vptr+0x18. Flags 0x0902 are draw-last, road, and TREE.
    alignas(8) unsigned char model[0x40] = {};
    const float lod = 123.5f;
    const std::uint16_t flags = 0x0902;
    const std::uint16_t txd = 42;
    std::memcpy(model + 0x38, &lod, sizeof lod);
    std::memcpy(model + 0x2c, &flags, sizeof flags);
    std::memcpy(model + 0x24, &txd, sizeof txd);
    using DamageFn = void* (*)(void*);
    DamageFn slots[4] = {};
    slots[3] = [](void* self) -> void* { return self; };
    const void* vptr = slots;
    std::memcpy(model, &vptr, sizeof vptr);
    WorldRPG::Models::StockModelView view;
    CHECK(WorldRPG::Models::ReadStockModelView(model, view));
    CHECK(view.lod == 123.5f);
    CHECK(view.txd == 42);
    CHECK(view.damageAtomic);
    CHECK(WorldRPG::Models::ReadIdeFlag(view, WorldRPG::Models::IdeFlag::DrawLast));
    CHECK(WorldRPG::Models::ReadIdeFlag(view, WorldRPG::Models::IdeFlag::IsRoad));
    CHECK(WorldRPG::Models::ReadIdeFlag(view, WorldRPG::Models::IdeFlag::IsTree));
    CHECK(WorldRPG::Models::ReadIdeFlag(view, WorldRPG::Models::IdeFlag::IgnoreLighting) == false);
    CHECK(WorldRPG::Models::ReadIdeFlag(view, WorldRPG::Models::IdeFlag::DisableBackfaceCulling));
    slots[3] = [](void*) -> void* { return nullptr; };
    CHECK(WorldRPG::Models::ReadStockModelView(model, view));
    CHECK(view.damageAtomic == false);
    WorldRPG::Models::IdeFlag parsed = WorldRPG::Models::IdeFlag::IsRoad;
    CHECK(WorldRPG::Models::IdeFlagFromName("IS_ROAD", parsed));
    CHECK(parsed == WorldRPG::Models::IdeFlag::IsRoad);
    CHECK(WorldRPG::Models::IdeFlagFromName("not-a-flag", parsed) == false);
    CHECK(WorldRPG::Models::ReadLiveStockModel(411, view) == WorldRPG::Models::LiveModelStatus::Unavailable);
    float lodDistance = 200.f;
    WorldRPG::Models::LodOverride lodSaved;
    CHECK(WorldRPG::Models::ApplyLodDistance(lodDistance, lodSaved, 100.f, false));
    CHECK(lodDistance == 100.f && lodSaved.original == 200.f);
    CHECK(WorldRPG::Models::ApplyLodDistance(lodDistance, lodSaved, 1000.f, false));
    CHECK(lodDistance == 325.f && lodSaved.original == 200.f);
    CHECK(WorldRPG::Models::ApplyLodDistance(lodDistance, lodSaved, 1000.f, true));
    CHECK(lodDistance == 1000.f && lodSaved.original == 200.f);
    CHECK(WorldRPG::Models::ApplyLodDistance(lodDistance, lodSaved, 0.f, true) == false);
    CHECK(lodDistance == 1000.f);
    CHECK(WorldRPG::Models::RestoreLodDistance(lodDistance, lodSaved));
    CHECK(lodDistance == 200.f && lodSaved.saved == false);
    CHECK(WorldRPG::Models::RestoreLodDistance(lodDistance, lodSaved) == false);
    WorldRPG::Models::FlagRecord flagRecord;
    flagRecord.flags = 0xC0;
    WorldRPG::Models::SetRawFlags(flagRecord, 0x00FF);
    CHECK(flagRecord.haveOriginal && flagRecord.original == 0xC0);
    CHECK((flagRecord.flags & 0x80) != 0 && (flagRecord.flags & 0x7F) == 0x7F);
    WorldRPG::Models::SetIdeWord(flagRecord, 1u << 2, 1);
    CHECK((flagRecord.flags & 0x02) != 0 && (flagRecord.flags & 0x04) == 0 && (flagRecord.flags & 0x80) != 0);
    WorldRPG::Models::FlagRecord singleFlag;
    singleFlag.flags = 0xC0;
    WorldRPG::Models::SetOneIdeFlag(singleFlag, WorldRPG::Models::IdeFlag::IsRoad, true);
    CHECK(singleFlag.haveOriginal == false);
    CHECK(WorldRPG::Models::ResetRecordedFlags(singleFlag) == false);
    CHECK(WorldRPG::Models::ResetRecordedFlags(flagRecord));
    CHECK(flagRecord.flags == static_cast<std::uint16_t>((0xC0 & 0xFF7F) | (flagRecord.flags & 0x80)));
    WorldRPG::Models::TxdRecord txdRecord;
    txdRecord.id = 5;
    txdRecord.refs = 2;
    txdRecord.rwObject = true;
    const WorldRPG::Models::TxdTransfer moved = WorldRPG::Models::SetTxdId(txdRecord, 9);
    CHECK(moved.from == 5 && moved.to == 9 && moved.refs == 3 && txdRecord.original == 5 && txdRecord.id == 9);
    WorldRPG::Models::TxdTransfer back;
    CHECK(WorldRPG::Models::ResetTxdId(txdRecord, back));
    CHECK(back.to == 5 && txdRecord.id == 5 && txdRecord.haveDefault == false);
    CHECK(WorldRPG::Models::ResetTxdId(txdRecord, back) == false);
    WorldRPG::Models::VisibleTime visible;
    visible.on = 20;
    visible.off = 6;
    CHECK(WorldRPG::Models::HoursInRange(25, 1) == false);
    CHECK(WorldRPG::Models::ApplyVisibleTime(visible, 25, 1) == false);
    CHECK(visible.on == 20 && visible.saved == false);
    CHECK(WorldRPG::Models::ApplyVisibleTime(visible, 8, 18));
    CHECK(visible.on == 8 && visible.off == 18 && visible.originalOn == 20 && visible.originalOff == 6);
    CHECK(WorldRPG::Models::ApplyVisibleTime(visible, 1, 2));
    CHECK(visible.originalOn == 20 && visible.on == 1);
    unsigned char groupBytes[WorldRPG::Models::kObjectGroupBytes] = {};
    WorldRPG::Models::ObjectGroup written;
    written.mass = 12.5f;
    written.turnMass = 3.f;
    written.colDamageEffect = 20;
    WorldRPG::Models::WriteObjectGroup(written, groupBytes);
    WorldRPG::Models::ObjectGroup readBack;
    WorldRPG::Models::ReadObjectGroup(groupBytes, readBack);
    CHECK(readBack.mass == 12.5f && readBack.turnMass == 3.f && readBack.colDamageEffect == 20);
    written.smashMultiplier = 5.f;
    written.breakVelocity[0] = 1.f;
    written.breakVelocity[1] = 2.f;
    written.breakVelocity[2] = 3.f;
    written.breakVelocityRand = 4.f;
    written.breakMode = 2;
    written.sparksOnImpact = 7;
    WorldRPG::Models::WriteObjectGroup(written, groupBytes);
    float smash = 0.f;
    float breakX = 0.f;
    float breakRand = 0.f;
    std::uint32_t breakMode = 0;
    std::uint32_t sparks = 0;
    std::memcpy(&smash, groupBytes + WorldRPG::Models::kGroupSmash, sizeof smash);
    std::memcpy(&breakX, groupBytes + WorldRPG::Models::kGroupBreakVelocity, sizeof breakX);
    std::memcpy(&breakRand, groupBytes + WorldRPG::Models::kGroupBreakVelocityRand, sizeof breakRand);
    std::memcpy(&breakMode, groupBytes + WorldRPG::Models::kGroupBreakMode, sizeof breakMode);
    std::memcpy(&sparks, groupBytes + WorldRPG::Models::kGroupSparks, sizeof sparks);
    CHECK(smash == 5.f && breakX == 1.f && breakRand == 4.f && breakMode == 2 && sparks == 7);
    WorldRPG::Models::GroupOriginal groupSaved;
    WorldRPG::Models::AssignGroupFloat(groupBytes, groupSaved, WorldRPG::Models::kGroupMass, 12.5f);
    CHECK(groupSaved.saved == false);
    WorldRPG::Models::AssignGroupFloat(groupBytes, groupSaved, WorldRPG::Models::kGroupMass, 9.f);
    CHECK(groupSaved.saved);
    WorldRPG::Models::AssignGroupBoolWord(groupBytes, groupSaved, WorldRPG::Models::kGroupSparks, false);
    std::memcpy(&sparks, groupBytes + WorldRPG::Models::kGroupSparks, sizeof sparks);
    CHECK(sparks == 0);
    std::memcpy(groupBytes, groupSaved.bytes, WorldRPG::Models::kObjectGroupBytes);
    WorldRPG::Models::ReadObjectGroup(groupBytes, readBack);
    CHECK(readBack.mass == 12.5f && readBack.sparksOnImpact == 7);
    unsigned char surface[WorldRPG::Models::kSurfaceStride] = {};
    WorldRPG::Models::CopyOriginalSurface(0, surface);
    WorldRPG::Models::SurfaceRead surfaceRead;
    CHECK(WorldRPG::Models::ReadSurfaceRecord(surface, WorldRPG::Models::SurfaceProperty::Tyre, surfaceRead));
    CHECK(surfaceRead.number == surface[0]);
    CHECK(WorldRPG::Models::ReadSurfaceRecord(surface, WorldRPG::Models::SurfaceProperty::Bullet, surfaceRead));
    CHECK(std::string(surfaceRead.text) == "metal");
    CHECK(WorldRPG::Models::ReadSurfaceRecord(surface, WorldRPG::Models::SurfaceProperty::Adhesion, surfaceRead));
    CHECK(std::string(surfaceRead.text) == "road");
    CHECK(WorldRPG::Models::ReadSurfaceRecord(surface, WorldRPG::Models::SurfaceProperty::StepDust, surfaceRead) == false);
    CHECK(WorldRPG::Models::WriteSurfaceAudio(surface, "METAL"));
    CHECK(WorldRPG::Models::ReadSurfaceRecord(surface, WorldRPG::Models::SurfaceProperty::Audio, surfaceRead));
    CHECK(std::string(surfaceRead.text) == "metal");
    CHECK(WorldRPG::Models::WriteSurfaceWheel(surface, "disabled"));
    CHECK(WorldRPG::Models::ReadSurfaceRecord(surface, WorldRPG::Models::SurfaceProperty::Wheel, surfaceRead));
    CHECK(std::string(surfaceRead.text) == "disabled");
    CHECK(WorldRPG::Models::WriteSurfaceBool(surface, WorldRPG::Models::SurfaceProperty::See, true));
    CHECK(WorldRPG::Models::ReadSurfaceRecord(surface, WorldRPG::Models::SurfaceProperty::See, surfaceRead) && surfaceRead.flag);
    const unsigned char tyre = surface[0];
    surface[0] = 1;
    WorldRPG::Models::CopyOriginalSurface(0, surface);
    CHECK(surface[0] == tyre);
    WorldRPG::Models::SetStreamingMemory(2048);
    CHECK(WorldRPG::Models::StreamingMemory() == 2048);
    WorldRPG::Models::RestoreStreamingMemory();
    CHECK(WorldRPG::Models::StreamingMemory() == WorldRPG::Models::kDefaultStreamingMemory);
    WorldRPG::Models::SetCacheLimits(4, std::nullopt);
    WorldRPG::Models::CacheLimits cache = WorldRPG::Models::GetCacheLimits();
    CHECK(cache.vehiclesSet && cache.vehicles == 4 && cache.pedsSet == false);
    WorldRPG::Models::SetCacheLimits(std::nullopt, 2);
    cache = WorldRPG::Models::GetCacheLimits();
    CHECK(cache.vehiclesSet == false && cache.pedsSet && cache.peds == 2);
    CHECK(WorldRPG::Models::PoolDefaultCapacity(0) == 32000);
    CHECK(WorldRPG::Models::PoolDefaultCapacity(2) == 1200);
    CHECK(WorldRPG::Models::PoolDefaultCapacity(18) == 4000);
    CHECK(WorldRPG::Models::DecidePoolCapacity(0, 100, 0) == WorldRPG::Models::PoolSetDecision::BelowDefault);
    CHECK(WorldRPG::Models::DecidePoolCapacity(1, 200, 0) == WorldRPG::Models::PoolSetDecision::NotChangeable);
    CHECK(WorldRPG::Models::DecidePoolCapacity(0, 32000, 40000) == WorldRPG::Models::PoolSetDecision::BelowUsed);
    CHECK(WorldRPG::Models::DecidePoolCapacity(16, 90000, 0) == WorldRPG::Models::PoolSetDecision::Resize);
    WorldRPG::Models::SetAsyncLoading(true, true);
    CHECK(WorldRPG::Models::AsyncLoadingEnabled() && WorldRPG::Models::AsyncLoadingForced());
    CHECK(WorldRPG::Models::PostFXValue(WorldRPG::Models::PostFX::Gamma) == 0.95f);
    CHECK(WorldRPG::Models::PostFXValue(WorldRPG::Models::PostFX::Brightness) == 1.03f);
    WorldRPG::Models::SetPostFXEnabled(WorldRPG::Models::PostFX::Brightness, true);
    WorldRPG::Models::SetPostFXEnabled(WorldRPG::Models::PostFX::Contrast, false);
    CHECK(WorldRPG::Models::PostFXEnabled(WorldRPG::Models::PostFX::Contrast));
    CHECK(WorldRPG::Models::PostFXEnabled(WorldRPG::Models::PostFX::Brightness));
    CHECK(WorldRPG::Models::PostFXEnabled(WorldRPG::Models::PostFX::Gamma) == false);
    WorldRPG::Models::SetPostFXMode(true, true);
    CHECK(WorldRPG::Models::PostFXMode() == 1);
    WorldRPG::Models::SetPostFXMode(false, true);
    CHECK(WorldRPG::Models::PostFXMode() == 2);
    CHECK(WorldRPG::Models::SetObjectStreamingLimits(1001, 0) == false);
    CHECK(WorldRPG::Models::SetObjectStreamingLimits(400, 400));
    CHECK(WorldRPG::Models::MaxStreamedObjects() == 400 && WorldRPG::Models::MaxLowLodObjects() == 400);
    CHECK(WorldRPG::Models::SetDistantLightDistance(100.f) == false);
    CHECK(WorldRPG::Models::SetDistantLightDistance(300.f));
    CHECK(WorldRPG::Models::GetDistantLights().distance == 300.f && WorldRPG::Models::GetDistantLights().automatic == false);
    CHECK(WorldRPG::Models::SetDistantLightDistance(5000.f));
    CHECK(WorldRPG::Models::SetDistantLightDistance(5000.1f) == false);
    int cullOwner = 1;
    WorldRPG::Models::CullZone cull;
    cull.width = cull.depth = cull.height = 10.f;
    const std::uint32_t cullId = WorldRPG::Models::CreateCullZone(cull, &cullOwner);
    CHECK(cullId == 1);
    const std::vector<WorldRPG::Models::CullZone> listed = WorldRPG::Models::ListCullZones(false, WorldRPG::Models::CullZoneType::Attribute);
    CHECK(listed.size() == 1 && listed[0].id == 1 && listed[0].width == 10.f && listed[0].centerX == 0.f && listed[0].original == false);
    CHECK(WorldRPG::Models::CreateCullZone(cull, nullptr) == 0);
    cull.width = 0.f;
    CHECK(WorldRPG::Models::CreateCullZone(cull, &cullOwner) == 0);
    cull.width = 10.f;
    cull.type = WorldRPG::Models::CullZoneType::Mirror;
    cull.mirrorNormalZ = 2.f;
    CHECK(WorldRPG::Models::CreateCullZone(cull, &cullOwner) == 0);
    CHECK(WorldRPG::Models::SetCullZoneEnabled(cullId, false, &cullOwner));
    CHECK(WorldRPG::Models::RemoveCullZone(cullId, &cullOwner));
    CHECK(WorldRPG::Models::ListCullZones(false, WorldRPG::Models::CullZoneType::Attribute).empty());
    WorldRPG::Models::ModelGroupRecord modelGroup;
    modelGroup.current = WorldRPG::Models::GroupIndexFromHalf(0xFFFF);
    CHECK(modelGroup.current == -1);
    CHECK(WorldRPG::Models::SetModelGroup(modelGroup, 10));
    CHECK(modelGroup.original == -1 && modelGroup.current == 10);
    CHECK(WorldRPG::Models::HalfFromGroupIndex(-1) == 0xFFFF);
    CHECK(WorldRPG::Models::SetModelGroup(modelGroup, -1));
    CHECK(WorldRPG::Models::RestoreModelGroup(modelGroup));
    CHECK(modelGroup.current == -1 && modelGroup.saved == false);

    RunLua(vm, "engine-model-names", R"LUA(
        assert(engineGetModelNameFromID(400) == "Landstalker")
        assert(engineGetModelNameFromID(400.9) == "Landstalker")
        assert(engineGetModelNameFromID("411") == "Infernus")
        assert(engineGetModelNameFromID(411, "ignored") == "Infernus")
        assert(engineGetModelIDFromName("infernus") == 411)
        assert(engineGetModelIDFromName("LANDSTALKER") == 400)
        assert(engineGetModelNameFromID(334) == "Night_Stick")
        assert(engineGetModelIDFromName("Night_Strick") == 334)
        assert(engineGetModelIDFromName("night_stick") == 334)
        assert(engineGetModelNameFromID(9557) == "lake_sfw")
        assert(engineGetModelIDFromName("lake_SFW") == 9557)
        assert(engineGetModelNameFromID(610) == "Farm Trailer")
        assert(engineGetModelIDFromName("Farm Trailer") == 610)
        assert(engineGetModelNameFromID(321) == "Regular_Dildo")
        assert(engineGetModelNameFromID(1337) == "Recycle1")
        assert(engineGetModelNameFromID(18630) == "cs_landbit_20_A")
        assert(engineGetModelIDFromName("Monster 1") == 444)
        assert(engineGetModelIDFromName("Monster") == false)
        assert(engineGetModelNameFromID(611) == false)
        assert(engineGetModelIDFromName("Street Clean Trailer") == false)
        assert(engineGetModelIDFromName("411") == false)
        assert(engineGetModelIDFromName(411) == false)
        assert(engineGetModelIDFromName("") == false)
        assert(engineGetModelNameFromID() == false)
        assert(engineGetModelNameFromID(nil) == false)
        assert(engineGetModelNameFromID(true) == false)
        assert(engineGetModelNameFromID("nope") == false)
        assert(engineGetModelNameFromID(-1) == false)
        assert(engineGetModelNameFromID(65535) == false)
        assert(engineGetModelNameFromID(65534.1) == false)
        assert(engineGetModelNameFromID(0/0) == false)
        assert(engineGetModelIDFromName() == false)
        assert(engineGetModelIDFromName(nil) == false)
        assert(engineGetModelIDFromName("not-a-real-model") == false)
        assert(engineGetModelLODDistance("Infernus") == false)
        assert(engineGetModelLODDistance("411") == false)
        assert(engineGetModelLODDistance(411) == false)
        assert(engineGetModelLODDistance("txd1") == false)
        assert(engineGetModelLODDistance("nope") == false)
        assert(engineGetModelLODDistance() == false)
        local ok, err = pcall(engineGetModelFlags, 411)
        assert(ok == false and err:find("0-19999", 1, true))
        ok, err = pcall(engineGetModelFlags, 70000)
        assert(ok == false and err:find("Expected a valid model ID at argument 1", 1, true) and not err:find("0-19999", 1, true))
        ok, err = pcall(engineGetModelFlags, "Infernus")
        assert(ok == false and err:find("Expected number", 1, true))
        ok, err = pcall(engineGetModelFlags, "411")
        assert(ok == false and err:find("0-19999", 1, true))
        ok, err = pcall(engineGetModelFlag, "nope", "nope")
        assert(ok == false and err:find("Expected number", 1, true))
        ok, err = pcall(engineGetModelFlag, 411, "nope")
        assert(ok == false and err:find("model-ide-flag", 1, true))
        ok, err = pcall(engineGetModelFlag, 411)
        assert(ok == false and err:find("Expected enum", 1, true))
        ok, err = pcall(engineGetModelFlag, 411, "IS_ROAD")
        assert(ok == false and err:find("0-19999", 1, true))
        ok, err = pcall(engineGetModelTXDID, 1337)
        assert(ok == false and err:find("0-19999", 1, true))
        ok, err = pcall(engineGetModelTXDID, -1)
        assert(ok == false and err:find("out-of-range", 1, true))
        assert(engineSetModelLODDistance("Infernus", 100) == false)
        assert(engineSetModelLODDistance("411", 100) == false)
        assert(engineSetModelLODDistance(411, 100) == false)
        assert(engineSetModelLODDistance("nope", 100) == false)
        assert(engineSetModelLODDistance("Infernus", 0) == false)
        assert(engineSetModelLODDistance("Infernus", -1) == false)
        assert(engineSetModelLODDistance("Infernus") == false)
        assert(engineSetModelLODDistance("Infernus", 100, "yes") == false)
        assert(engineResetModelLODDistance("Infernus") == false)
        assert(engineResetModelLODDistance("nope") == false)
        assert(engineResetModelLODDistance(411) == false)
        ok, err = pcall(engineResetModelLODDistance)
        assert(ok == false and err:find("Expected string", 1, true))
        ok, err = pcall(engineSetModelFlags, 411, 1)
        assert(ok == false and err:find("0-19999", 1, true))
        ok, err = pcall(engineSetModelFlags, 411, 1, "yes")
        assert(ok == false and err:find("Expected boolean", 1, true))
        ok, err = pcall(engineSetModelFlag, 411, "is_road", true)
        assert(ok == false and err:find("0-19999", 1, true))
        ok, err = pcall(engineResetModelFlags, 411)
        assert(ok == false and err:find("0-19999", 1, true))
        ok, err = pcall(engineGetTXDIDFromName, "")
        assert(ok == false and err:find("1 and 24", 1, true))
        ok, err = pcall(engineGetTXDIDFromName, string.rep("a", 25))
        assert(ok == false and err:find("1 and 24", 1, true))
        assert(engineGetTXDIDFromName("grass") == false)
        ok, err = pcall(engineSetModelTXDID, 411, 5000)
        assert(ok == false and err:find("valid model ID", 1, true))
        ok, err = pcall(engineSetModelTXDID, 411, 1)
        assert(ok == false and err:find("valid model ID", 1, true))
        ok, err = pcall(engineResetModelTXDID, 411)
        assert(ok == false and err:find("valid model ID", 1, true))
        assert(engineGetModelRuntimeID(411) == false)
        assert(engineGetModelServerID(411) == false)
        assert(engineGetModelParent(411) == false)
        assert(engineGetModelType(411) == false)
        assert(engineGetModelRuntimeID(-1) == false)
        assert(engineGetModelType() == false)
        assert(engineGetModelVisibleTime("nope") == false)
        assert(engineGetModelVisibleTime("Infernus") == false)
        assert(engineSetModelVisibleTime("nope", 8, 18) == false)
        assert(engineSetModelVisibleTime("Infernus", 25, 1) == false)
        assert(engineSetModelVisibleTime("Infernus", 8, 18) == false)
        local timeOk, timeErr = pcall(engineGetModelVisibleTime)
        assert(timeOk == false and timeErr:find("Expected string", 1, true))
        timeOk, timeErr = pcall(engineSetModelVisibleTime, "Infernus", 200, 1)
        assert(timeOk == false and timeErr:find("out-of-range", 1, true))
        local groupOk, groupErr = pcall(engineGetObjectGroupPhysicalProperty, 200, "mass")
        assert(groupOk == false and groupErr:find("0-159", 1, true))
        groupOk, groupErr = pcall(engineGetObjectGroupPhysicalProperty, 0, "nope")
        assert(groupOk == false and groupErr:find("objectgroup-modifiable", 1, true))
        groupOk, groupErr = pcall(engineGetObjectGroupPhysicalProperty, 0, "mass")
        assert(groupOk == false and groupErr:find("valid group", 1, true))
        groupOk, groupErr = pcall(engineGetObjectGroupPhysicalProperty, 0, "fx_system")
        assert(groupOk == false and groupErr:find("isn't possible", 1, true))
        groupOk, groupErr = pcall(engineGetModelPhysicalPropertiesGroup, 411)
        assert(groupOk == false and groupErr:find("valid model", 1, true))
        groupOk, groupErr = pcall(engineSetModelPhysicalPropertiesGroup, 411, 200)
        assert(groupOk == false and groupErr:find("0-159", 1, true))
        groupOk, groupErr = pcall(engineRestoreObjectGroupPhysicalProperties, -1)
        assert(groupOk == false and groupErr:find("0-159", 1, true))
        groupOk, groupErr = pcall(engineSetObjectGroupPhysicalProperty, 200, "mass", 1)
        assert(groupOk == false and groupErr:find("0-159", 1, true))
        groupOk, groupErr = pcall(engineSetObjectGroupPhysicalProperty, 0, "nope", 1)
        assert(groupOk == false and groupErr:find("objectgroup-modifiable", 1, true))
        groupOk, groupErr = pcall(engineSetObjectGroupPhysicalProperty, 0, "mass", 1)
        assert(groupOk == false and groupErr:find("valid group", 1, true))
        groupOk, groupErr = pcall(engineGetModelPhysicalPropertiesGroup, 50000)
        assert(groupOk == false and groupErr:find("Expected a valid model ID at argument 1", 1, true) and not groupErr:find("0-19999", 1, true))
        groupOk, groupErr = pcall(engineGetModelPhysicalPropertiesGroup, 70000)
        assert(groupOk == false and groupErr:find("supported range", 1, true))
        assert(engineGetSurfaceProperties(200, "audio") == nil)
        assert(engineGetSurfaceProperties(0, "nope") == nil)
        assert(engineGetSurfaceProperties(0, "audio") == false)
        assert(engineGetSurfaceProperties(0, "stepdust") == false)
        assert(engineSetSurfaceProperties(0, "audio", "nope") == nil)
        assert(engineSetSurfaceProperties(0, "tyregrip", -1) == nil)
        assert(engineSetSurfaceProperties(0, "audio", "grass") == false)
        assert(engineResetSurfaceProperties(200) == nil)
        assert(engineResetSurfaceProperties() == false)
        assert(engineResetSurfaceProperties("all") == false)
        local streamOk, streamErr = pcall(engineStreamingSetMemorySize, 0)
        assert(streamOk == false and streamErr:find("must be > 0", 1, true))
        streamOk, streamErr = pcall(engineStreamingSetMemorySize)
        assert(streamOk == false and streamErr:find("Expected number", 1, true))
        streamOk, streamErr = pcall(engineStreamingSetMemorySize, -1)
        assert(streamOk == false and streamErr:find("out-of-range", 1, true))
        assert(engineStreamingSetMemorySize(2048) == nil)
        assert(engineStreamingGetMemorySize() == 2048)
        assert(engineStreamingRestoreMemorySize() == nil)
        assert(engineStreamingGetMemorySize() == 50 * 1024 * 1024)
        assert(engineStreamingFreeUpMemory(100) == nil)
        assert(engineStreamingGetUsedMemory() == false)
        assert(engineStreamingSetBufferSize(100) == false)
        assert(engineStreamingSetBufferSize(2521 * 2048) == false)
        assert(engineStreamingGetBufferSize() == false)
        assert(engineStreamingRestoreBufferSize() == nil)
        assert(engineStreamingSetModelCacheLimits() == nil)
        assert(engineStreamingSetModelCacheLimits(nil, 3) == nil)
        streamOk, streamErr = pcall(engineStreamingSetModelCacheLimits, true)
        assert(streamOk == false and streamErr:find("Expected number", 1, true))
        streamOk, streamErr = pcall(engineStreamingGetModelLoadState)
        assert(streamOk == false and streamErr:find("Expected number", 1, true))
        streamOk, streamErr = pcall(engineStreamingGetModelLoadState, 70000)
        assert(streamOk == false and streamErr:find("valid model ID", 1, true))
        streamOk, streamErr = pcall(engineStreamingGetModelLoadState, 30000)
        assert(streamOk == false and streamErr:find("valid model ID", 1, true))
        assert(engineStreamingGetModelLoadState(411) == false)
        streamOk, streamErr = pcall(engineStreamingRequestModel, 411)
        assert(streamOk == false and streamErr:find("valid model ID", 1, true))
        streamOk, streamErr = pcall(engineStreamingRequestModel, 411, "yes")
        assert(streamOk == false and streamErr:find("boolean", 1, true))
        streamOk, streamErr = pcall(engineStreamingReleaseModel, 30000)
        assert(streamOk == false and streamErr:find("valid model ID", 1, true))
        assert(engineGetPoolDefaultCapacity("building") == 32000)
        assert(engineGetPoolDefaultCapacity("object") == 1200)
        assert(engineGetPoolDefaultCapacity("env-map-atomic") == 4000)
        assert(engineGetPoolCapacity("building") == false)
        assert(engineGetPoolUsedCapacity("ped") == false)
        assert(engineSetPoolCapacity("building", 100) == false)
        assert(engineSetPoolCapacity("ped", 200) == false)
        assert(engineSetAsynchronousLoading(true) == true)
        assert(engineSetAsynchronousLoading(true, nil) == true)
        assert(engineSetAsynchronousLoading() == false)
        assert(engineSetAsynchronousLoading("yes") == false)
        assert(enginePreloadWorldArea(0, 0, 0) == nil)
        assert(enginePreloadWorldArea(0, 0, 0, "models") == nil)
        assert(enginePreloadWorldAreaInDirection(0, 0, 0, 90) == false)
        assert(engineRestream() == nil)
        assert(engineRestream("vehicles") == nil)
        assert(engineRestreamModel(411) == false)
        assert(engineRestreamWorld() == false)
        streamOk, streamErr = pcall(engineGetPoolDefaultCapacity, "nope")
        assert(streamOk == false and streamErr:find("ePools", 1, true))
        streamOk, streamErr = pcall(engineSetPoolCapacity, "building", -1)
        assert(streamOk == false and streamErr:find("out-of-range", 1, true))
        streamOk, streamErr = pcall(enginePreloadWorldArea)
        assert(streamOk == false and streamErr:find("vector3", 1, true))
        streamOk, streamErr = pcall(enginePreloadWorldArea, 0, 0)
        assert(streamOk == false and streamErr:find("Expected number", 1, true))
        streamOk, streamErr = pcall(enginePreloadWorldArea, 0, 0, 0, "nope")
        assert(streamOk == false and streamErr:find("preload-area-option", 1, true))
        streamOk, streamErr = pcall(engineRestream, "nope")
        assert(streamOk == false and streamErr:find("restream-option", 1, true))
        streamOk, streamErr = pcall(engineRestreamModel, 70000)
        assert(streamOk == false and streamErr:find("valid model ID", 1, true))
        assert(math.abs(getPostFXValue("gamma") - 0.95) < 0.0001)
        assert(math.abs(getPostFXValue("brightness") - 1.03) < 0.0001)
        assert(getPostFXValue("contrast") == 1)
        assert(isPostFXEnabled("gamma") == false)
        assert(isPostFXEnabled("contrast") == true)
        streamOk, streamErr = pcall(getPostFXValue, "nope")
        assert(streamOk == false and streamErr:find("postfx-type", 1, true))
        local objects, lowLod, total = engineGetObjectStreamingLimits()
        assert(objects == 400 and lowLod == 400 and total == 1000)
        assert(engineSetObjectStreamingLimits(300, 300) == true)
        objects, lowLod, total = engineGetObjectStreamingLimits()
        assert(objects == 300 and lowLod == 300 and total == 1000)
        streamOk, streamErr = pcall(engineSetObjectStreamingLimits, 1000, 1)
        assert(streamOk == false and streamErr:find("between 0 and 1000", 1, true))
        local lights = engineGetDistantLightStats()
        assert(lights.enabled == false and lights.coronaCapacity == 25000 and lights.drawDistance == 5000 and lights.automaticDrawDistance == false)
        assert(engineSetDistantLightsDrawDistance(299) == false)
        assert(engineSetDistantLightsEnabled(true) == true)
        assert(engineSetDistantLightsAutomaticDrawDistance(true) == true)
        assert(engineRebuildDistantLights() == true)
        lights = engineGetDistantLightStats()
        assert(lights.enabled == true and lights.automaticDrawDistance == true and lights.drawDistance == 5000)
        local renderer = engineGetRendererStats()
        assert(renderer.visibleEntityCapacity == 8192 and renderer.visibleLodCapacity == 8192 and renderer.streamingRwObjectCapacity == 30000)
        assert(renderer.visibleEntities == 0)
        assert(engineResetRendererStats() == true)
        assert(engineGetRadarMapStats().hooksInstalled == false)
        assert(engineResetRadarMapTile(40, 0) == false)
        assert(engineResetRadarMapTile(0, 0) == false)
        streamOk, streamErr = pcall(engineSetDistantLightsEnabled)
        assert(streamOk == false and streamErr:find("bool", 1, true))
        streamOk, streamErr = pcall(engineSetRadarMapTile, 0, 0)
        assert(streamOk == false and streamErr:find("txd", 1, true))
        streamOk, streamErr = pcall(engineResetRadarMapTile)
        assert(streamOk == false and streamErr:find("Expected number", 1, true))
        assert(type(engineGetCullZones()) == "table")
        assert(engineCreateCullZone("attribute", 0, 0, 0, 0, 10, 10, 1) == false)
        assert(engineSetCullZone(1, "attribute", 0, 0, 0, 10, 10, 10, 1) == false)
        assert(engineRemoveCullZone(1) == false)
        assert(engineRestoreCullZone(1) == false)
        streamOk, streamErr = pcall(engineCreateCullZone, "nope")
        assert(streamOk == false and streamErr:find("Expected number", 1, true))
        streamOk, streamErr = pcall(engineCreateCullZone, "nope", 0, 0, 0, 10, 10, 10, 1)
        assert(streamOk == false and streamErr:find("0..65535", 1, true))
        streamOk, streamErr = pcall(engineCreateCullZone, "attribute", 0, 0, 0, 10, 10, 10, 70000)
        assert(streamOk == false and streamErr:find("0..65535", 1, true))
        streamOk, streamErr = pcall(engineGetCullZones, "nope")
        assert(streamOk == false and streamErr:find("attribute", 1, true))
        local loaded, detail = engineLoadVehicleAudioConfig("")
        assert(loaded == false and detail:find("non-empty", 1, true))
        loaded, detail = engineLoadVehicleAudioConfig(1)
        assert(loaded == false and detail:find("non-empty", 1, true))
        loaded, detail = engineLoadVehicleAudioConfig("vehicles.dat")
        assert(loaded == false and detail:find("Unable to load", 1, true) and detail:find("vehicles.dat", 1, true))
        loaded, detail = engineLoadVehicleAudioConfig()
        assert(loaded == false and detail:find("string", 1, true))
        assert(engineReloadVehicleAudioConfig() == false)
        assert(engineUnloadVehicleAudioConfig() == false)
        assert(enginePlayVehicleAudioBackfire() == false)
        assert(getBreakEffectCacheSize() == 0)
        assert(clearBreakEffectCache() == true)
        assert(getBreakEffectFragmentCount() == false)
        assert(getBreakEffectSourceTriangleCount() == false)
        assert(getBreakEffectSleepingFragmentCount() == false)
        assert(getBreakEffectCacheHit() == false)
        assert(isBreakEffectPaused() == false)
        assert(setBreakEffectPaused(nil, true) == false)
        assert(createObjectBreakEffect() == false)
        assert(setObjectBreakProfile() == false)
        assert(getObjectBreakProfile() == false)
        assert(getObjectBreakHealth() == false)
        assert(setObjectBreakHealth() == false)
        assert(resetObjectBreakHealth() == false)
        assert(clearObjectBreakProfile() == false)
        assert(engineLoadCOL("missing.col") == false)
        assert(engineLoadCOL(1) == false)
        assert(engineLoadDFF() == false)
        assert(engineLoadTXD("missing.txd", true) == false)
        assert(engineLoadIFP("missing.ifp") == false)
        assert(engineLoadIFP("missing.ifp", "block") == false)
        assert(engineLoadIMG("missing.img") == nil)
        assert(engineRestoreCOL("Infernus") == false)
        assert(engineRestoreCOL(411) == false)
        assert(engineRestoreModel("Infernus") == false)
        assert(engineRestoreModel(30000) == false)
        assert(engineRequestModel("object") == false)
        assert(engineRequestModel("nope") == false)
        assert(engineFreeModel(411) == false)
        assert(engineFreeModel(19999) == false)
        assert(engineRestoreDFFImage(411) == false)
        assert(engineRestoreTXDImage(0) == false)
        assert(engineRequestTXD("city") == 0)
        assert(engineFreeTXD(1) == false)
        assert(engineReplaceEncryptedModel("car.neonasset", "Infernus") == false)
        assert(engineGetModelTextureNames("Infernus") == false)
        assert(engineGetModelTextureNames() == false)
        assert(type(engineGetVisibleTextureNames()) == "table")
        assert(engineGetVisibleTextureNames("*", "nope") == false)
        local assetOk, assetErr = pcall(engineLoadIMG)
        assert(assetOk == false and assetErr:find("string", 1, true))
        assetOk, assetErr = pcall(engineAddImage)
        assert(assetOk == false and assetErr:find("img", 1, true))
        assetOk, assetErr = pcall(engineImageLinkDFF, nil, "car.dff", 411)
        assert(assetOk == false and assetErr:find("img", 1, true))
        assetOk, assetErr = pcall(engineRestoreDFFImage, 20000)
        assert(assetOk == false and assetErr:find("0-19999", 1, true))
        assetOk, assetErr = pcall(engineRestoreDFFImage, "Infernus")
        assert(assetOk == false and assetErr:find("number", 1, true))
        assetOk, assetErr = pcall(engineRestoreTXDImage, 5000)
        assert(assetOk == false and assetErr:find("0-4999", 1, true))
        assetOk, assetErr = pcall(engineFreeTXD, -1)
        assert(assetOk == false and assetErr:find("destination type range", 1, true))
        assetOk, assetErr = pcall(engineRequestTXD, "abcdefghijklmnopqrstuvwxyz")
        assert(assetOk == false and assetErr:find("24 characters", 1, true))
        assetOk, assetErr = pcall(engineAddClothingTXD)
        assert(assetOk == false and assetErr:find("txd", 1, true))
        assetOk, assetErr = pcall(engineAddClothingModel)
        assert(assetOk == false and assetErr:find("dff", 1, true))
        assetOk, assetErr = pcall(engineGetModelTextures)
        assert(assetOk == false and assetErr:find("string", 1, true))
        assetOk, assetErr = pcall(engineGetModelTextures, "Infernus")
        assert(assetOk == false and assetErr:find("Invalid model ID", 1, true))
        assert(engineFreeModel(20000) == false)
        assert(engineReplaceModel() == false)
        assert(engineReplaceCOL() == false)
        assert(engineImportTXD() == false)
        assert(engineReplaceAnimation() == false)
        assert(engineRestoreAnimation() == false)
        assert(engineApplyShaderToWorldTexture() == false)
        assert(engineRemoveShaderFromWorldTexture() == false)
        local map = engineParseSAMPMap("CreateObject(1337, 1, 2, 3, 0, 0, 90)")
        assert(map.success == true and map.objectCount == 1 and map.errorCount == 0)
        assert(map.objects[1].creationType == "object" and map.objects[1].model == 1337)
        assert(map.objects[1].x == 1 and map.objects[1].y == 2 and map.objects[1].z == 3 and map.objects[1].rz == 90)
        assert(map.removedBuildingCount == 0)
        local broken = engineParseSAMPMap("CreateObject(1)")
        assert(broken.success == false and broken.errorCount >= 1 and broken.diagnostics[1].severity == "error")
        assetOk, assetErr = pcall(engineParseSAMPMap)
        assert(assetOk == false and assetErr:find("string", 1, true))
    )LUA");
}

void CheckObjects(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG;
    RunLua(vm, "object-outside-resource", R"LUA(
        assert(createObject(1337, 0, 0, 0) == false, "createObject needs a resource")
        assert(isObjectBreakable(625) == true and isObjectBreakable(1337) == false, "breakable list")
        assert(isObjectBreakable(0) == false and isObjectBreakable(20000) == false, "breakable range")
        assert(pcall(isObjectMoving) == false, "isObjectMoving argument")
    )LUA");
    Net::ResourceStartInfo info;
    info.name = "objects";
    info.resourceId = 91;
    info.entityId = 0x3910;
    info.dynamicEntityId = 0x3911;
    info.noClientCacheScripts = 1;
    info.startCounter = 1;
    Script::ActiveVmOnResourceStart(info);
    const std::vector<uint8_t> body = NoCacheScriptBody(91, R"LUA(
        assert(createObject(0, 0, 0, 0) == false and createObject(400, 0, 0, 0) == false, "ped and vehicle models")
        assert(createObject(300, 0, 0, 0) == false and createObject(1337) == false, "cutscene and short args")
        local obj = createObject(1337, 1, 2, 3, 0, 90, 0, true)
        assert(isElement(obj) and getElementType(obj) == "object", "object element")
        local x, y, z = getElementPosition(obj)
        assert(x == 1 and y == 2 and z == 3, "create position")
        assert(setElementPosition(obj, 8, 9, 10) == true, "set position")
        x, y, z = getElementPosition(obj)
        assert(x == 8 and y == 9 and z == 10, "position stored")
        local sx, sy, sz = getObjectScale(obj)
        assert(sx == 1 and sy == 1 and sz == 1, "default scale")
        assert(setObjectScale(obj, 2) == true, "uniform scale")
        sx, sy, sz = getObjectScale(obj)
        assert(sx == 2 and sy == 2 and sz == 2, "uniform read")
        assert(setObjectScale(obj, 2, 1) == true, "partial scale")
        sx, sy, sz = getObjectScale(obj)
        assert(sx == 2 and sy == 1 and sz == 2, "z follows x")
        local child = createObject(1337, 0, 0, 0)
        assert(setElementParent(child, obj) == true, "parent")
        assert(setObjectScale(obj, 3, 3, 3) == true, "parent scale")
        sx = getObjectScale(child)
        assert(sx == 3, "child scale")
        assert(setElementCallPropagationEnabled(obj, false) == true, "propagation off")
        assert(setObjectScale(obj, 4) == true, "parent only")
        assert(getObjectScale(child) == 3, "child stays")
        assert(getObjectMass(obj) == -1, "default mass")
        assert(setObjectMass(obj, -1) == false and getObjectMass(obj) == -1, "negative mass")
        assert(setObjectMass(obj, 12) == true and getObjectMass(obj) == 12, "mass")
        assert(setObjectProperty(obj, "all", 1) == false, "property all")
        assert(setObjectProperty(obj, "turn_mass", 4) == true, "turn mass")
        assert(setObjectProperty(obj, "center_of_mass", 1, 2, 3) == true, "center")
        local cx, cy, cz = getObjectProperty(obj, "center_of_mass")
        assert(cx == 1 and cy == 2 and cz == 3, "center read")
        local props = getObjectProperty(obj, "all")
        assert(props.mass == 12 and props.turn_mass == 4 and props.center_of_mass.x == 1, "property table")
        assert(getObjectProperty(obj, "nope") == false, "bad property")
        assert(isObjectStatic(obj) == false and setObjectStatic(obj, true) == true and isObjectStatic(obj) == true, "static")
        assert(isObjectBreakable(obj) == false, "bin is not breakable")
        local glass = createObject(625, 0, 0, 0)
        assert(isObjectBreakable(glass) == true, "breakable object")
        assert(setObjectBreakable(glass, true) == false, "already breakable")
        assert(setObjectBreakable(glass, false) == true and isObjectBreakable(glass) == false, "disable break")
        assert(breakObject(glass) == false, "no CObject")
        assert(isObjectRespawnable(glass) == true, "respawn default")
        assert(toggleObjectRespawn(glass, false) == true and isObjectRespawnable(glass) == false, "respawn off")
        assert(respawnObject(glass) == true and respawnObject(glass) == false, "respawn once")
        assert(moveObject(obj, 0, 5, 6, 7) == true and isObjectMoving(obj) == false, "instant move")
        x, y, z = getElementPosition(obj)
        assert(x == 5 and y == 6 and z == 7, "moved")
        assert(moveObject(obj, 60000, 1, 1, 1) == true and isObjectMoving(obj) == true, "moving")
        assert(stopObject(obj) == true and isObjectMoving(obj) == false, "stopped")
        assert(setObjectMaterial(obj, 0, -1, "txd", "tex") == true, "color material")
        assert(removeObjectMaterial(obj, 0) == true, "remove material")
        assert(setObjectMaterial(obj, 0, 1337, "txd", "tex") == false, "texture miss")
        assert(setObjectMaterial(obj, 16, -1, "a", "b") == false, "slot")
        local tag = createObject(1490, 0, 0, 0)
        assert(acquireObjectGangTag(obj) == false, "not a tag")
        assert(acquireObjectGangTag(tag) == true and getObjectGangTagProgress(tag) == 0, "acquire")
        assert(setObjectGangTagProgress(tag, 10) == true and getObjectGangTagProgress(tag) == 10, "progress")
        assert(setObjectGangTagProgress(tag, 300) == false, "progress range")
        assert(setObjectGangTagAlpha(tag, 128) == false, "no tag renderer")
        assert(releaseObjectGangTag(tag) == true and getObjectGangTagProgress(tag) == false, "release")
        assert(destroyElement(child) == true and isElement(child) == false, "destroy")
        _G.obj = obj
        _G.objectOk = true
    )LUA");
    CHECK(Script::ActiveVmFeedScripts(body.data(), body.size(), body.size() * 8));
    Script::ActiveVmPulseResources();
    RunLua(vm, "object-created", "assert(_G.objectOk == true and getElementType(_G.obj) == 'object', 'object script')");
    Net::BitStreamWriter stop;
    stop.Write(static_cast<uint16_t>(91));
    Script::ActiveVmOnResourceStop(stop.GetData(), stop.GetByteLength(), stop.GetBitLength());
    RunLua(vm, "object-resource-stop", "assert(isElement(_G.obj) == false, 'object left with the resource')");
}

void CheckWater(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG;
    RunLua(vm, "water-outside-resource",
           "assert(createWater(0, 0, 0, 1, 0, 0, 0, 1, 0) == false, 'createWater needs a resource')");
    Net::ResourceStartInfo info;
    info.name = "water";
    info.resourceId = 92;
    info.entityId = 0x3920;
    info.dynamicEntityId = 0x3921;
    info.noClientCacheScripts = 1;
    info.startCounter = 1;
    Script::ActiveVmOnResourceStart(info);
    const std::vector<uint8_t> body = NoCacheScriptBody(92, R"LUA(
        local bad = createWater(0, 0, 0, 1, 0, 0, 0, 1, 0, 9)
        assert(bad == false, "flag")
        local w = createWater(0, 0, 5, 10, 0, 5, 0, 10, 5)
        assert(isElement(w) and getElementType(w) == "water", "water element")
        assert(getWaterLevel(w) == 5, "average level")
        local x, y, z = getWaterVertexPosition(w, 1)
        assert(x == 0 and y == 0 and z == 5, "vertex 1")
        assert(getWaterVertexPosition(w, 0) == false and getWaterVertexPosition(w, 4) == false, "vertex range")
        assert(setWaterVertexPosition(w, 2, 10, 0, 7) == true, "move vertex")
        x, y, z = getWaterVertexPosition(w, 2)
        assert(x == 10 and y == 0 and z == 7, "vertex 2")
        local surface = getWaterLevel(1, 1, 5)
        assert(type(surface) == "number", "point on the triangle")
        assert(getWaterLevel(50, 50, 0) == false, "no polygon")
        local hit, hx, hy, hz = testLineAgainstWater(1, 1, 20, 1, 1, -10)
        assert(hit == true and hx == 1 and hy == 1 and hz > 4 and hz < 8, "line hits the triangle")
        assert(testLineAgainstWater(50, 50, 10, 50, 50, -10) == false, "line misses inside the map")
        local sea, sx, sy, sz = testLineAgainstWater(-4000, -4000, 10, -4000, -4000, -10)
        assert(sea == true and sz == 0, "outside ocean")
        assert(setWaterLevel(w, 2) == true and getWaterLevel(w) == 2, "element level")
        assert(setWaterLevel(4, true, false) == true and getWaterLevel(w) == 2, "world level leaves the polygon")
        assert(setWaterLevel(6) == true and getWaterLevel(w) == 6, "world level includes polygons")
        assert(setWaterLevel(50, 50, 0, 1) == false, "no polygon at the point")
        assert(resetWaterLevel() == true, "reset")
        _G.water = w
        _G.waterOk = true
    )LUA");
    CHECK(Script::ActiveVmFeedScripts(body.data(), body.size(), body.size() * 8));
    Script::ActiveVmPulseResources();
    RunLua(vm, "water-created", "assert(_G.waterOk == true and getElementType(_G.water) == 'water', 'water script')");
    Net::BitStreamWriter stop;
    stop.Write(static_cast<uint16_t>(92));
    Script::ActiveVmOnResourceStop(stop.GetData(), stop.GetByteLength(), stop.GetBitLength());
    RunLua(vm, "water-resource-stop", "assert(isElement(_G.water) == false, 'water left with the resource')");
}

void CheckFires(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG;
    RunLua(vm, "fire-outside-resource", R"LUA(
        assert(createFire(0, 0, 1, 2) == true and createFire("no") == false, "numeric fire")
        assert(createFire(0, 0, 0, {}) == false, "managed fire needs a resource")
    )LUA");
    Net::ResourceStartInfo info;
    info.name = "fires";
    info.resourceId = 93;
    info.entityId = 0x3930;
    info.dynamicEntityId = 0x3931;
    info.noClientCacheScripts = 1;
    info.startCounter = 1;
    Script::ActiveVmOnResourceStart(info);
    const std::vector<uint8_t> body = NoCacheScriptBody(93, R"LUA(
        local fire = createFire(1, 2, 3, {
            duration = 1000, strength = 2, damage = false, spread = true, maxGenerations = 3,
            damageTargets = {players = false}
        })
        assert(isElement(fire) and getElementType(fire) == "fire", "fire element")
        assert(getFireDuration(fire) == 1000 and getFireRemainingTime(fire) == 1000, "duration")
        assert(getFireStrength(fire) == 2, "strength")
        assert(isFireDamageEnabled(fire) == false and isFireSpreadEnabled(fire) == true, "flags")
        assert(getFireMaxGenerations(fire) == 3, "generations")
        local targets = getFireDamageTargets(fire)
        assert(targets.players == false and targets.peds == true and targets.vehicles == true and targets.objects == true, "mask")
        assert(setFireDuration(fire, 50) == true and getFireDuration(fire) == 50 and getFireRemainingTime(fire) == 50, "set duration")
        assert(setFireDuration(fire, -1) == false and setFireStrength(fire, 0) == false, "rejected writes")
        assert(setFireStrength(fire, 0.5) == true and getFireStrength(fire) == 0.5, "set strength")
        assert(setFireRemainingTime(fire, 10) == true and getFireRemainingTime(fire) == 10, "remaining")
        assert(setFireMaxGenerations(fire, 300) == false and setFireMaxGenerations(fire, 4) == true, "generation range")
        assert(setFireDamageEnabled(fire, true) == true and isFireDamageEnabled(fire) == true, "damage")
        assert(setFireSpreadEnabled(fire, false) == true and isFireSpreadEnabled(fire) == false, "spread")
        assert(setFireDamageTargets(fire, {objects = false}) == true, "set mask")
        targets = getFireDamageTargets(fire)
        assert(targets.objects == false and targets.players == true, "mask read")
        assert(getFireSource(fire) == false and setFireSource(fire, fire) == true, "source")
        assert(getFireSource(fire).id == fire.id, "source id")
        assert(setFireTarget(fire, nil) == true and getFireTarget(fire) == false, "clear target")
        assert(extinguishFire(fire) == true and isElement(fire) == false and getFireDuration(fire) == false, "extinguish element")
        assert(extinguishFire() == true and extinguishFire(10, 10, 10, 2) == true, "native extinguish")
        local defaults = createFire(0, 0, 0, {})
        assert(getFireDuration(defaults) == 5000 and getFireStrength(defaults) == 1, "defaults")
        assert(isFireDamageEnabled(defaults) == true and isFireSpreadEnabled(defaults) == false, "default flags")
        _G.fireOk = true
        _G.fire = defaults
    )LUA");
    CHECK(Script::ActiveVmFeedScripts(body.data(), body.size(), body.size() * 8));
    Script::ActiveVmPulseResources();
    RunLua(vm, "fire-created", "assert(_G.fireOk == true and getElementType(_G.fire) == 'fire', 'fire script')");
    Net::BitStreamWriter stop;
    stop.Write(static_cast<uint16_t>(93));
    Script::ActiveVmOnResourceStop(stop.GetData(), stop.GetByteLength(), stop.GetBitLength());
    RunLua(vm, "fire-resource-stop", "assert(isElement(_G.fire) == false, 'fire left with the resource')");
}

void CheckFx(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "fx-particles", R"LUA(
        assert(fxAddBlood(0, 0, 0, 0, 0, 1) == true and fxAddBlood(0, 0, 0, 0, 0, 1, 4, 0.5) == true, "blood")
        assert(fxAddBlood() == false and fxAddWood("no") == false, "fx arguments")
        assert(fxAddWood(0, 0, 0, 1, 0, 0) == true, "wood")
        assert(fxAddSparks(0, 0, 0, 0, 0, 1) == true, "sparks")
        assert(fxAddSparks(0, 0, 0, 0, 0, 1, 2, 3, 0, 1, 0, false, 0.2, 0.4) == true, "sparks full")
        assert(fxAddTyreBurst(0, 0, 0, 0, 0, 1) == true and fxAddPunchImpact(0, 0, 0, 1, 0, 0) == true, "burst and punch")
        assert(fxAddBulletImpact(0, 0, 0, 0, 1, 0, 2, 3, 0.5) == true, "bullet")
        assert(fxAddDebris(0, 0, 0) == true and fxAddGlass(0, 0, 0, 1, 2, 3, 4, 0.5, 2) == true, "debris and glass")
        assert(fxAddWaterHydrant(0, 0, 0) == true and fxAddWaterSplash(1, 2, 3) == true, "water fx")
        assert(fxAddBulletSplash(0, 0, 0) == true and fxAddFootSplash(0, 0, 0) == true, "splashes")
        assert(fxAddGunshot(0, 0, 0, 0, 0, 1, false) == true and fxAddTankFire(0, 0, 0, 1, 0, 0) == true, "gunshot and tank")
        assert(fxAddFootSplash() == false, "splash arguments")
        assert(fxCreateParticle("blood", 0, 0, 0, 0, 0, 1, 255, 0, 0, 255) == true, "particle")
        assert(fxCreateParticle("glass", 0, 0, 0, 1, 0, 0, 1, 1, 1, 1, true, 2, 0.5, 0.2, false, 3) == true, "particle options")
        local ok = pcall(fxCreateParticle, "nope", 0, 0, 0, 0, 0, 1, 1, 1, 1, 1)
        assert(ok == false, "unknown particle")
    )LUA");
}

void CheckWorldQueries(WorldRPG::Script::LuaVMStub& vm) {
    using namespace WorldRPG;
    RunLua(vm, "world-query-outside", R"LUA(
        assert(getZoneName(-2100, -2400, 10) == "Angel Pine", "zone")
        assert(getZoneName(-2100, -2400, 10, true) == "Whetstone", "city")
        assert(getZoneName(1500, -1700, 15) == "Pershing Square", "square")
        assert(getZoneName(1500, -1700, 15, true) == "Los Santos", "los santos")
        assert(getZoneName(0, 0, 10) == "Blueberry Acres" and getZoneName(0, 0, 10, true) == "Red County", "county")
        assert(getZoneName(4000, 0, 0) == "Unknown", "outside map")
        assert(getZoneName() == false and createExplosion() == false, "missing arguments")
        assert(createExplosion(0, 0, 0, 4) == true and createSWATRope(0, 0, 5) == true, "explosion and rope")
        assert(createBuilding(1337, 0, 0, 3) == false, "building needs a resource")
        assert(getGaragePosition(0) == false and getGarageSize(1) == false and getGarageBoundingBox(2) == false, "no garage pool")
        assert(removeWorldModel(1337, 10, 0, 0, 0) == false, "removal needs a resource")
        assert(removeGameWorld() == true and restoreGameWorld() == true, "world flag")
        local badModel, badPos = pcall(createBuilding, 400, 0, 0, 0), false
        assert(badModel == false, "vehicle is not a building")
    )LUA");
    Net::ResourceStartInfo info;
    info.name = "worldq";
    info.resourceId = 94;
    info.entityId = 0x3940;
    info.dynamicEntityId = 0x3941;
    info.noClientCacheScripts = 1;
    info.startCounter = 1;
    Script::ActiveVmOnResourceStart(info);
    const std::vector<uint8_t> body = NoCacheScriptBody(94, R"LUA(
        local building = createBuilding(1337, 0, 0, 3)
        assert(isElement(building) and getElementType(building) == "building", "building")
        local ok = pcall(createBuilding, 1337, 10000, 0, 0)
        assert(ok == false, "outside the world")
        local removed, count = removeWorldModel(1337, 10, 0, 0, 0)
        assert(removed == true and count == 0, "no building list")
        removed, count = restoreWorldModel(1337, 10, 0, 0, 0)
        assert(removed == true and count == 0, "restore one")
        removed, count = restoreAllWorldModels()
        assert(removed == true and count == 0, "restore all")
        assert(removeAllGameBuildings() == true and restoreAllGameBuildings() == true, "deprecated world")
        assert(acquireGarageControl(0) == false and releaseGarageControl(0) == false, "garage lease")
        _G.building = building
        _G.worldQueryOk = true
    )LUA");
    CHECK(Script::ActiveVmFeedScripts(body.data(), body.size(), body.size() * 8));
    Script::ActiveVmPulseResources();
    RunLua(vm, "world-query-created", "assert(_G.worldQueryOk == true and getElementType(_G.building) == 'building', 'query script')");
    Net::BitStreamWriter stop;
    stop.Write(static_cast<uint16_t>(94));
    Script::ActiveVmOnResourceStop(stop.GetData(), stop.GetByteLength(), stop.GetBitLength());
    RunLua(vm, "world-query-stop", "assert(isElement(_G.building) == false, 'building left with the resource')");
}

void CheckSight(WorldRPG::Script::LuaVMStub& vm) {
    RunLua(vm, "sight-and-paths", R"LUA(
        assert(pcall(getRoofPosition) == false, "roof arguments")
        assert(getRoofPosition(0, 0, 10) == false, "no roof")
        assert(processLineOfSight(0, 0, 0, 1, 1, 1) == false, "no line hit")
        assert(processLineOfSight() == false, "line arguments")
        assert(isLineOfSightClear(0, 0, 0, 1, 0, 0) == false, "sight query did not run")
        assert(getScreenFromWorldPosition(0, 0, 0) == false, "no screen projection")
        assert(getWorldFromScreenPosition(0, 0, 1) == false, "no world projection")
        local me = {id = localPlayer.id, element = true}
        assert(processLineAgainstMesh(me, 0, 0, 0, 1, 0, 0) == false, "no mesh")
        assert(pcall(processLineAgainstMesh) == false, "mesh arguments")
        local hit = testSphereAgainstWorld(0, 0, 0, 2)
        assert(hit == false, "sphere miss")
        local graph, graphError = engineGetPedPathGraphRevision()
        assert(graph == false and graphError == "no-patches", "graph revision")
        local patch, patchError = engineGetPedPathPatchRevision()
        assert(patch == false and patchError == "no-patch", "patch revision")
        assert(engineUnloadPedPathPatch() == false, "unload")
        local ok, err = pcall(updateAmbientPedPopulationModels)
        assert(not ok and err:find("Expected vector3 at argument 1, got none", 1, true), "update arguments: " .. tostring(err))
        ok, err = pcall(updateAmbientPedPopulationModels, "no")
        assert(not ok and err:find('Expected vector3 at argument 1, got string ("no")', 1, true), "update string: " .. tostring(err))
        ok, err = pcall(updateAmbientPedPopulationModels, 1, 2)
        assert(not ok and err:find("Expected vector3 at argument 1, got number (1)", 1, true), "update short: " .. tostring(err))
        ok, err = pcall(updateAmbientPedPopulationModels, 0, 0, 0/0)
        assert(not ok and err:find("Expected number at argument 3, got NaN", 1, true), "update nan: " .. tostring(err))
        ok, err = pcall(updateAmbientPedPopulationModels, 0, math.huge, 0)
        assert(not ok and err:find("Expected number at argument 2, got inf", 1, true), "update inf: " .. tostring(err))
        assert(updateAmbientPedPopulationModels(1, 2, 3) == false, "no navigation zone")
        assert(updateAmbientPedPopulationModels("4", "5", "6") == false, "numeric strings")
        assert(resetAmbientPedPopulationModels() == true, "reset")
        assert(resetAmbientPedPopulationModels("extra") == true, "reset ignores extra")
        assert(isWorldRPGMobilePopulation() == true, "phone population flag")
        local isMobileClient = (type(isWorldRPGMobilePopulation) == "function" and isWorldRPGMobilePopulation() == true) or
            type(resetAmbientPedPopulationZonesToBootstrap) ~= "function" or
            type(setAmbientPedPopulationZoneState) ~= "function"
        assert(isMobileClient == true, "phone detector stays on the mobile path")
        assert(type(resetAmbientPedPopulationZonesToBootstrap) == "function", "bootstrap is bound")
        assert(resetAmbientPedPopulationZonesToBootstrap() == false, "bootstrap has no zone array")
        local profile, profileReason = getAmbientPedPopulationProfile()
        assert(profile == false and profileReason == "inactive", "profile inactive")
        local spawned, spawnReason = getAmbientPedSpawnCandidate(0, 0, 0, "civilian", -1)
        assert(spawned == false and spawnReason == "invalid-origin", "spawn multipliers")
        assert(getAmbientPedSpawnCandidate(0, 0, 0, "nope", -1) == false, "spawn selection")
        assert(getAmbientPedSpawnCandidate(0, 0, 0, "civilian", 1) == false, "spawn gang id")
        local grouped, groupReason = getAmbientPedGangGroupCandidate(0, 0, 0, 1, 3)
        assert(grouped == false and groupReason == "invalid-origin", "gang group")
        assert(getAmbientPedGangGroupCandidate(0, 0, 0, 9, 3) == false, "gang range")
        local couple, coupleReason = getAmbientPedCivilianCoupleCandidate(0, 0, 0)
        assert(couple == false and coupleReason == "invalid-origin", "couple version")
        local vehicleModel, vehicleModelReason = getAmbientVehicleModelCandidate()
        assert(vehicleModel == false and vehicleModelReason == "population-unavailable", "vehicle model")
        assert(getAmbientVehicleOccupantModelCandidate(410, 4) == false, "occupant model info")
        assert(getAmbientVehicleOccupantModelCandidate(410, 0) == false, "occupant count")
        local vehicleSpawn, vehicleSpawnReason = getAmbientVehicleSpawnCandidate(0, 0, 0, 410)
        assert(vehicleSpawn == false and vehicleSpawnReason == "unsupported-model", "vehicle spawn")
        assert(isAmbientPedSphereVisible(0, 0, 0, 5) == false, "sphere camera")
        assert(isAmbientPedSphereVisible(0, 0, 0, 101) == false, "sphere radius")
        assert(pcall(isAmbientPedSphereVisible) == false, "sphere arguments")
        assert(setAmbientPedPopulationZoneState() == false, "zone arguments")
        assert(setAmbientPedPopulationZoneState("MARKST") == false, "zone table")
        assert(setAmbientPedPopulationZoneState("MARKST", {populationType = 20}) == false, "population type")
        assert(setAmbientPedPopulationZoneState("MARKST", {populationType = 1.5}) == false, "fraction")
        assert(setAmbientPedPopulationZoneState("MARKST", {noCops = 1}) == false, "noCops type")
        assert(setAmbientPedPopulationZoneState("MARKST", {gangStrengths = "x"}) == false, "gang table")
        assert(setAmbientPedPopulationZoneState("MARKST", {gangStrengths = {256}}) == false, "gang range")
        assert(setAmbientPedPopulationZoneState("nope", {}) == false, "label case")
        assert(setAmbientPedPopulationZoneState("", {}) == false, "empty label")
        assert(setAmbientPedPopulationZoneState("TOOLONG1", {}) == false, "long label")
        assert(setAmbientPedPopulationZoneState("ZZZZ", {}) == false, "unknown label")
        assert(setAmbientPedPopulationZoneState("MARKST", {}) == false, "known label, no zone array")
        assert(setAmbientPedPopulationZoneState("MARKST", {
            populationType = 14, races = 15, dealerStrength = 0, noCops = false,
            gangStrengths = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        }) == false, "full state")
        local loaded, loadError = engineLoadPedPathPatch("paths/none.nped")
        assert(loaded == false and type(loadError) == "string", "load")
        loaded, loadError = engineLoadPedPathPatch()
        assert(loaded == false and loadError == "invalid-resource-path", "load arguments")
    )LUA");
}

int main(int argc, char** argv) {
    CheckPacketLayouts();
    CheckPedEntityBody();
    CheckPlayerListCapture();

    // RESOURCE_START sets the root the phone reports for the resource (shim layout included).
    WorldRPG::Net::ResourceStartInfo root;
    root.name = "rootcheck";
    root.entityId = 0x0ABC;
    root.dynamicEntityId = 0x0ABD;
    WorldRPG::ServerWorld::OnResourceStart(root);
    CHECK(WorldRPG::ServerWorld::LastResourceRoot() == 0x0ABC);

    WorldRPG::Script::LuaVMStub vm;
    CHECK(vm.initialize());
    // Host has no libGTASA. Bad arguments return false (desktop CLuaWorldDefs::GetGroundPosition).
    // A valid triple also returns false here, because FindGroundZFor3DPosition cannot run.
    RunLua(vm, "get-ground-position",
           "assert(getGroundPosition() == false, \"no args\")\n"
           "assert(getGroundPosition({}) == false, \"table\")\n"
           "assert(getGroundPosition(1, 2) == false, \"short\")\n"
           "assert(getGroundPosition(1, 2, \"zz\") == false, \"bad z\")\n"
           "assert(getGroundPosition(0/0, 1, 2) == false, \"nan\")\n"
           "assert(getGroundPosition(1, \"2\", 3) == false, \"host has no world\")\n"
           "assert(getGroundPosition(10, 20, 30) == false, \"host triple\")");
    WorldRPG::ServerWorld::SetLocalPlayer(777);
    WorldRPG::ServerWorld::NotePlayerNick(777, "Sam");
    CheckCompatibilityAliases(vm);
    CheckEngineModelNames(vm);

    CheckUiViewport(vm);
    CheckJson(vm, argc > 1 ? argv[1] : nullptr);
    CheckLowRiskLuaQueries(vm);
    CheckHelpers(vm);
    CheckAudioQueries(vm);
    CheckWeaponSnapshots(vm);
    CheckGui(vm);
    CheckElementsGuiDx(vm);
    CheckDxTextLayout(vm);
    CheckMemoInput(vm);
    CheckMtaWindow(vm);
    CheckCoreClientCalls(vm);
    CheckDxDrawing(vm);
    CheckSvg(vm);
    CheckStopLifecycle(vm);
    CheckCustomWeapons(vm);
    CheckObjects(vm);
    CheckWater(vm);
    CheckFires(vm);
    CheckFx(vm);
    CheckWorldQueries(vm);
    CheckSight(vm);
    CheckCrypto(vm);
    CheckDxImage(vm);
    CheckGuiFonts(vm);
    CheckNetworkCalls(vm);
    CheckModelDownloads(vm);
    CheckPedBodyState(vm);

    if (argc > 1) {
        RunLua(vm, "catalog-count", "assert(_G.catalogModels == 1435, 'catalog holds 1435 models')");
    }
    RunLua(vm, "sound-before-session-reset", "_G.sessionSound = playSound('https://example/session.ogg')");
    lua_State* L = WorldRPG::Script::ActiveVmState();
    lua_getglobal(L, "sessionSound");
    lua_getfield(L, -1, "id");
    const uint32_t oldSound = static_cast<uint32_t>(lua_tointeger(L, -1));
    lua_pop(L, 2);
    WorldRPG::Script::ActiveVmResetSession();
    RunLua(vm, "sound-after-session-reset", "_G.sessionSound = playSound('https://example/new-session.ogg')");
    WorldRPG::Script::ActiveVmNoteSoundEnded(oldSound);
    WorldRPG::Script::ActiveVmNoteSoundLength(oldSound, 50);
    WorldRPG::Script::ActiveVmPump();
    RunLua(vm, "sound-stale-session-callbacks",
           "assert(isElement(sessionSound) and getSoundLength(sessionSound) == 0)");

    std::printf("neon_resource_tests: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
