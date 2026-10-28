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
//   - the connection against a server made here on the loopback: TCP, hello sent, the answer read;
//   - Direct connect: the embedded host (host.hpp) in-process with two clients on the loopback:
//     hello, the unasked INFO, chat, DATA both ways, the lobby, leave and find again, UDP BIND and
//     STATE, the host stopping or leaving (END); and the server's limits, from raw sockets;
//   - Direct connect's page logic: the address's ring rows, held steps, the hosting lines, and
//     the player's name chosen from the setting and the launcher's.
//
// The expected bytes are written out by hand from the server's README and its Java encoders.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "direct.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <poll.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "connection.hpp"
#include "host.hpp"
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

// One player for the Direct connect tests: a connection, its session, and what came in, pumped
// as the page does each frame.
struct Player {
    mp::Connection connection;
    std::unique_ptr<mp::Session> session;
    std::uint32_t generation = 0;
    std::vector<mp::Message> log;

    explicit Player(const std::string& name) {
        session = std::make_unique<mp::Session>(connection, "rac1-multiplayer/test", name);
    }

    void pump() {
        const bool connected = connection.state() == mp::Connection::State::kConnected;

        // A new link: hello again; a lost one: no party.
        if (connected && connection.generation() != generation) {
            generation = connection.generation();
            session->connected();
        } else if (!connected && session->greeted()) {
            session->disconnected();
        }

        for (const mp::Message& m : connection.receive()) {
            log.push_back(m);
            session->handle(m);
        }
    }

    // How many messages of `kind` (from `id`, when not 0) came in, and forgets them.
    int count(mp::Message::Kind kind, std::int32_t id = 0, const mp::Bytes* bytes = nullptr) {
        int n = 0;

        for (const mp::Message& m : log) {
            if (m.kind == kind && (id == 0 || m.id == id)
                && (bytes == nullptr || m.bytes == *bytes)) {
                ++n;
            }
        }

        return n;
    }
};

// Pumps every player until `done` (true) or `ms` pass (false).
bool pump_until(
    const std::vector<Player*>& players, const std::function<bool()>& done, int ms = 3000
) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

    while (std::chrono::steady_clock::now() < until) {
        for (Player* p : players) {
            p->pump();
        }

        if (done()) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return false;
}

