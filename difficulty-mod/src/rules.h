// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's rules, apart from the game: the sliders and their ranges, what plays with
// them and Chaos, Chaos's roll for each level and its timed events, and the arithmetic that turns a multiplier into whole hits, bolts, prices and cells. No
// game memory here: tests/rules_test.cpp checks it all.
#ifndef DIFFICULTY_RULES_H
#define DIFFICULTY_RULES_H

#include <cstdint>
#include <string>
#include <vector>

namespace dm {

// The sliders, in the manifest's order (openrac-mod.json "settings").
enum Knob {
    ENEMY_COUNT,
    DAMAGE_TAKEN,
    DAMAGE_DEALT,
    ENEMY_SPEED,
    PLAYER_SPEED,
    WEAPON_COST,
    AMMO_COST,
    AMMO_USE,
    MAX_AMMO,
    BOLTS,
    MAX_HEALTH,
    HEALTH_PICKUPS,
    KNOB_COUNT
};

struct KnobInfo {
    const char* key;              // the setting's key
    const char* name;             // what the log calls it
    double min, max;              // the slider's range (1.0 = retail inside it)
    double chaos_min, chaos_max;  // Chaos's range for each level
};

extern const KnobInfo k_knobs[KNOB_COUNT];

struct Values {
    double v[KNOB_COUNT];

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
std::string seed_code(std::uint64_t seed);

// splitmix64: the mod's only randomness, always from a seed.
struct Rng {
    std::uint64_t state;

    explicit Rng(std::uint64_t seed) : state(seed) {}

    std::uint64_t next();
    double unit();  // in [0, 1)
};

std::uint64_t mix(std::uint64_t a, std::uint64_t b);

// Whole units from a fractional rate: each call adds rate * units and gives the whole part, the
// rest kept for the next (so 0.5 takes a hit every other hit, 1.5 one then two, ...).
struct Accumulator {
    double carry = 0.0;
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
    const char* name;
    Knob knob[2];
    double factor[2];
};

extern const Event k_events[];
extern const int k_event_count;

struct EventClock {
    std::uint64_t seed = 0;
    double every = 180.0;  // seconds between events
    double lasts = 45.0;   // seconds an event lasts
    double clock = 0.0;    // seconds of play
    int count = 0;         // events started
    int current = -1;      // the event on, -1 for none
    double ends = 0.0;     // when it ends
    // Advances by dt seconds of play; returns 1 when an event starts, -1 when one ends, else 0.
    int advance(double dt);
};

// What plays with the event on, each knob still in a safe range.
Values with_event(const Values& values, int event);

// A party's rules: all a member needs to play by the host's (the sliders, Chaos, the resolved seed,
// so each level rolls the same, and the event on). The host's game publishes them, a member's takes
// them (openrac_api.party_rules_publish, on_party_rules).
struct PartyRules {
    bool chaos = false;
    bool events = false;
    int minutes = 0;  // the minutes between events
    Values sliders{};
    std::uint64_t seed = 0;
    int event = -1;  // the event on, -1 for none
};

// Their bytes, as a member's mod of the same version reads them; false for ones it cannot read.
std::vector<std::uint8_t> encode_party_rules(const PartyRules& rules);
bool decode_party_rules(const void* data, std::uint32_t size, PartyRules& out);
// Their words for a player, a rule a line: "Bolts x2\nEnemy count x1.5\nChaos on".
std::string party_rules_text(const PartyRules& rules);

// The moby pool's guard (README.md, "How many enemies"): the spawned mobys' pool keeps this many
// free slots for what the game spawns (effects, shots, bolts), whatever the enemy count asks for.
constexpr int k_pool_reserve = 160;
// On Gaspar (level 9) the lava sparks alone nearly fill a console's pool of 255: there the copies
// leave that many free for them too, so they get none in a pool of a console's size and the rest of
// a bigger one (OpenRAC's port gives most levels 767).
constexpr int k_gaspar_sparks = 255;
// Slots the copies may take on a level with free_slots free.
int clone_budget(int free_slots, int level);
// Extra slots a copy whose pvars are size bytes needs for them (a spawned moby has 0x80).
int pvar_slots(int size);

}  // namespace dm

#endif  // DIFFICULTY_RULES_H
