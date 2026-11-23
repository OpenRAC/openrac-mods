// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The connection to the OpenRAC server (connection.hpp): a socket on a thread of its own, as
// live-splits' client keeps its LiveSplit socket. The connect is non-blocking with a time limit, so
// that close() (at the game's end) never waits on an address that does not answer.

#include "connection.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "session.hpp"
#include "sockets.hpp"

namespace mp {

using namespace net;

namespace {

// How long a connect may take, how long the thread waits before it tries again, and how long it
// waits for the server or a queued frame before it looks again, in milliseconds (assumed).
constexpr int kConnectMs = 3000;
constexpr int kRetryMs = 3000;
constexpr int kSliceMs = 20;
// On a server that keeps a dropped player's slot: how often this thread says something whatever the
// game is doing (a level loading stops its ticks), so silence means a dead link, and how long
// nothing from the server means that on this side.
constexpr std::int64_t kAliveMs = 2000;
constexpr std::int64_t kServerSilentMs = 6000;
// How long one select waits in connect_to, so a stop is noticed.
constexpr int kWaitSliceMs = 50;

// How long a hub, or a game server, may take to answer HELLO and offer UDP, then REGIONS; and how
// long past the HELLO answer an offer may still come (a server from before offers never sends one).
constexpr std::int64_t kProbeMs = 5000;
constexpr std::int64_t kOfferMs = 1500;

// The bytes one read takes at most.
constexpr std::size_t kReadChunkBytes = 4096;

// Waits at most `ms` for the socket to be readable (or writable); true when it is.
bool wait(socket_t fd, bool for_write, int ms, const std::atomic<bool>& stop) {
    // In short slices, so a stop (the game switching to Direct connect) is not kept waiting for
    // an unreachable server's whole connect time.
    for (int waited = 0; waited < ms && !stop.load(); waited += kWaitSliceMs) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = kWaitSliceMs * 1000;
        const int n = select(
            static_cast<int>(fd) + 1,
            for_write ? nullptr : &set,
            for_write ? &set : nullptr,
            nullptr,
            &tv
        );

        // A timeout (0) is waited out again; anything else is the answer, ready or an error.
        if (n != 0) {
            return n > 0;
        }
    }

    return false;
}

// Connects to host:port, each address the name gives in turn, within kConnectMs each. The socket,
// blocking again and with TCP_NODELAY, or kNoSocket.
socket_t connect_to(const std::string& host, int port, const std::atomic<bool>& stop) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;

    // A name that does not resolve has no address to try.
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &addresses) != 0) {
        return kNoSocket;
    }

    socket_t fd = kNoSocket;

    // Each address in turn, until one connects.
    for (addrinfo* a = addresses; a != nullptr && !stop.load(); a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);

        // No socket for this address family: the next address.
        if (fd == kNoSocket) {
            continue;
        }

        set_blocking(fd, false);
        connect(fd, a->ai_addr, static_cast<int>(a->ai_addrlen));
        int error = -1;
        socklen_t length = sizeof error;

        // Connected when the socket turns writable with no error pending.
        if (wait(fd, true, kConnectMs, stop)
            && getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == 0
            && error == 0) {
            set_blocking(fd, true);
            set_nodelay(fd);
            no_sigpipe(fd);
            break;
        }

        close_socket(fd);
        fd = kNoSocket;
    }

    freeaddrinfo(addresses);
    return fd;
}

// Writes all of `bytes`; false when the socket failed.
bool write_all(socket_t fd, const Bytes& bytes) {
    std::size_t sent = 0;

    // send may take part of it at a time.
    while (sent < bytes.size()) {
        const auto n = ::send(
            fd,
            reinterpret_cast<const char*>(bytes.data() + sent),
            static_cast<int>(bytes.size() - sent),
            kSendFlags
        );

        // The server is gone.
        if (n <= 0) {
            return false;
        }

        sent += static_cast<std::size_t>(n);
    }

    return true;
}

