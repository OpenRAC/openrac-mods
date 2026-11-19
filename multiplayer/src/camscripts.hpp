// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Camera scripts shown to the players near (the level's shots, its doors, its lifts). No socket and
// no game here: given the Session, the Game and the Coop, on the game's thread, once a tick.
//
// What a script is. The game has no list of them: any moby's update calls CameraScript with a view,
// moves it each tick and hands back. A script runs where its moby runs, and most mobys run in the
// host's game only (world.hpp), so the others would see nothing of a door opening beside them. A
// member's game cannot run that moby, so nothing is started there "by number": the host sends what
// the camera shows, and a game shows it. Its `id` is the class of the moby running it, the same on
// both discs. The views are the host's game's as they are, so the small differences of a level's
// camera data between discs do not come into it, and nothing here needs a table of a disc's.
//
// A script of the host's that a member's stand-in set off (the world.hpp gate, the member being the
// player its moby goes for, by World::pick_target) is the member's: the port keeps this game's
// camera and hero out of it, and its views go to the party, this game's player among them, by the
// same rules.
//
// Who sees one goes by its class (camera_share):
//   kCamOwn     the player's own, and any class not named: a player's seat, ride or machine, a
//               gold bolt's scene (settled: the collector's), the shots that open a fight;
//   kCamRiders  a lift's: those on it (kCamRiderReach of it when it starts);
//   kCamNear    the level showing something happen: those within kCamNear of its moby.
// Why each class is where it is: DECISIONS.md, "Camera scripts".
//
// Only the camera moves: nobody's control is ever held for a script, and it is never played in a
// way that costs a life. It starts only for a player who is in control, not hurt for kCamHurtMs and
// touching nothing for kCamQuietMs, standing in the level with the host's world, in no scene, in no
// script of its own, not in first person (the port refuses that). Anything pressed or a hit takes
// the camera back to the hero at once, and so do a scene, a menu, or kCamStaleMs of silence. A
// script already under way is not started: a joiner, or a player who comes near late, plays on.
//
// DATA kind 52 CAMSCRIPT (reliable, the host to all): play i32, level i32, sequence u16, id i16,
// started u8; started: where its moby is, the first eye and rotation (3 x f32 each).
// DATA kind 53 CAMSTATE (as state, the host to all, every kCamStateMs while it runs): sequence u16,
// stamp i64 (the server's clock, ms), eye and rotation (3 x f32 each). Played kCamDelayMs back, at
// least two sends behind, so a late one is still in time.

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

// The kind bytes of the CAMSCRIPT and CAMSTATE messages.
constexpr std::uint8_t kCamScriptKind = kind::kCamScript;
constexpr std::uint8_t kCamStateKind = kind::kCamState;

// Who sees a script besides the player it belongs to (see the file comment): the player alone,
// those riding, or those near.
enum class CamShare : std::uint8_t {
    kCamOwn,
    kCamRiders,
    kCamNear
};

// Who sees a script of class `oclass` beside its player (above).
CamShare camera_share(int oclass);

// How far from its moby a player sees a kCamNear script, in the game's world units.
constexpr float kCamNear = 60.0f;
// How close to a lift a player must be to ride it when its script starts, in the game's world
// units.
constexpr float kCamRiderReach = 4.0f;
// How often the host sends a view while a script runs, in milliseconds.
constexpr std::uint64_t kCamStateMs = 50;
// How far behind the server's clock views are played, in milliseconds.
constexpr std::uint64_t kCamDelayMs = 2 * kCamStateMs;
// How long without a view before a shown script is taken back, in milliseconds.
constexpr std::uint64_t kCamStaleMs = 1000;
// How long a player must touch nothing before a script may start, in milliseconds.
constexpr std::uint64_t kCamQuietMs = 500;
// How long after a hit a script may not start, in milliseconds.
constexpr std::uint64_t kCamHurtMs = 3000;
// A step of the eye past this between two views is a cut in the script, not a move.
constexpr float kCamCut = 15.0f;

// Whether a pad report (the first pad's, multiplayer.cpp) has anything pressed or a stick out of
// the game's dead zone.
bool pad_active(const std::uint8_t* report, int size);

// The CAMSCRIPT message: a script starting or ending on the host. A plain value.
struct CamScript {
    std::int32_t play = 0;          // The co-op game number it belongs to.
    std::int32_t level = -1;        // The level it runs in.
    std::uint16_t sequence = 0;     // Numbers the script, so its CAMSTATEs find it.
    std::int32_t id = -1;           // The class of the moby running it.
    bool started = false;           // True for a start, false for an end.
    float where[3] = {0, 0, 0};     // Where its moby is (a start only).
    float eye[3] = {0, 0, 0};       // The first eye position (a start only).
    float rotation[3] = {0, 0, 0};  // The first rotation (a start only).
};

// The CAMSTATE message: the host's camera at one moment of a script. A plain value.
struct CamState {
    std::uint16_t sequence = 0;     // The script it belongs to.
    std::int64_t stamp = 0;         // The server clock when it was taken, in ms.
    float eye[3] = {0, 0, 0};       // The eye position.
    float rotation[3] = {0, 0, 0};  // The rotation.
};

