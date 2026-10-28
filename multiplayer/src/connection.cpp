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

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

namespace mp {
namespace {

/* How long a connect may take, how long the thread waits before it tries again, and how long it
 * waits for the server or a queued frame before it looks again, in milliseconds (assumed). */
constexpr int kConnectMs = 3000;
constexpr int kRetryMs = 3000;
constexpr int kSliceMs = 20;

/* The bytes one read takes at most. */
constexpr std::size_t kReadChunkBytes = 4096;

#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kNoSocket = INVALID_SOCKET;

void close_socket(socket_t fd) {
    closesocket(fd);
}

void set_blocking(socket_t fd, bool blocking) {
    u_long nonblocking = blocking ? 0 : 1;
    ioctlsocket(fd, FIONBIO, &nonblocking);
}

/* Winsock is started per thread that uses it, and stopped after. */
struct Sockets {
    Sockets() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }

    ~Sockets() { WSACleanup(); }
};
#else
using socket_t = int;
constexpr socket_t kNoSocket = -1;

void close_socket(socket_t fd) {
    ::close(fd);
}

void set_blocking(socket_t fd, bool blocking) {
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
}

/* Nothing to start off Windows. */
struct Sockets {};
#endif

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
            int on = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
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
#if defined(MSG_NOSIGNAL)
        const auto n = ::send(
            fd,
            reinterpret_cast<const char*>(bytes.data() + sent),
            static_cast<int>(bytes.size() - sent),
            MSG_NOSIGNAL
        );
#else
        const auto n = ::send(
            fd,
            reinterpret_cast<const char*>(bytes.data() + sent),
            static_cast<int>(bytes.size() - sent),
            0
        );
#endif

        // The server is gone.
        if (n <= 0) {
            return false;
        }

        sent += static_cast<std::size_t>(n);
    }

    return true;
}

}  // namespace

Connection::~Connection() {
    close();
}

void Connection::open(const std::string& host, int port) {
    // One thread at a time.
    if (thread_.joinable()) {
        return;
    }

    host_ = host;
    port_ = port;
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

    state_ = State::kIdle;
}

void Connection::send(Bytes frame) {
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
        const socket_t fd = connect_to(host_, port_, stop_);

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

    // Until the server goes or the thread is stopped.
    while (!stop_.load()) {
        std::deque<Bytes> out;
        {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(kSliceMs / 2), [this] {
                return stop_.load() || !outbox_.empty();
            });
            out.swap(outbox_);
        }

        // Everything queued, in order.
        for (const Bytes& f : out) {
            // The server is gone.
            if (!write_all(fd, f)) {
                return;
            }
        }

        // Nothing came from the server this slice.
        if (!wait(fd, false, kSliceMs / 2)) {
            continue;
        }

        const auto n = recv(fd, reinterpret_cast<char*>(chunk), static_cast<int>(sizeof chunk), 0);

        // Closed by the server (it closes on anything it finds wrong), or failed.
        if (n <= 0) {
            return;
        }

        frames.feed(chunk, static_cast<std::size_t>(n));
        Bytes body;
        std::vector<Message> got;

        // Every whole frame the bytes complete.
        while (frames.next(body)) {
            Message m;

            // An unknown packet is skipped, as a newer server may send one.
            if (decode(body, m)) {
                got.push_back(std::move(m));
            }
        }

        // A length no server sends: the stream is out of step, so the connection is dropped.
        if (frames.broken()) {
            return;
        }

        const std::lock_guard lock(mutex_);
        inbox_.insert(
            inbox_.end(), std::make_move_iterator(got.begin()), std::make_move_iterator(got.end())
        );
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
    } else if (const auto colon = rest.rfind(':');
               colon != std::string::npos && rest.find(':') == colon) {
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
