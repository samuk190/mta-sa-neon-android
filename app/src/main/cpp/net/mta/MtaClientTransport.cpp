#include "MtaClientTransport.h"
#include "MtaOfflineCodec.h"
#include "MtaReliabilityCodec.h"
#include "MtaUdpSocket.h"
#include "MtaRandom.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif
#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <deque>
#include <map>
#include <tuple>

namespace WorldRPG::Net::Mta {
namespace {
constexpr Block16 Offer{0xed,0xca,1,1,0xa8,4,0xe8,3,0,0,0xc8,4,0xe8,3,0,0};
constexpr std::size_t ChunkBytes = 1000;
constexpr std::size_t MaxAssemblyBytes = 16 * 1024 * 1024;
constexpr std::size_t MaxPending = 2048;
std::uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void AppendLE(Bytes& bytes, std::uint64_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index) bytes.push_back(value >> (index*8));
}
} // namespace

struct ClientTransport::Impl {
    enum class State { Idle, Opening, Connecting, Connected, Failed } state = State::Idle;
    struct Pending { ReliableMessage message; std::uint64_t sent{}, firstSent{}; };
    struct Event { Bytes bytes; std::size_t bits{}; };
    struct SplitGroup {
        std::uint32_t count{};
        std::map<std::uint32_t, ReliableMessage> parts;
        std::size_t bytes{};
        std::uint64_t created{};
    };
    UdpSocket socket;
    std::optional<Cipher> cipher;
    Block16 nonce{};
    OpenRequest open{};
    Bytes prefix;
    bool started = false;
    std::string error;
    std::uint8_t selector{};
    std::uint16_t splitId{};
    std::uint32_t nextMessage{};
    std::array<std::uint32_t,32> nextOrder{}, nextSequence{}, expectedOrder{}, newestSequence{};
    std::array<bool,32> hasSequence{};
    std::map<std::uint32_t,Pending> pending;
    std::array<std::map<std::uint32_t,ReliableMessage>,32> ordered;
    std::map<std::tuple<std::uint16_t,std::uint8_t,std::uint32_t,std::uint8_t>,SplitGroup> groups;
    std::deque<Event> events;
    std::size_t assemblyBytes{}, eventBytes{}, orderedBytes{};
    std::optional<std::uint32_t> highestReceived;
    std::bitset<65536> received;
    std::uint64_t began{}, lastOpen{}, lastReceive{}, lastClock{};
    int ping{};
    std::uint64_t bytesSent{};
    std::uint64_t bytesReceived{};
    std::uint32_t packetsSent{};
    std::uint32_t packetsReceived{};

