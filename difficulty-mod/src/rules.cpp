// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's rules (rules.h), with no game memory in them: the sliders' ranges and what
// plays with them, Chaos's per-level roll and its timed events, the splitmix64 random stream and
// the seed's code, the accumulators and roundings that turn a multiplier into whole hits, bolts,
// prices and cells, and the moby pool's budget. Every random value comes from a seed, so the same
// seed and level always give the same values.
#include "rules.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace dm {

namespace {

// splitmix64's increment and its two multipliers (documented: Steele, Lea and Flood, "Fast
// splittable pseudorandom number generators", 2014), and the shifts of its finalizer.
constexpr std::uint64_t kSplitIncrement = 0x9E3779B97F4A7C15ull;
constexpr std::uint64_t kSplitMulA = 0xBF58476D1CE4E5B9ull;
constexpr std::uint64_t kSplitMulB = 0x94D049BB133111EBull;
constexpr int kSplitShiftA = 30;
constexpr int kSplitShiftB = 27;
constexpr int kSplitShiftC = 31;
// A double's 53 bits of mantissa: the stream's top 53 bits over 2^53 are a value in [0, 1).
constexpr int kUnitShift = 11;
constexpr double kUnitScale = 1.0 / 9007199254740992.0;

// FNV-1a's offset basis and prime, 64 bits (documented: the FNV specification).
constexpr std::uint64_t kFnvBasis = 0xCBF29CE484222325ull;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ull;

// A seed is 32 bits; its code is this many hex digits.
constexpr std::uint64_t kSeedMask = 0xFFFFFFFFull;
constexpr std::size_t kSeedCodeDigits = 8;

// The salts that keep Chaos's rolls and the events' picks on streams of their own.
constexpr std::uint64_t kChaosRollSalt = 0x1000u;
constexpr std::uint64_t kEventPickSalt = 0x2000u;

// Chaos's values are rounded to hundredths.
constexpr double kRollRounding = 100.0;
// The slack that keeps a whole number from falling just under itself in floating point.
constexpr double kWholeSlack = 1e-9;

// The HUD's counts of cells of health: 1 to 5 and 8 only (documented: rules.h: max_health).
constexpr int kAllowedCells[] = {1, 2, 3, 4, 5, 8};

// A spawned moby's own pvars, in bytes (documented: pool.h: k_spare_pvars).
constexpr int kSparePvarBytes = 0x80;
// Gaspar's level number (documented: rules.h: k_gaspar_sparks).
constexpr int kGasparLevel = 9;
// Blarg Station's level number (documented: rules.h: k_blarg_spawns).
constexpr int kBlargLevel = 6;

}  // namespace

// The sliders: the setting's key, what the log calls it, its range and Chaos's range (the
// manifest's "settings" are in the same order).
const KnobInfo k_knobs[KNOB_COUNT] = {
    {"enemy_count", "enemy count", 0.25, 4.0, 0.5, 2.5},
    {"damage_taken", "damage to Ratchet", 0.25, 4.0, 0.5, 3.0},
    {"damage_dealt", "damage to enemies", 0.25, 4.0, 0.5, 2.0},
    {"enemy_speed", "enemy speed", 0.5, 2.0, 0.7, 1.6},
    {"player_speed", "Ratchet's speed", 0.5, 2.0, 0.8, 1.4},
    {"weapon_cost", "weapon prices", 0.1, 5.0, 0.5, 2.5},
    {"ammo_cost", "ammo prices", 0.1, 5.0, 0.5, 2.5},
    {"ammo_use", "ammo per shot", 0.25, 4.0, 0.5, 2.0},
    {"max_ammo", "max ammo", 0.25, 4.0, 0.5, 2.0},
    {"bolts", "bolt drops", 0.0, 5.0, 0.5, 3.0},
    {"max_health", "max health", 0.25, 2.0, 0.5, 2.0},
    {"health_pickups", "health pickups", 0.0, 4.0, 0.5, 2.0},
};

namespace {

// What an event may push a knob to, past the sliders' ranges but no further.
const double k_event_min[KNOB_COUNT] =
    {0.25, 0.1, 0.1, 0.25, 0.4, 0.1, 0.1, 0.0, 0.25, 0.0, 0.25, 0.0};
const double k_event_max[KNOB_COUNT] = {4.0, 6.0, 6.0, 2.5, 2.5, 5.0, 5.0, 6.0, 4.0, 8.0, 2.0, 6.0};

}  // namespace

