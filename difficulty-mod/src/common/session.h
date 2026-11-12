// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's state that every game shares (README.md): what the player chose (the
// sliders, Chaos and its seed, the trace, the events), what plays on the level loaded and with the
// event on, the events' clock, and a party's host's rules (on_party_rules). Each game's code
// (src/rac1, src/rac2) reads it through session() and changes the game where it takes each value;
// nothing here knows an address. All of it is the game's thread's.
#ifndef DIFFICULTY_SESSION_H
#define DIFFICULTY_SESSION_H

#include <cstdint>
#include <string>

#include "openrac/mod.hpp"
#include "rules.h"

namespace dm {

struct Session {
    openrac::Mod* mod = nullptr;

    // What the player chose (read at load).
    bool chaos = false;
    Values sliders = retail();
    std::uint64_t seed = 0;
    bool trace = false;
    bool events_on = false;

    // What plays on the level loaded, and with the event on.
    Values level = retail();
    Values active = retail();
    EventClock clock;

    // The host's rules, while a party's host has given them.
    bool follow = false;
    PartyRules rules;
    // The seconds of play the host's event has been on here: one far past an event's length is
    // ended here (a host that took over passes on the rules as they were, the event never ending).
    double event_held = 0.0;

    // The knobs this game honours (rules.h: a bit each): the log names only these, and the events
    // of others are never picked.
    unsigned knobs = k_all_knobs;
};

// The one session of the library.
Session& session();

// Starts the session for `mod`, honouring `knobs`: reads the settings (the sliders clamped, the
// seed, a new one when blank, the trace, the events and their clock), puts what plays back to the
// sliders as at a start, publishes this game's rules for a party it hosts and listens for a host's.
void start(openrac::Mod& mod, unsigned knobs);

// What plays now: the level's values with the event on, if any.
void set_active();

// A level's values: rolled from the seed with Chaos, else the sliders, both the host's in a party.
// Logs them when Chaos rolls or the trace asks. Returns the seed they come from, for the level's
// own random stream.
std::uint64_t level_values(int level);

// Advances the events' clock by `dt` seconds of play: an event starting or ending changes what
// plays, is published to a party's members, and logged. Does nothing with the events off. In a
// party's member the host decides the events: only one on far longer than an event lasts is ended.
void advance_events(double dt);

// The values as one log line: each honoured slider's name and value, two decimals each.
std::string describe(const Values& values);

// A value as the log shows it: two decimals.
std::string fmt(double value);

// The load's lines in the log: how it plays (retail, sliders or chaos), the disc, the seed, what
// the game's code adds (`detail`, after a "; "), the events, and the values without Chaos.
void log_start(const std::string& detail);

}  // namespace dm

#endif  // DIFFICULTY_SESSION_H
