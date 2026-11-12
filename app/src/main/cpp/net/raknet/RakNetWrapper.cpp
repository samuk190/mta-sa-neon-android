#include "RakNetWrapper.h"

#include <cstring>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <atomic>
#include <vector>

#include "../mta/MtaClientTransport.h"

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "WorldRPGNeon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Net {

// ---------------------------------------------------------------------------
// ASCII Normalization
// ---------------------------------------------------------------------------
std::string NormalizeAscii(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (unsigned char ch : input) {
        // Map common UTF-8 accented characters to ASCII equivalents
        // Since we receive already-decoded bytes, handle direct Latin-1 range
        if (ch >= 0xC0 && ch <= 0xC5) { result += 'A'; continue; } // A variants
        if (ch == 0xC7) { result += 'C'; continue; }
        if (ch >= 0xC8 && ch <= 0xCB) { result += 'E'; continue; }
        if (ch >= 0xCC && ch <= 0xCF) { result += 'I'; continue; }
        if (ch == 0xD1) { result += 'N'; continue; }
        if (ch >= 0xD2 && ch <= 0xD6) { result += 'O'; continue; }
        if (ch >= 0xD9 && ch <= 0xDC) { result += 'U'; continue; }
        if (ch >= 0xE0 && ch <= 0xE5) { result += 'a'; continue; }
        if (ch == 0xE7) { result += 'c'; continue; }
        if (ch >= 0xE8 && ch <= 0xEB) { result += 'e'; continue; }
        if (ch >= 0xEC && ch <= 0xEF) { result += 'i'; continue; }
        if (ch == 0xF1) { result += 'n'; continue; }
        if (ch >= 0xF2 && ch <= 0xF6) { result += 'o'; continue; }
        if (ch >= 0xF9 && ch <= 0xFC) { result += 'u'; continue; }
        // Skip continuation bytes of multi-byte UTF-8 sequences
        if (ch >= 0x80 && ch <= 0xBF) continue;
        // Replace bullet and special markers
        if (ch == 0xE2) continue; // Start of bullet/em-dash sequence
        if (ch == 0x80) continue;
        if (ch == 0xA2) { result += '*'; continue; } // bullet -> *
        if (ch == 0x93 || ch == 0x94) { result += '"'; continue; } // smart quotes
        if (ch == 0x91 || ch == 0x92) { result += '\''; continue; }
        if (ch == 0x96 || ch == 0x97) { result += '-'; continue; } // en/em dash
        result += static_cast<char>(ch);
    }
    return result;
}

// ---------------------------------------------------------------------------
// BitStreamWriter Implementation
// ---------------------------------------------------------------------------
struct BitStreamWriter::Impl {
    std::vector<uint8_t> buffer;
    size_t bitOffset = 0;

    void EnsureBits(size_t bitsNeeded) {
        size_t bytesNeeded = (bitOffset + bitsNeeded + 7) / 8;
        if (bytesNeeded > buffer.size()) {
            buffer.resize(bytesNeeded, 0);
        }
    }

    // MTA/RakNet wire bit order, cross-validated against the production-proven
    // reader in backend_mta/scripts/mobile_handshake.hpp and RakNet
    // BitStream::WriteBits(rightAlignedBits=true):
    //  - stream packs bits MSB-first within each byte;
    //  - full source bytes flow as-is (little-endian integers stay LE);
    //  - a trailing partial byte is right-aligned: its low (numBits%8) bits
    //    are written, most significant of them first.
    void WriteBitsInternal(const uint8_t* data, size_t numBits) {
        EnsureBits(numBits);
        const size_t fullBytes = numBits / 8;
        const size_t rem = numBits % 8;
        size_t k = 0;
        auto put = [&](bool bit) {
            const size_t dstByte = (bitOffset + k) / 8;
            const size_t dstBit = 7 - ((bitOffset + k) % 8);
            if (bit)
                buffer[dstByte] |= (1u << dstBit);
            else
                buffer[dstByte] &= ~(1u << dstBit);
            ++k;
        };
        for (size_t b = 0; b < fullBytes; ++b)
            for (size_t j = 0; j < 8; ++j)
                put((data[b] >> (7 - j)) & 1);
        if (rem > 0)
            for (size_t j = 0; j < rem; ++j)
                put((data[fullBytes] >> (rem - 1 - j)) & 1);
        bitOffset += numBits;
    }
};

