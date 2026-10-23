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
 *
 * Settings (openrac-mod.json):
 *   split      (choice: reached | left, default reached)  When the timer is split. Reached: when the
 *              planet's level is loaded, so the flight and the loading are in the segment before.
 *              Left: when the game goes back to the galaxy map, so they are in the segment after.
 *   start      (choice: first | hand, default first)  Who starts the timer. First: the mod resets
 *              LiveSplit and starts it when the run's first planet is loaded. Hand: the mod never
 *              resets or starts the timer; the runner does, before the first split.
 *   game_time  (bool, default on)  Send the game's own time (its ticks) to LiveSplit, for the Game
 *              Time timing method, counted from the first planet.
 *   port       (int, default 27500)  The port LiveSplit's server listens on.
 *   protocol   (choice: auto | tcp | websocket, default auto)  How the mod talks to LiveSplit's
 *              server; auto tries the TCP server and, when that is not there, the WebSocket one.
 *   trace      (bool, default off)  For bug reports: a log line for every planet reached and left,
 *              and for the timer the mod starts.
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

// ---- Game addresses ----

/* What the mod reads of each disc: the game's time base (the seconds a tick lasts, D_0015EE7C on
 * PAL and D_0015ED7C on NTSC-U), which the port's own 60 Hz keeps at a sixtieth of a second. */
struct live_splits_disc {
    const char* version; /* openrac_api.version */
    std::uint32_t tick_seconds;
};

/* The PAL and NTSC-U discs: the address of the float holding the seconds a tick lasts, in the
 * executable (seen in game code). */
const live_splits_disc k_live_splits_discs[] = {
    {"pal", 0x0015EE7Cu},
    {"ntsc", 0x0015ED7Cu},
};

// ---- Constants ----

/* The host LiveSplit's server is looked for on: this machine (assumed). */
constexpr const char* k_livesplit_host = "127.0.0.1";

/* The seconds a tick lasts when the game's own value cannot be read: the port's 60 Hz (assumed). */
constexpr float k_fallback_tick_seconds = 1.0f / 60.0f;

/* How many ticks lie between two game-time updates sent to LiveSplit: half a second at 60 Hz
 * (assumed). */
constexpr std::uint64_t k_game_time_period_ticks = 30u;

// ---- State ----

/* All of this is touched only by the game's thread (load, the hook and the tick), except that the
 * client has its own thread behind g_client. */

/* The running disc's addresses, or null before load matches one. */
const live_splits_disc* g_disc = nullptr;

/* The connection to LiveSplit; its thread is the only other one. */
std::unique_ptr<livesplit::Client> g_client;

/* The state machine that turns planets into commands. */
std::unique_ptr<livesplit::Splits> g_splits;

/* The game_time setting: the game's own time goes to LiveSplit. */
bool g_game_time = false;

/* The trace setting: every split is logged. */
bool g_trace = false;

/* The level the game was playing at the last tick, -1 for none. */
int g_last_level = -1;

/* Whether the level's gameplay was loaded since the last tick; set by the hook, cleared by the tick. */
bool g_loaded = false;

/* The ticks since the run began. */
std::uint64_t g_ticks = 0;

/* Whether the run began, so the game's time counts. */
bool g_running = false;

/* The client's state, as it was last logged, so a change is logged once. */
std::string g_state;

// ---- Helpers ----

/* The seconds a tick of the game lasts: its own time base, which the port keeps at 60 Hz. Falls
 * back to a sixtieth when the disc is unknown or the value is not positive. */
float tick_seconds(openrac::Mod& mod) {
    // Before a disc is matched there is no address to read.
    if (g_disc == nullptr) {
        return k_fallback_tick_seconds;
    }

    const openrac::gaddr address = mod.relocate(g_disc->tick_seconds);
    const float value = mod.memory().read<float>(address);

    // A value of zero or less would stop or reverse the clock; use the fallback instead.
    if (value > 0.0f) {
        return value;
    }

    return k_fallback_tick_seconds;
}

/* The game's own time since the run began, for LiveSplit's game time. */
void send_game_time(openrac::Mod& mod) {
    const double seconds = static_cast<double>(g_ticks) * tick_seconds(mod);
    g_client->send("setgametime " + livesplit::format_time(seconds));
}

