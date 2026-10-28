/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The OpenRAC server's wire format (protocol.hpp), written as the server's OutputBuffer writes it
 * and read as its InputBuffer reads it: big-endian integers, strings behind a signed 16-bit length
 * that is -1 for an empty one, and a frame's length counting what follows it (Netty's
 * LengthFieldPrepender and LengthFieldBasedFrameDecoder with a 4-byte field, no adjustment).
 */
#include "protocol.hpp"

#include <algorithm>

namespace mp {
namespace {

/* The 16-bit length an empty string is written with (OutputBuffer.writeString). */
constexpr std::uint16_t kEmptyString = 0xFFFFu;

/* The longest string a 16-bit signed length holds. */
constexpr std::size_t kMaxStringBytes = 0x7FFF;

/* A PARTY body's start: the id and the opcode. */
Writer party(PartyOp op) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(PacketId::kParty)).u8(static_cast<std::uint8_t>(op));
    return w;
}

}  // namespace

Writer& Writer::u8(std::uint8_t value) {
    bytes_.push_back(value);
    return *this;
}

Writer& Writer::i32(std::int32_t value) {
    const auto v = static_cast<std::uint32_t>(value);
    bytes_.push_back(static_cast<std::uint8_t>(v >> 24));
    bytes_.push_back(static_cast<std::uint8_t>(v >> 16));
    bytes_.push_back(static_cast<std::uint8_t>(v >> 8));
    bytes_.push_back(static_cast<std::uint8_t>(v));
    return *this;
}

Writer& Writer::i64(std::int64_t value) {
    const auto v = static_cast<std::uint64_t>(value);
    i32(static_cast<std::int32_t>(static_cast<std::uint32_t>(v >> 32)));
    i32(static_cast<std::int32_t>(static_cast<std::uint32_t>(v)));
    return *this;
}

Writer& Writer::str(const std::string& value) {
    // An empty string is its length alone, -1: the reader skips it.
    if (value.empty()) {
        bytes_.push_back(static_cast<std::uint8_t>(kEmptyString >> 8));
        bytes_.push_back(static_cast<std::uint8_t>(kEmptyString));
        return *this;
    }

    const std::size_t length = std::min(value.size(), kMaxStringBytes);
    bytes_.push_back(static_cast<std::uint8_t>(length >> 8));
    bytes_.push_back(static_cast<std::uint8_t>(length));
    bytes_.insert(bytes_.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(length));
    return *this;
}

Writer& Writer::raw(const Bytes& value) {
    bytes_.insert(bytes_.end(), value.begin(), value.end());
    return *this;
}

std::uint8_t Reader::u8() {
    // Past the end: the packet is cut short.
    if (left() < 1) {
        ok_ = false;
        return 0;
    }

    return data_[at_++];
}

std::int32_t Reader::i32() {
    // Past the end: the packet is cut short.
    if (left() < 4) {
        ok_ = false;
        at_ = size_;
        return 0;
    }

    const std::uint32_t v = (std::uint32_t{data_[at_]} << 24)
                            | (std::uint32_t{data_[at_ + 1]} << 16)
                            | (std::uint32_t{data_[at_ + 2]} << 8) | std::uint32_t{data_[at_ + 3]};
    at_ += 4;
    return static_cast<std::int32_t>(v);
}

std::int64_t Reader::i64() {
    const auto high = static_cast<std::uint32_t>(i32());
    const auto low = static_cast<std::uint32_t>(i32());
    return static_cast<std::int64_t>((std::uint64_t{high} << 32) | low);
}

std::string Reader::str() {
    // Past the end: the packet is cut short.
    if (left() < 2) {
        ok_ = false;
        at_ = size_;
        return {};
    }

    const auto length = static_cast<std::int16_t>((data_[at_] << 8) | data_[at_ + 1]);
    at_ += 2;

    // The writer marks an empty string with a length under 1, and no bytes follow.
    if (length < 1) {
        return {};
    }

    // Past the end: the packet is cut short.
    if (left() < static_cast<std::size_t>(length)) {
        ok_ = false;
        at_ = size_;
        return {};
    }

    std::string out(reinterpret_cast<const char*>(data_ + at_), static_cast<std::size_t>(length));
    at_ += static_cast<std::size_t>(length);
    return out;
}

Bytes Reader::rest() {
    Bytes out(data_ + at_, data_ + size_);
    at_ = size_;
    return out;
}

