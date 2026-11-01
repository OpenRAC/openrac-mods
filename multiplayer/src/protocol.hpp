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

#include "kinds.hpp"

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
/* A party's description (CREATE's, the host's save: "Pokitaru, 12 gold bolts"), as the server keeps
 * it: printable ASCII, at most this long; and the most parties LIST answers with. */
constexpr std::size_t kMaxDescriptionLength = 48;
constexpr std::size_t kMaxListedParties = 32;
/* The longest launcher key a game takes (the launcher's are 32 hex digits). */
constexpr std::size_t kMaxKeyLength = 64;
constexpr const char* kCodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

/* The server's port when the mod's setting names none, and the embedded host's by default. */
constexpr int kDefaultPort = 7770;

/* The party code an embedded host (Direct connect, host.hpp) gives its one party: it has no join
 * codes, so INFO carries this instead (a lobby shows the host's address in its place). */
constexpr const char* kDirectCode = "DIRECT";

/* The packet ids (Packets.java), the PARTY opcodes and the ERROR reasons (Opcodes.java). */
enum class PacketId : std::uint8_t {
    kHello = 0,
    kParty = 1,
    kChat = 2,
    kData = 3,
    kError = 4,
    kTime = 5,
    kUdp = 6
};
enum class PartyOp : std::uint8_t {
    kCreate = 0,
    kJoin = 1,
    kLeave = 2,
    kInfo = 3,
    kJoined = 4,
    kLeft = 5,
    kEnd = 6,
    kFind = 7,
    kList = 8,
    /* 9 and 10 are host migration's. */
    kKick = 11,
    kKicked = 12
};
enum class ErrorReason : std::uint8_t {
    kPartyNotFound = 0,
    kPartyFull = 1,
    kAlreadyInParty = 2,
    kNotInParty = 3,
    kNotHost = 4
};

/* The UDP offer's feature bits: which requests the server takes. An older server's offer has none,
 * and a request it does not know closes the connection. */
constexpr std::uint8_t kFeatureKick = 1;

/* DATA's target for every other member of the party (session ids start at 1). */
constexpr std::int32_t kBroadcast = 0;

/* UDP (the server's README, UDP): optional, for game state only. After HELLO's answer the server
 * sends UDP (a 64-bit token and its UDP port, the TCP port). A datagram has no length prefix; the
 * client's is session id i32, token i64, kind u8, then the kind's payload; the server's is kind u8,
 * then the payload. BIND (no payload) binds the sender's address, STATE carries target i32 + bytes
 * from a client and sender i32 + bytes from the server. At most kMaxDatagramBytes either way;
 * anything bigger, cut short, of an unknown kind or with the wrong token is dropped unanswered. */
constexpr std::size_t kMaxDatagramBytes = 1200;
enum class DatagramKind : std::uint8_t {
    kBind = 0,
    kState = 1
};
/* The bytes ahead of STATE's payload: in a client's datagram (id, token, kind, target), and in
 * the server's (kind, sender). */
constexpr std::size_t kClientStateHeader = 4 + 8 + 1 + 4;
constexpr std::size_t kServerStateHeader = 1 + 4;
/* A binding no valid datagram refreshed for this long is not trusted: state goes by TCP again. */
constexpr int kUdpTimeoutMs = 15000;

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

/* Whether `key` could be a launcher's key (the SDK's identity_key): letters and digits, of a length
 * that fits a line of a save's note. Empty is none. */
