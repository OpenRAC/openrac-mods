// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The mods a party has to agree on (party_mods.cpp). Every game of a party must play the same game:
// the same build, and the same gameplay mods at the same versions with the same gameplay settings.
// Input and cosmetic mods are each player's own, and so are the settings a mod declares its own or
// the host's (its manifest's "multiplayer", OpenRAC's MODS.md section 3). No socket and no game
// here: given the Session and the Game, on the game's thread, once a tick.
//
// Over DATA, the host's word first:
//   29 MODS   multiplayer's version, OpenRAC's and the disc (3 strings), count u8, then per mod: id,
//             name, version (strings), kind u8 (OPENRAC_PARTY_MOD_*), settings u8, per setting: key,
//             name, value, shown (strings), kind u8 (OPENRAC_PARTY_SETTING_*), default u8; then the
//             world family (string), the mod code (string, "" for none: modcode.hpp), the sender's
//             key (string: a member's, "" from the host), count u8, then per data pack: id, name,
//             version (strings), digest i32
//             a member to the host every kModsMs until the host's comes; the host's to that member;
//   30 FITS   count u8, then id i32, fit u8 (0 unknown, 1 they match, 2 they differ), why (string)
//             the host to everyone, at each change;
//   31 RULES  count u8, then per mod: id, length u16 + its bytes, its words (a string)
//             the host to one member it found matching, and to all whenever its mods publish new
//             rules (a Chaos event). On the reliable stream ahead of PLAY: a member has them before
//             it loads any level.
//
// Data packs. Those that change the game (the SDK's packs_info: gameplay data, collision, a map)
// are compared as gameplay mods are, in order, by what they hold: a pack that holds the same on two
// discs has the same digest. One of looks alone is each player's own, and the port does not list
// it.
//
// Discs. Two discs of one world family (openrac_api.world_family) play together: the check, the
// digest and the build follow the family, and the disc is only said. A level that still does not
// agree is found as it loads (World, sim digest).
//
// The check. The host compares each member's MODS with its own (compare_mods) and tells everyone
// (FITS); a member compares for itself too, for the words on its page. The host takes no Ready of a
// member that differs, or has said nothing, which holds Start, and gives it no game (Coop's gate).
// The mods' digest is also in the build PLAY carries, so a game that slipped past is still refused.
//
// The rules. A mod publishes its rules in every game (openrac_api.party_rules_publish); a host's
// mods' go to the members, whose same mods take them (party_rules_deliver) for as long as the party
// lasts, through any level load or respawn, and are told when it goes.
//
// A member that takes over as host keeps the party's rules and passes them on. Each mod that took
// them is told it hosts now by an empty delivery (0 bytes, not null: a host never publishes 0
// bytes); a mod that then publishes again (Difficulty: the party's rules, and the events and copies
// it now runs) has its new rules passed on, and the rest keep the old host's.

#ifndef MULTIPLAYER_PARTY_MODS_H
#define MULTIPLAYER_PARTY_MODS_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "protocol.hpp"
#include "session.hpp"

namespace mp {

class Game;

// The kind byte that starts a DATA message of this file: the message kinds MODS, FITS and RULES of
// the file comment, numbered as protocol.hpp's `kind` values.
enum class ModsKind : std::uint8_t {
    kMods = kind::kMods,
    kFits = kind::kFits,
    kRules = kind::kRules
};

// What a mod is to a party, and a setting (the SDK's OPENRAC_PARTY_MOD_*, _SETTING_*).
constexpr int kModGameplay = 0;
constexpr int kModInput = 1;
constexpr int kModCosmetic = 2;
constexpr int kModSolo = 3;
constexpr int kSettingShared = 0;
constexpr int kSettingOwn = 1;
constexpr int kSettingHost = 2;

// How often a member says MODS until the host's comes, how long a member that says nothing is
// only being checked, and how often the host looks at its mods' rules.
constexpr std::uint64_t kModsMs = 2000;
constexpr std::uint64_t kModsQuietMs = 6000;
constexpr std::uint64_t kRulesPollMs = 200;

// What a player reads of a disc that cannot play with the host's.
constexpr const char* kDifferentDisc = "Different disc";

// The widest line of a page of the title's menus and of the pause menu's Options pages.
constexpr std::size_t kTitleLine = 33;
constexpr std::size_t kPauseLine = 26;

// One setting of a mod, as a party compares and shows it. A plain value.
struct ModSettingInfo {
    std::string key;             // The setting's key in the mod's manifest.
    std::string name;            // The name a player reads.
    std::string value;           // The value as compared between games.
    std::string shown;           // The value as a player reads it.
    int party = kSettingShared;  // A kSetting* value: who the setting is shared with.
    bool is_default = false;     // True when `value` is the setting's default.
};

// One mod of a game's list. A plain value.
struct ModInfo {
    std::string id;                        // The mod's id.
    std::string name;                      // The name a player reads.
    std::string version;                   // The mod's version.
    int party = kModGameplay;              // A kMod* value: what the mod is to a party.
    std::vector<ModSettingInfo> settings;  // The settings the mod declares.

