/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The one TCP connection to the OpenRAC server (connection.cpp), on a thread of its own.
 *
 * The game's thread never waits for the network: it queues frames (send) and takes the server's
 * messages (receive), both under a mutex. The connection's thread connects (TCP_NODELAY on),
 * writes what is queued, reads and cuts frames (protocol.hpp), and when the server is not there or
 * goes, tries again every few seconds. Each connection made is a new generation(): the session
 * says hello again, since the server knows nothing of the last one. Frames queued while there is
 * no connection are dropped, as a party's state on the server went with the connection.
 *
 * It links the session one of three ways (mode()): to the configured OpenRAC server (open, the
 * default), to another player's game by address (connect_direct, Direct connect), or to this
 * game's own embedded host (start_hosting, host.hpp), linked in-process: then it is connected at
 * once, the host's frames arrive through receive() as the server's would, and the session cannot
 * tell the difference. Each switch is a new generation, so the session says hello again.
 *
 * UDP: when the server (or a host) offers it (the UDP packet after HELLO), the thread binds a UDP
 * socket to the same address, sends BIND every kBindMs, and hands STATE datagrams in as DATA
 * messages. send_state sends game state as a STATE datagram, and also as TCP DATA until datagrams
 * come back (UDP is confirmed, udp()), so blocked UDP never loses state; once confirmed, UDP only.
 * Without a UDP offer (an older server), state goes as DATA.
 *
 * Testing a bad link: OPENRAC_MP_LAG="<ms>[,<jitter ms>]" holds each message the server sent for
 * that long more before receive() hands it on, plus a random extra of about the jitter (mostly
 * less, now and then several times it), in the order they came (a TCP stream's stall and burst).
 */
#ifndef MULTIPLAYER_CONNECTION_H
#define MULTIPLAYER_CONNECTION_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "host.hpp"
#include "protocol.hpp"

namespace mp {

/* Where frames go: the connection, or a test's recorder (session.hpp). */
class Transport {
public:
    virtual ~Transport() = default;
    /* Queues one whole frame (protocol.hpp's encoders give them). */
    virtual void send(Bytes frame) = 0;

    /* Game state for `target` (session.hpp's send_state): as DATA, unless the transport has
     * something better (UDP). */
    virtual void send_state(std::int32_t target, const Bytes& bytes) { send(data(target, bytes)); }
};

/* What the embedded host is doing, for the page (Connection::hosting). */
struct HostingStatus {
    /* Listening, on which port, and whether UDP is offered too. */
    bool listening = false;
    int port = 0;
    bool udp = false;
    /* This machine's addresses to give the others (with the port), and the party's members. */
    std::vector<std::string> addresses;
    std::vector<Member> members;
    /* Why the last start_hosting failed ("Port 7770 is in use"), else empty. */
    std::string error;
};

class Connection : public Transport {
public:
    /* What the connection is doing, for the page's status line. */
    enum class State {
        kIdle,
        kConnecting,
        kConnected,
        kUnreachable,
        kLost
    };

    /* How the session is linked. */
    enum class Mode {
        kNone,
        kServer,
        kDirect,
        kHosting
    };

    Connection() = default;
    ~Connection() override;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    /* Starts the thread for the server at host:port; it connects when it can. Nothing when the
     * connection is linked already, any way (so the page may call it each time it opens). */
    void open(const std::string& host, int port);
    /* Stops whatever runs: the thread and its socket, or the embedded host (END to its clients). */
    void close();

    /* Direct connect, for the page. Each closes whatever ran before.
     * connect_server: back to an OpenRAC server (the default mode), as open does.
     * connect_direct: to a player hosting at "host:port" (port 7770 when none); false, and nothing
     *   changed, for an address with no host.
     * start_hosting: this game hosts on `port` (TCP and UDP) and is connected to itself at once;
     *   false when it cannot listen, with hosting().error saying why. The others join it with
     *   connect_direct; the session gets the party's INFO (code kDirectCode) after its hello.
     * stop_hosting: ends the party (the clients get END) and stops listening. The host's LEAVE
     *   (session.leave()) does the same. */
    void connect_server(const std::string& host, int port);
    bool connect_direct(const std::string& address);
    bool start_hosting(int port = kDefaultPort);
    void stop_hosting();

    Mode mode() const { return mode_; }

    /* The address linked to: the server's or the direct host's, "host:port"; empty when hosting. */
    const std::string& address() const { return address_; }

    HostingStatus hosting() const;

    /* Whether game state travels over UDP now: datagrams came back lately. */
    bool udp() const;

    /* The embedded host's settings (tests shrink its limits); used by the next start_hosting. */
    void set_host_config(const HostConfig& config) { host_config_ = config; }

    void send(Bytes frame) override;
    void send_state(std::int32_t target, const Bytes& bytes) override;
    /* The messages the server sent since the last call, in order (OPENRAC_MP_LAG: those whose
     * extra delay is over). */
    std::vector<Message> receive();

    State state() const { return state_.load(); }

    /* Counts the connections made: a new value, a new session on the server. */
    std::uint32_t generation() const { return generation_.load(); }

private:
    void run();
    /* One connection's life: writes and reads until it goes or the thread stops. */
    void serve(std::intptr_t fd);
    /* Starts the thread for host:port in `mode`. */
    void start_thread(const std::string& host, int port, Mode mode);

    std::string host_name_;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<State> state_{State::kIdle};
    std::atomic<std::uint32_t> generation_{0};
    std::thread thread_;
    /* Guards outbox_ and inbox_, which both threads touch. */
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Bytes> outbox_;
    std::vector<Message> inbox_;
    /* Datagrams waiting for the thread's UDP socket (under mutex_). */
    std::deque<Bytes> udp_outbox_;

    /* Set on the game's thread only. */
    Mode mode_ = Mode::kNone;
    /* OPENRAC_MP_LAG: the extra delay and its jitter (ms; read once), the messages held and when
     * each is handed on (steady ms), the last one's, and the jitter's random state. */
    int lag_read_ = 0;
    double lag_ms_ = 0;
    double lag_jitter_ms_ = 0;
    std::deque<std::pair<double, Message>> lagged_;
    double lag_last_ = 0;
    std::uint64_t lag_random_ = 0x9E3779B97F4A7C15ull;
    std::string address_;
    HostConfig host_config_;
    std::unique_ptr<Host> embedded_;
    std::string host_error_;
    std::vector<std::string> host_addresses_;

    /* UDP, as the thread learns it: the session id and token (UDP is usable once both are known
     * and the socket is up), and when the last datagram came (now_ms; 0: none yet). */
    std::atomic<std::int32_t> udp_session_{0};
    std::atomic<std::int64_t> udp_token_{0};
    std::atomic<bool> udp_ready_{false};
    std::atomic<std::uint64_t> udp_heard_ms_{0};
};

/* How often a client sends BIND to keep its NAT mapping open (the server expects 2 to 5 s). */
constexpr int kBindMs = 3000;

/* Splits "host:port" (or "[v6]:port", or a bare host) into its parts; a missing or bad port is
 * kDefaultPort. False for an empty host. */
bool parse_address(const std::string& text, std::string& host, int& port);

}  // namespace mp

#endif /* MULTIPLAYER_CONNECTION_H */
