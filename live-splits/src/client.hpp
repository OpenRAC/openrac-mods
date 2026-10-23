/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The connection to a LiveSplit server (client.cpp): the commands the mod sends it, and what the
 * mod knows of it.
 */
#ifndef LIVE_SPLITS_CLIENT_H
#define LIVE_SPLITS_CLIENT_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace livesplit {

/* How the mod talks to LiveSplit's server. */
enum class Protocol {
    kAuto,      /* ask the server which one it is */
    kTcp,       /* the TCP server: a line of text per command */
    kWebSocket, /* the WebSocket server: the same commands in text frames */
};

/* The connection to a LiveSplit server.
 *
 * The socket is the thread's own: the game's thread only puts commands in the queue and reads the
 * state, and never waits for the network. A command given before the server is there is kept and
 * sent when it comes; a command given while a server that was there is gone is dropped: a queue
 * of stale splits would jump the timer forward when the server comes back.
 */
class Client {
public:
    Client(std::string host, int port, Protocol protocol);
    ~Client();

    /* Starts the thread. It connects when it can and keeps trying while it cannot. */
    void start();
    /* Stops the thread and closes the connection. */
    void stop();

    /* A command for LiveSplit ("split", "reset", "starttimer", ...). From the game's thread. */
    void send(const std::string& command);

    /* What the client knows of the connection, for the log. */
    std::string state() const;

private:
    /* The client thread's loop; it is the only one to touch the socket and its buffer. */
    void run();
    /* Connects and prepares the protocol; true when the connection is ready. */
    bool open();
    /* The WebSocket upgrade request and its answer; true when the server accepts. */
    bool handshake();
    /* The "auto" protocol's ping, which tells a TCP server from a WebSocket one. */
    bool probe();
    /* Closes the socket and forgets what was read. */
    void close();
    /* Writes every queued command to the server. */
    void write_queued();
    /* Reads what the server says and drops it. */
    void discard_replies();
    void write_all(const char* data, std::size_t length);
    bool fill(int timeout_ms);          /* reads into the buffer; false when nothing came */
    bool read_line(std::string& line);  /* the TCP server's answers */
    bool read_frame(std::string& text); /* the WebSocket server's answers */
    void write_frame(const std::string& text);

    /* The address and protocol given at construction; never change, so any thread may read them. */
    const std::string host_;
    const int port_;
    const Protocol protocol_;

    /* Set by stop, read by the client thread, to end its loop. */
    std::atomic<bool> stop_{false};
    /* Whether a server is connected now: written by the client thread, read by the game's thread. */
    std::atomic<bool> connected_{false};
    /* Whether a server has ever been connected: it tells a stale command from an early one. */
    std::atomic<bool> ever_connected_{false};
    /* The client's thread. Started and joined by the game's thread. */
    std::thread thread_;
    /* Guards queue_ and state_, which both threads touch. */
    mutable std::mutex mutex_;
    /* Wakes the client thread when a command is queued or the client is stopped. */
    std::condition_variable ready_;
    /* The commands the game's thread has given and the client's thread has not yet written. */
    std::deque<std::string> queue_;
    /* What the client knows of the connection, for the log. Under mutex_. */
    std::string state_ = "not connected";
    /* The client thread's own: the bytes read and not yet used. */
    std::string buffer_;
    /* The client thread's own: the socket, -1 when none (an int on Unix, a SOCKET on Windows). */
    std::intptr_t socket_ = -1;
    /* The client thread's own: whether the server speaks WebSocket rather than TCP. */
    bool websocket_ = false;
};

}  // namespace livesplit

#endif /* LIVE_SPLITS_CLIENT_H */
