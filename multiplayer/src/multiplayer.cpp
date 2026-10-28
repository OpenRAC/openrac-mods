/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: online multiplayer (README.md): a Multiplayer item in the title's main menu,
 * under Load Game, whose pages are the game's own menus (mod interface 1.7): they connect to the
 * OpenRAC server, show the player's name (the `name` setting, else the launcher's,
 * OPENRAC_PLAYER_NAME), list the games others host, create one from a save (or a new game), or
 * join one by its code, then show the lobby: the code, the save, the members with the host marked,
 * Ready or Start, and Leave. Direct connect plays without a server: this game hosts
 * (connection.hpp's embedded host), or joins another's by its address. The host's Start starts the
 * game for everyone, and each game shows the others' Ratchets in its level (coop.hpp, on the play
 * calls of interface 1.8).
 *
 * The pages, drawn by the game (its frames, list, font, prompt box and sounds):
 *   Home    Browse games (the parties others host), Create match (a party of the player's own,
 *           from a save), Join by code and Direct connect (the next pages), and the player's name;
 *   Browse  Refresh, then a row per party with room (its host, its save, how many are in it);
 *           Cross on one joins it; the list is asked again every few seconds;
 *   Saves   New game and the memory card's saves (their planet, gold bolts and time): the one
 *           chosen is what the party plays (Create match, and Host directly);
 *   Code    a row per character of the code, Left and Right step it through the server's code
 *           alphabet, then Join;
 *   Direct  Host directly and Join by address; Join by address's rows (the address's four octets
 *           and the port, direct.hpp's rings) and Join take the page's place;
 *   Lobby   Ready (a member) or Start (the host), Leave, and the party's members; a direct one
 *           says where it is hosted instead of a code. Leave and Triangle ask first, in the
 *           game's prompt box. Leaving a direct party goes back to the server.
 * Cross chooses, Triangle goes back a page (from Home, to the game's menu). The footer says what
 * the connection is doing. The connection (connection.cpp) is opened the first time Home shows and
 * lasts until the game ends, reconnecting when the server goes; the session follows it every tick.
 *
 * Settings (openrac-mod.json):
 *   server  (string, default localhost:7770)  The OpenRAC server, host:port.
 *   name    (string, default empty)  The player's name (3 to 16 letters, digits, underscores);
 *           empty or invalid: the launcher's Multiplayer name.
 */
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "connection.hpp"
#include "coop.hpp"
#include "direct.hpp"
#include "menu.hpp"
#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "world.hpp"

namespace {

/* The version HELLO gives the server: the mod's (at most 32 characters). */
constexpr const char* kVersion = "rac1-multiplayer/0.6.0";

/* The question's tag: leaving the party (or hosting it). */
constexpr std::uint32_t kAskLeave = 1;

/* Home's buttons. */
constexpr int kBrowseItem = 0;
constexpr int kCreateItem = 1;
constexpr int kCodeItem = 2;
constexpr int kDirectItem = 3;

/* The Lobby's buttons, then its lines; the Code page's characters, then Join. */
constexpr int kReadyItem = 0;
constexpr int kLeaveItem = 1;
constexpr int kJoinItem = static_cast<int>(mp::kCodeLength);

/* The Direct page's buttons; Join by address's rows: four octets, the port by one and by a
 * hundred, then Join. */
constexpr int kHostItem = 0;
constexpr int kByAddressItem = 1;
constexpr int kPortItem = 4;
constexpr int kPortCoarseItem = 5;
constexpr int kJoinAddressItem = 6;

/* Browse's Refresh, its party rows after it (as many as fit), and how often the list is asked
 * again while it shows. */
constexpr int kRefreshItem = 0;
constexpr std::size_t kBrowseRows = OPENRAC_UI_ITEMS_MAX - 2;
constexpr std::uint64_t kListMs = 3000;

/* The Saves page's New game, the card's slots after it; what the save chosen is for. */
constexpr int kNewGameItem = 0;
constexpr int kSaveSlots = 5;

enum class SavesFor {
    kCreate,
    kHost
};

/* What the Direct page shows: its two buttons, or Join by address's rows. */
enum class DirectView {
    kMenu,
    kJoin
};

// ---- The game, for the co-op (coop.hpp's Game, on interface 1.8's play calls) ----

class ModGame : public mp::Game {
public:
    explicit ModGame(openrac::Mod& mod) : mod_(mod) {}

    mp::GameStatus status() override {
        const openrac_play_status s = mod_.play_status();
        return {s.phase, s.level, s.loads};
    }

    bool start(int level, const mp::Bytes& save) override {
        return mod_.play_start(level, save) == OPENRAC_HOOK_OK;
    }

    bool travel(int level) override { return mod_.play_travel(level) == OPENRAC_HOOK_OK; }

    mp::Bytes capture() override { return mod_.save_capture(); }

    bool hero(mp::Pose& out) override {
        openrac_pose p{};

        if (!mod_.hero_pose(p)) {
            return false;
        }

        out = from_sdk(p);
        openrac_held held{};

        // A port without the pieces: the pose wears nothing.
        if (mod_.hero_held(held)) {
            held_from_sdk(held, out);
        }

        return true;
    }

    int figure_create() override { return mod_.figure_create(); }

    bool figure_pose(int id, const mp::Pose& pose) override {
        if (!mod_.figure_pose(id, to_sdk(pose))) {
            return false;
        }

        if (mod_.has_held()) {
            mod_.figure_held(id, held_to_sdk(pose));
        }

        return true;
    }

    std::vector<mp::Shot> hero_shots() override {
        std::vector<mp::Shot> out;

        for (const openrac_shot& s : mod_.hero_shots()) {
            mp::Shot shot;
            shot.item = s.item;
            std::copy(s.position, s.position + 3, shot.position);
            std::copy(s.velocity, s.velocity + 3, shot.velocity);
            std::copy(s.aim, s.aim + 4, shot.aim);
            out.push_back(shot);
        }

        return out;
    }

    bool figure_shot(int id, const mp::Shot& shot) override {
        openrac_shot s{};
        s.size = sizeof s;
        s.item = shot.item;
        std::copy(shot.position, shot.position + 3, s.position);
        std::copy(shot.velocity, shot.velocity + 3, s.velocity);
        std::copy(shot.aim, shot.aim + 4, s.aim);
        return mod_.figure_shot(id, s);
    }

    void figure_remove(int id) override { mod_.figure_remove(id); }

    std::vector<mp::MobyState> mobys() override {
        std::vector<mp::MobyState> out;

        for (const openrac_moby& m : mod_.moby_list()) {
            mp::MobyState s;
            s.slot = m.slot;
            s.key = m.key;
            s.oclass = m.oclass;
            s.flags = m.flags;
            std::copy(m.position, m.position + 3, s.pose.position);
            std::copy(m.rotation, m.rotation + 3, s.pose.rotation);
            s.pose.scale = m.scale;
            s.pose.seq = m.seq;
            s.pose.frame = m.frame;
            s.pose.next_frame = m.next_frame;
            s.pose.anim_time = m.anim_time;
            s.pose.anim_speed = m.anim_speed;
            out.push_back(s);
        }

        return out;
    }

    int moby_slot(int key) override { return mod_.moby_slot(key); }

    bool moby_drive(int slot, bool on) override { return mod_.moby_drive(slot, on); }

    bool moby_pose(int slot, const mp::Pose& pose) override { return mod_.moby_pose(slot, moby_of(pose)); }

    bool moby_kill(int slot) override { return mod_.moby_kill(slot); }

    int moby_spawn(int oclass, const mp::Pose& pose) override {
        return mod_.moby_spawn(oclass, moby_of(pose));
    }

    int figure_slot(int id) override { return mod_.figure_slot(id); }

    bool stand_in(const float* position, int figure) override {
        return mod_.hero_stand_in(position, figure);
    }

    bool hit(const mp::Hit& h) override {
        openrac_hit out{};
        out.victim = h.victim;
        out.attacker = h.attacker;
        out.mask = h.mask;
        out.damage = h.damage;
        std::copy(h.position, h.position + 3, out.position);
        std::copy(h.velocity, h.velocity + 3, out.velocity);
        return mod_.moby_hit(out);
    }

    void eyes(const std::vector<float>& xyz) override { mod_.play_eyes(xyz); }

    int bolts(int add) override { return mod_.bolts(add); }

    void overlay_pause(bool on) override { mod_.play_overlay_pause(on); }

    void skip() override { mod_.play_skip(); }

    static openrac_moby moby_of(const mp::Pose& p) {
        openrac_moby out{};
        out.size = sizeof out;
        std::copy(p.position, p.position + 3, out.position);
        std::copy(p.rotation, p.rotation + 3, out.rotation);
        out.scale = p.scale;
        out.seq = p.seq;
        out.frame = p.frame;
        out.next_frame = p.next_frame;
        out.anim_time = p.anim_time;
        out.anim_speed = p.anim_speed;
        return out;
    }

    /* The two poses are the same fields: one after its size, the other alone. */
    static mp::Pose from_sdk(const openrac_pose& p) {
        mp::Pose out;
        out.level = p.level;
        std::copy(p.position, p.position + 3, out.position);
        std::copy(p.rotation, p.rotation + 3, out.rotation);
        out.scale = p.scale;
        std::copy(p.velocity, p.velocity + 3, out.velocity);
        out.state = p.state;
        out.health = p.health;
        out.item = p.item;
        out.seq = p.seq;
        out.frame = p.frame;
        out.next_frame = p.next_frame;
        out.anim_time = p.anim_time;
        out.anim_speed = p.anim_speed;
        out.flags = p.flags;
        return out;
    }

    /* The pieces: the same fields, the SDK's in openrac_held. */
    static void held_from_sdk(const openrac_held& h, mp::Pose& out) {
        const auto count = static_cast<std::size_t>(
            std::clamp<std::int32_t>(h.count, 0, static_cast<std::int32_t>(mp::kHeldMax))
        );
        out.held_count = static_cast<std::uint32_t>(count);

        for (std::size_t i = 0; i < count; ++i) {
            const openrac_held_piece& s = h.pieces[i];
            mp::HeldPiece& d = out.held[i];
            d.slot = s.slot;
            d.part = s.part;
            d.item = s.item;
            d.oclass = s.oclass;
            d.joint = s.joint;
            d.flags = s.flags;
            std::copy(s.position, s.position + 3, d.position);
            std::copy(s.rotation, s.rotation + 3, d.rotation);
            d.seq = s.seq;
            d.frame = s.frame;
            d.next_frame = s.next_frame;
            d.anim_time = s.anim_time;
            d.anim_speed = s.anim_speed;
        }
    }

    static openrac_held held_to_sdk(const mp::Pose& p) {
        openrac_held h{};
        h.size = sizeof h;
        const std::size_t count = std::min<std::size_t>(p.held_count, OPENRAC_HELD_MAX);
        h.count = static_cast<std::int32_t>(count);

        for (std::size_t i = 0; i < count; ++i) {
            const mp::HeldPiece& s = p.held[i];
            openrac_held_piece& d = h.pieces[i];
            d.slot = s.slot;
            d.part = s.part;
            d.item = s.item;
            d.oclass = s.oclass;
            d.joint = s.joint;
            d.flags = s.flags;
            std::copy(s.position, s.position + 3, d.position);
            std::copy(s.rotation, s.rotation + 3, d.rotation);
            d.seq = s.seq;
            d.frame = s.frame;
            d.next_frame = s.next_frame;
            d.anim_time = s.anim_time;
            d.anim_speed = s.anim_speed;
        }

        return h;
    }

    static openrac_pose to_sdk(const mp::Pose& p) {
        openrac_pose out{};
        out.size = sizeof out;
        out.level = p.level;
        std::copy(p.position, p.position + 3, out.position);
        std::copy(p.rotation, p.rotation + 3, out.rotation);
        out.scale = p.scale;
        std::copy(p.velocity, p.velocity + 3, out.velocity);
        out.state = p.state;
        out.health = p.health;
        out.item = p.item;
        out.seq = p.seq;
        out.frame = p.frame;
        out.next_frame = p.next_frame;
        out.anim_time = p.anim_time;
        out.anim_speed = p.anim_speed;
        out.flags = p.flags;
        return out;
    }

private:
    openrac::Mod& mod_;
};

// ---- State ----

struct State {
    std::string host;
    int port = mp::kDefaultPort;
    std::unique_ptr<mp::Connection> connection;
    std::unique_ptr<mp::Session> session;
    std::unique_ptr<ModGame> game;
    std::unique_ptr<mp::Coop> coop;
    std::unique_ptr<mp::World> world;
    openrac::Mod* mod = nullptr;
    std::uint32_t generation = 0;  // the connection's, as the session last saw it
    openrac::UiPage home, code_page, direct_page, lobby, browse, saves;
    std::uint64_t listed_ms = 0;   // when Browse last asked for the list
    bool home_greeted = false;     // whether Home last saw the server's hello
    std::uint64_t frame_ms = 0;    // when the game last ticked (on_frame)
    bool start_held = false;       // Start as the pad last reported it
    SavesFor saves_for = SavesFor::kCreate;
    std::vector<int> code = std::vector<int>(mp::kCodeLength, 0);  // indices into kCodeAlphabet
    std::string notice;  // the last refusal or news, until the next action
    std::string banner;  // the line shown over the game (play_notice)
    DirectView view = DirectView::kMenu;
    mp::DirectAddress address;  // Join by address's, the last joined kept for the game
    mp::Acceleration acceleration;
    std::string direct_error;  // why Host directly or Join failed, on the Direct page
    openrac::UiPage pause_page;  // the pause menu's Multiplayer page (Options, in a level)
    int pause_leave = -1;        // its Leave party item, -1 out of a party
    std::uint64_t link_ms = 0;   // when the player's link was last reported to the party
};

std::unique_ptr<State> g;

/* The player's name: the mod's `name` setting, else the launcher's (OPENRAC_PLAYER_NAME), else
 * one of the mod's own (neither valid, or started without the launcher). */
std::string player_name(const std::string& setting) {
    const char* launcher = std::getenv("OPENRAC_PLAYER_NAME");
    const std::string name = mp::choose_name(setting, launcher != nullptr ? launcher : "");

    // Neither is a valid name: one of the mod's own.
    if (!name.empty()) {
        return name;
    }

    return "Player" + std::to_string(mp::now_ms() % 1000);
}

/* The status line: the connection, or the last notice (menu.hpp). */
std::string status() {
    mp::StatusInput in;
    in.mode = g->connection->mode();
    in.state = g->connection->state();
    in.greeted = g->session->greeted();
    in.waiting = g->session->waiting();
    in.notice = g->notice;
    in.address = g->connection->address();

    if (in.mode == mp::Connection::Mode::kHosting) {
        const mp::HostingStatus h = g->connection->hosting();
        in.hosting_port = h.port;
        in.hosting_udp = h.udp;
    }

    return mp::status_text(in);
}

/* Brings the session up to date with the connection: hello on a new connection, the server's
 * messages, the last notice. Once a game tick. */
void poll() {
    const std::uint32_t generation = g->connection->generation();
    const bool connected = g->connection->state() == mp::Connection::State::kConnected;

    // A new connection: a new session on the server.
    if (connected && generation != g->generation) {
        g->generation = generation;
        g->session->connected();
    } else if (!connected && g->session->greeted()) {
        // The connection went: so did the party.
        g->session->disconnected();
    }

    const bool was_in = g->session->in_party();

    for (const mp::Message& m : g->connection->receive()) {
        g->session->handle(m);
    }

    // Into a party or out of one: a line in the log.
    if (g->session->in_party() != was_in) {
        const mp::Session& s = *g->session;
        g->mod->info(
            s.in_party() ? "multiplayer: in party " + s.code() + (s.is_host() ? " as its host" : "")
                               + " (" + std::to_string(s.members().size()) + " players)"
                         : "multiplayer: out of the party"
        );
    }

    // A refusal or news replaces the last.
    if (std::string notice = g->session->take_notice(); !notice.empty()) {
        g->notice = notice;
    }

    if (std::string notice = g->coop->take_notice(); !notice.empty()) {
        g->notice = notice;
    }
}

/* The code as the server reads it, a character per slot. */
std::string code_text() {
    std::string code;

    for (const int i : g->code) {
        code += mp::kCodeAlphabet[i];
    }

    return code;
}

// ---- The pages, in the game's own menus ----

/* Home: Find match, Create match, Join by code (usable once the server said hello), Direct
 * connect (always: it needs no server), the name. */
void show_home() {
    const bool usable =
        mp::server_items_enabled(g->connection->mode(), g->session->greeted());
    openrac::UiContent c;
    c.title = "Multiplayer";
    c.items = {
        openrac::UiItem::button("Browse games", usable),
        openrac::UiItem::button("Create match", usable),
        openrac::UiItem::button("Join by code", usable),
        openrac::UiItem::button("Direct connect"),
        openrac::UiItem::text("Playing as " + g->session->name()),
    };
    c.footer = {status(), mp::kSelectBackHints};
    g->home.set(c);
}

/* Code: a choice of the code alphabet per character, then Join. */
void show_code() {
    const std::string alphabet = mp::kCodeAlphabet;
    std::vector<std::string> letters;

    for (const char c : alphabet) {
        letters.emplace_back(1, c);
    }

    openrac::UiContent c;
    c.title = "Party code " + code_text();

    for (std::size_t i = 0; i < mp::kCodeLength; ++i) {
        c.items.push_back(
            openrac::UiItem::choice_of(
                "Character " + std::to_string(i + 1),
                letters,
                static_cast<std::uint32_t>(g->code[i])
            )
        );
    }

    c.items.push_back(openrac::UiItem::button("Join", g->session->greeted()));
    c.footer = {status(), mp::kCodeHints};
    g->code_page.set(c);
}

/* A ring row (direct.hpp): its window of values around `value`, the current one in the middle. */
openrac::UiItem ring_row(const std::string& label, int value, const mp::Ring& ring) {
    return openrac::UiItem::choice_of(
        label, mp::ring_choices(value, ring), static_cast<std::uint32_t>(mp::kRingCentre)
    );
}

/* Direct: Host directly and Join by address; or, in Join by address's place, the address's rows
 * and Join. The footer: the connection, the last failure, the buttons. */
void show_direct() {
    openrac::UiContent c;
    std::string hints;

    if (g->view == DirectView::kMenu) {
        c.title = "Direct connect";
        c.items = {
            openrac::UiItem::button("Host directly"),
            openrac::UiItem::button("Join by address"),
            openrac::UiItem::text(
                "No server: port " + std::to_string(mp::kDefaultPort) + ", TCP and UDP"
            ),
        };
        hints = mp::kSelectBackHints;
    } else {
        const mp::DirectAddress& a = g->address;
        c.title = "Join " + a.text();

        for (std::size_t i = 0; i < a.octets.size(); ++i) {
            c.items.push_back(
                ring_row("Address part " + std::to_string(i + 1), a.octets[i], mp::kOctetRing)
            );
        }

        c.items.push_back(ring_row("Port", a.port, mp::kPortRing));
        c.items.push_back(ring_row("Port, by 100", a.port, mp::kPortCoarseRing));
        c.items.push_back(openrac::UiItem::button("Join"));
        hints = mp::kJoinHints;
    }

    // Before a host or a join, the server's status says nothing of Direct connect.
    const mp::Connection::Mode mode = g->connection->mode();
    const bool linked =
        mode == mp::Connection::Mode::kDirect || mode == mp::Connection::Mode::kHosting;
    c.footer = {linked ? status() : "Plays without the server"};

    if (!g->direct_error.empty()) {
        c.footer.push_back(g->direct_error);
    }

    c.footer.push_back(hints);
    g->direct_page.set(c);
}

/* Lobby: Ready or Start, Leave, then the members (the host marked, the ready ones, the player). A
 * direct party's title says where it is: the host's own addresses (all that fit, the first in the
 * title), or the address joined. */
void show_lobby() {
    const mp::Session& s = *g->session;
    openrac::UiContent c;
    // The host starts from the title once; a member says it is ready until then, and once the
    // game is on, back in the title, joins it again.
    const bool title = g->game->status().phase == mp::kPhaseFrontEnd;
    const bool rejoin = !s.is_host() && g->coop->playing() && title;
    const std::string first = s.is_host()         ? "Start"
                              : rejoin            ? "Join the game"
                              : s.ready(s.id())   ? "Not ready"
                                                  : "Ready";
    c.items = {
        openrac::UiItem::button(
            first, rejoin ? !g->coop->receiving() : !s.started() && (!s.is_host() || title)
        ),
        openrac::UiItem::button("Leave"),
    };

    // Where the party is: its code, or for a direct one, where it is hosted.
    if (!s.direct()) {
        c.title = "Party " + s.code();
    } else if (g->connection->mode() == mp::Connection::Mode::kHosting) {
        const mp::HostingStatus h = g->connection->hosting();
        // The room left past the buttons, the members and the count line, plus the title.
        const std::size_t used = c.items.size() + s.members().size() + 1;
        const std::size_t room =
            1 + (OPENRAC_UI_ITEMS_MAX > used ? OPENRAC_UI_ITEMS_MAX - used : 0);
        const std::vector<std::string> lines = mp::hosting_lines(h.addresses, h.port, room);
        c.title = lines[0];

        for (std::size_t i = 1; i < lines.size(); ++i) {
            c.items.push_back(openrac::UiItem::text(lines[i]));
        }
    } else {
        c.title = "Party at " + g->connection->address();
    }

    // What the party plays: the host's save (or a new game), as its INFO said.
    const std::string& playing = s.is_host() ? g->coop->description() : s.description();

    if (!playing.empty()) {
        c.items.push_back(openrac::UiItem::text("Playing: " + playing));
    }

    if (g->coop->receiving()) {
        c.items.push_back(openrac::UiItem::text("Receiving the host's game..."));
    }

    for (const mp::Member& m : s.members()) {
        const bool host = m.id == s.host();
        std::string line = m.name + (host ? " (host)" : s.ready(m.id) ? " (ready)" : "");
        line += m.id == s.id() ? " - you" : "";
        c.items.push_back(openrac::UiItem::text(line));
    }

    const std::string ping = s.has_clock() ? ", ping " + std::to_string(s.rtt_ms()) + " ms" : "";
    c.items.push_back(
        openrac::UiItem::text(
            std::to_string(s.members().size()) + " of " + std::to_string(mp::kMaxPartySize)
            + " players" + ping
        )
    );
    c.footer = {status(), mp::kSelectLeaveHints};
    g->lobby.set(c);
}

/* The pause menu's Multiplayer page (Options, in a level; menu.hpp's party_lines): the party's code
 * (or where a direct one is), the host's level, each member with its round trip and UDP or TCP
 * (the host marked), and Leave party; out of a party, that it is in none. */
void show_pause() {
    const mp::Session& s = *g->session;
    mp::PartyPageInput in;
    in.in_party = s.in_party();
    in.direct = s.direct();
    in.code = s.code();

    if (g->connection->mode() == mp::Connection::Mode::kHosting) {
        in.where = "port " + std::to_string(g->connection->hosting().port);
    } else {
        in.where = g->connection->address();
    }

    // The host's level: the host's own, a member's as the host's LEVEL said.
    const mp::GameStatus status = g->game->status();
    const int level = s.is_host() ? (mp::in_level(status.phase) ? status.level : -1)
                                  : g->coop->host_level();
    in.host_level = level >= 0 ? g->mod->level_name(level) : "";

    for (const mp::Member& m : s.members()) {
        const mp::Link link = s.link(m.id);
        in.members.push_back({m.name, m.id == s.host(), m.id == s.id(), link.ping_ms, link.udp});
    }

    openrac::UiContent c;
    c.title = mp::kPartyPageTitle;
    g->pause_leave = -1;

    for (const mp::PartyLine& line : mp::party_lines(in)) {
        switch (line.kind) {
            case mp::PartyLine::Kind::kInfo:
                c.items.push_back(openrac::UiItem::choice_of(line.label, {line.value}, 0));
                break;
            case mp::PartyLine::Kind::kLeave:
                g->pause_leave = static_cast<int>(c.items.size());
                c.items.push_back(openrac::UiItem::button(line.label));
                break;
            case mp::PartyLine::Kind::kText:
                c.items.push_back(openrac::UiItem::text(line.label));
                break;
        }
    }

    g->pause_page.set(c);
}

/* Leaves the party (Lobby's and the pause page's Leave): a host of a direct party stops hosting
 * (its clients get END); anyone else leaves. */
void leave_party() {
    g->notice.clear();

    if (g->connection->mode() == mp::Connection::Mode::kHosting) {
        g->connection->stop_hosting();
        poll();
    } else {
        g->session->leave();
    }
}

/* The pause page's events: Leave party asks first; the page follows the party each tick. */
void on_pause(const openrac::UiPage& page, const openrac::UiEvent& e) {
    const mp::Session& s = *g->session;

    if (e.is(OPENRAC_UI_CHOOSE) && e.item() == g->pause_leave && g->pause_leave >= 0) {
        const bool hosting = g->connection->mode() == mp::Connection::Mode::kHosting;
        page.confirm(mp::pause_leave_question(hosting, s.is_host()), kAskLeave);
    } else if (e.is(OPENRAC_UI_ANSWER) && e.tag() == kAskLeave && e.value() == 1 && s.in_party()) {
        g->mod->info("multiplayer: left the party from the pause menu");
        leave_party();
    }

    show_pause();
}

/* Back to the configured server, from a direct link or none (a host stopped, a join given up), so
 * Find, Create and Join by code work again. Nothing while linked to the server already. */
void back_to_server() {
    const mp::Connection::Mode mode = g->connection->mode();

    if (mode == mp::Connection::Mode::kNone) {
        g->connection->open(g->host, g->port);
    } else if (mode != mp::Connection::Mode::kServer) {
        g->connection->connect_server(g->host, g->port);
        poll();
    }
}

/* Stops the server link and its reconnecting (a hosted or direct party is the only link then),
 * nothing when hosting already. Home links to the server again when it shows. */
void stop_server_loop() {
    if (mp::server_loop_wanted(g->connection->mode())
        || g->connection->mode() == mp::Connection::Mode::kDirect) {
        g->connection->close();
        poll();
    }
}

/* Home's events: the connection opens the first time it shows (and goes back to the server after
 * Direct connect); in a party, the lobby. */
void on_home(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Session& s = *g->session;

    // Showing: linked to the server, opened the first time; it stays for the rest of the game.
    if (e.is(OPENRAC_UI_SHOW)) {
        back_to_server();
        g->notice.clear();
    } else if (e.is(OPENRAC_UI_CHOOSE)) {
        g->notice.clear();

        // Browse games lists the parties; Create match asks for a save first; Join by code asks
        // for a code, Direct connect for a host or an address.
        if (e.item() == kBrowseItem) {
            g->browse.open();
        } else if (e.item() == kCreateItem) {
            g->saves_for = SavesFor::kCreate;
            g->saves.open();
        } else if (e.item() == kCodeItem) {
            g->code_page.open();
        } else if (e.item() == kDirectItem) {
            // Direct connect needs no server: its retries stop here, and resume at Home.
            stop_server_loop();
            g->view = DirectView::kMenu;
            g->direct_error.clear();
            g->direct_page.open();
        }
    }

    // The server's hello came while Home showed: the cursor, left on Direct connect while the rest
    // was dim, goes up to Browse games.
    if (s.greeted() && !g->home_greeted && page.cursor() == kDirectItem) {
        page.set_cursor(kBrowseItem);
    }

    g->home_greeted = s.greeted();

    // In a party (found, made, or kept from before): the lobby.
    if (s.in_party()) {
        g->lobby.open();
    }

    show_home();
}

/* Code's events: a character stepped, Join; once in the party, the lobby in this page's place. */
void on_code(const openrac::UiPage& page, const openrac::UiEvent& e) {
    // A character's choice is its index in the alphabet.
    if (e.is(OPENRAC_UI_CHANGE) && e.item() >= 0 && e.item() < kJoinItem) {
        g->code[static_cast<std::size_t>(e.item())] = e.value();
    } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kJoinItem) {
        g->notice.clear();
        g->session->join(code_text());
    }

    // Joined: the lobby.
    if (g->session->in_party()) {
        g->lobby.open();
    }

    show_code();
    (void)page;
}

/* Join by address's row `item` changed to `choice`: the octet or the port it steps. */
void step_address(int item, int choice) {
    mp::DirectAddress& a = g->address;
    const int times = g->acceleration.times(item, mp::now_ms());

    if (item >= 0 && item < static_cast<int>(a.octets.size())) {
        int& octet = a.octets[static_cast<std::size_t>(item)];
        octet = mp::ring_step(octet, choice, mp::kOctetRing, times);
    } else if (item == kPortItem) {
        a.port = mp::ring_step(a.port, choice, mp::kPortRing, times);
    } else if (item == kPortCoarseItem) {
        a.port = mp::ring_step(a.port, choice, mp::kPortCoarseRing, times);
    }
}

/* The Direct page's view changes in place, with the game's page-change sound, the cursor on
 * `item`. */
void switch_view(const openrac::UiPage& page, DirectView view, int item) {
    g->view = view;
    g->direct_error.clear();
    g->acceleration.reset();
    show_direct();
    page.set_cursor(item);
    page.sound(OPENRAC_UI_SOUND_PAGE);
}

/* Direct's events: Host directly hosts at once; Join by address shows the address's rows, whose
 * Join connects; once in the party (the hello answered), the lobby in this page's place. Triangle
 * goes from the rows back to the two buttons (giving up a join that is connecting), and from the
 * buttons back to Home. */
void on_direct(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Connection& connection = *g->connection;

    // This page never has the server: showing it (also back from a party) stops its retries.
    if (e.is(OPENRAC_UI_SHOW)) {
        stop_server_loop();
    }

    if (e.is(OPENRAC_UI_BACK)) {
        // From the rows: the two buttons again, and no join pending.
        if (g->view == DirectView::kJoin) {
            // Giving up a join: no link, and no server retries on this page.
            stop_server_loop();

            switch_view(page, DirectView::kMenu, kByAddressItem);
            return;
        }

        page.back();
        return;
    }

    if (g->view == DirectView::kMenu && e.is(OPENRAC_UI_CHOOSE)) {
        // Host directly: this game hosts; a port taken stays here, saying so.
        if (e.item() == kHostItem) {
            g->notice.clear();
            g->direct_error.clear();
            g->saves_for = SavesFor::kHost;
            g->saves.open();
            return;
        } else if (e.item() == kByAddressItem) {
            switch_view(page, DirectView::kJoin, 0);
            return;
        }
    } else if (g->view == DirectView::kJoin) {
        if (e.is(OPENRAC_UI_CHANGE)) {
            step_address(e.item(), e.value());
        } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kJoinAddressItem) {
            g->notice.clear();
            g->direct_error.clear();

            // Only an address with no host is refused; the rows always give one.
            if (connection.connect_direct(g->address.text())) {
                poll();
            } else {
                g->direct_error = "Not an address: " + g->address.text();
            }
        }
    }

    // The connection's failures (unreachable, the host gone) and the host's refusals (a full
    // party) show in the status line.
    // In the party (hosting, or the host's hello answered): the lobby.
    if (g->session->in_party() && g->session->direct() && !e.is(OPENRAC_UI_HIDE)) {
        g->lobby.open();
    }

    show_direct();
}

