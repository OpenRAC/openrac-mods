/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's shared state and the functions its files call each other by: the
 * entry and glue (multiplayer.cpp), the pages (pages_*.cpp) and the game adapter
 * (game_adapter.hpp). All of it is mod-internal: nothing here is the mod's interface.
 */
#ifndef MULTIPLAYER_MOD_STATE_H
#define MULTIPLAYER_MOD_STATE_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bot.hpp"
#include "connection.hpp"
#include "coop.hpp"
#include "cosave.hpp"
#include "direct.hpp"
#include "menu.hpp"
#include "modcode.hpp"
#include "nametags.hpp"
#include "progress.hpp"
#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"
#include "party_mods.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "trace.hpp"
#include "world.hpp"

#include "game_adapter.hpp"

namespace mpmod {

/* The version HELLO gives the server: the mod's (at most 32 characters). */
constexpr const char* kModVersion = "0.9.0";
constexpr const char* kVersion = "rac1-multiplayer/0.9.0";


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
constexpr int kRulesItem = 2;

/* The Refused page's button after Leave party, when the host has a mod code. */
constexpr int kRefusedOpenItem = 1;
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
/* After the card's slots, Continue as host: the party's own saves (cosave.hpp). */
constexpr int kContinueItem = kSaveSlots + 1;

enum class SavesFor {
    kCreate,
    kHost
};

/* What the Direct page shows: its two buttons, or Join by address's rows. */
enum class DirectView {
    kMenu,
    kJoin
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
    std::unique_ptr<mp::Progress> progress;
    std::unique_ptr<mp::PartyMods> mods;
    std::unique_ptr<mp::CoSave> cosave;
    mp::Router router;             // where a received DATA goes (kinds.hpp)
    bool listed_mods = false;      // the mods listed, once they had all loaded
    bool told_refused = false;     // the page of why this party's game is refused was shown
    bool refused_in_party = false;  // that page shows for a party, not for a solo mod
#ifdef MULTIPLAYER_BOT
    std::unique_ptr<mp::Bot> bot;  // the end-to-end tests' driver (bot.hpp)
#endif
    mp::Trace trace;            // the verbose log (trace.hpp), off unless asked for
    std::ofstream trace_file;   // mp-trace.log, where it goes
    openrac::Mod* mod = nullptr;
    std::uint32_t generation = 0;  // the connection's, as the session last saw it
    openrac::UiPage home, code_page, direct_page, lobby, browse, saves, refused, rules;
    std::uint64_t listed_ms = 0;   // when Browse last asked for the list
    bool home_greeted = false;     // whether Home last saw the server's hello
    std::uint64_t frame_ms = 0;    // when the game last ticked (on_frame)
    bool start_held = false;       // Start as the pad last reported it
    SavesFor saves_for = SavesFor::kCreate;
    bool saves_coop = false;       // the Saves page shows the party's own saves
    bool mod_code_long = false;  // the launcher's file matches, and has no short code
    std::uint64_t mod_code_ms = 0;  // when it was last read
    std::vector<int> code = std::vector<int>(mp::kCodeLength, 0);  // indices into kCodeAlphabet
    std::string notice;  // the last refusal or news, until the next action
    std::string banner;  // the line shown over the game (play_notice)
    DirectView view = DirectView::kMenu;
    mp::DirectAddress address;  // Join by address's, the last joined kept for the game
    mp::Acceleration acceleration;
    std::string direct_error;  // why Host directly or Join failed, on the Direct page
    openrac::UiPage pause_page;  // the pause menu's Multiplayer page (Options, in a level)
    int pause_leave = -1;        // its Leave party item, -1 out of a party
    int pause_rules = -1;        // its Party rules item, -1 out of a party
    int pause_save = -1;         // its Save the party item, -1 out of a party's game
    int pause_go = -1;           // its Go to host item, -1 when there is none
    std::uint64_t link_ms = 0;   // when the player's link was last reported to the party
    std::uint64_t net_ms = 0;    // when the verbose log last gave the network's counts
    std::uint64_t pause_ms = 0;  // when the pause page was last set from the frame
    bool leaving = false;        // the player leaves the party (Leave, Stop hosting) this tick
    std::string leave_reason;    // why the party went, for Home when it next shows
    bool left_differs = false;   // left because the host's level does not agree with this game's
};

extern std::unique_ptr<State> g;

// ---- pages_home.cpp ----
std::string code_text();
void show_home();
void show_code();
openrac::UiItem ring_row(const std::string& label, int value, const mp::Ring& ring);
void show_direct();
void show_pause();
void leave_party();
void on_pause(const openrac::UiPage& page, const openrac::UiEvent& e);
void back_to_server();
void stop_server_loop();
void on_home(const openrac::UiPage& page, const openrac::UiEvent& e);
void on_code(const openrac::UiPage& page, const openrac::UiEvent& e);
void step_address(int item, int choice);
void switch_view(const openrac::UiPage& page, DirectView view, int item);
void on_direct(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_browse.cpp ----
void show_browse();
void on_browse(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_saves.cpp ----
void show_saves();
void on_saves(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_lobby.cpp ----
bool refused();
bool checking();
openrac::UiContent lobby_content();
void show_lobby();
void on_lobby(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_refused.cpp ----
openrac::UiContent refused_content();
void show_refused();
void on_refused(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_rules.cpp ----
openrac::UiContent rules_content();
void show_rules();
void on_rules(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- multiplayer.cpp ----
void trace_net();
std::string player_name(const std::string& setting);
std::string status();
void show_banner(std::uint64_t now);
void poll();

}  // namespace mpmod

#endif