Values retail() {
    Values v;

    // Every knob.
    for (double& x : v.v) {
        x = 1.0;
    }

    return v;
}

double clamp_knob(int knob, double value) {
    // Not a number, or infinite: retail.
    if (!std::isfinite(value)) {
        return 1.0;
    }

    const KnobInfo& k = k_knobs[knob];
    return value < k.min ? k.min : value > k.max ? k.max : value;
}

bool sliders_moved(const Values& sliders) {
    // Each slider, as the game would use it.
    for (int k = 0; k < KNOB_COUNT; ++k) {
        // This one is off 1.0.
        if (clamp_knob(k, sliders[k]) != 1.0) {
            return true;
        }
    }

    return false;
}

Values effective(bool chaos, const Values& sliders, std::uint64_t seed, int level) {
    Values out = chaos ? chaos_roll(seed, level) : retail();

    // Each slider, clamped.
    for (int k = 0; k < KNOB_COUNT; ++k) {
        const double s = clamp_knob(k, sliders[k]);

        // Without Chaos every slider plays; with it only one moved off 1.0 does, and holds.
        if (!chaos || s != 1.0) {
            out[k] = s;
        }
    }

    return out;
}

std::uint64_t mix(std::uint64_t a, std::uint64_t b) {
    Rng r(a ^ (b * kSplitIncrement));
    r.next();
    return r.next();
}

std::uint64_t Rng::next() {
    std::uint64_t z = (state += kSplitIncrement);
    z = (z ^ (z >> kSplitShiftA)) * kSplitMulA;
    z = (z ^ (z >> kSplitShiftB)) * kSplitMulB;
    return z ^ (z >> kSplitShiftC);
}

double Rng::unit() {
    return static_cast<double>(next() >> kUnitShift) * kUnitScale;
}

Values chaos_roll(std::uint64_t seed, int level) {
    Rng rng(mix(seed, kChaosRollSalt + static_cast<std::uint64_t>(level)));
    Values out;

    // Each knob: log-uniform in its Chaos range.
    for (int k = 0; k < KNOB_COUNT; ++k) {
        const double lo = std::log(k_knobs[k].chaos_min);
        const double hi = std::log(k_knobs[k].chaos_max);
        // Rounded to hundredths, so that the log reads as what plays.
        out[k] = std::round(std::exp(lo + (hi - lo) * rng.unit()) * kRollRounding) / kRollRounding;
    }

    return out;
}

std::uint64_t seed_from_text(const std::string& text) {
    // The size of a code: it may be one.
    if (text.size() == kSeedCodeDigits) {
        std::uint64_t v = 0;
        bool hex = true;

        // Each character, as a hex digit.
        for (const char c : text) {
            const int d = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                 : -1;

            // Not a hex digit: this is a word, not a code.
            if (d < 0) {
                hex = false;
                break;
            }

            v = v << 4 | static_cast<std::uint64_t>(d);
        }

        // All eight were digits: the code is the seed.
        if (hex) {
            return v;
        }
    }

    std::uint64_t h = kFnvBasis;  // FNV-1a, folded to the 32 bits a code shows

    // Each byte of the text.
    for (const unsigned char c : text) {
        h = (h ^ c) * kFnvPrime;
    }

    return (h ^ (h >> 32)) & kSeedMask;
}

std::string seed_code(std::uint64_t seed) {
    char out[16];
    std::snprintf(out, sizeof out, "%08X", static_cast<unsigned>(seed & kSeedMask));
    return out;
}

int Accumulator::take(double rate, int units) {
    carry += rate * units;

    // Negative or not a number: start again from none.
    if (!(carry >= 0.0)) {
        carry = 0.0;
    }

    const double whole = std::floor(carry + kWholeSlack);
    carry -= whole;

    // The slack took more than the carry had.
    if (carry < 0.0) {
        carry = 0.0;
    }

    return static_cast<int>(whole);
}