/* Lobby's events: Ready or Start, and Leave or Triangle asking first; out of the party, back. A
 * host's Leave stops hosting. */
void on_lobby(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Session& s = *g->session;
    const bool hosting = g->connection->mode() == mp::Connection::Mode::kHosting;
    const bool direct_party = s.direct() || hosting
                              || g->connection->mode() == mp::Connection::Mode::kDirect;

    // Ready or Start; Leave and Triangle ask before the party is left.
    if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kReadyItem) {
        g->notice.clear();

        // The host starts the game for everyone; a member says it is ready, or no longer.
        if (s.is_host()) {
            if (!g->coop->start()) {
                page.sound(OPENRAC_UI_SOUND_DENIED);
            }
        } else if (g->coop->playing() && g->game->status().phase == mp::kPhaseFrontEnd) {
            g->coop->rejoin();
        } else {
            s.toggle_ready();
        }
    } else if ((e.is(OPENRAC_UI_CHOOSE) && e.item() == kLeaveItem) || e.is(OPENRAC_UI_BACK)) {
        const char* question = hosting       ? "Stop hosting? The party ends for everyone."
                               : s.is_host() ? "Leave the party? It ends for everyone."
                                             : "Leave the party?";
        page.confirm(question, kAskLeave);
    } else if (e.is(OPENRAC_UI_ANSWER) && e.tag() == kAskLeave && e.value() == 1) {
        leave_party();
    }

    // The notice the action made shows at once.
    if (std::string notice = s.take_notice(); !notice.empty()) {
        g->notice = notice;
    }

    // Out of the party (left, ended by the host, or the connection gone): back to Home, which
    // links to the server again after a direct party.
    if (!s.in_party() && !e.is(OPENRAC_UI_HIDE)) {
        // A direct party returns to Home, not to the Direct page.
        if (direct_party) {
            g->home.open();
        } else {
            page.back();
        }
    }

    show_lobby();
}

