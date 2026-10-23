/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The connection to a LiveSplit server (client.hpp).
 *
 * LiveSplit's own server speaks a line of text per command ("split", "reset", "starttimer",
 * "setgametime 12:34.56", "ping"), over TCP or over a WebSocket; both are here, and "auto" asks
 * the server which one it is before it says anything else.
 *
 * How it works: the game's thread calls Client::send, which only puts the command in a queue under
 * a mutex. The client's own thread (run) connects, writes what is queued, reads and discards what
 * the server says so the socket does not fill up, and goes back to waiting. The thread reconnects
 * every second while there is no server. The socket and its buffers belong to that thread alone;
 * the queue, the state text and the atomic flags are what the two threads share.
 */
#include "client.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

namespace livesplit {
namespace {

// ---- Constants ----

/* How long the client thread waits before it tries to connect again, in milliseconds (assumed). */
constexpr int kReconnectMs = 1000;

/* How long the client thread waits for a command or for the server to speak, and how long one read
 * of a handshake or probe waits, in milliseconds. A command is written at once, so a split is not
 * a tick late (assumed). */
constexpr int kSliceMs = 50;

/* How many slices (kSliceMs) the "auto" probe waits for the server's answer to its ping (assumed). */
constexpr std::size_t kProbeSlices = 32;

/* How many slices (kSliceMs) the WebSocket handshake waits for the server's answer (assumed). */
constexpr int kHandshakeSlices = 20;

/* The bytes one read of the socket takes at most (assumed). */
constexpr std::size_t kReadChunkBytes = 1024;

/* The TCP server's ping line, and its length (assumed). */
constexpr const char* kPingLine = "ping\n";
constexpr std::size_t kPingLineLength = 5;

/* The end of an HTTP header, and its length (documented: RFC 9112). */
constexpr const char* kHeaderEnd = "\r\n\r\n";
constexpr std::size_t kHeaderEndLength = 4;

/* The number of random bytes in a WebSocket handshake's key (documented: RFC 6455, section 4.1). */
constexpr std::size_t kHandshakeKeyBytes = 16;

/* The number of bytes in a WebSocket frame's mask (documented: RFC 6455, section 5.2). */
constexpr std::size_t kMaskBytes = 4;

/* The WebSocket frame's first byte for a final text frame, and for a final pong frame (documented:
 * RFC 6455, section 5.2). */
constexpr unsigned kFrameFinText = 0x81u;
constexpr unsigned kFrameFinPong = 0x8Au;

/* The low four bits of a frame's first byte: the opcode (documented: RFC 6455, section 5.2). */
constexpr unsigned kOpcodeMask = 0x0Fu;

/* The opcodes the client acts on: a continued frame, text, close and ping (documented: RFC 6455,
 * section 5.2). */
constexpr unsigned kOpcodeContinuation = 0x0u;
constexpr unsigned kOpcodeText = 0x1u;
constexpr unsigned kOpcodeClose = 0x8u;
constexpr unsigned kOpcodePing = 0x9u;

/* The top bit of a frame's second byte: the payload is masked (documented: RFC 6455, section 5.2). */
constexpr unsigned kMaskBit = 0x80u;

/* The low seven bits of a frame's second byte: the length, or a marker for a longer one
 * (documented: RFC 6455, section 5.2). */
constexpr unsigned kLengthMask = 0x7Fu;

/* The length markers: 126 says a 16-bit length follows, 127 a 64-bit one (documented: RFC 6455,
 * section 5.2). */
constexpr unsigned kLength16Marker = 126u;
constexpr unsigned kLength64Marker = 127u;

/* The largest length a 16-bit length field holds (documented: RFC 6455, section 5.2). */
constexpr std::size_t kLength16Max = 0xFFFFu;

/* The bytes a frame's header holds before any extended length: the two first bytes. */
constexpr std::size_t kFrameHeaderBytes = 2;

/* The bytes of an extended length: 16 bits is two, 64 bits is eight (documented: RFC 6455,
 * section 5.2). */
constexpr std::size_t kLength16Bytes = 2;
constexpr std::size_t kLength64Bytes = 8;

/* A number mixed into the random seed so it is never zero: the 32-bit golden-ratio constant often
 * used for hashing (assumed). */
constexpr std::uint32_t kSeedMix = 0x9E3779B9u;

/* The three shifts of the xorshift32 generator (documented: Marsaglia, "Xorshift RNGs", 2003). */
constexpr int kXorShiftA = 13;
constexpr int kXorShiftB = 17;
constexpr int kXorShiftC = 5;

// ---- Platform ----

#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kNoSocket = INVALID_SOCKET;

void close_socket(socket_t fd) {
    closesocket(fd);
}

/* Windows needs its sockets library started before the first call, and stopped after the last. */
bool start_winsock() {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void stop_winsock() {
    WSACleanup();
}
#else
using socket_t = int;
constexpr socket_t kNoSocket = -1;

void close_socket(socket_t fd) {
    ::close(fd);
}

/* Nothing to start off Windows; the call exists so run reads the same on every platform. */
bool start_winsock() {
    return true;
}

void stop_winsock() {}
#endif

// ---- Helpers ----

/* The bytes of a base64 text: what a WebSocket handshake's key is made of. The shifts and masks
 * are base64's own (RFC 4648): three bytes become four six-bit digits, with '=' padding. */
std::string base64(const unsigned char* data, std::size_t length) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;

    // Whole groups of three bytes.
    while (i + 3 <= length) {
        const unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(table[(v >> 18) & 0x3F]);
        out.push_back(table[(v >> 12) & 0x3F]);
        out.push_back(table[(v >> 6) & 0x3F]);
        out.push_back(table[v & 0x3F]);
        i += 3;
    }

    // One byte left over: two digits and two padding characters.
    if (i + 1 == length) {
        const unsigned v = static_cast<unsigned>(data[i]) << 16;
        out.push_back(table[(v >> 18) & 0x3F]);
        out.push_back(table[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == length) {
        /* Two bytes left over: three digits and one padding character. */
        const unsigned v =
            (static_cast<unsigned>(data[i]) << 16) | (static_cast<unsigned>(data[i + 1]) << 8);
        out.push_back(table[(v >> 18) & 0x3F]);
        out.push_back(table[(v >> 12) & 0x3F]);
        out.push_back(table[(v >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

/* The random bytes a WebSocket handshake's key and every frame's mask are made of: a seed from the
 * clock, never zero (an xorshift generator stays at zero once it is there). */
std::uint32_t random_seed() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::uint32_t seed = static_cast<std::uint32_t>(now) ^ kSeedMix;
    return seed != 0 ? seed : kSeedMix;
}

/* The next number of the xorshift32 generator; the seed moves on. */
std::uint32_t next_random(std::uint32_t& seed) {
    seed ^= seed << kXorShiftA;
    seed ^= seed >> kXorShiftB;
    seed ^= seed << kXorShiftC;
    return seed;
}

/* Fills out with length random bytes. */
void random_bytes(std::uint32_t& seed, unsigned char* out, std::size_t length) {
    // One generated number gives one byte.
    for (std::size_t i = 0; i < length; ++i) {
        out[i] = static_cast<unsigned char>(next_random(seed) & 0xFFu);
    }
}

/* Whether the socket has something to read, waiting at most timeout_ms for it. */
bool wait_readable(socket_t fd, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return select(static_cast<int>(fd) + 1, &set, nullptr, nullptr, &tv) > 0;
}

/* Connects to the host and port, trying each address the name gives in turn. Returns the connected
 * socket, or kNoSocket when none answered. The caller owns the socket. */
socket_t connect_to(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);

    // A name that does not resolve has no address to try.
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) {
        return kNoSocket;
    }

    socket_t fd = kNoSocket;

    // Each address in turn, until one connects.
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);

        // No socket for this address family: try the next address.
        if (fd == kNoSocket) {
            continue;
        }

        // Connected: this is the socket to use.
        if (connect(fd, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
            break;
        }

        close_socket(fd);
        fd = kNoSocket;
    }

    freeaddrinfo(addresses);
    return fd;
}

/* Reads the length of a WebSocket frame from the buffer. On entry length holds the seven-bit field
 * and offset the bytes of the header used so far; on success length is the payload's size and
 * offset has moved past any extended length. Returns false when the buffer does not hold the whole
 * length yet. */
bool read_frame_length(const std::string& buffer, std::size_t& length, std::size_t& offset) {
    // A 16-bit length follows the header.
    if (length == kLength16Marker) {
        // The buffer ends before the length does: wait for more bytes.
        if (buffer.size() < offset + kLength16Bytes) {
            return false;
        }

        length = (static_cast<unsigned char>(buffer[offset]) << 8)
                 | static_cast<unsigned char>(buffer[offset + 1]);
        offset += kLength16Bytes;
    } else if (length == kLength64Marker) {
        // A 64-bit length follows the header.
        if (buffer.size() < offset + kLength64Bytes) {
            return false;
        }

        length = 0;

        // Eight bytes, the most significant first.
        for (int i = 0; i < 8; ++i) {
            length = (length << 8)
                     | static_cast<unsigned char>(buffer[offset + static_cast<std::size_t>(i)]);
        }

        offset += kLength64Bytes;
    }

    return true;
}

/* The pong frame that answers a ping with this payload: masked, as a client's frames must be. */
std::string pong_frame(const std::string& payload) {
    std::uint32_t seed = random_seed();
    std::string frame;
    frame.push_back(static_cast<char>(kFrameFinPong));
    frame.push_back(static_cast<char>(kMaskBit | payload.size()));
    unsigned char key[kMaskBytes];
    random_bytes(seed, key, sizeof key);
    frame.append(reinterpret_cast<const char*>(key), kMaskBytes);

    // The payload is masked with the key, byte by byte.
    for (std::size_t i = 0; i < payload.size(); ++i) {
        frame.push_back(static_cast<char>(static_cast<unsigned char>(payload[i]) ^ key[i % 4]));
    }

    return frame;
}

/* The text frame that carries this text: masked, as a client's frames must be. */
std::string text_frame(const std::string& text) {
    std::uint32_t seed = random_seed();
    unsigned char key[kMaskBytes];
    random_bytes(seed, key, sizeof key);
    std::string frame;
    frame.push_back(static_cast<char>(kFrameFinText));
    const std::size_t length = text.size();

    // A short text has its length in the header; a longer one takes a 16-bit or a 64-bit length.
    if (length < kLength16Marker) {
        frame.push_back(static_cast<char>(kMaskBit | length));
    } else if (length <= kLength16Max) {
        frame.push_back(static_cast<char>(kMaskBit | kLength16Marker));
        frame.push_back(static_cast<char>((length >> 8) & 0xFFu));
        frame.push_back(static_cast<char>(length & 0xFFu));
    } else {
        frame.push_back(static_cast<char>(kMaskBit | kLength64Marker));

        // Eight bytes, the most significant first.
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((length >> (i * 8)) & 0xFFu));
        }
    }

    frame.append(reinterpret_cast<const char*>(key), kMaskBytes);

    // The text is masked with the key, byte by byte.
    for (std::size_t i = 0; i < length; ++i) {
        frame.push_back(static_cast<char>(static_cast<unsigned char>(text[i]) ^ key[i % 4]));
    }

    return frame;
}

}  // namespace

// ---- Socket reading and writing (the client's thread) ----

/* Closes the socket and forgets what was read. When a server had been there, what is queued is
 * stale and goes too. Client thread only. */
void Client::close() {
    // Only an open socket is closed.
    if (socket_ != kNoSocket) {
        close_socket(static_cast<socket_t>(socket_));
        socket_ = kNoSocket;
    }

    connected_ = false;
    buffer_.clear();
    std::lock_guard<std::mutex> lock(mutex_);

    // The server was there and is not: what is queued is stale.
    if (ever_connected_) {
        queue_.clear();
    }
}

/* Writes all length bytes. Closes the connection when a write fails. Client thread only. */
void Client::write_all(const char* data, std::size_t length) {
    std::size_t written = 0;

    // A write may take only part of what is asked: go on until all is out.
    while (written < length) {
        const int n = ::send(
            static_cast<socket_t>(socket_), data + written, static_cast<int>(length - written), 0
        );

        // The write failed or the peer is gone: the connection is over.
        if (n <= 0) {
            close();
            return;
        }

        written += static_cast<std::size_t>(n);
    }
}

/* Reads what the socket has into the buffer, waiting at most timeout_ms. Returns false when nothing
 * came; closes the connection when the peer has closed it. Client thread only. */
bool Client::fill(int timeout_ms) {
    // There is nothing to read from without a socket.
    if (socket_ == kNoSocket) {
        return false;
    }

    // Nothing arrived in time.
    if (!wait_readable(static_cast<socket_t>(socket_), timeout_ms)) {
        return false;
    }

    char chunk[kReadChunkBytes];
    const int n = ::recv(static_cast<socket_t>(socket_), chunk, sizeof chunk, 0);

    // Zero is the peer closing; less is an error: either way the connection is over.
    if (n <= 0) {
        close();
        return false;
    }

    buffer_.append(chunk, static_cast<std::size_t>(n));
    return true;
}

/* Takes one line (without its newline) from the buffer. Returns false when the buffer holds no
 * whole line yet. Client thread only. */
bool Client::read_line(std::string& line) {
    const std::size_t newline = buffer_.find('\n');

    // No newline yet: the line is not whole.
    if (newline == std::string::npos) {
        return false;
    }

    line = buffer_.substr(0, newline);
    buffer_.erase(0, newline + 1);
    return true;
}

/* A WebSocket frame: the header, the length, and the payload. The server's frames are not masked.
 * Returns true, with the text, for a text frame; answers a ping with a pong and closes the
 * connection on a close frame, returning false for both and for frames not yet whole. Client thread
 * only. */
bool Client::read_frame(std::string& text) {
    // Not even the two header bytes are here yet.
    if (buffer_.size() < kFrameHeaderBytes) {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(buffer_[0]);
    const unsigned char second = static_cast<unsigned char>(buffer_[1]);
    const unsigned opcode = first & kOpcodeMask;
    const bool masked = (second & kMaskBit) != 0;
    std::size_t length = second & kLengthMask;
    std::size_t offset = kFrameHeaderBytes;

    // The extended length is not all here yet.
    if (!read_frame_length(buffer_, length, offset)) {
        return false;
    }

    unsigned char mask[kMaskBytes] = {0, 0, 0, 0};

    // A masked frame carries its mask before the payload.
    if (masked) {
        // The mask is not all here yet.
        if (buffer_.size() < offset + kMaskBytes) {
            return false;
        }

        // The mask's four bytes.
        for (int i = 0; i < 4; ++i) {
            mask[i] = static_cast<unsigned char>(buffer_[offset + static_cast<std::size_t>(i)]);
        }

        offset += kMaskBytes;
    }

    // The payload is not all here yet.
    if (buffer_.size() < offset + length) {
        return false;
    }

    std::string payload = buffer_.substr(offset, length);
    buffer_.erase(0, offset + length);

    // A masked payload is unmasked with the frame's mask.
    if (masked) {
        // Each byte is combined with the mask byte at its place.
        for (std::size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % 4]);
        }
    }