    // Whether a party has to agree on it.
    bool counts() const { return party == kModGameplay || party == kModSolo; }
};

// A data pack that changes the game.
struct PackInfo {
    std::string id;            // The pack's id.
    std::string name;          // The name a player reads.
    std::string version;       // The pack's version.
    std::uint32_t digest = 0;  // A hash of what the pack holds, equal for equal contents.
};

// A game's build and its mods, Multiplayer's own left out.
struct ModList {
    std::string multiplayer;    // Multiplayer's version.
    std::string openrac;        // OpenRAC's version.
    std::string disc;           // the player's own: for the logs, and never compared
    std::string family;         // the world the disc belongs to: what is compared
    std::vector<ModInfo> mods;  // The game's mods.
    std::string modcode;  // the mod code of this list, if it has one (modcode.hpp): never compared
    std::string key;      // this installation's (the SDK's identity_key), for a ban: never compared
    std::vector<PackInfo> packs;  // in the order they apply
};

// Writes `list` as the body of a MODS message (the layout in the file comment). Past 255 mods,
// settings of one mod or packs, the rest are left out: each count is one byte.
Bytes encode_mod_list(const ModList& list);
// Reads a MODS body into `out`; false when the bytes are cut short. A list from an older game,
// which stops after the disc, reads with the disc as its family and no code, key or packs. A code
// or key of the wrong shape is cleared.
bool decode_mod_list(Reader& r, ModList& out);

// A number that is the same for two lists that fit each other (the build, the gameplay mods, their
// versions and compared settings, in any order) and, but for chance, differs for two that do not.
std::uint32_t mods_digest(const ModList& list);

// One way a player's game differs from the host's.
// One way a player's game differs from the host's.
struct Difference {
    // The ways a game can differ, in the order compare_mods finds them.
    enum class Kind {
        kDisc,         // another world family (or a level that does not agree): "a different disc"
        kOpenRac,      // another OpenRAC: mine, theirs
        kMultiplayer,  // another Multiplayer: mine, theirs
        kHostOnly,     // the host has `mod`, the player has not
        kYouOnly,      // the player has `mod`, the host has not
        kVersion,      // `mod` at another version: mine, theirs
        kSettings,     // `mod`'s `settings` (their names) differ
        kSolo,         // `mod` cannot be played in a party
        kPack,         // the data pack `mod` holds something else
        kPackOrder     // the same data packs, applied in another order
    };
    Kind kind = Kind::kDisc;            // Which way the game differs.
    std::string mod;                    // The mod's or pack's name; empty for a build.
    std::string mine;                   // This player's value: a version or a build.
    std::string theirs;                 // The host's value, as `mine`.
    std::vector<std::string> settings;  // The names of the settings that differ (kSettings).
};

// How `own` differs from the `host`'s: nothing when they fit.
std::vector<Difference> compare_mods(const ModList& own, const ModList& host);

// The solo mods of a list (each player is told, and can neither host nor join).
std::vector<Difference> solo_mods(const ModList& own);

// Words for a player. refusal_lines: a line or more a difference, each at most `width` bytes, for
// the page that says why a game is refused. refusal_short: a few words for the lobby's line about
// that player, the first difference and how many more.
std::vector<std::string> refusal_lines(
    const std::vector<Difference>& differences, std::size_t width = kTitleLine
);
std::string refusal_short(const std::vector<Difference>& differences);

// `text` broken at spaces into lines of at most `width` bytes.
std::vector<std::string> wrap_text(const std::string& text, std::size_t width);
// A list's items side by side, ", " between them, in lines of at most `width` bytes.
std::vector<std::string> wrap_list(const std::vector<std::string>& items, std::size_t width);

// A mod's rules as a party carries them.
struct PartyRules {
    std::string id;    // The id of the mod the rules are for.
    Bytes data;        // The rules as the mod publishes them, opaque here.
    std::string text;  // The mod's words about them, one item a line.
    // How often this game's mod has published (party_rules_read's version); not sent.
    std::uint32_t version = 0;
};

// The party's rules for a player, each line at most `width` bytes: per gameplay mod of the `host`'s
// its name and version, then what `rules` say of it (its words, else the compared settings that are
// not the default); then what stays `own`'s own (the player's input and cosmetic mods, and the
// settings marked own).
std::vector<std::string> rules_lines(
    const ModList& host, const std::vector<PartyRules>& rules, const ModList& own, std::size_t width
);

// The games, as openrac_api.game names them.
constexpr const char* kGameRac1 = "rac1";
constexpr const char* kGameRac2 = "rac2";

// The description a party is listed with carries the host's digest, for Browse to mark the games a
// player's mods would not fit: "Pokitaru, 12 gold bolts|3fa9" (kMaxDescriptionLength in all). A
// game other than Ratchet & Clank adds its id after the digest, "Going Commando|3fa9/rac2", so its
// parties and Ratchet & Clank's never mix (an older Ratchet & Clank game reads that tag as another
// digest: the party differs).
std::string tagged_description(
    const std::string& description, std::uint32_t digest, const std::string& game = kGameRac1
);
// The description without the tag, the digest's four hex digits ("" for none), and the game the tag
// names: "" for a description without one, kGameRac1 for a tag with the digest alone.
std::string plain_description(const std::string& description);
std::string description_tag(const std::string& description);
std::string description_game(const std::string& description);
// Returns the low 16 bits of `digest` as four lowercase hex digits.
std::string digest_tag(std::uint32_t digest);
// Whether the party listed with `description` plays another game than `game`. Ratchet & Clank
// takes an untagged party for its own (its hosts before the tag); any other game takes only a party
// its tag names.
bool other_game(const std::string& description, const std::string& game);

// How a member's mods compare with the host's, as FITS carries it.
enum class Fit : std::uint8_t {
    kUnknown = 0,
    kMatch = 1,
    kDiffers = 2
};

// The agreement on mods of one party, kept by one game: it checks this game's mods against the
// host's, and the host's verdicts and rules reach every member through it.
//
// Holds references to the Session and the Game, which outlive it. Only the game's tick thread uses
// it: tick() and handle() both run there.
class PartyMods {
public:
    // Creates the agreement for a game whose mods are `own`. `clock` returns milliseconds; when
    // empty it is the wall clock (now_ms), and a test passes its own.
    PartyMods(Session& session, Game& game, ModList own, std::function<std::uint64_t()> clock = {});

