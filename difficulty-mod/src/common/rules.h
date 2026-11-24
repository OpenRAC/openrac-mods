// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's rules, apart from the game: the sliders and their ranges, what plays with
// them and Chaos, Chaos's roll for each level and its timed events, and the arithmetic that turns
// a multiplier into whole hits, bolts, prices and cells. No game memory here: tests/rules_test.cpp
// checks it all.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace difficulty {

// The sliders, in the manifest's order (openrac-mod.json "settings").
enum Knob {
    kEnemyCount,
    kDamageTaken,
    kDamageDealt,
    kEnemySpeed,
    kPlayerSpeed,
    kWeaponCost,
    kAmmoCost,
    kAmmoUse,
    kMaxAmmo,
    kBolts,
    kMaxHealth,
    kHealthPickups,
    kKnobCount
};

// One slider: its setting, its name in the log and its ranges.
struct KnobInfo {
    const char* key;   // The setting's key.
    const char* name;  // What the log calls it.
    double min;        // The slider's lowest value (1.0 is retail, inside the range).
    double max;        // The slider's highest value.
    double chaos_min;  // The lowest value Chaos rolls for a level.
    double chaos_max;  // The highest value Chaos rolls for a level.
};

// Every slider's key, name and ranges, indexed by Knob.
extern const KnobInfo kKnobs[kKnobCount];

// One value for each slider, indexed by Knob.
struct Values {
    double v[kKnobCount];

    // Returns the value of slider `k`.
    double& operator[](int k) { return v[k]; }

    double operator[](int k) const { return v[k]; }
};

// Every knob at 1.0. The presets (Easy, Retail, Hard, Brutal, Chaos) are the manifest's: the
// launcher sets the sliders and the "chaos" switch from them.
Values retail();

// A slider's value in its range: NaN and infinities are 1.0 (retail).
double clamp_knob(int knob, double value);

// What plays: the sliders, clamped. With Chaos, each slider left at 1.0 takes its roll for
// `level` instead, and one moved off 1.0 holds (README.md).
Values effective(bool chaos, const Values& sliders, std::uint64_t seed, int level);
// Whether any slider is off 1.0.
bool sliders_moved(const Values& sliders);

// Chaos's values for one level: each knob in its chaos range, log-uniform around 1.0,
// from the seed and the level only, so the same seed gives the same game.
Values chaos_roll(std::uint64_t seed, int level);

// A seed's code: 8 hex digits; a code given back gives the same seed, any other text is hashed.
std::uint64_t seed_from_text(const std::string& text);

// Returns the seed's code: 8 hex digits, upper case.
std::string seed_code(std::uint64_t seed);

// splitmix64: the mod's only randomness, always from a seed.
struct Rng {
    // The stream's position; it moves on with each value.
    std::uint64_t state;

    explicit Rng(std::uint64_t seed) : state(seed) {}

    // Returns the next 64 random bits.
    std::uint64_t next();

    // Returns the next value in [0, 1).
    double unit();
};

// Mixes `salt` into `seed`, giving the seed of a stream of its own: the same two always give the
// same result.
std::uint64_t mix(std::uint64_t seed, std::uint64_t salt);

// Whole units from a fractional rate: each call adds rate * units and gives the whole part, the
// rest kept for the next (so 0.5 takes a hit every other hit, 1.5 one then two, ...).
struct Accumulator {
    // The fraction kept for the next call.
    double carry = 0.0;

    // Adds `rate * units` to the carry and returns the whole part, which it takes out.
    int take(double rate, int units = 1);
};

// n scaled by m, rounded at random to a neighbour so that on average it is n * m.
int scale_count(int n, double m, Rng& rng);
// A price or an ammo field scaled: 0 stays 0 (the game's "none"), anything else at least 1, at
// most limit.
int scale_amount(int value, double m, int limit);
// The cells of health for a retail count (4, 5 or 8) and a multiplier: the HUD has places for
// 1 to 5 and 8 cells only, so the nearest of those at or below, at least 1.
int max_health(int retail_cells, double m);

