// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Direct connect's embedded host (host.cpp): a small copy of the OpenRAC server's party routing
// (the Server repository's ServerPacketHandler, PartyController, Party and ServerDatagramHandler)
// run inside the hosting player's game, for one party and no matchmaking. Clients connect to it
// exactly as to the server: the same frames on TCP, the same datagrams on UDP (the same port), so
// the lobby, the puppets and their interpolation work unchanged in either mode.
//
// The hosting game itself is the party's host, linked in-process (local_send, local_state and the
// sink), not through a socket. How it differs from the server:
//   - One party, code kDirectCode. It exists from the local game's HELLO, which makes it the host.
//   - A client is put in it right after its HELLO (and UDP) answer: INFO comes unasked, so the
//     client's lobby opens with no code typed. CREATE, JOIN (any code) and FIND join the same party
//     (after a LEAVE, say); in it already: ALREADY_IN_PARTY; full: PARTY_FULL (the client stays
//     connected, out of the party, as on the server). A client greeted before the local HELLO
//     joins once the party exists.
//   - KICK from the local game takes a member out as on the server: KICKED to it, LEFT to the
// others, its connection staying; a client's KICK is refused (NOT_HOST).
//   - The host leaving (LEAVE from the local game) ends the party: END to every member, and ended()
//     tells the owner to stop() the host. stop() sends END too, flushes, and closes everything.
//   - The local link has no rate limits, no hello deadline, and its bad frames are dropped rather
//     than closing anything.
// The same limits as the server otherwise (its .env.defaults): frames of 65,536 bytes at most,
// names and versions 1 to 32 characters, chat 1 to 256, a party of 4, per-connection token buckets
// with strikes, hello within 10 s, 8 connections an address; plus at most max_connections clients.
//
// Its own thread does the sockets (poll over the listener, the UDP socket and the clients). Every
// change of the party runs under one mutex, from that thread or the game's (local_*), and frames
// for the local game go to the sink under it: the sink must not call back into the host.

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

// A token bucket's size: a burst, then the steady rate per second.
struct Rate {
    int burst = 0;
    double per_second = 0.0;
};

// The host's settings; the defaults are the server's (.env.defaults).
struct HostConfig {
    // The TCP and UDP port; 0 picks a free one (port() says which).
    int port = kDefaultPort;
    // The most members in the party, the host included.
    std::size_t max_party_size = kMaxPartySize;
    // Client connections at once, in the party or not.
    std::size_t max_connections = 8;
    // The most client connections at once from one address.
    std::size_t max_connections_per_address = 8;
    // How long a connection may go without a HELLO before it is closed.
    int hello_timeout_ms = 10000;
    // How long a client's UDP binding is trusted after its last datagram.
    int udp_timeout_ms = kUdpTimeoutMs;
    // Whether to offer UDP at all.
    bool udp = true;
    // The party's description (the host's save), as CREATE gives the server one; kept as the server
    // keeps it (description_text).
    std::string description;
    // A bucket per client for each kind of packet. A packet over its bucket is dropped and takes a
    // token from `strikes`; with none left the connection is closed.
    Rate chat{5, 1.0};
    Rate party{5, 1.0};
    Rate data{120, 60.0};
    Rate time{5, 2.0};
    Rate strikes{20, 1.0};
};

// The embedded host: one party, linked in-process to the local game and over sockets to its
// clients. Its own thread does the sockets. The game's thread calls start, stop, local_send,
// local_state and the accessors; every change of the party runs under `mutex_`. Not copyable.
class Host {
public:
    // Takes one whole frame for the local game (decode it as the server's).
    using Sink = std::function<void(const Bytes& frame)>;

    // Makes a host with these settings. It listens only once start() has run.
    explicit Host(HostConfig config = {});
    // Stops the host if it still runs.
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Listens (TCP, and UDP on the same port when it can) and starts the thread. False, with
    // error() saying why ("Port 7770 is in use"), when it cannot listen.
    bool start(Sink sink);
    // Ends the party (END to the clients), flushes, closes every socket and stops the thread.
    void stop();

    // A frame from the local game, as it would write it to the server (protocol.hpp's encoders).
    void local_send(const Bytes& frame);
    // Game state from the local game, routed as a STATE datagram would be.
    void local_state(std::int32_t target, const Bytes& bytes);

    // Returns whether the host has started and not stopped.
    bool running() const { return running_.load(); }

    // Whether the local game left, so the party is over: stop() the host.
    bool ended() const { return ended_.load(); }

    // The port listened on, whether UDP is offered, and why start() failed.
    int port() const { return port_; }

    bool udp() const { return udp_ok_; }

