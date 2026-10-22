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
    void run();
    bool open();
    bool handshake();
    bool probe();
    void close();
    void write_all(const char* data, std::size_t length);
    bool fill(int timeout_ms);          /* reads into the buffer; false when nothing came */
    bool read_line(std::string& line);  /* the TCP server's answers */
    bool read_frame(std::string& text); /* the WebSocket server's answers */
    void write_frame(const std::string& text);

    const std::string host_;
    const int port_;
    const Protocol protocol_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> ever_connected_{false};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::string> queue_;
    std::string state_ = "not connected";
    std::string buffer_; /* the bytes read and not yet used */
    std::intptr_t socket_ = -1;
    bool websocket_ = false;
};

}  // namespace livesplit

#endif /* LIVE_SPLITS_CLIENT_H */