// A UDP socket connected to the TCP peer's address at `port`, non-blocking; kNoSocket when there
// is none to be had (then state stays on TCP).
socket_t open_udp(socket_t tcp, std::int32_t port) {
    sockaddr_storage peer{};
    socklen_t length = sizeof peer;

    // A port no server gives, or no peer to read.
    if (port < 1 || port > 65535
        || getpeername(tcp, reinterpret_cast<sockaddr*>(&peer), &length) != 0) {
        return kNoSocket;
    }

#ifdef MULTIPLAYER_BOT
    // The end-to-end tests' proxies on an address they share (macOS): the proxy's own port, not the
    // server's it passed on.
    const bool keep_port = std::getenv("OPENRAC_MP_BOT_UDP_AT_TCP") != nullptr;
#else
    const bool keep_port = false;
#endif

    // The tests' proxies keep the TCP port; the server's UDP port replaces it otherwise.
    if (keep_port) {
        // The peer's port stays.
    } else if (peer.ss_family == AF_INET6) {
        reinterpret_cast<sockaddr_in6*>(&peer)->sin6_port = htons(static_cast<std::uint16_t>(port));
    } else {
        reinterpret_cast<sockaddr_in*>(&peer)->sin_port = htons(static_cast<std::uint16_t>(port));
    }

    const socket_t fd = socket(peer.ss_family, SOCK_DGRAM, 0);

    // No UDP socket for this address family: state stays on TCP.
    if (fd == kNoSocket) {
        return kNoSocket;
    }

    // Connected: only the server's datagrams come in, and send needs no address.
    if (connect(fd, reinterpret_cast<sockaddr*>(&peer), length) != 0) {
        close_socket(fd);
        return kNoSocket;
    }

    set_blocking(fd, false);
    return fd;
}

}  // namespace

Connection::~Connection() {
    close();
}

void Connection::open(const std::string& host, int port) {
    // Linked already, one way or another.
    if (mode_ != Mode::kNone || thread_.joinable()) {
        return;
    }

    start_thread(host, port, Mode::kServer);
}

void Connection::set_hello(const std::string& version, const std::string& name) {
    const std::lock_guard lock(mutex_);
    hello_version_ = version.empty() ? hello_version_ : version;
    hello_name_ = name.empty() ? hello_name_ : name;
}

void Connection::set_family(const std::string& family) {
    const std::lock_guard lock(mutex_);
    family_ = family;
}

void Connection::set_region(const std::string& region) {
    const std::lock_guard lock(mutex_);
    region_ = region;
}

std::string Connection::game_server() const {
    const std::lock_guard lock(via_mutex_);
    return via_;
}

ListedServer Connection::game_server_listing() const {
    const std::lock_guard lock(via_mutex_);
    return via_listing_;
}

void Connection::start_thread(const std::string& host, int port, Mode mode) {
    embedded_ = nullptr;
    server_link_ = mode == Mode::kServer;
    plain_ = false;
    {
        const std::lock_guard lock(via_mutex_);
        via_.clear();
        via_listing_ = ListedServer{};
    }
    address_ = (host.find(':') != std::string::npos ? "[" + host + "]" : host) + ":"
               + std::to_string(port);
    host_name_ = host;
    port_ = port;
    mode_ = mode;
    stop_ = false;
    state_ = State::kConnecting;
    thread_ = std::thread([this] { run(); });
}

void Connection::close() {
    stop_ = true;
    wake_.notify_all();

    // Nothing to stop when it never started.
    if (thread_.joinable()) {
        thread_.join();
    }

    // The embedded host ends its party with it.
    if (embedded_ != nullptr) {
        embedded_->stop();
        embedded_.reset();
    }

    {
        const std::lock_guard lock(mutex_);
        outbox_.clear();
        udp_outbox_.clear();
    }
    mode_ = Mode::kNone;
    address_.clear();
    state_ = State::kIdle;
    {
        const std::lock_guard lock(via_mutex_);
        via_.clear();
        via_listing_ = ListedServer{};
    }
}

void Connection::connect_server(const std::string& host, int port) {
    close();
    start_thread(host, port, Mode::kServer);
}

bool Connection::connect_direct(const std::string& address) {
    std::string host;
    int port = kDefaultPort;

    // No host in it: nothing to connect to, and the link as it was.
    if (!parse_address(address, host, port)) {
        return false;
    }

    close();
    start_thread(host, port, Mode::kDirect);
    return true;
}