    const std::string& error() const { return error_; }

    // The party's members, the host first; empty before the local HELLO.
    std::vector<Member> members() const;

private:
    struct Peer;

    // The thread's body: polls the listener, the UDP socket and the clients, and handles what is
    // ready, until stop_ is set.
    void run();
    // Accepts every connection waiting, within the connection limits.
    void accept_clients();
    // Reads every datagram waiting and routes the STATE ones.
    void read_datagrams();
    // Reads what a client sent and handles each whole frame.
    void read_peer(Peer& peer);
    // Writes what is queued for a client, as far as its socket takes it now.
    void flush(Peer& peer);
    // Acts on one frame from `peer`, after the checks and rate limits the server makes.
    void handle(Peer& peer, const Bytes& body);
    // Greets `peer`: its session id, a client's UDP offer, and its place in the party.
    void hello(Peer& peer, const Request& request);
    // Puts `peer` in the party, or tells it why it cannot be.
    void join(Peer& peer);
    // Takes `peer` out of the party; `asked` is false for a dropped connection. The host leaving
    // ends the party.
    void leave(Peer& peer, bool asked);
    // Takes `member` out of the party at the request of `peer`, which must be the host.
    void kick(Peer& peer, std::int32_t member);
    // Gives `peer` back the slot the request's session and token name, or refuses it.
    void resume(Peer& peer, const Request& request);
    // Drops the slots whose time ran out, and joins the clients whose RESUME did not come. `now_ns`
    // is the steady clock in nanoseconds.
    void tick_slots(std::int64_t now_ns);
    // INFO as the party stands, the slots that wait among its members.
    Bytes info() const;
    // Tells every member the party is over and clears it; ended() is true after.
    void end_party();
    // Calls `delivery` for each peer a message from `sender` goes to: the host for a member, and
    // for the host the `target` member or, for kBroadcast, everyone else.
    void forward(Peer& sender, std::int32_t target, const std::function<void(Peer&)>& delivery);
    // Hands STATE from `sender` to `to`: as a datagram when it can, else as DATA.
    void deliver_state(std::int32_t sender, Peer& to, const Bytes& bytes);
    // Queues a frame for a client, or gives it to the sink for the local game.
    void send_to(Peer& peer, const Bytes& frame);
    // Closes a client's connection, which makes it leave its party. The local game is never closed.
    void kill(Peer& peer);
    // Flushes the last frames to the clients, closes every socket and forgets the peers.
    void close_all();
    // Returns the milliseconds since start(), the server's clock for TIME.
    std::int64_t clock_ms() const;

    // The settings given at construction.
    HostConfig config_;
    // Where the local game's frames go; called under `mutex_`.
    Sink sink_;
    // Why start() failed; empty otherwise.
    std::string error_;
    int port_ = 0;                      // The port listened on.
    bool udp_ok_ = false;               // Whether UDP is offered.
    std::intptr_t listener_ = -1;       // The TCP listening socket; -1 for none.
    std::intptr_t udp_ = -1;            // The UDP socket; -1 for none.
    std::atomic<bool> stop_{false};     // Set to ask the thread to end.
    std::atomic<bool> running_{false};  // True from start() to stop().
    std::atomic<bool> ended_{false};    // True once the party ended, by the host leaving or stop().
    // The host's thread.
    std::thread thread_;
    // Guards everything below, and the peers' state.
    mutable std::mutex mutex_;
    // Every connection, the local game among them.
    std::vector<std::unique_ptr<Peer>> peers_;
    // The local game's peer, which lives in peers_; null when stopped.
    Peer* local_ = nullptr;
    // The party: the host (the local game once greeted) first, then the members as they joined.
    std::vector<Peer*> party_;

    // A member whose connection dropped: its slot, for it to come back to (migrate.hpp).
    struct Slot {
        Member member;                // The member who dropped.
        std::int64_t token = 0;       // The token that takes the slot back.
        std::int64_t dropped_ns = 0;  // When the connection dropped (steady ns).
    };

    // The slots that wait, oldest first.
    std::vector<Slot> slots_;
    // The next session id to give; ids start at 1.
    std::int32_t next_id_ = 1;
    // The steady clock when start() ran, in nanoseconds.
    std::int64_t started_ns_ = 0;
};

// ServerPacketHandler.sanitizeDescription: printable ASCII only, trimmed, at most
// kMaxDescriptionLength characters.
std::string description_text(const std::string& text);

// This machine's addresses others may reach it on (IPv4, the loopback left out), for the page to
// show beside the port. Empty when it cannot tell.
std::vector<std::string> local_addresses();

}  // namespace mp

#endif  // MULTIPLAYER_HOST_H
