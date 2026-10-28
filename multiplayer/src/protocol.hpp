/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The OpenRAC server's wire format, the client's side (protocol.cpp; the server's README and its
 * common module, com.openrac.net, are the reference). Every packet is a frame: a 4-byte big-endian
 * length (of what follows it), the packet id byte, an opcode byte for the packets that have one,
 * then the payload. Integers are big-endian; a string is a 2-byte length then its UTF-8 bytes, an
 * empty one written as length -1. A frame is at most 65,536 bytes, its length included.
 *
 * Plain bytes in and out: no socket here (connection.cpp has it), so the tests check the bytes.
 */
#ifndef MULTIPLAYER_PROTOCOL_H
#define MULTIPLAYER_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mp {

using Bytes = std::vector<std::uint8_t>;

/* The length prefix's bytes, the longest frame (its prefix included) and so the longest body. */
constexpr std::size_t kLengthBytes = 4;
constexpr std::size_t kMaxFrameBytes = 65536;
constexpr std::size_t kMaxBodyBytes = kMaxFrameBytes - kLengthBytes;

/* The server's limits: a name and a version 1 to 32 characters, a chat message 1 to 256, a party
 * of 4 (its host included), a party code of 6 characters from kCodeAlphabet (no 0, O, 1 or I). */
constexpr std::size_t kMaxNameLength = 32;
constexpr std::size_t kMaxChatLength = 256;
constexpr std::size_t kMaxPartySize = 4;
constexpr std::size_t kCodeLength = 6;
constexpr const char* kCodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

/* The server's port when the mod's setting names none. */
constexpr int kDefaultPort = 7770;

/* The packet ids (Packets.java), the PARTY opcodes and the ERROR reasons (Opcodes.java). */
enum class PacketId : std::uint8_t {
    kHello = 0,
    kParty = 1,
    kChat = 2,
    kData = 3,
    kError = 4,
    kTime = 5
};
enum class PartyOp : std::uint8_t {
    kCreate = 0,
    kJoin = 1,
    kLeave = 2,
    kInfo = 3,
    kJoined = 4,
    kLeft = 5,
    kEnd = 6,
    kFind = 7
};
enum class ErrorReason : std::uint8_t {
    kPartyNotFound = 0,
    kPartyFull = 1,
    kAlreadyInParty = 2,
    kNotInParty = 3
};

/* DATA's target for every other member of the party (session ids start at 1). */
constexpr std::int32_t kBroadcast = 0;

/* A body being written: bytes, big-endian integers, strings as the server reads them. */
class Writer {
public:
    Writer& u8(std::uint8_t value);
    Writer& i32(std::int32_t value);
    Writer& i64(std::int64_t value);
    Writer& str(const std::string& value);
    Writer& raw(const Bytes& value);

    const Bytes& bytes() const { return bytes_; }

private:
    Bytes bytes_;
};

/* A body being read. A read past the end gives zero or empty and makes ok() false, as the server's
 * underflow makes a packet malformed. */
class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::uint8_t u8();
    std::int32_t i32();
    std::int64_t i64();
    std::string str();
    Bytes rest();

    bool ok() const { return ok_; }

    std::size_t left() const { return size_ - at_; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

/* The frame of a body: its length, big-endian, then the body. */
Bytes frame(const Bytes& body);

/* Cuts frames out of the bytes a socket gives, however they arrive. broken() once a length is
 * larger than the server ever sends: the stream can no longer be trusted. */
class FrameReader {
public:
    void feed(const std::uint8_t* data, std::size_t size);
    /* The next whole frame's body into `body`; false when none is whole yet. */
    bool next(Bytes& body);

    bool broken() const { return broken_; }

    void reset();

private:
    Bytes buffer_;
    bool broken_ = false;
};

/* The frames the client sends: HELLO first, then PARTY, CHAT, DATA and TIME. CREATE makes a
 * private party (joined by its code), FIND joins the oldest open party with room or hosts a new
 * open one; both are answered with INFO. TIME carries the client's clock in milliseconds. */
Bytes hello(const std::string& version, const std::string& name);
Bytes party_create();
Bytes party_find();
Bytes party_join(const std::string& code);
Bytes party_leave();
Bytes chat(const std::string& message);
Bytes data(std::int32_t target, const Bytes& payload);
Bytes time(std::int64_t client_ms);

/* A member of a party as the server lists it. */
struct Member {
    std::int32_t id = 0;
    std::string name;
};

/* What the server says, one frame of it. */
struct Message {
    enum class Kind {
        kHello,
        kPartyInfo,
        kPartyJoined,
        kPartyLeft,
        kPartyEnd,
        kChat,
        kData,
        kError,
        kTime
    };
    Kind kind = Kind::kHello;
    /* HELLO: the session's id; JOINED and LEFT: the member's; CHAT and DATA: the sender's. */
    std::int32_t id = 0;
    /* INFO: the party's code, its host's id and its members (the host among them). */
    std::string code;
    std::int32_t host = 0;
    std::vector<Member> members;
    /* JOINED: the member's name; CHAT: the message. */
    std::string text;
    /* DATA: the bytes the sender gave. */
    Bytes bytes;
    /* TIME: the client's clock as it sent it, and the server's (ms since the server started). */
    std::int64_t client_ms = 0;
    std::int64_t server_ms = 0;
    /* ERROR: why the server refused. */
    ErrorReason reason = ErrorReason::kPartyNotFound;
};

/* Reads a frame's body the server sent into `out`; false for one the client does not know (an
 * unknown id or opcode, a body cut short), which the client ignores. */
bool decode(const Bytes& body, Message& out);

}  // namespace mp

#endif /* MULTIPLAYER_PROTOCOL_H */
