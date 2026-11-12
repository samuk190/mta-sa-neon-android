#include "MtaReliabilityCodec.h"
#include <algorithm>
#include <stdexcept>

namespace WorldRPG::Net::Mta {
namespace {
constexpr std::size_t MaxDatagram = 2048;
constexpr std::size_t MaxRecords = 256;
constexpr std::uint16_t MaxPayloadBits = 11928;
bool HasOrdering(std::uint8_t reliability) { return reliability == 1 || reliability == 3 || reliability == 4; }

struct Writer {
    Bytes data;
    std::size_t offset = 0;
    void Bit(bool value) {
        if (offset >= MaxDatagram * 8) throw std::runtime_error("Frame exceeds datagram limit");
        if ((offset & 7) == 0) data.push_back(0);
        if (value) data.back() |= static_cast<std::uint8_t>(0x80 >> (offset & 7));
        ++offset;
    }
    void Small(std::uint64_t value, unsigned bits) {
        for (unsigned bit = bits; bit > 0; --bit) Bit((value >> (bit - 1)) & 1);
    }
    void Integer(std::uint64_t value, unsigned bytes) {
        for (unsigned index = 0; index < bytes; ++index) Small((value >> (index * 8)) & 255, 8);
    }
    void Compressed(std::uint32_t value, unsigned bytes) {
        for (unsigned index = bytes; index > 1; --index) {
            const bool zero = (value >> ((index - 1) * 8)) == 0;
            Bit(zero);
            if (!zero) { Integer(value, index); return; }
        }
        const bool nibble = value < 16;
        Bit(nibble); Small(value, nibble ? 4 : 8);
    }
    void Align() { while (offset & 7) Bit(false); }
};

struct Reader {
    std::span<const std::uint8_t> data;
    std::size_t offset = 0;
    std::size_t Remaining() const { return data.size() * 8 - offset; }
    bool Bit() {
        if (!Remaining()) throw std::runtime_error("Truncated bit field");
        const bool value = (data[offset >> 3] & (0x80 >> (offset & 7))) != 0;
        ++offset; return value;
    }
    std::uint64_t Small(unsigned bits) {
        std::uint64_t value = 0;
        for (unsigned bit = 0; bit < bits; ++bit) value = (value << 1) | Bit();
        return value;
    }
    std::uint64_t Integer(unsigned bytes) {
        std::uint64_t value = 0;
        for (unsigned index = 0; index < bytes; ++index) value |= Small(8) << (index * 8);
        return value;
    }
    std::uint32_t Compressed(unsigned bytes) {
        for (unsigned index = bytes; index > 1; --index) if (!Bit()) return Integer(index);
        return Small(Bit() ? 4 : 8);
    }
    void Align() { offset = (offset + 7) & ~std::size_t{7}; }
};
} // namespace

std::variant<Bytes, FrameError> EncodeFrame(const ReliableFrame& frame) {
    Writer writer;
    try {
        if (frame.acknowledgments.size() > MaxRecords || frame.messages.size() > MaxRecords)
            throw std::runtime_error("Too many frame records");
        if (!frame.ackTime && !frame.acknowledgments.empty()) throw std::runtime_error("ACK time absent");
        writer.Bit(frame.ackTime.has_value());
        if (frame.ackTime) {
            writer.Integer(*frame.ackTime, 8);
            writer.Compressed(frame.acknowledgments.size(), 2);
            for (const auto& range : frame.acknowledgments) {
                if (range.last < range.first) throw std::runtime_error("Reversed ACK range");
                writer.Bit(range.first == range.last);
                writer.Integer(range.first, 4);
                if (range.first != range.last) writer.Integer(range.last, 4);
            }
        }
        writer.Bit(frame.remoteTime.has_value());
        if (frame.remoteTime) writer.Integer(*frame.remoteTime, 8);
        for (const auto& message : frame.messages) {
            if (message.reliability > 4 || message.channel > 31 || message.bitLength == 0
                    || message.bitLength > MaxPayloadBits || message.payload.size() != (message.bitLength + 7u) / 8u)
                throw std::runtime_error("Invalid message fields");
            writer.Integer(message.number, 4);
            writer.Small(message.reliability, 3);
            if (HasOrdering(message.reliability)) { writer.Small(message.channel, 5); writer.Integer(message.order, 4); }
            writer.Bit(message.split.has_value());
            if (message.split) {
                const auto& split = *message.split;
                if (split.count == 0 || split.count > 16384 || split.index >= split.count)
                    throw std::runtime_error("Invalid split metadata");
                writer.Integer(split.id, 2);
                writer.Compressed(split.index, 4); writer.Compressed(split.count, 4);
            }
            writer.Compressed(message.bitLength, 2);
            writer.Align();
            for (const auto byte : message.payload) writer.Small(byte, 8);
        }
        return writer.data;
    } catch (const std::exception& error) { return FrameError{error.what(), writer.offset}; }
}

std::variant<ReliableFrame, FrameError> DecodeFrame(std::span<const std::uint8_t> bytes) {
    Reader reader{bytes};
    try {
        if (bytes.empty() || bytes.size() > MaxDatagram) throw std::runtime_error("Invalid datagram size");
        ReliableFrame frame;
        if (reader.Bit()) {
            frame.ackTime = reader.Integer(8);
            const auto count = reader.Compressed(2);
            if (count > MaxRecords) throw std::runtime_error("Too many ACK ranges");
            for (std::uint32_t index = 0; index < count; ++index) {
                const bool single = reader.Bit();
                AckRange range; range.first = reader.Integer(4); range.last = single ? range.first : reader.Integer(4);
                if (range.last < range.first) throw std::runtime_error("Reversed ACK range");
                frame.acknowledgments.push_back(range);
            }
        }
        // Native ACK-only frames may end before this optional data header.
        if (!reader.Remaining()) return frame;
        if (reader.Bit()) frame.remoteTime = reader.Integer(8);
        while (reader.Remaining() >= 8) {
            if (frame.messages.size() >= MaxRecords) throw std::runtime_error("Too many messages");
            ReliableMessage message;
            message.number = reader.Integer(4); message.reliability = reader.Small(3);
            if (message.reliability > 4) throw std::runtime_error("Unsupported reliability");
            if (HasOrdering(message.reliability)) { message.channel = reader.Small(5); message.order = reader.Integer(4); }
            if (reader.Bit()) {
                SplitInfo split; split.id = reader.Integer(2);
                split.index = reader.Compressed(4); split.count = reader.Compressed(4);
                if (split.count == 0 || split.count > 16384 || split.index >= split.count)
                    throw std::runtime_error("Invalid split metadata");
                message.split = split;
            }
            message.bitLength = reader.Compressed(2);
            if (!message.bitLength || message.bitLength > MaxPayloadBits) throw std::runtime_error("Invalid payload bit length");
            reader.Align();
            const auto size = (message.bitLength + 7u) / 8u;
            if (reader.Remaining() < size * 8) throw std::runtime_error("Truncated message payload");
            const auto payload = bytes.subspan(reader.offset >> 3, size);
            message.payload.assign(payload.begin(), payload.end());
            reader.offset += size * 8;
            frame.messages.push_back(std::move(message));
        }
        return frame;
    } catch (const std::exception& error) { return FrameError{error.what(), reader.offset}; }
}
} // namespace WorldRPG::Net::Mta
