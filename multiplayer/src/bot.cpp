/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The test bot (bot.hpp): development builds only. POSIX sockets: the end-to-end tests run on
 * Linux and macOS.
 */
#ifdef MULTIPLAYER_BOT

#include "bot.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace mp {
namespace {

constexpr int kTeleportTicks = 10;

void nonblocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

std::string quoted(const std::vector<std::string>& lines) {
    std::string out = "[";

    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += i != 0 ? ",\"" : "\"";

        for (const char c : lines[i]) {
            if (c == '"' || c == '\\') {
                out += '\\';
            }

            if (static_cast<unsigned char>(c) >= 0x20) {
                out += c;
            }
        }

        out += "\"";
    }

    return out + "]";
}

std::string number(float f) {
    char text[32];
    std::snprintf(text, sizeof text, "%.3f", static_cast<double>(f));
    return text;
}

}  // namespace

Bot::Bot(Parts parts) : parts_(std::move(parts)) {
    const char* port = std::getenv("OPENRAC_MP_BOT");

    if (port == nullptr) {
        return;
    }

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_port = htons(static_cast<std::uint16_t>(std::atoi(port)));
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(fd, reinterpret_cast<sockaddr*>(&at), sizeof at) != 0 || listen(fd, 1) != 0) {
        std::fprintf(stderr, "multiplayer: the bot cannot listen on %s\n", port);
        close(fd);
        return;
    }

    nonblocking(fd);
    listen_ = fd;
}

Bot::~Bot() {
    if (client_ >= 0) {
        close(static_cast<int>(client_));
    }

    if (listen_ >= 0) {
        close(static_cast<int>(listen_));
    }
}

void Bot::poll() {
    if (listen_ < 0) {
        return;
    }

    if (teleport_ticks_ > 0) {
        --teleport_ticks_;
        parts_.teleport(at_);
    }

    if (client_ < 0) {
        const int fd = accept(static_cast<int>(listen_), nullptr, nullptr);

        if (fd < 0) {
            return;
        }

        nonblocking(fd);
        client_ = fd;
    }

    char chunk[1024];
    const auto n = recv(static_cast<int>(client_), chunk, sizeof chunk, 0);

    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(static_cast<int>(client_));
        client_ = -1;
        input_.clear();
        return;
    }

    if (n > 0) {
        input_.append(chunk, static_cast<std::size_t>(n));
    }

    for (auto end = input_.find('\n'); end != std::string::npos; end = input_.find('\n')) {
        const std::string answer = run(input_.substr(0, end)) + "\n";
        input_.erase(0, end + 1);
        std::size_t sent = 0;

        while (sent < answer.size()) {
            const auto wrote = send(static_cast<int>(client_), answer.data() + sent, answer.size() - sent, MSG_NOSIGNAL);

            if (wrote > 0) {
                sent += static_cast<std::size_t>(wrote);
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        }
    }
}

void Bot::pad(std::uint8_t* report, int size) {
    if (press_ticks_ <= 0 || size < 6) {
        return;
    }

    --press_ticks_;
    report[0] = static_cast<std::uint8_t>(report[0] & ~(mask_ & 0xFF));
    report[1] = static_cast<std::uint8_t>(report[1] & ~(mask_ >> 8));
    report[4] = static_cast<std::uint8_t>(stick_[0]);
    report[5] = static_cast<std::uint8_t>(stick_[1]);
}