Bytes frame(const Bytes& body) {
    Bytes out = Writer().i32(static_cast<std::int32_t>(body.size())).bytes();
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

void FrameReader::feed(const std::uint8_t* data, std::size_t size) {
    buffer_.insert(buffer_.end(), data, data + size);
}

bool FrameReader::next(Bytes& body) {
    // A broken stream gives nothing more, and no length is whole yet.
    if (broken_ || buffer_.size() < kLengthBytes) {
        return false;
    }

    Reader r(buffer_.data(), kLengthBytes);
    const auto length = static_cast<std::uint32_t>(r.i32());

    // Longer than any frame the server sends: the stream is out of step.
    if (length > kMaxBodyBytes) {
        broken_ = true;
        return false;
    }

    // The body has not all come yet.
    if (buffer_.size() < kLengthBytes + length) {
        return false;
    }

    const auto begin = buffer_.begin() + static_cast<std::ptrdiff_t>(kLengthBytes);
    body.assign(begin, begin + static_cast<std::ptrdiff_t>(length));
    buffer_.erase(buffer_.begin(), begin + static_cast<std::ptrdiff_t>(length));
    return true;
}

void FrameReader::reset() {
    buffer_.clear();
    broken_ = false;
}

Bytes hello(const std::string& version, const std::string& name) {
    return frame(
        Writer().u8(static_cast<std::uint8_t>(PacketId::kHello)).str(version).str(name).bytes()
    );
}

Bytes party_create() {
    return frame(party(PartyOp::kCreate).bytes());
}

Bytes party_find() {
    return frame(party(PartyOp::kFind).bytes());
}

Bytes party_join(const std::string& code) {
    return frame(party(PartyOp::kJoin).str(code).bytes());
}

Bytes party_leave() {
    return frame(party(PartyOp::kLeave).bytes());
}

Bytes chat(const std::string& message) {
    return frame(Writer().u8(static_cast<std::uint8_t>(PacketId::kChat)).str(message).bytes());
}

Bytes data(std::int32_t target, const Bytes& payload) {
    return frame(
        Writer().u8(static_cast<std::uint8_t>(PacketId::kData)).i32(target).raw(payload).bytes()
    );
}

Bytes time(std::int64_t client_ms) {
    return frame(Writer().u8(static_cast<std::uint8_t>(PacketId::kTime)).i64(client_ms).bytes());
}

bool decode(const Bytes& body, Message& out) {
    out = Message{};
    Reader r(body.data(), body.size());
    const std::uint8_t id = r.u8();

    // An empty frame has no id.
    if (!r.ok()) {
        return false;
    }

    // By the packet id; PARTY goes on below, by its opcode.
    switch (static_cast<PacketId>(id)) {
        case PacketId::kHello:
            out.kind = Message::Kind::kHello;
            out.id = r.i32();
            return r.ok();
        case PacketId::kChat:
            out.kind = Message::Kind::kChat;
            out.id = r.i32();
            out.text = r.str();
            return r.ok();
        case PacketId::kData:
            out.kind = Message::Kind::kData;
            out.id = r.i32();
            out.bytes = r.rest();
            return r.ok();
        case PacketId::kError: {
            out.kind = Message::Kind::kError;
            const std::uint8_t reason = r.u8();
            out.reason = static_cast<ErrorReason>(reason);
            return r.ok() && reason <= static_cast<std::uint8_t>(ErrorReason::kNotInParty);
        }
        case PacketId::kTime:
            out.kind = Message::Kind::kTime;
            out.client_ms = r.i64();
            out.server_ms = r.i64();
            return r.ok();
        case PacketId::kParty:
            break;
        default:
            // An id the client does not know.
            return false;
    }

    // A PARTY packet: by its opcode.
    switch (static_cast<PartyOp>(r.u8())) {
        case PartyOp::kInfo: {
            out.kind = Message::Kind::kPartyInfo;
            out.code = r.str();
            out.host = r.i32();
            const std::uint8_t count = r.u8();

            // Each member: an id and a name.
            for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
                Member m;
                m.id = r.i32();
                m.name = r.str();
                out.members.push_back(m);
            }

            return r.ok();
        }
        case PartyOp::kJoined:
            out.kind = Message::Kind::kPartyJoined;
            out.id = r.i32();
            out.text = r.str();
            return r.ok();
        case PartyOp::kLeft:
            out.kind = Message::Kind::kPartyLeft;
            out.id = r.i32();
            return r.ok();
        case PartyOp::kEnd:
            out.kind = Message::Kind::kPartyEnd;
            return r.ok();
        default:
            // CREATE, JOIN, LEAVE and FIND are the client's, and any other opcode is unknown.
            return false;
    }
}

}  // namespace mp