BitStreamWriter::BitStreamWriter() : pImpl_(std::make_unique<Impl>()) {}
BitStreamWriter::~BitStreamWriter() = default;

void BitStreamWriter::Write(uint8_t value) {
    pImpl_->WriteBitsInternal(&value, 8);
}

void BitStreamWriter::Write(uint16_t value) {
    uint8_t bytes[2];
    memcpy(bytes, &value, 2);
    pImpl_->WriteBitsInternal(bytes, 16);
}

void BitStreamWriter::Write(uint32_t value) {
    uint8_t bytes[4];
    memcpy(bytes, &value, 4);
    pImpl_->WriteBitsInternal(bytes, 32);
}

void BitStreamWriter::Write(int32_t value) {
    uint8_t bytes[4];
    memcpy(bytes, &value, 4);
    pImpl_->WriteBitsInternal(bytes, 32);
}

void BitStreamWriter::Write(float value) {
    uint8_t bytes[4];
    memcpy(bytes, &value, 4);
    pImpl_->WriteBitsInternal(bytes, 32);
}

void BitStreamWriter::Write(const char* str) {
    if (!str) {
        Write(static_cast<uint16_t>(0));
        return;
    }
    std::string normalized = NormalizeAscii(str);
    uint16_t len = static_cast<uint16_t>(std::min(normalized.size(), size_t{512}));
    Write(len);
    pImpl_->WriteBitsInternal(reinterpret_cast<const uint8_t*>(normalized.data()), len * 8);
}

void BitStreamWriter::WriteBytes(const uint8_t* data, size_t length) {
    if (data && length > 0) {
        pImpl_->WriteBitsInternal(data, length * 8);
    }
}

void BitStreamWriter::WriteBits(const uint8_t* data, size_t bitCount) {
    if (data && bitCount > 0) {
        pImpl_->WriteBitsInternal(data, bitCount);
    }
}

const uint8_t* BitStreamWriter::GetData() const {
    return pImpl_->buffer.data();
}

size_t BitStreamWriter::GetByteLength() const {
    return (pImpl_->bitOffset + 7) / 8;
}

size_t BitStreamWriter::GetBitLength() const {
    return pImpl_->bitOffset;
}

void BitStreamWriter::Reset() {
    pImpl_->buffer.clear();
    pImpl_->bitOffset = 0;
}

// ---------------------------------------------------------------------------
// BitStreamReader Implementation
// ---------------------------------------------------------------------------
struct BitStreamReader::Impl {
    const uint8_t* data;
    size_t bitLength;
    size_t readBitOffset = 0;

    // Exact mirror of WriteBitsInternal (RakNet ReadBits with
    // alignBitsToRight=true): full output bytes fill MSB-first; a trailing
    // partial byte receives the stream bits right-aligned (first stream bit
    // lands at bit rem-1).
    bool ReadBitsInternal(uint8_t* out, size_t numBits) {
        const size_t fullBytes = numBits / 8;
        const size_t rem = numBits % 8;
        if (numBits > bitLength - readBitOffset) return false;
        auto get = [&](size_t k) {
            const size_t pos = readBitOffset + k;
            return (data[pos / 8] >> (7 - (pos % 8))) & 1;
        };
        size_t k = 0;
        for (size_t b = 0; b < fullBytes; ++b) {
            uint8_t byte = 0;
            for (size_t j = 0; j < 8; ++j) byte |= static_cast<uint8_t>(get(k++) << (7 - j));
            out[b] = byte;
        }
        if (rem > 0) {
            uint8_t byte = 0;
            for (size_t j = 0; j < rem; ++j) byte |= static_cast<uint8_t>(get(k++) << (rem - 1 - j));
            out[fullBytes] = byte;
        }
        readBitOffset += numBits;
        return true;
    }
};

BitStreamReader::BitStreamReader(const uint8_t* data, size_t byteLength, size_t bitLength)
    : pImpl_(std::make_unique<Impl>()) {
    pImpl_->data = data;
    pImpl_->bitLength = bitLength ? std::min(bitLength,byteLength*8) : byteLength*8;
}

BitStreamReader::~BitStreamReader() = default;

bool BitStreamReader::Read(uint8_t& value) {
    value = 0;
    return pImpl_->ReadBitsInternal(&value, 8);
}