/* A save's row: "Save 1: Pokitaru, 3 gold bolts, 2:05", or empty. */
std::string save_row(int slot, const openrac_save_info& info, bool there) {
    const std::string name = "Save " + std::to_string(slot + 1) + ": ";

    if (!there) {
        return name + "empty";
    }

    const int minutes = info.seconds / 60;
    const std::string time = std::to_string(minutes / 60) + ":" + (minutes % 60 < 10 ? "0" : "")
                             + std::to_string(minutes % 60);
    return name + mp::save_description(g->mod->level_name(info.level), info.gold_bolts, false) + ", "
           + time;
}

/* The card's saves as the Saves page last read them (when it showed). */
struct SlotRow {
    openrac_save_info info{};
    bool there = false;
};
std::vector<SlotRow> g_slots(kSaveSlots);

void read_slots() {
    for (int slot = 0; slot < kSaveSlots; ++slot) {
        SlotRow& row = g_slots[static_cast<std::size_t>(slot)];
        row.there = g->mod->save_slot(slot, row.info) == 1;
    }
}

/* Saves: New game, then the card's saves (an empty slot dim). */
void show_saves() {
    openrac::UiContent c;
    c.title = g->saves_for == SavesFor::kHost ? "Host with which save?" : "Play which save?";
    c.items.push_back(openrac::UiItem::button("New game"));

    for (int slot = 0; slot < kSaveSlots; ++slot) {
        const SlotRow& row = g_slots[static_cast<std::size_t>(slot)];
        c.items.push_back(openrac::UiItem::button(save_row(slot, row.info, row.there), row.there));
    }

    c.footer = {g->saves_for == SavesFor::kHost ? "No server: port 7770" : status(),
                g->direct_error.empty() ? "The party plays it; your card is kept"
                                        : g->direct_error,
                mp::kSelectBackHints};
    g->saves.set(c);
}