    // The server closes the connection.
    if (opcode == kOpcodeClose) {
        close();
        return false;
    }

    // A ping: a pong is the answer.
    if (opcode == kOpcodePing) {
        const std::string frame = pong_frame(payload);
        write_all(frame.data(), frame.size());
        return false;
    }

    // Text, or a continuation of it: hand its payload to the caller.
    if (opcode == kOpcodeText || opcode == kOpcodeContinuation) {
        text = payload;
        return true;
    }

    return false;
}

/* Writes the text as one WebSocket text frame. Client thread only. */
void Client::write_frame(const std::string& text) {
    const std::string frame = text_frame(text);
    write_all(frame.data(), frame.size());
}

// ---- Connecting (the client's thread) ----

/* The WebSocket handshake: the request that upgrades the connection, and the wait for the server's
 * answer. Returns true when the server answers 101 (switching protocols). Client thread only. */
bool Client::handshake() {
    std::uint32_t seed = random_seed();
    unsigned char key_bytes[kHandshakeKeyBytes];
    random_bytes(seed, key_bytes, sizeof key_bytes);
    const std::string key = base64(key_bytes, sizeof key_bytes);
    const std::string request = "GET /livesplit HTTP/1.1\r\n"
                                "Host: "
                                + host_ + ":" + std::to_string(port_)
                                + "\r\n"
                                  "Upgrade: websocket\r\n"
                                  "Connection: Upgrade\r\n"
                                  "Sec-WebSocket-Key: "
                                + key
                                + "\r\n"
                                  "Sec-WebSocket-Version: 13\r\n"
                                  "\r\n";
    write_all(request.data(), request.size());
    std::string answer;

    // Wait a few slices for the answer's header to arrive whole.
    for (int i = 0; i < kHandshakeSlices; ++i) {
        // Nothing came this slice.
        if (!fill(kSliceMs)) {
            continue;
        }

        const std::size_t end = buffer_.find(kHeaderEnd);

        // The header is not whole yet.
        if (end == std::string::npos) {
            continue;
        }

        answer = buffer_.substr(0, end);
        buffer_.erase(0, end + kHeaderEndLength);
        break;
    }

    return answer.rfind("HTTP/1.1 101", 0) == 0;
}

/* The "auto" protocol: the server is asked which one it is. A TCP server answers "pong"; a
 * WebSocket server answers an HTTP error, which says it is one. Returns true when the connection is
 * ready to use, either way. Client thread only. */
bool Client::probe() {
    write_all(kPingLine, kPingLineLength);
    std::string answer;

    // Wait a few slices for the first line of the answer.
    for (std::size_t i = 0; i < kProbeSlices; ++i) {
        // Nothing came this slice.
        if (!fill(kSliceMs)) {
            continue;
        }

        const std::size_t newline = buffer_.find('\n');

        // The first line is not whole yet.
        if (newline == std::string::npos) {
            continue;
        }

        answer = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        break;
    }

    // A WebSocket server: it wants a handshake, and it took the first line for one. It closes the
    // connection after its error, so a new one is made for the handshake.
    if (answer.rfind("HTTP/", 0) == 0) {
        close();
        const socket_t fd = connect_to(host_, port_);

        // The server did not take the second connection.
        if (fd == kNoSocket) {
            return false;
        }

        socket_ = static_cast<std::intptr_t>(fd);
        buffer_.clear();
        websocket_ = true;
        return handshake();
    }

    /* "pong", or nothing at all: a TCP server. */
    buffer_.clear();
    return true;
}

/* Connects and prepares the protocol the setting asks for. Returns true when the connection is
 * ready; otherwise the state says why and it returns false. Client thread only. */
bool Client::open() {
    socket_t fd = connect_to(host_, port_);

    // No server answers on the address: say so, and let run try again later.
    if (fd == kNoSocket) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = "no LiveSplit server on " + host_ + ":" + std::to_string(port_);
        return false;
    }

