// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The byte stream under the WebSocket: a TCP connection, plain (ws://) or through TLS (wss://,
// what archipelago.gg asks for), both from mbedTLS's network layer so that one code path works on
// every system the mods are built for. Used by the client's thread only.
#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace ap {

class Transport {
public:
    Transport();
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    // Connects to host:port, with TLS when tls is set (the server's certificate checked against
    // the system's roots, for host's name). On failure returns false and error says why.
    bool open(const std::string& host, int port, bool tls, std::string& error);

    // Lets the connection go (safe when closed).
    void close();

    bool is_open() const;

    // Sends every byte; false when the connection broke.
    bool send(const char* data, std::size_t size);

    // Reads what has come, waiting at most timeout_ms for something. Returns the bytes read, 0
    // when nothing came in time, -1 when the connection is closed or broke.
    long receive(char* out, std::size_t size, int timeout_ms);

private:
    struct State;
    std::unique_ptr<State> state_;
};

// The system's roots for checking a server's certificate, loaded once; how many were found.
int load_system_roots();

}  // namespace ap