    void Queue(Bytes bytes, std::size_t bits = 0) {
        // A sync burst can arrive faster than one packet per frame. Closing
        // the socket here is the "connection lost" that shows up after the
        // world starts streaming. Drop the oldest packet and keep the session.
        while ((events.size() >= 2048 || eventBytes + bytes.size() > MaxAssemblyBytes) && !events.empty()) {
            eventBytes -= std::min(eventBytes, events.front().bytes.size());
            events.pop_front();
        }
        eventBytes += bytes.size();
        const auto count = bits ? bits : bytes.size()*8;
        events.push_back({std::move(bytes),count});
    }
    void Fail(const std::string& reason) {
        if (state == State::Failed) return;
        const bool connected = state == State::Connected;
        error = reason;
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "WorldRPGNeon", "MTA transport failed: %s", reason.c_str());
#endif
        state = State::Failed;
        pending.clear(); groups.clear(); assemblyBytes = 0;
        for (auto& queue : ordered) queue.clear();
        orderedBytes = 0;
        events.clear(); eventBytes = 0;
        events.push_back({Bytes{static_cast<std::uint8_t>(connected ? 20 : 15)},8});
        socket.Close();
    }
    bool Raw(std::span<const std::uint8_t> bytes) {
        if (!cipher || !socket.Send(cipher->Seal(bytes, selector++))) { Fail("MTA datagram send failed"); return false; }
        return true;
    }
    bool Frame(const ReliableFrame& frame) {
        auto encoded = EncodeFrame(frame);
        if (auto* failure = std::get_if<FrameError>(&encoded)) { Fail(failure->reason); return false; }
        return Raw(std::get<Bytes>(encoded));
    }
    bool Transmit(const ReliableMessage& message) {
        ReliableFrame frame; frame.remoteTime = Now(); frame.messages = {message};
        return Frame(frame);
    }
    std::uint32_t Order(std::uint8_t reliability, std::uint8_t channel) {
        if (reliability == 3) return nextOrder[channel]++;
        if (reliability == 1 || reliability == 4) return nextSequence[channel]++;
        return 0;
    }
    bool Message(std::span<const std::uint8_t> bytes, std::size_t bits, std::uint8_t reliability, std::uint8_t channel) {
        if (!cipher || !bits || bits > bytes.size()*8 || (bits+7)/8 != bytes.size()
                || reliability > 4 || channel > 31 || bytes.size() > MaxAssemblyBytes) return false;
        const auto count = (bytes.size()+ChunkBytes-1)/ChunkBytes;
        if (count > 16384 || pending.size()+count > MaxPending) return false;
        if (count > 1 && reliability < 2) reliability = reliability == 0 ? 2 : 4;
        const auto order = Order(reliability,channel);
        const auto group = count > 1 ? splitId++ : std::uint16_t{0};
        for (std::size_t index = 0, offset = 0; offset < bytes.size(); ++index) {
            ReliableMessage message;
            message.number = nextMessage++; message.reliability = reliability;
            message.channel = channel; message.order = order;
            const auto length = std::min(ChunkBytes,bytes.size()-offset);
            message.payload.assign(bytes.begin()+offset,bytes.begin()+offset+length);
            message.bitLength = std::min<std::size_t>(bits-offset*8,length*8);
            if (count > 1) message.split = SplitInfo{group,static_cast<std::uint32_t>(index),static_cast<std::uint32_t>(count)};
            if (reliability >= 2) pending.emplace(message.number,Pending{message,Now(),Now()});
            if (!Transmit(message)) return false;
            offset += length;
        }
        return true;
    }
    bool Duplicate(std::uint32_t number) {
        if (!highestReceived) { highestReceived = number; received.set(0); return false; }
        const auto forward = static_cast<std::int32_t>(number-*highestReceived);
        if (forward > 0) { received <<= static_cast<std::size_t>(forward); highestReceived = number; received.set(0); return false; }
        const auto behind = *highestReceived-number;
        if (behind >= received.size() || received[behind]) return true;
        received.set(behind); return false;
    }
    void Deliver(ReliableMessage message) {
        if (message.payload.empty()) return;
        if (message.bitLength < 8) { Fail("Truncated MTA packet identifier"); return; }
        const auto id = message.payload[0];
        if (id == 14) {
            if (state == State::Connected) return;
            if (message.payload.size() < 15) { Fail("Truncated MTA connection acceptance"); return; }
            Bytes nic{17};
            nic.insert(nic.end(),message.payload.begin()+9,message.payload.begin()+15);
            nic.insert(nic.end(),message.payload.begin()+1,message.payload.begin()+7);
            for (int index = 0; index < 9; ++index) nic.insert(nic.end(),{0,0,0,0,0xff,0xff});
            if (!Message(nic,nic.size()*8,3,0)) { Fail("Could not confirm MTA connection"); return; }
            state = State::Connected;
            Queue(Bytes{14});
        } else if (id == 0 && message.payload.size() >= 9) {
            Bytes pong{3}; pong.insert(pong.end(),message.payload.begin()+1,message.payload.begin()+9);
            AppendLE(pong,Now(),8);
            if (!Message(pong,pong.size()*8,0,0)) Fail("Could not answer MTA ping");
        } else if (id == 19 || id == 22 || id == 23 || id == 95) {
            Queue(std::move(message.payload),message.bitLength);
            state = State::Failed; error = "MTA peer disconnected or refused connection"; socket.Close();
        } else if (id >= 99) {
            Queue(std::move(message.payload),message.bitLength);
        }
    }
    void Ordered(ReliableMessage message) {
        const auto channel = message.channel;
        if (message.reliability == 3) {
            const auto difference = static_cast<std::int32_t>(message.order-expectedOrder[channel]);
            if (difference < 0) return;
            if (difference > 0) {
                auto& queue = ordered[channel];
                if (queue.contains(message.order)) return;
                if (queue.size() >= 2048 || orderedBytes+message.payload.size() > MaxAssemblyBytes) { Fail("MTA ordered queue limit"); return; }
                orderedBytes += message.payload.size(); queue.emplace(message.order,std::move(message)); return;
            }
            Deliver(std::move(message)); ++expectedOrder[channel];
            auto& queue = ordered[channel];
            while (state != State::Failed) {
                auto found = queue.find(expectedOrder[channel]);
                if (found == queue.end()) break;
                auto next = std::move(found->second); orderedBytes -= next.payload.size(); queue.erase(found);
                Deliver(std::move(next)); ++expectedOrder[channel];
            }
        } else if (message.reliability == 1 || message.reliability == 4) {
            if (hasSequence[channel] && static_cast<std::int32_t>(message.order-newestSequence[channel]) <= 0) return;
            hasSequence[channel] = true; newestSequence[channel] = message.order;
            for (auto entry = groups.begin(); entry != groups.end();) {
                const auto& [id,reliability,order,groupChannel] = entry->first;
                (void)id;
                if (groupChannel == channel && (reliability == 1 || reliability == 4)
                        && static_cast<std::int32_t>(order-message.order) <= 0) {
                    assemblyBytes -= entry->second.bytes; entry = groups.erase(entry);
                } else ++entry;
            }
            Deliver(std::move(message));
        } else Deliver(std::move(message));
    }
    void Incoming(ReliableMessage message) {
        if (Duplicate(message.number)) return;
        if ((message.reliability == 1 || message.reliability == 4) && hasSequence[message.channel]
                && static_cast<std::int32_t>(message.order-newestSequence[message.channel]) <= 0) return;
        if (!message.split) { Ordered(std::move(message)); return; }
        const auto split = *message.split;
        const auto identity = std::make_tuple(split.id,message.reliability,message.order,message.channel);
        auto found = groups.find(identity);
        if (found == groups.end()) {
            if (groups.size() >= 64) { Fail("MTA split group limit"); return; }
            found = groups.emplace(identity,SplitGroup{split.count,{},0,Now()}).first;
        }
        auto& group = found->second;
        if (group.count != split.count) { Fail("Inconsistent MTA fragment count"); return; }
        if (group.parts.contains(split.index)) return;
        if ((split.index+1 < split.count && (message.bitLength & 7)) || assemblyBytes+message.payload.size() > MaxAssemblyBytes) {
            Fail("Invalid or oversized MTA fragment group"); return;
        }
        assemblyBytes += message.payload.size(); group.bytes += message.payload.size();
        group.parts.emplace(split.index,std::move(message));
        if (group.parts.size() != group.count) return;
        ReliableMessage complete = group.parts.begin()->second; complete.payload.clear(); complete.bitLength = 0; complete.split.reset();
        complete.payload.reserve(group.bytes);
        for (auto& [index,part] : group.parts) {
            (void)index;
            complete.payload.insert(complete.payload.end(),part.payload.begin(),part.payload.end());
            complete.bitLength += part.bitLength;
        }
        assemblyBytes -= group.bytes; groups.erase(found);
        Ordered(std::move(complete));
    }
    void Receive(Bytes datagram) {
        if (state == State::Opening) {
            auto parsed = ParseOpenReply(datagram);
            auto* reply = std::get_if<OpenReply>(&parsed);
            if (!reply) { Fail("Invalid MTA OPEN_REPLY"); return; }
            Bytes key; bool selected = false;
            for (const auto& field : reply->fields) {
                if (field.tag == 0x80 && field.value == Bytes{1}) selected = true;
                if (field.tag == 0x6e) key = field.value;
                if (field.tag == 0x1a) prefix = field.value;
            }
            if (!selected || key.empty()) { Fail("Unsupported MTA cipher negotiation"); return; }
            cipher = Cipher::Create(key);
            if (!cipher) { Fail("Invalid MTA cipher key length"); return; }
            state = State::Connecting; lastReceive = Now();
            Bytes request{4}; request.insert(request.end(),OfflineMagic.begin(),OfflineMagic.end()); request.insert(request.end(),nonce.begin(),nonce.end());
            if (!Message(request,request.size()*8,2,0)) Fail("Could not send MTA connection request");
            return;
        }
        if (!cipher || state == State::Failed || state == State::Idle) return;
        // Retransmitted offline replies must not be interpreted as ciphertext.
        if (datagram.size() >= 17 && datagram[0] == 10 && std::equal(OfflineMagic.begin(),OfflineMagic.end(),datagram.begin()+1)) return;
        const auto decrypted = cipher->Open(datagram,prefix);
        const auto* bytes = std::get_if<Bytes>(&decrypted);
        if (!bytes) return;
        if (bytes->size() >= 18 && (*bytes)[0] == 0x26 && std::equal(OfflineMagic.begin(),OfflineMagic.end(),bytes->begin()+1)) { lastReceive = Now(); return; }
        auto decoded = DecodeFrame(*bytes);
        const auto* frame = std::get_if<ReliableFrame>(&decoded);
        if (!frame) { Fail("Malformed MTA reliable frame"); return; }
        lastReceive = Now();
        if (frame->ackTime && Now() >= *frame->ackTime && Now()-*frame->ackTime <= 10000) ping = Now()-*frame->ackTime;
        for (const auto& range : frame->acknowledgments) {
            auto entry = pending.lower_bound(range.first);
            while (entry != pending.end() && entry->first <= range.last) entry = pending.erase(entry);
        }
        ReliableFrame ack; ack.ackTime = frame->remoteTime.value_or(0);
        for (const auto& message : frame->messages) if (message.reliability >= 2) ack.acknowledgments.push_back({message.number,message.number});
        if (!ack.acknowledgments.empty() && !Frame(ack)) return;
        for (const auto& message : frame->messages) { Incoming(message); if (state == State::Failed) return; }
    }
    void Pulse() {
        if (state == State::Idle || state == State::Failed) return;
        for (unsigned i = 0; i < 32 && state != State::Failed; ++i) {
            Bytes bytes; const auto result = socket.Receive(bytes,0);
            if (result < 0) { Fail(socket.Error()); return; }
            if (!result) break;
            Receive(std::move(bytes));
        }
        const auto now = Now();
        if (state == State::Opening) {
            if (now-began > 10000) { Fail("MTA OPEN timed out"); return; }
            if (now-lastOpen >= 750) { if (!socket.Send(open)) Fail(socket.Error()); lastOpen = now; }
            return;
        }
        if (state != State::Connecting && state != State::Connected) return;
        if (now-lastReceive > 30000) { Fail("MTA connection timed out"); return; }
        if (now-lastClock >= 2000) {
            Bytes clock{0x26}; clock.insert(clock.end(),OfflineMagic.begin(),OfflineMagic.end()); clock.push_back(1); AppendLE(clock,now-began,4);
            if (!Raw(clock)) return;
            lastClock = now;
        }
        for (auto& [number,entry] : pending) {
            (void)number;
            if (now-entry.firstSent > 30000) { Fail("MTA reliable delivery timed out"); return; }
            if (now-entry.sent >= 300) { entry.sent = now; if (!Transmit(entry.message)) return; }
        }
        for (const auto& [identity,group] : groups) { (void)identity; if (now-group.created > 30000) { Fail("MTA fragment assembly timed out"); return; } }
    }
};

