/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The party's shared progress past the items, gold bolts and planets (progress.hpp): the gold
 * weapons, the skill points, the story's flags and the game's special items. No socket and no game
 * here: given the Session, the Game and the Coop, on the game's thread, once a tick.
 *
 * Host-authoritative, as PROGRESS. Each game reads its sections a few times a second. A member tells
 * the host what it has (SHARED) when that changes and every few seconds; the host merges what it
 * lacks and tells every member what the party has. A game only ever gets more: a flag is set, never
 * cleared, and a flag a game once had and lost by itself (a special item spent, a challenge
 * restarted) is not put back. Everything is set silently, in place: no banner, no sound, no reward,
 * nothing on the memory card, whether the player was there or joined late. What the host's game
 * holds is what its save, and so each member's co-op save, holds.
 *
 * The story's flags are the game's per-level mission bytes (shared.c in the port; saved_mission_state,
 * 16 for each of 20 levels, 0 or 0xFF). A flag is set when a mission moby is done or a checkpoint is
 * reached; doors, lifts and people read it. No reader starts a scene because a flag was set, so a
 * flag can be set in a level that is playing. It goes the other way round for the scenes a player
 * starts at their own copy of a person or a pickup (local_class in world.hpp): the flag is set as
 * the scene ends, and in the game that has it the scene will not play again. Those flags
 * (kSceneFlags, read from the game's code and both discs' levels) must not cross to a game that is
 * still to play the scene:
 *   - a member's never reaches the host: the story is the host's, and it plays the scene for the
 *     party when it gets there;
 *   - the host's reaches a member only once the member has left that level, so that the person is
 *     not taken from under a member talking to them, and a later visit finds the story as it is.
 *
 * MISSION carries one level's row, host to members: at once when the row of the host's level
 * changes, and on a member's arrival in a level (SHARED_ASK), so a member's doors and people follow
 * the host's without waiting for the next SHARED. The nanotech upgrades among the special items stay
 * each player's own, as all health does.
 *
 * DATA kind 36 SHARED: u8 count, then per section u8 tag, u16 length, its bytes: bits, 1 for had.
 * Tags (a section of a tag unknown here is skipped): 1 gold weapons (40 bits by item id), 2 skill
 * points (32), 3 flags (320: level x 16 + flag), 4 special items (128).
 * DATA kind 37 MISSION: i8 level, 16 bytes of the level's row, the host's.
 * DATA kind 38 SHARED_ASK: i8 level, a member's: the host answers with a MISSION.
 */
#ifndef MULTIPLAYER_SHARED_H
#define MULTIPLAYER_SHARED_H

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>

#include "coop.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "trace.hpp"

namespace mp {

constexpr std::uint8_t kSharedKind = kind::kShared;
constexpr std::uint8_t kMissionKind = kind::kMission;
constexpr std::uint8_t kSharedAskKind = kind::kSharedAsk;

/* The tags of a section, on the wire and in the SDK's shared_read (OPENRAC_SHARED_*). */
constexpr int kSharedGoldWeapons = 1;
constexpr int kSharedSkillPoints = 2;
constexpr int kSharedFlags = 3;
constexpr int kSharedSpecials = 4;

constexpr std::size_t kGoldWeapons = 40;
constexpr std::size_t kSkillPoints = 32;
constexpr std::size_t kFlagLevels = 20;
constexpr std::size_t kFlagsPerLevel = 16;
constexpr std::size_t kFlags = kFlagLevels * kFlagsPerLevel;
constexpr std::size_t kSpecials = 128;

/* A game's shared progress: a byte each, non-zero had. */
struct SharedBits {
    std::array<std::uint8_t, kGoldWeapons> gold_weapons{};
    std::array<std::uint8_t, kSkillPoints> skills{};
    std::array<std::uint8_t, kFlags> flags{};
    std::array<std::uint8_t, kSpecials> specials{};

    bool operator==(const SharedBits& o) const {
        return gold_weapons == o.gold_weapons && skills == o.skills && flags == o.flags
               && specials == o.specials;
    }

    bool operator!=(const SharedBits& o) const { return !(*this == o); }
};

/* Whether flag `flag` of level `level` is one a player's own scene sets (above). */
bool scene_flag(int level, int flag);

/* Whether a special item stays each player's own (the nanotech upgrades). */
bool own_special(int item);

Bytes encode_shared(const SharedBits& bits);
bool decode_shared(const Bytes& bytes, SharedBits& out);
Bytes encode_mission(int level, const std::uint8_t* row);
bool decode_mission(const Bytes& bytes, int& level, std::array<std::uint8_t, kFlagsPerLevel>& row);
Bytes encode_shared_ask(int level);
bool decode_shared_ask(const Bytes& bytes, int& level);

class Shared {
public:
    Shared(Session& session, Game& game, Coop& coop);

    /* A SHARED, MISSION or SHARED_ASK from `sender`. */
    void handle(std::int32_t sender, const Bytes& bytes);

    /* Once a game tick. */
    void tick(std::uint64_t now_ms);

    /* The party or its game is gone: nothing is kept. */
    void reset();

    /* Someone came back to the party: everything goes across again, this level's row with it. */
    void again() {
        sent_any_ = false;
        asked_load_ = false;
    }

    /* What it knows: this game's last reading, the party's (a member: the host's last word). */
    const SharedBits& here() const { return here_; }

    const SharedBits& party() const { return party_; }

    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

    void trace(Trace* trace) { trace_ = trace; }

private:
    /* Reads this game's sections into here_; false when it has none to read. */
    bool read();
    /* Merges into this game what `want` has that it lacks and may have; true when it took some.
     * `level` is the level whose scene flags are left out, -1 for none; `all_scenes` leaves out
     * every level's. */
    bool merge(const SharedBits& want, int level, bool all_scenes, const std::string& from);
    void send(std::int32_t target, const std::string& to);
    void trace_line(const std::string& text) {
        if (trace_ != nullptr && trace_->on()) {
            trace_->line("shared", text);
        }
    }
    void log(const std::string& line) const {
        if (log_) {
            log_(line);
        }
    }

    Session& session_;
    Game& game_;
    Coop& coop_;
    std::function<void(const std::string&)> log_;
    Trace* trace_ = nullptr;
    bool read_ = false;
    std::uint64_t read_ms_ = 0;
    SharedBits here_;
    /* All this game has held, so that what it lost by itself is not put back. */
    SharedBits once_;
    /* Member: the host's word (and whether one came). Host: unused. */
    SharedBits party_;
    bool heard_ = false;
    /* Host: each member's last SHARED, not yet merged here, and who asked for which level. */
    std::map<std::int32_t, SharedBits> reports_;
    std::set<std::pair<std::int32_t, int>> asked_;
    /* Host: the row of its level as last told, which level that is. */
    std::array<std::uint8_t, kFlagsPerLevel> row_{};
    int row_level_ = -1;
    /* Member: the load it last asked about. */
    bool asked_load_ = false;
    std::uint32_t load_ = 0;
    /* What was last sent, and when. */
    SharedBits sent_;
    bool sent_any_ = false;
    std::uint64_t sent_ms_ = 0;
    std::size_t members_ = 0;
};

}  // namespace mp

#endif /* MULTIPLAYER_SHARED_H */