// Writes `script` as a CAMSCRIPT message; where, eye and rotation go only for a start.
Bytes encode_camscript(const CamScript& script);
// Reads a CAMSCRIPT into `out`; false for another kind, a cut message or bytes left over.
bool decode_camscript(const Bytes& bytes, CamScript& out);
// Writes `state` as a CAMSTATE message.
Bytes encode_camstate(const CamState& state);
// Reads a CAMSTATE into `out`; false for another kind, a cut message or bytes left over.
bool decode_camstate(const Bytes& bytes, CamState& out);

// The camera scripts of one game: the host tells what its camera shows, and a member's game shows
// it to a player who is near.
//
// Holds references to the Session, the Game and the Coop, which outlive it. Only the game's thread
// uses it.
class CamScripts {
public:
    // `clock` gives the monotonic milliseconds of `tick` and `input` (now_ms when none is given).
    CamScripts(Session& session, Game& game, Coop& coop, std::function<std::uint64_t()> clock = {});

    // A CAMSCRIPT or CAMSTATE from `sender`.
    void handle(std::int32_t sender, const Bytes& bytes);

    // The game's word of a script of its own starting or ending (the SDK's on_camera_script).
    void script(int id, bool started);

    // The first pad's report had something pressed, at `now`.
    void input(std::uint64_t now);

    // Once a game tick.
    void tick(std::uint64_t now);

    // The party or its game is gone: nothing is kept, and the camera is the hero's.
    void reset();

    // What it does, for the tests and the bot.
    bool showing() const { return showing_; }

    // Returns how many scripts this game has shown.
    std::uint64_t shown() const { return shown_; }

    // Returns how many scripts this game, as host, has told the party of.
    std::uint64_t sent() const { return sent_; }

    // Sets where this object's log lines go.
    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

    // Sets the trace this writes to; nullptr for none.
    void trace(Trace* trace) { trace_ = trace; }

private:
    // One view of a script: the eye and rotation at the server's clock `stamp`.
    struct View {
        std::int64_t stamp = 0;         // The server clock when it was taken, in ms.
        float eye[3] = {0, 0, 0};       // The eye position.
        float rotation[3] = {0, 0, 0};  // The rotation.
    };

    // Sends the camera's view as a CAMSTATE, at most every kCamStateMs, while the host tells a
    // script.
    void send_view(std::uint64_t now);
    // Takes the start of a script, the host's own or a member's, and shows it when may_start
    // allows.
    void heard_start(const CamScript& script, std::uint64_t now);
    // Keeps `view` for playback; one not newer than the last kept is dropped.
    void heard_view(const View& view, std::uint64_t now);
    // Takes the end of the script heard and gives the camera back if it was shown.
    void heard_end(std::uint64_t now);
    // Returns whether the player may be shown `script` now: in control, in its level and near
    // enough, and quiet.
    bool may_start(const CamScript& script, std::uint64_t now);
    // Plays the kept view, or gives the camera back when the player is elsewhere, moved or was
    // hurt, or nothing came.
    void show(std::uint64_t now);
    // Gives the camera back to the hero. `cut` is true when the script is broken off, false when it
    // ended; `why` goes to the log.
    void take_back(bool cut, const std::string& why);
    // The view at `when`, from what is kept.
    View view_at(std::int64_t when) const;

    // Writes `line` to the log callback, if one is set.
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
    // True while the script told is a member's stand-in's, which this game's player may see.
    bool telling_own_ = false;
    // The class of the script told.
    int telling_id_ = -1;
    // The number of the last script told; 0 is never used.
    std::uint16_t sequence_ = 0;
    // When the last view was sent.
    std::uint64_t view_ms_ = 0;

    // The script heard of and shown here.
    // True from a script's start until its end or a silence.
    bool heard_ = false;
    // The number of the script heard, which its views must carry.
    std::uint16_t heard_sequence_ = 0;
    // The co-op game number the script was heard in.
    std::int32_t heard_play_ = 0;
    // The level the script runs in.
    std::int32_t heard_level_ = -1;
    // The class of the script heard.
    int heard_id_ = -1;
    // When anything of the script was last heard.
    std::uint64_t heard_ms_ = 0;
    // True while this game's camera plays the script.
    bool showing_ = false;
    // When the script began to show.
    std::uint64_t show_ms_ = 0;
    // The latest views of the script, oldest first.
    std::deque<View> views_;

    // The player.
    // When a pad report last had something pressed.
    std::uint64_t input_ms_ = 0;
    // When the hero's health last fell.
    std::uint64_t hurt_ms_ = 0;
    // The hero's health at the last tick; -1 for none yet.
    int health_ = -1;

    // How many scripts were shown.
    std::uint64_t shown_ = 0;
    // How many scripts were told.
    std::uint64_t sent_ = 0;
};

}  // namespace mp

#endif
