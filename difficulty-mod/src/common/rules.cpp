// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's rules (rules.h), with no game memory in them: the sliders' ranges and what
// plays with them, Chaos's per-level roll and its timed events, the splitmix64 random stream and
// the seed's code, the accumulators and roundings that turn a multiplier into whole hits, bolts,
// prices and cells, and the moby pool's budget. Every random value comes from a seed, so the same
// seed and level always give the same values.

#include "rules.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace difficulty {

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

// The bits one hex digit holds, and the bits of a seed (the hash is folded to them).
constexpr int kHexDigitBits = 4;
constexpr int kSeedBits = 32;

// The salts that keep Chaos's rolls and the events' picks on streams of their own.
constexpr std::uint64_t kChaosRollSalt = 0x1000u;
constexpr std::uint64_t kEventPickSalt = 0x2000u;

// Chaos's values are rounded to hundredths.
constexpr double kRollRounding = 100.0;
// The slack that keeps a whole number from falling just under itself in floating point.
constexpr double kWholeSlack = 1e-9;

// The HUD's counts of cells of health: 1 to 5 and 8 only (documented: rules.h: max_health).
constexpr int kAllowedCells[] = {1, 2, 3, 4, 5, 8};

// A spawned moby's own pvars, in bytes (documented: pool.h: kSparePvars).
constexpr int kSparePvarBytes = 0x80;
// Gaspar's level number (documented: rules.h: kGasparSparks).
constexpr int kGasparLevel = 9;
// Blarg Station's level number (documented: rules.h: kBlargSpawns).
constexpr int kBlargLevel = 6;

// A hex digit's value, or -1 for any other character.
int hex_digit_value(char c) {
    // The decimal digits are 0 to 9.
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    // The capital letters A to F are 10 to 15.
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    // The small letters a to f are 10 to 15 too.
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    return -1;
}

}  // namespace

// The sliders: the setting's key, what the log calls it, its range and Chaos's range (the
// manifest's "settings" are in the same order).
const KnobInfo kKnobs[kKnobCount] = {
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
const double kEventMin[kKnobCount] =
    {0.25, 0.1, 0.1, 0.25, 0.4, 0.1, 0.1, 0.0, 0.25, 0.0, 0.25, 0.0};
const double kEventMax[kKnobCount] = {4.0, 6.0, 6.0, 2.5, 2.5, 5.0, 5.0, 6.0, 4.0, 8.0, 2.0, 6.0};

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

    const KnobInfo& k = kKnobs[knob];

    // Below the slider's range: its lowest value.
    if (value < k.min) {
        return k.min;
    }

    // Above the slider's range: its highest value.
    if (value > k.max) {
        return k.max;
    }

    return value;
}

