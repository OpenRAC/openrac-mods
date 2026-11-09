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
 * calls of interface 1.8). The mods a party has to agree on are checked as a player joins
 * (party_mods.hpp): one whose differ is told why, and cannot be Ready.
 *
 * Over the game the other players' names (nametags.hpp), through the port's overlay.
 *
 * The pages, drawn by the game (its frames, list, font, prompt box and sounds):
 *   Home    Browse games (the parties others host), Create match (a party of the player's own,
 *           from a save), Join by code and Direct connect (the next pages), and the player's name;
 *   Browse  Refresh, then a row per party with room (its host, its save, how many are in it);
 *           Cross on one joins it; the list is asked again every few seconds;
 *   Saves   New game and the memory card's saves (their planet, gold bolts and time): the one
 *           chosen is what the party plays (Create match, and Host directly); Continue as host
 *           shows the party saves of past parties (cosave.hpp) in their place;
 *   Code    a row per character of the code, Left and Right step it through the server's code
 *           alphabet, then Join;
 *   Direct  Host directly and Join by address; Join by address's rows (the address's four octets
 *           and the port, direct.hpp's rings) and Join take the page's place;
 *   Refused why this game cannot join the host's (the mods that differ) and what to do;
 *   Rules   what the party plays by, and what stays the player's own (also from the pause menu);
 *   Lobby   Ready (a member) or Start (the host), Leave, Party rules, and the party's members (each
 *           with whether its mods match); a direct one
 *           says where it is hosted instead of a code. Leave and Triangle ask first, in the
 *           game's prompt box. Leaving a direct party goes back to the server. A host's Cross on a
 *           member opens Remove;
 *   Remove  a member taken out of the party, or banned from this save (from the lobby, and from the
 *           pause menu's Multiplayer page);
 *   Team tools  co-op rescue, its window and the host's rally point (the host's to set), Mark host
 *           position, Clear rally point, Give up, and each member's health (comfort.hpp; from the
 *           lobby and the pause menu's Multiplayer page).
 * The pages speak the game's language (words.hpp: the five PAL languages), read again each tick.
 * Cross chooses, Triangle goes back a page (from Home, to the game's menu). The footer says what
 * the connection is doing. The connection (connection.cpp) is opened the first time Home shows and
 * lasts until the game ends, reconnecting when the server goes; the session follows it every tick.
 *
 * Settings (openrac-mod.json):
 *   server  (string, default game.openrac.dev:7770)  The OpenRAC server, host:port.
 *   name    (string, default empty)  The player's name (3 to 16 letters, digits, underscores);
 *           empty or invalid: the launcher's Multiplayer name.
 */
#include "mod_state.hpp"
#include "rac2/game.hpp"

#include <ctime>
#include <memory>

namespace mpmod {

std::unique_ptr<State> g;
mp::Language g_language = mp::Language::kEnglish;

mp::Language game_language(const openrac::Mod& mod) {
    // Going Commando's is a byte of the executable's (rac2_language_address).
    if (mod.game() == "rac2") {
        const std::uint32_t address = mp::rac2_language_address(mod.version());
        return address == 0 ? mp::Language::kEnglish
                            : mp::language_from_game(mod.memory().read<std::uint8_t>(address));
    }

    const std::uint32_t address = mp::language_address(mod.version());
    return address == 0 ? mp::Language::kEnglish
                        : mp::language_from_game(mod.memory().read<std::int32_t>(mod.relocate(address)));
}

const char* game_tag() {
    return g != nullptr && g->rac2 ? mp::kGameRac2 : mp::kGameRac1;
}

const char* session_version() {
    return g != nullptr && g->rac2 ? kVersionRac2 : kVersion;
}

// The port accepts at most OPENRAC_UI_TEXT_MAX bytes a field. Fit them here, before the API's byte
// truncation, so accented player, save and planet names stay valid UTF-8.
void set_content(const openrac::UiPage& page, openrac::UiContent& content) {
    content.title = mp::fit_ui_text(content.title, mp::kTitleMax);

    for (openrac::UiItem& item : content.items) {
        item.label = mp::fit_ui_text(item.label, mp::kItemMax);

        for (std::string& choice : item.choices) {
            choice = mp::fit_ui_text(choice, mp::kItemMax);
        }
    }

    for (std::string& line : content.footer) {
        line = mp::fit_ui_text(line, mp::kFooterLineMax);
    }

    page.set(content);
}

/* How often the verbose log gives the network's counts. */
constexpr std::uint64_t kNetTraceMs = 5000;

/* One line of what went through the session so far: per DATA kind the messages and bytes as
 * events, as state and received, and what the buckets held back. */
void trace_net() {
    const mp::NetCounts& c = g->session->counts();
    std::string line = "frames " + std::to_string(c.data_frames) + " state " + std::to_string(c.state_datagrams)
                       + " state held " + std::to_string(c.state_held) + " state by tcp " + std::to_string(c.state_tcp)
                       + " tcp copy held " + std::to_string(c.state_tcp_held) + " flushes that waited "
                       + std::to_string(c.waited) + " most queued " + std::to_string(c.queue_most);
    const auto kinds = [&line](const char* what, const std::array<mp::NetCounts::Kind, 256>& all) {
        for (std::size_t kind = 0; kind < all.size(); ++kind) {
            if (all[kind].messages != 0) {
                line += std::string(" ") + what + std::to_string(kind) + "=" + std::to_string(all[kind].messages) + "/"
                        + std::to_string(all[kind].bytes);
            }
        }
    };
    kinds("e", c.events_out);
    kinds("s", c.state_out);
    kinds("r", c.in);
    g->trace.line("net", line);
}

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

    return mp::status_text(in, g_language);
}

/* Team tools' line over the game: down and waiting, a teammate down nearby, the rally point. */
std::string comfort_banner(std::uint64_t now) {
    if (!g->comfort) {
        return {};
    }

    if (g->comfort->down()) {
        return std::string(text("Awaiting rescue ")) + std::to_string(g->comfort->seconds_left(now)) + " s";
    }

    if (g->comfort->near_downed()) {
        return text("Hold L3 + R3");
    }

    const float distance = g->comfort->rally_distance();

    if (distance >= 0) {
        return std::string(text("Rally point: ")) + std::to_string(static_cast<int>(distance)) + text(" units");
    }

    return {};
}

/* Team tools' items, in this order (on_comfort reads them by it). */
enum ComfortItem {
    kComfortRevive,
    kComfortSeconds,
    kComfortRally,
    kComfortMark,
    kComfortClear,
    kComfortGiveUp
};

/* Whether the party's own revive rule (world.hpp: a fallen hero held down) is what catches a death
 * here: then a lethal hit goes to it, and Team tools' rescue stands aside. */
bool revive_rule_held() {
    return g->world->revive_on() && g->game->can_revive();
}

/* Team tools: the host's rescue and rally settings (dim for a member), Mark host position, Clear
 * rally point, Give up, the rally point's distance, then each member's health or need of rescue. */
void show_comfort() {
    if (!g->comfort || g->comfort_page.id() < 0) {
        return;
    }

    const mp::ComfortOptions& o = g->comfort->options();
    const bool host = g->session->is_host();
    openrac::UiContent c;
    c.title = text("Team tools");
    const auto choice = [host](const char* label, std::vector<std::string> values, int value) {
        openrac::UiItem item = openrac::UiItem::choice_of(text(label), std::move(values), static_cast<std::uint32_t>(value));

        if (!host) {
            item.flags = OPENRAC_UI_DISABLED;
        }

        return item;
    };
    const bool party = g->session->in_party();
    c.items = {
        choice("Co-op rescue", {text("Off"), text("On")}, o.revive ? 1 : 0),
        choice("Time to rescue", {"10 s", "20 s", "30 s"}, o.down_seconds / 10 - 1),
        choice("Host rally point", {text("Off"), text("On")}, o.rally ? 1 : 0),
        openrac::UiItem::button(text("Mark host position"), party && o.rally && g->coop->playing()),
        openrac::UiItem::button(text("Clear rally point"), host && g->comfort->rally().active),
        openrac::UiItem::button(text("Give up"), g->comfort->down()),
    };
    const float distance = g->comfort->rally_distance();
    c.items.push_back(openrac::UiItem::text(
        distance >= 0 ? std::string(text("Rally point: ")) + std::to_string(static_cast<int>(distance)) + text(" units")
                      : std::string(text("No rally point"))
    ));

    // The party's revive rule takes a fallen hero first (see the take-damage hook).
    if (party && revive_rule_held()) {
        c.items.push_back(openrac::UiItem::text(text("Revive rule: on")));
    }

    for (const mp::Member& member : g->session->members()) {
        if (c.items.size() >= OPENRAC_UI_ITEMS_MAX) {
            break;
        }

        mp::Pose p;
        const bool have = member.id == g->session->id() ? g->game->hero(p) : g->coop->latest(member.id, p);
        const auto peer = g->comfort->peers().find(member.id);
        const bool help = member.id == g->session->id() ? g->comfort->down()
                                                         : peer != g->comfort->peers().end() && peer->second.down;
        const std::string line = member.name + ": "
                                 + (help   ? std::string(text("Needs rescue"))
                                    : have ? "HP " + std::to_string(std::max(0, p.health))
                                           : std::string(text("Loading")));
        c.items.push_back(openrac::UiItem::text(line));
    }

    c.footer = {text("Hold L3 + R3")};
    set_content(g->comfort_page, c);
}

/* Team tools' events: the host's settings, the rally point, giving up; out of a party, back. */
void on_comfort(const openrac::UiPage& page, const openrac::UiEvent& e) {
    if (e.is(OPENRAC_UI_CHANGE) && g->session->is_host()) {
        mp::ComfortOptions options = g->comfort->options();

        if (e.item() == kComfortRevive && e.value() >= 0 && e.value() < 2) {
            options.revive = e.value() != 0;
        } else if (e.item() == kComfortSeconds && e.value() >= 0 && e.value() < 3) {
            options.down_seconds = (e.value() + 1) * 10;
        } else if (e.item() == kComfortRally && e.value() >= 0 && e.value() < 2) {
            options.rally = e.value() != 0;
        }

        g->comfort->configure(options);
    } else if (e.is(OPENRAC_UI_CHOOSE)) {
        if (e.item() == kComfortMark) {
            g->comfort->request_rally(mp::now_ms());
        } else if (e.item() == kComfortClear) {
            g->comfort->clear_rally();
        } else if (e.item() == kComfortGiveUp) {
            g->comfort->give_up();
        }
    }

    if (!g->session->in_party() && !e.is(OPENRAC_UI_HIDE)) {
        page.back();
        return;
    }

    show_comfort();
}

/* Every page again, in the language the game is set to now. */
void repaint_pages() {
    show_home();
    show_code();
    show_direct();
    show_lobby();
    show_browse();
    show_saves();
    show_refused();
    show_rules();
    show_remove();
    show_comfort();

    if (g->pause_page.id() >= 0) {
        show_pause();
    }

    // An unchanged line over the game, in the new language too.
    g->mod->play_notice(mp::localized_notice(g->banner, g_language));
}

/* The party's line over the game (play_notice), when it changed: a refused ship travel or skip,
 * a wait for the others. */
/* The port's overlay, as the name tags draw on it. */
class OverlayPainter : public mp::TagPainter {
public:
    explicit OverlayPainter(const openrac_overlay& overlay) : o_(overlay) {}

    bool project(const float world[3], float& x, float& y) override {
        return o_.project(o_.context, world, &x, &y) != 0;
    }

    void rect(float x, float y, float w, float h, std::uint32_t colour) override {
        o_.rect(o_.context, x, y, w, h, colour);
    }

    void text(float x, float y, float height, std::uint32_t colour, const std::string& line) override {
        o_.text(o_.context, x, y, height, colour, line.c_str());
    }

    float text_width(float height, const std::string& line) override {
        return o_.text_width(o_.context, height, line.c_str());
    }

    float fade(float distance, float near_distance, float far_distance) override {
        return o_.fade(o_.context, distance, near_distance, far_distance);
    }

private:
    const openrac_overlay& o_;
};

void draw_tags(void*, const openrac_overlay* overlay) {
    float camera[3];

    // An older port's handle has no text_width: no tags, as with no overlay at all.
    if (g == nullptr || overlay->size < sizeof(openrac_overlay) || !g->game->camera(camera)) {
        return;
    }

    OverlayPainter painter(*overlay);
    g->coop->note_tags(mp::draw_name_tags(painter, camera, g->coop->name_tags()));
}

/* The host: the launcher's mod code, a few times a minute (a player may share while the game runs). */
void refresh_mod_code(std::uint64_t now) {
    constexpr std::uint64_t kEveryMs = 3000;

    if (!g->session->is_host() || (g->mod_code_ms != 0 && now - g->mod_code_ms < kEveryMs)) {
        return;
    }

    g->mod_code_ms = now;
    const mp::ModCode found = mp::read_mod_code(g->mod->plugins_folder(), g->mods->own());
    g->mod_code_long = found.shared && found.code.empty();
    g->mods->set_mod_code(found.code);
}

constexpr std::uint64_t kAutoJoinMs = 20000;

/* The party the launcher said to join, from the title: linked to the server, then in, then its
 * lobby. Not in time, Home says so; a mod that plays alone stays out, as by hand. */
void tick_auto_join(std::uint64_t now) {
    if (g->join_party.empty() || g->game->status().phase != mp::kPhaseFrontEnd) {
        return;
    }

    mp::Session& s = *g->session;
    const auto give_up = [&](const std::string& why) {
        g->mod->info("multiplayer: " + why);
        g->leave_reason = why;
        g->join_party.clear();
    };

    if (g->mods->solo()) {
        give_up("A mod you have on can't be played in a party");
    } else if (s.in_party()) {
        g->mod->info("multiplayer: joined party " + g->join_party + " as the launcher asked");
        g->join_party.clear();
        g->lobby.open();
    } else if (g->join_ms == 0) {
        g->join_ms = now;
        back_to_server();
    } else if (now - g->join_ms >= kAutoJoinMs) {
        give_up("Could not join party " + g->join_party);
    } else if (!g->join_asked && s.greeted() && !s.waiting()) {
        g->join_asked = true;
        s.join(g->join_party);
    }
}

void show_banner(std::uint64_t now) {
    // Who is gone first; then the party's own line: a save's only says it went well.
    std::string line = g->migrate->banner(now);

    if (line.empty()) {
        line = g->coop->banner(now);
    }

    if (line.empty()) {
        line = g->cosave->banner(now);
    }

    if (line.empty()) {
        line = comfort_banner(now);
    }

    if (std::string banner = std::move(line); banner != g->banner) {
        g->banner = banner;
        g->mod->play_notice(mp::localized_notice(banner, g_language));
    }
}

/* Brings the session up to date with the connection: hello on a new connection, the server's
 * messages, the last notice. Once a game tick. */
void poll() {
    const std::uint32_t generation = g->connection->generation();
    const bool connected = g->connection->state() == mp::Connection::State::kConnected;
    const bool was_in = g->session->in_party();
    const bool direct = g->session->direct()
                        || g->connection->mode() == mp::Connection::Mode::kDirect;

    // A new connection: a new session on the server.
    if (connected && generation != g->generation) {
        g->generation = generation;
        g->session->connected();
    } else if (!connected && g->session->greeted()) {
        // The connection went: so did the party.
        g->session->disconnected();
    }

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

    // A refusal or news replaces the last; the party gone, why, for Home (none when the player
    // left it). With nobody to follow the host, its party save is where the game goes on.
    std::string notice = g->session->take_notice();

    if (notice == mp::kHostLeftNotice && g->cosave->slot() >= 0) {
        notice = mp::kContinueNotice;
    }

    if (was_in && !g->session->in_party()) {
        g->leave_reason = g->left_differs ? mp::kDifferentDisc
                                          : mp::leave_reason(g->leaving, !connected, direct, notice);
        g->left_differs = false;
    } else if (g->session->in_party()) {
        g->leave_reason.clear();
    }

    g->leaving = false;

    if (!notice.empty()) {
        g->notice = notice;
    }

    if (std::string notice = g->coop->take_notice(); !notice.empty()) {
        g->notice = notice;
    }
}

}  // namespace mpmod