std::string Bot::status() {
    const GameStatus s = parts_.game.status();
    const Session& session = parts_.session;
    Pose hero;
    const bool has_hero = in_level(s.phase) && parts_.game.hero(hero);
    std::ostringstream o;
    o << "{\"phase\":" << s.phase << ",\"level\":" << s.level << ",\"loads\":" << s.loads
      << ",\"scene\":" << s.scene << ",\"id\":" << session.id() << ",\"greeted\":" << session.greeted()
      << ",\"code\":\"" << session.code() << "\",\"is_host\":" << session.is_host()
      << ",\"members\":" << session.members().size() << ",\"all_ready\":" << session.all_ready()
      << ",\"playing\":" << parts_.coop.playing() << ",\"host_level\":" << parts_.coop.host_level()
      << ",\"hold\":" << parts_.coop.hold_input() << ",\"arriving\":" << parts_.coop.arriving()
      << ",\"host_scene\":" << parts_.coop.host_scene() << ",\"figures\":" << parts_.coop.figures()
      << ",\"driven\":" << parts_.world.driven() << ",\"spawned\":" << parts_.world.spawned()
      << ",\"mobys\":" << parts_.world.mobys() << ",\"bolts\":" << parts_.game.bolts(0)
      << ",\"udp\":" << parts_.connection.udp() << ",\"rtt\":" << session.rtt_ms()
      << ",\"joining\":" << parts_.coop.joining() << ",\"synced\":" << parts_.world.synced()
      << ",\"going\":" << parts_.coop.going() << ",\"shielded\":" << parts_.coop.shielded()
      << ",\"wait_kind\":" << static_cast<int>(parts_.coop.wait_kind())
      << ",\"wait_votes\":" << parts_.coop.wait_votes() << ",\"banner\":\"" << parts_.coop.banner(now_ms()) << "\""
      << ",\"shots_sent\":" << parts_.coop.shots_sent() << ",\"shots_fired\":" << parts_.coop.shots_fired()
      << ",\"shots_late\":" << parts_.coop.shots_late() << ",\"tags\":" << parts_.coop.tag_stats().tags
      << ",\"tags_on_picture\":" << parts_.coop.tag_stats().on_picture << ",\"tag_x\":" << number(parts_.coop.tag_stats().x)
      << ",\"tag_y\":" << number(parts_.coop.tag_stats().y);

    if (has_hero) {
        o << ",\"hero\":[" << number(hero.position[0]) << "," << number(hero.position[1]) << ","
          << number(hero.position[2]) << "],\"health\":" << hero.health;
    }

    o << "}";
    return o.str();
}

std::string Bot::mods() {
    const PartyMods& m = parts_.mods;
    const Session& session = parts_.session;
    std::ostringstream o;
    std::vector<std::string> rules;

    for (const PartyRules& r : m.rules()) {
        rules.push_back(r.id + ": " + r.text);
    }

    o << "{\"digest\":\"" << digest_tag(mods_digest(m.own())) << "\",\"heard_host\":" << m.heard_host()
      << ",\"host_silent\":" << m.host_silent() << ",\"solo\":" << m.solo() << ",\"fits\":{";
    bool first = true;

    for (const Member& member : session.members()) {
        o << (first ? "" : ",") << "\"" << member.name << "\":" << static_cast<int>(m.fit(member.id));
        first = false;
    }

    o << "},\"differences\":" << quoted(refusal_lines(m.differences())) << ",\"rules\":" << quoted(rules) << "}";
    return o.str();
}

std::string Bot::dump() {
    std::ostringstream o;
    o << "[";
    bool first = true;

    for (const MobyState& m : parts_.game.mobys()) {
        o << (first ? "" : ",") << "[" << m.slot << "," << m.key << "," << m.oclass << "," << m.flags
          << "," << m.state << "," << number(m.pose.position[0]) << "," << number(m.pose.position[1])
          << "," << number(m.pose.position[2]) << "]";
        first = false;
    }

    o << "]";
    return o.str();
}

std::string Bot::stats() {
    const NetCounts& c = parts_.session.counts();
    std::ostringstream o;
    const auto kinds = [&o](const char* name, const std::array<NetCounts::Kind, 256>& all) {
        o << "\"" << name << "\":{";
        bool first = true;

        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i].messages != 0) {
                o << (first ? "" : ",") << "\"" << i << "\":[" << all[i].messages << "," << all[i].bytes << "]";
                first = false;
            }
        }

        o << "}";
    };
    o << "{";
    kinds("events", c.events_out);
    o << ",";
    kinds("state", c.state_out);
    o << ",";
    kinds("in", c.in);
    o << ",\"data_frames\":" << c.data_frames << ",\"state_datagrams\":" << c.state_datagrams
      << ",\"state_held\":" << c.state_held << ",\"state_tcp\":" << c.state_tcp
      << ",\"state_tcp_held\":" << c.state_tcp_held << ",\"waited\":" << c.waited
      << ",\"queue_most\":" << c.queue_most << "}";
    return o.str();
}

