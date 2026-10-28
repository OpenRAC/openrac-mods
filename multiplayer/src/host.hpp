/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Direct connect's embedded host (host.cpp): a small copy of the OpenRAC server's party routing
 * (the Server repository's ServerPacketHandler, PartyController, Party and ServerDatagramHandler)
 * run inside the hosting player's game, for one party and no matchmaking. Clients connect to it
 * exactly as to the server: the same frames on TCP, the same datagrams on UDP (the same port), so
 * the lobby, the puppets and their interpolation work unchanged in either mode.
 *
 * The hosting game itself is the party's host, linked in-process (local_send, local_state and the
 * sink), not through a socket. How it differs from the server:
 *   - One party, code kDirectCode. It exists from the local game's HELLO, which makes it the host.
 *   - A client is put in it right after its HELLO (and UDP) answer: INFO comes unasked, so the
 *     client's lobby opens with no code typed. CREATE, JOIN (any code) and FIND join the same party
 *     (after a LEAVE, say); in it already: ALREADY_IN_PARTY; full: PARTY_FULL (the client stays
 *     connected, out of the party, as on the server). A client greeted before the local HELLO
 *     joins once the party exists.
 *   - The host leaving (LEAVE from the local game) ends the party: END to every member, and ended()
 *     tells the owner to stop() the host. stop() sends END too, flushes, and closes everything.
 *   - The local link has no rate limits, no hello deadline, and its bad frames are dropped rather
 *     than closing anything.
 * The same limits as the server otherwise (its .env.defaults): frames of 65,536 bytes at most,
 * names and versions 1 to 32 characters, chat 1 to 256, a party of 4, per-connection token buckets
 * with strikes, hello within 10 s, 8 connections an address; plus at most max_connections clients.
 *
 * Its own thread does the sockets (poll over the listener, the UDP socket and the clients). Every
 * change of the party runs under one mutex, from that thread or the game's (local_*), and frames
 * for the local game go to the sink under it: the sink must not call back into the host.
 */
#ifndef MULTIPLAYER_HOST_H
#define MULTIPLAYER_HOST_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "protocol.hpp"

namespace mp {

/* A token bucket's size: a burst, then the steady rate per second. */
struct Rate {
    int burst = 0;
    double per_second = 0.0;
};

/* The host's settings; the defaults are the server's (.env.defaults). */
struct HostConfig {
    /* The TCP and UDP port; 0 picks a free one (port() says which). */
    int port = kDefaultPort;
    std::size_t max_party_size = kMaxPartySize;
    /* Client connections at once, in the party or not. */
    std::size_t max_connections = 8;
    std::size_t max_connections_per_address = 8;
    int hello_timeout_ms = 10000;
    int udp_timeout_ms = kUdpTimeoutMs;
    /* Whether to offer UDP at all. */
    bool udp = true;
    /* The party's description (the host's save), as CREATE gives the server one; kept as the server
     * keeps it (description_text). */
    std::string description;
    Rate chat{5, 1.0};
    Rate party{5, 1.0};
    Rate data{120, 60.0};
    Rate time{5, 2.0};
    Rate strikes{20, 1.0};
};

class Host {
public:
    /* Takes one whole frame for the local game (decode it as the server's). */
    using Sink = std::function<void(const Bytes& frame)>;

    explicit Host(HostConfig config = {});
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    /* Listens (TCP, and UDP on the same port when it can) and starts the thread. False, with
     * error() saying why ("Port 7770 is in use"), when it cannot listen. */
    bool start(Sink sink);
    /* Ends the party (END to the clients), flushes, closes every socket and stops the thread. */
    void stop();

    /* A frame from the local game, as it would write it to the server (protocol.hpp's encoders). */
    void local_send(const Bytes& frame);
    /* Game state from the local game, routed as a STATE datagram would be. */
    void local_state(std::int32_t target, const Bytes& bytes);

    bool running() const { return running_.load(); }

    /* Whether the local game left, so the party is over: stop() the host. */
    bool ended() const { return ended_.load(); }

    /* The port listened on, whether UDP is offered, and why start() failed. */
    int port() const { return port_; }

    bool udp() const { return udp_ok_; }

    const std::string& error() const { return error_; }

    /* The party's members, the host first; empty before the local HELLO. */
    std::vector<Member> members() const;

private:
    struct Peer;

    void run();
    void accept_clients();
    void read_datagrams();
    void read_peer(Peer& peer);
    void flush(Peer& peer);
    void handle(Peer& peer, const Bytes& body);
    void hello(Peer& peer, const Request& request);
    void join(Peer& peer);
    void leave(Peer& peer, bool asked);
    void end_party();
    void forward(Peer& sender, std::int32_t target, const std::function<void(Peer&)>& delivery);
    void deliver_state(std::int32_t sender, Peer& to, const Bytes& bytes);
    void send_to(Peer& peer, const Bytes& frame);
    void kill(Peer& peer);
    void close_all();
    std::int64_t clock_ms() const;

    HostConfig config_;
    Sink sink_;
    std::string error_;
    int port_ = 0;
    bool udp_ok_ = false;
    std::intptr_t listener_ = -1;
    std::intptr_t udp_ = -1;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> ended_{false};
    std::thread thread_;
    /* Guards everything below, and the peers' state. */
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Peer>> peers_;
    Peer* local_ = nullptr;
    /* The party: the host (the local game once greeted) first, then the members as they joined. */
    std::vector<Peer*> party_;
    std::int32_t next_id_ = 1;
    std::int64_t started_ns_ = 0;
};

/* ServerPacketHandler.sanitizeDescription: printable ASCII only, trimmed, at most
 * kMaxDescriptionLength characters. */
std::string description_text(const std::string& text);

/* This machine's addresses others may reach it on (IPv4, the loopback left out), for the page to
 * show beside the port. Empty when it cannot tell. */
std::vector<std::string> local_addresses();

}  // namespace mp

#endif /* MULTIPLAYER_HOST_H */