namespace {

using namespace mpmod;

/* The port calls this when it loads the library: checks the game and the port, reads the server
 * address, makes the pages and puts Multiplayer in the main menu under Load Game. Throws
 * openrac::Error to refuse to run. */
void load(openrac::Mod& mod) {
    // The same pages and party in Going Commando, on what OpenRAC answers for it (rac2/game.hpp).
    const bool rac2 = mod.game() == "rac2";

    if (rac2 && !mod.has_play()) {
        throw openrac::Error("this OpenRAC has no play calls for Going Commando (a newer OpenRAC has them)");
    }

    if (mod.game() != "rac1" && !rac2) {
        throw openrac::Error("this mod is for Ratchet & Clank (rac1) and Going Commando (rac2), not "
                             + std::string(mod.game()));
    }

    // The pages of the game's own menus are interface 1.7's, playing together 1.8's.
    if (!mod.has_ui() || !mod.has_world() || !mod.has_party_rules()) {
        throw openrac::Error(
            "this OpenRAC has no pages of the game's menus, world calls or party rules for mods (interface 1.11)"
        );
    }

    // An earlier load's connection would keep its thread: stop it first.
    if (g) {
        g->connection->close();
    }

    g = std::make_unique<State>();
    g->rac2 = rac2;
    g_language = game_language(mod);
    const std::string server = mod.setting<std::string>("server");

    // An address that does not read is the default's.
    if (!mp::parse_address(server, g->host, g->port)) {
        g->host = "game.openrac.dev";
        g->port = mp::kDefaultPort;
        mod.warn("server \"" + server + "\" is not host:port; using game.openrac.dev:7770");
    }

    g->connection = std::make_unique<mp::Connection>();
    g->session = std::make_unique<mp::Session>(
        *g->connection, session_version(), player_name(mod.setting<std::string>("name"))
    );
    g->mod = &mod;
    g->game = rac2 ? std::make_unique<Rac2Game>(mod) : std::make_unique<ModGame>(mod);
    g->coop = std::make_unique<mp::Coop>(*g->session, *g->game);

    // Going Commando tells no scene to the mods yet: nobody waits for anyone's (Coop::barrier).
    if (rac2) {
        g->coop->barrier(false);
    }
    g->mods = std::make_unique<mp::PartyMods>(*g->session, *g->game, mp::ModList{});
    g->mods->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    // No Ready and no game for a member whose mods differ from the host's (party_mods.hpp).
    g->session->set_ready_gate([](std::int32_t member) {
        return g->session->is_host() ? g->mods->may_play(member) : g->mods->heard_host() && !refused();
    });
    g->coop->set_gate([](std::int32_t member) { return g->mods->may_play(member); });
    g->world = std::make_unique<mp::World>(*g->session, *g->game, *g->coop);
    g->progress = std::make_unique<mp::Progress>(*g->session, *g->game, *g->coop);
    g->progress->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    g->cosave = std::make_unique<mp::CoSave>(*g->session, *g->game, mp::now_ms);
    g->coop->set_save_hooks([](std::int32_t target) { g->cosave->announce(target); },
                            [](const mp::Bytes& save) { return g->cosave->starting(save); });
    g->coop->on_start_again([] { g->cosave->start_again(); });
    g->coop->on_joiner_save([] { return g->world->save_for_a_joiner(); });
    g->shared = std::make_unique<mp::Shared>(*g->session, *g->game, *g->coop);
    g->shared->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    g->camscripts = std::make_unique<mp::CamScripts>(*g->session, *g->game, *g->coop);
    g->camscripts->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    g->migrate = std::make_unique<mp::Migrate>(*g->session, *g->game, *g->coop, *g->world, *g->progress, *g->shared, *g->mods,
                                              *g->cosave, *g->camscripts);
    g->migrate->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    g->coop->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    // Team tools (comfort.hpp): what it knows of the party, the hero and the others' poses.
    mp::ComfortHooks comfort;
    comfort.self = [] { return static_cast<int>(g->session->id()); };
    comfort.host = [] { return static_cast<int>(g->session->host()); };
    comfort.room = [] { return g->session->in_party() ? g->session->code() : std::string{}; };
    comfort.members = [] {
        std::vector<int> ids;

        for (const mp::Member& m : g->session->members()) {
            ids.push_back(static_cast<int>(m.id));
        }

        return ids;
    };
    comfort.pose = [](int id, mp::Pose& out, bool menus) {
        if (!g->coop->playing()) {
            return false;
        }

        if (id == g->session->id()) {
            const int phase = g->game->status().phase;

            if (phase != mp::kPhasePlaying && !(menus && phase == mp::kPhaseMenu)) {
                return false;
            }

            return g->game->hero(out);
        }

        const auto it = g->coop->remotes().find(id);

        if (it == g->coop->remotes().end()
            || !mp::remote_pose_available(it->second, static_cast<double>(mp::now_ms()), menus)) {
            return false;
        }

        return g->coop->latest(id, out);
    };
    comfort.send = [](int target, const mp::Bytes& bytes) { g->session->send_data(target, bytes); };
    // The deferred lethal hit, given to the game's own damage at last (no health or place written).
    comfort.expire = [] {
        if (auto original = g->mod->original<openrac::rac1::Hero_TakeDamage>()) {
            original(1);
        }
    };
    g->comfort = std::make_unique<mp::Comfort>(std::move(comfort));
    g->world->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    // The verbose log (trace.hpp): the `verbose_log` setting, or OPENRAC_MP_TRACE=1. Its lines go
    // to mp-trace.log in the game's working directory (beside run.log), else to the game's log.
    const char* trace_env = std::getenv("OPENRAC_MP_TRACE");

    g->revive = mod.setting<bool>("revive");
    g->join_party = mp::take_join_request(mod.plugins_folder(), static_cast<std::int64_t>(std::time(nullptr)));
    g->hold_party = mod.setting<bool>("hold_party");

    if (mod.setting<bool>("verbose_log") || (trace_env != nullptr && std::string(trace_env) == "1")) {
        std::error_code error;
        std::filesystem::path path = std::filesystem::current_path(error) / "mp-trace.log";
        path = std::filesystem::absolute(path, error);
        g->trace_file.open(path, std::ios::out | std::ios::app);

        if (g->trace_file) {
            g->trace.open([](const std::string& line) { g->trace_file << line << '\n'; });
            mod.info("multiplayer: verbose log on, its [mp-trace] lines in " + path.string());
        } else {
            g->trace.open([](const std::string& line) { g->mod->info(line); });
            mod.warn("multiplayer: verbose log on, but " + path.string() + " does not open: its [mp-trace] lines go here");
        }

        if (!mod.has_moby_run()) {
            mod.warn("multiplayer: this OpenRAC has no moby_run: the verbose log has no run lines or run cameras");
        }
    }

#ifdef MULTIPLAYER_BOT
    g->bot = std::make_unique<mp::Bot>(mp::Bot::Parts{
        *g->connection, *g->session, *g->game, *g->coop, *g->world, *g->mods, *g->camscripts, *g->migrate, [](const float* at) {
            // The hero's motion position (the bot is a test's).
            openrac::Memory memory = g->mod->memory();
            const openrac::gaddr hero = g->mod->version() == "ntsc" ? 0x0013F3D0u : 0x0013F4D0u;
            for (unsigned i = 0; i < 3; ++i) {
                memory.write<float>(hero + 4u * i, at[i]);
            }
        }, [](int item) {
            // weapon_ammo_counts and hero.pending_item[0] (the bot is a test's). The Drone Device
            // is no held weapon: choosing it asks for the drones (hero +0x1FF5), as the game does.
            g->game->give_item(item);
            openrac::Memory memory = g->mod->memory();
            const bool ntsc = g->mod->version() == "ntsc";
            const openrac::gaddr hero = ntsc ? 0x0013F350u : 0x0013F450u;
            const openrac::gaddr ammo = ntsc ? 0x0013D428u : 0x0013D530u;
            memory.write<std::int32_t>(ammo + 4u * static_cast<unsigned>(item), 40);

            if (item == 0x18) {
                memory.write<std::uint8_t>(hero + 0x1FF5u, 1);
            } else {
                memory.write<std::int32_t>(hero + 0x20B8u, item);
            }
        }, [](const std::string& name) {
            // What a page shows, as a player reads it.
            const openrac::UiContent c = name == "refused" ? refused_content()
                                         : name == "rules" ? rules_content()
                                         : name == "remove" ? remove_content()
                                                           : lobby_content();
            std::vector<std::string> lines = {c.title};

            for (const openrac::UiItem& item : c.items) {
                lines.push_back(item.label);
            }

            lines.insert(lines.end(), c.footer.begin(), c.footer.end());
            return lines;
        }, [](const std::string& text) {
            // How wide the game's list font draws it (each disc's glyph table; the bot is a test's):
            // the sum of each character's width, byte 3 of its 4-byte entry, as the port's menu_fit.
            const openrac::Memory memory = g->mod->memory();
            const openrac::gaddr glyphs = g->mod->version() == "ntsc" ? 0x001DF3F0u : 0x001DF770u;
            int width = 0;

            for (const char ch : text) {
                width += memory.read<std::int8_t>(glyphs + 4u * static_cast<std::uint8_t>(ch) + 3u);
            }

            return width;
        }, [] { return mp::tagged_description("New game", mp::mods_digest(g->mods->own()), game_tag()); },
        *g->cosave, [] { return g->game->game_save(); }
    });
#endif
    g->world->trace(&g->trace);
    g->coop->trace(&g->trace);
    g->progress->trace(&g->trace);
    g->shared->trace(&g->trace);
    g->camscripts->trace(&g->trace);
    // The game's DATA, to the owner of its kind (kinds.hpp); what nobody owns goes to the co-op.
    g->router.fallback([](std::int32_t sender, const mp::Bytes& bytes) { g->coop->handle(sender, bytes); });
    g->router.route(mp::Owner::kMods, [](std::int32_t sender, const mp::Bytes& bytes) { g->mods->handle(sender, bytes); });
    g->router.route(mp::Owner::kWorld, [](std::int32_t sender, const mp::Bytes& bytes) { g->world->handle(sender, bytes); });
    g->router.route(mp::Owner::kTether, [](std::int32_t sender, const mp::Bytes& bytes) { g->world->handle(sender, bytes); });
    g->router.route(mp::Owner::kProgress, [](std::int32_t sender, const mp::Bytes& bytes) { g->progress->handle(sender, bytes); });
    g->router.route(mp::Owner::kSave, [](std::int32_t sender, const mp::Bytes& bytes) { g->cosave->handle(sender, bytes); });
    g->router.route(mp::Owner::kShare, [](std::int32_t sender, const mp::Bytes& bytes) { g->shared->handle(sender, bytes); });
    g->router.route(mp::Owner::kMigrate, [](std::int32_t sender, const mp::Bytes& bytes) { g->migrate->handle(sender, bytes); });
    g->router.route(mp::Owner::kCamera, [](std::int32_t sender, const mp::Bytes& bytes) { g->camscripts->handle(sender, bytes); });
    g->router.route(mp::Owner::kComfort, [](std::int32_t sender, const mp::Bytes& bytes) {
        g->comfort->handle(sender, bytes, mp::now_ms());
    });
    g->session->on_data([](std::int32_t sender, const mp::Bytes& bytes) { g->router.dispatch(sender, bytes); });
    // Ratchet & Clank's hook points (the manifest's): Going Commando's world, rescue and ship are
    // not shared yet (rac2/game.hpp).
    if (!rac2) {
        // A member's game in the host's level drops no bolts of its own (the host's copy of the crate
        // or enemy drops them, and they come here as the host's): the death recorded as the game does it
        // (its killed bits), its drop left out. SetDeathBits drops about the s16 at +0xB4, or (for a
        // tallied source, +0xB1 not below 0) at least one of what +0xB6 has left: +0xB4 cleared (as the
        // game clears it after) and +0xB1 untallied (0xFF; 0xFE, a death not recorded, is left) for
        // the call, +0xB1 put back.
        mod.hook<openrac::rac1::Moby_RecordDeath>(
            [](auto original, std::uint32_t moby, std::int32_t flags, std::int32_t bolts) {
                const int slot = g->mod->moby_slot_of(moby);
                g->world->note_death(slot);

                if (!g->world->quiet_drop(slot)) {
                    original(moby, flags, bolts);
                    return;
                }

                openrac::Memory memory = g->mod->memory();
                const auto source = memory.read<std::uint8_t>(moby + 0xB1u);
                memory.write<std::int16_t>(moby + 0xB4u, 0);

                if (source != 0xFEu) {
                    memory.write<std::uint8_t>(moby + 0xB1u, 0xFFu);
                }

                original(moby, flags, bolts);
                memory.write<std::uint8_t>(moby + 0xB1u, source);
            }
        );
        // A bolt reaching this game's Ratchet: in a member's game in the host's level, the host's to
        // give (a claim; BOLTS brings its value), so nothing is added here; the bolt update deletes the
        // bolt either way.
        mod.hook<openrac::rac1::Moby_CollectBolt>([](auto original, std::uint32_t bolt) {
            if (g->world->collect(g->mod->moby_slot_of(bolt))) {
                original(bolt);
            }
        });
        // A normal lethal hit, deferred while a teammate can rescue (Team tools): the hero keeps one
        // health. Falls and crushers set health directly and keep the game's own checkpoint. With the
        // party's revive rule on, the death is that rule's (the game holds the hero down), so the hit
        // goes on as it is: the two never both catch one death.
        mod.hook<openrac::rac1::Hero_TakeDamage>([](auto original, std::int32_t damage) {
            if (revive_rule_held()) {
                original(damage);
                return;
            }

            const openrac::gaddr hero = g->mod->relocate(g->mod->version() == "pal" ? 0x13F450u : 0x13F350u);
            const bool on_foot = g->mod->memory().read<std::uint8_t>(hero + 0x20A4u) == 0;

            if (!g->comfort->hit(mp::now_ms(), damage, on_foot)) {
                original(damage);
            }
        });
        // The ship's planet select: the host's travel takes the party along, a member's to another
        // planet lands the ship again where it is (coop.hpp: only the host flies).
        mod.hook<openrac::rac1::Ship_TravelTo>([](auto original, std::int32_t dest) {
            const int here = g->game->status().level;
            original(g->coop->taking_off() || g->coop->ship_travel(dest, here) ? dest : here);
        });
    }
    // The first pad: the host's Start in a scene or a movie skips it for the party (a member's is
    // taken out: only the host skips), a party's skip presses Start for a movie playing here, and
    // the scene barrier holds the control (coop.hpp). The report: buttons in
    // bytes 0 and 1 (a bit clear while held; Start 0x08), the sticks, then the pressures.
    mod.on_camera_script([](void*, int id, int started) { g->camscripts->script(id, started != 0); });
    mod.on_pad([](int port, std::uint8_t* report, int size) {
        if (port != 0 || size < 6) {
            return;
        }

        const std::uint64_t now = mp::now_ms();

        // A movie plays outside the game's ticks (no on_frame): the party's messages are read here.
        if (now - g->frame_ms > 100) {
            poll();
            g->session->flush();
#ifdef MULTIPLAYER_BOT
            g->bot->poll();
#endif
        }

#ifdef MULTIPLAYER_BOT
        g->bot->pad(report, size);
#endif

        // A camera script shown here gives way to the player's hands (camscripts.hpp).
        if (mp::pad_active(report, size)) {
            g->camscripts->input(now);
        }

        // Start pressed in a scene or a movie: the host's skips it for the whole party; a member's
        // does nothing here but say so (a movie has no game ticks: the banner is shown from here).
        const bool start = (report[0] & 0x08) == 0;
        const int phase = g->game->status().phase;
        // L3 and R3 held together (bits 0x02 and 0x04 of byte 0 clear), in a level, the window ours:
        // a rescue (Team tools).
        const openrac_input& input = g->mod->input().raw();
        g->comfort->holding(phase == mp::kPhasePlaying && input.focused && !input.overlay && (report[0] & 0x06) == 0);
        // The press that says a member is ready for the host's ship is that and no more: to its
        // release, or the rest of it opens the pause menu.
        g->start_taken = start && (g->start_taken || g->coop->ship_held());

        if (start && !g->start_held) {
            g->coop->own_skip(phase, now);
            show_banner(now);
        }

        g->start_held = start;

        if (start && (g->start_taken || g->coop->start_blocked(phase))) {
            report[0] = static_cast<std::uint8_t>(report[0] | 0x08);
        }

        if (g->coop->hold_input()) {
            report[0] = report[1] = 0xFF;
            std::fill(report + 2, report + 6, static_cast<std::uint8_t>(0x80));
            std::fill(report + 6, report + size, static_cast<std::uint8_t>(0));
        } else if (g->comfort->down()) {
            // Down, waiting for rescue: no moving or acting; Start and Select still work (the
            // pause menu's Team tools has Give up).
            report[0] = static_cast<std::uint8_t>(0xF6u | (report[0] & 0x09u));
            report[1] = 0xFF;
            std::fill(report + 2, report + 6, static_cast<std::uint8_t>(0x80));
            std::fill(report + 6, report + size, static_cast<std::uint8_t>(0));
        } else if (g->coop->movie_press(now)) {
            // The party's skip of the movie playing: Start, for the movie player.
            report[0] = static_cast<std::uint8_t>(report[0] & ~0x08);
        }
    });
    // The game asks the world around each moby's update and of each hit it posts.
    // Two players must not dig one pile: in a member's game a dig waits for the host's word.
    if (mod.has_metal_detect()) {
        mod.on_metal_detect([](void*, int, int key) { return g->world->metal_detect(key) ? 1 : 0; });
    }

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
    g->refused = mod.ui_page(on_refused, OPENRAC_UI_OWN_BACK);
    g->rules = mod.ui_page(on_rules, OPENRAC_UI_OWN_BACK);
    g->remove = mod.ui_page(on_remove, OPENRAC_UI_OWN_BACK);
    g->comfort_page = mod.ui_page(on_comfort);
    g->game->home = &g->home;
    show_home();
    show_code();
    show_direct();
    show_lobby();
    show_browse();
    show_saves();
    show_refused();
    show_rules();
    show_remove();
    show_comfort();
    mod.ui_main_item(OPENRAC_UI_LOAD_GAME, "Multiplayer", g->home);

    // In a level, the pause menu's Options page has a Multiplayer item too (interface 1.9).
    if (mod.has_pause_items()) {
        g->pause_page = mod.ui_page(on_pause);
        show_pause();
        mod.ui_pause_item("Multiplayer", g->pause_page);
    }

    // The names over the others' figures.
    if (mod.has_overlay()) {
        mod.on_overlay(draw_tags);
    }

    // The session follows the server every tick, whether a page shows or not, and the co-op the
    // game.
    mod.on_frame([] {
        // The game's language may change in its options: every page again in the new one.
        if (const mp::Language language = game_language(*g->mod); language != g_language) {
            g_language = language;
            repaint_pages();
        }

        // Every mod has loaded by the first tick: what a party has to agree on is known.
        if (!g->listed_mods) {
            g->listed_mods = true;
            mp::ModList own = g->game->mod_list();
            char digest[16];
            std::snprintf(digest, sizeof digest, "%08x", static_cast<unsigned>(mp::mods_digest(own)));
            g->coop->set_build(
                std::string(session_version()) + " " + own.openrac + " " + own.family + " " + digest
            );
            g->mods->set_own(std::move(own));
        }

        poll();
#ifdef MULTIPLAYER_BOT
        g->bot->poll();
#endif
        const std::uint64_t now = mp::now_ms();
        g->frame_ms = now;
        g->mods->tick(now);
        refresh_mod_code(now);
        remove_banned();

        // A level that does not agree with the host's cannot be shared: out of the party, and why.
        // Nor can a party of another game's (Going Commando's: its description's tag says so).
        const bool other_game = g->session->in_party() && !g->session->is_host()
                                && mp::other_game(g->session->description(), game_tag());

        if (g->world->take_level_differs() || other_game) {
            g->left_differs = true;
            leave_party();
        }

        if (!g->session->in_party()) {
            g->told_refused = false;
        }

        // In a party, the player's link every few seconds: a fresh round trip asked, the last one
        // told to the party (the pause page's pings).
        if (g->session->in_party() && now - g->link_ms >= mp::kLinkMs) {
            g->link_ms = now;
            g->session->sync_clock();
            g->session->report_link(
                g->session->has_clock() ? g->session->rtt_ms() : -1, g->connection->udp()
            );
        }

        // The pause page kept current while it is not open too, so it flies in with the party.
        if (g->pause_page.id() >= 0 && now - g->pause_ms >= mp::kLinkMs / 4) {
            g->pause_ms = now;
            show_pause();
            show_comfort();
        }

        if (g->trace.on()) {
            g->trace.tick(
                now,
                !g->session->in_party() ? "solo" : g->session->is_host() ? "host" : "member"
            );
        }

        tick_auto_join(now);
        g->migrate->tick(now);
        g->coop->tick(now);
        // A host's rule: for a member, what its host said; one who took over keeps it.
        if (g->session->is_host() && !g->migrate->took_over()) {
            g->world->rule_revive(g->revive);
            g->coop->rule_hold_party(g->hold_party);
        }

        g->world->tick(now);
        g->progress->tick(now);
        g->cosave->tick(now, g->coop->playing());
        g->shared->tick(now);
        g->camscripts->tick(now);
        g->comfort->tick(now);
        g->session->flush();

        // The network's own account, for reports of a bad link (README, Verbose log).
        if (g->trace.on() && now - g->net_ms >= kNetTraceMs) {
            g->net_ms = now;
            trace_net();
        }

        if (g->trace.on() && g->trace_file.is_open()) {
            g->trace_file.flush();
        }

        // The party's line over the game: a refused ship travel or skip, a wait for the others.
        show_banner(now);
    });
    mod.info(
        "multiplayer: " + g->session->name() + ", server " + g->host + ":" + std::to_string(g->port)
    );
}

}  // namespace

/* The id and version are the manifest's: the port checks they agree. */
OPENRAC_MOD_CPP("multiplayer", "0.12.1", load)

/* The port calls this at its exit: the connection's thread goes with it. */
extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    // The state exists only if load got as far as making it.
    if (g) {
        g->connection->close();
        g.reset();
    }
}
