// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The live-splits mod's library as the port loads it, against a host table made here
// (openrac_mod.h, interface 1.0): it reports its id and interface, refuses another game and
// another disc, and takes the level's gameplay hook point. Then, against a LiveSplit server made
// here (a socket that answers what the mod sends it): the run starts at the first planet, every
// planet after it is a split, the level loaded again (Ratchet died) is not, and the run ends when
// the game leaves Veldin.
//
// Run with the library's path: load_test <live_splits.dylib|.so|.dll>.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kNoSocket = INVALID_SOCKET;

void close_socket(socket_t fd) {
    closesocket(fd);
}
#else
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
using socket_t = int;
constexpr socket_t kNoSocket = -1;

void close_socket(socket_t fd) {
    ::close(fd);
}
#endif

#include "openrac/openrac_mod.h"

#if defined(_WIN32)
static void* open_library(const char* path) {
    return reinterpret_cast<void*>(LoadLibraryA(path));
}

static void* symbol(void* lib, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
}
#else
#include <dlfcn.h>

static void* open_library(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void* symbol(void* lib, const char* name) {
    return dlsym(lib, name);
}
#endif

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

std::string g_failed;
std::string g_version = "pal";
std::string g_protocol = "tcp";
int g_level = -1;
int g_port = 0;
std::string g_hook_name;
std::string g_hook_signature;
void* g_hook = nullptr;
openrac_frame_fn g_frame = nullptr;
void* g_frame_user = nullptr;
std::vector<std::string> g_log;
/* The game's memory, as the port hands it: the mod reads its time base from it. */
std::vector<std::uint8_t> g_memory(0x00200000u);

void host_log(openrac_mod*, int, const char* text) {
    g_log.emplace_back(text);
}

void host_fail(openrac_mod*, const char* reason) {
    g_failed = reason;
}

/* The game's own level loader, the hook goes on to. */
std::uint32_t game_load_gameplay(std::int32_t) {
    return 0;
}

int setting_bool(openrac_mod*, const char*) {
    return 0;
}

std::int64_t setting_int(openrac_mod*, const char* key) {
    return std::strcmp(key, "port") == 0 ? g_port : 0;
}

double setting_float(openrac_mod*, const char*) {
    return 0.0;
}

const char* setting_string(openrac_mod*, const char* key) {
    if (std::strcmp(key, "split") == 0) {
        return "reached";
    }
    if (std::strcmp(key, "start") == 0) {
        return "first";
    }
    if (std::strcmp(key, "protocol") == 0) {
        return g_protocol.c_str();
    }
    return "";
}

int host_hook(
    openrac_mod*, const char* name, const char* signature, void* replacement, void** original
) {
    g_hook_name = name;
    g_hook_signature = signature;
    g_hook = replacement;
    *original = reinterpret_cast<void*>(&game_load_gameplay);
    return OPENRAC_HOOK_OK;
}

int host_on_frame(openrac_mod*, openrac_frame_fn fn, void* user) {
    g_frame = fn;
    g_frame_user = user;
    return 0;
}

void* host_function(const char*) {
    return nullptr;
}

void* host_function_at(openrac_gaddr) {
    return nullptr;
}

openrac_gaddr host_relocate(openrac_gaddr a) {
    return a;
}

openrac_gaddr host_relocate_level(int, openrac_gaddr a) {
    return a;
}

int host_loaded_level() {
    return g_level;
}

openrac_gaddr host_stack_push(std::uint32_t size, openrac_gaddr* saved) {
    *saved = 0x001F0000u;
    return 0x001F0000u - size;
}

void host_stack_pop(openrac_gaddr) {}

const openrac_input* host_input() {
    return nullptr;
}

int host_take_input(openrac_mod*, std::uint32_t) {
    return 0;
}

void host_capture_mouse(openrac_mod*, int) {}

openrac_api make_api() {
    openrac_api api{};
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = "rac1";
    api.version = g_version.c_str();
    api.openrac_version = "0.0.0";
    api.platform = "test";
    api.memory = g_memory.data();
    api.log = host_log;
    api.fail = host_fail;
    api.setting_bool = setting_bool;
    api.setting_int = setting_int;
    api.setting_float = setting_float;
    api.setting_string = setting_string;
    api.hook = host_hook;
    api.on_frame = host_on_frame;
    api.on_pad = nullptr;
    api.function = host_function;
    api.function_at = host_function_at;
    api.relocate_data = host_relocate;
    api.relocate_level_data = host_relocate_level;
    api.loaded_level = host_loaded_level;
    api.stack_push = host_stack_push;
    api.stack_pop = host_stack_pop;
    api.input = host_input;
    api.take_input = host_take_input;
    api.capture_mouse = host_capture_mouse;
    return api;
}

/* The LiveSplit server the test makes: a socket that listens on a port of the system's choosing and
 * answers what the mod sends it, over TCP or over a WebSocket. */
class Server {
public:
    /* Whether the server speaks the WebSocket: it then answers a handshake and reads the frames. */
    void websocket(bool on) { websocket_ = on; }

    bool listen() {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ == kNoSocket) {
            return false;
        }
        int yes = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
            return false;
        }
        if (::listen(fd_, 4) != 0) {
            return false;
        }
        socklen_t length = sizeof address;
        if (getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            return false;
        }
        port_ = ntohs(address.sin_port);
        return true;
    }

    int port() const { return port_; }

    /* The socket the client is on, to answer it. */
    socket_t client_fd() const { return client_; }

    /* The connection, waiting for it. */
    bool accept(int timeout_ms) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(fd_, &set);
            timeval tv{0, 50000};
            if (select(static_cast<int>(fd_) + 1, &set, nullptr, nullptr, &tv) > 0) {
                client_ = ::accept(fd_, nullptr, nullptr);
                return client_ != kNoSocket;
            }
        }
        return false;
    }

    /* A line the mod sent, waiting for it. */
    bool line(std::string& out, int timeout_ms) {
        if (websocket_) {
            return frame(out, timeout_ms);
        }
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            const std::size_t newline = buffer_.find('\n');
            if (newline != std::string::npos) {
                out = buffer_.substr(0, newline);
                buffer_.erase(0, newline + 1);
                return true;
            }
            if (!read_more(50)) {
                continue;
            }
        }
        return false;
    }

    /* The handshake the WebSocket server answers, once the request has come. */
    bool handshake(int timeout_ms) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            const std::size_t end = buffer_.find("\r\n\r\n");
            if (end != std::string::npos) {
                buffer_.erase(0, end + 4);
                const std::string answer = "HTTP/1.1 101 Switching Protocols\r\n"
                                           "Upgrade: websocket\r\n"
                                           "Connection: Upgrade\r\n"
                                           "\r\n";
                ::send(client_, answer.data(), static_cast<int>(answer.size()), 0);
                return true;
            }
            if (!read_more(50)) {
                continue;
            }
        }
        return false;
    }

    /* Nothing more comes within the time given. */
    bool quiet(int timeout_ms) {
        std::string line;
        return !this->line(line, timeout_ms);
    }

