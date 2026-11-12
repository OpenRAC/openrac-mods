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

namespace dm {

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

// The events' clock as at a start.
void reset_clock(Session& s) {
    s.clock = EventClock{};
    s.clock.seed = mix(s.seed, kEventClockSalt);
    s.clock.knobs = s.knobs;
    s.clock.every = kSecondsPerMinute * static_cast<double>(s.mod->setting<int>("chaos_minutes"));

    // Less than a minute (or not a number): the default gap.
    if (!(s.clock.every >= kSecondsPerMinute)) {
        s.clock.every = kDefaultEventGap;
    }
}

// What this game plays by, for a party it hosts.
void publish_rules(Session& s) {
    PartyRules r;
    r.chaos = s.chaos;
    r.events = s.events_on;
    r.minutes = static_cast<int>(s.clock.every / kSecondsPerMinute);
    r.sliders = s.sliders;
    r.seed = s.seed;
    r.event = s.clock.current;
    s.mod->party_rules_publish(encode_party_rules(r), party_rules_text(r));
}

// The host's rules reached this game, or (none) the party went and its own count again from the
// next level loaded. An event starting or ending in the host's game comes this way too.
void on_party_rules(void*, const void* data, std::uint32_t size) {
    Session& s = session();

    if (data == nullptr) {
        // Only a member that took rules has any to leave.
        if (s.follow) {
            s.follow = false;
            reset_clock(s);
            set_active();
            s.mod->info("out of the party: this game's own settings count again");
        }

        return;
    }

    PartyRules rules;

    if (!decode_party_rules(data, size, rules)) {
        s.mod->warn("the host's rules cannot be read here: its Difficulty is another version");
        return;
    }

    const bool first = !s.follow;
    const int event = s.clock.current;

    // A new event, or the first rules: its time starts now.
    if (first || rules.event != event) {
        s.event_held = 0.0;
    }

    s.follow = true;
    s.rules = rules;
    s.clock.current = rules.event;
    set_active();

    if (first) {
        s.mod->info(
            "in a party: playing by the host's rules, seed " + seed_code(rules.seed) + ": "
            + describe(rules.sliders) + (rules.chaos ? ", chaos" : "")
        );
    }

    // The host's event, as this game's own would say it.
    if (rules.event != event) {
        s.mod->info(
            rules.event >= 0 ? std::string("chaos: ") + k_events[rules.event].name
                             : std::string("chaos: back to normal")
        );
    }
}

}  // namespace

Session& session() {
    static Session s;
    return s;
}

std::string fmt(double value) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", value);
    return b;
}

std::string describe(const Values& values) {
    const unsigned knobs = session().knobs;
    std::string out;

    // Each slider the game honours, in the manifest's order.
    for (int k = 0; k < KNOB_COUNT; ++k) {
        // One this game has.
        if ((knobs & 1u << k) != 0) {
            out += std::string(out.empty() ? "" : ", ") + k_knobs[k].name + " " + fmt(values[k]);
        }
    }

    return out;
}

void set_active() {
    Session& s = session();
    s.active = with_event(s.level, s.clock.current);
}

void start(openrac::Mod& mod, unsigned knobs) {
    Session& s = session();
    s.mod = &mod;
    s.knobs = knobs;
    s.chaos = mod.setting<bool>("chaos");

    // Each slider's setting.
    for (int k = 0; k < KNOB_COUNT; ++k) {
        s.sliders[k] = clamp_knob(k, mod.setting<double>(k_knobs[k].key));
    }

    const std::string seed_text = mod.setting<std::string>("seed");

    // Blank: a new seed from the machine and the time, shown in the log to play it again.
    if (seed_text.empty()) {
        std::random_device rd;
        s.seed = (static_cast<std::uint64_t>(rd()) ^ static_cast<std::uint64_t>(std::time(nullptr)))
                 & kSeedMask;
    } else {
        s.seed = seed_from_text(seed_text);
    }

    s.trace = mod.setting<bool>("trace");
    s.events_on = mod.setting<bool>("chaos_events");
    reset_clock(s);
    s.level = effective(s.chaos, s.sliders, s.seed, -1);
    set_active();
    s.follow = false;
    publish_rules(s);
    mod.on_party_rules(on_party_rules);
}

std::uint64_t level_values(int level) {
    Session& s = session();
    // A party's member plays by the host's: the same rolls from the same seed.
    const bool chaos = s.follow ? s.rules.chaos : s.chaos;
    const Values& sliders = s.follow ? s.rules.sliders : s.sliders;
    const std::uint64_t seed = s.follow ? s.rules.seed : s.seed;
    s.level = effective(chaos, sliders, seed, level);
    set_active();

    // Chaos rolls new values each level, and the trace asks for them: say what plays.
    if (chaos || s.trace) {
        s.mod->info("level " + std::to_string(level) + ": " + describe(s.level));
    }

    return seed;
}

void advance_events(double dt) {
    Session& s = session();

    // A party's member: the host decides the events. One on far longer than an event lasts is
    // ended here (a host that took over passes on the rules as they were, the event never ending).
    if (s.follow) {
        // No event on, or no time passed.
        if (s.clock.current < 0 || !(dt > 0.0)) {
            return;
        }

        s.event_held += dt;

        // On too long: back to normal here.
        if (s.event_held >= s.clock.lasts * kEventHeldLimit) {
            s.clock.current = -1;
            set_active();
            s.mod->info("chaos: back to normal (the host's event never ended)");
        }

        return;
    }

    // The events are off.
    if (!s.events_on) {
        return;
    }

    const int change = s.clock.advance(dt);

    // An event started or ended: a party's members take it from these rules.
    if (change != 0) {
        set_active();
        publish_rules(s);
        s.mod->info(
            change > 0 ? std::string("chaos: ") + k_events[s.clock.current].name
                       : std::string("chaos: back to normal")
        );
    }
}

void log_start(const std::string& detail) {
    Session& s = session();
    std::string mode = s.chaos ? "chaos" : sliders_moved(s.sliders) ? "sliders" : "retail";

    // Chaos with sliders held off 1.0: say that they hold.
    if (s.chaos && sliders_moved(s.sliders)) {
        mode += ", with sliders moved off 1.0 holding";
    }

    s.mod->info(
        mode + " on the " + std::string(s.mod->version()) + " disc; seed " + seed_code(s.seed)
        + " (put it in Seed to play the same rolls again)" + (detail.empty() ? "" : "; " + detail)
        + (s.events_on ? "; chaos events every " + fmt(s.clock.every / 60.0) + " min" : "")
    );

    // Without Chaos the values are the sliders': one line says them.
    if (!s.chaos) {
        s.mod->info(describe(s.level));
    }
}

}  // namespace dm