bool Connection::start_hosting(int port) {
    close();
    HostConfig config = host_config_;
    config.port = port;
    auto host = std::make_unique<Host>(config);

    // The host's frames reach the session as the server's would: decoded into the inbox.
    const bool listening = host->start([this](const Bytes& frame) {
        FrameReader reader;
        reader.feed(frame.data(), frame.size());
        Bytes body;
        Message m;

        // The host's frames are cut and decoded as a socket's would be.
        while (reader.next(body)) {
            // A frame this client does not know is skipped, as from a newer server.
            if (decode(body, m)) {
                const std::lock_guard lock(mutex_);
                inbox_.push_back(std::move(m));
            }
        }
    });

    // Not listening: why, for the page, and no link.
    if (!listening) {
        host_error_ = host->error();
        return false;
    }

    host_error_.clear();
    host_addresses_ = local_addresses();
    {
        const std::lock_guard lock(mutex_);
        inbox_.clear();
    }
    embedded_ = std::move(host);
    mode_ = Mode::kHosting;
    udp_ready_ = false;
    udp_heard_ms_ = 0;
    ++generation_;
    state_ = State::kConnected;
    return true;
}

void Connection::stop_hosting() {
    // Only a host stops hosting.
    if (mode_ == Mode::kHosting) {
        close();
    }
}

HostingStatus Connection::hosting() const {
    HostingStatus status;
    status.error = host_error_;

    // Not hosting: only the last error.
    if (mode_ != Mode::kHosting || embedded_ == nullptr) {
        return status;
    }

    status.listening = embedded_->running();
    status.port = embedded_->port();
    status.udp = embedded_->udp();
    status.addresses = host_addresses_;
    status.members = embedded_->members();
    return status;
}

bool Connection::udp() const {
    const std::uint64_t heard = udp_heard_ms_.load();
    return udp_ready_.load() && heard != 0
           && now_ms() - heard < static_cast<std::uint64_t>(kUdpTimeoutMs);
}

void Connection::send(Bytes frame) {
    // Hosting: straight to the embedded host, and when that was the host leaving, it stops.
    if (mode_ == Mode::kHosting) {
        if (embedded_ != nullptr) {
            embedded_->local_send(frame);

            // The host's LEAVE ended the party, so the host stops with it.
            if (embedded_->ended()) {
                close();
            }
        }

        return;
    }

    // A frame with nowhere to go is dropped: the server forgets a session with its connection.
    if (state_.load() != State::kConnected) {
        return;
    }

    {
        const std::lock_guard lock(mutex_);
        outbox_.push_back(std::move(frame));
    }
    wake_.notify_all();
}

bool Connection::state_needs_tcp(std::size_t bytes) const {
    return mode_ != Mode::kHosting && !(udp() && kClientStateHeader + bytes <= kMaxDatagramBytes);
}

void Connection::send_state(std::int32_t target, const Bytes& bytes, bool tcp) {
    // Hosting: routed by the host itself.
    if (mode_ == Mode::kHosting) {
        if (embedded_ != nullptr) {
            embedded_->local_state(target, bytes);
        }

        return;
    }

    // Over UDP when it was offered and the datagram fits; the session says when TCP goes as well.
    if (state_.load() == State::kConnected && udp_ready_.load()
        && kClientStateHeader + bytes.size() <= kMaxDatagramBytes) {
        {
            const std::lock_guard lock(mutex_);
            udp_outbox_.push_back(
                datagram_state(udp_session_.load(), udp_token_.load(), target, bytes)
            );
        }
        wake_.notify_all();
    }

    // The session asks for DATA too while UDP is unconfirmed, or when the state does not fit.
    if (tcp) {
        send(data(target, bytes));
    }
}

