/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The party's shared progress (progress.cpp): what one player gets, every player has. The items
 * owned (weapons, gadgets, the packs: whatever the game gave, bought or found), the gold bolts
 * collected (and so their count) and the planets unlocked. No socket and no game here: given the
 * Session, the Game and the Coop, on the game's thread, once a tick.
 *
 * Host-authoritative. Each game reads its own progress a few times a second. A member tells the
 * host what it has (PROGRESS) when that changes and every few seconds; the host gives its own game
 * what a member has that it lacks, and tells every member what the party has (its own game's
 * progress, PROGRESS to all) when that changes and every few seconds. A member gives its game what
 * the host's has that it lacks; nothing is ever taken away. An item is given as the game gives it
 * (GiveItem: its banner, its starting ammo, the vendor's mark; not put in hand), a planet unlocked
 * as the game unlocks one, a gold bolt marked collected (its count follows; one placed in a level
 * not reached yet is gone when that level loads). Given while the player is in control only (no
 * menu, scene or load in the way). Nothing is written to any memory card: the progress lives in
 * each game's memory, and a member who joins later gets it with the host's live save.
 *
 * What the party had when a member arrived is not news: while a late joiner joins and for a while
 * after (Coop::quiet_gifts: what changed between the save it was given and its arrival) it is given
 * at once and silently, with no banner, no sound and nothing to wait for.
 *
 * A gold bolt's pickup scene is the collector's alone: the game plays it in its level's play mode
 * (CameraScript, not a scene of the level's), so the co-op neither shares it (SCENE) nor waits for
 * it; the gold bolt moby is a pickup (world.hpp), gone from every game when taken.
 *
 * DATA kind 23 PROGRESS: items u8 count, then a byte each; gold bolts u8 count, a byte each;
 * planets u8 count, a byte each (non-zero had). A member's to the host (what it has), the host's to
 * everyone (what the party has).
 */
#ifndef MULTIPLAYER_PROGRESS_H
#define MULTIPLAYER_PROGRESS_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "coop.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "trace.hpp"

namespace mp {

constexpr std::uint8_t kProgressKind = kind::kProgress;
/* How often a game reads its progress, and tells it again unchanged. */
constexpr std::uint64_t kProgressReadMs = 250;
constexpr std::uint64_t kProgressResendMs = 5000;

Bytes encode_progress(const ProgressBits& bits);
bool decode_progress(const Bytes& bytes, ProgressBits& out);

class Progress {
public:
    Progress(Session& session, Game& game, Coop& coop);

    /* A PROGRESS from `sender`. */
    void handle(std::int32_t sender, const Bytes& bytes);

    /* Once a game tick. */
    void tick(std::uint64_t now_ms);

    /* The party or its game is gone: nothing is kept. */
    void reset();

    /* What it knows: this game's last reading, the party's (a member: the host's last word). */
    const ProgressBits& here() const { return here_; }

    const ProgressBits& party() const { return party_; }

    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

    void trace(Trace* trace) { trace_ = trace; }

private:
    /* Gives this game what `want` has and it lacks (a line each); true when it gave something. */
    bool give(const ProgressBits& want, const std::string& from);
    void send(std::int32_t target, const ProgressBits& bits, const std::string& to);
    void trace_line(const std::string& text) {
        if (trace_ != nullptr && trace_->on()) {
            trace_->line("progress", text);
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
    ProgressBits here_;
    /* Member: the host's last PROGRESS (and whether one came). Host: unused. */
    ProgressBits party_;
    bool heard_ = false;
    /* Host: each member's last PROGRESS, not yet given here. */
    std::map<std::int32_t, ProgressBits> reports_;
    /* What was last sent (a member's to the host, the host's to all), and when. */
    ProgressBits sent_;
    bool sent_any_ = false;
    std::uint64_t sent_ms_ = 0;
    std::size_t members_ = 0;
};

}  // namespace mp

#endif /* MULTIPLAYER_PROGRESS_H */