// A raw client of the host, for the limits.
socket_t raw_connect(int port) {
    const socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    CHECK(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    return fd;
}

void raw_send(socket_t fd, const Bytes& bytes) {
    send(fd, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
}

// Whether the host closes the socket within `ms` (what it sends meanwhile is read and dropped).
bool closed_within(socket_t fd, int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

    while (std::chrono::steady_clock::now() < until) {
#if defined(_WIN32)
        WSAPOLLFD p{};
        p.fd = fd;
        p.events = POLLIN;
        const int ready = WSAPoll(&p, 1, 20);
#else
        pollfd p{};
        p.fd = fd;
        p.events = POLLIN;
        const int ready = poll(&p, 1, 20);
#endif

        if (ready > 0) {
            char buffer[4096];
            const auto n = recv(fd, buffer, static_cast<int>(sizeof buffer), 0);

            if (n <= 0) {
                return true;
            }
        }
    }

    return false;
}

// Reads frames from a raw socket for `ms` and gives what came, decoded.
std::vector<mp::Message> raw_read(socket_t fd, int ms) {
    mp::FrameReader frames;
    std::vector<mp::Message> out;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

    while (std::chrono::steady_clock::now() < until) {
#if defined(_WIN32)
        WSAPOLLFD p{};
        p.fd = fd;
        p.events = POLLIN;
        const int ready = WSAPoll(&p, 1, 20);
#else
        pollfd p{};
        p.fd = fd;
        p.events = POLLIN;
        const int ready = poll(&p, 1, 20);
#endif

        if (ready <= 0) {
            continue;
        }

        std::uint8_t buffer[4096];
        const auto n =
            recv(fd, reinterpret_cast<char*>(buffer), static_cast<int>(sizeof buffer), 0);

        if (n <= 0) {
            break;
        }

        frames.feed(buffer, static_cast<std::size_t>(n));
        Bytes body;

        while (frames.next(body)) {
            mp::Message m;

            if (mp::decode(body, m)) {
                out.push_back(m);
            }
        }
    }

    return out;
}

// The server's frames from the host's side, byte for byte as the Java encoders write them.
void host_frames_are_the_servers_bytes() {
    CHECK(mp::hello_reply(7) == server(mp::Writer().u8(0).i32(7)));
    CHECK(
        mp::udp_offer(0x0102030405060708LL, 7770)
        == server(mp::Writer().u8(6).i64(0x0102030405060708LL).i32(7770))
    );
    CHECK(
        mp::party_info("DIRECT", 1, {{1, "A"}, {2, "B"}})
        == server(
            mp::Writer().u8(1).u8(3).str("DIRECT").i32(1).u8(2).i32(1).str("A").i32(2).str("B")
        )
    );
    CHECK(mp::party_joined({2, "B"}) == server(mp::Writer().u8(1).u8(4).i32(2).str("B")));
    CHECK(mp::party_left(2) == server(mp::Writer().u8(1).u8(5).i32(2)));
    CHECK(mp::party_end() == server(mp::Writer().u8(1).u8(6)));
    CHECK(mp::chat_from(3, "hi") == server(mp::Writer().u8(2).i32(3).str("hi")));
    CHECK(mp::data_from(3, {9, 8}) == server(mp::Writer().u8(3).i32(3).u8(9).u8(8)));
    CHECK(mp::error_reply(mp::ErrorReason::kPartyFull) == server(mp::Writer().u8(4).u8(1)));
    CHECK(mp::time_reply(5, 6) == server(mp::Writer().u8(5).i64(5).i64(6)));

    // Datagrams: no frame; the client's id, token and kind first, the server's kind and sender.
    CHECK(mp::datagram_bind(3, 9) == mp::Writer().i32(3).i64(9).u8(0).bytes());
    CHECK(
        mp::datagram_state(3, 9, 0, {7}) == mp::Writer().i32(3).i64(9).u8(1).i32(0).u8(7).bytes()
    );
    CHECK(mp::datagram_state_from(3, {7}) == mp::Writer().u8(1).i32(3).u8(7).bytes());

    // The UDP offer as the client reads it.
    const mp::Message offer = decoded(mp::udp_offer(-2, 7771));
    CHECK(offer.kind == mp::Message::Kind::kUdp && offer.token == -2 && offer.udp_port == 7771);

    // A server's STATE datagram reads as the DATA it stands for; anything else is dropped.
    const Bytes state = mp::datagram_state_from(4, {1, 2});
    mp::Message m;
    CHECK(mp::decode_datagram(state.data(), state.size(), m));
    CHECK(m.kind == mp::Message::Kind::kData && m.id == 4 && m.bytes == Bytes({1, 2}));
    const Bytes bind = mp::Writer().u8(0).bytes();
    CHECK(!mp::decode_datagram(bind.data(), bind.size(), m));
    const Bytes big(mp::kMaxDatagramBytes + 1, 1);
    CHECK(!mp::decode_datagram(big.data(), big.size(), m));

    // The client's frames, as the host reads them.
    CHECK(mp::decode_request(body_of(mp::hello("v", "N"))).kind == mp::Request::Kind::kHello);
    CHECK(mp::decode_request(body_of(mp::hello("v", "N"))).text == "N");
    CHECK(mp::decode_request(body_of(mp::party_join("abc"))).text == "abc");
    CHECK(mp::decode_request(body_of(mp::party_find())).kind == mp::Request::Kind::kFind);
    CHECK(mp::decode_request(body_of(mp::party_leave())).kind == mp::Request::Kind::kLeave);
    CHECK(mp::decode_request(body_of(mp::data(5, {1}))).target == 5);
    CHECK(mp::decode_request(body_of(mp::time(42))).client_ms == 42);
    // ERROR, UDP, a server's party notice, an unknown id and a cut body are all invalid.
    CHECK(
        mp::decode_request(body_of(mp::error_reply(mp::ErrorReason::kPartyFull))).kind
        == mp::Request::Kind::kInvalid
    );
    CHECK(mp::decode_request(body_of(mp::udp_offer(1, 1))).kind == mp::Request::Kind::kInvalid);
    CHECK(mp::decode_request(body_of(mp::party_end())).kind == mp::Request::Kind::kInvalid);
    CHECK(mp::decode_request(Bytes{9}).kind == mp::Request::Kind::kInvalid);
    CHECK(mp::decode_request(Bytes{3, 0, 0}).kind == mp::Request::Kind::kInvalid);

    mp::ClientDatagram d;
    const Bytes st = mp::datagram_state(3, 9, 2, {7, 7});
    CHECK(mp::decode_client_datagram(st.data(), st.size(), d));
    CHECK(
        d.session == 3 && d.token == 9 && d.kind == mp::DatagramKind::kState && d.target == 2
        && d.bytes == Bytes({7, 7})
    );
    const Bytes unknown = mp::Writer().i32(3).i64(9).u8(2).bytes();
    CHECK(!mp::decode_client_datagram(unknown.data(), unknown.size(), d));
    const Bytes cut = mp::Writer().i32(3).i64(9).bytes();
    CHECK(!mp::decode_client_datagram(cut.data(), cut.size(), d));

    // Lengths as Java counts them: UTF-16 units.
    CHECK(mp::utf16_length("abc") == 3);
    CHECK(mp::utf16_length("\xc3\xa9") == 1);
    CHECK(mp::utf16_length("\xf0\x9f\x98\x80") == 2);
}

// A host and two clients on the loopback, as three games would play.
void direct_connect_plays_a_party() {
    Player host("Host");
    Player a("Alice");
    Player b("Bob");
    const std::vector<Player*> all{&host, &a, &b};

    CHECK(host.connection.start_hosting(0));
    CHECK(host.connection.mode() == mp::Connection::Mode::kHosting);
    const mp::HostingStatus status = host.connection.hosting();
    CHECK(status.listening && status.port > 0 && status.udp && status.error.empty());
    const std::string address = "127.0.0.1:" + std::to_string(status.port);

    // The host's own game: greeted, and the host of the one party at once.
    CHECK(pump_until(all, [&] { return host.session->in_party(); }));
    CHECK(host.session->is_host() && host.session->direct() && host.session->members().size() == 1);

    // The clients join by address alone: the party comes after hello, unasked.
    CHECK(!a.connection.connect_direct(""));
    CHECK(a.connection.connect_direct(address));
    CHECK(
        a.connection.mode() == mp::Connection::Mode::kDirect && a.connection.address() == address
    );
    CHECK(pump_until(all, [&] { return a.session->in_party(); }));
    CHECK(b.connection.connect_direct(address));
    CHECK(pump_until(all, [&] {
        return b.session->in_party() && host.session->members().size() == 3
               && a.session->members().size() == 3;
    }));
    CHECK(!a.session->is_host() && a.session->host() == host.session->id() && a.session->direct());
    CHECK(host.connection.hosting().members.size() == 3);
    CHECK(host.connection.hosting().members[0].name == "Host");
    CHECK(b.count(mp::Message::Kind::kUdp) == 1);

    // The clock: every session has an estimate.
    CHECK(pump_until(all, [&] {
        return host.session->has_clock() && a.session->has_clock() && b.session->has_clock();
    }));

    // A second FIND or JOIN in the party already is refused, as on the server.
    a.connection.send(mp::party_find());
    CHECK(pump_until(all, [&] { return a.count(mp::Message::Kind::kError) == 1; }));
    CHECK(a.session->take_notice() == mp::error_text(mp::ErrorReason::kAlreadyInParty));

    // Chat reaches everyone, the sender too.
    a.connection.send(mp::chat("hello"));
    CHECK(pump_until(all, [&] {
        return host.count(mp::Message::Kind::kChat, a.session->id()) == 1
               && a.count(mp::Message::Kind::kChat, a.session->id()) == 1
               && b.count(mp::Message::Kind::kChat, a.session->id()) == 1;
    }));

    // DATA: a member's to the host only, whatever its target.
    const Bytes from_a{0x10, 1};
    a.session->send_data(b.session->id(), from_a);
    CHECK(pump_until(all, [&] {
        return host.count(mp::Message::Kind::kData, a.session->id(), &from_a) == 1;
    }));
    CHECK(b.count(mp::Message::Kind::kData, a.session->id(), &from_a) == 0);

    // The host's to everyone else (kBroadcast), or to the one member named.
    const Bytes to_all{0x10, 2};
    const Bytes to_b{0x10, 3};
    host.session->send_data(mp::kBroadcast, to_all);
    host.session->send_data(b.session->id(), to_b);
    CHECK(pump_until(all, [&] {
        return a.count(mp::Message::Kind::kData, 0, &to_all) == 1
               && b.count(mp::Message::Kind::kData, 0, &to_all) == 1
               && b.count(mp::Message::Kind::kData, 0, &to_b) == 1;
    }));
    CHECK(a.count(mp::Message::Kind::kData, 0, &to_b) == 0);
    CHECK(host.count(mp::Message::Kind::kData, 0, &to_all) == 0);

    // The lobby rides on it unchanged: Ready reaches the host, and its STATE the others.
    a.session->toggle_ready();
    CHECK(pump_until(all, [&] {
        return host.session->ready(a.session->id()) && b.session->ready(a.session->id());
    }));

    // UDP: state over datagrams once bound, both ways; confirmed when datagrams come back.
    const Bytes pose_host{0x20, 1};
    const Bytes pose_a{0x20, 2};
    CHECK(pump_until(all, [&] {
        host.session->send_state(mp::kBroadcast, pose_host);
        a.session->send_state(mp::kBroadcast, pose_a);
        b.session->send_state(mp::kBroadcast, pose_a);
        return a.connection.udp() && b.connection.udp();
    }));
    CHECK(host.count(mp::Message::Kind::kData, a.session->id(), &pose_a) >= 1);
    CHECK(a.count(mp::Message::Kind::kData, host.session->id(), &pose_host) >= 1);
    CHECK(b.count(mp::Message::Kind::kData, a.session->id(), &pose_a) == 0);

    // State too large for a datagram goes as DATA, and arrives all the same.
    const Bytes big_pose = [] {
        Bytes x(1500, 7);
        x[0] = 0x20;
        return x;
    }();
    host.session->send_state(a.session->id(), big_pose);
    CHECK(pump_until(all, [&] {
        return a.count(mp::Message::Kind::kData, host.session->id(), &big_pose) == 1;
    }));

    // A member leaves: the others hear it; FIND brings it back into the one party.
    const std::int32_t bob = b.session->id();
    b.session->leave();
    CHECK(pump_until(all, [&] {
        return host.count(mp::Message::Kind::kPartyLeft, bob) == 1
               && a.session->members().size() == 2;
    }));
    b.session->find();
    CHECK(pump_until(all, [&] {
        return b.session->in_party() && host.session->members().size() == 3;
    }));

    // The host stops: END for everyone, and its game is linked to nothing.
    host.connection.stop_hosting();
    CHECK(host.connection.mode() == mp::Connection::Mode::kNone);
    CHECK(host.connection.state() == mp::Connection::State::kIdle);
    CHECK(pump_until(all, [&] {
        return a.count(mp::Message::Kind::kPartyEnd) == 1
               && b.count(mp::Message::Kind::kPartyEnd) == 1;
    }));
    CHECK(!a.session->in_party() && !b.session->in_party());
    a.connection.close();
    b.connection.close();
    host.pump();
    CHECK(!host.session->in_party());
}

// The host leaving its own party ends it, and the hosting stops with it.
void the_host_leaving_ends_hosting() {
    Player host("Host");
    Player a("Alice");
    const std::vector<Player*> all{&host, &a};

    CHECK(host.connection.start_hosting(0));
    const int port = host.connection.hosting().port;
    CHECK(a.connection.connect_direct("127.0.0.1:" + std::to_string(port)));
    CHECK(pump_until(all, [&] {
        return a.session->in_party() && host.session->members().size() == 2;
    }));

    host.session->leave();
    CHECK(host.connection.mode() == mp::Connection::Mode::kNone);
    CHECK(pump_until(all, [&] { return a.count(mp::Message::Kind::kPartyEnd) == 1; }));
    CHECK(!host.connection.hosting().listening);
    a.connection.close();

    // The port is free again: hosting starts anew on it.
    CHECK(host.connection.start_hosting(port));
    host.connection.close();
}

// The server's limits, held by the host.
void the_host_keeps_the_servers_limits() {
    // A port in use is refused, with the words for the page.
    {
        mp::Connection first;
        CHECK(first.start_hosting(0));
        const int port = first.hosting().port;
        mp::Connection second;
        CHECK(!second.start_hosting(port));
        CHECK(second.hosting().error == "Port " + std::to_string(port) + " is in use");
        CHECK(second.mode() == mp::Connection::Mode::kNone);
        first.close();
    }

    mp::HostConfig config;
    config.port = 0;
    config.max_party_size = 2;
    config.hello_timeout_ms = 300;
    config.max_connections_per_address = 6;
    config.chat = {1, 0.001};
    config.strikes = {2, 0.001};
    std::vector<Bytes> local;
    std::mutex local_mutex;
    mp::Host host(config);
    CHECK(host.start([&](const Bytes& f) {
        const std::lock_guard lock(local_mutex);
        local.push_back(f);
    }));
    const int port = host.port();
    host.local_send(mp::hello("v", "Host"));
    CHECK(host.members().size() == 1);

    // Anything before hello closes the connection.
    {
        const socket_t fd = raw_connect(port);
        raw_send(fd, mp::chat("hi"));
        CHECK(closed_within(fd, 2000));
        close_socket(fd);
    }

    // No hello in time: closed.
    {
        const socket_t fd = raw_connect(port);
        CHECK(closed_within(fd, 2000));
        close_socket(fd);
    }

    // A frame longer than any may be: closed.
    {
        const socket_t fd = raw_connect(port);
        raw_send(fd, mp::Writer().i32(70000).u8(0).bytes());
        CHECK(closed_within(fd, 2000));
        close_socket(fd);
    }

    // A name past 32 characters, or blank: closed.
    {
        const socket_t fd = raw_connect(port);
        raw_send(fd, mp::hello("v", std::string(33, 'n')));
        CHECK(closed_within(fd, 2000));
        close_socket(fd);
        const socket_t blank = raw_connect(port);
        raw_send(blank, mp::hello("v", "   "));
        CHECK(closed_within(blank, 2000));
        close_socket(blank);
    }

    // A full party: the newcomer is greeted and refused, but stays connected.
    const socket_t member = raw_connect(port);
    raw_send(member, mp::hello("v", "Member"));
    std::vector<mp::Message> got = raw_read(member, 300);
    CHECK(
        got.size() == 3 && got[0].kind == mp::Message::Kind::kHello
        && got[1].kind == mp::Message::Kind::kUdp && got[2].kind == mp::Message::Kind::kPartyInfo
        && got[2].code == mp::kDirectCode && got[2].members.size() == 2
    );
    const socket_t extra = raw_connect(port);
    raw_send(extra, mp::hello("v", "Extra"));
    got = raw_read(extra, 300);
    CHECK(
        got.size() == 3 && got[2].kind == mp::Message::Kind::kError
        && got[2].reason == mp::ErrorReason::kPartyFull
    );
    CHECK(host.members().size() == 2);

    // Chat over its rate is dropped, then a flood is closed.
    raw_send(member, mp::chat("one"));
    got = raw_read(member, 200);
    CHECK(got.size() == 1 && got[0].kind == mp::Message::Kind::kChat && got[0].text == "one");
    for (int i = 0; i < 5; ++i) {
        raw_send(member, mp::chat("flood"));
    }
    CHECK(closed_within(member, 2000));
    close_socket(member);

    // The member closed: it left, and the one waiting can now find its way in.
    CHECK(pump_until({}, [&] { return host.members().size() == 1; }));
    raw_send(extra, mp::party_join("ANYONE"));
    got = raw_read(extra, 300);
    CHECK(got.size() == 1 && got[0].kind == mp::Message::Kind::kPartyInfo);

    // A forged datagram (a wrong token) is dropped: no state reaches the host.
    {
        const std::int32_t id = host.members().back().id;
        const socket_t udp = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        to.sin_port = htons(static_cast<std::uint16_t>(port));
        const Bytes forged = mp::datagram_state(id, 12345, 0, {0x20});
        {
            const std::lock_guard lock(local_mutex);
            local.clear();
        }
        sendto(
            udp,
            reinterpret_cast<const char*>(forged.data()),
            static_cast<int>(forged.size()),
            0,
            reinterpret_cast<sockaddr*>(&to),
            sizeof to
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const std::lock_guard lock(local_mutex);
        CHECK(local.empty());
        close_socket(udp);
    }

    // Connections from one address past the limit are closed at once.
    std::vector<socket_t> many;
    for (int i = 0; i < 6; ++i) {
        many.push_back(raw_connect(port));
    }
    // extra is one of the address's six already, so the sixth here is past it.
    CHECK(closed_within(many.back(), 1000));
    for (const socket_t fd : many) {
        close_socket(fd);
    }

    // Stopping ends the party: the member left hears END, then the socket closes.
    host.stop();
    got = raw_read(extra, 500);
    CHECK(!got.empty() && got.back().kind == mp::Message::Kind::kPartyEnd);
    close_socket(extra);
}

// Direct connect's page logic (direct.hpp): the ring rows, holding, the address, the lobby's lines.
void direct_page_logic() {
    // A ring row: 64 choices, the value in the middle, around the ends.
    std::vector<std::string> c = mp::ring_choices(0, mp::kOctetRing);
    CHECK(c.size() == static_cast<std::size_t>(mp::kRingWindow) && c.size() <= 64);
    CHECK(
        c[mp::kRingCentre] == "0" && c[mp::kRingCentre - 1] == "255"
        && c[mp::kRingCentre + 1] == "1"
    );
    CHECK(c.front() == "224" && c.back() == "31");
    c = mp::ring_choices(7770, mp::kPortCoarseRing);
    CHECK(
        c[mp::kRingCentre] == "7770" && c[mp::kRingCentre + 1] == "7870"
        && c[mp::kRingCentre - 1] == "7670"
    );
    c = mp::ring_choices(1, mp::kPortRing);
    CHECK(c[mp::kRingCentre - 1] == "65535" && c[mp::kRingCentre + 1] == "2");

    // Stepping: a choice either side of the middle, around the ends, and ten at a time.
    CHECK(mp::ring_step(168, mp::kRingCentre + 1, mp::kOctetRing) == 169);
    CHECK(mp::ring_step(0, mp::kRingCentre - 1, mp::kOctetRing) == 255);
    CHECK(mp::ring_step(255, mp::kRingCentre + 1, mp::kOctetRing) == 0);
    CHECK(mp::ring_step(250, mp::kRingCentre + 1, mp::kOctetRing, 10) == 4);
    CHECK(mp::ring_step(7770, mp::kRingCentre, mp::kPortRing) == 7770);
    CHECK(mp::ring_step(65535, mp::kRingCentre + 1, mp::kPortRing) == 1);
    CHECK(mp::ring_step(7770, mp::kRingCentre - 1, mp::kPortCoarseRing) == 7670);
    CHECK(mp::ring_step(65500, mp::kRingCentre + 1, mp::kPortCoarseRing) == 65);
    CHECK(mp::ring_wrap(-1, mp::kOctetRing) == 255 && mp::ring_wrap(256, mp::kOctetRing) == 0);

    // Holding: single steps, then ten once the port has repeated a while on one row.
    mp::Acceleration a;
    CHECK(a.times(0, 1000) == 1);
    std::uint64_t t = 1250;  // the port's first repeat comes later than the gap: a new press
    CHECK(a.times(0, t) == 1);
    int fast_at = 0;
    for (int i = 1; i <= 12; ++i) {
        t += 67;
        if (a.times(0, t) == mp::Acceleration::kFast && fast_at == 0) {
            fast_at = i;
        }
    }
    CHECK(fast_at == mp::Acceleration::kHeldRepeats);
    CHECK(a.times(0, t + 67) == mp::Acceleration::kFast);
    CHECK(a.times(1, t + 134) == 1);   // another row
    CHECK(a.times(1, t + 1000) == 1);  // let go
    a.reset();
    CHECK(a.times(1, t + 1050) == 1);

    // The address as connect_direct takes it, and as it parses back.
    mp::DirectAddress address;
    CHECK(address.text() == "192.168.1.1:7770");
    address.octets = {10, 0, 0, 255};
    address.port = 25565;
    CHECK(address.text() == "10.0.0.255:25565");
    std::string host;
    int port = 0;
    CHECK(mp::parse_address(address.text(), host, port) && host == "10.0.0.255" && port == 25565);

    // The hosting lobby's lines: each address with the port, as many as fit; the port alone.
    std::vector<std::string> lines = mp::hosting_lines({"192.168.1.20"}, 7770, 5);
    CHECK(lines == std::vector<std::string>{"Hosting on 192.168.1.20:7770"});
    lines = mp::hosting_lines({"192.168.1.20", "10.0.0.5", "172.16.0.2"}, 7770, 2);
    CHECK(
        lines
        == (std::vector<std::string>{"Hosting on 192.168.1.20:7770", "Hosting on 10.0.0.5:7770"})
    );
    CHECK(mp::hosting_lines({}, 7770, 5) == std::vector<std::string>{"Hosting on port 7770"});
    CHECK(
        mp::hosting_lines({"192.168.1.20"}, 7770, 0)
        == std::vector<std::string>{"Hosting on port 7770"}
    );
    CHECK(mp::hosting_lines({"255.255.255.255"}, 65535, 1)[0].size() <= 47);

    // The player's name: the setting when valid, else the launcher's when valid, else none.
    CHECK(mp::choose_name("Clank_2", "Ratchet") == "Clank_2");
    CHECK(mp::choose_name("", "Ratchet") == "Ratchet");
    CHECK(mp::choose_name("ab", "Ratchet") == "Ratchet");
    CHECK(mp::choose_name("has space", "Ratchet") == "Ratchet");
    CHECK(mp::choose_name("seventeen_letters", "Ratchet") == "Ratchet");
    CHECK(mp::choose_name("", "no!").empty());
    CHECK(mp::choose_name("", "").empty());
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
    host_frames_are_the_servers_bytes();
    direct_connect_plays_a_party();
    the_host_leaving_ends_hosting();
    the_host_keeps_the_servers_limits();
    direct_page_logic();

    // The count of failed checks is the whole verdict.
    if (g_failures == 0) {
        std::puts("multiplayer: all passed");
    }

    return g_failures == 0 ? 0 : 1;
}