std::vector<Message> Connection::receive() {
    std::vector<Message> out;

    {
        const std::lock_guard lock(mutex_);
        out.swap(inbox_);
    }

    // The lag setting is read from the environment on the first call only.
    if (lag_read_ == 0) {
        lag_read_ = 1;
        const char* lag = std::getenv("OPENRAC_MP_LAG");

        // One or two numbers set the delay and the jitter; anything else leaves no lag.
        if (lag != nullptr && std::sscanf(lag, "%lf,%lf", &lag_ms_, &lag_jitter_ms_) >= 1) {
            lag_ms_ = std::max(0.0, lag_ms_);
            lag_jitter_ms_ = std::max(0.0, lag_jitter_ms_);
            std::fprintf(
                stderr,
                "multiplayer: OPENRAC_MP_LAG: %.0f ms more, jitter %.0f ms\n",
                lag_ms_,
                lag_jitter_ms_
            );
        }
    }

    // With no lag set and nothing held, messages go straight on.
    if (lag_ms_ <= 0.0 && lag_jitter_ms_ <= 0.0 && lagged_.empty()) {
        return out;
    }

    using std::chrono::steady_clock;
    const double now =
        std::chrono::duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();

    // Each new message gets a time to be handed on, never before the one ahead of it.
    for (Message& m : out) {
        // One step of a linear congruential generator (Knuth's MMIX constants); its top 53 bits
        // become a fraction below 1. The extra delay is exponential: mostly under the jitter,
        // sometimes several times it. The 0.999 keeps the log's argument above zero.
        lag_random_ = lag_random_ * 6364136223846793005ull + 1442695040888963407ull;
        const double u = static_cast<double>(lag_random_ >> 11) / 9007199254740992.0;
        const double extra = -lag_jitter_ms_ * std::log(1.0 - 0.999 * u);
        lag_last_ = std::max(lag_last_, now + lag_ms_ + extra);
        lagged_.emplace_back(lag_last_, std::move(m));
    }

    out.clear();

    // Messages whose time has come, in the order they arrived.
    while (!lagged_.empty() && lagged_.front().first <= now) {
        out.push_back(std::move(lagged_.front().second));
        lagged_.pop_front();
    }

    return out;
}

void Connection::run() {
    [[maybe_unused]] const Sockets sockets;

    // Connect, serve the connection while it lasts, wait, and again, until stopped.
    while (!stop_.load()) {
        state_ = State::kConnecting;
        socket_t fd = connect_to(host_name_, port_, stop_);

        // A server link's address may be the hub: asked first, on a connection of its own.
        if (fd != kNoSocket && server_link_ && !plain_) {
            std::string host;
            int port = 0;
            ListedServer picked;
            const Probe found = probe(static_cast<std::intptr_t>(fd), host, port, picked);
            close_socket(fd);
            fd = kNoSocket;

            // A game server: played on as ever, on a connection of its own.
            if (found == Probe::kPlain) {
                plain_ = true;
                fd = connect_to(host_name_, port_, stop_);
            } else if (found == Probe::kHub) {
                // The hub's pick: the game server's own connection.
                {
                    const std::lock_guard lock(via_mutex_);
                    via_ = (host.find(':') != std::string::npos ? "[" + host + "]" : host) + ":"
                           + std::to_string(port);
                    via_listing_ = picked;
                }
                fd = connect_to(host, port, stop_);
            }
        }

        // Connected: a new session on the server.
        if (fd != kNoSocket) {
            {
                const std::lock_guard lock(mutex_);
                outbox_.clear();
            }
            ++generation_;
            state_ = State::kConnected;
            serve(static_cast<std::intptr_t>(fd));
            close_socket(fd);
            state_ = State::kLost;
        } else {
            state_ = State::kUnreachable;
        }

        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(kRetryMs), [this] { return stop_.load(); });
    }
}