/* A save chosen (or a new game): the co-op keeps it, then the party is made, on the server or by
 * this game hosting. */
void on_saves(const openrac::UiPage& page, const openrac::UiEvent& e) {
    if (e.is(OPENRAC_UI_SHOW)) {
        g->direct_error.clear();
        read_slots();
    } else if (e.is(OPENRAC_UI_CHOOSE)) {
        mp::Bytes save;
        std::string description = mp::save_description("", 0, true);

        if (e.item() > kNewGameItem) {
            const int slot = e.item() - 1;
            openrac_save_info info{};
            save = g->mod->save_read(slot);

            if (save.empty() || g->mod->save_slot(slot, info) != 1) {
                page.sound(OPENRAC_UI_SOUND_DENIED);
                show_saves();
                return;
            }

            description =
                mp::save_description(g->mod->level_name(info.level), info.gold_bolts, false);
        }

        g->coop->choose(save, description);
        g->notice.clear();

        if (g->saves_for == SavesFor::kCreate) {
            g->session->create(description);
        } else {
            // Hosting: the party's description is the save's; a port taken stays here.
            mp::HostConfig config;
            config.description = description;
            g->connection->set_host_config(config);

            if (g->connection->start_hosting(mp::kDefaultPort)) {
                poll();
            } else {
                const std::string error = g->connection->hosting().error;
                g->direct_error = error.empty() ? "Cannot host here" : error;
                page.sound(OPENRAC_UI_SOUND_DENIED);
            }
        }
    }

    // In the party: the lobby.
    if (g->session->in_party() && !e.is(OPENRAC_UI_HIDE)) {
        g->lobby.open();
    }

    show_saves();
}

