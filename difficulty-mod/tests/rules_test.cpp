// SPDX-License-Identifier: GPL-3.0-or-later
// The difficulty mod's rules (src/rules.h): the sliders' ranges and clamping, what plays with
// the sliders and with Chaos, Chaos's rolls (the same seed and level always give the same values,
// each in its range) and its timed events, the accumulators that make whole hits and rounds of a
// multiplier, prices and max health, and the pool's budget.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>

#include "rules.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

using namespace dm;

static void ranges() {
    // 1.0 (retail) is in every slider's range, and Chaos's range inside it around 1.0. The presets
    // are the manifest's (tests/presets_test.py).
    for (int k = 0; k < KNOB_COUNT; ++k) {
        CHECK(retail()[k] == 1.0);
        CHECK(k_knobs[k].min <= 1.0 && 1.0 <= k_knobs[k].max);
        CHECK(k_knobs[k].min <= k_knobs[k].chaos_min && k_knobs[k].chaos_min <= 1.0);
        CHECK(1.0 <= k_knobs[k].chaos_max && k_knobs[k].chaos_max <= k_knobs[k].max);
    }
}

static void clamping() {
    CHECK(clamp_knob(ENEMY_COUNT, 10.0) == 4.0);
    CHECK(clamp_knob(ENEMY_COUNT, 0.0) == 0.25);
    CHECK(clamp_knob(ENEMY_SPEED, 3.0) == 2.0);
    CHECK(clamp_knob(BOLTS, -1.0) == 0.0);
    CHECK(clamp_knob(DAMAGE_TAKEN, std::numeric_limits<double>::quiet_NaN()) == 1.0);
    CHECK(clamp_knob(DAMAGE_TAKEN, std::numeric_limits<double>::infinity()) == 1.0);
    CHECK(clamp_knob(WEAPON_COST, 2.5) == 2.5);
}

static void effective_values() {
    const Values ones = retail();
    // The sliders untouched: everything 1.0, the game as it is.
    Values v = effective(false, ones, 1234, 3);
    for (int k = 0; k < KNOB_COUNT; ++k) {
        CHECK(v[k] == 1.0);
    }
    CHECK(!sliders_moved(ones));
    // The sliders are what plays (a preset fills them).
    Values sliders = ones;
    sliders[ENEMY_SPEED] = 1.7;
    sliders[DAMAGE_TAKEN] = 2.0;
    CHECK(sliders_moved(sliders));
    v = effective(false, sliders, 1, 1);
    CHECK(v[ENEMY_SPEED] == 1.7 && v[DAMAGE_TAKEN] == 2.0 && v[BOLTS] == 1.0);
    // A slider out of range counts clamped.
    sliders[ENEMY_SPEED] = 9.0;
    CHECK(effective(false, sliders, 1, 1)[ENEMY_SPEED] == 2.0);
    // Chaos with a slider moved: the slider holds on every level, the rest roll.
    sliders = ones;
    sliders[BOLTS] = 0.0;
    for (int level = 0; level < 19; ++level) {
        const Values c = effective(true, sliders, 77, level);
        CHECK(c[BOLTS] == 0.0);
        CHECK(c[DAMAGE_TAKEN] == chaos_roll(77, level)[DAMAGE_TAKEN]);
    }
}

static void chaos() {
    // The same seed and level: the same values, each in its range; other levels and seeds differ.
    std::set<double> seen;
    for (std::uint64_t seed : {1ull, 0xDEADBEEFull, 42ull}) {
        for (int level = -1; level < 19; ++level) {
            const Values a = chaos_roll(seed, level), b = chaos_roll(seed, level);
            for (int k = 0; k < KNOB_COUNT; ++k) {
                CHECK(a[k] == b[k]);
                CHECK(a[k] >= k_knobs[k].chaos_min - 1e-9 && a[k] <= k_knobs[k].chaos_max + 1e-9);
                CHECK(std::fabs(a[k] * 100.0 - std::round(a[k] * 100.0)) < 1e-6);
            }
            seen.insert(a[DAMAGE_TAKEN]);
        }
    }
    CHECK(seen.size() > 20);
    // A known roll stays the same from build to build (the seed a player shares means one game).
    const Values fixed = chaos_roll(0x12345678ull, 4);
    const Values again = chaos_roll(seed_from_text("12345678"), 4);
    for (int k = 0; k < KNOB_COUNT; ++k) {
        CHECK(fixed[k] == again[k]);
    }
    // Seeds: a code round-trips, any other text hashes the same way each time.
    CHECK(seed_code(0x0A1B2C3Dull) == "0A1B2C3D");
    CHECK(seed_from_text("0A1B2C3D") == 0x0A1B2C3Dull);
    CHECK(seed_from_text("0a1b2c3d") == 0x0A1B2C3Dull);
    CHECK(seed_from_text("ratchet") == seed_from_text("ratchet"));
    CHECK(seed_from_text("ratchet") != seed_from_text("clank"));
    CHECK(seed_from_text(seed_code(seed_from_text("ratchet"))) == seed_from_text("ratchet"));
    CHECK(seed_from_text("ratchet") <= 0xFFFFFFFFull);
    // The generator is the same stream for the same seed.
    Rng r1(99), r2(99);
    for (int i = 0; i < 100; ++i) {
        const double u = r1.unit();
        CHECK(u == r2.unit() && u >= 0.0 && u < 1.0);
    }
}