ClientTransport::ClientTransport() : pImpl_(std::make_unique<Impl>()) {}
ClientTransport::~ClientTransport() { shutdown(); }
bool ClientTransport::startup(int maxConnections, int) { pImpl_->started = maxConnections == 1; return pImpl_->started; }
bool ClientTransport::connect(const char* host, unsigned short port) {
    if (!pImpl_->started || !host || !*host || !port) return false;
    pImpl_ = std::make_unique<Impl>(); auto& client = *pImpl_; client.started = true;
    if (!RandomBytes(client.nonce) || !RandomBytes({&client.selector,1})) { client.error = "Connection randomness unavailable"; return false; }
    if (!client.socket.Connect(host,port)) { client.error = client.socket.Error(); return false; }
    client.open = BuildOpenRequest(client.nonce,Offer);
    if (!client.socket.Send(client.open)) { client.error = client.socket.Error(); return false; }
    client.state = Impl::State::Opening; client.began = client.lastOpen = client.lastReceive = Now();
    return true;
}
bool ClientTransport::send(const unsigned char* data, int length, int priority, int reliability, int channel, std::size_t bits) {
    if (!isConnected() || !data || length <= 0 || priority < 0 || priority > 2 || reliability < 0 || reliability > 4 || channel < 0 || channel > 31) return false;
    const bool sent = pImpl_->Message({data,static_cast<std::size_t>(length)},bits ? bits : static_cast<std::size_t>(length)*8,reliability,channel);
    if (sent) {
        pImpl_->packetsSent += 1;
        pImpl_->bytesSent += static_cast<std::uint64_t>(length);
    }
    return sent;
}
int ClientTransport::receive(unsigned char* buffer, int capacity, std::size_t* bits) {
    if (!buffer || capacity <= 0) return 0;
    pImpl_->Pulse();
    if (pImpl_->events.empty()) return 0;
    if (pImpl_->events.front().bytes.size() > static_cast<std::size_t>(capacity)) {
        // A resource script bundle is often larger than the caller buffer.
        // Closing the socket here is what the player sees as Desconectado
        // right after the resource names. Drop this one packet and stay up.
        const auto dropped = pImpl_->events.front().bytes.size();
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "WorldRPGNeon",
            "MTA packet of %zu bytes exceeds the %d byte receive buffer; dropped, session kept",
            dropped, capacity);
#else
        (void)dropped;
#endif
        pImpl_->eventBytes -= std::min(pImpl_->eventBytes, dropped);
        pImpl_->events.pop_front();
        return 0;
    }
    auto event = std::move(pImpl_->events.front()); pImpl_->events.pop_front(); pImpl_->eventBytes -= std::min(pImpl_->eventBytes,event.bytes.size());
    std::copy(event.bytes.begin(),event.bytes.end(),buffer);
    if (bits) *bits = event.bits;
    pImpl_->packetsReceived += 1;
    pImpl_->bytesReceived += event.bytes.size();
    return event.bytes.size();
}
FrameCounters ClientTransport::frameCounters() const {
    FrameCounters out;
    out.bytesReceived = pImpl_->bytesReceived;
    out.bytesSent = pImpl_->bytesSent;
    out.packetsReceived = pImpl_->packetsReceived;
    out.packetsSent = pImpl_->packetsSent;
    out.messagesInResendBuffer = static_cast<std::uint32_t>(pImpl_->pending.size());
    return out;
}
int ClientTransport::getPing() const { return pImpl_->ping; }
bool ClientTransport::isConnected() const { return pImpl_->state == Impl::State::Connected; }
void ClientTransport::shutdown() {
    if (isConnected()) { const Bytes bye{19}; pImpl_->Message(bye,8,3,0); }
    pImpl_->socket.Close(); pImpl_->state = Impl::State::Idle; pImpl_->started = false;
    pImpl_->pending.clear(); pImpl_->events.clear(); pImpl_->cipher.reset(); pImpl_->prefix.clear();
}
const char* ClientTransport::lastError() const { return pImpl_->error.c_str(); }
} // namespace WorldRPG::Net::Mta
