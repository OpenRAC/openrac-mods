/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The connection to the OpenRAC server (connection.hpp): a socket on a thread of its own, as
 * live-splits' client keeps its LiveSplit socket. The connect is non-blocking with a time limit, so
 * that close() (at the game's end) never waits on an address that does not answer.
 */
#include "connection.hpp"

#include <chrono>
#include <cstring>
#include <utility>

#include "session.hpp"
#include "sockets.hpp"

namespace mp {

using namespace net;

namespace {

/* How long a connect may take, how long the thread waits before it tries again, and how long it
 * waits for the server or a queued frame before it looks again, in milliseconds (assumed). */
constexpr int kConnectMs = 3000;
constexpr int kRetryMs = 3000;
constexpr int kSliceMs = 20;

/* The bytes one read takes at most. */
constexpr std::size_t kReadChunkBytes = 4096;

/* Waits at most `ms` for the socket to be readable (or writable); true when it is. */
bool wait(socket_t fd, bool for_write, int ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    const int n = select(
        static_cast<int>(fd) + 1,
        for_write ? nullptr : &set,
        for_write ? &set : nullptr,
        nullptr,
        &tv
    );
    return n > 0;
}

/* Connects to host:port, each address the name gives in turn, within kConnectMs each. The socket,
 * blocking again and with TCP_NODELAY, or kNoSocket. */
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
        if (wait(fd, true, kConnectMs)
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

/* Writes all of `bytes`; false when the socket failed. */
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

/* A UDP socket connected to the TCP peer's address at `port`, non-blocking; kNoSocket when there
 * is none to be had (then state stays on TCP). */
socket_t open_udp(socket_t tcp, std::int32_t port) {
    sockaddr_storage peer{};
    socklen_t length = sizeof peer;

    // A port no server gives, or no peer to read.
    if (port < 1 || port > 65535
        || getpeername(tcp, reinterpret_cast<sockaddr*>(&peer), &length) != 0) {
        return kNoSocket;
    }

    if (peer.ss_family == AF_INET6) {
        reinterpret_cast<sockaddr_in6*>(&peer)->sin6_port = htons(static_cast<std::uint16_t>(port));
    } else {
        reinterpret_cast<sockaddr_in*>(&peer)->sin_port = htons(static_cast<std::uint16_t>(port));
    }

    const socket_t fd = socket(peer.ss_family, SOCK_DGRAM, 0);

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

void Connection::start_thread(const std::string& host, int port, Mode mode) {
    embedded_ = nullptr;
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

        while (reader.next(body)) {
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

void Connection::send_state(std::int32_t target, const Bytes& bytes) {
    // Hosting: routed by the host itself.
    if (mode_ == Mode::kHosting) {
        if (embedded_ != nullptr) {
            embedded_->local_state(target, bytes);
        }

        return;
    }

    // Over UDP when it was offered and the datagram fits; TCP as well until UDP is confirmed.
    if (state_.load() == State::kConnected && udp_ready_.load()
        && kClientStateHeader + bytes.size() <= kMaxDatagramBytes) {
        {
            const std::lock_guard lock(mutex_);
            udp_outbox_.push_back(
                datagram_state(udp_session_.load(), udp_token_.load(), target, bytes)
            );
        }
        wake_.notify_all();

        if (udp()) {
            return;
        }
    }

    send(data(target, bytes));
}

std::vector<Message> Connection::receive() {
    const std::lock_guard lock(mutex_);
    std::vector<Message> out;
    out.swap(inbox_);
    return out;
}

void Connection::run() {
    [[maybe_unused]] const Sockets sockets;

    // Connect, serve the connection while it lasts, wait, and again, until stopped.
    while (!stop_.load()) {
        state_ = State::kConnecting;
        const socket_t fd = connect_to(host_name_, port_, stop_);

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

void Connection::serve(std::intptr_t handle) {
    const auto fd = static_cast<socket_t>(handle);
    FrameReader frames;
    std::uint8_t chunk[kReadChunkBytes];
    // UDP, once offered: a socket connected to the server's address and its UDP port.
    socket_t udp = kNoSocket;
    std::int64_t next_bind_ms = 0;
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

        // Everything queued, in order.
        for (const Bytes& f : out) {
            // The server is gone.
            if (!write_all(fd, f)) {
                lost = true;
                break;
            }
        }

        if (lost) {
            break;
        }

        // The state datagrams, and a BIND every few seconds to keep the NAT mapping open. A lost
        // or refused datagram is no matter: the TCP link carries on.
        if (udp != kNoSocket) {
            const auto now = static_cast<std::int64_t>(now_ms());

            if (now >= next_bind_ms) {
                datagrams.push_front(datagram_bind(udp_session_.load(), udp_token_.load()));
                next_bind_ms = now + kBindMs;
            }

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
            std::uint8_t datagram[kMaxDatagramBytes + 1];

            // A bounded drain: an error report comes once, but never spin on one.
            for (int i = 0; i < 256; ++i) {
                const auto n = recv(
                    udp, reinterpret_cast<char*>(datagram), static_cast<int>(sizeof datagram), 0
                );

                // None left, or an error report (a port unreachable): no datagram.
                if (n < 0) {
                    if (would_block()) {
                        break;
                    }

                    continue;
                }

                Message m;

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
                } else if (m.kind == Message::Kind::kUdp && udp == kNoSocket) {
                    udp_token_ = m.token;
                    udp = open_udp(fd, m.udp_port);
                    udp_ready_ = udp != kNoSocket && udp_session_.load() != 0;
                    next_bind_ms = 0;
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

    if (udp != kNoSocket) {
        close_socket(udp);
    }
}

bool parse_address(const std::string& text, std::string& host, int& port) {
    port = kDefaultPort;
    std::string rest = text;
    std::string port_text;

    // "[v6]:port" or "[v6]".
    if (!rest.empty() && rest.front() == '[') {
        const auto close = rest.find(']');

        // An opening bracket alone is no address.
        if (close == std::string::npos) {
            return false;
        }

        host = rest.substr(1, close - 1);
        port_text = close + 1 < rest.size() && rest[close + 1] == ':' ? rest.substr(close + 2) : "";
    } else if (
        const auto colon = rest.rfind(':'); colon != std::string::npos && rest.find(':') == colon
    ) {
        // "host:port": one colon (more is a bare IPv6 address).
        host = rest.substr(0, colon);
        port_text = rest.substr(colon + 1);
    } else {
        host = rest;
    }

    // A port of digits from 1 to 65535; anything else keeps the default.
    if (!port_text.empty() && port_text.size() <= 5
        && port_text.find_first_not_of("0123456789") == std::string::npos) {
        const int p = std::stoi(port_text);
        port = p >= 1 && p <= 65535 ? p : kDefaultPort;
    }

    return !host.empty();
}

}  // namespace mp