bool sliders_moved(const Values& sliders) {
    // Each slider, as the game would use it.
    for (int k = 0; k < kKnobCount; ++k) {
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
    for (int k = 0; k < kKnobCount; ++k) {
        const double s = clamp_knob(k, sliders[k]);

        // Without Chaos every slider plays; with it only one moved off 1.0 does, and holds.
        if (!chaos || s != 1.0) {
            out[k] = s;
        }
    }

    return out;
}

std::uint64_t mix(std::uint64_t seed, std::uint64_t salt) {
    Rng stream(seed ^ (salt * kSplitIncrement));
    stream.next();
    return stream.next();
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
    for (int k = 0; k < kKnobCount; ++k) {
        const double lo = std::log(kKnobs[k].chaos_min);
        const double hi = std::log(kKnobs[k].chaos_max);
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
            const int d = hex_digit_value(c);

            // Not a hex digit: this is a word, not a code.
            if (d < 0) {
                hex = false;
                break;
            }

            v = v << kHexDigitBits | static_cast<std::uint64_t>(d);
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

    return (h ^ (h >> kSeedBits)) & kSeedMask;
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

    // A price or an ammo field that rounds to nothing stays at least 1: 0 is the game's "none".
    if (x < 1.0) {
        return 1;
    }

    // The field cannot hold more than its limit.
    if (x > limit) {
        return limit;
    }

    return static_cast<int>(x);
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

const Event kEvents[] = {
    {"Turbo Ratchet: Ratchet runs half again as fast", {kPlayerSpeed, kPlayerSpeed}, {1.5, 1.0}},
    {"Slow motion: the enemies at half speed", {kEnemySpeed, kEnemySpeed}, {0.5, 1.0}},
    {"Frenzy: the enemies half again as fast", {kEnemySpeed, kEnemySpeed}, {1.5, 1.0}},
    {"Double time: everyone half again as fast", {kPlayerSpeed, kEnemySpeed}, {1.5, 1.5}},
    {"Glass cannon: twice the damage, both ways", {kDamageDealt, kDamageTaken}, {2.0, 2.0}},
    {"Iron hide: half the damage to Ratchet", {kDamageTaken, kDamageTaken}, {0.5, 1.0}},
    {"Bolt rain: three times the bolts", {kBolts, kBolts}, {3.0, 1.0}},
    {"Ammo famine: twice the ammo per shot", {kAmmoUse, kAmmoUse}, {2.0, 1.0}},
    {"Trigger happy: a quarter of the ammo per shot", {kAmmoUse, kAmmoUse}, {0.25, 1.0}},
    {"Nanotech surge: health pickups count three times",
     {kHealthPickups, kHealthPickups},
     {3.0, 1.0}},
};
const int kEventCount = static_cast<int>(sizeof kEvents / sizeof kEvents[0]);

bool event_allowed(int event, unsigned knobs) {
    // Not one of the table's.
    if (event < 0 || event >= kEventCount) {
        return false;
    }

    const Event& e = kEvents[event];
    return (knobs & 1u << e.knob[0]) != 0 && (knobs & 1u << e.knob[1]) != 0;
}

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

    // Time for the next event: one is picked from the seed and how many there have been, among
    // those the game can play (all of them in rac1, so its picks are as they always were).
    if (clock >= every * (count + 1)) {
        int allowed[sizeof kEvents / sizeof kEvents[0]];
        int n = 0;

        // Each event, in the table's order.
        for (int e = 0; e < kEventCount; ++e) {
            // One the game can play.
            if (event_allowed(e, knobs)) {
                allowed[n++] = e;
            }
        }

        // No event this game can play: none ever starts.
        if (n == 0) {
            return 0;
        }

        Rng rng(mix(seed, kEventPickSalt + static_cast<std::uint64_t>(count)));
        current = allowed[rng.next() % static_cast<std::uint64_t>(n)];
        ends = clock + lasts;
        ++count;
        return 1;
    }

    return 0;
}

namespace {

// What a player reads for each slider, in the manifest's order.
constexpr const char* kRuleNames[kKnobCount] = {
    "Enemy count",
    "Damage to Ratchet",
    "Damage to enemies",
    "Enemy speed",
    "Ratchet's speed",
    "Weapon prices",
    "Ammo prices",
    "Ammo per shot",
    "Max ammo",
    "Bolts",
    "Max health",
    "Health pickups",
};

// The bytes' format: format, chaos, events, minutes (u8 each), event (i8), seed (u32), then the
// sliders (f64 bits each), big-endian.
constexpr std::uint8_t kRulesFormat = 1;

// The bytes before the seed: the format, chaos, events, minutes and event.
constexpr std::size_t kRulesHeaderBytes = 5;

// The seed's bytes (a seed is 32 bits), and each slider's (an f64's bits).
constexpr int kSeedBytes = 4;
constexpr int kSliderBytes = 8;

// How many bytes the rules take in all.
constexpr std::size_t kRulesBytes = kRulesHeaderBytes + kSeedBytes + kSliderBytes * kKnobCount;

// The most minutes between events the bytes can say (a u8).
constexpr int kMaxEventMinutes = 255;

// The bits in a byte, to step through a big-endian number.
constexpr int kByteBits = 8;

// The room a formatted multiplier needs, with its sign, digits and end mark.
constexpr std::size_t kTextBytes = 32;

// A multiplier as a player reads it: at most two decimals, no trailing zeros: x2, x1.5, x0.25.
std::string times(double value) {
    char text[kTextBytes];
    std::snprintf(text, sizeof text, "x%.2f", value);
    std::string out = text;

    // Trailing zeros are noise: x2.50 reads x2.5.
    while (out.back() == '0') {
        out.pop_back();
    }

    // A whole number has no point left: x2.00 reads x2.
    if (out.back() == '.') {
        out.pop_back();
    }

    return out;
}

}  // namespace

std::vector<std::uint8_t> encode_party_rules(const PartyRules& rules) {
    std::vector<std::uint8_t> out;
    out.reserve(kRulesBytes);
    out.push_back(kRulesFormat);
    out.push_back(rules.chaos ? 1 : 0);
    out.push_back(rules.events ? 1 : 0);
    out.push_back(static_cast<std::uint8_t>(std::clamp(rules.minutes, 0, kMaxEventMinutes)));
    out.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(rules.event)));

    // The seed, most significant byte first.
    for (int shift = (kSeedBytes - 1) * kByteBits; shift >= 0; shift -= kByteBits) {
        out.push_back(static_cast<std::uint8_t>(rules.seed >> shift));
    }

    // Each slider's double, as its bits, most significant byte first.
    for (int knob = 0; knob < kKnobCount; ++knob) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &rules.sliders.v[knob], sizeof bits);

        // One byte of the bits at a time.
        for (int shift = (kSliderBytes - 1) * kByteBits; shift >= 0; shift -= kByteBits) {
            out.push_back(static_cast<std::uint8_t>(bits >> shift));
        }
    }

    return out;
}