    // A DATA message of the kinds above, from `sender`.
    void handle(std::int32_t sender, const Bytes& bytes);

    // Once a game tick.
    void tick(std::uint64_t now);

    // This game's list, once every mod has loaded (a mod loaded after this one is not known at
    // this one's load).
    void set_own(ModList own) { own_ = std::move(own); }

    // The host: the mod code of this game's mods, "" for none; a party hears when it changes.
    void set_mod_code(const std::string& code);

    // Returns this game's own list.
    const ModList& own() const { return own_; }

    // Whether `member` of the party is known to match the host.
    Fit fit(std::int32_t member) const;

    // What the host said of `member`'s mods, for its line ("Difficulty settings").
    std::string why(std::int32_t member) const;

    // The host: `member`'s key, as its MODS said; "" for none yet, or a game with none.
    std::string key(std::int32_t member) const;

    // The host: whether `member` may be Ready and get the game: not one on its way out.
    bool may_play(std::int32_t member) const;

    // This player, in a party: how its game differs from the host's (none yet, while the host's
    // list has not come); whether the host's list came; whether the host has been silent too long
    // (an older game, which answers nothing).
    const std::vector<Difference>& differences() const { return differences_; }

    // Returns whether the host's list has come.
    bool heard_host() const { return heard_host_; }

    // Returns whether this member has waited kModsQuietMs for the host's list and heard nothing.
    bool host_silent() const;

    // The host's list (this game's own, for a host), and the rules in force: the host's, for a
    // member, this game's own for a host.
    const ModList& host_list() const;

