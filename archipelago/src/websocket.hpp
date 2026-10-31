// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// A WebSocket client (RFC 6455) on a Transport: the opening handshake, text messages out (masked,
// as a client's must be), whole messages in (fragments joined), pings answered. The frame codec is
// apart from the socket so that the tests can feed it bytes.
#pragma once

#include <cstdint>
#include <string>

#include "transport.hpp"

namespace ap {

// The codec: frames to bytes and bytes to messages.
namespace ws {

// Opcodes the client deals in.
enum Opcode : std::uint8_t {
    kContinuation = 0x0,
    kText = 0x1,
    kBinary = 0x2,
    kClose = 0x8,
    kPing = 0x9,
    kPong = 0xA,
};

// A client's frame: final, masked with `mask`, the payload as given.
std::string encode(Opcode op, const std::string& payload, const std::uint8_t mask[4]);

// One frame taken from the front of buffer: true and its opcode, fin bit and payload (unmasked),
// with its bytes removed from buffer; false when buffer does not hold a whole frame yet.
// bad is set for a frame the client cannot take (a length past what it allows).
bool decode(std::string& buffer, Opcode& op, bool& fin, std::string& payload, bool& bad);

// The handshake's request for host:port at path, with the key (base64 of 16 random bytes).
std::string handshake_request(const std::string& host, int port, const std::string& key);

// The value the server's Sec-WebSocket-Accept must have for this key.
std::string accept_for(const std::string& key);

std::string base64(const unsigned char* data, std::size_t size);

}  // namespace ws

class WebSocket {
public:
    // Connects the transport and makes the handshake. False and error when either failed.
    bool open(const std::string& host, int port, bool tls, std::string& error);

    void close();
    bool is_open() const;

    // Sends a text message; false when the connection broke.
    bool send_text(const std::string& text);

    // The next whole message: 1 and its text, 0 when none came within timeout_ms, -1 when the
    // connection closed or broke.
    int receive(std::string& text, int timeout_ms);

private:
    bool send_frame(ws::Opcode op, const std::string& payload);

    Transport transport_;
    std::string buffer_;   // bytes read and not yet a whole frame
    std::string message_;  // the fragments of a message so far
    std::uint32_t seed_ = 0x9E3779B9u;
};

}  // namespace ap