bool valid_key(const std::string& key);

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
 * party with a description (its host's save; LIST shows it, INFO carries it), joined by its code or
 * from LIST; FIND joins the oldest party with room or hosts a new one; both are answered with INFO.
 * LIST asks for the parties with room (the asker's own left out), answered with LIST. TIME carries
 * the client's clock in milliseconds. */
Bytes hello(const std::string& version, const std::string& name);
Bytes party_create(const std::string& description = "");
Bytes party_list();
Bytes party_find();
Bytes party_join(const std::string& code);
Bytes party_leave();
/* KICK: the host has `member` taken out of the party (the member gets KICKED, the others LEFT). */
Bytes party_kick(std::int32_t member);
Bytes chat(const std::string& message);
Bytes data(std::int32_t target, const Bytes& payload);
Bytes time(std::int64_t client_ms);

/* The client's datagrams (no frame): BIND, and STATE for `target`. */
Bytes datagram_bind(std::int32_t session, std::int64_t token);
Bytes datagram_state(
    std::int32_t session, std::int64_t token, std::int32_t target, const Bytes& payload
);

/* A member of a party as the server lists it. */
struct Member {
    std::int32_t id = 0;
    std::string name;
};

/* A party LIST names: its code, its host's name, how many are in it of how many it takes, and its
 * description. */
struct Listing {
    std::string code;
    std::string host;
    int members = 0;
    int size = 0;
    std::string description;
};

/* What the server says, one frame of it. */
struct Message {
    enum class Kind {
        kHello,
        kPartyInfo,
        kPartyJoined,
        kPartyLeft,
        kPartyEnd,
        kPartyKicked,
        kPartyList,
        kChat,
        kData,
        kError,
        kTime,
        kUdp
    };
    Kind kind = Kind::kHello;
    /* HELLO: the session's id; JOINED and LEFT: the member's; CHAT and DATA: the sender's. */
    std::int32_t id = 0;
    /* INFO: the party's code, its host's id, its members (the host among them) and its description
     * (empty from a server that sends none). */
    std::string code;
    std::int32_t host = 0;
    std::vector<Member> members;
    std::string description;
    /* LIST: the parties. */
    std::vector<Listing> parties;
    /* JOINED: the member's name; CHAT: the message. */
    std::string text;
    /* DATA: the bytes the sender gave. */
    Bytes bytes;
    /* TIME: the client's clock as it sent it, and the server's (ms since the server started). */
    std::int64_t client_ms = 0;
    std::int64_t server_ms = 0;
    /* ERROR: why the server refused. */
    ErrorReason reason = ErrorReason::kPartyNotFound;
    /* UDP: the session's token, the server's UDP port and its feature bits (none from an older one). */
    std::int64_t token = 0;
    std::int32_t udp_port = 0;
    std::uint8_t features = 0;
};

/* Reads a frame's body the server sent into `out`; false for one the client does not know (an
 * unknown id or opcode, a body cut short), which the client ignores. */
bool decode(const Bytes& body, Message& out);

/* Reads a datagram the server sent: a STATE becomes a kData message (the sender and the bytes), as
 * the same state would have come by TCP. False for anything else, which the client drops. */
bool decode_datagram(const std::uint8_t* data, std::size_t size, Message& out);

/* The server's side, for the embedded host of Direct connect (host.hpp): the frames the server
 * sends, and the client's frames and datagrams as the server reads them. */
Bytes hello_reply(std::int32_t session);
Bytes udp_offer(std::int64_t token, std::int32_t port, std::uint8_t features = kFeatureKick);
Bytes party_info(
    const std::string& code,
    std::int32_t host,
    const std::vector<Member>& members,
    const std::string& description = ""
);
Bytes party_list_reply(const std::vector<Listing>& parties);
Bytes party_joined(const Member& member);
Bytes party_left(std::int32_t member);
Bytes party_end();
Bytes party_kicked();
Bytes chat_from(std::int32_t sender, const std::string& message);
Bytes data_from(std::int32_t sender, const Bytes& payload);
Bytes error_reply(ErrorReason reason);
Bytes time_reply(std::int64_t client_ms, std::int64_t server_ms);
Bytes datagram_state_from(std::int32_t sender, const Bytes& payload);

/* A client's frame as the server reads it. kInvalid: an id or opcode a client never sends (ERROR,
 * UDP, the server's party notices, unknown ones) or a body cut short, which closes a connection. */
struct Request {
    enum class Kind {
        kInvalid,
        kHello,
        kCreate,
        kJoin,
        kFind,
        kList,
        kLeave,
        kKick,
        kChat,
        kData,
        kTime
    };
    Kind kind = Kind::kInvalid;
    /* HELLO: the version and the name; JOIN: the code; CREATE: the description; CHAT: the
     * message. */
    std::string version;
    std::string text;
    /* DATA: the target and the bytes; KICK: the member, in target. */
    std::int32_t target = 0;
    Bytes bytes;
    /* TIME: the client's clock. */
    std::int64_t client_ms = 0;
};

Request decode_request(const Bytes& body);

/* A client's datagram as the server reads it; false for one cut short or of an unknown kind. */
struct ClientDatagram {
    std::int32_t session = 0;
    std::int64_t token = 0;
    DatagramKind kind = DatagramKind::kBind;
    std::int32_t target = 0;
    Bytes bytes;
};

bool decode_client_datagram(const std::uint8_t* data, std::size_t size, ClientDatagram& out);

/* A string's length as the server counts it (Java's String.length: UTF-16 units). */
std::size_t utf16_length(const std::string& text);

}  // namespace mp

#endif /* MULTIPLAYER_PROTOCOL_H */
