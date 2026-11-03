// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The Archipelago client (ap_bridge.h): a thread that keeps a WebSocket to the server and runs the
// session (session.hpp) on what comes. The game side's calls only take the lock for a moment.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ap_bridge.h"
#include "ap_ids.h"
#include "session.hpp"
#include "websocket.hpp"

namespace {

// The server's port when the setting names none (Archipelago's own).
constexpr int kDefaultPort = 38281;

// How long the thread waits for the server between its other work, and between tries.
constexpr int kPollMs = 50;
constexpr int kRetrySeconds = 5;

// Where to connect: the host, the port, and the schemes to try in turn.
struct Target {
    std::string host;
    int port = kDefaultPort;
    std::vector<bool> tls;  // each try's TLS or not
};

// "host", "host:port", "ws://host:port" or "wss://host:port" (a trailing "/" ignored). No scheme
// tries wss first, then ws, as Archipelago's own clients do.
Target parse_server(std::string text) {
    Target t;
    // Spaces around it are a typing slip.
    const auto first = text.find_first_not_of(" \t");
    const auto last = text.find_last_not_of(" \t/");
    text = first == std::string::npos ? std::string() : text.substr(first, last - first + 1);

    // The scheme, when there is one.
    if (text.rfind("wss://", 0) == 0) {
        t.tls = {true};
        text.erase(0, 6);
    } else if (text.rfind("ws://", 0) == 0) {
        t.tls = {false};
        text.erase(0, 5);
    } else {
        t.tls = {true, false};
    }

    const auto colon = text.rfind(':');

    // A port after the last colon (not inside an IPv6 address's brackets).
    if (colon != std::string::npos && text.find(']', colon) == std::string::npos) {
        t.port = std::atoi(text.c_str() + colon + 1);
        text.resize(colon);
    }

    // [::1] style: the brackets go.
    if (text.size() >= 2 && text.front() == '[' && text.back() == ']') {
        text = text.substr(1, text.size() - 2);
    }

    t.host = text;
    t.port = t.port > 0 && t.port < 65536 ? t.port : kDefaultPort;
    return t;
}

class Client {
public:
    explicit Client(const ap_config& c)
        : target_(parse_server(c.server != nullptr ? c.server : "")),
          session_(
              {AP_GAME,
               c.slot != nullptr ? c.slot : "",
               c.password != nullptr ? c.password : "",
               c.uuid != nullptr ? c.uuid : ""}
          ),
          book_path_(c.data_dir != nullptr && *c.data_dir != 0 ? std::string(c.data_dir) + "/save-slots.tsv" : "") {
        read_book();
        thread_ = std::thread([this] { run(); });
    }

    ~Client() {
        running_ = false;
        wake_.notify_all();
        thread_.join();
    }

    int state() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (session_.refused()) {
            return AP_STATE_REFUSED;
        }

        return session_.connected() ? AP_STATE_CONNECTED : AP_STATE_CONNECTING;
    }

    // The game side's requests, sent by the thread.
    void check(std::int64_t location) {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.check(location, outbox_);
    }

    void goal() {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.goal(outbox_);
    }

    void save_loaded(const std::int64_t* had, std::size_t n, int card_slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.save_loaded(std::vector<std::int64_t>(had, had + n), card_slot, outbox_);
        write_book();
    }

    void save_written(int card_slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.save_written(card_slot);
        write_book();
    }

    std::size_t items(std::int64_t* out, std::size_t max, std::uint32_t* generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& list = session_.items();
        std::copy_n(list.begin(), std::min(max, list.size()), out);

        if (generation != nullptr) {
            *generation = session_.generation();
        }

        return list.size();
    }

    bool synced() {
        std::lock_guard<std::mutex> lock(mutex_);
        return session_.synced();
    }

    bool message(std::string& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        return session_.next_message(out);
    }

    bool location_item(std::int64_t location, std::int64_t& item, bool& own) {
        std::lock_guard<std::mutex> lock(mutex_);
        return session_.location_item(location, item, own);
    }

private:
    // Connects with each scheme in turn: true once one is open.
    bool connect() {
        std::string error;

        for (const bool tls : target_.tls) {
            if (socket_.open(target_.host, target_.port, tls, error)) {
                return true;
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // The same reason once, not every few seconds.
        if (error != last_error_) {
            session_.add_message("Archipelago: " + error);
            last_error_ = error;
        }

        return false;
    }

    void run() {
        // Until stopped: connect, talk, and after a drop wait and connect again.
        while (running_) {
            if (target_.host.empty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                session_.add_message("Archipelago: no server set (the mod's Server setting)");
                break;
            }

            if (connect()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    session_.on_open();
                    outbox_.clear();
                    last_error_.clear();
                }

                talk();
                std::lock_guard<std::mutex> lock(mutex_);
                session_.on_close();

                // A refusal is final until the settings change (the game's next start).
                if (session_.refused()) {
                    break;
                }

                session_.add_message("Archipelago: connection lost, trying again");
            }

            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::seconds(kRetrySeconds), [this] {
                return !running_.load();
            });
        }

        socket_.close();
    }

