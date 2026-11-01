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
 *           pause menu's Multiplayer page).
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

#include <memory>

namespace mpmod {

std::unique_ptr<State> g;

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

    return mp::status_text(in);
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

void show_banner(std::uint64_t now) {
    // The party's own line first: a save's only says it went well.
    std::string line = g->coop->banner(now);

    if (line.empty()) {
        line = g->cosave->banner(now);
    }

    if (std::string banner = std::move(line); banner != g->banner) {
        g->banner = banner;
        g->mod->play_notice(banner);
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
    // left it).
    std::string notice = g->session->take_notice();

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
    // The pages are Ratchet & Clank's front end.
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
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
    const std::string server = mod.setting<std::string>("server");

    // An address that does not read is the default's.
    if (!mp::parse_address(server, g->host, g->port)) {
        g->host = "game.openrac.dev";
        g->port = mp::kDefaultPort;
        mod.warn("server \"" + server + "\" is not host:port; using game.openrac.dev:7770");
    }

    g->connection = std::make_unique<mp::Connection>();
    g->session = std::make_unique<mp::Session>(
        *g->connection, kVersion, player_name(mod.setting<std::string>("name"))
    );
    g->mod = &mod;
    g->game = std::make_unique<ModGame>(mod);
    g->coop = std::make_unique<mp::Coop>(*g->session, *g->game);
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
                            [](const mp::Bytes& save) { g->cosave->starting(save); });
    g->coop->on_start_again([] { g->cosave->start_again(); });
    g->shared = std::make_unique<mp::Shared>(*g->session, *g->game, *g->coop);
    g->shared->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    g->coop->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    g->world->on_log([](const std::string& line) { g->mod->info("multiplayer: " + line); });
    // The verbose log (trace.hpp): the `verbose_log` setting, or OPENRAC_MP_TRACE=1. Its lines go
    // to mp-trace.log in the game's working directory (beside run.log), else to the game's log.
    const char* trace_env = std::getenv("OPENRAC_MP_TRACE");

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
        *g->connection, *g->session, *g->game, *g->coop, *g->world, *g->mods, [](const float* at) {
            // The hero's motion position (the bot is a test's).
            openrac::Memory memory = g->mod->memory();
            const openrac::gaddr hero = g->mod->version() == "ntsc" ? 0x0013F3D0u : 0x0013F4D0u;
            for (unsigned i = 0; i < 3; ++i) {
                memory.write<float>(hero + 4u * i, at[i]);
            }
        }, [](int item) {
            // weapon_ammo_counts and hero.pending_item[0] (PAL; the bot is a test's).
            g->game->give_item(item);
            openrac::Memory memory = g->mod->memory();
            memory.write<std::int32_t>(0x0013D530u + 4u * static_cast<unsigned>(item), 40);
            memory.write<std::int32_t>(0x0013F450u + 0x20B8u, item);
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
        }, [] { return mp::tagged_description("New game", mp::mods_digest(g->mods->own())); },
        *g->cosave, [] { return g->game->game_save(); }
    });
#endif
    g->world->trace(&g->trace);
    g->coop->trace(&g->trace);
    g->progress->trace(&g->trace);
    g->shared->trace(&g->trace);
    // The game's DATA, to the owner of its kind (kinds.hpp); what nobody owns goes to the co-op.
    g->router.fallback([](std::int32_t sender, const mp::Bytes& bytes) { g->coop->handle(sender, bytes); });
    g->router.route(mp::Owner::kMods, [](std::int32_t sender, const mp::Bytes& bytes) { g->mods->handle(sender, bytes); });
    g->router.route(mp::Owner::kWorld, [](std::int32_t sender, const mp::Bytes& bytes) { g->world->handle(sender, bytes); });
    g->router.route(mp::Owner::kProgress, [](std::int32_t sender, const mp::Bytes& bytes) { g->progress->handle(sender, bytes); });
    g->router.route(mp::Owner::kSave, [](std::int32_t sender, const mp::Bytes& bytes) { g->cosave->handle(sender, bytes); });
    g->router.route(mp::Owner::kShare, [](std::int32_t sender, const mp::Bytes& bytes) { g->shared->handle(sender, bytes); });
    g->session->on_data([](std::int32_t sender, const mp::Bytes& bytes) { g->router.dispatch(sender, bytes); });
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
    // The ship's planet select: the host's travel takes the party along, a member's to another
    // planet lands the ship again where it is (coop.hpp: only the host flies).
    mod.hook<openrac::rac1::Ship_TravelTo>([](auto original, std::int32_t dest) {
        const int here = g->game->status().level;
        original(g->coop->ship_travel(dest, here) ? dest : here);
    });
    // The first pad: the host's Start in a scene or a movie skips it for the party (a member's is
    // taken out: only the host skips), a party's skip presses Start for a movie playing here, and
    // the scene barrier holds the control (coop.hpp). The report: buttons in
    // bytes 0 and 1 (a bit clear while held; Start 0x08), the sticks, then the pressures.
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

        // Start pressed in a scene or a movie: the host's skips it for the whole party; a member's
        // does nothing here but say so (a movie has no game ticks: the banner is shown from here).
        const bool start = (report[0] & 0x08) == 0;
        const int phase = g->game->status().phase;

        if (start && !g->start_held) {
            g->coop->own_skip(phase, now);
            show_banner(now);
        }

        g->start_held = start;

        if (start && g->coop->start_blocked(phase)) {
            report[0] = static_cast<std::uint8_t>(report[0] | 0x08);
        }

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
    g->refused = mod.ui_page(on_refused, OPENRAC_UI_OWN_BACK);
    g->rules = mod.ui_page(on_rules, OPENRAC_UI_OWN_BACK);
    g->remove = mod.ui_page(on_remove, OPENRAC_UI_OWN_BACK);
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
        // Every mod has loaded by the first tick: what a party has to agree on is known.
        if (!g->listed_mods) {
            g->listed_mods = true;
            mp::ModList own = g->game->mod_list();
            char digest[16];
            std::snprintf(digest, sizeof digest, "%08x", static_cast<unsigned>(mp::mods_digest(own)));
            g->coop->set_build(
                std::string(kVersion) + " " + own.openrac + " " + own.family + " " + digest
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
        if (g->world->take_level_differs()) {
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
        }

        if (g->trace.on()) {
            g->trace.tick(
                now,
                !g->session->in_party() ? "solo" : g->session->is_host() ? "host" : "member"
            );
        }

        g->coop->tick(now);
        g->world->tick(now);
        g->progress->tick(now);
        g->cosave->tick(now, g->coop->playing());
        g->shared->tick(now);
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
OPENRAC_MOD_CPP("multiplayer", "0.9.0", load)

/* The port calls this at its exit: the connection's thread goes with it. */
extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    // The state exists only if load got as far as making it.
    if (g) {
        g->connection->close();
        g.reset();
    }
}