    // Returns the rules in force.
    const std::vector<PartyRules>& rules() const { return rules_; }

    // Whether any of the list's mods cannot be played in a party.
    bool solo() const { return !solo_mods(own_).empty(); }

    // Sets where this object's log lines go.
    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

private:
    // What the host knows of one member, or, on a member, what the host said of it.
    struct Peer {
        Fit fit = Fit::kUnknown;  // The verdict on the member's mods.
        std::string why;          // The reason, as refusal_short wrote it; empty for a match.
        std::uint64_t since = 0;  // When the host began waiting for its MODS, on `clock_`.
        bool answered = false;    // True once its MODS has come.
        std::string key;          // The key its MODS carried; empty for none.
    };

    // Sends a MODS to `target` (kBroadcast for all): the party's list from a host, with its key
    // removed, and this game's own from a member.
    void send_mods(std::int32_t target);
    // Sends every member's fit and reason to everyone. Host only.
    void send_fits();
    // Sends the rules in force to `target`.
    void send_rules(std::int32_t target);
    // Takes a MODS from `sender`. A member keeps the host's list and compares it with its own. The
    // host compares the sender's list with the party's, answers a first MODS with its own, and
    // sends FITS when the verdict changed.
    void on_mods(std::int32_t sender, Reader& r);
    // Takes the host's FITS: copies each member's fit and reason.
    void on_fits(Reader& r);
    // Takes the host's RULES: hands each new or changed rule to this game's mod of that id and
    // keeps the set in force. A message that is cut short changes nothing.
    void on_rules(Reader& r);
    // The host's part of a tick: forgets members that left, starts the wait for new ones, marks one
    // that stayed silent as an old game, and reads this game's rules every kRulesPollMs.
    void tick_host(std::uint64_t now);
    // The host: this game's rules read again and, when new, sent to every matching member. True
    // when they were new.
    bool refresh_rules();
    // Forgets everything of the party and sets the mods that took its rules back to their own
    // settings.
    void clear();

    // Writes `line` to the log callback, if one is set.
    void log(const std::string& line) const {
        if (log_) {
            log_(line);
        }
    }

    Session& session_;                             // The session of the party.
    Game& game_;                                   // The game, which takes the rules.
    ModList own_;                                  // This game's own list.
    std::function<std::uint64_t()> clock_;         // Milliseconds; a test may give its own.
    std::function<void(const std::string&)> log_;  // Where log lines go.
    // True from the first tick in a party until the tick that finds it gone.
    bool in_party_ = false;
    // The host of the party being tracked, to tell a new party from the same one.
    std::int32_t party_host_ = 0;
    // The code of the party being tracked.
    std::string party_code_;
    // When this game joined, or when its host changed, on `clock_`.
    std::uint64_t joined_ms_ = 0;
    // When a member last sent its MODS; 0 before the first.
    std::uint64_t sent_ms_ = 0;
    // When the host last read this game's rules.
    std::uint64_t polled_ms_ = 0;
    // True once the host's MODS has come.
    bool heard_host_ = false;
    // The host's list as its MODS said (a member only).
    ModList host_;
    // How `own_` differs from `host_` (a member only).
    std::vector<Difference> differences_;
    // The host: what it knows of each member; a member: what the host said of each.
    std::map<std::int32_t, Peer> peers_;
    // The rules in force, and the mods of this game that took the host's.
    std::vector<PartyRules> rules_;
    // The party's host changed (migrate.hpp), so its epoch: the rules stand, and the new host goes
    // on by the list everyone was checked against, not its own.
    void host_moved(std::uint64_t now);

    // Returns the list the party was checked against: the one inherited from the first host after a
    // change of host, else this game's own.
    const ModList& party_list() const { return inherited_ ? basis_ : own_; }

    // The host epoch of the party at the last tick (the session's role).
    std::uint16_t party_epoch_ = 0;
    // True on a host that took over: `basis_` is the list the party was checked against.
    bool inherited_ = false;
    // The first host's list with this game's disc, used while `inherited_`.
    ModList basis_;
    // The ids of this game's mods that took the party's rules, so clear() can reset them.
    std::vector<std::string> delivered_;
    // At the takeover: each of this game's mods' publish count, to tell which published since.
    std::map<std::string, std::uint32_t> versions_at_takeover_;
};

}  // namespace mp

#endif  // MULTIPLAYER_PARTY_MODS_H