    socket_ = static_cast<std::intptr_t>(fd);
    buffer_.clear();
    websocket_ = false;
    bool ready = false;

    // The protocol is the setting's: WebSocket handshakes, TCP needs nothing, auto asks the server.
    if (protocol_ == Protocol::kWebSocket) {
        websocket_ = true;
        ready = handshake();
    } else if (protocol_ == Protocol::kTcp) {
        ready = true;
    } else {
        ready = probe();
    }

    // The server was there but did not speak the protocol: drop the connection and say so.
    if (!ready) {
        close();
        std::lock_guard<std::mutex> lock(mutex_);
        state_ =
            "the LiveSplit server on " + host_ + ":" + std::to_string(port_) + " did not answer";
        return false;
    }

    connected_ = true;
    ever_connected_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = websocket_ ? "connected (WebSocket)" : "connected (TCP)";
    return true;
}

// ---- The client's thread ----

/* Writes every queued command to the server, in order, in the protocol's form. Client thread only. */
void Client::write_queued() {
    std::deque<std::string> commands;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commands.swap(queue_);
    }

    // One write per command.
    for (const std::string& command : commands) {
        // A WebSocket server takes a frame; a TCP server takes a line.
        if (websocket_) {
            write_frame(command);
        } else {
            const std::string line = command + "\n";
            write_all(line.data(), line.size());
        }
    }
}