private:
    /* Reads what the socket has, waiting for it. */
    bool read_more(int timeout_ms) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(client_, &set);
        timeval tv{0, 50000};
        if (select(static_cast<int>(client_) + 1, &set, nullptr, nullptr, &tv) <= 0) {
            return false;
        }
        char chunk[512];
        const int n = ::recv(client_, chunk, sizeof chunk, 0);
        if (n <= 0) {
            return false;
        }
        buffer_.append(chunk, static_cast<std::size_t>(n));
        return true;
    }

    /* A WebSocket frame: the client masks its frames, the server does not. */
    bool frame(std::string& out, int timeout_ms) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (buffer_.size() >= 2) {
                const unsigned char first = static_cast<unsigned char>(buffer_[0]);
                const unsigned char second = static_cast<unsigned char>(buffer_[1]);
                std::size_t length = second & 0x7Fu;
                std::size_t offset = 2;
                if (length == 126) {
                    if (buffer_.size() < 4) {
                        length = 0;
                    } else {
                        length = (static_cast<unsigned char>(buffer_[2]) << 8)
                                 | static_cast<unsigned char>(buffer_[3]);
                        offset = 4;
                    }
                } else if (length == 127) {
                    length = 0;
                }
                if (length != 0 && buffer_.size() >= offset + 4 + length) {
                    unsigned char mask[4];
                    for (int i = 0; i < 4; ++i) {
                        mask[i] = static_cast<unsigned char>(
                            buffer_[offset + static_cast<std::size_t>(i)]
                        );
                    }
                    std::string payload = buffer_.substr(offset + 4, length);
                    buffer_.erase(0, offset + 4 + length);
                    for (std::size_t i = 0; i < payload.size(); ++i) {
                        payload[i] =
                            static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % 4]);
                    }
                    out = payload;
                    return true;
                }
            }
            if (!read_more(50)) {
                continue;
            }
        }
        return false;
    }

    socket_t fd_ = kNoSocket;
    socket_t client_ = kNoSocket;
    int port_ = 0;
    bool websocket_ = false;
    std::string buffer_;
};

/* A tick of the game: the level's gameplay is loaded (the hook the port took), then the frame
 * callback, as the port runs them. */
void tick(bool gameplay_loaded) {
    if (gameplay_loaded && g_hook != nullptr) {
        reinterpret_cast<std::uint32_t (*)(std::int32_t)>(g_hook)(1);
    }
    g_frame(g_frame_user);
}