bool BitStreamReader::Read(uint16_t& value) {
    uint8_t bytes[2] = {};
    if (!pImpl_->ReadBitsInternal(bytes, 16)) return false;
    memcpy(&value, bytes, 2);
    return true;
}

bool BitStreamReader::Read(uint32_t& value) {
    uint8_t bytes[4] = {};
    if (!pImpl_->ReadBitsInternal(bytes, 32)) return false;
    memcpy(&value, bytes, 4);
    return true;
}

bool BitStreamReader::Read(int32_t& value) {
    uint8_t bytes[4] = {};
    if (!pImpl_->ReadBitsInternal(bytes, 32)) return false;
    memcpy(&value, bytes, 4);
    return true;
}

bool BitStreamReader::Read(float& value) {
    uint8_t bytes[4] = {};
    if (!pImpl_->ReadBitsInternal(bytes, 32)) return false;
    memcpy(&value, bytes, 4);
    return true;
}

bool BitStreamReader::ReadString(std::string& outStr, size_t maxLen) {
    uint16_t len = 0;
    if (!Read(len)) return false;
    if (len > maxLen) return false;
    if (len == 0) { outStr.clear(); return true; }
    std::vector<uint8_t> buf(len, 0);
    if (len > 0 && !pImpl_->ReadBitsInternal(buf.data(), len * 8)) return false;
    outStr.assign(reinterpret_cast<const char*>(buf.data()), len);
    return true;
}

bool BitStreamReader::ReadBytes(uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) return true;
    return pImpl_->ReadBitsInternal(buffer, length * 8);
}

bool BitStreamReader::ReadBits(uint8_t* buffer, size_t bitCount) {
    if (!buffer || bitCount == 0) return true;
    return pImpl_->ReadBitsInternal(buffer, bitCount);
}

size_t BitStreamReader::GetReadOffsetBits() const {
    return pImpl_->readBitOffset;
}

size_t BitStreamReader::GetRemainingBits() const {
    return pImpl_->bitLength - pImpl_->readBitOffset;
}

void BitStreamReader::Reset() {
    pImpl_->readBitOffset = 0;
}

// ---------------------------------------------------------------------------
// RakNetWrapper Implementation
// ---------------------------------------------------------------------------
// MTA handshake states
enum class HandshakeState : uint8_t {
    IDLE = 0,
    RAKNET_CONNECTING,
    RAKNET_CONNECTED,       // RakNet CONNECTION_REQUEST_ACCEPTED received
    JOIN_SENT,              // SendJoinRequest dispatched
    HANDSHAKE_COMPLETE,     // Server accepted join (user packet or spawn)
    FAILED
};

struct RakNetWrapper::Impl {
    Mta::ClientTransport adapter;
    PacketHandlerCallback packetHandler;
    ConnectionStateCallback connectionStateCallback;
    std::atomic<bool> ready{false};
    std::atomic<bool> running{false};
    std::atomic<HandshakeState> handshakeState{HandshakeState::IDLE};
    uint32_t timeoutMs = 10000;
    std::string localIP = "0.0.0.0";
    std::string serverHost;
    uint16_t serverPort = 0;
    uint16_t clientPort = 0;
    std::atomic<uint8_t> connectionError{0};
    char serialBuffer[64] = {};
    uint32_t startTime = 0;
    std::string pendingNickname;
    std::string pendingSerial;
};

RakNetWrapper::RakNetWrapper() : pImpl_(std::make_unique<Impl>()) {}
RakNetWrapper::~RakNetWrapper() {
    Shutdown();
}

bool RakNetWrapper::StartNetwork(const char* serverHost, uint16_t serverPort, bool /*packetTag*/) {
    if (pImpl_->running.load()) return true;

    if (!pImpl_->adapter.startup(1, 1)) {
        LOGE("RakNetWrapper: adapter startup failed");
        return false;
    }

    if (!pImpl_->adapter.connect(serverHost, serverPort)) {
        LOGE("RakNetWrapper: adapter connect failed for %s:%u", serverHost, serverPort);
        return false;
    }

    pImpl_->serverHost = serverHost ? serverHost : "";
    pImpl_->serverPort = serverPort;
    pImpl_->startTime = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    pImpl_->handshakeState.store(HandshakeState::RAKNET_CONNECTING);
    pImpl_->ready.store(true);
    pImpl_->running.store(true);
    LOGI("MTA native transport opening %s:%u", serverHost, serverPort);

    return true;
}

