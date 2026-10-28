/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.cpp): the game started for the whole party, everyone kept in the host's
 * level, each player's Ratchet sent to the others, and the others shown as figures (puppets) in
 * one's own level. No socket and no game here: it is given the Session and a Game (the SDK's play
 * calls in the mod, a fake in the tests), on the game's thread, once a tick.
 *
 * The game's own kinds of DATA (after the lobby's 1 to 3, session.hpp), the host's word first:
 *   4 PLAY   play i32, mode u8 (0 a new game, 1 a save), level i32, save size i32
 *            the host to everyone (at Start) or to one member (who came late, or asked): play this;
 *            a save follows in CHUNKs. `play` names the host's game: a member plays each once.
 *   5 CHUNK  play i32, offset i32, bytes        a piece of PLAY's save, at most kChunkBytes;
 *   6 LEVEL  play i32, level i32                the host to everyone, once a second and at each
 *            change: the level it plays (-1 none); a member in another level travels there;
 *   7 POSE   (play.hpp)                         each player's Ratchet, as state: a member's to the
 *            host, who passes everyone's on to everyone;
 *   8 WANT   play i32                           a member in the title's menus that has not played
 *            the host's game (it left it, or missed PLAY) asks for it again.
 * A member's DATA always reaches the host only; the host's goes to everyone or to one.
 *
 * The host plays what Create match chose: a new game (the first level) or a save of its memory
 * card. A member starts the same from the title (play_start), its own card untouched. Joining a
 * game that is on, a member gets the host's progress as it is now (save_capture), in the host's
 * level. The world is each game's own: only the players are shared (the next step shares the rest).
 */
#ifndef MULTIPLAYER_COOP_H
#define MULTIPLAYER_COOP_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "play.hpp"
#include "protocol.hpp"
#include "session.hpp"

namespace mp {

/* The game's kinds of DATA. */
enum class GameKind : std::uint8_t {
    kPlay = 4,
    kChunk = 5,
    kLevel = 6,
    kPose = kPoseKind,
    kWant = 8
};

/* The most bytes of a save a CHUNK carries (a TCP frame takes 64 KiB; four make a save). */
constexpr std::size_t kChunkBytes = 16000;
/* The largest save a member takes (Ratchet & Clank's are 59,912 bytes). */
constexpr std::size_t kMaxSaveBytes = 1u << 17;
/* How often a pose goes (ms: 20 a second), the host's LEVEL, a member's WANT, a travel tried
 * again, a figure made again after the level refused one; and a remote player's silence after
 * which its figure goes. */
constexpr std::uint64_t kPoseMs = 50;
constexpr std::uint64_t kLevelMs = 1000;
constexpr std::uint64_t kWantMs = 5000;
constexpr std::uint64_t kTravelMs = 4000;
constexpr std::uint64_t kFigureRetryMs = 1000;
constexpr double kGoneMs = 5000.0;

/* What the game is doing (the SDK's openrac_play_status). */
struct GameStatus {
    int phase = 0;
    int level = -1;
    std::uint32_t loads = 0;
};

/* The phases (the SDK's OPENRAC_PLAY_*). */
constexpr int kPhaseNone = 0;
constexpr int kPhaseFrontEnd = 1;
constexpr int kPhaseLoading = 2;
constexpr int kPhasePlaying = 3;
constexpr int kPhaseScene = 4;
constexpr int kPhaseMenu = 5;

/* Whether a phase is in a level (playing, a scene, a menu over it). */
inline bool in_level(int phase) {
    return phase == kPhasePlaying || phase == kPhaseScene || phase == kPhaseMenu;
}

/* What the co-op needs of the game: the SDK's play calls (multiplayer.cpp), or a test's. */
class Game {
public:
    virtual ~Game() = default;
    virtual GameStatus status() = 0;
    /* From the title: a new game in `level` (no save), or the save continued. True when asked. */
    virtual bool start(int level, const Bytes& save) = 0;
    virtual bool travel(int level) = 0;
    /* The progress now, as a save; empty for none. */
    virtual Bytes capture() = 0;
    virtual bool hero(Pose& out) = 0;
    /* A figure's id, -1 for none to have; posed (false: gone); removed. */
    virtual int figure_create() = 0;
    virtual bool figure_pose(int id, const Pose& pose) = 0;
    virtual void figure_remove(int id) = 0;
};

/* A remote player as the co-op keeps it: its poses and its figure. */
struct Remote {
    Interpolator poses;
    int figure = -1;
    std::uint64_t retry_ms = 0;  // when a figure may be made again
};

class Coop {
public:
    /* `clock` gives the monotonic milliseconds poses arrive at (now_ms when none is given). */
    Coop(Session& session, Game& game, std::function<std::uint64_t()> clock = {});