Connection::Probe Connection::probe(
    std::intptr_t handle, std::string& host, int& port, ListedServer& picked
) {
    const auto fd = static_cast<socket_t>(handle);
    std::string version;
    std::string name;
    std::string family;
    std::string region;
    {
        const std::lock_guard lock(mutex_);
        version = hello_version_;
        name = hello_name_;
        family = family_;
        region = region_;
    }

    // HELLO with no bits: neither a hub nor a game server keeps this connection's slot.
    if (!write_all(fd, hello(version, name, 0))) {
        return Probe::kFailed;
    }

    FrameReader frames;
    std::uint8_t chunk[kReadChunkBytes];
    bool greeted = false;
    bool hub = false;
    auto deadline = static_cast<std::int64_t>(now_ms()) + kProbeMs;

    // Until the answers are in, the time is up or the thread is stopped.
    while (!stop_.load()) {
        const auto now = static_cast<std::int64_t>(now_ms());

        // The time is up: a greeting without an offer is a server from before offers; a hub
        // that has not listed its servers is no use yet.
        if (now >= deadline) {
            return greeted && !hub ? Probe::kPlain : Probe::kFailed;
        }

        const auto slice = static_cast<int>(std::min<std::int64_t>(deadline - now, kSliceMs * 5));

        // Nothing yet: look again (wait returns within its slice for a stop).
        if (!wait(fd, false, slice, stop_)) {
            continue;
        }

        const auto n = recv(fd, reinterpret_cast<char*>(chunk), static_cast<int>(sizeof chunk), 0);

        // Closed or failed before it said what it is.
        if (n <= 0) {
            return Probe::kFailed;
        }

        frames.feed(chunk, static_cast<std::size_t>(n));
        Bytes body;

        // Every whole frame.
        while (frames.next(body)) {
            Message m;

            // A frame this client does not know is passed over, as on the game's link.
            if (!decode(body, m)) {
                continue;
            }

            // The HELLO answer: the offer follows at once, if it comes.
            if (m.kind == Message::Kind::kHello) {
                greeted = true;
                deadline = std::min(deadline, static_cast<std::int64_t>(now_ms()) + kOfferMs);
            } else if (m.kind == Message::Kind::kUdp) {
                // A game server's offer: play there.
                if ((m.features & kFeatureServers) == 0) {
                    return Probe::kPlain;
                }

                // The hub: its list of game servers, for this game.
                hub = true;

                // The request goes, or the hub is gone.
                if (!write_all(fd, regions(family))) {
                    return Probe::kFailed;
                }

                deadline = static_cast<std::int64_t>(now_ms()) + kProbeMs;
            } else if (m.kind == Message::Kind::kRegions) {
                return pick_server(m.servers, region, host_name_, host, port, &picked)
                           ? Probe::kHub
                           : Probe::kNoServer;
            } else if (m.kind == Message::Kind::kError) {
                return Probe::kFailed;
            }
        }

        // A length no server sends.
        if (frames.broken()) {
            return Probe::kFailed;
        }
    }

    return Probe::kFailed;
}

