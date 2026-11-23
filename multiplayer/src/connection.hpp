// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The one TCP connection to the OpenRAC server (connection.cpp), on a thread of its own.
//
// The game's thread never waits for the network: it queues frames (send) and takes the server's
// messages (receive), both under a mutex. The connection's thread connects (TCP_NODELAY on),
// writes what is queued, reads and cuts frames (protocol.hpp), and when the server is not there or
// goes, tries again every few seconds. Each connection made is a new generation(): the session
// says hello again, since the server knows nothing of the last one. Frames queued while there is
// no connection are dropped, as a party's state on the server went with the connection.
//
// It links the session one of three ways (mode()): to the configured OpenRAC server (open, the
// default), to another player's game by address (connect_direct, Direct connect), or to this
// game's own embedded host (start_hosting, host.hpp), linked in-process: then it is connected at
// once, the host's frames arrive through receive() as the server's would, and the session cannot
// tell the difference. Each switch is a new generation, so the session says hello again.
//
// UDP: when the server (or a host) offers it (the UDP packet after HELLO), the thread binds a UDP
// socket to the same address, sends BIND every kBindMs, and hands STATE datagrams in as DATA
// messages. send_state sends game state as a STATE datagram, and also as TCP DATA until datagrams
// come back (UDP is confirmed, udp()), so blocked UDP never loses state; once confirmed, UDP only.
// Without a UDP offer (an older server), state goes as DATA.
//
// The hub (the server's README, The hub): the address a server link names may be the hub, which
// lists game servers and runs no parties (a PARTY sent to it closes the connection). So a server
// link first says HELLO there on a connection of its own and reads the UDP offer: without
// kFeatureServers it is a game server, and the link connects to it again as before (once per
// open: a reconnect goes straight there). With it, the link asks REGIONS, picks a game server
// (pick_server: the region set_region names, else the first listed; an empty host is the hub's)
// and connects there instead; each reconnect asks the hub again. Only that game server's
// connection is a generation the session sees. A hub that lists none, or does not answer, is
// kUnreachable until the next try. No login: the player plays under its HELLO name.
//
// Testing a bad link: OPENRAC_MP_LAG="<ms>[,<jitter ms>]" holds each message the server sent for
// that long more before receive() hands it on, plus a random extra of about the jitter (mostly
// less, now and then several times it), in the order they came (a TCP stream's stall and burst).

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

// Where frames go: the connection, or a test's recorder (session.hpp).
class Transport {
public:
    virtual ~Transport() = default;
    // Queues one whole frame (protocol.hpp's encoders give them).
    virtual void send(Bytes frame) = 0;

    // Whether game state of `bytes` bytes needs the reliable stream as well: not when the transport
    // has something better that is known to work (UDP datagrams coming back, the embedded host).
    virtual bool state_needs_tcp(std::size_t /*bytes*/) const { return true; }

    // Game state for `target` (session.hpp's send_state): as DATA when `tcp` says, and by whatever
    // better the transport has (UDP).
    virtual void send_state(std::int32_t target, const Bytes& bytes, bool tcp) {
        // A transport with nothing better sends the state as DATA when the session says so.
        if (tcp) {
            send(data(target, bytes));
        }
    }
};

// What the embedded host is doing, for the page (Connection::hosting).
struct HostingStatus {
    // Listening, on which port, and whether UDP is offered too.
    bool listening = false;
    int port = 0;
    bool udp = false;
    // This machine's addresses to give the others (with the port), and the party's members.
    std::vector<std::string> addresses;
    std::vector<Member> members;
    // Why the last start_hosting failed ("Port 7770 is in use"), else empty.
    std::string error;
};

