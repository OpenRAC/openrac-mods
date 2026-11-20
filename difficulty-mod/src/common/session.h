// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's state that every game shares (README.md): what the player chose (the
// sliders, Chaos and its seed, the trace, the events), what plays on the level loaded and with the
// event on, the events' clock, and a party's host's rules (on_party_rules). Each game's code
// (src/rac1, src/rac2) owns one Session in its state and passes it to the functions below; it
// changes the game where it takes each value. Nothing here knows an address. All of it is the
// game's thread's.

#pragma once

#include <cstdint>
#include <string>

#include "openrac/mod.hpp"
#include "rules.h"

namespace difficulty {

// What the player chose and what plays now. One per game's state; the party's callback keeps a
// pointer to it, so it must not move once start has run.
struct Session {
    // The port's mod object; not owned, valid for the life of the program.
    openrac::Mod* mod = nullptr;

    // What the player chose (read at load).
    bool chaos = false;          // The Chaos switch.
    Values sliders = retail();   // Each slider, clamped to its range.
    std::uint64_t seed = 0;      // The seed Chaos rolls from.
    std::uint64_t own_seed = 0;  // This game's own, kept while it plays by a party's.
    bool trace = false;          // More lines in the log.
    bool events_on = false;      // The timed events' switch.

    // What plays on the level loaded, and with the event on.
    Values level = retail();   // The level's values, without the event.
    Values active = retail();  // The level's values with the event on.
    EventClock clock;          // The events' clock.

    // The host's rules, while a party's host has given them.
    bool follow = false;  // True while a host's rules count.
    PartyRules rules;     // The host's rules, valid while `follow`.
    // The seconds of play the host's event has been on here: one far past an event's length is
    // ended here (a host that took over passes on the rules as they were, the event never ending).
    double event_held = 0.0;
    // It took over as the party's host (the old host left), playing on by the party's rules.
    bool took_over = false;

    // The knobs this game honours (rules.h: a bit each): the log names only these, and the events
    // of others are never picked.
    unsigned knobs = kAllKnobs;
};

// Starts the session for `mod`, honouring `knobs`: reads the settings (the sliders clamped, the
// seed, a new one when blank, the trace, the events and their clock), puts what plays back to the
// sliders as at a start, publishes this game's rules for a party it hosts and listens for a host's.
void start(Session& session, openrac::Mod& mod, unsigned knobs);

// What plays now: the level's values with the event on, if any.
void set_active(Session& session);

// A level's values: rolled from the seed with Chaos, else the sliders, both the host's in a party.
// Logs them when Chaos rolls or the trace asks. Returns the seed they come from, for the level's
// own random stream.
std::uint64_t level_values(Session& session, int level);

// Advances the events' clock by `dt` seconds of play: an event starting or ending changes what
// plays, is published to a party's members, and logged. Does nothing with the events off. In a
// party's member the host decides the events: only one on far longer than an event lasts is ended.
void advance_events(Session& session, double dt);

// The values as one log line: each honoured slider's name and value, two decimals each.
std::string describe(const Session& session, const Values& values);

// A value as the log shows it: two decimals.
std::string fmt(double value);

// The load's lines in the log: how it plays (retail, sliders or chaos), the disc, the seed, what
// the game's code adds (`detail`, after a "; "), the events, and the values without Chaos.
void log_start(const Session& session, const std::string& detail);

}  // namespace difficulty
