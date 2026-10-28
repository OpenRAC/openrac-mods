/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The few socket calls the connection (connection.cpp) and the embedded host (host.cpp) share,
 * the same on Winsock and on POSIX sockets.
 */
#ifndef MULTIPLAYER_SOCKETS_H
#define MULTIPLAYER_SOCKETS_H

#include <cstdint>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

namespace mp::net {

#if defined(_WIN32)
using socket_t = SOCKET;
using pollfd_t = WSAPOLLFD;
constexpr socket_t kNoSocket = INVALID_SOCKET;

inline void close_socket(socket_t fd) {
    closesocket(fd);
}

inline void set_blocking(socket_t fd, bool blocking) {
    u_long nonblocking = blocking ? 0 : 1;
    ioctlsocket(fd, FIONBIO, &nonblocking);
}

/* Whether the last call failed only because it would have blocked. */
inline bool would_block() {
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}

/* A port another socket holds (exclusively, as the host binds it, Windows may say "access"). */
inline bool address_in_use() {
    const int e = WSAGetLastError();
    return e == WSAEADDRINUSE || e == WSAEACCES;
}

inline int poll_sockets(pollfd_t* fds, std::size_t count, int ms) {
    return WSAPoll(fds, static_cast<ULONG>(count), ms);
}

/* Winsock is started for each user (a thread, the host) and stopped after; it counts. */
struct Sockets {
    Sockets() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }

    ~Sockets() { WSACleanup(); }

    Sockets(const Sockets&) = delete;
    Sockets& operator=(const Sockets&) = delete;
};
#else
using socket_t = int;
using pollfd_t = pollfd;
constexpr socket_t kNoSocket = -1;

inline void close_socket(socket_t fd) {
    ::close(fd);
}

inline void set_blocking(socket_t fd, bool blocking) {
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
}

inline bool would_block() {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

inline bool address_in_use() {
    return errno == EADDRINUSE;
}

inline int poll_sockets(pollfd_t* fds, std::size_t count, int ms) {
    return ::poll(fds, static_cast<nfds_t>(count), ms);
}

/* Nothing to start off Windows. */
struct Sockets {};
#endif

/* send's flags: no SIGPIPE where the platform has the flag (macOS sets SO_NOSIGPIPE instead). */
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

/* No SIGPIPE from a socket whose peer went, where only a socket option can say so. */
inline void no_sigpipe([[maybe_unused]] socket_t fd) {
#if defined(SO_NOSIGPIPE)
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
}

inline void set_nodelay(socket_t fd) {
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
}

}  // namespace mp::net

#endif /* MULTIPLAYER_SOCKETS_H */