// The link to the server, to another player's game or to this game's own embedded host, as the
// session's Transport. The game's thread calls everything but run() and serve(), which belong to
// the connection's thread. The two share what is marked as guarded by `mutex_` or atomic below.
class Connection : public Transport {
public:
    // What the connection is doing, for the page's status line.
    enum class State {
        kIdle,         // Nothing runs.
        kConnecting,   // The thread is trying to connect.
        kConnected,    // A connection is up.
        kUnreachable,  // No connection could be made.
        kLost          // A connection ended.
    };

    // How the session is linked.
    enum class Mode {
        kNone,    // Not linked.
        kServer,  // An OpenRAC server.
        kDirect,  // Another player's game.
        kHosting  // This game's own embedded host.
    };

    Connection() = default;
    // Closes the link: stops the thread and the embedded host, if any.
    ~Connection() override;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Starts the thread for the server at host:port; it connects when it can. Nothing when the
    // connection is linked already, any way (so the page may call it each time it opens).
    void open(const std::string& host, int port);
    // Stops whatever runs: the thread and its socket, or the embedded host (END to its clients).
    void close();

    // Direct connect, for the page. Each closes whatever ran before.
    // connect_server: back to an OpenRAC server (the default mode), as open does.
    // connect_direct: to a player hosting at "host:port" (port 7770 when none); false, and nothing
    //   changed, for an address with no host.
    // start_hosting: this game hosts on `port` (TCP and UDP) and is connected to itself at once;
    //   false when it cannot listen, with hosting().error saying why. The others join it with
    //   connect_direct; the session gets the party's INFO (code kDirectCode) after its hello.
    // stop_hosting: ends the party (the clients get END) and stops listening. The host's LEAVE
    //   (session.leave()) does the same.
    void connect_server(const std::string& host, int port);
    bool connect_direct(const std::string& address);
    bool start_hosting(int port = kDefaultPort);
    void stop_hosting();

    // Returns how the session is linked now.
    Mode mode() const { return mode_; }

    // What a server link says to a hub: the HELLO's version and name (the session's), and the game
    // family REGIONS names. Set before open; a later change counts from the next connection.
    void set_hello(const std::string& version, const std::string& name);
    void set_family(const std::string& family);
    // The region of the hub's list to play in, any case; empty: the first listed.
    void set_region(const std::string& region);

    // The game server the hub gave, "host:port"; empty when the address is a game server itself
    // (or no hub answered yet).
    std::string game_server() const;
    // That game server as the hub listed it: its region's code and its name (both empty with no
    // hub's pick), for regions.hpp's server_text.
    ListedServer game_server_listing() const;

    // The address linked to: the server's or the direct host's, "host:port"; empty when hosting.
    const std::string& address() const { return address_; }

    // Returns what the embedded host is doing; only the last start failure when not hosting.
    HostingStatus hosting() const;

    // Whether game state travels over UDP now: datagrams came back lately.
    bool udp() const;

    // The embedded host's settings (tests shrink its limits); used by the next start_hosting.
    void set_host_config(const HostConfig& config) { host_config_ = config; }

    // Queues the frame for the connection's thread, or hands it to the embedded host when hosting.
    // A frame with no connection to go to is dropped.
    void send(Bytes frame) override;
    // Returns false when UDP is confirmed and a datagram holds `bytes`, and when hosting.
    bool state_needs_tcp(std::size_t bytes) const override;
    // Sends the state as a STATE datagram when UDP is up and it fits, and as DATA when `tcp` says.
    void send_state(std::int32_t target, const Bytes& bytes, bool tcp) override;
    // The messages the server sent since the last call, in order (OPENRAC_MP_LAG: those whose
    // extra delay is over).
    std::vector<Message> receive();

    // Returns what the connection is doing now.
    State state() const { return state_.load(); }

    // Counts the connections made: a new value, a new session on the server.
    std::uint32_t generation() const { return generation_.load(); }

private:
    // The thread's body: connects, serves the connection while it lasts, waits, and tries again
    // until stop_ is set.
    void run();
    // One connection's life: writes and reads until it goes or the thread stops.
    void serve(std::intptr_t fd);
    // Starts the thread for host:port in `mode`.
    void start_thread(const std::string& host, int port, Mode mode);

