/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The party's saves. Everyone in a party, the host too, plays in a co-op slot of its own card,
 * beside the game's folder (<the game's folder>-coop, save0.bin to save4.bin): whatever the game
 * saves while a party plays goes there, through the port's card filter, and a player's own saves
 * stay as they were.
 *
 * A session starts from a copy of the save it plays: a player's slot is written with those bytes
 * before the game starts (a new game's, from the game's first moment in a level, since a save of
 * the game's own writes only two parts of a file that must be there). When the host's game saves,
 * the host's slot goes to every member, which writes it over its own; a member's game saving
 * writes that member's slot, so a slot is the newest of the two. Each player's slot is a save like
 * any other: Continue as host (the Saves page) starts a party from one, whoever made it.
 *
 * A slot has a note beside it: the party it belongs to (a key the host makes when a game starts
 * from anything but a co-op save, which goes on when one does), when it last saved, whose party it
 * was and who is banned from it. The same party comes back to the same slot; another takes a free
 * one, then the oldest.
 *
 * A ban is of a co-op save, against the key of a launcher (the SDK's identity_key, which MODS
 * carries), so neither a new name nor another address gets round it. It lasts as long as the save:
 * Continue as host brings it back, and the bans go to the members with COPARTY, so any member's
 * slot has them when that member hosts it. A key is a text no party can read anything from: a
 * player's own to give, not a proof of who it is.
 *
 * The filter stays on until the game is back at the title: a party gone leaves the game in a level
 * for a while, with the party's progress in it, and the game's next save would reach the player's
 * own slot. Before that, one last save of what the game holds now.
 *
 * Wire (kinds.hpp, 32 to 35; TCP, in order):
 *   32 COSAVE     tag u8, total i32, index i32, bytes    host to member: a piece of the host's save,
 *                                                        at most kChunkBytes; `tag` names the save
 *   33 COSAVE_ACK tag u8, ok u8                          member to host: that save is in its slot
 *   34 SAVE_NOW                                          member to host: please save the party
 *   35 COPARTY    key i32, count u8, then each ban (string)  host to member, before PLAY: whose
 *                                                        this is; to all when the bans change
 *
 * The host's save is untrusted here as anywhere: only the host's word counts, the pieces must make
 * a save of a size a game has, and every save written to a slot passes the one check first
 * (Game::save_usable here, the same check again in the port's coop_save_write). Continue as host
 * goes the way of any chosen save: Coop::start's check, then the port's before the game loads it.
 */
#ifndef MULTIPLAYER_COSAVE_H
#define MULTIPLAYER_COSAVE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "coop.hpp"
#include "kinds.hpp"
#include "protocol.hpp"
#include "session.hpp"

namespace mp {

/* The card folder and how many slots it has. */
constexpr const char* kCoFolder = "coop";
constexpr int kCoSlots = 5;
/* The most bans a save keeps: a new one past them replaces the oldest. */
constexpr std::size_t kMaxBans = 32;

/* How long "Party saved" stays, and how soon a player can save the party again. */
constexpr std::uint64_t kCoSavedMs = 2500;
constexpr std::uint64_t kCoSaveEveryMs = 3000;

/* What Saves and the lobby say of where the party's saves go. */
constexpr const char* kCoSavesLine = "The party saves in a slot of its own";

/* What a slot's note says. A key of 0 is a slot no party has. */
struct CoNote {
    std::uint32_t key = 0;
    std::int64_t saved = 0;  // seconds since 1970
    std::string host;
    std::vector<std::string> bans;  // the keys banned from this save
};

/* The note's text (lines of name=value, which the launcher reads too) and back; a host's name keeps
 * to its line. False for a text that is no note. */
std::string note_text(const CoNote& note);
bool parse_note(const std::string& text, CoNote& out);

/* The slot of the party `key` among the notes of the kCoSlots slots: its own, else a free one,
 * else the one saved longest ago. */
int pick_slot(const std::vector<CoNote>& notes, std::uint32_t key);

/* A slot's row: "Co-op 2: Pokitaru, 3 gold bolts, Kero's party", or empty ("Co-op 2: empty"). */
std::string coop_row(int slot, const std::string& save, const CoNote& note, bool there);

class CoSave {
public:
    CoSave(Session& session, Game& game, std::function<std::uint64_t()> clock);

    /* What the host chose to play: anything but a co-op save is a party of its own; `slot`, a
     * co-op save, goes on as that party. */
    void choose_new();
    void choose_slot(int slot);

    /* The host: tells `target` whose game this is, before PLAY reaches it. */
    void announce(std::int32_t target);

    /* The host starts again (coop.hpp): a party of its own, so the last game's slot stays as it is
     * on every card. The members hear of it by the next announce. */
    void start_again();

    /* A game of the party's is about to start here with `save` (none: a new game). */
    void starting(const Bytes& save);

    /* A DATA of ours. */
    void handle(std::int32_t sender, const Bytes& bytes);

    /* Each tick, `playing` whether the party's game is on. */
    void tick(std::uint64_t now, bool playing);

    /* The bans of the party's save. The host: a key banned, written beside the save at once and
     * told to the members; lift_bans takes them all away. */
    bool ban(const std::string& key);
    void lift_bans();
    bool banned(const std::string& key) const;
    const std::vector<std::string>& bans() const { return bans_; }
    /* The host: the members in the party whose key (`key_of`) is banned; none of its own. */
    std::vector<Member> banned_members(const std::function<std::string(std::int32_t)>& key_of) const;

    /* The Save the party item: the host saves for everyone, a member asks it to. False too soon, or
     * with nothing to save. */
    bool save_party();

    /* The player is leaving (Leave party, Stop hosting): the last save, to the party too. */
    void leaving();

    /* Someone else hosts: a save on its way never comes whole, and is left out. The slot keeps its
     * last whole one. */
    void host_changed();

    /* Whether the game's saves are the party's now, and in which slot (-1: none). */
    bool on() const { return on_; }
    int slot() const { return slot_; }
    std::uint32_t key() const { return key_; }

    /* What the pause page says of where saves go; empty out of a game of the party's. */
    std::string where() const;

    /* The line over the game; empty for none. */
    std::string banner(std::uint64_t now) const;

    /* The host: how many members have its last save, of how many it went to. */
    std::size_t acked() const { return acked_.size(); }
    std::size_t sent_to() const { return sent_to_; }

private:
    void ensure_key();
    void engage();
    bool write_copy(const Bytes& save);
    void write_note();
    void saved(std::uint64_t now);
    void broadcast(const Bytes& save);
    void bans_changed();
    void take_piece(Reader& r);
    std::string host_name() const;

    Session& session_;
    Game& game_;
    std::function<std::uint64_t()> clock_;

    bool on_ = false;
    bool refused_ = false;  // the port has no filter, or would not
    bool copied_ = false;   // the slot holds this session's save
    bool last_ = false;     // the last save was made
    int slot_ = -1;
    int continuing_ = -1;  // the co-op slot the host chose
    std::uint32_t key_ = 0;
    std::uint32_t chosen_key_ = 0;
    std::string host_;
    std::vector<std::string> bans_;
    std::uint32_t saves_seen_ = 0;
    std::uint64_t saved_until_ = 0;
    std::uint64_t asked_ms_ = 0;

    // The host: its last save sent, who has it.
    std::uint8_t tag_ = 0;
    std::size_t sent_to_ = 0;
    std::vector<std::int32_t> acked_;

    // A member: the host's save on its way.
    bool receiving_ = false;
    std::uint8_t in_tag_ = 0;
    int in_total_ = 0;
    std::vector<bool> in_got_;
    Bytes in_bytes_;
    std::size_t in_size_ = 0;
};

}  // namespace mp

#endif
