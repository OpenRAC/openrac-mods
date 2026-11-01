/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.cpp): the game started for the whole party, everyone kept in the host's
 * level, each player's Ratchet sent to the others, and the others shown as figures (puppets) in
 * one's own level. No socket and no game here: it is given the Session and a Game (the SDK's play
 * calls in the mod, a fake in the tests), on the game's thread, once a tick.
 *
 * The game's own kinds of DATA (after the lobby's 1 to 3, session.hpp), the host's word first:
 *   4 PLAY   play i32, mode u8 (0 a new game, 1 a save), level i32, save size i32, build string,
 *            joining u8
 *            the host to everyone (at Start) or to one member (who came late, or asked): play this;
 *            a save follows in CHUNKs. `play` names the host's game: a member plays each once.
 *            `joining`: the game is on, this member comes into it (below).
 *   5 CHUNK  play i32, offset i32, bytes        a piece of PLAY's save, at most kChunkBytes;
 *   6 LEVEL  play i32, level i32, scene u8, travel u16, dest i32, arriving u8
 *            the host to everyone, once a second and at each change: the level it plays (-1 none),
 *            whether it plays a scene, its last ship travel (a count, and the planet it flies to)
 *            and whether the party is still arriving there; a member in another level travels
 *            there, and a member who has not left for the host's newest travel leaves at once;
 *   7 POSE   (play.hpp)                         each player's Ratchet, as state: a member's to the
 *            host, who passes everyone's on to everyone;
 *   8 WANT   play i32                           a member in the title's menus that has not played
 *            the host's game (it left it, or missed PLAY) asks for it again;
 *  18 SKIP   origin i32, sequence u16, play i32, what u8, scene u16
 *            the host pressed Start in a scene or a movie: to everyone (a member's, from an older
 *            build, the host ignores); each game skips the scene or movie it plays (the host's own
 *            is already skipping), and only that one: `what` says what it was pressed
 *            in (kSkipOther a scene of its game's own, kSkipShared the host's scene `scene`, by
 *            SCENE's sequence, kSkipMovie a movie). A skip of an older scene of the host's, of
 *            another play, or without these (an older build) skips nothing.
 *  20 SCENE  play i32, level i32, scene i32, sequence u16, elapsed u32
 *            the host to everyone, when an in-level scene starts in its game (the level's number of
 *            it, a new sequence each start), once a second while it plays, and when it ends (scene
 *            -1): `elapsed` the ms it has played; a member in that level plays the same scene
 *            (coop_scenes.cpp).
 *  21 SCENE_STATE play i32, sequence u16, state u8
 *            a member to the host, at each change and once a second while it knows of a host's
 *            scene: where it is with the host's scene `sequence` (kScenePending to play,
 *            kScenePlaying, kSceneOut played, skipped or given up); the host's barrier waits for
 *            those of its newest scene only (a word of an older one is no word of this one).
 *  44 PLACE  play i32, ticket u16, answer u8, position 3 x f32, yaw f32
 *            the host to a member who asked: where to stand (coop_join.cpp, kPlace*);
 *  45 PLACE_ASK play i32, ticket u16, last u8
 *            a member to the host, twice a second while it is to be put beside the host; `last`:
 *            it waited long enough, the host's last safe spot will do;
 *  46 WAIT_VOTE play i32, wait u16; from the host then kind u8, votes u8, voters u8, passed u8
 *            a player held by a wait for others (coop_wait.cpp): a member's vote to go on without
 *            them; the host's word of the wait, at each change and once a second;
 *  25 POSES  count u8, then per pose length u16 + its POSE
 *            the host to everyone, as state, every kPoseMs: its own Ratchet's and the members'
 *            newest, in one datagram (each POSE on its own would be 20 a second a player of the
 *            server's 60 a second for the host); a game skips its own. With one member there is
 *            nobody to pass poses on to, and a POSE goes.
 * A member's DATA always reaches the host only; the host's goes to everyone or to one.
 *
 * The host plays what Create match chose: a new game (the first level) or a save of its memory
 * card. A member starts the same from the title (play_start), its own card untouched. Joining a
 * game that is on, a member gets the host's progress as it is now (save_capture), in the host's
 * level. A member whose build differs from the host's (the mod, OpenRAC, the world family, the gameplay mods'
 * digest: party_mods.hpp) refuses it, and the host gives no game to one the gate says differs.
 * Scenes and movies end together: only the host skips them. Its Start in a scene or a movie skips
 * it in every game (SKIP: a scene through the game's own skip, play_skip; a movie by Start pressed
 * for the movie player); a member's Start there does nothing (the pad's Start taken out of the
 * report: start_blocked) but say "Only the host can skip". When the host is out of its scene, a
 * member still in one is skipped too. While this game plays a scene the other players' figures are
 * hidden, their pieces with them, and their shots dropped; shown again after it. Then nobody
 * moves on until everyone is out (the barrier: a player out first has no control, for at most
 * kBarrierMs, a member while the host's scene goes on).
 * The level's own scenes play in every game: the scenes the level's mobys start (a person spoken
 * to, a place reached, Veldin's opening) run in the host's game only, since the members' copies of
 * those mobys are driven from the host's (world.hpp), so the host says which scene starts (SCENE)
 * and a member in the same level starts it too, from its start, through the game's own start
 * (play_scene), as soon as its player is in control (late: from the start all the same, the
 * barrier waiting for it). A member's scene that started late is skipped after the host's ends
 * only once it had as long as it was late, so it plays out; the host's skip skips it at once.
 * Travel. Only the host flies the ship while the party plays: a member's planet select to another
 * planet lands the ship again where it is (ship_travel says no). When the host's ship leaves for
 * another planet, every member leaves its level that moment, from wherever it is (playing, a scene,
 * the pause menu, a vendor: the game's own way out of a level, then its flight and its landing),
 * and the party arrives together: until every member who was in the host's game is in the new
 * level (or kArriveMs went by), nobody has control once out of its landing, no figure is shown and
 * the host's world sends nothing (world.hpp), so nothing of the old level carries over and nothing
 * of the new one moves before all are in. A member is told kTravelWarnMs before it is pulled away:
 * the banner names the planet, and it leaves when that is up (the host's take-off plays meanwhile).
 * Joining. A member that comes into a game already on has none of the party's scenes to play and
 * nobody to wait for it. Its game loads the host's level from the host's live save, which has
 * everything it must get past on the way: the game's movies, the ship's landing, whatever story
 * scene the level's mobys start at their first update. From PLAY's `joining` until it stands in the
 * host's level with the host's world (World's SYNCED), this game presses through all of it by
 * itself (Start for a movie, the game's own skip for a scene), says no scene of the host's is its
 * to play, tells no scene in its poses, and is held by nothing, so no other player waits on it; the
 * "only the host skips" rule is for scenes the party plays together, which this is not.
 * Then it stays held, the banner saying so, until the host stands somewhere safe (on the level's
 * ground, not in a scene, not arriving: the pose's kPoseSteady, at most kSafeFreshMs ago), and is
 * put beside the host for a moment of safety (no DAMAGE, kShieldMs). After kPlaceWaitMs without such
 * a spot, the host's last safe one; with none in this level load, it stays where the ship or the
 * level's start put it. A host's scene does not use up that wait: it is waited out and not replayed,
 * and the joiner's own Start ends the wait where it stands. A port that cannot place the hero:
 * no wait, as before. Whatever comes to this game while it joins and for kQuietMs after (the
 * party's progress) comes without its banners (Game::quiet_gifts).
 * Go to host. A member in the host's level may ask to be put beside the host at any time, by the
 * same answers, from the pause page; the hero moves once the menu is closed. No spot: the place
 * this game landed in the level.
 * Starting again. The host, back in the title with the party still there, may Start again: a new
 * `play`. A member in a level leaves it for the title and starts the new game from there.
 * Waits. A player held for others can press Start: the host goes on at once, a member's is a vote
 * (coop_wait.cpp).
 * Leaving. A player whose party goes while it plays the party's game (it left, the host ended the
 * party, the connection went) leaves its level for the title at once, from wherever it is (playing,
 * a scene, the pause menu: play_travel(-1)), and the title opens the mod's Multiplayer page (Home):
 * the host leaving takes everyone there, a member leaving only itself (the host's party goes on
 * with the others). A player in the title's pages (the lobby) stays where it is. While the
 * party plays, no game pauses: the pause menu and the game's dialogs open over a world that keeps
 * running (the SDK's play_overlay_pause), so a player in a menu never stops the others.
 */
#ifndef MULTIPLAYER_COOP_H
#define MULTIPLAYER_COOP_H

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "nametags.hpp"
#include "party_mods.hpp"
#include "play.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "trace.hpp"

namespace mp {

/* The game's kinds of DATA. */
enum class GameKind : std::uint8_t {
    kPlay = kind::kPlay,
    kChunk = kind::kChunk,
    kLevel = kind::kLevel,
    kPose = kPoseKind,
    kWant = kind::kWant,
    kSkip = kind::kSkip,
    kShot = kShotKind,
    kShotX = kShotXKind,
    kScene = kind::kScene,
    kSceneState = kind::kSceneState,
    kPoses = kind::kPoses,
    kPlace = kind::kPlace,
    kPlaceAsk = kind::kPlaceAsk,
    kWaitVote = kind::kWaitVote
};

/* What a skip was pressed in (SKIP's `what`), and where a member is with the host's scene
 * (SCENE_STATE's `state`). */
constexpr std::uint8_t kSkipOther = 0;
constexpr std::uint8_t kSkipShared = 1;
constexpr std::uint8_t kSkipMovie = 2;
constexpr std::uint8_t kScenePending = 0;
constexpr std::uint8_t kScenePlaying = 1;
constexpr std::uint8_t kSceneOut = 2;

/* Whether the host's scene sequence `a` is older than `b` (the count wraps). */
inline bool older_scene(std::uint16_t a, std::uint16_t b) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(a - b)) < 0;
}

/* The most bytes of a save a CHUNK carries (a TCP frame takes 64 KiB; four make a save). */
constexpr std::size_t kChunkBytes = 16000;
/* What a player reads of a save that does not pass (save_check.hpp): the host choosing it, a member
 * given it. */
constexpr const char* kBadSaveHost = "That save can't be used: it may be damaged";
constexpr const char* kBadSaveMember = "You can't join: the host's save can't be used";
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
/* A shot not fired this long after it was due is dropped: a late flash reads as a bug, a lost one
 * goes unseen. The most a remote player's shots wait, the longest one waits to be sent, and how often
 * shots go: the host's state is mostly spent (POSES and WORLD take 50 of the 52 a second), and the
 * member's is not. */
constexpr double kShotLateMs = 250.0;
constexpr std::size_t kShotsWaiting = 32;
constexpr std::uint64_t kShotHoldMs = 250;
constexpr std::uint64_t kShotSendHostMs = 100;
constexpr std::uint64_t kShotSendMemberMs = 50;
constexpr std::uint64_t kShotShareMs = 1000;
/* How often the server's clock is asked again (TIME): the poses' stamps keep to it over a long
 * session. */
constexpr std::uint64_t kClockMs = 30000;
/* How often the verbose log says how each figure's poses are shown. */
constexpr std::uint64_t kPoseTraceMs = 500;
/* A member's scene skipped again this often while the host is out of its own; the most a player
 * out of a scene waits for the others (the host for the members; a member for the host). */
constexpr std::uint64_t kSkipMs = 500;
/* How long the host must have been out of scenes before a member's scene is skipped (the same
 * scene may start in the member's game a moment before the host's word of its own comes). */
constexpr std::uint64_t kHostOutMs = 1000;
constexpr std::uint64_t kBarrierMs = 10000;
constexpr std::uint64_t kHostSceneMs = 120000;
/* The longest the party waits for its members to arrive after a ship travel (from the host's
 * departure: its flight out, the flight between planets, the load, the landing), and how often a
 * member's leave is asked again while its game has not left. */
constexpr std::uint64_t kArriveMs = 90000;
constexpr std::uint64_t kLeaveMs = 1000;
/* How long a refused ship travel is said over the game. */
constexpr std::uint64_t kRefusedMs = 4000;
/* How long the banner says a member's skip is the host's. */
constexpr std::uint64_t kSkipRefusedMs = 3000;
/* Leaving for the title: how long after a start a game still in the title is on its way into the
 * level (it goes back once there); how long the title is given to show its main menu, and how
 * often the Multiplayer page is asked for meanwhile. */
constexpr std::uint64_t kStartingMs = 3000;
constexpr std::uint64_t kHomeMs = 30000;
constexpr std::uint64_t kHomeRetryMs = 500;

/* How often a member asks again for the host's scene its game has not started (a request the game
 * dropped with a load), and the most a late member's scene is let play on after the host's ended
 * (beyond this its lateness is not waited for: the host's barrier is kBarrierMs). */
constexpr std::uint64_t kSceneAskMs = 3000;
constexpr std::uint64_t kSceneLagMs = 15000;
/* The longest a member is taken to be joining (a game that never gets the host's world answers to
 * the party's scenes again after this). */
constexpr std::uint64_t kJoiningMs = 120000;

/* PLACE's answers: not now, stand beside here (where the host stands, or stood a moment ago), its
 * last safe spot of this level load (asked with `last`), and none at all. */
constexpr std::uint8_t kPlaceWait = 0;
constexpr std::uint8_t kPlaceHere = 1;
constexpr std::uint8_t kPlaceLast = 2;
constexpr std::uint8_t kPlaceNone = 3;
/* How often a member asks for its place; how long it waits for the host to be safe, a scene of the
 * host's not counted, then takes the last safe spot; the most it waits at all; how long a hero
 * asked to move has to get there once in control; how close counts as there. */
constexpr std::uint64_t kPlaceAskMs = 500;
constexpr std::uint64_t kPlaceWaitMs = 10000;
constexpr std::uint64_t kPlaceMostMs = 150000;
constexpr std::uint64_t kPlaceMoveMs = 1500;
constexpr float kPlaceNear = 3.0f;
/* How old the host's safe spot may be and still count as where it stands now (a hop, a menu). */
constexpr std::uint64_t kSafeFreshMs = 2000;
/* How long a placed hero takes no damage from the party's enemies, how long a joiner takes its gifts
 * quietly after it is in, and how soon Go to host works again. */
constexpr std::uint64_t kShieldMs = 3000;
constexpr std::uint64_t kQuietMs = 5000;
constexpr std::uint64_t kGoAgainMs = 5000;
/* The warning a member has before the host's ship takes it along. */
constexpr std::uint64_t kTravelWarnMs = 2500;
/* What a wait is for (WAIT_VOTE's kind): nothing, the players still arriving, a scene. */
constexpr std::uint8_t kWaitNone = 0;
constexpr std::uint8_t kWaitArrival = 1;
constexpr std::uint8_t kWaitScene = 2;

/* What the game is doing (the SDK's openrac_play_status). */
struct GameStatus {
    int phase = 0;
    int level = -1;
    std::uint32_t loads = 0;
    /* The level's in-game scene playing (its number in the level, the SDK's scene_playing), -1 for
     * none (no scene, or a movie, a slideshow, the ship's landing). */
    int scene = -1;
};

/* A game's progress that a party shares (progress.hpp), a byte each, non-zero had: the items owned
 * (item_available: weapons, gadgets, the packs and the rest GiveItem gives), the gold bolts
 * (gold_bolts_collected: level * 4 + the level's number of it; their count is how many are set)
 * and the planets unlocked (level_available). */
constexpr std::size_t kProgressItems = 40;
constexpr std::size_t kProgressGoldBolts = 76;
constexpr std::size_t kProgressPlanets = 20;

struct ProgressBits {
    std::array<std::uint8_t, kProgressItems> items{};
    std::array<std::uint8_t, kProgressGoldBolts> gold{};
    std::array<std::uint8_t, kProgressPlanets> planets{};

    bool operator==(const ProgressBits& o) const {
        return items == o.items && gold == o.gold && planets == o.planets;
    }

    bool operator!=(const ProgressBits& o) const { return !(*this == o); }
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
 * level's record, the same in every game; -1 for one spawned), class, flags, the game's state
 * byte, and pose. */
struct MobyState {
    int slot = -1;
    int key = -1;
    int oclass = 0;
    std::uint32_t flags = 0;
    std::uint32_t state = 0;
    /* The mission it goes with, -1 for none. */
    int mission = -1;
    Pose pose;
};

/* openrac_moby's flags. */
constexpr std::uint32_t kMobyUpdates = 0x1;
constexpr std::uint32_t kMobyDriven = 0x2;
constexpr std::uint32_t kMobyPlaced = 0x4;
constexpr std::uint32_t kMobyCreature = 0x8;
constexpr std::uint32_t kMobyDead = 0x10;
constexpr std::uint32_t kMobyScene = 0x20;
constexpr std::uint32_t kMobyHidden = 0x40;
constexpr std::uint32_t kMobyDrawn = 0x80;

/* A hit (the SDK's openrac_hit): victim and attacker by slot (-1 none). */
struct Hit {
    int victim = -1;
    int attacker = -1;
    std::uint32_t mask = 0;
    float damage = 0;
    float position[3] = {0, 0, 0};
    float velocity[3] = {0, 0, 0};
};

/* How the game ran the level's mobys in the last tick, and one moby in it (the SDK's
 * openrac_moby_run: the verbose log's, trace.hpp). */
struct MobyRun {
    std::uint32_t runs = 0;
    float camera[3] = {0, 0, 0};
    std::uint32_t menu_ticks = 0;
    int menu = 0;
    float level_camera[3] = {0, 0, 0};
    int level_mode = 0;
    int context = 0;
    int update_dist = 0;
    int draw_dist = 0;
    int drawn = 0;
    int ran = -1;
    float distance = 0;
};

/* What the game's card writes do (Game::card_filter; the SDK's OPENRAC_CARD_*). */
constexpr int kCardPass = 0;
constexpr int kCardRedirect = 2;

/* openrac_moby_run.ran and .menu (OPENRAC_MOBY_RUN_*, OPENRAC_RUN_MENU_*). */
constexpr int kRunNone = -1;
constexpr int kRunFar = 0;

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
    /* A moby hidden from drawing or shown again; the game's last deletion of a slot (despawn.hpp's
     * MobyDeletion: by, by_class, by_mod, oclass); where the camera is. False for none. */
    virtual bool moby_hide(int /*slot*/, bool /*hidden*/) { return false; }
    virtual bool moby_deletion(int /*slot*/, int& /*by*/, int& /*by_class*/, bool& /*by_mod*/) {
        return false;
    }
    virtual bool camera(float* /*xyz*/) { return false; }
    /* How the last tick ran the level's mobys and the moby in `slot` (-1: none) in it. */
    virtual bool moby_run(int /*slot*/, MobyRun& /*out*/) { return false; }
    virtual int moby_spawn(int /*oclass*/, const Pose& /*pose*/) { return -1; }
    virtual int figure_slot(int /*id*/) { return -1; }
    virtual bool stand_in(const float* /*position*/, int /*figure*/) { return false; }
    virtual bool hit(const Hit& /*hit*/) { return false; }
    virtual void eyes(const std::vector<float>& /*xyz*/) {}
    virtual int bolts(int /*add*/) { return 0; }
    /* The shared progress (progress.hpp): what this game has (false outside a level), and an item
     * given as the game gives one (GiveItem, not in hand), a gold bolt marked collected, a planet
     * unlocked as the game unlocks one (UnlockPlanet). Nothing is written to the memory card. */
    virtual bool progress(ProgressBits& /*out*/) { return false; }
    virtual bool give_item(int /*id*/) { return false; }
    virtual bool give_gold_bolt(int /*index*/) { return false; }
    virtual bool unlock_planet(int /*planet*/) { return false; }
    /* The world kept running under the game's menus (the SDK's play_overlay_pause). */
    virtual void overlay_pause(bool /*on*/) {}
    /* The port's card filter (cosave.hpp, the SDK's card_filter and its fellows): none in a game
     * without it. */
    virtual bool has_card_filter() { return false; }
    virtual bool card_filter(int /*mode*/) { return false; }
    virtual void card_slot(int /*slot*/) {}
    virtual bool card_write(int /*slot*/, const Bytes& /*save*/) { return false; }
    virtual Bytes card_read(int /*slot*/) { return {}; }
    virtual std::uint32_t card_saves() { return 0; }
    virtual std::string card_note(int /*slot*/) { return {}; }
    virtual void card_note_write(int /*slot*/, const std::string& /*text*/) {}
    /* The clock on the wall, in seconds since 1970. */
    virtual std::int64_t wall_seconds() { return 0; }
    /* The scene playing skipped as the game's own skip does (the SDK's play_skip). */
    virtual void skip() {}
    /* The level's scene `scene` started as the game's own trigger starts it, once the player is in
     * control; -1 drops a request still waiting (the SDK's play_scene). False when it cannot. */
    virtual bool play_scene(int /*scene*/) { return false; }
    /* The rules this game's mods published, for a party it hosts; the host's rules for the mod
     * `id` handed to this game's same mod (empty: there are none any more). False when it has no
     * such mod, or it takes none (party_mods.hpp). */
    virtual std::vector<PartyRules> party_rules() { return {}; }
    virtual bool deliver_rules(const std::string& /*id*/, const Bytes& /*data*/) { return false; }
    /* The shots the hero fired since the last call; a figure firing one (hitting nothing here). */
    virtual std::vector<Shot> hero_shots() { return {}; }
    virtual bool figure_shot(int /*id*/, const Shot& /*shot*/) { return false; }
    /* The title's Multiplayer page opened (the mod's Home, from the title's main menu): true when
     * asked. */
    virtual bool open_home() { return false; }
    /* Each work package adds its virtuals in its own block, and only there (with a default that
     * does nothing), implemented in the package's own piece of the mod's game adapter. */
    // --- WP-SAVE ---
    // --- end WP-SAVE ---
    // --- WP-SHARE ---
    /* The shared progress past the items (shared.hpp; the SDK's shared_read and shared_give): a
     * section's bytes (false: none), and bytes merged in, upward only and silently. */
    virtual bool shared_read(int /*tag*/, Bytes& /*out*/) { return false; }
    virtual bool shared_give(int /*tag*/, const Bytes& /*data*/) { return false; }
    // --- end WP-SHARE ---
    // --- WP-SHOTS ---
    // --- end WP-SHOTS ---
    // --- WP-DRAW ---
    // --- end WP-DRAW ---
    // --- WP-JOIN ---
    /* Whether this game can put its hero somewhere. */
    virtual bool can_place() { return false; }
    /* The hero put beside `position` (a spot around it, starting behind `yaw`, looking at it) or, not
     * `beside`, at `position`, unharmed for a moment; it happens once the player is in control, and
     * not at all when the game finds no spot. False when refused. */
    virtual bool place(const float* /*position*/, float /*yaw*/, bool /*beside*/) { return false; }
    /* A level's name as the game shows it; empty when not known. */
    virtual std::string level_name(int /*level*/) { return {}; }
    /* While on, what the party's progress gives (give_item, unlock_planet) comes without the game's
     * banners, everything else about it as ever. */
    virtual void quiet_gifts(bool /*on*/) {}
    // --- end WP-JOIN ---
    // --- WP-CAMERA ---
    // --- end WP-CAMERA ---
    // --- WP-TETHER ---
    // --- end WP-TETHER ---
    // --- WP-DEATH ---
    /* Whether this game can keep a dead hero down, and bring him back. */
    virtual bool can_revive() { return false; }
    /* While on, a hero who dies on his feet stays down until he is revived or sent back. */
    virtual void hold_death(bool /*on*/) {}
    /* A dead hero up again where he lies. */
    virtual bool revive() { return false; }
    /* The hero back at his checkpoint now, dead or not (the game's own respawn). */
    virtual bool respawn() { return false; }
    /* Whether mission `mission` of `level` is done in this game. */
    virtual bool mission_done(int /*level*/, int /*mission*/) { return false; }
    // --- end WP-DEATH ---
    // --- WP-LOBBY ---
    // --- end WP-LOBBY ---
    // --- WP-CROSS ---
    /* Whether a save can be given to the game: it may come from a host on another disc, and a host
     * is not trusted with what the game would read (save_check.hpp). */
    virtual bool save_usable(const Bytes& /*save*/) { return true; }
    /* What must agree for two games to share `level` as loaded (the port's sim digest); 0 when it
     * is not known. */
    virtual std::uint32_t sim_digest(int /*level*/) { return 0; }
    // --- end WP-CROSS ---
    // --- WP-CODES ---
    // --- end WP-CODES ---
    // --- WP-MIGRATE ---
    // --- end WP-MIGRATE ---
};

/* A remote player as the co-op keeps it: its poses and its figure. */
struct Remote {
    Interpolator poses;
    bool scene = false;  // its game plays a scene (its last pose said)
    int figure = -1;
    bool hidden = false;         // its figure hidden for a scene playing in this game
    float shown_at[3] = {0, 0, 0};  // where its figure was last put
    bool late = false;              // its poses or its link lag: its name tag says so
    std::uint64_t retry_ms = 0;  // when a figure may be made again
    std::uint64_t traced_ms = 0;  // when the verbose log last said how its poses are shown
    /* Its shots to fire, each when its figure is as far behind as its poses are played. */
    struct PendingShot {
        double due_ms = 0;
        std::int32_t level = -1;
        Shot shot;
        /* When it should have been fired: what lateness is counted from. */
        double shown_ms = 0;
    };
    std::deque<PendingShot> shots;
};

class Coop {
public:
    /* `clock` gives the monotonic milliseconds poses arrive at (now_ms when none is given). */
    Coop(Session& session, Game& game, std::function<std::uint64_t()> clock = {});

    /* The verbose log (trace.hpp; null or off: none): each pose received (its stamp, when it came,
     * its time in transit) and twice a second how each figure's poses are shown ("pose" lines). */
    void trace(Trace* trace) { trace_ = trace; }

    /* What every game of a party must have the same of (the mod's version, OpenRAC's, the disc's world family, the
     * gameplay mods' digest): the host sends it with PLAY, and a member with another refuses to
     * start. */
    void set_build(std::string build) { build_ = std::move(build); }

    /* Who may get the host's game (party_mods.hpp: a member whose mods match); none set: anyone. */
    void set_gate(std::function<bool(std::int32_t)> gate) { gate_ = std::move(gate); }

    /* The party's saves (cosave.hpp): told before a PLAY goes to a target, and before a game of the
     * party's starts here, with the save it starts from. */
    void set_save_hooks(std::function<void(std::int32_t)> announce,
                        std::function<void(const Bytes&)> starting) {
        announce_ = std::move(announce);
        starting_ = std::move(starting);
    }

    /* The host is starting again: told before anyone hears of the new game. */
    void on_start_again(std::function<void()> again) { start_again_ = std::move(again); }

    /* The host's choice (Create match's save picker): a new game (no bytes) or a save, and what the
     * lobby and the list say of it. */
    void choose(Bytes save, std::string description);

    const Bytes& chosen() const { return chosen_; }

    const std::string& description() const { return description_; }

    /* The host's Start: everyone plays the chosen game, the host too. False when it cannot (not
     * the host, already playing, the game not in the title, a member not ready). */
    bool start();

    /* Whether start() would start now: the first time once all are ready; the host back in the
     * title with the party's game on starts it again whatever the members are doing. */
    bool can_start() const;

    /* Whether the host is back in the title from a game of this party (Start starts it again). */
    bool starts_again() const;

    /* A member back in the title with the host's game on: asks for it again (WANT), as one that
     * missed it does. False for the host, or with no game on. */
    bool rejoin();

    /* One of the game's DATA messages (GameKind first), from `sender`. */
    void handle(std::int32_t sender, const Bytes& bytes);

    /* Once a game tick, `now_ms` the monotonic clock. */
    void tick(std::uint64_t now_ms);

    /* The party is gone (left, ended, the connection lost): the figures go, nothing is pending. */
    void reset();

    /* Leaving: whether this game is on its way back to the title after its party went, and whether
     * the title's Multiplayer page is still to open; home_shown() says it did (its page showed). */
    bool to_title() const { return to_title_; }

    bool wants_home() const { return home_; }

    void home_shown() { home_ = false; }

    /* What the pages show: whether the host's game is on (a member: PLAY came), whether a member
     * waits for its save, the level the host plays, the remote players shown now. */
    bool playing() const { return play_ != 0; }

    /* This game is joining, or just joined: what the party's progress gives comes quietly
     * (Game::quiet_gifts), and need not wait for control. */
    bool quiet_gifts() const { return quiet_; }

    std::int32_t play() const { return play_; }

    bool receiving() const { return pending_ && received_ < expected_; }

    int host_level() const { return host_level_; }

    int figures() const;

    const std::map<std::int32_t, Remote>& remotes() const { return remotes_; }

    /* A remote player's latest pose (its newest, not the played-back one) and its figure; false
     * for one with none. */
    bool latest(std::int32_t player, Pose& out) const;
    /* A member: what it heard of WORLD, for its next pose to say (world.hpp); none after a load.
     * The host: a member's latest word on it, once (false: nothing new). */
    void world_ack(std::uint16_t newest, std::uint32_t bits) {
        world_ack_ = true;
        world_newest_ = newest;
        world_bits_ = bits;
    }

    void no_world_ack() { world_ack_ = false; }

    bool take_world_ack(std::int32_t player, std::uint16_t& newest, std::uint32_t& bits);
    /* Whether a SHOTX went in the last second: the party fires, and it takes from WORLD's share. */
    bool firing(std::uint64_t now) const { return shots_sent_ms_ != 0 && now - shots_sent_ms_ < kShotShareMs; }

    /* The players whose figures are shown here now, for their name tags (nametags.hpp). */
    std::vector<NameTag> name_tags() const;

    /* What the frame's tags came to, kept for the bot. */
    void note_tags(const TagStats& stats) { tag_stats_ = stats; }

    const TagStats& tag_stats() const { return tag_stats_; }

    int figure_of(std::int32_t player) const;

    /* The hero's shots sent, and the remote players' shots their figures fired. */
    std::uint64_t shots_sent() const { return shots_sent_; }
    std::uint64_t shots_fired() const { return shots_fired_; }
    /* The shots dropped as too late to show, for the log and the tests. */
    std::uint64_t shots_late() const { return shots_late_; }

    /* The scene barrier, for the pad: whether this player's control is held (out of a scene while
     * another player is still in one). */
    bool hold_input() const { return hold_ || joining_hold_; }

    /* This player pressed Start in a scene or a movie (`phase`): the host's skips it for the party;
     * a member's skips nothing (start_blocked) and has the banner say only the host can. */
    void own_skip(int phase, std::uint64_t now);

    /* Whether the pad's Start is taken out in `phase`: a member's in a scene or a movie of a game
     * of the party's (only the host skips). */
    bool start_blocked(int phase) const;

    /* Whether the pad should press Start for a movie now (a party's skip of the movie playing):
     * pressed and let go in turns for a while, so the movie player sees a press. */
    bool movie_press(std::uint64_t now);

    bool host_scene() const { return host_scene_; }

    /* The game's ship flies from `here` to `dest` (its planet select closed): true lets it, false
     * has it land again where it is (a member's travel to another planet while the party plays:
     * only the host flies). The host's departure: every member leaves with it. */
    bool ship_travel(int dest, int here);

    /* A member coming into a game already on (Joining, above), and the world telling it that it
     * has the host's world of the level it stands in. */
    bool joining() const { return joining_; }

    void world_ready(bool ready) { world_ready_ = ready; }

    /* Whether the party's enemies leave this hero alone now: joining, or just placed. */
    bool shielded() const;

    /* Go to host: whether a member can ask now, and it asking (false, and why on the banner, when
     * it cannot). */
    bool can_go() const;
    bool go_to_host(std::uint64_t now);
    bool going() const { return placing_ != PlaceFor::kNone; }

    /* A wait for others, as this player sees it: its kind (kWait*), the votes and who votes. */
    std::uint8_t wait_kind() const { return wait_.kind; }
    int wait_votes() const { return wait_.votes; }
    int wait_voters() const { return wait_.voters; }

    /* Whether the party is arriving after a ship travel (the barrier above): the world waits. */
    bool arriving() const { return arriving_; }

    /* The line to show over the game now (the SDK's play_notice), empty for none: a member's ship
     * travel refused (a while), the party waiting for its players to arrive. */
    std::string banner(std::uint64_t now) const;

    /* The players the host still waits for (the host's side). */
    std::size_t waiting_for() const { return waiting_for_.size(); }

    /* The last thing worth a line (a start, a save that would not load); take_notice() gives it
     * once. */
    std::string take_notice();

    /* Where the co-op says what it does, a line at a time (the mod's log); none: nowhere. */
    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

private:
    void send_play(std::int32_t target, const Bytes& save, int level, bool joining);
    void about_to_start(const Bytes& save);
    /* A member's about_to_start, once for a start. */
    void start_pending();
    void tick_joining(std::uint64_t now, const GameStatus& status);
    /* Being put beside the host (coop_join.cpp): a request begun and ended, what the host said, the
     * member's tick, the host's account of where it is safe and its answer. */
    enum class PlaceFor : std::uint8_t {
        kNone,
        kJoin,
        kHost
    };
    void begin_place(PlaceFor why, std::uint64_t now);
    void end_place(std::uint64_t now, const std::string& how);
    void place_at(const float* position, float yaw, bool beside, std::uint64_t now);
    void no_place(std::uint64_t now);
    void on_place(bool from_host, Reader& r);
    void on_place_ask(std::int32_t sender, Reader& r);
    void tick_place(std::uint64_t now, const GameStatus& status);
    void tick_safe(std::uint64_t now, const GameStatus& status);
    /* A new `play` of the host's: what the old one left, on either side (coop_join.cpp). */
    void clear_game();
    /* The host's game asked of a member in a level (Starting again). */
    void tick_again(std::uint64_t now);
    /* Waits for others (coop_wait.cpp): this player's Start in one, a wait gone by, the host's
     * count and word, a vote heard. */
    void skip_wait(std::uint64_t now);
    void release_wait(std::uint64_t now, const char* why);
    void tick_wait(std::uint64_t now, const GameStatus& status);
    void send_wait(std::uint64_t now, bool passed);
    void on_wait_vote(std::int32_t sender, bool from_host, Reader& r);
    void on_pose(std::int32_t sender, const Bytes& bytes);
    void on_poses(std::int32_t sender, Reader& r);
    /* The host: the poses waiting to go out, with `own` when given, in as few datagrams as fit. */
    void send_poses(const Bytes* own);
    void tick_host(std::uint64_t now, const GameStatus& status);
    void tick_member(std::uint64_t now, const GameStatus& status);
    void send_pose(std::uint64_t now, const GameStatus& status);
    void show_remotes(std::uint64_t now, const GameStatus& status);
    void send_shots(const GameStatus& status);
    void on_shot(std::int32_t sender, const Bytes& bytes);
    void on_shotx(std::int32_t sender, const Bytes& bytes);
    void take_shot(const ShotEntry& shot);
    void queue_shot(const ShotEntry& shot);
    void send_queued_shots(std::uint64_t now);
    void fire_shots(Remote& r, std::uint64_t now, const GameStatus& status);
    void update_barrier(std::uint64_t now, const GameStatus& status);
    void send_level(std::uint64_t now, int level, bool scene);
    void tick_arrival(std::uint64_t now, const GameStatus& status);
    void drop_figure(std::int32_t player, Remote& r, const char* why);
    /* A figure hidden for this game's scene, or shown again; false when the game cannot hide it. */
    bool hide_figure(std::int32_t player, Remote& r, bool hidden);
    void tick_leaving(std::uint64_t now);
    /* The level's scenes played together (coop_scenes.cpp): SCENE heard, the host's scenes told,
     * a member's started, and the lateness a member's scene is let play out for. */
    void on_scene(bool from_host, Reader& r);
    void tick_host_scene(std::uint64_t now, const GameStatus& status);
    void tick_member_scene(std::uint64_t now, const GameStatus& status);
    void send_scene(std::uint64_t now);
    void reset_scenes();
    std::uint64_t scene_lag_ms() const { return shared_lag_ms_; }
    /* Skips and the scenes they are for (coop_scenes.cpp): the game's scenes and loads counted as
     * they start (each its own instance: a skip acts on the one it came in), SKIP heard, a skip
     * begun on the scene playing here, the host's scene sequence of the one this game plays (0 for
     * none), a member's SCENE_STATE sent and the host's heard. */
    void note_phase(const GameStatus& status);
    void on_skip(std::int32_t sender, bool from_host, const Bytes& bytes, Reader& r);
    void skip_here(std::uint64_t now);
    std::uint16_t shared_here(std::uint64_t now, const GameStatus& status);
    void send_scene_state(std::uint64_t now);
    void on_scene_state(std::int32_t sender, Reader& r);
    bool members_in_scene(std::uint64_t now, const GameStatus& status) const;

    /* Whether `member` may get the host's game. */
    bool may_play(std::int32_t member) const { return !gate_ || gate_(member); }

    Session& session_;
    Game& game_;
    std::string build_;
    std::function<bool(std::int32_t)> gate_;
    /* Whether this game plays the party's game (the host's own once started, a member's once it
     * started the host's): it goes back to the title when the party goes. When it last started one
     * (0 none). */
    bool party_game_ = false;
    std::uint64_t start_ms_ = 0;
    /* Whether the game was asked to keep its world running under its menus. */
    bool overlay_ = false;
    std::function<std::uint64_t()> clock_;
    Trace* trace_ = nullptr;
    Bytes chosen_;
    std::string description_;
    /* The host's game (0: none yet): its id, and the members told of it. */
    std::int32_t play_ = 0;
    std::vector<std::int32_t> told_;
    std::function<void(std::int32_t)> announce_;
    std::function<void(const Bytes&)> starting_;
    std::function<void()> start_again_;
    bool start_copied_ = false;

    /* A member: the host's PLAY waiting to be started (its mode, level and save), and which play
     * the member started last. */
    bool pending_ = false;
    std::int32_t pending_play_ = 0;
    int pending_level_ = 0;
    bool pending_save_ = false;
    bool pending_joining_ = false;
    std::size_t expected_ = 0;
    std::size_t received_ = 0;
    Bytes save_;
    /* A member coming into a game on (Joining, above): since when, the world's word, whether the
     * pad presses Start this report. */
    bool joining_ = false;
    std::uint64_t joining_since_ = 0;
    bool world_ready_ = false;
    bool joining_press_ = false;
    bool joining_hold_ = false;
    /* Being put beside the host: why (kNone: not), this asking's ticket, since when it waits (a
     * host's scene puts this forward), when it asked last, whether it now asks for the last safe
     * spot; whether the game was asked to move the hero, when, to where, and whether that was the
     * landing spot. */
    PlaceFor placing_ = PlaceFor::kNone;
    std::uint16_t place_ticket_ = 0;
    std::uint64_t place_since_ = 0;
    std::uint64_t place_began_ = 0;
    std::uint64_t place_ask_ms_ = 0;
    bool place_last_ = false;
    bool place_moving_ = false;
    std::uint64_t place_moved_ms_ = 0;
    float place_to_[3] = {0, 0, 0};
    bool place_landing_ = false;
    /* Until when: no damage, no banners, no Go to host, "no safe spot" on the banner. */
    std::uint64_t shield_until_ = 0;
    std::uint64_t quiet_until_ = 0;
    bool quiet_ = false;
    std::uint64_t go_again_ms_ = 0;
    std::uint64_t no_spot_until_ = 0;
    /* A member: where this level load put the hero. The host: its last safe pose, when, its load. */
    Pose landing_;
    bool has_landing_ = false;
    std::uint32_t landing_loads_ = 0;
    Pose safe_;
    std::uint64_t safe_ms_ = 0;
    std::uint32_t safe_loads_ = 0;
    /* The host: whether it has been in a level of this game. */
    bool host_played_ = false;
    /* Starting again, a member: going to the title for the host's new game, since when, last asked. */
    bool again_ = false;
    std::uint64_t again_since_ = 0;
    std::uint64_t again_ms_ = 0;
    /* A wait for others. Its id and kind as the host told it (the host's own too), the votes, who
     * votes; a member's vote given; the host's voters' ids; whether the barrier was let go, until
     * what it waited for is over, and the host's scene that was; when the host last told it. */
    struct Wait {
        std::uint16_t id = 0;
        std::uint8_t kind = kWaitNone;
        int votes = 0;
        int voters = 0;
    };
    Wait wait_;
    std::uint16_t wait_count_ = 0;
    std::uint16_t voted_ = 0;
    std::vector<std::int32_t> votes_;
    bool barrier_open_ = false;
    std::uint16_t released_scene_ = 0;
    std::uint64_t wait_sent_ms_ = 0;
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
    /* Skips: this player's last (its sequence), when it was pressed, until when a skip asked of
     * this game still acts (on its scene or movie only: skip_scene_, movie_load_), and the presses
     * for a movie. */
    std::uint16_t skip_sequence_ = 0;
    std::uint64_t own_skip_ms_ = 0;
    std::uint64_t skip_until_ = 0;
    int movie_presses_ = 0;
    /* Whether this game played a scene at the last tick, and whether a skip was asked in it. */
    bool in_scene_ = false;
    bool skip_asked_ = false;
    int sent_level_ = -2;
    /* Ship travels. The host's: its count, the planet it flies to, whether the party is arriving
     * there, since when, the level load it left from, the members it waits for, and whether the
     * last LEVEL said it arrived. A member's: the host's last travel heard (whether any), the
     * planet to leave for (-1 none), when it was last asked, the load it was asked in, and whether
     * the host's LEVEL says the party still arrives. */
    std::uint16_t travel_ = 0;
    int travel_dest_ = -1;
    bool arriving_ = false;
    std::uint64_t depart_ms_ = 0;
    std::uint32_t depart_loads_ = 0;
    std::vector<std::int32_t> waiting_for_;
    bool sent_arriving_ = false;
    bool heard_travel_ = false;
    std::uint16_t host_travel_ = 0;
    int leave_dest_ = -1;
    std::uint64_t leave_at_ = 0;
    std::uint64_t leave_ms_ = 0;
    std::uint32_t leave_loads_ = 0;
    bool host_arriving_ = false;
    /* Until when the refusal of a member's ship travel is shown. */
    std::uint64_t refused_until_ = 0;
    std::uint64_t skip_refused_until_ = 0;
    /* The level's scenes. The host's: the scene playing (-1 none), its level and sequence, when it
     * started and was last told. A member's: the host's scene (-1 none), its sequence, level and
     * when it started by this clock; whether it played here already, when it was asked for last
     * (0: no request waiting), and the host's scene playing here and how late it started. */
    int scene_ = -1;
    int scene_level_ = -1;
    std::uint16_t scene_sequence_ = 0;
    std::uint64_t scene_start_ms_ = 0;
    std::uint64_t scene_sent_ms_ = 0;
    int host_scene_id_ = -1;
    std::uint16_t host_scene_sequence_ = 0;
    int host_scene_level_ = -1;
    std::uint64_t host_scene_start_ms_ = 0;
    bool scene_done_ = false;
    std::uint64_t scene_asked_ms_ = 0;
    int shared_scene_ = -1;
    std::uint64_t shared_lag_ms_ = 0;
    /* A member's more: when the host's scene ended (0: it plays on, as far as this game knows),
     * when this game was first asked to start it, whether it is still to play here (the barrier
     * waits for it), the sequence of the one playing here, and its SCENE_STATE last sent. */
    std::uint64_t host_scene_end_ms_ = 0;
    std::uint64_t scene_first_ask_ms_ = 0;
    bool scene_pending_ = false;
    std::uint16_t shared_sequence_ = 0;
    std::uint16_t state_sequence_ = 0;
    std::uint8_t state_sent_ = 0xFF;
    std::uint64_t state_ms_ = 0;
    /* The host's: each member's last SCENE_STATE (its sequence, state, when heard). */
    struct SceneState {
        std::uint16_t sequence = 0;
        std::uint8_t state = kSceneOut;
        std::uint64_t ms = 0;
    };
    std::map<std::int32_t, SceneState> scene_states_;
    /* Instances: the game's scenes and loads (a movie plays in one) counted as they start, its
     * phase (not a menu) and scene last seen; the scene a party's skip acts on and the load its
     * movie presses are for (0: none); the host's scene sequence the party skipped last (0 none). */
    std::uint32_t scene_instance_ = 0;
    std::uint32_t load_instance_ = 0;
    int noted_phase_ = -1;
    int noted_scene_ = -1;
    std::uint32_t skip_scene_ = 0;
    std::uint32_t movie_load_ = 0;
    std::uint16_t party_skip_ = 0;
    /* The game's state last logged. */
    GameStatus logged_{-1, -2, 0};
    /* Leaving for the title: since when, whether the game was out of the title meanwhile, when the
     * travel was last asked (0 not yet), whether it was taken; then the Multiplayer page wanted,
     * since when, when last asked. */
    bool to_title_ = false;
    std::uint64_t title_since_ = 0;
    bool left_title_ = false;
    std::uint64_t title_ms_ = 0;
    bool title_taken_ = false;
    bool home_ = false;
    std::uint64_t home_since_ = 0;
    std::uint64_t home_ms_ = 0;
    /* The host: each member's newest pose not passed on yet, and when they last went. */
    std::map<std::int32_t, Bytes> relay_;
    std::uint64_t relay_ms_ = 0;
    /* WORLD's delivery: a member's own word for its next pose; the host, each member's latest. */
    bool world_ack_ = false;
    std::uint16_t world_newest_ = 0;
    std::uint32_t world_bits_ = 0;
    struct WorldAck {
        std::uint16_t newest = 0;
        std::uint32_t bits = 0;
    };
    std::map<std::int32_t, WorldAck> world_acks_;
    /* Poses sent: when, the sequence, the level load of the last. */
    std::uint64_t pose_ms_ = 0;
    /* The verbose log: this game's hero's z each tick, said twice a second (trace_hero). */
    struct HeroZ {
        std::uint64_t since_ms = 0;
        int ticks = 0;
        int changes = 0;
        float low = 0, high = 0, last = 0;
    };
    HeroZ hero_z_;
    void trace_hero(std::uint64_t now, const GameStatus& status);
    std::uint16_t sequence_ = 0;
    std::uint32_t pose_loads_ = 0;
    bool pose_sent_ = false;
    /* What goes out in the next SHOTX, when each was queued. */
    struct QueuedShot {
        ShotEntry entry;
        std::uint64_t queued_ms = 0;
    };
    std::deque<QueuedShot> shots_out_;
    std::uint64_t shots_sent_ms_ = 0;
    /* The shots sent and the remote players' shots fired, for the log and the tests. */
    std::uint64_t shots_sent_ = 0;
    std::uint64_t shots_fired_ = 0;
    std::uint64_t shots_late_ = 0;
    Pose last_pose_;
    /* The level loads the figures were made in. */
    std::uint32_t figure_loads_ = 0;
    TagStats tag_stats_;
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