void RakNetWrapper::StopNetwork() {
    pImpl_->running.store(false);
    pImpl_->ready.store(false);
    pImpl_->adapter.shutdown();
}

bool RakNetWrapper::IsReady() const {
    return pImpl_->ready.load();
}

bool RakNetWrapper::IsConnected() const {
    return pImpl_->adapter.isConnected();
}

void RakNetWrapper::DoPulse() {
    if (!pImpl_->running.load()) return;

    // Transport-level timeout covers only the RakNet connect phase. Once
    // CONNECTION_REQUEST_ACCEPTED arrives, the NeonJoinClient owns per-step
    // timeouts for the MTA application join (MOD_NAME/JOINDATA/JOINEDGAME).
    auto currentState = pImpl_->handshakeState.load();
    if (currentState == HandshakeState::RAKNET_CONNECTING) {
        uint32_t now = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        uint32_t elapsed = now - pImpl_->startTime;
        if (elapsed > pImpl_->timeoutMs) {
            LOGE("RakNetWrapper: connection timeout after %ums to %s:%u (state=%d)",
                 elapsed, pImpl_->serverHost.c_str(), pImpl_->serverPort, static_cast<int>(currentState));
            pImpl_->handshakeState.store(HandshakeState::FAILED);
            pImpl_->connectionError.store(14); // Timeout
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Connection timed out");
            }
            return;
        }
    }

    // Resource client-script bundles are reassembled far above one MTU.
    // A 64 KiB stack buffer used to make the transport close the session.
    static thread_local std::vector<uint8_t> recvBuf;
    constexpr size_t kReceiveCap = 4 * 1024 * 1024;
    if (recvBuf.size() < kReceiveCap) recvBuf.resize(kReceiveCap);
    // One frame used to take a single datagram while Pulse() had already
    // queued the rest. The queue then hit its limit and the transport
    // reported connection lost. Drain the burst in this pulse.
    for (int burst = 0; burst < 48; ++burst) {
    size_t receivedBits = 0;
    int received = pImpl_->adapter.receive(recvBuf.data(), static_cast<int>(recvBuf.size()), &receivedBits);
    if (received <= 0) {
        if (burst > 0) break;
        static uint32_t lastLogTime = 0;
        uint32_t now = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        if (now - lastLogTime > 2000) {
            LOGI("RakNetWrapper: DoPulse alive, no packets (state=%d, connected=%d)",
                 static_cast<int>(pImpl_->handshakeState.load()),
                 pImpl_->adapter.isConnected() ? 1 : 0);
            lastLogTime = now;
        }
        break;
    }

    const uint8_t rawId = recvBuf[0];
    uint8_t packetId = rawId;
    // MTA application IDs are RID_USER_PACKET_ENUM + ePacketID on the wire.
    // Preserve the existing local [gameId][body] callback representation.
    if (rawId >= static_cast<uint8_t>(PacketID::USER_PACKET_ENUM)) {
        packetId = static_cast<uint8_t>(PacketID::USER_PACKET_ENUM);
        recvBuf[0] = rawId - packetId;
    }
    const bool chatty = rawId == 131 || rawId == 132 || rawId == 150 || rawId == 155 || rawId == 162 || rawId == 176;
    if (!chatty) LOGI("MTA native received wire ID=%u bytes=%d bits=%zu", rawId, received, receivedBits);
    const uint8_t* payload = recvBuf.data() + (packetId == 99 ? 0 : 1);
    size_t payloadLen = static_cast<size_t>(received - (packetId == 99 ? 0 : 1));
    size_t payloadBits = packetId == 99 ? receivedBits : (receivedBits >= 8 ? receivedBits - 8 : 0);

    // Handle RakNet internal connection-state packets
    switch (packetId) {
        case static_cast<uint8_t>(PacketID::CONNECTION_REQUEST_ACCEPTED): {
            LOGI("RakNetWrapper: CONNECTION_REQUEST_ACCEPTED from %s:%u",
                 pImpl_->serverHost.c_str(), pImpl_->serverPort);
            pImpl_->handshakeState.store(HandshakeState::RAKNET_CONNECTED);

            // The MTA application join (PLAYER_JOIN -> MOD_NAME ->
            // PLAYER_JOINDATA -> ...) is owned by NeonJoinClient, which polls
            // IsRakNetConnected() in its Pulse(). No join is sent here.
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(true, "RakNet connected");
            }
            break;
        }
        case static_cast<uint8_t>(PacketID::CONNECTION_ATTEMPT_FAILED): {
            LOGE("RakNetWrapper: CONNECTION_ATTEMPT_FAILED to %s:%u",
                 pImpl_->serverHost.c_str(), pImpl_->serverPort);
            pImpl_->handshakeState.store(HandshakeState::FAILED);
            pImpl_->connectionError.store(15);
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Connection attempt failed");
            }
            break;
        }
        case static_cast<uint8_t>(PacketID::DISCONNECTION_NOTIFICATION): {
            LOGI("RakNetWrapper: DISCONNECTION_NOTIFICATION");
            pImpl_->handshakeState.store(HandshakeState::IDLE);
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Disconnected by server");
            }
            break;
        }
        case static_cast<uint8_t>(PacketID::CONNECTION_LOST): {
            LOGE("RakNetWrapper: CONNECTION_LOST");
            pImpl_->handshakeState.store(HandshakeState::IDLE);
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Connection lost");
            }
            break;
        }
        case static_cast<uint8_t>(PacketID::CONNECTION_BANNED): {
            LOGE("RakNetWrapper: CONNECTION_BANNED");
            pImpl_->handshakeState.store(HandshakeState::FAILED);
            pImpl_->connectionError.store(22);
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Banned from server");
            }
            break;
        }
        case static_cast<uint8_t>(PacketID::INVALID_PASSWORD): {
            LOGE("RakNetWrapper: INVALID_PASSWORD");
            pImpl_->handshakeState.store(HandshakeState::FAILED);
            pImpl_->connectionError.store(23);
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Invalid password");
            }
            break;
        }
        default: {
            // User/game packets — forward to registered handler
            if (pImpl_->packetHandler) {
                bool handled = pImpl_->packetHandler(packetId, payload, payloadLen, payloadBits);
                (void)handled; // Only NeonJoinClient can accept the session.
            }
            break;
        }
    }
    }
}