int scale_count(int n, double m, Rng& rng) {
    // Nothing to scale, or retail.
    if (n <= 0 || m == 1.0) {
        return n;
    }

    const double x = n * m;
    const double whole = std::floor(x);
    int out = static_cast<int>(whole) + (rng.unit() < x - whole ? 1 : 0);
    return out;
}

int scale_amount(int value, double m, int limit) {
    // None stays none, and retail stays as it is.
    if (value <= 0 || m == 1.0) {
        return value;
    }

    const double x = std::round(value * m);
    return x < 1.0 ? 1 : x > limit ? limit : static_cast<int>(x);
}

int max_health(int retail_cells, double m) {
    // Retail stays as it is.
    if (m == 1.0) {
        return retail_cells;
    }

    const double want = std::floor(retail_cells * m + kWholeSlack);
    int out = 1;

    // Each count the HUD can show, the largest one not over what was asked for.
    for (const int a : kAllowedCells) {
        // This count is not over.
        if (a <= want) {
            out = a;
        }
    }

    return out;
}

const Event k_events[] = {
    {"Turbo Ratchet: Ratchet runs half again as fast", {PLAYER_SPEED, PLAYER_SPEED}, {1.5, 1.0}},
    {"Slow motion: the enemies at half speed", {ENEMY_SPEED, ENEMY_SPEED}, {0.5, 1.0}},
    {"Frenzy: the enemies half again as fast", {ENEMY_SPEED, ENEMY_SPEED}, {1.5, 1.0}},
    {"Double time: everyone half again as fast", {PLAYER_SPEED, ENEMY_SPEED}, {1.5, 1.5}},
    {"Glass cannon: twice the damage, both ways", {DAMAGE_DEALT, DAMAGE_TAKEN}, {2.0, 2.0}},
    {"Iron hide: half the damage to Ratchet", {DAMAGE_TAKEN, DAMAGE_TAKEN}, {0.5, 1.0}},
    {"Bolt rain: three times the bolts", {BOLTS, BOLTS}, {3.0, 1.0}},
    {"Ammo famine: twice the ammo per shot", {AMMO_USE, AMMO_USE}, {2.0, 1.0}},
    {"Trigger happy: a quarter of the ammo per shot", {AMMO_USE, AMMO_USE}, {0.25, 1.0}},
    {"Nanotech surge: health pickups count three times",
     {HEALTH_PICKUPS, HEALTH_PICKUPS},
     {3.0, 1.0}},
};
const int k_event_count = static_cast<int>(sizeof k_events / sizeof k_events[0]);

int EventClock::advance(double dt) {
    // No time passed (or not a number).
    if (!(dt > 0.0)) {
        return 0;
    }

    clock += dt;

    // An event is on.
    if (current >= 0) {
        // Its time is up.
        if (clock >= ends) {
            current = -1;
            return -1;
        }

        return 0;
    }

    // Time for the next event: one is picked from the seed and how many there have been.
    if (clock >= every * (count + 1)) {
        Rng rng(mix(seed, kEventPickSalt + static_cast<std::uint64_t>(count)));
        current = static_cast<int>(rng.next() % static_cast<std::uint64_t>(k_event_count));
        ends = clock + lasts;
        ++count;
        return 1;
    }

    return 0;
}

Values with_event(const Values& values, int event) {
    Values out = values;

    // No event on (or one that is not there).
    if (event < 0 || event >= k_event_count) {
        return out;
    }

    const Event& e = k_events[event];

    // The event's two knobs.
    for (int i = 0; i < 2; ++i) {
        // The second is the first again: not scaled twice.
        if (i == 1 && e.knob[1] == e.knob[0]) {
            break;
        }

        const int k = e.knob[i];
        double x = out[k] * e.factor[i];
        x = x < k_event_min[k] ? k_event_min[k] : x > k_event_max[k] ? k_event_max[k] : x;
        out[k] = x;
    }

    return out;
}

int clone_budget(int free_slots, int level) {
    const int level_use = level == kGasparLevel  ? k_gaspar_sparks
                          : level == kBlargLevel ? k_blarg_spawns
                                                 : 0;
    const int b = free_slots - k_pool_reserve - level_use;
    return b > 0 ? b : 0;
}

int pvar_slots(int size) {
    return size <= kSparePvarBytes ? 0 : (size + kSparePvarBytes - 1) / kSparePvarBytes;
}

}  // namespace dm
