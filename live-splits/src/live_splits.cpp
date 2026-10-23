/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: the live splits, a mod of OpenRAC's native port (README.md).
 *
 * The mod watches which planet the game is playing and tells a running LiveSplit about it, so that
 * a speedrunner does not have to touch the timer: one split a planet, the run started at the first
 * planet.
 *
 * How the game is read:
 *
 *   - Level_LoadGameplay (the level's gameplay file is loaded): the planet is reached. The game
 *     loads it again when Ratchet dies, which is not a new planet: the mod keeps the planet it
 *     last saw.
 *   - the level the port has loaded (openrac_api.loaded_level): none means the game is in the
 *     galaxy map, so the planet before it was left.
 *
 * Nothing of the game is changed, and nothing of the network is in the game's thread: the client
 * (client.cpp) has a thread of its own, and the game's thread only puts the commands in its queue.
 */
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "client.hpp"
#include "levels.hpp"
#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"
#include "splits.hpp"

namespace {

/* What the mod reads of each disc: the game's time base (the seconds a tick lasts, D_0015EE7C on
 * PAL and D_0015ED7C on NTSC-U), which the port's own 60 Hz keeps at a sixtieth of a second. */
struct live_splits_disc {
    const char* version; /* openrac_api.version */
    std::uint32_t tick_seconds;
};

const live_splits_disc k_live_splits_discs[] = {
    {"pal", 0x0015EE7Cu},
    {"ntsc", 0x0015ED7Cu},
};

const live_splits_disc* g_disc = nullptr;
std::unique_ptr<livesplit::Client> g_client;
std::unique_ptr<livesplit::Splits> g_splits;
bool g_game_time = false;  /* the game's own time goes to LiveSplit */
bool g_trace = false;      /* every split is logged */
int g_last_level = -1;     /* the level the game was playing */
bool g_loaded = false;     /* the level's gameplay was loaded this tick */
std::uint64_t g_ticks = 0; /* the ticks since the run began */
bool g_running = false;    /* the run began */
std::string g_state;       /* the client's state, as it was last logged */

/* The seconds a tick of the game lasts: its own time base, which the port keeps at 60 Hz. */
float tick_seconds(openrac::Mod& mod) {
    if (g_disc != nullptr) {
        const openrac::gaddr address = mod.relocate(g_disc->tick_seconds);
        const float value = mod.memory().read<float>(address);

        if (value > 0.0f) {
            return value;
        }
    }

    return 1.0f / 60.0f;
}

/* The game's own time since the run began, for LiveSplit's game time. */
void send_game_time(openrac::Mod& mod) {
    const double seconds = static_cast<double>(g_ticks) * tick_seconds(mod);
    g_client->send("setgametime " + livesplit::format_time(seconds));
}

/* The commands the state machine asks for, in order. */
void send_actions(openrac::Mod& mod, const std::vector<livesplit::Action>& actions) {
    for (const livesplit::Action action : actions) {
        switch (action) {
            case livesplit::Action::kReset:
                g_client->send("reset");
                g_ticks = 0;
                break;
            case livesplit::Action::kStart:
                g_client->send("starttimer");
                g_running = true;
                break;
            case livesplit::Action::kSplit:
                if (g_game_time) {
                    send_game_time(mod);
                }

                g_client->send("split");
                break;
        }
    }
}

/* The level the game plays now: none (the galaxy map) or a planet. */
int level_of(openrac::Mod& mod) {
    const int level = mod.loaded_level();

    if (level < 0 || level >= livesplit::kPlanetCount) {
        return -1;
    }

    return level;
}

void load(openrac::Mod& mod) {
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }

    const std::string version(mod.version());

    for (const live_splits_disc& disc : k_live_splits_discs) {
        if (version == disc.version) {
            g_disc = &disc;
            break;
        }
    }

    if (g_disc == nullptr) {
        throw openrac::Error("this mod does not know the " + version + " disc");
    }

    const std::string split = mod.setting<std::string>("split");
    const std::string start = mod.setting<std::string>("start");
    const std::string protocol = mod.setting<std::string>("protocol");
    const auto when =
        split == "left" ? livesplit::SplitWhen::kLeft : livesplit::SplitWhen::kReached;
    const auto who = start == "hand" ? livesplit::StartWho::kRunner : livesplit::StartWho::kMod;
    const auto proto = protocol == "tcp"         ? livesplit::Protocol::kTcp
                       : protocol == "websocket" ? livesplit::Protocol::kWebSocket
                                                 : livesplit::Protocol::kAuto;
    const int port = static_cast<int>(mod.setting<long long>("port"));
    g_game_time = mod.setting<bool>("game_time");
    g_trace = mod.setting<bool>("trace");

    if (g_client) {
        g_client->stop();
    }

    g_splits = std::make_unique<livesplit::Splits>(when, who);
    g_client = std::make_unique<livesplit::Client>("127.0.0.1", port, proto);
    g_client->start();
    g_last_level = -1;
    g_loaded = false;
    g_ticks = 0;
    g_running = false;
    g_state.clear();

    /* The level's gameplay is loaded: the planet is reached. */
    mod.hook<openrac::rac1::Level_LoadGameplay>([](auto original, std::int32_t full) {
        g_loaded = true;
        return original(full);
    });

    /* Once a tick: the planet the game is on, and the clock the mod keeps of its own. */
    mod.on_frame([&mod]() {
        const int level = level_of(mod);

        if (level != g_last_level) {
            if (g_last_level >= 0 && level < 0) {
                /* The game left the planet for the galaxy map. */

                if (g_trace) {
                    mod.info("left " + std::string(livesplit::kPlanetNames[g_last_level]));
                }
                send_actions(mod, g_splits->left(g_last_level));
            }
            g_last_level = level;
        }

        if (g_loaded) {
            g_loaded = false;

            if (g_trace && level >= 0) {
                mod.info("reached " + std::string(livesplit::kPlanetNames[level]));
            }
            const std::vector<livesplit::Action> actions = g_splits->reached(level);

            if (g_splits->began()) {
                /* The run begins (or begins again): the game's own time starts here too. */
                g_ticks = 0;
                g_running = true;
            }
            send_actions(mod, actions);
        }

        if (g_running) {
            ++g_ticks;

            if (g_game_time && (g_ticks % 30u) == 0u) {
                send_game_time(mod);
            }
        }
        /* What the client knows of the connection, when it changes. */
        const std::string state = g_client->state();

        if (state != g_state) {
            g_state = state;
            mod.info(state);
        }
    });

    mod.info(
        "live splits: "
        + std::string(
            when == livesplit::SplitWhen::kLeft ? "splitting when a planet is left"
                                                : "splitting when a planet is reached"
        )
        + ", the timer is "
        + (who == livesplit::StartWho::kMod ? "started by the mod" : "started by hand")
        + ", on port " + std::to_string(port)
    );
}

}  // namespace

/* The id and version are the manifest's: the port checks they agree. */
OPENRAC_MOD_CPP("live-splits", "1.0.0", load)

/* The port calls this at its exit: the client's thread goes with it. */
extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    if (g_client) {
        g_client->stop();
        g_client.reset();
    }

    g_splits.reset();
}
