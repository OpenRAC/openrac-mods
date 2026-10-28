/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.cpp): the game started for the whole party, everyone kept in the host's
 * level, each player's Ratchet sent to the others, and the others shown as figures (puppets) in
 * one's own level. No socket and no game here: it is given the Session and a Game (the SDK's play
 * calls in the mod, a fake in the tests), on the game's thread, once a tick.
 *
 * The game's own kinds of DATA (after the lobby's 1 to 3, session.hpp), the host's word first:
 *   4 PLAY   play i32, mode u8 (0 a new game, 1 a save), level i32, save size i32, build string
 *            the host to everyone (at Start) or to one member (who came late, or asked): play this;
 *            a save follows in CHUNKs. `play` names the host's game: a member plays each once.
 *   5 CHUNK  play i32, offset i32, bytes        a piece of PLAY's save, at most kChunkBytes;
 *   6 LEVEL  play i32, level i32, scene u8      the host to everyone, once a second and at each
 *            change: the level it plays (-1 none), and whether it plays a scene; a member in another
 *            level travels there;
 *   7 POSE   (play.hpp)                         each player's Ratchet, as state: a member's to the
 *            host, who passes everyone's on to everyone;
 *   8 WANT   play i32                           a member in the title's menus that has not played
 *            the host's game (it left it, or missed PLAY) asks for it again.
 * A member's DATA always reaches the host only; the host's goes to everyone or to one.
 *
 * The host plays what Create match chose: a new game (the first level) or a save of its memory
 * card. A member starts the same from the title (play_start), its own card untouched. Joining a
 * game that is on, a member gets the host's progress as it is now (save_capture), in the host's
 * level. A member whose build differs from the host's (the mod, OpenRAC, the disc) refuses it.
 * Scenes end together: only the host's skip counts (a member's Start does nothing while the host
 * plays a scene too); when the host is out of its scene (skipped, or played out), a member still in
 * one is skipped through the game's own skip (play_skip). Then nobody moves on until everyone is
 * out (the barrier: a player out first has no control, for at most kBarrierMs, a member while the
 * host's scene goes on).
 * When the party ends under a member in the host's game, it goes back to the title. While the
 * party plays, no game pauses: the pause menu and the game's dialogs open over a world that keeps
 * running (the SDK's play_overlay_pause), so a player in a menu never stops the others.
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
/* How often the server's clock is asked again (TIME): the poses' stamps keep to it over a long
 * session. */
constexpr std::uint64_t kClockMs = 30000;
/* A member's scene skipped again this often while the host is out of its own; the most a player
 * out of a scene waits for the others (the host for the members; a member for the host). */
constexpr std::uint64_t kSkipMs = 500;
/* How long the host must have been out of scenes before a member's scene is skipped (the same
 * scene may start in the member's game a moment before the host's word of its own comes). */
constexpr std::uint64_t kHostOutMs = 1000;
constexpr std::uint64_t kBarrierMs = 10000;
constexpr std::uint64_t kHostSceneMs = 120000;

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

/* A moby of the level as the world sees it (the SDK's openrac_moby): its slot here, its key (the
 * level's record, the same in every game; -1 for one spawned), class, flags, and pose. */
struct MobyState {
    int slot = -1;
    int key = -1;
    int oclass = 0;
    std::uint32_t flags = 0;
    Pose pose;
};

/* openrac_moby's flags. */
constexpr std::uint32_t kMobyUpdates = 0x1;
constexpr std::uint32_t kMobyDriven = 0x2;
constexpr std::uint32_t kMobyPlaced = 0x4;
constexpr std::uint32_t kMobyCreature = 0x8;
constexpr std::uint32_t kMobyDead = 0x10;

/* A hit (the SDK's openrac_hit): victim and attacker by slot (-1 none). */
struct Hit {
    int victim = -1;
    int attacker = -1;
    std::uint32_t mask = 0;
    float damage = 0;
    float position[3] = {0, 0, 0};
    float velocity[3] = {0, 0, 0};
};

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

    /* The world (world.hpp; the SDK's 1.9 calls). A game without them lists nothing. */
    virtual std::vector<MobyState> mobys() { return {}; }
    virtual int moby_slot(int /*key*/) { return -1; }
    virtual bool moby_drive(int /*slot*/, bool /*on*/) { return false; }
    virtual bool moby_pose(int /*slot*/, const Pose& /*pose*/) { return false; }
    virtual bool moby_kill(int /*slot*/) { return false; }
    virtual int moby_spawn(int /*oclass*/, const Pose& /*pose*/) { return -1; }
    virtual int figure_slot(int /*id*/) { return -1; }
    virtual bool stand_in(const float* /*position*/, int /*figure*/) { return false; }
    virtual bool hit(const Hit& /*hit*/) { return false; }
    virtual void eyes(const std::vector<float>& /*xyz*/) {}
    virtual int bolts(int /*add*/) { return 0; }
    /* The world kept running under the game's menus (the SDK's play_overlay_pause). */
    virtual void overlay_pause(bool /*on*/) {}
    /* The scene playing skipped as the game's own skip does (the SDK's play_skip). */
    virtual void skip() {}
};

/* A remote player as the co-op keeps it: its poses and its figure. */
struct Remote {
    Interpolator poses;
    bool scene = false;  // its game plays a scene (its last pose said)
    int figure = -1;
    std::uint64_t retry_ms = 0;  // when a figure may be made again
};

class Coop {
public:
    /* `clock` gives the monotonic milliseconds poses arrive at (now_ms when none is given). */
    Coop(Session& session, Game& game, std::function<std::uint64_t()> clock = {});

    /* What every game of a party must have the same of (the mod's version, OpenRAC's, the disc):
     * the host sends it with PLAY, and a member with another refuses to start. */
    void set_build(std::string build) { build_ = std::move(build); }

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

    /* A remote player's latest pose (its newest, not the played-back one) and its figure; false
     * for one with none. */
    bool latest(std::int32_t player, Pose& out) const;

    int figure_of(std::int32_t player) const;

    /* The scene barrier, for the pad: whether this player's control is held (out of a scene while
     * another player is still in one), and whether its Start is held back (a member in a scene the
     * host plays too: the host's skip decides). */
    bool hold_input() const { return hold_; }

    bool block_start() const { return block_start_; }

    bool host_scene() const { return host_scene_; }

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
    void update_barrier(std::uint64_t now, const GameStatus& status);
    void drop_figure(Remote& r);

    Session& session_;
    Game& game_;
    std::string build_;
    /* Whether this game played a member's part of the host's game (it goes back to the title when
     * the party ends). */
    bool member_playing_ = false;
    /* Whether the game was asked to keep its world running under its menus. */
    bool overlay_ = false;
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
    std::uint64_t clock_ms_ = 0;
    /* Scenes: the host's (as LEVEL says, or this game's for the host), the last sent, when a
     * member's skip was last asked, the barrier's start. */
    bool host_scene_ = false;
    bool sent_scene_ = false;
    std::uint64_t skip_ms_ = 0;
    std::uint64_t host_out_since_ = 0;
    std::uint64_t hold_since_ = 0;
    bool hold_ = false;
    bool block_start_ = false;
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
