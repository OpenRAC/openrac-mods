// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The multiplayer mod's shared state and the functions its files call each other by: the
// entry and glue (multiplayer.cpp), the pages (pages_*.cpp) and the game adapter
// (game_adapter.hpp). All of it is mod-internal: nothing here is the mod's interface.

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
#include <utility>
#include <vector>

#include "bot.hpp"
#include "camscripts.hpp"
#include "comfort.hpp"
#include "connection.hpp"
#include "coop.hpp"
#include "cosave.hpp"
#include "direct.hpp"
#include "game_adapter.hpp"
#include "menu.hpp"
#include "migrate.hpp"
#include "modcode.hpp"
#include "nametags.hpp"
#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"
#include "pad_intent.hpp"
#include "party_mods.hpp"
#include "progress.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "shared.hpp"
#include "trace.hpp"
#include "words.hpp"
#include "world.hpp"

namespace mpmod {

// The version HELLO gives the server: the mod's (at most 32 characters).
constexpr const char* kModVersion = "0.13.0";
// The version HELLO gives the server for Ratchet & Clank: the mod's, with the game's name.
constexpr const char* kVersion = "rac1-multiplayer/0.13.0";
// Going Commando's: its parties are its own (a party's description names the game).
constexpr const char* kVersionRac2 = "rac2-multiplayer/0.13.0";

// The question's tag: leaving the party (or hosting it).
constexpr std::uint32_t kAskLeave = 1;

// Home's buttons.
constexpr int kBrowseItem = 0;  // Browse games
constexpr int kCreateItem = 1;  // Create match
constexpr int kCodeItem = 2;    // Join by code
constexpr int kDirectItem = 3;  // Direct connect

// The Lobby's buttons, then its lines; the Code page's characters, then Join.
constexpr int kReadyItem = 0;  // Ready, or Start for the host
constexpr int kLeaveItem = 1;  // Leave
constexpr int kRulesItem = 2;  // Party rules

// The Refused page's buttons after Leave party, when the host has a mod code.
constexpr int kRefusedOpenItem = 1;  // Open the mod code in the launcher
constexpr int kRefusedCopyItem = 2;  // Copy mod code

// The Rules page's Copy mod code, right after Back; the Remove page's buttons.
constexpr int kRulesBackItem = 0;                             // Back
constexpr int kRemoveItem = 0;                                // Remove
constexpr int kBanItem = 1;                                   // Ban from save
constexpr int kRemoveBackItem = 2;                            // Back
constexpr int kJoinItem = static_cast<int>(mp::kCodeLength);  // Join, after a row per character

// The Direct page's buttons; Join by address's rows: four octets, the port by one and by a
// hundred, then Join.
constexpr int kHostItem = 0;         // Host directly
constexpr int kByAddressItem = 1;    // Join by address
constexpr int kPortItem = 4;         // Port, by one
constexpr int kPortCoarseItem = 5;   // Port, by a hundred
constexpr int kJoinAddressItem = 6;  // Join

// Browse's Refresh, its party rows after it (as many as fit), and how often the list is asked
// again while it shows.
constexpr int kRefreshItem = 0;  // Refresh
// The most party rows Browse shows: the page's item limit less two.
constexpr std::size_t kBrowseRows = OPENRAC_UI_ITEMS_MAX - 2;
constexpr std::uint64_t kListMs = 3000;  // Milliseconds between list requests

// The Saves page's New game, the card's slots after it; what the save chosen is for.
constexpr int kNewGameItem = 0;  // New game
constexpr int kSaveSlots = 5;    // The card's save slots (documented: cosave.hpp)
// After the card's slots, Continue as host: the party's own saves (cosave.hpp).
constexpr int kContinueItem = kSaveSlots + 1;

// What the save chosen on the Saves page is for.
enum class SavesFor {
    kCreate,  // Create match, on the server
    kHost     // Host directly
};

// What the Direct page shows: its two buttons, or Join by address's rows.
enum class DirectView {
    kMenu,  // Host directly and Join by address
    kJoin   // Join by address's rows
};

// ---- State ----

// A save slot as the Saves page last read it from the card.
struct SlotRow {
    openrac_save_info info{};  // What the card says about the slot.
    bool there = false;        // Whether the slot holds a save.
};

// The mod's one state, made by load and dropped by unload: the parts of a party, the pages and the
// flags between them. Everything in it is used from the port's calls into the mod (hooks, pad,
// frame and page events); the connection's own thread never touches it.
struct State {
    // The server's host name, from the `server` setting.
    std::string host;
    // The server's port, from the same setting.
    int port = mp::kDefaultPort;
    // The link to the server or to a direct party: its thread connects and reads, the game's side
    // queues and takes messages (connection.hpp).
    std::unique_ptr<mp::Connection> connection;
    // The player's session and party on that link (session.hpp).
    std::unique_ptr<mp::Session> session;
    // What the mod asks of the game through the port (game_adapter.hpp; Going Commando has its
    // own).
    std::unique_ptr<ModGame> game;
    // Shows the other players' Ratchets and starts the game for the party (coop.hpp).
    std::unique_ptr<mp::Coop> coop;
    // The shared world: the host runs the level and the members show it (world.hpp).
    std::unique_ptr<mp::World> world;
    // The party's shared items, gold bolts and planets (progress.hpp).
    std::unique_ptr<mp::Progress> progress;
    // The party's shared gold weapons, skill points, story flags and special items (shared.hpp).
    std::unique_ptr<mp::Shared> shared;
    // Camera scripts shown to the players near them (camscripts.hpp).
    std::unique_ptr<mp::CamScripts> camscripts;
    // The mods the party has to agree on, and who may play (party_mods.hpp).
    std::unique_ptr<mp::PartyMods> mods;
    // The party's own save slots on each card (cosave.hpp).
    std::unique_ptr<mp::CoSave> cosave;
    // A lost connection, and the party going on without its host (migrate.hpp).
    std::unique_ptr<mp::Migrate> migrate;
    std::unique_ptr<mp::Comfort> comfort;  // rescue, rally points and Team tools (comfort.hpp)
    mp::Router router;                     // where a received DATA goes (kinds.hpp)
    bool listed_mods = false;              // the mods listed, once they had all loaded
    bool told_refused = false;             // the page of why this party's game is refused was shown
    bool refused_in_party = false;         // that page shows for a party, not for a solo mod
    std::string join_party;                // the launcher's word: join this party (modcode.hpp)
    // When the launcher's join request began its wait, 0 before that.
    std::uint64_t join_ms = 0;
    // Whether the join has been asked of the server yet.
    bool join_asked = false;
    bool start_taken = false;  // this press of Start said ready for the host's ship
    bool hold_party = false;   // the host's rule (the `hold_party` setting)
    bool revive = false;       // the host's rule (the `revive` setting)
#ifdef MULTIPLAYER_BOT
    std::unique_ptr<mp::Bot> bot;  // the end-to-end tests' driver (bot.hpp)
#endif
    mp::Trace trace;           // the verbose log (trace.hpp), off unless asked for
    std::ofstream trace_file;  // mp-trace.log, where it goes
    // The port's handle for this mod; the port owns it and keeps it until the library unloads.
    openrac::Mod* mod = nullptr;
    bool rac2 = false;             // Going Commando (rac2/game.hpp): its parties, its version
    std::uint32_t generation = 0;  // the connection's, as the session last saw it
    // The mod's pages in the game's menus, made in load.
    openrac::UiPage home, code_page, direct_page, lobby, browse, saves, refused, rules, remove;
    openrac::UiPage comfort_page;  // Team tools, from the lobby and the pause page
    int lobby_tools = -1;          // the Lobby's Team tools item
    std::uint64_t listed_ms = 0;   // when Browse last asked for the list
    bool home_greeted = false;     // whether Home last saw the server's hello
    std::uint64_t frame_ms = 0;    // when the game last ticked (on_frame)
    bool start_held = false;       // Start as the pad last reported it
    // What the Saves page's choice is for.
    SavesFor saves_for = SavesFor::kCreate;
    bool saves_coop = false;     // the Saves page shows the party's own saves
    bool mod_code_long = false;  // the launcher's file matches, and has no short code
    bool code_copied = false;    // the shown mod code is on the clipboard: its button says so
    int rules_copy = -1;         // the Rules page's Copy mod code item, -1 with no code
    int rules_lift = -1;         // the Rules page's Lift bans item, -1 with none to lift
    std::vector<std::pair<int, std::int32_t>> lobby_rows;  // the Lobby's member rows: item, member
    std::int32_t remove_member = 0;                        // who the Remove page is about
    std::uint64_t mod_code_ms = 0;                         // when it was last read
    std::vector<int> code = std::vector<int>(mp::kCodeLength, 0);  // indices into kCodeAlphabet
    std::string notice;   // the last refusal or news, until the next action
    std::string refusal;  // the server's refusal of a join into another game's party, for Refused
    std::string banner;   // the line shown over the game (play_notice)
    // Which view the Direct page shows.
    DirectView view = DirectView::kMenu;
    mp::DirectAddress address;  // Join by address's, the last joined kept for the game
    // Speeds up a held step on Join by address's rows (direct.hpp).
    mp::Acceleration acceleration;
    std::string direct_error;    // why Host directly or Join failed, on the Direct page
    openrac::UiPage pause_page;  // the pause menu's Multiplayer page (Options, in a level)
    int pause_leave = -1;        // its Leave party item, -1 out of a party
    int pause_rules = -1;        // its Party rules item, -1 out of a party
    int pause_save = -1;         // its Save the party item, -1 out of a party's game
    int pause_go = -1;           // its Go to host item, -1 when there is none
    int pause_tools = -1;        // its Team tools item, -1 out of a party (or no room)
    std::vector<std::pair<int, std::int32_t>> pause_rows;  // its member rows: item, member
    std::uint64_t link_ms = 0;   // when the player's link was last reported to the party
    std::uint64_t net_ms = 0;    // when the verbose log last gave the network's counts
    std::uint64_t pause_ms = 0;  // when the pause page was last set from the frame
    bool leaving = false;        // the player leaves the party (Leave, Stop hosting) this tick
    std::string leave_reason;    // why the party went, for Home when it next shows
    mp::Language language = mp::Language::kEnglish;  // The game's language, read again each tick.
    mp::Intent intent = mp::Intent::Unknown;         // What the first pad's last report says.
    std::vector<SlotRow> slots = std::vector<SlotRow>(kSaveSlots);         // The card's saves.
    std::vector<SlotRow> coop_slots = std::vector<SlotRow>(mp::kCoSlots);  // The party's saves.
    bool any_coop = false;      // Whether any of the party's own saves exists.
    bool left_differs = false;  // left because the host's level does not agree with this game's
};

// The mod's state: made by load, dropped by unload, null before and after.
extern std::unique_ptr<State> g;

// The game's language (words.hpp), read again each tick; only the UI thread uses it. Protocol and
// session messages and the log keep their English. English before the mod has loaded.
inline mp::Language language() {
    return g ? g->language : mp::Language::kEnglish;
}

// A page's words in the game's language; what words.hpp has no row for passes through.
inline const char* text(const char* english) {
    return mp::translated(english, language());
}

// The language the game is set to (PAL's five; English for any other disc).
mp::Language game_language(const openrac::Mod& mod);

// A page set with its labels fitted to the port's bytes (words.hpp's fit_ui_text), so an accented
// name is never cut in half.
void set_content(const openrac::UiPage& page, openrac::UiContent& content);

// The game this run plays, as a party's description names it (party_mods.hpp), and the session's
// version (each game's parties are its own).
const char* game_tag();
const char* session_version();

// The member whose row was chosen among `rows`, 0 for no such event.
inline std::int32_t row_member(
    const std::vector<std::pair<int, std::int32_t>>& rows, const openrac::UiEvent& e
) {
    // Only a choice of a row names a member, not the cursor moving over it.
    if (e.is(OPENRAC_UI_CHOOSE)) {
        // The rows are looked up by item number; an item that is no member's row gives none.
        for (const auto& [item, member] : rows) {
            if (item == e.item()) {
                return member;
            }
        }
    }

    return 0;
}

// ---- pages_home.cpp ----
// Returns the party code the Code page shows, one character per slot.
std::string code_text();
// Sets Home's page content from the state.
void show_home();
// Sets the Code page's content from the state.
void show_code();
// Returns a choice row named `label` that shows `ring`'s window of values around `value`, the
// current one in the middle.
openrac::UiItem ring_row(const std::string& label, int value, const mp::Ring& ring);
// Sets the Direct page's content: its buttons, or Join by address's rows.
void show_direct();
// Sets the pause menu's Multiplayer page from the party, and notes the item number of each of its
// buttons for on_pause.
void show_pause();
// Leaves the party: a host of a direct party stops hosting, anyone else leaves. The party's last
// save and ledger are handed over first.
void leave_party();
// Handles an event of the pause menu's Multiplayer page, then sets it again. `e` is the event as
// the player meant it.
void on_pause(const openrac::UiPage& page, const openrac::UiEvent& e);
// Links to the configured server when there is no link or only a direct one; does nothing while
// linked to it.
void back_to_server();
// Closes the server link and its reconnecting, for Direct connect, which needs none; does nothing
// while hosting.
void stop_server_loop();
// Handles an event of Home, then sets it again; in a party, opens the lobby.
void on_home(const openrac::UiPage& page, const openrac::UiEvent& e);
// Handles an event of the Code page, then sets it again; in a party, opens the lobby.
void on_code(const openrac::UiPage& page, const openrac::UiEvent& e);
// Steps the row `item` of Join by address by `choice`, the index the page chose: an octet, or the
// port by one or by a hundred.
void step_address(int item, int choice);
// Changes the Direct page to `view` in place, with the page-change sound and the cursor on `item`.
void switch_view(const openrac::UiPage& page, DirectView view, int item);
// Handles an event of the Direct page, then sets it again; in a direct party, opens the lobby.
void on_direct(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_browse.cpp ----
// Sets the Browse page's content: Refresh, then the parties with room.
void show_browse();
// Handles an event of Browse: asks for the list, joins a party by its code, then sets it again.
void on_browse(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_saves.cpp ----
// Sets the Saves page's content: New game and the card's saves, or the party's own saves under
// Continue as host.
void show_saves();
// Handles an event of Saves: the save chosen starts a party of its own, on the server or by this
// game hosting.
void on_saves(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_lobby.cpp ----
// Returns whether this player, a member, is refused the host's game because its mods differ.
bool refused();
// Returns whether this player, a member, has not yet heard the host's mods.
bool checking();
// Returns the Lobby page's content, and notes its member rows and Team tools item in the state. The
// test bot reads it too.
openrac::UiContent lobby_content();
// Sets the Lobby page's content from the party.
void show_lobby();
// Handles an event of the Lobby: Ready or Start, Team tools, Party rules, a member's Remove, and
// Leave (which asks first).
void on_lobby(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_refused.cpp ----
// Returns the Refused page's content: why this game cannot join, or play, and what to do.
openrac::UiContent refused_content();
// Sets the Refused page's content.
void show_refused();
// Handles an event of the Refused page: Leave party or Back, opening the mod code, copying it.
void on_refused(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_rules.cpp ----
// Returns the Rules page's content: what the party plays by and what stays the player's own.
openrac::UiContent rules_content();
// Sets the Rules page's content.
void show_rules();
// Handles an event of the Rules page: copying the mod code, lifting bans, going back.
void on_rules(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- pages_remove.cpp ----
// Returns the Remove page's content for the member the state names.
openrac::UiContent remove_content();
// Sets the Remove page's content.
void show_remove();
// Opens the Remove page for `member`.
void open_remove(std::int32_t member);
// Takes `member` out of the party (the host only), and logs it when the server was asked.
void remove_member(std::int32_t member);
// Takes out every party member who is under a key banned from this save (the host only). Called
// once a game tick.
void remove_banned();
// Handles an event of the Remove page: Remove, Ban from save or Back.
void on_remove(const openrac::UiPage& page, const openrac::UiEvent& e);

// ---- multiplayer.cpp ----
// Returns Team tools' line over the game at time `now` (milliseconds), empty for none.
std::string comfort_banner(std::uint64_t now);
// Sets the Team tools page's content: the host's settings, the buttons and each member's health.
void show_comfort();
// Handles an event of the Team tools page: the host's settings, the rally point, giving up.
void on_comfort(const openrac::UiPage& page, const openrac::UiEvent& e);
// Writes one line of the session's network counts to the verbose log.
void trace_net();
// Returns the player's name: `setting` (the mod's `name`) when valid, else the launcher's, else one
// the mod makes up.
std::string player_name(const std::string& setting);
// Returns the status line: the connection's state, or the last notice.
std::string status();
// Sets the party's line over the game at time `now` when it changed.
void show_banner(std::uint64_t now);
// Brings the session up to date with the connection: hello on a new connection, the server's
// messages, the last notice. Called once a game tick, and from the pad hook while a movie plays.
void poll();

}  // namespace mpmod

#endif