/* A party's row in Browse: "Clank - Pokitaru, 3 gold bolts 2/4", cut to the page's width. */
std::string party_row(const mp::Listing& l) {
    const std::string count = " " + std::to_string(l.members) + "/" + std::to_string(l.size);
    std::string text = l.host + (l.description.empty() ? "" : " - " + l.description);
    const std::size_t room = OPENRAC_UI_TEXT_MAX - count.size();

    if (text.size() > room) {
        text = text.substr(0, room - 3) + "...";
    }

    return text + count;
}

/* Browse: Refresh, then the parties with room; none: a line saying so. */
void show_browse() {
    const mp::Session& s = *g->session;
    openrac::UiContent c;
    c.title = "Browse games";
    c.items.push_back(openrac::UiItem::button("Refresh", s.greeted()));
    const std::vector<mp::Listing>& parties = s.parties();

    for (std::size_t i = 0; i < parties.size() && i < kBrowseRows; ++i) {
        c.items.push_back(openrac::UiItem::button(party_row(parties[i])));
    }

    if (parties.empty()) {
        c.items.push_back(
            openrac::UiItem::text(
                s.lists() == 0 ? "Asking the server..." : "No games are hosted: Create match hosts one"
            )
        );
    }

    c.footer = {status(), mp::kSelectBackHints};
    g->browse.set(c);
}