void Connection::serve(std::intptr_t handle) {
    const auto fd = static_cast<socket_t>(handle);
    FrameReader frames;
    std::uint8_t chunk[kReadChunkBytes];
    // UDP, once offered: a socket connected to the server's address and its UDP port.
    socket_t udp = kNoSocket;
    std::int64_t next_bind_ms = 0;
    // Keeping the link's word (kAliveMs): whether the server wants it, when it last went, when the
    // server was last heard.
    bool keep_alive = false;
    std::int64_t alive_ms = 0;
    std::int64_t heard_ms = static_cast<std::int64_t>(now_ms());
    udp_session_ = 0;
    udp_token_ = 0;
    udp_ready_ = false;
    udp_heard_ms_ = 0;
    {
        const std::lock_guard lock(mutex_);
        udp_outbox_.clear();
    }

    // Until the server goes or the thread is stopped.
    while (!stop_.load()) {
        std::deque<Bytes> out;
        std::deque<Bytes> datagrams;
        {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(kSliceMs / 2), [this] {
                return stop_.load() || !outbox_.empty() || !udp_outbox_.empty();
            });
            out.swap(outbox_);
            datagrams.swap(udp_outbox_);
        }

        bool lost = false;

        // On a server that keeps slots, this thread speaks and listens even while the game's ticks
        // stop.
        if (keep_alive) {
            const auto now = static_cast<std::int64_t>(now_ms());

            // A link that has gone quiet closes nothing by itself.
            if (now - heard_ms >= kServerSilentMs) {
                break;
            }

            // A TIME frame tells the server the link is alive.
            if (now - alive_ms >= kAliveMs) {
                alive_ms = now;
                out.push_back(time(now));
            }
        }

        // Everything queued, in order.
        for (const Bytes& f : out) {
            // The server is gone.
            if (!write_all(fd, f)) {
                lost = true;
                break;
            }
        }

        // A failed write ends this connection; run() tries again.
        if (lost) {
            break;
        }

        // The state datagrams, and a BIND every few seconds to keep the NAT mapping open. A lost
        // or refused datagram is no matter: the TCP link carries on.
        if (udp != kNoSocket) {
            const auto now = static_cast<std::int64_t>(now_ms());

            // BIND is due: it binds this address to the session and keeps the NAT mapping open.
            if (now >= next_bind_ms) {
                datagrams.push_front(datagram_bind(udp_session_.load(), udp_token_.load()));
                next_bind_ms = now + kBindMs;
            }

            // The BIND goes first, then the state in the order it was sent.
            for (const Bytes& d : datagrams) {
                ::send(udp, reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()), 0);
            }
        }

        pollfd_t fds[2] = {};
        fds[0].fd = fd;
        fds[0].events = POLLIN;
        fds[1].fd = udp;
        fds[1].events = POLLIN;

        // Nothing came from the server this slice.
        if (poll_sockets(fds, udp != kNoSocket ? 2 : 1, kSliceMs / 2) <= 0) {
            continue;
        }

        std::vector<Message> got;

        // Datagrams: STATE, handed in as the DATA it stands for.
        if (udp != kNoSocket && (fds[1].revents & POLLIN) != 0) {
            // One byte over the longest datagram, so a longer one is seen as too long.
            std::uint8_t datagram[kMaxDatagramBytes + 1];

            // A bounded drain: an error report comes once, but never spin on one.
            for (int i = 0; i < 256; ++i) {
                const auto n = recv(
                    udp, reinterpret_cast<char*>(datagram), static_cast<int>(sizeof datagram), 0
                );

                // None left, or an error report (a port unreachable): no datagram.
                if (n < 0) {
                    // No datagram is waiting any more.
                    if (would_block()) {
                        break;
                    }

                    continue;
                }

                Message m;

                // Only a STATE counts; it proves datagrams come back.
                if (decode_datagram(datagram, static_cast<std::size_t>(n), m)) {
                    udp_heard_ms_ = now_ms();
                    got.push_back(std::move(m));
                }
            }
        }

        // The TCP stream.
        if ((fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            const auto n =
                recv(fd, reinterpret_cast<char*>(chunk), static_cast<int>(sizeof chunk), 0);

            // Closed by the server (it closes on anything it finds wrong), or failed.
            if (n <= 0) {
                break;
            }

            heard_ms = static_cast<std::int64_t>(now_ms());
            frames.feed(chunk, static_cast<std::size_t>(n));
            Bytes body;

            // Every whole frame the bytes complete.
            while (frames.next(body)) {
                Message m;

                // An unknown packet is skipped, as a newer server may send one.
                if (!decode(body, m)) {
                    continue;
                }

                // The session id and the UDP offer are the connection's to keep as well.
                if (m.kind == Message::Kind::kHello) {
                    udp_session_ = m.id;
                } else if (m.kind == Message::Kind::kUdp) {
                    // A second offer: this connection took back an old session, bound afresh.
                    udp_token_ = m.token;
                    udp = udp == kNoSocket ? open_udp(fd, m.udp_port) : udp;
                    udp_ready_ = udp != kNoSocket && udp_session_.load() != 0;
                    next_bind_ms = 0;
                    keep_alive = (m.features & kFeatureResume) != 0;
                }

                got.push_back(std::move(m));
            }

            // A length no server sends: the stream is out of step, so the connection is dropped.
            if (frames.broken()) {
                break;
            }
        }

        const std::lock_guard lock(mutex_);
        inbox_.insert(
            inbox_.end(), std::make_move_iterator(got.begin()), std::make_move_iterator(got.end())
        );
    }

    udp_ready_ = false;

    // Only a connection that was offered UDP has a socket to close.
    if (udp != kNoSocket) {
        close_socket(udp);
    }
}

}  // namespace mp