/* The commands the state machine asks for, in order. Call it from the game's thread. */
void send_actions(openrac::Mod& mod, const std::vector<livesplit::Action>& actions) {
    // One command per action, in the order the state machine gave them.
    for (const livesplit::Action action : actions) {
        // Each action is one LiveSplit command, with the mod's own clock kept in step.
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
                // LiveSplit's game time is told just before the split so the split carries it.
                if (g_game_time) {
                    send_game_time(mod);
                }

                g_client->send("split");
                break;
        }
    }
}

/* The level the game plays now: -1 for none (the galaxy map) or a planet. */
int level_of(openrac::Mod& mod) {
    const int level = mod.loaded_level();

    // A level outside the planets (none, or one the mod has no name for) counts as none.
    if (level < 0 || level >= livesplit::kPlanetCount) {
        return -1;
    }

    return level;
}

/* Sets g_disc from the running disc's version, or throws when the mod does not know it. */
void find_disc(openrac::Mod& mod) {
    const std::string version(mod.version());

    // The first row whose version matches names the disc's addresses.
    for (const live_splits_disc& disc : k_live_splits_discs) {
        // A match: stop looking.
        if (version == disc.version) {
            g_disc = &disc;
            break;
        }
    }

    // An unknown disc has no address for the time base; refuse to run.
    if (g_disc == nullptr) {
        throw openrac::Error("this mod does not know the " + version + " disc");
    }
}

// ---- Main logic ----

/* The game left a planet for the galaxy map: splits when the setting says to split on leaving.
 * Call it from the tick. */
void on_planet_left(openrac::Mod& mod, int planet) {
    // The trace setting asks for a log line for every planet left.
    if (g_trace) {
        mod.info("left " + std::string(livesplit::kPlanetNames[planet]));
    }

    send_actions(mod, g_splits->left(planet));
}

/* The level's gameplay was loaded: the planet is reached, and the run may begin here. Call it from
 * the tick. */
void on_planet_reached(openrac::Mod& mod, int level) {
    // The trace setting asks for a log line for every planet reached.
    if (g_trace && level >= 0) {
        mod.info("reached " + std::string(livesplit::kPlanetNames[level]));
    }

    const std::vector<livesplit::Action> actions = g_splits->reached(level);

    // The run begins (or begins again): the game's own time starts here too.
    if (g_splits->began()) {
        g_ticks = 0;
        g_running = true;
    }

    send_actions(mod, actions);
}

/* Once a tick: the planet the game is on, and the clock the mod keeps of its own. */
void tick(openrac::Mod& mod) {
    const int level = level_of(mod);

    // The game moved to another level (or none) since the last tick.
    if (level != g_last_level) {
        // The game left a planet for the galaxy map: the planet was left.
        if (g_last_level >= 0 && level < 0) {
            on_planet_left(mod, g_last_level);
        }

        g_last_level = level;
    }

    // The hook saw the level's gameplay loaded since the last tick: the planet is reached.
    if (g_loaded) {
        g_loaded = false;
        on_planet_reached(mod, level);
    }

    // The game's time counts only once the run has begun.
    if (g_running) {
        ++g_ticks;

        // Game time goes to LiveSplit every so often, not every tick, to keep the socket quiet.
        if (g_game_time && (g_ticks % k_game_time_period_ticks) == 0u) {
            send_game_time(mod);
        }
    }

    /* What the client knows of the connection, when it changes. */
    const std::string state = g_client->state();

    // Log the connection's state only when it differs from the last one logged.
    if (state != g_state) {
        g_state = state;
        mod.info(state);
    }
}

// ---- Entry point ----

/* The port calls this when it loads the library: checks the game and the disc, reads the settings,
 * starts the client and registers the hook and the tick. Throws openrac::Error to refuse to run. */
void load(openrac::Mod& mod) {
    // The addresses are those of Ratchet & Clank; another game would be read at the wrong places.
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }

    find_disc(mod);

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

    // A client left from an earlier load would keep its thread and socket; stop it first.
    if (g_client) {
        g_client->stop();
    }

    g_splits = std::make_unique<livesplit::Splits>(when, who);
    g_client = std::make_unique<livesplit::Client>(k_livesplit_host, port, proto);
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

    mod.on_frame([&mod]() { tick(mod); });

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
    // The client exists only if load got as far as making it.
    if (g_client) {
        g_client->stop();
        g_client.reset();
    }

    g_splits.reset();
}
