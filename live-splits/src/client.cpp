/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The connection to a LiveSplit server (client.hpp).
 *
 * LiveSplit's own server speaks a line of text per command ("split", "reset", "starttimer",
 * "setgametime 12:34.56", "ping"), over TCP or over a WebSocket; both are here, and "auto" asks
 * the server which one it is before it says anything else.
 */
#include "client.hpp"

#include <chrono>
#include <cstring>

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

#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kNoSocket = INVALID_SOCKET;

void close_socket(socket_t fd) {
    closesocket(fd);
}

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

bool start_winsock() {
    return true;
}

void stop_winsock() {}
#endif

/* The bytes of a base64 text: what a WebSocket handshake's key is made of. */
std::string base64(const unsigned char* data, std::size_t length) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    while (i + 3 <= length) {
        const unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(table[(v >> 18) & 0x3F]);
        out.push_back(table[(v >> 12) & 0x3F]);
        out.push_back(table[(v >> 6) & 0x3F]);
        out.push_back(table[v & 0x3F]);
        i += 3;
    }
    if (i + 1 == length) {
        const unsigned v = static_cast<unsigned>(data[i]) << 16;
        out.push_back(table[(v >> 18) & 0x3F]);
        out.push_back(table[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == length) {
        const unsigned v =
            (static_cast<unsigned>(data[i]) << 16) | (static_cast<unsigned>(data[i + 1]) << 8);
        out.push_back(table[(v >> 18) & 0x3F]);
        out.push_back(table[(v >> 12) & 0x3F]);
        out.push_back(table[(v >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

/* The random bytes a WebSocket handshake's key and every frame's mask are made of. */
std::uint32_t random_seed() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::uint32_t seed = static_cast<std::uint32_t>(now) ^ 0x9E3779B9u;
    return seed != 0 ? seed : 0x9E3779B9u;
}

std::uint32_t next_random(std::uint32_t& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

void random_bytes(std::uint32_t& seed, unsigned char* out, std::size_t length) {
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

socket_t connect_to(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) {
        return kNoSocket;
    }
    socket_t fd = kNoSocket;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd == kNoSocket) {
            continue;
        }
        if (connect(fd, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
            break;
        }
        close_socket(fd);
        fd = kNoSocket;
    }
    freeaddrinfo(addresses);
    return fd;
}

}  // namespace

Client::Client(std::string host, int port, Protocol protocol)
    : host_(std::move(host)),
      port_(port),
      protocol_(protocol) {}

Client::~Client() {
    stop();
}

void Client::start() {
    if (thread_.joinable()) {
        return;
    }
    stop_ = false;
    thread_ = std::thread([this] { run(); });
}

void Client::stop() {
    if (!thread_.joinable()) {
        return;
    }
    stop_ = true;
    ready_.notify_all();
    thread_.join();
}

void Client::send(const std::string& command) {
    if (!connected_ && ever_connected_) {
        /* A server that was there and is not: the command is stale, and a queue of stale splits
         * would jump the timer forward when the server comes back. */
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(command);
    }
    ready_.notify_one();
}

std::string Client::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void Client::run() {
    start_winsock();
    while (!stop_) {
        if (socket_ == kNoSocket) {
            if (!open()) {
                /* A second before the next try, or until the client is stopped. */
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait_for(lock, std::chrono::milliseconds(1000), [this] {
                    return stop_.load();
                });
                continue;
            }
        }
        /* The commands to write. */
        std::deque<std::string> commands;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            commands.swap(queue_);
        }
        for (const std::string& command : commands) {
            if (websocket_) {
                write_frame(command);
            } else {
                const std::string line = command + "\n";
                write_all(line.data(), line.size());
            }
        }
        /* What the server says: nothing of it is needed, but it is read so the socket does not
         * fill up. */
        if (fill(0)) {
            if (websocket_) {
                std::string text;
                while (read_frame(text)) {
                    /* nothing */
                }
            } else {
                std::string line;
                while (read_line(line)) {
                    /* nothing */
                }
            }
        }
        /* Until a command comes, or the socket has something to say: a command is written at
         * once, so a split is not a tick late. */
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait_for(lock, std::chrono::milliseconds(50), [this] {
                return stop_.load() || !queue_.empty();
            });
        }
    }
    close();
    stop_winsock();
}

bool Client::open() {
    socket_t fd = connect_to(host_, port_);
    if (fd == kNoSocket) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = "no LiveSplit server on " + host_ + ":" + std::to_string(port_);
        return false;
    }
    socket_ = static_cast<std::intptr_t>(fd);
    buffer_.clear();
    websocket_ = false;
    bool ready = false;
    if (protocol_ == Protocol::kWebSocket) {
        websocket_ = true;
        ready = handshake();
    } else if (protocol_ == Protocol::kTcp) {
        ready = true;
    } else {
        ready = probe();
    }
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

/* The "auto" protocol: the server is asked which one it is. A TCP server answers "pong"; a
 * WebSocket server answers an HTTP error, which says it is one. */
bool Client::probe() {
    write_all("ping\n", 5);
    std::string answer;
    const std::size_t deadline = 32;
    for (std::size_t i = 0; i < deadline; ++i) {
        if (!fill(50)) {
            continue;
        }
        const std::size_t newline = buffer_.find('\n');
        if (newline == std::string::npos) {
            continue;
        }
        answer = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        break;
    }
    if (answer.rfind("HTTP/", 0) == 0) {
        /* A WebSocket server: it wants a handshake, and it took the first line for one. */
        close();
        const socket_t fd = connect_to(host_, port_);
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

bool Client::handshake() {
    std::uint32_t seed = random_seed();
    unsigned char key_bytes[16];
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
    for (int i = 0; i < 20; ++i) {
        if (!fill(50)) {
            continue;
        }
        const std::size_t end = buffer_.find("\r\n\r\n");
        if (end == std::string::npos) {
            continue;
        }
        answer = buffer_.substr(0, end);
        buffer_.erase(0, end + 4);
        break;
    }
    return answer.rfind("HTTP/1.1 101", 0) == 0;
}

void Client::close() {
    if (socket_ != kNoSocket) {
        close_socket(static_cast<socket_t>(socket_));
        socket_ = kNoSocket;
    }
    connected_ = false;
    buffer_.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (ever_connected_) {
        /* The server was there and is not: what is queued is stale. */
        queue_.clear();
    }
}

void Client::write_all(const char* data, std::size_t length) {
    std::size_t written = 0;
    while (written < length) {
        const int n = ::send(
            static_cast<socket_t>(socket_), data + written, static_cast<int>(length - written), 0
        );
        if (n <= 0) {
            close();
            return;
        }
        written += static_cast<std::size_t>(n);
    }
}

bool Client::fill(int timeout_ms) {
    if (socket_ == kNoSocket) {
        return false;
    }
    if (!wait_readable(static_cast<socket_t>(socket_), timeout_ms)) {
        return false;
    }
    char chunk[1024];
    const int n = ::recv(static_cast<socket_t>(socket_), chunk, sizeof chunk, 0);
    if (n <= 0) {
        close();
        return false;
    }
    buffer_.append(chunk, static_cast<std::size_t>(n));
    return true;
}

bool Client::read_line(std::string& line) {
    const std::size_t newline = buffer_.find('\n');
    if (newline == std::string::npos) {
        return false;
    }
    line = buffer_.substr(0, newline);
    buffer_.erase(0, newline + 1);
    return true;
}

/* A WebSocket frame: the header, the length, and the payload. The server's frames are not
 * masked. */
bool Client::read_frame(std::string& text) {
    if (buffer_.size() < 2) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(buffer_[0]);
    const unsigned char second = static_cast<unsigned char>(buffer_[1]);
    const unsigned opcode = first & 0x0Fu;
    const bool masked = (second & 0x80u) != 0;
    std::size_t length = second & 0x7Fu;
    std::size_t offset = 2;
    if (length == 126) {
        if (buffer_.size() < offset + 2) {
            return false;
        }
        length = (static_cast<unsigned char>(buffer_[offset]) << 8)
                 | static_cast<unsigned char>(buffer_[offset + 1]);
        offset += 2;
    } else if (length == 127) {
        if (buffer_.size() < offset + 8) {
            return false;
        }
        length = 0;
        for (int i = 0; i < 8; ++i) {
            length = (length << 8)
                     | static_cast<unsigned char>(buffer_[offset + static_cast<std::size_t>(i)]);
        }
        offset += 8;
    }
    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (buffer_.size() < offset + 4) {
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            mask[i] = static_cast<unsigned char>(buffer_[offset + static_cast<std::size_t>(i)]);
        }
        offset += 4;
    }
    if (buffer_.size() < offset + length) {
        return false;
    }
    std::string payload = buffer_.substr(offset, length);
    buffer_.erase(0, offset + length);
    if (masked) {
        for (std::size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % 4]);
        }
    }
    if (opcode == 0x8u) {
        /* The server closes the connection. */
        close();
        return false;
    }
    if (opcode == 0x9u) {
        /* A ping: a pong is the answer. */
        std::uint32_t seed = random_seed();
        std::string frame;
        frame.push_back(static_cast<char>(0x8Au));
        frame.push_back(static_cast<char>(0x80u | payload.size()));
        unsigned char key[4];
        random_bytes(seed, key, sizeof key);
        frame.append(reinterpret_cast<const char*>(key), 4);
        for (std::size_t i = 0; i < payload.size(); ++i) {
            frame.push_back(static_cast<char>(static_cast<unsigned char>(payload[i]) ^ key[i % 4]));
        }
        write_all(frame.data(), frame.size());
        return false;
    }
    if (opcode == 0x1u || opcode == 0x0u) {
        text = payload;
        return true;
    }
    return false;
}

void Client::write_frame(const std::string& text) {
    std::uint32_t seed = random_seed();
    unsigned char key[4];
    random_bytes(seed, key, sizeof key);
    std::string frame;
    frame.push_back(static_cast<char>(0x81u)); /* the last frame, text */
    const std::size_t length = text.size();
    if (length < 126) {
        frame.push_back(static_cast<char>(0x80u | length));
    } else if (length <= 0xFFFFu) {
        frame.push_back(static_cast<char>(0x80u | 126u));
        frame.push_back(static_cast<char>((length >> 8) & 0xFFu));
        frame.push_back(static_cast<char>(length & 0xFFu));
    } else {
        frame.push_back(static_cast<char>(0x80u | 127u));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((length >> (i * 8)) & 0xFFu));
        }
    }
    frame.append(reinterpret_cast<const char*>(key), 4);
    for (std::size_t i = 0; i < length; ++i) {
        frame.push_back(static_cast<char>(static_cast<unsigned char>(text[i]) ^ key[i % 4]));
    }
    write_all(frame.data(), frame.size());
}

}  // namespace livesplit
