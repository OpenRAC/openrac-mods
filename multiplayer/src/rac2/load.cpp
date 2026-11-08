/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Going Commando: online co-op without pages (README.md, Going Commando). Its title has no mod pages,
 * so the party is the settings' (rac2/party.hpp): host one, join one by its code, host directly or
 * join an address. The same server, protocol and session as Ratchet & Clank, its own party (the
 * party's description names the game). The co-op is Ratchet & Clank's (coop.hpp) on the play calls
 * OpenRAC answers for rac2 (rac2/game.hpp): the host's game is the party's, the members are brought
 * into its level from its live save and follow its travels, and each sees the others as figures.
 * Nothing of the world, progress, saves, rescue or the pad: README.md lists what is still to come.
 * No hook point of either game's.
 */
#include "rac2/load.hpp"

#include <ctime>
#include <memory>

#include "mod_state.hpp"
#include "rac2/game.hpp"
#include "rac2/party.hpp"

namespace mpmod {

namespace {

struct Rac2State {
    openrac::Mod* mod = nullptr;
    std::string host;
    int port = mp::kDefaultPort;
    std::unique_ptr<mp::Connection> connection;
    std::unique_ptr<mp::Session> session;
    std::unique_ptr<Rac2Game> game;
    std::unique_ptr<mp::Coop> coop;
    std::unique_ptr<mp::PartyMods> mods;
    std::unique_ptr<mp::PageParty> party;
    mp::Router router;
    mp::Trace trace;
    std::ofstream trace_file;
    std::uint32_t generation = 0;  // the connection's, as the session last saw it
    bool listed_mods = false;      // the mods listed, once they had all loaded
    std::string banner;            // the line shown over the game (play_notice)
};

std::unique_ptr<Rac2State> r2;

void log_line(const std::string& line) {
    r2->mod->info(line);
}

/* Brings the session up to date with the connection: hello on a new connection, the server's
 * messages. Once a game tick. */
void poll_rac2() {
    const std::uint32_t generation = r2->connection->generation();
    const bool connected = r2->connection->state() == mp::Connection::State::kConnected;

    if (connected && generation != r2->generation) {
        r2->generation = generation;
        r2->session->connected();
    } else if (!connected && r2->session->greeted()) {
        r2->session->disconnected();
    }

    for (const mp::Message& m : r2->connection->receive()) {
        r2->session->handle(m);
    }
}

/* The verbose log (trace.hpp), as in Ratchet & Clank: the `verbose_log` setting or OPENRAC_MP_TRACE=1. */
void open_trace(openrac::Mod& mod) {
    const char* env = std::getenv("OPENRAC_MP_TRACE");

    if (!mod.setting<bool>("verbose_log") && !(env != nullptr && std::string(env) == "1")) {
        return;
    }

    std::error_code error;
    std::filesystem::path path =
        std::filesystem::absolute(std::filesystem::current_path(error) / "mp-trace.log", error);
    r2->trace_file.open(path, std::ios::out | std::ios::app);

    if (r2->trace_file) {
        r2->trace.open([](const std::string& line) { r2->trace_file << line << '\n'; });
        mod.info("multiplayer: verbose log on, its [mp-trace] lines in " + path.string());
    } else {
        r2->trace.open([](const std::string& line) { r2->mod->info(line); });
    }

    r2->coop->trace(&r2->trace);
}

}  // namespace

void rac2_load(openrac::Mod& mod) {
    // The co-op is on the play calls: OpenRAC answers them for Going Commando since its play table.
    if (!mod.has_play()) {
        throw openrac::Error(
            "this OpenRAC has no play calls for Going Commando (a newer OpenRAC has them)"
        );
    }

    rac2_unload();
    r2 = std::make_unique<Rac2State>();
    r2->mod = &mod;
    const std::string server = mod.setting<std::string>("server");

    if (!mp::parse_address(server, r2->host, r2->port)) {
        r2->host = "game.openrac.dev";
        r2->port = mp::kDefaultPort;
        mod.warn("server \"" + server + "\" is not host:port; using game.openrac.dev:7770");
    }

    r2->connection = std::make_unique<mp::Connection>();
    r2->session = std::make_unique<mp::Session>(
        *r2->connection, kVersionRac2, player_name(mod.setting<std::string>("name"))
    );
    r2->game = std::make_unique<Rac2Game>(mod);
    r2->coop = std::make_unique<mp::Coop>(*r2->session, *r2->game);
    r2->coop->on_log(log_line);
    r2->mods = std::make_unique<mp::PartyMods>(*r2->session, *r2->game, mp::ModList{});
    r2->mods->on_log([](const std::string& line) { r2->mod->info("multiplayer: " + line); });
    // The host brings into its game only a member whose mods fit (party_mods.hpp).
    r2->coop->set_gate([](std::int32_t member) { return r2->mods->may_play(member); });

    mp::PartyNet net;
    net.server = [] {
        r2->connection->open(r2->host, r2->port);
    };
    net.host_direct = [](const std::string& description, std::string& error) {
        mp::HostConfig config;
        config.description = description;
        r2->connection->set_host_config(config);

        if (r2->connection->start_hosting(mp::kDefaultPort)) {
            return true;
        }

        error = r2->connection->hosting().error;
        return false;
    };
    net.join_direct = [](const std::string& address) {
        return r2->connection->connect_direct(address);
    };
    r2->party = std::make_unique<mp::PageParty>(*r2->session, *r2->coop, *r2->mods, std::move(net));
    r2->party->on_log(log_line);

    // What to do: the settings, or the launcher's word to join a party (modcode.hpp).
    const std::string join =
        mp::take_join_request(mod.plugins_folder(), static_cast<std::int64_t>(std::time(nullptr)));

    if (!join.empty()) {
        r2->party->choose(mp::PartyAction::kJoin, join, "");
    } else {
        r2->party->choose(
            mp::party_action(mod.setting<std::string>("party")),
            mod.setting<std::string>("code"),
            mod.setting<std::string>("address")
        );
    }

    open_trace(mod);
    r2->router.route(mp::Owner::kMods, [](std::int32_t sender, const mp::Bytes& bytes) {
        r2->mods->handle(sender, bytes);
    });
    r2->router.fallback([](std::int32_t sender, const mp::Bytes& bytes) {
        r2->coop->handle(sender, bytes);
    });
    r2->session->on_data([](std::int32_t sender, const mp::Bytes& bytes) {
        r2->router.dispatch(sender, bytes);
    });

    mod.on_frame([] {
        const std::uint64_t now = mp::now_ms();

        // Every mod has loaded by the first tick: what a party has to agree on is known, and the
        // settings' party can begin (its description carries the mods' digest).
        if (!r2->listed_mods) {
            r2->listed_mods = true;
            mp::ModList own = r2->game->mod_list();
            char digest[16];
            std::snprintf(
                digest, sizeof digest, "%08x", static_cast<unsigned>(mp::mods_digest(own))
            );
            r2->coop->set_build(
                std::string(kVersionRac2) + " " + own.openrac + " " + own.family + " " + digest
            );
            r2->mods->set_own(std::move(own));
            r2->party->begin(now);
        }

        poll_rac2();
        r2->mods->tick(now);
        r2->party->tick(now, r2->game->status());
        r2->coop->tick(now);
        r2->session->flush();

        if (r2->trace.on()) {
            r2->trace.tick(
                now,
                !r2->session->in_party() ? "solo"
                : r2->session->is_host() ? "host"
                                         : "member"
            );

            if (r2->trace_file.is_open()) {
                r2->trace_file.flush();
            }
        }

        // The party's line over the game, when it changed.
        if (std::string banner = r2->party->banner(now); banner != r2->banner) {
            r2->banner = banner;
            r2->mod->play_notice(banner);
        }
    });
    mod.info(
        "multiplayer: Going Commando, " + r2->session->name() + ", server " + r2->host + ":"
        + std::to_string(r2->port)
    );
}

void rac2_unload() {
    // The state exists only if a load got as far as making it.
    if (r2) {
        r2->connection->close();
        r2.reset();
    }
}

}  // namespace mpmod