// Chaos's timed events (the "chaos_events" switch): every few minutes one of these for a while,
// on top of what plays. Only knobs read as the game goes (not prices, counts or cells).
struct Event {
    const char* name;  // What the log and the party's text call it.
    Knob knob[2];      // The two sliders it changes (the same one twice for a single change).
    double factor[2];  // What each is multiplied by while it lasts.
};

// The events there are, and how many.
extern const Event kEvents[];
extern const int kEventCount;

// A set of knobs, a bit each (1u << knob): the ones a game honours. Every knob is rac1's.
constexpr unsigned kAllKnobs = (1u << kKnobCount) - 1u;

// Whether every knob an event changes is in `knobs`: an event a game cannot play is never picked
// there (rac2 has no enemy clock, so no Slow motion, Frenzy or Double time).
bool event_allowed(int event, unsigned knobs);

// The events' timetable: when the next one starts and which it is, from a seed.
struct EventClock {
    std::uint64_t seed = 0;      // The stream the events are picked from.
    double every = 180.0;        // Seconds between events (the default gap, 3 minutes).
    double lasts = 45.0;         // Seconds an event lasts.
    double clock = 0.0;          // Seconds of play.
    int count = 0;               // Events started.
    int current = -1;            // The event on, -1 for none.
    double ends = 0.0;           // When it ends, in seconds of play.
    unsigned knobs = kAllKnobs;  // The knobs the game honours: events of others are not picked.

    // Advances by `dt` seconds of play; returns 1 when an event starts, -1 when one ends, else 0.
    int advance(double dt);
};

// What plays with the event on, each knob still in a safe range.
Values with_event(const Values& values, int event);

// A party's rules: all a member needs to play by the host's (the sliders, Chaos, the resolved seed,
// so each level rolls the same, and the event on). The host's game publishes them, a member's takes
// them (openrac_api.party_rules_publish, on_party_rules).
struct PartyRules {
    bool chaos = false;      // The Chaos switch.
    bool events = false;     // The timed events' switch.
    int minutes = 0;         // The minutes between events.
    Values sliders{};        // Each slider, clamped.
    std::uint64_t seed = 0;  // The resolved seed, so each level rolls the same.
    int event = -1;          // The event on, -1 for none.
};

// Returns the rules' bytes, as a member's mod of the same version reads them.
std::vector<std::uint8_t> encode_party_rules(const PartyRules& rules);

// Reads `size` bytes at `data` into `out`; false, and `out` unchanged, for bytes it cannot read.
bool decode_party_rules(const void* data, std::uint32_t size, PartyRules& out);

// Returns their words for a player, a rule a line: "Bolts x2\nEnemy count x1.5\nChaos on".
std::string party_rules_text(const PartyRules& rules);

// The moby pool's guard (README.md, "How many enemies"): the spawned mobys' pool keeps this many
// free slots for what the game spawns (effects, shots, bolts), whatever the enemy count asks for.
constexpr int kPoolReserve = 160;
// On Gaspar (level 9) the lava sparks alone nearly fill a console's pool of 255: there the copies
// leave that many free for them too, so they get none in a pool of a console's size and the rest of
// a bigger one (OpenRAC's port gives most levels 767).
constexpr int kGasparSparks = 255;
// Blarg Station (level 6) spawns this many mobys of its own after the level load, before its
// landing (measured on PAL: the first free slot goes from 2 to 195 of 255). The copies leave them
// room too, or the landing has no slot for the ship and Clank.
constexpr int kBlargSpawns = 193;
// Slots the copies may take on a level with free_slots free.
int clone_budget(int free_slots, int level);
// Extra slots a copy whose pvars are size bytes needs for them (a spawned moby has 0x80).
int pvar_slots(int size);

}  // namespace difficulty
