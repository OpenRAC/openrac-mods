// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The multiplayer mod's network side, without the game:
//   - the protocol's frames, byte for byte as the OpenRAC server's OutputBuffer writes them and its
//     InputBuffer reads them (a 4-byte big-endian length of what follows, the id, the opcode,
//     big-endian integers, strings behind a 16-bit length, -1 for an empty one), and the frame
//     reader cutting frames however the bytes arrive;
//   - the session's lobby against a recorder: hello, a party created and joined, members coming
//     and going, Ready and Start in DATA, refusals, the host leaving, the game's own DATA passed on;
//   - the connection against a server made here on the loopback: TCP, hello sent, the answer read.
//
// The expected bytes are written out by hand from the server's README and its Java encoders.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "connection.hpp"
#include "protocol.hpp"
#include "session.hpp"

namespace {

int g_failures = 0;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

using mp::Bytes;

// Records what the session sends.
struct Recorder : mp::Transport {
    std::vector<Bytes> frames;

    void send(Bytes frame) override { frames.push_back(std::move(frame)); }
};

// A message to decode into and throw away.
mp::Message& x0() {
    static mp::Message m;
    return m;
}

// A server frame built as the Java side builds it.
Bytes server(const mp::Writer& body) {
    return mp::frame(body.bytes());
}

// The body of a frame (its length checked against its size).
Bytes body_of(const Bytes& frame) {
    mp::FrameReader r;
    r.feed(frame.data(), frame.size());
    Bytes body;
    CHECK(r.next(body));
    return body;
}

mp::Message decoded(const Bytes& frame) {
    mp::Message m;
    CHECK(mp::decode(body_of(frame), m));
    return m;
}

void client_frames_are_the_servers_bytes() {
    // HELLO "v1", "Bob": length 10, id 0, then two strings.
    CHECK(mp::hello("v1", "Bob") == (Bytes{0, 0, 0, 10, 0, 0, 2, 'v', '1', 0, 3, 'B', 'o', 'b'}));
    // PARTY CREATE and LEAVE: id 1 and the opcode alone.
    CHECK(mp::party_create() == (Bytes{0, 0, 0, 2, 1, 0}));
    CHECK(mp::party_leave() == (Bytes{0, 0, 0, 2, 1, 2}));
    // PARTY JOIN "abc123": opcode 1 and the code.
    CHECK(
        mp::party_join("abc123") == (Bytes{0, 0, 0, 10, 1, 1, 0, 6, 'a', 'b', 'c', '1', '2', '3'})
    );
    // CHAT "hi"; an empty string is written as length -1 with nothing after.
    CHECK(mp::chat("hi") == (Bytes{0, 0, 0, 5, 2, 0, 2, 'h', 'i'}));
    CHECK(mp::chat("") == (Bytes{0, 0, 0, 3, 2, 0xFF, 0xFF}));
    // DATA to session 258 with three bytes: the target big-endian, the bytes to the end.
    CHECK(mp::data(258, {9, 8, 7}) == (Bytes{0, 0, 0, 8, 3, 0, 0, 1, 2, 9, 8, 7}));
    // PARTY FIND: opcode 7 alone.
    CHECK(mp::party_find() == (Bytes{0, 0, 0, 2, 1, 7}));
    // TIME: id 5, the client's clock as a big-endian long.
    CHECK(mp::time(0x0102030405060708) == (Bytes{0, 0, 0, 9, 5, 1, 2, 3, 4, 5, 6, 7, 8}));
}

void server_frames_are_read() {
    mp::Message m = decoded(server(mp::Writer().u8(0).i32(7)));
    CHECK(m.kind == mp::Message::Kind::kHello && m.id == 7);

    // INFO: code, host id, member count, then id + name per member.
    m = decoded(server(
        mp::Writer().u8(1).u8(3).str("K7MXQ2").i32(4).u8(2).i32(4).str("Host").i32(9).str("Guest")
    ));
    CHECK(m.kind == mp::Message::Kind::kPartyInfo && m.code == "K7MXQ2" && m.host == 4);
    CHECK(m.members.size() == 2 && m.members[1].id == 9 && m.members[1].name == "Guest");

    m = decoded(server(mp::Writer().u8(1).u8(4).i32(5).str("Newbie")));
    CHECK(m.kind == mp::Message::Kind::kPartyJoined && m.id == 5 && m.text == "Newbie");
    m = decoded(server(mp::Writer().u8(1).u8(5).i32(5)));
    CHECK(m.kind == mp::Message::Kind::kPartyLeft && m.id == 5);
    m = decoded(server(mp::Writer().u8(1).u8(6)));
    CHECK(m.kind == mp::Message::Kind::kPartyEnd);
    m = decoded(server(mp::Writer().u8(2).i32(3).str("yo")));
    CHECK(m.kind == mp::Message::Kind::kChat && m.id == 3 && m.text == "yo");
    m = decoded(server(mp::Writer().u8(3).i32(3).u8(42).u8(43)));
    CHECK(m.kind == mp::Message::Kind::kData && m.id == 3 && m.bytes == (Bytes{42, 43}));
    // TIME: the echoed client clock, then the server's, both longs.
    m = decoded(server(mp::Writer().u8(5).i64(1000).i64(-2)));
    CHECK(m.kind == mp::Message::Kind::kTime && m.client_ms == 1000 && m.server_ms == -2);
    CHECK(!mp::decode(Bytes{5, 0, 0, 0, 0, 0, 0, 0, 1}, x0()));
    m = decoded(server(mp::Writer().u8(4).u8(1)));
    CHECK(m.kind == mp::Message::Kind::kError && m.reason == mp::ErrorReason::kPartyFull);

    // An empty string (-1) reads as "", and nothing of what follows is taken for it.
    m = decoded(server(mp::Writer().u8(2).i32(3).str("")));
    CHECK(m.kind == mp::Message::Kind::kChat && m.text.empty());

    // Unknown ids, opcodes the client sends itself, unknown reasons and cut-short bodies are refused.
    mp::Message x;
    CHECK(!mp::decode(Bytes{9}, x));
    CHECK(!mp::decode(Bytes{1, 0}, x));
    CHECK(!mp::decode(Bytes{4, 9}, x));
    CHECK(!mp::decode(Bytes{0, 0, 0}, x));
    CHECK(!mp::decode(Bytes{}, x));
    CHECK(!mp::decode(Bytes{1, 3, 0, 6, 'A'}, x));
}

void frames_are_cut_however_they_arrive() {
    const Bytes a = mp::frame({1, 2, 3});
    const Bytes b = mp::frame({4});
    Bytes both = a;
    both.insert(both.end(), b.begin(), b.end());
    mp::FrameReader r;
    Bytes body;

    // A byte at a time: nothing until a frame is whole.
    for (std::size_t i = 0; i < both.size(); ++i) {
        r.feed(&both[i], 1);

        if (i + 1 < a.size()) {
            CHECK(!r.next(body));
        }
    }

    CHECK(r.next(body) && body == (Bytes{1, 2, 3}));
    CHECK(r.next(body) && body == (Bytes{4}));
    CHECK(!r.next(body));

    // A length past the largest frame breaks the stream.
    const Bytes huge{0, 1, 0, 0};
    r.feed(huge.data(), huge.size());
    CHECK(!r.next(body) && r.broken());
}

void a_party_is_created_joined_and_left() {
    Recorder host_link;
    mp::Session host(host_link, "v", "Hosty");

    // Nothing before hello is answered.
    host.create();
    CHECK(host_link.frames.empty());
    host.connected();
    CHECK(host_link.frames.size() == 1 && host_link.frames[0] == mp::hello("v", "Hosty"));
    host.handle(decoded(server(mp::Writer().u8(0).i32(1))));
    CHECK(host.greeted() && host.id() == 1);
    // The answer to hello asks the server's clock.
    CHECK(host_link.frames.size() == 2 && body_of(host_link.frames[1])[0] == 5);
    host.create();
    CHECK(host.waiting() && host_link.frames.back() == mp::party_create());
    host.handle(
        decoded(server(mp::Writer().u8(1).u8(3).str("ABCDEF").i32(1).u8(1).i32(1).str("Hosty")))
    );
    CHECK(host.in_party() && host.is_host() && host.code() == "ABCDEF" && !host.waiting());

    // A member joins: the host tells everyone who is ready.
    host.handle(decoded(server(mp::Writer().u8(1).u8(4).i32(2).str("Guesty"))));
    CHECK(host.members().size() == 2 && host.members()[1].name == "Guesty");
    CHECK(
        host_link.frames.back() == mp::data(mp::kBroadcast, {2, 2, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0})
    );

    // The member's side: joins by code, its READY goes to the host.
    Recorder guest_link;
    mp::Session guest(guest_link, "v", "Guesty");
    guest.connected();
    guest.handle(decoded(server(mp::Writer().u8(0).i32(2))));
    guest.join("ABC");  // too short: nothing sent (hello and the clock's TIME only)
    CHECK(guest_link.frames.size() == 2);
    guest.join("ABCDEF");
    CHECK(guest_link.frames.back() == mp::party_join("ABCDEF"));
    // The server lists the members in its own order; the host comes first on the page.
    guest.handle(decoded(server(
        mp::Writer().u8(1).u8(3).str("ABCDEF").i32(1).u8(2).i32(2).str("Guesty").i32(1).str("Hosty")
    )));
    CHECK(guest.in_party() && !guest.is_host() && guest.members()[0].id == 1);
    guest.toggle_ready();
    CHECK(guest.ready(2) && guest_link.frames.back() == mp::data(1, {1, 1}));
    guest.start();  // only the host starts
    CHECK(!guest.started());

    // The host hears the READY (the server gives the sender's id) and says so to everyone.
    host.handle(decoded(server(mp::Writer().u8(3).i32(2).u8(1).u8(1))));
    CHECK(host.ready(2));
    CHECK(
        host_link.frames.back() == mp::data(mp::kBroadcast, {2, 2, 0, 0, 0, 1, 0, 0, 0, 0, 2, 1})
    );

    // STATE from someone else than the host is not believed; from the host it is.
    guest.handle(decoded(server(mp::Writer().u8(3).i32(2).u8(2).u8(1).i32(2).u8(0))));
    CHECK(guest.ready(2));
    guest.handle(decoded(server(mp::Writer().u8(3).i32(1).u8(2).u8(1).i32(2).u8(0))));
    CHECK(!guest.ready(2));

    // Start, from the host to everyone.
    host.start();
    CHECK(host.started() && host_link.frames.back() == mp::data(mp::kBroadcast, {3}));
    CHECK(!host.take_notice().empty() && host.take_notice().empty());
    guest.handle(decoded(server(mp::Writer().u8(3).i32(1).u8(3))));
    CHECK(guest.started());

    // The game's own DATA goes to the callback, the lobby's never; and is sent as it is.
    Bytes got;
    guest.on_data([&](std::int32_t sender, const Bytes& bytes) {
        CHECK(sender == 1);
        got = bytes;
    });
    guest.handle(decoded(server(mp::Writer().u8(3).i32(1).u8(16).u8(5))));
    CHECK(got == (Bytes{16, 5}));
    guest.send_data(1, {16, 6});
    CHECK(guest_link.frames.back() == mp::data(1, {16, 6}));
    const std::size_t sent = guest_link.frames.size();
    guest.send_data(1, {3});  // a lobby kind: not sent
    CHECK(guest_link.frames.size() == sent);
    CHECK(mp::now_ms() > 0);

    // A member leaves; then the host leaves and the party ends for the guest.
    host.handle(decoded(server(mp::Writer().u8(1).u8(5).i32(2))));
    CHECK(host.members().size() == 1 && !host.ready(2));
    guest.handle(decoded(server(mp::Writer().u8(1).u8(6))));
    CHECK(!guest.in_party() && guest.take_notice() == "The host left: the party is over");

    // A refusal: a line on the page, nothing pending any more.
    guest.join("ZZZZZZ");
    CHECK(guest.waiting());
    guest.handle(decoded(server(mp::Writer().u8(4).u8(0))));
    CHECK(!guest.waiting() && guest.take_notice() == "No party has that code");

    // Leaving: the frame, and the party is gone at once.
    host.leave();
    CHECK(host_link.frames.back() == mp::party_leave() && !host.in_party());
}

// FIND is sent like CREATE; TIME's answers give the round trip and a smoothed offset.
void match_finding_and_the_clock() {
    Recorder link;
    std::uint64_t now = 1000;
    mp::Session s(link, "v", "Finder", [&] { return now; });
    s.connected();
    s.handle(decoded(server(mp::Writer().u8(0).i32(3))));
    CHECK(link.frames.back() == mp::time(1000) && !s.has_clock());

    // Sent at 1000, answered at 1100 with the server at 50000: 100 ms there and back, so the
    // server read its clock at 1050 of ours, an offset of 48950.
    now = 1100;
    s.handle(decoded(server(mp::Writer().u8(5).i64(1000).i64(50000))));
    CHECK(s.has_clock() && s.rtt_ms() == 100 && s.clock_offset_ms() == 48950.0);
    CHECK(s.server_ms() == 1100 + 48950);

    // A second answer 100 ms further off moves the offset by 5 % of it.
    s.sync_clock();
    CHECK(link.frames.back() == mp::time(1100));
    now = 1120;
    s.handle(decoded(server(mp::Writer().u8(5).i64(1100).i64(50160))));
    CHECK(s.rtt_ms() == 20);
    CHECK(s.clock_offset_ms() > 48954.9 && s.clock_offset_ms() < 48955.1);

    s.find();
    CHECK(link.frames.back() == mp::party_find() && s.waiting());
    s.find();  // pending: not sent twice
    CHECK(link.frames.size() == 4);
    s.handle(decoded(server(
        mp::Writer().u8(1).u8(3).str("OPEN22").i32(9).u8(2).i32(9).str("Other").i32(3).str("Finder")
    )));
    CHECK(s.in_party() && !s.is_host() && s.host() == 9);
}

void names_and_addresses() {
    CHECK(mp::valid_name("Brave_Lombax42") && mp::valid_name("abc"));
    CHECK(
        !mp::valid_name("ab") && !mp::valid_name("x2345678901234567") && !mp::valid_name("a b c")
    );
    std::string host;
    int port = 0;
    CHECK(mp::parse_address("localhost:7770", host, port) && host == "localhost" && port == 7770);
    CHECK(
        mp::parse_address("play.example.org", host, port) && host == "play.example.org"
        && port == 7770
    );
    CHECK(mp::parse_address("10.0.0.2:9000", host, port) && host == "10.0.0.2" && port == 9000);
    CHECK(mp::parse_address("[::1]:9001", host, port) && host == "::1" && port == 9001);
    CHECK(mp::parse_address("host:notaport", host, port) && port == 7770);
    CHECK(!mp::parse_address("", host, port) && !mp::parse_address(":7770", host, port));
}

#if defined(_WIN32)
using socket_t = SOCKET;

void close_socket(socket_t fd) {
    closesocket(fd);
}
#else
using socket_t = int;

void close_socket(socket_t fd) {
    ::close(fd);
}
#endif

// A server on the loopback: takes one connection, checks the HELLO, answers with session id 7.
void the_connection_talks_tcp() {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    const socket_t listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    CHECK(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    CHECK(listen(listener, 1) == 0);
    socklen_t length = sizeof address;
    getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
    const int port = ntohs(address.sin_port);

    Bytes heard;
    std::thread server_thread([&] {
        const socket_t client = accept(listener, nullptr, nullptr);
        const Bytes expected = mp::hello("v", "Tester");
        heard.resize(expected.size());
        std::size_t got = 0;

        while (got < heard.size()) {
            const auto n = recv(
                client,
                reinterpret_cast<char*>(heard.data() + got),
                static_cast<int>(heard.size() - got),
                0
            );

            if (n <= 0) {
                break;
            }
            got += static_cast<std::size_t>(n);
        }
        const Bytes answer = mp::frame(mp::Writer().u8(0).i32(7).bytes());
        send(
            client, reinterpret_cast<const char*>(answer.data()), static_cast<int>(answer.size()), 0
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        close_socket(client);
    });

    mp::Connection connection;
    mp::Session session(connection, "v", "Tester");
    connection.open("127.0.0.1", port);
    std::uint32_t generation = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    // As the page does each frame: hello on a new connection, then the messages.
    while (!session.greeted() && std::chrono::steady_clock::now() < until) {
        if (connection.state() == mp::Connection::State::kConnected
            && connection.generation() != generation) {
            generation = connection.generation();
            session.connected();
        }

        for (const mp::Message& m : connection.receive()) {
            session.handle(m);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(session.greeted() && session.id() == 7);
    server_thread.join();
    CHECK(heard == mp::hello("v", "Tester"));
    connection.close();
    CHECK(connection.state() == mp::Connection::State::kIdle);
    close_socket(listener);
}

}  // namespace

int main() {
    client_frames_are_the_servers_bytes();
    server_frames_are_read();
    frames_are_cut_however_they_arrive();
    a_party_is_created_joined_and_left();
    match_finding_and_the_clock();
    names_and_addresses();
    the_connection_talks_tcp();

    // The count of failed checks is the whole verdict.
    if (g_failures == 0) {
        std::puts("multiplayer: all passed");
    }

    return g_failures == 0 ? 0 : 1;
}