void run(const char* library) {
    void* lib = open_library(library);
    CHECK(lib != nullptr);
    auto query = reinterpret_cast<openrac_mod_query_fn>(symbol(lib, OPENRAC_MOD_QUERY));
    auto load = reinterpret_cast<openrac_mod_load_fn>(symbol(lib, OPENRAC_MOD_LOAD));
    auto unload = reinterpret_cast<openrac_mod_unload_fn>(symbol(lib, OPENRAC_MOD_UNLOAD));
    CHECK(query != nullptr && load != nullptr);

    openrac_mod_info info{};
    info.size = sizeof info;
    query(&info);
    CHECK(std::strcmp(info.id, "live-splits") == 0);
    CHECK(info.api_major == 1 && info.api_minor == 0);

    Server server;
    CHECK(server.listen());
    g_port = server.port();

    // Another game is refused.
    {
        openrac_api api = make_api();
        api.game = "rac2";
        g_failed.clear();
        CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) != 0 && !g_failed.empty());
    }
    // A disc the mod does not know is refused.
    {
        g_version = "ntsc-pro";
        openrac_api api = make_api();
        g_failed.clear();
        CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) != 0 && !g_failed.empty());
        g_version = "pal";
    }

    // The mod itself: it takes the level's gameplay hook point and its frame callback.
    g_failed.clear();
    g_hook_name.clear();
    g_hook_signature.clear();
    openrac_api api = make_api();
    CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) == 0);
    CHECK(g_failed.empty());
    CHECK(g_hook_name == "rac1.level.load_gameplay");
    CHECK(g_hook_signature == "u32(i32)");
    CHECK(g_frame != nullptr);

    // The mod connects to the server of the test.
    CHECK(server.accept(5000));

    // The first planet: the run begins.
    g_level = 0;
    tick(true);
    std::string line;
    CHECK(server.line(line, 5000) && line == "reset");
    CHECK(server.line(line, 5000) && line == "starttimer");

    // The next planet: a split.
    g_level = 1;
    tick(true);
    CHECK(server.line(line, 5000) && line == "split");

    // The same planet again (Ratchet died and the level was loaded again): nothing.
    g_level = 1;
    tick(true);
    CHECK(server.quiet(300));

    // The last planet: a split, and the run ends when the game leaves it.
    g_level = 18;
    tick(true);
    CHECK(server.line(line, 5000) && line == "split");
    g_level = -1;
    tick(false);
    CHECK(server.line(line, 5000) && line == "split");
    CHECK(server.quiet(300));

    // The mod's log names the connection.
    bool named = false;
    for (const std::string& text : g_log) {
        if (text.find("connected") != std::string::npos) {
            named = true;
        }
    }
    CHECK(named);

    if (unload != nullptr) {
        unload();
    }

    // The same over a WebSocket: the handshake, then the commands in frames.
    Server socket_server;
    socket_server.websocket(true);
    CHECK(socket_server.listen());
    g_port = socket_server.port();
    g_protocol = "websocket";
    g_level = -1;
    g_failed.clear();
    openrac_api other = make_api();
    CHECK(load(&other, reinterpret_cast<openrac_mod*>(1)) == 0);
    CHECK(g_failed.empty());
    CHECK(socket_server.accept(5000));
    CHECK(socket_server.handshake(5000));

    g_level = 0;
    tick(true);
    CHECK(socket_server.line(line, 5000) && line == "reset");
    CHECK(socket_server.line(line, 5000) && line == "starttimer");
    g_level = 2;
    tick(true);
    CHECK(socket_server.line(line, 5000) && line == "split");

    if (unload != nullptr) {
        unload();
    }

    // And the protocol that asks the server which one it is: the mod pings it, and the TCP server
    // answers "pong".
    Server asked;
    CHECK(asked.listen());
    g_port = asked.port();
    g_protocol = "auto";
    g_level = -1;
    g_failed.clear();
    openrac_api third = make_api();
    CHECK(load(&third, reinterpret_cast<openrac_mod*>(1)) == 0);
    CHECK(g_failed.empty());
    CHECK(asked.accept(5000));
    CHECK(asked.line(line, 5000) && line == "ping");
    const std::string pong = "pong\n";
    ::send(asked.client_fd(), pong.data(), static_cast<int>(pong.size()), 0);
    g_level = 0;
    tick(true);
    CHECK(asked.line(line, 5000) && line == "reset");
    CHECK(asked.line(line, 5000) && line == "starttimer");

    if (unload != nullptr) {
        unload();
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: load_test <library>\n");
        return 2;
    }
    run(argv[1]);
    std::puts("live splits load: all passed");
    return 0;
}
