// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The verbose log (trace.cpp; the `verbose_log` setting, or OPENRAC_MP_TRACE=1): for reports of a
// party's world going wrong (mobys lost when a player pauses). Off, nothing is written. On, each
// event is one line, `[mp-trace] f=<frame> t=<ms> <side> <what> ...`, into mp-trace.log beside the
// game's run.log (multiplayer.cpp), where:
//
//   pause     the game's phase changing, a menu opened or closed (phase, level, level mode, scene),
//             and each menu tick: the camera mods read (camera_position), the camera the game's run
//             list measured from, and what the menu update did with the world (moby_run's menu);
//   run       a moby the run list left out by distance (and so not updated) while a menu is open,
//             or one going in or out of range, with its distance and its update and draw distances;
//   vis       a moby hidden or shown, drawn or not (while a menu is open), dead;
//   despawn   a moby gone (or hidden) with who removed it and why (despawn.hpp);
//   eyes      the points the host's game runs the world around (play_eyes);
//   snap      a WORLD snapshot sent or received: its count, the ids new and the ids missing since
//             the last one; received, its stamp (the server's clock), the stamp in this game's
//             clock, when it came and its time in transit;
//   pose      a player's pose received (its sequence, stamp, the stamp in this clock, when it
//             came, its transit, its position, velocity and state); twice a second this game's
//             hero: his z's range over the ticks and how many ticks it changed, where he is and his
//             velocity; twice a second for each figure shown, what was shown (between
//             two poses, ahead of the newest, held, a jump, before the first), where, the playback
//             cursor, the buffer (play.hpp's describe: delay, transit, jitter, interval, loss, how
//             far ahead the newest pose is, how many are kept), how many frames showed each, and
//             the clock's round trip and offset; once a second the host's mobys driven here: the
//             frames each way, how many something else moved after they were put there, and three
//             of their buffers;
//   drive     a moby driven from the host's or let go;
//   msg       KILLED, DEAD, DESPAWN, BROKE and SPAWN sent or received;
//   census    every second: the live mobys by class, and the ones that came and went.
//
// At most kTraceLinesPerSecond lines a second; the rest are counted and said in one line when the
// second is over.

#ifndef MULTIPLAYER_TRACE_H
#define MULTIPLAYER_TRACE_H

#include <cstdint>
#include <functional>
#include <string>

namespace mp {

// The most lines the log writes in a second; the rest are counted and said when the second ends.
constexpr int kTraceLinesPerSecond = 2000;

// The verbose log's writer: it counts the game's ticks, caps the lines of each second and hands
// each line to a sink.
//
// The other parts hold a pointer to it and use it on the game's thread.
class Trace {
public:
    // Receives each finished line.
    using Sink = std::function<void(const std::string&)>;

    // On, its lines to `sink`; an empty one turns it off.
    void open(Sink sink) { sink_ = std::move(sink); }

    // Returns whether a sink is set, so the log is on.
    bool on() const { return static_cast<bool>(sink_); }

    // A new game tick at `now_ms`, this game's side of the party (host, member, solo).
    void tick(std::uint64_t now_ms, const std::string& side);

    // Returns how many ticks the log has seen.
    std::uint64_t frame() const { return frame_; }

    // One event: `what` (pause, run, ...) and the rest of its line.
    void line(const std::string& what, const std::string& text);

    // Returns how many lines went to the sink in all.
    int written() const { return written_; }

    // Returns how many lines the cap dropped in all.
    int dropped() const { return dropped_total_; }

private:
    // Says how many lines the last second dropped, once a new second has begun.
    void flush();

    // Where the lines go; empty while the log is off.
    Sink sink_;
    std::string side_ = "solo";    // This game's side of the party, as the last tick named it.
    std::uint64_t frame_ = 0;      // Ticks seen so far.
    std::uint64_t now_ = 0;        // The last tick's time in milliseconds.
    std::uint64_t second_ms_ = 0;  // When the current second of the cap began.
    int lines_ = 0;                // Lines written in the current second.
    int dropped_ = 0;              // Lines dropped in the current second.
    int written_ = 0;              // Lines written in all.
    int dropped_total_ = 0;        // Lines dropped in all.
};

// "x y z" to a tenth.
std::string xyz(const float* v);

}  // namespace mp

#endif  // MULTIPLAYER_TRACE_H