/* Reads what the server says and drops it: nothing of it is needed, but it is read so the socket
 * does not fill up. Client thread only. */
void Client::discard_replies() {
    // Nothing to read is the usual case.
    if (!fill(0)) {
        return;
    }

    // A WebSocket server speaks in frames (and its pings are answered by read_frame).
    if (websocket_) {
        std::string text;

        // Frame after frame, until the buffer holds no more whole ones.
        while (read_frame(text)) {
            /* nothing */
        }
    } else {
        std::string line;

        // Line after line, until the buffer holds no more whole ones.
        while (read_line(line)) {
            /* nothing */
        }
    }
}

/* The client thread's loop: connect (trying again every second), write what is queued, read what
 * the server says, wait for the next command; until stop. Client thread only. */
void Client::run() {
    start_winsock();

    // Until the client is stopped.
    while (!stop_) {
        // Not connected: try, and when there is no server wait a second before the next try.
        if (socket_ == kNoSocket) {
            // No connection yet.
            if (!open()) {
                /* A second before the next try, or until the client is stopped. */
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait_for(lock, std::chrono::milliseconds(kReconnectMs), [this] {
                    return stop_.load();
                });
                continue;
            }
        }

        write_queued();
        discard_replies();

        /* Until a command comes, or the socket has something to say: a command is written at
         * once, so a split is not a tick late. */
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait_for(lock, std::chrono::milliseconds(kSliceMs), [this] {
                return stop_.load() || !queue_.empty();
            });
        }
    }

    close();
    stop_winsock();
}