bool decode_party_rules(const void* data, std::uint32_t size, PartyRules& out) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);

    // Another format is another version's: the versions match in a party, so never read.
    if (bytes == nullptr || size != kRulesBytes || bytes[0] != kRulesFormat) {
        return false;
    }

    PartyRules rules;
    rules.chaos = bytes[1] != 0;
    rules.events = bytes[2] != 0;
    rules.minutes = bytes[3];
    rules.event = static_cast<std::int8_t>(bytes[4]);

    // An event index this version does not have means no event.
    if (rules.event < 0 || rules.event >= kEventCount) {
        rules.event = -1;
    }

    std::size_t at = kRulesHeaderBytes;

    // The seed, most significant byte first.
    for (int n = 0; n < kSeedBytes; ++n) {
        rules.seed = rules.seed << kByteBits | bytes[at++];
    }

    // Each slider's bits, most significant byte first, clamped to the slider's range.
    for (int knob = 0; knob < kKnobCount; ++knob) {
        std::uint64_t bits = 0;

        // One byte of the bits at a time.
        for (int n = 0; n < kSliderBytes; ++n) {
            bits = bits << kByteBits | bytes[at++];
        }

        std::memcpy(&rules.sliders.v[knob], &bits, sizeof bits);
        rules.sliders.v[knob] = clamp_knob(knob, rules.sliders.v[knob]);
    }

    out = rules;
    return true;
}

std::string party_rules_text(const PartyRules& rules) {
    std::string out;
    const auto add = [&out](const std::string& line) {
        out += (out.empty() ? "" : "\n") + line;
    };

    // A line for each slider off 1.0.
    for (int knob = 0; knob < kKnobCount; ++knob) {
        // A slider at 1.0 is the game's own, so it is not worth a line.
        if (rules.sliders[knob] != 1.0) {
            add(std::string(kRuleNames[knob]) + " " + times(rules.sliders[knob]));
        }
    }

    // Chaos rolls the sliders left at 1.0.
    if (rules.chaos) {
        add("Chaos on");
    }

    // The events' clock is on.
    if (rules.events) {
        add("Chaos events every " + std::to_string(rules.minutes) + " min");
    }

    return out.empty() ? "As in the game" : out;
}

Values with_event(const Values& values, int event) {
    Values out = values;

    // No event on (or one that is not there).
    if (event < 0 || event >= kEventCount) {
        return out;
    }

    const Event& e = kEvents[event];

    // The event's two knobs.
    for (int i = 0; i < 2; ++i) {
        // The second is the first again: not scaled twice.
        if (i == 1 && e.knob[1] == e.knob[0]) {
            break;
        }

        const int k = e.knob[i];
        double x = out[k] * e.factor[i];
        x = std::min(std::max(x, kEventMin[k]), kEventMax[k]);
        out[k] = x;
    }

    return out;
}

int clone_budget(int free_slots, int level) {
    int level_use = 0;

    // Gaspar's lava sparks nearly fill a console's pool.
    if (level == kGasparLevel) {
        level_use = kGasparSparks;
    }

    // Blarg Station spawns mobys of its own after the load.
    if (level == kBlargLevel) {
        level_use = kBlargSpawns;
    }

    const int b = free_slots - kPoolReserve - level_use;
    return b > 0 ? b : 0;
}

int pvar_slots(int size) {
    return size <= kSparePvarBytes ? 0 : (size + kSparePvarBytes - 1) / kSparePvarBytes;
}

}  // namespace difficulty