void RakNetWrapper::Shutdown() {
    StopNetwork();
}

void RakNetWrapper::SetPacketHandler(PacketHandlerCallback handler) {
    pImpl_->packetHandler = std::move(handler);
}

bool RakNetWrapper::SendPacket(uint8_t packetId, const BitStreamWriter& stream,
                               PacketPriority priority, PacketReliability reliability,
                               PacketOrdering ordering) {
    if (!pImpl_->running.load() || !pImpl_->adapter.isConnected()) return false;

    const size_t dataLen = stream.GetByteLength();
    std::vector<uint8_t> sendBuf;
    size_t bits = stream.GetBitLength();
    if (packetId == static_cast<uint8_t>(PacketID::USER_PACKET_ENUM)) {
        if (bits < 8 || stream.GetData()[0] > 156) return false;
        sendBuf.assign(stream.GetData(),stream.GetData()+dataLen);
        sendBuf[0] += packetId;
    } else {
        sendBuf.push_back(packetId);
        if (dataLen) sendBuf.insert(sendBuf.end(),stream.GetData(),stream.GetData()+dataLen);
        bits += 8;
    }
    return pImpl_->adapter.send(sendBuf.data(), static_cast<int>(sendBuf.size()),
                                static_cast<int>(priority), static_cast<int>(reliability),
                                static_cast<int>(ordering), bits);
}

void RakNetWrapper::SetClientPort(uint16_t port) {
    pImpl_->clientPort = port;
}

std::string RakNetWrapper::GetConnectedServer(bool includePort) const {
    if (!pImpl_->adapter.isConnected()) return "";
    if (includePort) {
        return pImpl_->serverHost + ":" + std::to_string(pImpl_->serverPort);
    }
    return pImpl_->serverHost;
}

bool RakNetWrapper::GetNetworkStatistics(NetworkStatistics& stats) const {
    memset(&stats, 0, sizeof(stats));
    // Placeholder: real stats come from RakPeerInterface::GetStatistics
    return true;
}