/* Browse's events: the list asked for when it shows and every few seconds; Refresh asks at once;
 * a row joins its party (by its code); in the party, the lobby. */
void on_browse(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Session& s = *g->session;
    const std::uint64_t now = mp::now_ms();

    if (e.is(OPENRAC_UI_SHOW) || (e.is(OPENRAC_UI_FRAME) && now - g->listed_ms >= kListMs)
        || (e.is(OPENRAC_UI_CHOOSE) && e.item() == kRefreshItem)) {
        g->listed_ms = now;
        s.list();
    } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() > kRefreshItem) {
        const auto row = static_cast<std::size_t>(e.item() - 1);

        if (row < s.parties().size()) {
            g->notice.clear();
            s.join(s.parties()[row].code);
        }
    }

    if (s.in_party() && !e.is(OPENRAC_UI_HIDE)) {
        g->lobby.open();
    }

    show_browse();
    (void)page;
}

/* The port calls this when it loads the library: checks the game and the port, reads the server
 * address, makes the pages and puts Multiplayer in the main menu under Load Game. Throws
 * openrac::Error to refuse to run. */
void load(openrac::Mod& mod) {
    // The pages are Ratchet & Clank's front end.
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }

    // The pages of the game's own menus are interface 1.7's, playing together 1.8's.
    if (!mod.has_ui() || !mod.has_world()) {
        throw openrac::Error(
            "this OpenRAC has no pages of the game's menus or world calls for mods (interface 1.9)"
        );
    }

    // An earlier load's connection would keep its thread: stop it first.
    if (g) {
        g->connection->close();
    }

    g = std::make_unique<State>();
    const std::string server = mod.setting<std::string>("server");

    // An address that does not read is the default's.
    if (!mp::parse_address(server, g->host, g->port)) {
        g->host = "localhost";
        g->port = mp::kDefaultPort;
        mod.warn("server \"" + server + "\" is not host:port; using localhost:7770");
    }

    g->connection = std::make_unique<mp::Connection>();
    g->session = std::make_unique<mp::Session>(
        *g->connection, kVersion, player_name(mod.setting<std::string>("name"))
    );
    g->mod = &mod;
    g->game = std::make_unique<ModGame>(mod);
    g->coop = std::make_unique<mp::Coop>(*g->session, *g->game);
    g->coop->set_build(
        std::string(kVersion) + " " + std::string(mod.openrac_version()) + " " + std::string(mod.version())
    );
    g->world = std::make_unique<mp::World>(*g->session, *g->game, *g->coop);
    g->coop->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    g->world->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    // The game's DATA: the world's kinds to the world, the rest to the co-op.
    g->session->on_data([](std::int32_t sender, const mp::Bytes& bytes) {
        if (!bytes.empty() && mp::world_kind(bytes[0])) {
            g->world->handle(sender, bytes);
        } else {
            g->coop->handle(sender, bytes);
        }
    });
    // A crate broken here to show another game's break drops nothing (its bolts are the breaker's).
    mod.hook<openrac::rac1::Moby_RecordDeath>(
        [](auto original, std::uint32_t moby, std::int32_t flags, std::int32_t bolts) {
            if (!g->world->quiet_death(g->mod->moby_slot_of(moby))) {
                original(moby, flags, bolts);
            }
        }
    );
    // The ship's planet select: the host's travel takes the party along, a member's to another
    // planet lands the ship again where it is (coop.hpp: only the host flies).
    mod.hook<openrac::rac1::Ship_TravelTo>([](auto original, std::int32_t dest) {
        const int here = g->game->status().level;
        original(g->coop->ship_travel(dest, here) ? dest : here);
    });
    // The first pad: a Start in a scene or a movie skips it for the party, a party's skip presses
    // Start for a movie playing here, and the scene barrier holds the control (coop.hpp). The report: buttons in
    // bytes 0 and 1 (a bit clear while held; Start 0x08), the sticks, then the pressures.
    mod.on_pad([](int port, std::uint8_t* report, int size) {
        if (port != 0 || size < 6) {
            return;
        }

        const std::uint64_t now = mp::now_ms();

        // A movie plays outside the game's ticks (no on_frame): the party's messages are read here.
        if (now - g->frame_ms > 100) {
            poll();
        }

        // Start pressed in a scene or a movie: the whole party skips it.
        const bool start = (report[0] & 0x08) == 0;

        if (start && !g->start_held) {
            g->coop->own_skip(g->game->status().phase, now);
        }

        g->start_held = start;

        if (g->coop->hold_input()) {
            report[0] = report[1] = 0xFF;
            std::fill(report + 2, report + 6, static_cast<std::uint8_t>(0x80));
            std::fill(report + 6, report + size, static_cast<std::uint8_t>(0));
        } else if (g->coop->movie_press(now)) {
            // The party's skip of the movie playing: Start, for the movie player.
            report[0] = static_cast<std::uint8_t>(report[0] & ~0x08);
        }
    });
    // The game asks the world around each moby's update and of each hit it posts.
    mod.on_moby_update(
        [](void*, int slot, int phase) { return g->world->gate(slot, phase) ? 1 : 0; }
    );
    mod.on_moby_hit([](void*, const openrac_hit* hit) {
        mp::Hit h;
        h.victim = hit->victim;
        h.attacker = hit->attacker;
        h.mask = hit->mask;
        h.damage = hit->damage;
        std::copy(hit->position, hit->position + 3, h.position);
        std::copy(hit->velocity, hit->velocity + 3, h.velocity);
        return g->world->hit(h) ? 1 : 0;
    });
    g->home = mod.ui_page(on_home);
    g->code_page = mod.ui_page(on_code);
    g->direct_page = mod.ui_page(on_direct, OPENRAC_UI_OWN_BACK);
    g->lobby = mod.ui_page(on_lobby, OPENRAC_UI_OWN_BACK);
    g->browse = mod.ui_page(on_browse);
    g->saves = mod.ui_page(on_saves);
    show_home();
    show_code();
    show_direct();
    show_lobby();
    show_browse();
    show_saves();
    mod.ui_main_item(OPENRAC_UI_LOAD_GAME, "Multiplayer", g->home);

    // In a level, the pause menu's Options page has a Multiplayer item too (interface 1.9).
    if (mod.has_pause_items()) {
        g->pause_page = mod.ui_page(on_pause);
        show_pause();
        mod.ui_pause_item("Multiplayer", g->pause_page);
    }

    // The session follows the server every tick, whether a page shows or not, and the co-op the
    // game.
    mod.on_frame([] {
        poll();
        const std::uint64_t now = mp::now_ms();
        g->frame_ms = now;

        // In a party, the player's link every few seconds: a fresh round trip asked, the last one
        // told to the party (the pause page's pings).
        if (g->session->in_party() && now - g->link_ms >= mp::kLinkMs) {
            g->link_ms = now;
            g->session->sync_clock();
            g->session->report_link(
                g->session->has_clock() ? g->session->rtt_ms() : -1, g->connection->udp()
            );
        }

        g->coop->tick(now);
        g->world->tick(now);

        // The party's line over the game: a refused ship travel, a wait for the others.
        if (std::string banner = g->coop->banner(now); banner != g->banner) {
            g->banner = banner;
            g->mod->play_notice(banner);
        }
    });
    mod.info(
        "multiplayer: " + g->session->name() + ", server " + g->host + ":" + std::to_string(g->port)
    );
}

}  // namespace

/* The id and version are the manifest's: the port checks they agree. */
OPENRAC_MOD_CPP("multiplayer", "0.6.0", load)

/* The port calls this at its exit: the connection's thread goes with it. */
extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    // The state exists only if load got as far as making it.
    if (g) {
        g->connection->close();
        g.reset();
    }
}
