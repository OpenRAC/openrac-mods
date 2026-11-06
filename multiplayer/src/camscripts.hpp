/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Camera scripts shown to the players near (the level's shots, its doors, its lifts). No socket and
 * no game here: given the Session, the Game and the Coop, on the game's thread, once a tick.
 *
 * What a script is. The game has no list of them: any moby's update calls CameraScript with a view,
 * moves it each tick and hands back. A script runs where its moby runs, and most mobys run in the
 * host's game only (world.hpp), so the others would see nothing of a door opening beside them. A
 * member's game cannot run that moby, so nothing is started there "by number": the host sends what
 * the camera shows, and a game shows it. Its `id` is the class of the moby running it, the same on
 * both discs. The views are the host's game's as they are, so the small differences of a level's
 * camera data between discs do not come into it, and nothing here needs a table of a disc's.
 *
 * A script of the host's that a member's stand-in set off (the world.hpp gate, the member being the
 * player its moby goes for, by World::pick_target) is the member's: the port keeps this game's
 * camera and hero out of it, and its views go to the party, this game's player among them, by the
 * same rules.
 *
 * Who sees one goes by its class (camera_share):
 *   kCamOwn     the player's own, and any class not named: a player's seat, ride or machine, a
 *               gold bolt's scene (settled: the collector's), the shots that open a fight;
 *   kCamRiders  a lift's: those on it (kCamRiderReach of it when it starts);
 *   kCamNear    the level showing something happen: those within kCamNear of its moby.
 * Why each class is where it is: DECISIONS.md, "Camera scripts".
 *
 * Only the camera moves: nobody's control is ever held for a script, and it is never played in a
 * way that costs a life. It starts only for a player who is in control, not hurt for kCamHurtMs and
 * touching nothing for kCamQuietMs, standing in the level with the host's world, in no scene, in no
 * script of its own, not in first person (the port refuses that). Anything pressed or a hit takes
 * the camera back to the hero at once, and so do a scene, a menu, or kCamStaleMs of silence. A
 * script already under way is not started: a joiner, or a player who comes near late, plays on.
 *
 * DATA kind 52 CAMSCRIPT (reliable, the host to all): play i32, level i32, sequence u16, id i16,
 * started u8; started: where its moby is, the first eye and rotation (3 x f32 each).
 * DATA kind 53 CAMSTATE (as state, the host to all, every kCamStateMs while it runs): sequence u16,
 * stamp i64 (the server's clock, ms), eye and rotation (3 x f32 each). Played kCamDelayMs back, at
 * least two sends behind, so a late one is still in time.
 */
#ifndef MULTIPLAYER_CAMSCRIPTS_H
#define MULTIPLAYER_CAMSCRIPTS_H

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "coop.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "trace.hpp"

namespace mp {

constexpr std::uint8_t kCamScriptKind = kind::kCamScript;
constexpr std::uint8_t kCamStateKind = kind::kCamState;

enum class CamShare : std::uint8_t { kCamOwn, kCamRiders, kCamNear };

/* Who sees a script of class `oclass` beside its player (above). */
CamShare camera_share(int oclass);

constexpr float kCamNear = 60.0f;
constexpr float kCamRiderReach = 4.0f;
constexpr std::uint64_t kCamStateMs = 50;
constexpr std::uint64_t kCamDelayMs = 2 * kCamStateMs;
constexpr std::uint64_t kCamStaleMs = 1000;
constexpr std::uint64_t kCamQuietMs = 500;
constexpr std::uint64_t kCamHurtMs = 3000;
/* A step of the eye past this between two views is a cut in the script, not a move. */
constexpr float kCamCut = 15.0f;

/* Whether a pad report (the first pad's, multiplayer.cpp) has anything pressed or a stick out of
 * the game's dead zone. */
bool pad_active(const std::uint8_t* report, int size);

struct CamScript {
    std::int32_t play = 0;
    std::int32_t level = -1;
    std::uint16_t sequence = 0;
    std::int32_t id = -1;
    bool started = false;
    float where[3] = {0, 0, 0};
    float eye[3] = {0, 0, 0};
    float rotation[3] = {0, 0, 0};
};

struct CamState {
    std::uint16_t sequence = 0;
    std::int64_t stamp = 0;
    float eye[3] = {0, 0, 0};
    float rotation[3] = {0, 0, 0};
};

Bytes encode_camscript(const CamScript& script);
bool decode_camscript(const Bytes& bytes, CamScript& out);
Bytes encode_camstate(const CamState& state);
bool decode_camstate(const Bytes& bytes, CamState& out);

class CamScripts {
public:
    /* `clock` gives the monotonic milliseconds of `tick` and `input` (now_ms when none is given). */
    CamScripts(Session& session, Game& game, Coop& coop, std::function<std::uint64_t()> clock = {});

    /* A CAMSCRIPT or CAMSTATE from `sender`. */
    void handle(std::int32_t sender, const Bytes& bytes);

    /* The game's word of a script of its own starting or ending (the SDK's on_camera_script). */
    void script(int id, bool started);

    /* The first pad's report had something pressed, at `now`. */
    void input(std::uint64_t now);

    /* Once a game tick. */
    void tick(std::uint64_t now);

    /* The party or its game is gone: nothing is kept, and the camera is the hero's. */
    void reset();

    /* What it does, for the tests and the bot. */
    bool showing() const { return showing_; }

    std::uint64_t shown() const { return shown_; }

    std::uint64_t sent() const { return sent_; }

    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

    void trace(Trace* trace) { trace_ = trace; }

private:
    struct View {
        std::int64_t stamp = 0;
        float eye[3] = {0, 0, 0};
        float rotation[3] = {0, 0, 0};
    };

    void send_view(std::uint64_t now);
    void heard_start(const CamScript& script, std::uint64_t now);
    void heard_view(const View& view, std::uint64_t now);
    void heard_end(std::uint64_t now);
    bool may_start(const CamScript& script, std::uint64_t now);
    void show(std::uint64_t now);
    void take_back(bool cut, const std::string& why);
    /* The view at `when`, from what is kept. */
    View view_at(std::int64_t when) const;
    void log(const std::string& line) const {
        if (log_) {
            log_(line);
        }
    }

    Session& session_;
    Game& game_;
    Coop& coop_;
    std::function<std::uint64_t()> clock_;
    std::function<void(const std::string&)> log_;
    Trace* trace_ = nullptr;

    // The host: the script it is telling of.
    bool telling_ = false;
    bool telling_own_ = false;
    int telling_id_ = -1;
    std::uint16_t sequence_ = 0;
    std::uint64_t view_ms_ = 0;

    // The script heard of and shown here.
    bool heard_ = false;
    std::uint16_t heard_sequence_ = 0;
    std::int32_t heard_play_ = 0;
    std::int32_t heard_level_ = -1;
    int heard_id_ = -1;
    std::uint64_t heard_ms_ = 0;
    bool showing_ = false;
    std::uint64_t show_ms_ = 0;
    std::deque<View> views_;

    // The player.
    std::uint64_t input_ms_ = 0;
    std::uint64_t hurt_ms_ = 0;
    int health_ = -1;

    std::uint64_t shown_ = 0;
    std::uint64_t sent_ = 0;
};

}  // namespace mp

#endif