int RakNetWrapper::GetPing() const {
    return pImpl_->adapter.getPing();
}

uint32_t RakNetWrapper::GetTime() const {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string RakNetWrapper::GetLocalIP() const {
    return pImpl_->localIP;
}

void RakNetWrapper::GetSerial(char* buffer, size_t maxLength) const {
    if (!buffer || maxLength == 0) return;
    size_t copyLen = std::min(maxLength - 1, strlen(pImpl_->serialBuffer));
    memcpy(buffer, pImpl_->serialBuffer, copyLen);
    buffer[copyLen] = '\0';
}

uint8_t RakNetWrapper::GetConnectionError() const {
    return pImpl_->connectionError.load();
}

void RakNetWrapper::SetConnectionError(uint8_t error) {
    pImpl_->connectionError.store(error);
}

void RakNetWrapper::Reset() {
    StopNetwork();
    pImpl_->adapter.shutdown();
    pImpl_->packetHandler = nullptr;
    pImpl_->timeoutMs = 10000;
}

void RakNetWrapper::SetTimeoutTime(uint32_t timeoutMs) {
    pImpl_->timeoutMs = timeoutMs;
}

bool RakNetWrapper::SendOpenPacket() {
    BitStreamWriter stream;
    stream.Write(static_cast<uint8_t>(PacketID::OPEN_CONNECTION_REQUEST));
    // MTA open packet contains protocol version and game version
    stream.Write(static_cast<uint32_t>(1)); // protocol version placeholder
    return SendPacket(static_cast<uint8_t>(PacketID::OPEN_CONNECTION_REQUEST),
                      stream, PacketPriority::HIGH, PacketReliability::RELIABLE);
}

bool RakNetWrapper::SendJoinRequest(const std::string& nickname, const std::string& serial) {
    // Store credentials for deferred send if RakNet hasn't connected yet
    pImpl_->pendingNickname = nickname;
    pImpl_->pendingSerial = serial;

    auto state = pImpl_->handshakeState.load();
    if (state != HandshakeState::RAKNET_CONNECTED && state != HandshakeState::JOIN_SENT) {
        LOGI("RakNetWrapper: JoinRequest deferred (state=%d, waiting for RakNet connect)",
             static_cast<int>(state));
        return true; // Will be sent automatically when CONNECTION_REQUEST_ACCEPTED arrives
    }

    // Envelope: SendPacket() writes the RID_USER_PACKET_ENUM (99) prefix byte
    // exactly once; the stream carries only the game packet body. Writing the
    // UPE byte here too would double-prefix the join on the wire.
    // TODO(P1B.2): replace simplified body with real PLAYER_JOINDATA layout
    // from Client/mods/deathmatch/logic/CPacketHandler.cpp.
    BitStreamWriter stream;
    stream.Write(NormalizeAscii(nickname).c_str());
    stream.Write(NormalizeAscii(serial).c_str());
    bool sent = SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM),
                           stream, PacketPriority::HIGH, PacketReliability::RELIABLE_ORDERED);
    if (sent) {
        pImpl_->handshakeState.store(HandshakeState::JOIN_SENT);
        LOGI("RakNetWrapper: JoinRequest sent for '%s'", nickname.c_str());
    }
    return sent;
}

void RakNetWrapper::SetConnectionStateCallback(ConnectionStateCallback callback) {
    pImpl_->connectionStateCallback = std::move(callback);
}

bool RakNetWrapper::IsHandshakeComplete() const {
    return pImpl_->handshakeState.load() == HandshakeState::HANDSHAKE_COMPLETE;
}

void RakNetWrapper::MarkSessionAccepted() {
    if (IsRakNetConnected()) pImpl_->handshakeState.store(HandshakeState::HANDSHAKE_COMPLETE);
}

bool RakNetWrapper::IsRakNetConnected() const {
    const auto s = pImpl_->handshakeState.load();
    return s == HandshakeState::RAKNET_CONNECTED ||
           s == HandshakeState::JOIN_SENT ||
           s == HandshakeState::HANDSHAKE_COMPLETE;
}

bool RakNetWrapper::VerifySerialSignature(const uint8_t* data, size_t length) {
    // No signature verifier is linked yet. Never claim a successful check.
    (void)data;
    (void)length;
    return false;
}

} // namespace WorldRPG::Net