    // What a connection to the configured address turned out to be (probe).
    enum class Probe {
        kPlain,     // A game server: connect to it again and play there.
        kHub,       // A hub that gave a game server.
        kNoServer,  // A hub that lists no game server to play on.
        kFailed     // No answer, or the connection went.
    };
    // On a fresh connection to the configured address: HELLO, then the UDP offer; a hub is asked
    // REGIONS and its pick goes into `host`, `port` and `picked`. The connection's thread only.
    Probe probe(std::intptr_t fd, std::string& host, int& port, ListedServer& picked);

    // The server or direct host the thread connects to.
    std::string host_name_;
    int port_ = 0;
    // Set by start_thread for a server link, which may name a hub; and, on the thread, once the
    // address proved to be a game server, so a reconnect skips the hub's questions.
    bool server_link_ = false;
    bool plain_ = false;
    // What a hub is told (set_hello, set_family, set_region), under mutex_.
    std::string hello_version_ = "openrac-multiplayer";
    std::string hello_name_ = "Player";
    std::string family_;
    std::string region_;
    // The game server the hub gave (game_server, game_server_listing), under via_mutex_.
    mutable std::mutex via_mutex_;
    std::string via_;
    ListedServer via_listing_;
    // Set to ask the thread to end.
    std::atomic<bool> stop_{false};
    // What the connection is doing; both threads read it.
    std::atomic<State> state_{State::kIdle};
    // The connections made, as generation() counts them.
    std::atomic<std::uint32_t> generation_{0};
    // The connection's thread; joinable while one runs.
    std::thread thread_;
    // Guards outbox_ and inbox_, which both threads touch.
    std::mutex mutex_;
    // Wakes the thread when a frame is queued or stop_ is set.
    std::condition_variable wake_;
    // Frames waiting to be written to the server.
    std::deque<Bytes> outbox_;
    // Messages the server sent that receive() has not taken yet.
    std::vector<Message> inbox_;
    // Datagrams waiting for the thread's UDP socket (under mutex_).
    std::deque<Bytes> udp_outbox_;

    // Set on the game's thread only.
    Mode mode_ = Mode::kNone;
    // OPENRAC_MP_LAG: the extra delay and its jitter (ms; read once), the messages held and when
    // each is handed on (steady ms), the last one's, and the jitter's random state.
    int lag_read_ = 0;
    double lag_ms_ = 0;
    double lag_jitter_ms_ = 0;
    std::deque<std::pair<double, Message>> lagged_;
    double lag_last_ = 0;
    std::uint64_t lag_random_ = 0x9E3779B97F4A7C15ull;
    // The address linked to, as address() gives it.
    std::string address_;
    // The settings the next start_hosting gives the embedded host.
    HostConfig host_config_;
    // The embedded host while hosting, else null; used on the game's thread only.
    std::unique_ptr<Host> embedded_;
    // Why the last start_hosting failed; empty when it did not.
    std::string host_error_;
    // This machine's addresses, found when hosting started.
    std::vector<std::string> host_addresses_;

    // UDP, as the thread learns it: the session id and token (UDP is usable once both are known
    // and the socket is up), and when the last datagram came (now_ms; 0: none yet).
    std::atomic<std::int32_t> udp_session_{0};
    std::atomic<std::int64_t> udp_token_{0};
    std::atomic<bool> udp_ready_{false};
    std::atomic<std::uint64_t> udp_heard_ms_{0};
};

// How often a client sends BIND to keep its NAT mapping open (the server expects 2 to 5 s).
constexpr int kBindMs = 3000;

// Splits "host:port" (or "[v6]:port", or a bare host) into its parts; a missing or bad port is
// kDefaultPort. False for an empty host.
bool parse_address(const std::string& text, std::string& host, int& port);

}  // namespace mp

#endif  // MULTIPLAYER_CONNECTION_H