    // One connection's life: what comes is fed to the session, what it and the game side give is
    // sent.
    void talk() {
        while (running_ && socket_.is_open()) {
            std::vector<std::string> send;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                send.swap(outbox_);
            }

            for (const std::string& text : send) {
                if (!socket_.send_text(text)) {
                    return;
                }
            }

            std::string text;
            const int got = socket_.receive(text, kPollMs);

            if (got < 0) {
                return;
            }

            if (got > 0) {
                std::lock_guard<std::mutex> lock(mutex_);
                session_.on_message(text, outbox_);
                write_book();

                if (session_.refused()) {
                    socket_.close();
                    return;
                }
            }
        }
    }

    // The book of which room's game each memory card slot holds (session.hpp), one line a slot:
    // "<slot>\t<seed name>\t<slot name>", in the mod's own folder. None without a folder.
    void read_book() {
        std::ifstream in(book_path_);
        std::string line;

        while (!book_path_.empty() && std::getline(in, line)) {
            std::istringstream fields(line);
            std::string slot, seed, name;

            if (std::getline(fields, slot, '\t') && std::getline(fields, seed, '\t')
                && std::getline(fields, name)) {
                session_.book().slots[std::atoi(slot.c_str())] = {seed, name};
            }
        }
    }

    // Under the lock: the book written again when it changed, through a file put in its place.
    void write_book() {
        auto& book = session_.book();

        if (!book.changed || book_path_.empty()) {
            return;
        }

        book.changed = false;
        const std::string temporary = book_path_ + ".new";
        {
            std::ofstream out(temporary, std::ios::trunc);

            for (const auto& [slot, room] : book.slots) {
                out << slot << '\t' << room.first << '\t' << room.second << '\n';
            }
        }
        std::remove(book_path_.c_str());
        std::rename(temporary.c_str(), book_path_.c_str());
    }

    Target target_;
    ap::WebSocket socket_;  // the thread's only
    std::mutex mutex_;      // guards the rest
    std::condition_variable wake_;
    ap::Session session_;
    std::vector<std::string> outbox_;
    std::string last_error_;
    std::atomic<bool> running_{true};
    std::string book_path_;
    std::thread thread_;  // last: started once the rest is made
};

std::unique_ptr<Client> g_client;

}  // namespace

extern "C" {

void ap_client_start(const ap_config* config) {
    g_client.reset();
    g_client = std::make_unique<Client>(*config);
}

void ap_client_stop(void) {
    g_client.reset();
}

int ap_client_state(void) {
    return g_client != nullptr ? g_client->state() : AP_STATE_OFF;
}

void ap_client_check(int64_t location) {
    if (g_client != nullptr) {
        g_client->check(location);
    }
}

void ap_client_goal(void) {
    if (g_client != nullptr) {
        g_client->goal();
    }
}

void ap_client_save_loaded(const int64_t* had, size_t n, int card_slot) {
    if (g_client != nullptr) {
        g_client->save_loaded(had, n, card_slot);
    }
}

void ap_client_save_written(int card_slot) {
    if (g_client != nullptr) {
        g_client->save_written(card_slot);
    }
}

size_t ap_client_items(int64_t* out, size_t max, uint32_t* generation) {
    if (g_client == nullptr) {
        if (generation != nullptr) {
            *generation = 0;
        }

        return 0;
    }

    return g_client->items(out, max, generation);
}

int ap_client_synced(void) {
    return g_client != nullptr && g_client->synced();
}

int ap_client_message(char* out, size_t size) {
    std::string line;

    if (g_client == nullptr || size == 0 || !g_client->message(line)) {
        return 0;
    }

    const std::size_t n = std::min(line.size(), size - 1);
    std::memcpy(out, line.data(), n);
    out[n] = 0;
    return 1;
}

int ap_client_location_item(int64_t location, int64_t* item, int* own) {
    std::int64_t i = -1;
    bool o = false;

    if (g_client == nullptr || !g_client->location_item(location, i, o)) {
        return 0;
    }

    *item = i;
    *own = o ? 1 : 0;
    return 1;
}

}  // extern "C"
