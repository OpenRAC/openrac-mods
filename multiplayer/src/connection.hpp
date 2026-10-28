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
 */
#ifndef MULTIPLAYER_CONNECTION_H
#define MULTIPLAYER_CONNECTION_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "protocol.hpp"

namespace mp {

/* Where frames go: the connection, or a test's recorder (session.hpp). */
class Transport {
public:
    virtual ~Transport() = default;
    /* Queues one whole frame (protocol.hpp's encoders give them). */
    virtual void send(Bytes frame) = 0;
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

    Connection() = default;
    ~Connection() override;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    /* Starts the thread for host:port; it connects when it can. Nothing when it runs already. */
    void open(const std::string& host, int port);
    /* Stops the thread and closes the socket. */
    void close();

    void send(Bytes frame) override;
    /* The messages the server sent since the last call, in order. */
    std::vector<Message> receive();

    State state() const { return state_.load(); }

    /* Counts the connections made: a new value, a new session on the server. */
    std::uint32_t generation() const { return generation_.load(); }

private:
    void run();
    /* One connection's life: writes and reads until it goes or the thread stops. */
    void serve(std::intptr_t fd);

    std::string host_;
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
};

/* Splits "host:port" (or "[v6]:port", or a bare host) into its parts; a missing or bad port is
 * kDefaultPort. False for an empty host. */
bool parse_address(const std::string& text, std::string& host, int& port);

}  // namespace mp

#endif /* MULTIPLAYER_CONNECTION_H */