    /* The host's choice (Create match's save picker): a new game (no bytes) or a save, and what the
     * lobby and the list say of it. */
    void choose(Bytes save, std::string description);

    const Bytes& chosen() const { return chosen_; }

    const std::string& description() const { return description_; }

    /* The host's Start: everyone plays the chosen game, the host too. False when it cannot (not
     * the host, already playing, the game not in the title). */
    bool start();

    /* A member back in the title with the host's game on: asks for it again (WANT), as one that
     * missed it does. False for the host, or with no game on. */
    bool rejoin();

    /* One of the game's DATA messages (GameKind first), from `sender`. */
    void handle(std::int32_t sender, const Bytes& bytes);

    /* Once a game tick, `now_ms` the monotonic clock. */
    void tick(std::uint64_t now_ms);

    /* The party is gone (left, ended, the connection lost): the figures go, nothing is pending. */
    void reset();

    /* What the pages show: whether the host's game is on (a member: PLAY came), whether a member
     * waits for its save, the level the host plays, the remote players shown now. */
    bool playing() const { return play_ != 0; }

    bool receiving() const { return pending_ && received_ < expected_; }

    int host_level() const { return host_level_; }

    int figures() const;

    const std::map<std::int32_t, Remote>& remotes() const { return remotes_; }

    /* The last thing worth a line (a start, a save that would not load); take_notice() gives it
     * once. */
    std::string take_notice();

    /* Where the co-op says what it does, a line at a time (the mod's log); none: nowhere. */
    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

private:
    void send_play(std::int32_t target, const Bytes& save, int level);
    void on_pose(std::int32_t sender, const Bytes& bytes);
    void tick_host(std::uint64_t now, const GameStatus& status);
    void tick_member(std::uint64_t now, const GameStatus& status);
    void send_pose(std::uint64_t now, const GameStatus& status);
    void show_remotes(std::uint64_t now, const GameStatus& status);
    void drop_figure(Remote& r);

    Session& session_;
    Game& game_;
    std::function<std::uint64_t()> clock_;
    Bytes chosen_;
    std::string description_;
    /* The host's game (0: none yet): its id, and the members told of it. */
    std::int32_t play_ = 0;
    std::vector<std::int32_t> told_;
    /* A member: the host's PLAY waiting to be started (its mode, level and save), and which play
     * the member started last. */
    bool pending_ = false;
    std::int32_t pending_play_ = 0;
    int pending_level_ = 0;
    bool pending_save_ = false;
    std::size_t expected_ = 0;
    std::size_t received_ = 0;
    Bytes save_;
    std::int32_t applied_ = 0;
    int host_level_ = -1;
    std::uint64_t want_ms_ = 0;
    std::uint64_t travel_ms_ = 0;
    std::uint64_t level_ms_ = 0;
    int sent_level_ = -2;
    /* Poses sent: when, the sequence, the level load of the last. */
    std::uint64_t pose_ms_ = 0;
    std::uint16_t sequence_ = 0;
    std::uint32_t pose_loads_ = 0;
    bool pose_sent_ = false;
    Pose last_pose_;
    /* The level loads the figures were made in. */
    std::uint32_t figure_loads_ = 0;
    std::map<std::int32_t, Remote> remotes_;
    std::string notice_;
    std::function<void(const std::string&)> log_;

    void log(const std::string& line) const {
        if (log_) {
            log_(line);
        }
    }
};

/* A save's words for the lists and the lobby: "Pokitaru, 12 gold bolts", or "New game". */
std::string save_description(const std::string& planet, int gold_bolts, bool new_game);

}  // namespace mp

#endif /* MULTIPLAYER_COOP_H */
