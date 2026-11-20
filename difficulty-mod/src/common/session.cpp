// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's shared state (session.h): the settings, the level's values, Chaos's events
// and a party's rules, the same in every game.

#include "session.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <random>
#include <string>

namespace difficulty {
namespace {

// The salt that keeps the events' clock on a stream of its own.
constexpr std::uint64_t kEventClockSalt = 0x4000u;

// A seed is 32 bits (the code is 8 hex digits).
constexpr std::uint64_t kSeedMask = 0xFFFFFFFFu;

// The seconds in a minute, and the gap between events when the setting is below one.
constexpr double kSecondsPerMinute = 60.0;
constexpr double kDefaultEventGap = 180.0;
// A host's event on for this many times an event's length (in this game's play) is taken as over.
constexpr double kEventHeldLimit = 2.0;

// The room a value formatted with two decimals needs.
constexpr std::size_t kTextBytes = 32;

// This game's own settings: Chaos, the sliders, the seed (as resolved at the start) and the
// events.
void read_own(Session& session) {
    session.chaos = session.mod->setting<bool>("chaos");

    // Each slider's setting.
    for (int knob = 0; knob < kKnobCount; ++knob) {
        session.sliders[knob] = clamp_knob(knob, session.mod->setting<double>(kKnobs[knob].key));
    }

    session.seed = session.own_seed;
    session.events_on = session.mod->setting<bool>("chaos_events");
}

// The events' clock as at a start.
void reset_clock(Session& session) {
    session.clock = EventClock{};
    session.clock.seed = mix(session.seed, kEventClockSalt);
    session.clock.knobs = session.knobs;
    session.clock.every =
        kSecondsPerMinute * static_cast<double>(session.mod->setting<int>("chaos_minutes"));

    // Less than a minute (or not a number): the default gap.
    if (!(session.clock.every >= kSecondsPerMinute)) {
        session.clock.every = kDefaultEventGap;
    }
}

// What this game plays by, for a party it hosts.
void publish_rules(Session& session) {
    PartyRules rules;
    rules.chaos = session.chaos;
    rules.events = session.events_on;
    rules.minutes = static_cast<int>(session.clock.every / kSecondsPerMinute);
    rules.sliders = session.sliders;
    rules.seed = session.seed;
    rules.event = session.clock.current;
    session.mod->party_rules_publish(encode_party_rules(rules), party_rules_text(rules));
}

// This game hosts the party now (the old host left): the party's rules become its own, the event
// on goes on for a whole event's length from here, and the next starts a gap after it.
void take_over(Session& session) {
    session.follow = false;
    session.took_over = true;
    session.chaos = session.rules.chaos;
    session.sliders = session.rules.sliders;
    session.seed = session.rules.seed;
    session.events_on = session.rules.events;
    const int event = session.clock.current;
    session.clock = EventClock{};
    session.clock.seed = mix(session.seed, kEventClockSalt);
    session.clock.knobs = session.knobs;
    session.clock.every = kSecondsPerMinute * static_cast<double>(session.rules.minutes);

    // Not a gap the settings allow: the default.
    if (!(session.clock.every >= kSecondsPerMinute)) {
        session.clock.every = kDefaultEventGap;
    }

    // An event on: it ends an event's length from now.
    if (event >= 0 && session.events_on) {
        session.clock.current = event;
        session.clock.ends = session.clock.lasts;
    }

    set_active(session);
    publish_rules(session);
    session.mod->info(
        "this game hosts the party now: it plays on by the party's rules, seed "
        + seed_code(session.seed)
    );
}

// The log's line for an event on or off: its name, or "back to normal" for none.
std::string event_line(int event) {
    // No event is on: the values are the level's again.
    if (event < 0) {
        return "chaos: back to normal";
    }

    return std::string("chaos: ") + kEvents[event].name;
}

// How the mod plays, for the load's log line: chaos, moved sliders or retail.
std::string play_mode(const Session& session) {
    // Chaos rolls whatever the sliders leave at 1.0.
    if (session.chaos) {
        return "chaos";
    }

    // No Chaos: the sliders are the values, if any is moved.
    if (sliders_moved(session.sliders)) {
        return "sliders";
    }

    return "retail";
}

// The host's rules reached this game, or (none) the party went and its own count again from the
// next level loaded. An event starting or ending in the host's game comes this way too, and (0
// bytes) word that this game took over as host. `user` is the game's Session.
void on_party_rules(void* user, const void* data, std::uint32_t size) {
    Session& session = *static_cast<Session*>(user);

    // The party went.
    if (data == nullptr) {
        // It had taken over as host: its own settings again, from the next level loaded.
        if (session.took_over) {
            session.took_over = false;
            read_own(session);
            reset_clock(session);
            set_active(session);
            publish_rules(session);
            session.mod->info("out of the party: this game's own settings count again");
        }

        // Only a member that took rules has any to leave.
        if (session.follow) {
            session.follow = false;
            reset_clock(session);
            set_active(session);
            session.mod->info("out of the party: this game's own settings count again");
        }

        return;
    }

    // 0 bytes, not null: this game took over as the party's host. It keeps the party's rules as
    // its own, runs the events (and, in a level loaded from now on, the copies) a host runs, and
    // publishes them for the members.
    if (size == 0) {
        if (session.follow) {
            take_over(session);
        }

        return;
    }

    PartyRules rules;

    // The host runs another version of the mod, whose rules are laid out differently.
    if (!decode_party_rules(data, size, rules)) {
        session.mod->warn(
            "the host's rules cannot be read here: its Difficulty is another version"
        );
        return;
    }

    const bool first = !session.follow;
    const int event = session.clock.current;

    // A new event, or the first rules: its time starts now.
    if (first || rules.event != event) {
        session.event_held = 0.0;
    }

    session.follow = true;
    session.rules = rules;
    session.clock.current = rules.event;
    set_active(session);

    // The first rules of a party: say what the host plays by.
    if (first) {
        session.mod->info(
            "in a party: playing by the host's rules, seed " + seed_code(rules.seed) + ": "
            + describe(session, rules.sliders) + (rules.chaos ? ", chaos" : "")
        );
    }

    // The host's event, as this game's own would say it.
    if (rules.event != event) {
        session.mod->info(event_line(rules.event));
    }
}

}  // namespace

std::string fmt(double value) {
    char text[kTextBytes];
    std::snprintf(text, sizeof text, "%.2f", value);
    return text;
}

std::string describe(const Session& session, const Values& values) {
    std::string out;

    // Each slider the game honours, in the manifest's order.
    for (int knob = 0; knob < kKnobCount; ++knob) {
        // Only the sliders this game has.
        if ((session.knobs & 1u << knob) != 0) {
            out +=
                std::string(out.empty() ? "" : ", ") + kKnobs[knob].name + " " + fmt(values[knob]);
        }
    }

    return out;
}

void set_active(Session& session) {
    session.active = with_event(session.level, session.clock.current);
}

void start(Session& session, openrac::Mod& mod, unsigned knobs) {
    session.mod = &mod;
    session.knobs = knobs;
    const std::string seed_text = mod.setting<std::string>("seed");

    // Blank: a new seed from the machine and the time, shown in the log to play it again.
    if (seed_text.empty()) {
        std::random_device device;
        session.seed =
            (static_cast<std::uint64_t>(device()) ^ static_cast<std::uint64_t>(std::time(nullptr)))
            & kSeedMask;
    } else {
        session.seed = seed_from_text(seed_text);
    }

    session.own_seed = session.seed;
    read_own(session);
    session.trace = mod.setting<bool>("trace");
    reset_clock(session);
    session.level = effective(session.chaos, session.sliders, session.seed, -1);
    set_active(session);
    session.follow = false;
    publish_rules(session);
    mod.on_party_rules(on_party_rules, &session);
}

std::uint64_t level_values(Session& session, int level) {
    // A party's member plays by the host's: the same rolls from the same seed.
    const bool chaos = session.follow ? session.rules.chaos : session.chaos;
    const Values& sliders = session.follow ? session.rules.sliders : session.sliders;
    const std::uint64_t seed = session.follow ? session.rules.seed : session.seed;
    session.level = effective(chaos, sliders, seed, level);
    set_active(session);

    // Chaos rolls new values each level, and the trace asks for them: say what plays.
    if (chaos || session.trace) {
        session.mod->info(
            "level " + std::to_string(level) + ": " + describe(session, session.level)
        );
    }

    return seed;
}

void advance_events(Session& session, double dt) {
    // A party's member: the host decides the events. One on far longer than an event lasts is
    // ended here (a host that took over passes on the rules as they were, the event never ending).
    if (session.follow) {
        // No event on, or no time passed.
        if (session.clock.current < 0 || !(dt > 0.0)) {
            return;
        }

        session.event_held += dt;

        // On too long: back to normal here.
        if (session.event_held >= session.clock.lasts * kEventHeldLimit) {
            session.clock.current = -1;
            set_active(session);
            session.mod->info("chaos: back to normal (the host's event never ended)");
        }

        return;
    }

    // The events are off.
    if (!session.events_on) {
        return;
    }

    const int change = session.clock.advance(dt);

    // An event started or ended: a party's members take it from these rules.
    if (change != 0) {
        set_active(session);
        publish_rules(session);
        session.mod->info(event_line(change > 0 ? session.clock.current : -1));
    }
}

void log_start(const Session& session, const std::string& detail) {
    std::string mode = play_mode(session);

    // Chaos with sliders held off 1.0: say that they hold.
    if (session.chaos && sliders_moved(session.sliders)) {
        mode += ", with sliders moved off 1.0 holding";
    }

    session.mod->info(
        mode + " on the " + std::string(session.mod->version()) + " disc; seed "
        + seed_code(session.seed) + " (put it in Seed to play the same rolls again)"
        + (detail.empty() ? "" : "; " + detail)
        + (session.events_on
               ? "; chaos events every " + fmt(session.clock.every / kSecondsPerMinute) + " min"
               : "")
    );

    // Without Chaos the values are the sliders': one line says them.
    if (!session.chaos) {
        session.mod->info(describe(session, session.level));
    }
}

}  // namespace difficulty