static void events() {
    EventClock clock;
    clock.seed = 5;
    clock.every = 60.0;
    clock.lasts = 10.0;
    int starts = 0, ends = 0;
    for (int tick = 0; tick < 60 * 330; ++tick) {  // five and a half minutes at 60 ticks a second
        const int c = clock.advance(1.0 / 60.0);
        starts += c > 0;
        ends += c < 0;
        if (c > 0) {
            CHECK(clock.current >= 0 && clock.current < k_event_count);
        }
    }
    CHECK(starts == 5 && ends == 5 && clock.current == -1);
    // The same seed: the same events in the same order.
    EventClock a, b;
    a.seed = b.seed = 1234;
    a.every = b.every = 30.0;
    for (int tick = 0; tick < 60 * 60 * 3; ++tick) {
        CHECK(a.advance(1.0 / 60.0) == b.advance(1.0 / 60.0));
        CHECK(a.current == b.current);
    }
    // An event changes only its knobs, within the safe ranges; none leaves a knob outside them.
    const Values ones = retail();
    for (int e = 0; e < k_event_count; ++e) {
        const Values v = with_event(ones, e);
        int changed = 0;
        for (int k = 0; k < KNOB_COUNT; ++k) {
            changed += v[k] != 1.0;
            CHECK(
                k != ENEMY_COUNT && k != WEAPON_COST && k != AMMO_COST && k != MAX_AMMO
                        && k != MAX_HEALTH
                    ? true
                    : v[k] == 1.0
            );
        }
        CHECK(changed >= 1 && changed <= 2);
        Values high = ones;
        high[PLAYER_SPEED] = 2.0;
        high[ENEMY_SPEED] = 2.0;
        const Values h = with_event(high, e);
        CHECK(h[PLAYER_SPEED] <= 2.5 && h[ENEMY_SPEED] <= 2.5);
    }
    CHECK(with_event(ones, -1)[PLAYER_SPEED] == 1.0);
}

static void accumulators() {
    // 1.0: one for one, always.
    Accumulator a;
    for (int i = 0; i < 1000; ++i) {
        CHECK(a.take(1.0) == 1);
    }
    // 0.5: every other one; 1.5: one, two, one, two; 0.25: one in four.
    Accumulator h;
    int total = 0;
    for (int i = 0; i < 100; ++i) {
        const int t = h.take(0.5);
        CHECK(t == (i % 2 == 1 ? 1 : 0));
        total += t;
    }
    CHECK(total == 50);
    Accumulator o;
    CHECK(o.take(1.5) == 1 && o.take(1.5) == 2 && o.take(1.5) == 1 && o.take(1.5) == 2);
    Accumulator q;
    total = 0;
    for (int i = 0; i < 400; ++i) {
        total += q.take(0.25);
    }
    CHECK(total == 100);
    // Several units at once (rounds of a shot, cells of a pickup).
    Accumulator s;
    CHECK(s.take(2.0, 3) == 6 && s.take(0.0, 5) == 0);
    // Counts: 1.0 exact, others right on average.
    Rng rng(3);
    CHECK(scale_count(17, 1.0, rng) == 17);
    CHECK(scale_count(0, 3.0, rng) == 0);
    long sum = 0;
    for (int i = 0; i < 10000; ++i) {
        const int n = scale_count(10, 0.55, rng);
        CHECK(n == 5 || n == 6);
        sum += n;
    }
    CHECK(sum > 54000 && sum < 56000);
}

static void amounts() {
    CHECK(scale_amount(0, 3.0, 65535) == 0);  // no ammo item stays none
    CHECK(scale_amount(1, 0.1, 65535) == 1);  // nothing becomes free
    CHECK(scale_amount(2500, 2.0, 9999999) == 5000);
    CHECK(scale_amount(40000, 2.0, 65535) == 65535);
    CHECK(scale_amount(150000, 1.0, 9999) == 150000);  // 1.0 leaves it as the game has it
    // Max health: the HUD's counts only.
    CHECK(max_health(4, 1.0) == 4 && max_health(5, 1.0) == 5 && max_health(8, 1.0) == 8);
    CHECK(max_health(4, 2.0) == 8);
    CHECK(max_health(4, 1.5) == 5);
    CHECK(max_health(4, 1.25) == 5);
    CHECK(max_health(5, 2.0) == 8);
    CHECK(max_health(8, 2.0) == 8);
    CHECK(max_health(8, 0.5) == 4);
    CHECK(max_health(4, 0.5) == 2);
    CHECK(max_health(4, 0.25) == 1);
    CHECK(max_health(5, 1.5) == 5);  // 7.5: no place for 6 or 7
    for (int r : {4, 5, 8}) {
        for (double m = 0.25; m <= 2.0; m += 0.25) {
            const int c = max_health(r, m);
            CHECK(c == 1 || c == 2 || c == 3 || c == 4 || c == 5 || c == 8);
        }
    }
}

static void budget() {
    CHECK(clone_budget(255, 4) == 255 - k_pool_reserve);
    CHECK(clone_budget(399, 17) == 399 - k_pool_reserve);
    CHECK(clone_budget(100, 4) == 0);
    CHECK(clone_budget(255, 9) == 0);  // Gaspar
    CHECK(pvar_slots(0) == 0 && pvar_slots(0x80) == 0);
    CHECK(
        pvar_slots(0x81) == 2 && pvar_slots(0x100) == 2 && pvar_slots(0x101) == 3
        && pvar_slots(644) == 6
    );
}

int main() {
    ranges();
    clamping();
    effective_values();
    chaos();
    events();
    accumulators();
    amounts();
    budget();
    std::printf("rules_test: ok\n");
    return 0;
}