std::string Bot::run(const std::string& line) {
    std::istringstream in(line);
    std::string command;
    in >> command;

    if (command == "connect") {
        std::string host;
        int port = 0;
        in >> host >> port;
        parts_.connection.open(host, port);
    } else if (command == "create") {
        // A save's file to start from (a test's), else a new game.
        std::string path;
        in >> path;
        Bytes save;

        if (!path.empty()) {
            std::ifstream file(path, std::ios::binary);
            save.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        parts_.coop.choose(save, save.empty() ? "New game" : "A save");
        parts_.session.create(parts_.listed());
    } else if (command == "join") {
        std::string code;
        in >> code;
        parts_.session.join(code);
    } else if (command == "ready") {
        parts_.session.toggle_ready();
    } else if (command == "start") {
        return parts_.coop.start() ? "ok" : "error it did not start";
    } else if (command == "leave") {
        parts_.session.leave();
    } else if (command == "status") {
        return status();
    } else if (command == "dump") {
        return dump();
    } else if (command == "stats") {
        return stats();
    } else if (command == "mods") {
        return mods();
    } else if (command == "page") {
        std::string name;
        in >> name;
        return quoted(parts_.page(name));
    } else if (command == "widths") {
        std::string name;
        in >> name;
        std::string out = "[";

        for (const std::string& text : parts_.page(name)) {
            out += (out.size() > 1 ? "," : "") + std::to_string(parts_.width(text));
        }

        return out + "]";
    } else if (command == "teleport") {
        in >> at_[0] >> at_[1] >> at_[2];
        teleport_ticks_ = kTeleportTicks;
        parts_.teleport(at_);
    } else if (command == "equip") {
        int item = 0;
        in >> item;
        parts_.equip(item);
    } else if (command == "press") {
        in >> std::hex >> mask_ >> std::dec >> press_ticks_;
        stick_[0] = stick_[1] = 128;
        in >> stick_[0] >> stick_[1];
    } else if (command == "hit") {
        int key = -1;
        std::uint32_t mask = 0;
        in >> key >> std::hex >> mask;
        const int slot = parts_.game.moby_slot(key);
        Pose hero;

        if (slot < 0 || !parts_.game.hero(hero)) {
            return "error no such moby";
        }

        Hit h;
        h.victim = slot;
        h.attacker = parts_.game.figure_slot(-1);
        h.mask = mask;
        h.damage = 1.0f;
        std::copy(hero.position, hero.position + 3, h.position);
        return parts_.game.hit(h) ? "ok" : "error refused";
    } else if (command == "travel") {
        // As the ship's planet select: the host's departure, then the level's way out.
        int level = -1;
        in >> level;
        parts_.coop.ship_travel(level, parts_.game.status().level);
        parts_.game.travel(level);
    } else if (command == "go") {
        // The pause page's Go to host.
        return parts_.coop.go_to_host(now_ms()) ? "ok" : "error refused";
    } else if (command == "title") {
        // The game's Quit Game.
        return parts_.game.travel(-1) ? "ok" : "error refused";
    } else if (command == "scene") {
        int scene = -1;
        in >> scene;
        return parts_.game.play_scene(scene) ? "ok" : "error refused";
    } else if (command == "bolts") {
        return std::to_string(parts_.game.bolts(0));
    } else if (command == "cosave") {
        CoSave& c = parts_.cosave;
        return "{\"on\":" + std::string(c.on() ? "true" : "false") + ",\"slot\":" + std::to_string(c.slot())
               + ",\"key\":" + std::to_string(c.key()) + ",\"saves\":" + std::to_string(parts_.game.card_saves())
               + ",\"acked\":" + std::to_string(c.acked()) + ",\"sent_to\":" + std::to_string(c.sent_to())
               + "}";
    } else if (command == "saveparty") {
        return parts_.cosave.save_party() ? "ok" : "error refused";
    } else if (command == "gamesave") {
        return parts_.game_save() ? "ok" : "error refused";
    } else if (command == "continue") {
        // Continue as host from the Saves page: co-op slot N, then a party on the server.
        int slot = -1;
        in >> slot;
        const Bytes save = parts_.game.card_read(slot);

        if (save.empty()) {
            return "error no such save";
        }

        parts_.cosave.choose_slot(slot);
        parts_.coop.choose(save, "A save");
        parts_.session.create(parts_.listed());
    } else if (command == "fall") {
        const float out[3] = {1.0f, 1.0f, 100.0f};
        parts_.teleport(out);
    } else if (command != "hello") {
        return "error unknown command";
    }

    return "ok";
}

}  // namespace mp

#endif /* MULTIPLAYER_BOT */
