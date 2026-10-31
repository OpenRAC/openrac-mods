// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The WebSocket client (websocket.hpp).
#include "websocket.hpp"

#include <chrono>
#include <cstring>

#include <mbedtls/sha1.h>

namespace ap {

namespace {

// The handshake's magic suffix (RFC 6455, 1.3).
const char kAcceptGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// The most a message may be: a data package of a large multiworld is a few megabytes.
constexpr std::size_t kMaxMessage = 64u * 1024u * 1024u;

// The most a handshake reply may be before it is taken as wrong.
constexpr std::size_t kMaxHandshake = 16u * 1024u;

// The frame header's bits.
constexpr std::uint8_t kFin = 0x80;
constexpr std::uint8_t kMask = 0x80;
constexpr std::uint8_t kOpcodeBits = 0x0F;
constexpr std::uint8_t kLengthBits = 0x7F;
constexpr std::uint8_t kLength16 = 126;
constexpr std::uint8_t kLength64 = 127;

// The next number of an xorshift32 generator: a frame's mask and the key need no secret.
std::uint32_t next(std::uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// Case-insensitive compare of a header's name.
bool same_name(const std::string& a, const char* b) {
    const std::size_t n = std::strlen(b);

    // Lengths first, then each character folded to lower case.
    if (a.size() != n) {
        return false;
    }

    for (std::size_t i = 0; i < n; ++i) {
        // ASCII only: header names are.
        const auto lower = [](char c) {
            return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
        };

        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }

    return true;
}

}  // namespace

namespace ws {

std::string base64(const unsigned char* data, std::size_t size) {
    static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;

    // Three bytes make four digits.
    for (; i + 3 <= size; i += 3) {
        const unsigned v = (unsigned(data[i]) << 16) | (unsigned(data[i + 1]) << 8) | data[i + 2];
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += kTable[(v >> 6) & 63];
        out += kTable[v & 63];
    }

    // One or two bytes left: padded with '='.
    if (i < size) {
        const unsigned v =
            (unsigned(data[i]) << 16) | (i + 1 < size ? unsigned(data[i + 1]) << 8 : 0);
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += i + 1 < size ? kTable[(v >> 6) & 63] : '=';
        out += '=';
    }

    return out;
}

std::string accept_for(const std::string& key) {
    const std::string text = key + kAcceptGuid;
    unsigned char digest[20];
    mbedtls_sha1(reinterpret_cast<const unsigned char*>(text.data()), text.size(), digest);
    return base64(digest, sizeof digest);
}

std::string handshake_request(const std::string& host, int port, const std::string& key) {
    return "GET / HTTP/1.1\r\n"
           "Host: "
           + host + ":" + std::to_string(port)
           + "\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: "
           + key
           + "\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "User-Agent: OpenRAC-Archipelago\r\n\r\n";
}

std::string encode(Opcode op, const std::string& payload, const std::uint8_t mask[4]) {
    std::string frame;
    const std::size_t n = payload.size();
    frame += char(kFin | op);

    // The length: in the header, in 16 bits, or in 64 bits.
    if (n < kLength16) {
        frame += char(kMask | n);
    } else if (n <= 0xFFFF) {
        frame += char(kMask | kLength16);
        frame += char((n >> 8) & 0xFF);
        frame += char(n & 0xFF);
    } else {
        frame += char(kMask | kLength64);

        // Most significant byte first.
        for (int i = 7; i >= 0; --i) {
            frame += char((std::uint64_t(n) >> (i * 8)) & 0xFF);
        }
    }

    frame.append(reinterpret_cast<const char*>(mask), 4);

    // The payload, masked byte by byte.
    for (std::size_t i = 0; i < n; ++i) {
        frame += char(std::uint8_t(payload[i]) ^ mask[i % 4]);
    }

    return frame;
}

bool decode(std::string& buffer, Opcode& op, bool& fin, std::string& payload, bool& bad) {
    bad = false;

    // The two bytes every header has.
    if (buffer.size() < 2) {
        return false;
    }

    const auto* b = reinterpret_cast<const std::uint8_t*>(buffer.data());
    fin = (b[0] & kFin) != 0;
    op = Opcode(b[0] & kOpcodeBits);
    const bool masked = (b[1] & kMask) != 0;
    std::uint64_t n = b[1] & kLengthBits;
    std::size_t at = 2;

    // A longer length follows the header.
    if (n == kLength16 || n == kLength64) {
        const std::size_t bytes = n == kLength16 ? 2 : 8;

        // Not all of the length yet.
        if (buffer.size() < at + bytes) {
            return false;
        }

        n = 0;

        // Most significant byte first.
        for (std::size_t i = 0; i < bytes; ++i) {
            n = (n << 8) | b[at + i];
        }

        at += bytes;
    }

    // Past what the client takes: the connection is dropped.
    if (n > kMaxMessage) {
        bad = true;
        return false;
    }

    std::uint8_t key[4] = {0, 0, 0, 0};

    // A server's frames are not masked, but one that is still reads right.
    if (masked) {
        // Not all of the key yet.
        if (buffer.size() < at + 4) {
            return false;
        }

        std::memcpy(key, b + at, 4);
        at += 4;
    }

    // Not all of the payload yet.
    if (buffer.size() < at + n) {
        return false;
    }

    payload.assign(buffer, at, std::size_t(n));

    // Unmask when masked.
    if (masked) {
        for (std::size_t i = 0; i < payload.size(); ++i) {
            payload[i] = char(std::uint8_t(payload[i]) ^ key[i % 4]);
        }
    }

    buffer.erase(0, at + std::size_t(n));
    return true;
}

}  // namespace ws

bool WebSocket::open(const std::string& host, int port, bool tls, std::string& error) {
    close();
    seed_ ^= std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
    seed_ = seed_ != 0 ? seed_ : 0x9E3779B9u;

    // The stream first.
    if (!transport_.open(host, port, tls, error)) {
        return false;
    }

    // The handshake: a random key, the request, and the server's reply up to its blank line.
    unsigned char raw[16];

    for (unsigned char& c : raw) {
        c = std::uint8_t(next(seed_));
    }

    const std::string key = ws::base64(raw, sizeof raw);
    const std::string request = ws::handshake_request(host, port, key);

    // The request could not be sent.
    if (!transport_.send(request.data(), request.size())) {
        error = "the connection closed during the WebSocket handshake";
        close();
        return false;
    }

    std::string reply;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    // Read until the headers end.
    while (reply.find("\r\n\r\n") == std::string::npos) {
        char chunk[2048];
        const long got = transport_.receive(chunk, sizeof chunk, 200);

        // Broken, too long, or too slow.
        if (got < 0 || reply.size() > kMaxHandshake
            || std::chrono::steady_clock::now() > deadline) {
            error = tls ? "no WebSocket answer over TLS" : "no WebSocket answer";
            close();
            return false;
        }

        reply.append(chunk, std::size_t(got));
    }

    const std::size_t end = reply.find("\r\n\r\n");
    buffer_ = reply.substr(end + 4);  // the server may have sent a frame already
    reply.resize(end);

    // The status must be 101.
    if (reply.compare(0, 12, "HTTP/1.1 101") != 0) {
        error = "the server refused the WebSocket: " + reply.substr(0, reply.find("\r\n"));
        close();
        return false;
    }

    const std::string want = ws::accept_for(key);
    bool accepted = false;
    std::size_t at = reply.find("\r\n");

    // Each header line, for Sec-WebSocket-Accept.
    while (at != std::string::npos) {
        const std::size_t start = at + 2;
        at = reply.find("\r\n", start);
        const std::string line =
            reply.substr(start, at == std::string::npos ? std::string::npos : at - start);
        const std::size_t colon = line.find(':');

        // A header with a name and a value.
        if (colon != std::string::npos
            && same_name(line.substr(0, colon), "sec-websocket-accept")) {
            std::size_t v = colon + 1;

            while (v < line.size() && line[v] == ' ') {
                ++v;
            }

            accepted = line.substr(v) == want;
        }
    }

    // A server that does not answer the key is not a WebSocket server.
    if (!accepted) {
        error = "the server's WebSocket answer is wrong";
        close();
        return false;
    }

    message_.clear();
    return true;
}

void WebSocket::close() {
    transport_.close();
    buffer_.clear();
    message_.clear();
}

bool WebSocket::is_open() const {
    return transport_.is_open();
}

bool WebSocket::send_frame(ws::Opcode op, const std::string& payload) {
    const std::uint32_t r = next(seed_);
    const std::uint8_t mask[4] =
        {std::uint8_t(r), std::uint8_t(r >> 8), std::uint8_t(r >> 16), std::uint8_t(r >> 24)};
    const std::string frame = ws::encode(op, payload, mask);
    return transport_.send(frame.data(), frame.size());
}

bool WebSocket::send_text(const std::string& text) {
    return send_frame(ws::kText, text);
}

int WebSocket::receive(std::string& text, int timeout_ms) {
    // Until a whole message, or nothing more in time.
    for (;;) {
        ws::Opcode op;
        bool fin = false, bad = false;
        std::string payload;

        // Every whole frame in the buffer, before reading more.
        while (ws::decode(buffer_, op, fin, payload, bad)) {
            switch (op) {
                case ws::kPing:
                    send_frame(ws::kPong, payload);
                    break;
                case ws::kClose:
                    send_frame(ws::kClose, payload.substr(0, 2));
                    close();
                    return -1;
                case ws::kText:
                case ws::kBinary:
                case ws::kContinuation:
                    message_ += payload;

                    // Too big, joined.
                    if (message_.size() > kMaxMessage) {
                        close();
                        return -1;
                    }

                    // The last fragment: the message is whole.
                    if (fin) {
                        text.swap(message_);
                        message_.clear();
                        return 1;
                    }

                    break;
                default:
                    break;  // a pong, or a reserved opcode: nothing to do
            }
        }

        // A frame the client cannot take.
        if (bad) {
            close();
            return -1;
        }

        char chunk[16384];
        const long got = transport_.receive(chunk, sizeof chunk, timeout_ms);

        // Broken, or nothing in time.
        if (got <= 0) {
            return got < 0 ? -1 : 0;
        }

        buffer_.append(chunk, std::size_t(got));
        timeout_ms = 0;  // the rest of a message that started is read without waiting long
    }
}

}  // namespace ap
