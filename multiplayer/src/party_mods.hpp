/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The mods a party has to agree on (party_mods.cpp). Every game of a party must play the same game:
 * the same build, and the same gameplay mods at the same versions with the same gameplay settings.
 * Input and cosmetic mods are each player's own, and so are the settings a mod declares its own or
 * the host's (its manifest's "multiplayer", OpenRAC's MODS.md section 3). No socket and no game
 * here: given the Session and the Game, on the game's thread, once a tick.
 *
 * Over DATA, the host's word first:
 *   29 MODS   multiplayer's version, OpenRAC's and the disc (3 strings), count u8, then per mod: id,
 *             name, version (strings), kind u8 (OPENRAC_PARTY_MOD_*), settings u8, per setting: key,
 *             name, value, shown (strings), kind u8 (OPENRAC_PARTY_SETTING_*), default u8; then the
 *             world family (string), the mod code (string, "" for none: modcode.hpp)
 *             a member to the host every kModsMs until the host's comes; the host's to that member;
 *   30 FITS   count u8, then id i32, fit u8 (0 unknown, 1 they match, 2 they differ), why (string)
 *             the host to everyone, at each change;
 *   31 RULES  count u8, then per mod: id, length u16 + its bytes, its words (a string)
 *             the host to one member it found matching, and to all whenever its mods publish new
 *             rules (a Chaos event). On the reliable stream ahead of PLAY: a member has them before
 *             it loads any level.
 *
 * Discs. Two discs of one world family (openrac_api.world_family) play together: the check, the
 * digest and the build follow the family, and the disc is only said. A level that still does not
 * agree is found as it loads (World, sim digest).
 *
 * The check. The host compares each member's MODS with its own (compare_mods) and tells everyone
 * (FITS); a member compares for itself too, for the words on its page. The host takes no Ready of a
 * member that differs, or has said nothing, which holds Start, and gives it no game (Coop's gate).
 * The mods' digest is also in the build PLAY carries, so a game that slipped past is still refused.
 *
 * The rules. A mod publishes its rules in every game (openrac_api.party_rules_publish); a host's
 * mods' go to the members, whose same mods take them (party_rules_deliver) for as long as the party
 * lasts, through any level load or respawn, and are told when it goes.
 */
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

enum class ModsKind : std::uint8_t {
    kMods = kind::kMods,
    kFits = kind::kFits,
    kRules = kind::kRules
};

/* What a mod is to a party, and a setting (the SDK's OPENRAC_PARTY_MOD_*, _SETTING_*). */
constexpr int kModGameplay = 0;
constexpr int kModInput = 1;
constexpr int kModCosmetic = 2;
constexpr int kModSolo = 3;
constexpr int kSettingShared = 0;
constexpr int kSettingOwn = 1;
constexpr int kSettingHost = 2;

/* How often a member says MODS until the host's comes, how long a member that says nothing is
 * only being checked, and how often the host looks at its mods' rules. */
constexpr std::uint64_t kModsMs = 2000;
constexpr std::uint64_t kModsQuietMs = 6000;
constexpr std::uint64_t kRulesPollMs = 200;

/* What a player reads of a disc that cannot play with the host's. */
constexpr const char* kDifferentDisc = "You have a different disc from the host";

/* The widest line of a page of the title's menus and of the pause menu's Options pages. */
constexpr std::size_t kTitleLine = 47;
constexpr std::size_t kPauseLine = 36;

struct ModSettingInfo {
    std::string key;
    std::string name;
    std::string value;
    std::string shown;
    int party = kSettingShared;
    bool is_default = false;
};

struct ModInfo {
    std::string id;
    std::string name;
    std::string version;
    int party = kModGameplay;
    std::vector<ModSettingInfo> settings;

    /* Whether a party has to agree on it. */
    bool counts() const { return party == kModGameplay || party == kModSolo; }
};

/* A game's build and its mods, Multiplayer's own left out. */
struct ModList {
    std::string multiplayer;
    std::string openrac;
    std::string disc;    // the player's own: for the logs, and never compared
    std::string family;  // the world the disc belongs to: what is compared
    std::vector<ModInfo> mods;
    std::string modcode; // the mod code of this list, if it has one (modcode.hpp): never compared
};

Bytes encode_mod_list(const ModList& list);
bool decode_mod_list(Reader& r, ModList& out);

/* A number that is the same for two lists that fit each other (the build, the gameplay mods, their
 * versions and compared settings, in any order) and, but for chance, differs for two that do not. */
std::uint32_t mods_digest(const ModList& list);

/* One way a player's game differs from the host's. */
struct Difference {
    enum class Kind {
        kDisc,         // another world family (or a level that does not agree): "a different disc"
        kOpenRac,      // another OpenRAC: mine, theirs
        kMultiplayer,  // another Multiplayer: mine, theirs
        kHostOnly,     // the host has `mod`, the player has not
        kYouOnly,      // the player has `mod`, the host has not
        kVersion,      // `mod` at another version: mine, theirs
        kSettings,     // `mod`'s `settings` (their names) differ
        kSolo          // `mod` cannot be played in a party
    };
    Kind kind = Kind::kDisc;
    std::string mod;
    std::string mine;
    std::string theirs;
    std::vector<std::string> settings;
};

/* How `own` differs from the `host`'s: nothing when they fit. */
std::vector<Difference> compare_mods(const ModList& own, const ModList& host);

/* The solo mods of a list (each player is told, and can neither host nor join). */
std::vector<Difference> solo_mods(const ModList& own);

/* Words for a player. refusal_lines: a line or more a difference, each at most `width` bytes, for
 * the page that says why a game is refused. refusal_short: a few words for the lobby's line about
 * that player, the first difference and how many more. */
std::vector<std::string> refusal_lines(
    const std::vector<Difference>& differences, std::size_t width = kTitleLine
);
std::string refusal_short(const std::vector<Difference>& differences);

/* `text` broken at spaces into lines of at most `width` bytes. */
std::vector<std::string> wrap_text(const std::string& text, std::size_t width);
/* A list's items side by side, ", " between them, in lines of at most `width` bytes. */
std::vector<std::string> wrap_list(const std::vector<std::string>& items, std::size_t width);

/* A mod's rules as a party carries them. */
struct PartyRules {
    std::string id;
    Bytes data;
    std::string text;
};

/* The party's rules for a player, each line at most `width` bytes: per gameplay mod of the `host`'s
 * its name and version, then what `rules` say of it (its words, else the compared settings that are
 * not the default); then what stays `own`'s own (the player's input and cosmetic mods, and the
 * settings marked own). */
std::vector<std::string> rules_lines(
    const ModList& host, const std::vector<PartyRules>& rules, const ModList& own, std::size_t width
);

/* The description a party is listed with carries the host's digest, for Browse to mark the games a
 * player's mods would not fit: "Pokitaru, 12 gold bolts|3fa9" (kMaxDescriptionLength in all). */
std::string tagged_description(const std::string& description, std::uint32_t digest);
/* The description without the digest, and the digest's four hex digits, "" for none. */
std::string plain_description(const std::string& description);
std::string description_tag(const std::string& description);
std::string digest_tag(std::uint32_t digest);

enum class Fit : std::uint8_t {
    kUnknown = 0,
    kMatch = 1,
    kDiffers = 2
};

class PartyMods {
public:
    PartyMods(Session& session, Game& game, ModList own, std::function<std::uint64_t()> clock = {});

    /* A DATA message of the kinds above, from `sender`. */
    void handle(std::int32_t sender, const Bytes& bytes);

    /* Once a game tick. */
    void tick(std::uint64_t now);

    /* This game's list, once every mod has loaded (a mod loaded after this one is not known at
     * this one's load). */
    void set_own(ModList own) { own_ = std::move(own); }

    /* The host: the mod code of this game's mods, "" for none; a party hears when it changes. */
    void set_mod_code(const std::string& code);

    const ModList& own() const { return own_; }

    /* Whether `member` of the party is known to match the host. */
    Fit fit(std::int32_t member) const;

    /* What the host said of `member`'s mods, for its line ("Difficulty settings"). */
    std::string why(std::int32_t member) const;

    /* The host: whether `member` may be Ready and get the game. */
    bool may_play(std::int32_t member) const;

    /* This player, in a party: how its game differs from the host's (none yet, while the host's
     * list has not come); whether the host's list came; whether the host has been silent too long
     * (an older game, which answers nothing). */
    const std::vector<Difference>& differences() const { return differences_; }

    bool heard_host() const { return heard_host_; }

    bool host_silent() const;

    /* The host's list (this game's own, for a host), and the rules in force: the host's, for a
     * member, this game's own for a host. */
    const ModList& host_list() const;

    const std::vector<PartyRules>& rules() const { return rules_; }

    /* Whether any of the list's mods cannot be played in a party. */
    bool solo() const { return !solo_mods(own_).empty(); }

    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

private:
    struct Peer {
        Fit fit = Fit::kUnknown;
        std::string why;
        std::uint64_t since = 0;
        bool answered = false;
    };

    void send_mods(std::int32_t target);
    void send_fits();
    void send_rules(std::int32_t target);
    void on_mods(std::int32_t sender, Reader& r);
    void on_fits(Reader& r);
    void on_rules(Reader& r);
    void tick_host(std::uint64_t now);
    /* The host: this game's rules read again and, when new, sent to every matching member. True
     * when they were new. */
    bool refresh_rules();
    void clear();

    void log(const std::string& line) const {
        if (log_) {
            log_(line);
        }
    }

    Session& session_;
    Game& game_;
    ModList own_;
    std::function<std::uint64_t()> clock_;
    std::function<void(const std::string&)> log_;
    bool in_party_ = false;
    std::int32_t party_host_ = 0;
    std::string party_code_;
    std::uint64_t joined_ms_ = 0;
    std::uint64_t sent_ms_ = 0;
    std::uint64_t polled_ms_ = 0;
    bool heard_host_ = false;
    ModList host_;
    std::vector<Difference> differences_;
    /* The host: what it knows of each member; a member: what the host said of each. */
    std::map<std::int32_t, Peer> peers_;
    /* The rules in force, and the mods of this game that took the host's. */
    std::vector<PartyRules> rules_;
    std::vector<std::string> delivered_;
};

}  // namespace mp

#endif /* MULTIPLAYER_PARTY_MODS_H */