// ---- Public interface (the game's thread) ----

/* Keeps the settings of the connection; nothing is opened until start. */
Client::Client(std::string host, int port, Protocol protocol)
    : host_(std::move(host)),
      port_(port),
      protocol_(protocol) {}

/* Stops the thread, so the object is never destroyed under a running thread. */
Client::~Client() {
    stop();
}

/* Starts the client's thread; does nothing when it is already running. */
void Client::start() {
    // Already running: a second thread would share the socket.
    if (thread_.joinable()) {
        return;
    }

    stop_ = false;
    thread_ = std::thread([this] { run(); });
}

/* Stops the thread and waits for it to end; does nothing when it is not running. */
void Client::stop() {
    // Not running: nothing to join.
    if (!thread_.joinable()) {
        return;
    }

    stop_ = true;
    ready_.notify_all();
    thread_.join();
}

/* Queues a command for the thread to write. Never waits for the network. Call it from the game's
 * thread. */
void Client::send(const std::string& command) {
    // A server that was there and is not: the command is stale, and a queue of stale splits would
    // jump the timer forward when the server comes back.
    if (!connected_ && ever_connected_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(command);
    }
    ready_.notify_one();
}

/* What the client knows of the connection, as text for the log. Any thread. */
std::string Client::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

}  // namespace livesplit
