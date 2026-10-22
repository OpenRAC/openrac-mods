// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's rules (rules.h).
#include "rules.h"

#include <cmath>
#include <cstdio>

namespace dm {

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
    for (double& x : v.v) {
        x = 1.0;
    }
    return v;
}

double clamp_knob(int knob, double value) {
    if (!std::isfinite(value)) {
        return 1.0;
    }
    const KnobInfo& k = k_knobs[knob];
    return value < k.min ? k.min : value > k.max ? k.max : value;
}

bool sliders_moved(const Values& sliders) {
    for (int k = 0; k < KNOB_COUNT; ++k) {
        if (clamp_knob(k, sliders[k]) != 1.0) {
            return true;
        }
    }
    return false;
}

Values effective(bool chaos, const Values& sliders, std::uint64_t seed, int level) {
    Values out = chaos ? chaos_roll(seed, level) : retail();
    for (int k = 0; k < KNOB_COUNT; ++k) {
        const double s = clamp_knob(k, sliders[k]);
        if (!chaos || s != 1.0) {
            out[k] = s;
        }
    }
    return out;
}

std::uint64_t mix(std::uint64_t a, std::uint64_t b) {
    Rng r(a ^ (b * 0x9E3779B97F4A7C15ull));
    r.next();
    return r.next();
}

std::uint64_t Rng::next() {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double Rng::unit() {
    return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
}

Values chaos_roll(std::uint64_t seed, int level) {
    Rng rng(mix(seed, 0x1000u + static_cast<std::uint64_t>(level)));
    Values out;
    for (int k = 0; k < KNOB_COUNT; ++k) {
        const double lo = std::log(k_knobs[k].chaos_min);
        const double hi = std::log(k_knobs[k].chaos_max);
        // Rounded to hundredths, so that the log reads as what plays.
        out[k] = std::round(std::exp(lo + (hi - lo) * rng.unit()) * 100.0) / 100.0;
    }
    return out;
}

std::uint64_t seed_from_text(const std::string& text) {
    if (text.size() == 8) {
        std::uint64_t v = 0;
        bool hex = true;
        for (const char c : text) {
            const int d = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                 : -1;
            if (d < 0) {
                hex = false;
                break;
            }
            v = v << 4 | static_cast<std::uint64_t>(d);
        }
        if (hex) {
            return v;
        }
    }
    std::uint64_t h = 0xCBF29CE484222325ull;  // FNV-1a, folded to the 32 bits a code shows
    for (const unsigned char c : text) {
        h = (h ^ c) * 0x100000001B3ull;
    }
    return (h ^ (h >> 32)) & 0xFFFFFFFFull;
}

std::string seed_code(std::uint64_t seed) {
    char out[16];
    std::snprintf(out, sizeof out, "%08X", static_cast<unsigned>(seed & 0xFFFFFFFFull));
    return out;
}

int Accumulator::take(double rate, int units) {
    carry += rate * units;
    if (!(carry >= 0.0)) {
        carry = 0.0;
    }
    const double whole = std::floor(carry + 1e-9);
    carry -= whole;
    if (carry < 0.0) {
        carry = 0.0;
    }
    return static_cast<int>(whole);
}

int scale_count(int n, double m, Rng& rng) {
    if (n <= 0 || m == 1.0) {
        return n;
    }
    const double x = n * m;
    const double whole = std::floor(x);
    int out = static_cast<int>(whole) + (rng.unit() < x - whole ? 1 : 0);
    return out;
}

int scale_amount(int value, double m, int limit) {
    if (value <= 0 || m == 1.0) {
        return value;
    }
    const double x = std::round(value * m);
    return x < 1.0 ? 1 : x > limit ? limit : static_cast<int>(x);
}

int max_health(int retail_cells, double m) {
    static const int allowed[] = {1, 2, 3, 4, 5, 8};
    if (m == 1.0) {
        return retail_cells;
    }
    const double want = std::floor(retail_cells * m + 1e-9);
    int out = 1;
    for (const int a : allowed) {
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
    if (!(dt > 0.0)) {
        return 0;
    }
    clock += dt;
    if (current >= 0) {
        if (clock >= ends) {
            current = -1;
            return -1;
        }
        return 0;
    }
    if (clock >= every * (count + 1)) {
        Rng rng(mix(seed, 0x2000u + static_cast<std::uint64_t>(count)));
        current = static_cast<int>(rng.next() % static_cast<std::uint64_t>(k_event_count));
        ends = clock + lasts;
        ++count;
        return 1;
    }
    return 0;
}

Values with_event(const Values& values, int event) {
    Values out = values;
    if (event < 0 || event >= k_event_count) {
        return out;
    }
    const Event& e = k_events[event];
    for (int i = 0; i < 2; ++i) {
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
    if (level == 9) {
        return 0;
    }
    const int b = free_slots - k_pool_reserve;
    return b > 0 ? b : 0;
}

int pvar_slots(int size) {
    return size <= 0x80 ? 0 : (size + 0x7F) / 0x80;
}

}  // namespace dm
