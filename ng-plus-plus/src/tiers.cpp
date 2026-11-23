// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++'s rules (tiers.h). The tables below are the mod's balance: change them here.

#include "tiers.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ng_plus_plus {
namespace {

// clang-format off: one step per row, so the columns read as the README's table.
// The eight tiers: buy, then level. A bought tier's price is price()'s, from the weapon's Mega.
constexpr Step kTierSteps[kTiers] = {
    {"Alpha",   How::Buy, 0,      1.50f, 1.10f, Tint::Lime},
    {"Beta",    How::Exp, 8'000,  2.00f, 1.20f, Tint::Cyan},
    {"Gamma",   How::Buy, 0,      2.50f, 1.30f, Tint::Emerald},
    {"Delta",   How::Exp, 12'000, 3.00f, 1.40f, Tint::Teal},
    {"Epsilon", How::Buy, 0,      3.75f, 1.50f, Tint::Violet},
    {"Zeta",    How::Exp, 16'000, 4.50f, 1.60f, Tint::Magenta},
    {"Eta",     How::Buy, 0,      5.50f, 1.80f, Tint::Crimson},
    {"Omega",   How::Exp, 24'000, 7.00f, 2.00f, Tint::Prismatic},
};
// clang-format on

// clang-format off: one step per row, as kTierSteps.
// The eight tiers of a weapon whose last real version is bought (Kind::Two, Kind::Single): Alpha
// by experience, kMasteryExp more at that version, then as kTierSteps. A full bar levels it up.
constexpr Step kMasterySteps[kTiers] = {
    {"Alpha",   How::Exp, kMasteryExp, 1.50f, 1.10f, Tint::Lime},
    {"Beta",    How::Exp, 8'000,       2.00f, 1.20f, Tint::Cyan},
    {"Gamma",   How::Buy, 0,           2.50f, 1.30f, Tint::Emerald},
    {"Delta",   How::Exp, 12'000,      3.00f, 1.40f, Tint::Teal},
    {"Epsilon", How::Buy, 0,           3.75f, 1.50f, Tint::Violet},
    {"Zeta",    How::Exp, 16'000,      4.50f, 1.60f, Tint::Magenta},
    {"Eta",     How::Buy, 0,           5.50f, 1.80f, Tint::Crimson},
    {"Omega",   How::Exp, 24'000,      7.00f, 2.00f, Tint::Prismatic},
};
// clang-format on

// clang-format off: one step per row, as kTierSteps.
// The eight tiers of a weapon that never earns experience (Kind::NoExp): all bought, the rest as
// kTierSteps.
constexpr Step kBoughtSteps[kTiers] = {
    {"Alpha",   How::Buy, 0, 1.50f, 1.10f, Tint::Lime},
    {"Beta",    How::Buy, 0, 2.00f, 1.20f, Tint::Cyan},
    {"Gamma",   How::Buy, 0, 2.50f, 1.30f, Tint::Emerald},
    {"Delta",   How::Buy, 0, 3.00f, 1.40f, Tint::Teal},
    {"Epsilon", How::Buy, 0, 3.75f, 1.50f, Tint::Violet},
    {"Zeta",    How::Buy, 0, 4.50f, 1.60f, Tint::Magenta},
    {"Eta",     How::Buy, 0, 5.50f, 1.80f, Tint::Crimson},
    {"Omega",   How::Buy, 0, 7.00f, 2.00f, Tint::Prismatic},
};
// clang-format on

// clang-format off: one step per row, as kTierSteps.
// The RYNO II's path: Heavy by experience, the eight tiers on it, and Ultima last, by experience at
// Omega. Its own curve, rising from Heavy's x1.4 to Ultima's x9.7, each step above the one before.
constexpr Step kRynoSteps[kRynoStepCount] = {
    {"Heavy",   How::Exp, 7'500,  1.40f, 1.50f, Tint::Orange},
    {"Alpha",   How::Buy, 0,      2.00f, 1.65f, Tint::Lime},
    {"Beta",    How::Exp, 10'000, 2.70f, 1.80f, Tint::Cyan},
    {"Gamma",   How::Buy, 0,      3.45f, 1.95f, Tint::Emerald},
    {"Delta",   How::Exp, 15'000, 4.30f, 2.15f, Tint::Teal},
    {"Epsilon", How::Buy, 0,      5.20f, 2.35f, Tint::Violet},
    {"Zeta",    How::Exp, 20'000, 6.20f, 2.55f, Tint::Magenta},
    {"Eta",     How::Buy, 0,      7.30f, 2.75f, Tint::Crimson},
    {"Omega",   How::Exp, 30'000, 8.45f, 2.95f, Tint::Prismatic},
    {"Ultima",  How::Exp, 37'500, 9.70f, 3.20f, Tint::Ultima},
};
// clang-format on

// The steady tints, 0x00BBGGRR as the game's own colours are (seen in game code: D_001ADAF8), in
// the order of Tint's first seven.
constexpr std::uint32_t kTintRgb[] = {
    0x20FFB0u,  // lime
    0xFFE000u,  // cyan
    0x50A010u,  // emerald
    0x909000u,  // teal
    0xFF4090u,  // violet
    0xD020FFu,  // magenta
    0x3C14DCu,  // crimson
};

// How many steady tints there are.
constexpr std::uint32_t kTintCount = sizeof kTintRgb / sizeof kTintRgb[0];

// RYNO II Heavy's orange, 0x00BBGGRR.
constexpr std::uint32_t kOrangeRgb = 0x0080FFu;

// RYNO II Ultima's two colours: a red that sinks to near black and back, 0x00BBGGRR.
constexpr std::uint32_t kUltimaRed = 0x0000E0u;
constexpr std::uint32_t kUltimaBlack = 0x080808u;

// RYNO II Ultima's effects: a bright red and a deep one, 0x00BBGGRR (effect_rgb).
constexpr std::uint32_t kEffectUltimaBright = 0x1A1AFFu;
constexpr std::uint32_t kEffectUltimaDeep = 0x000060u;

// Ticks a prismatic tint takes from one colour to the next.
constexpr std::uint32_t kPrismTicks = 45u;

// Ticks of one beat of Ultima's black and red.
constexpr std::uint32_t kUltimaTicks = 60u;

// The steps of a mix of two colours: 256 is all of the second.
constexpr std::uint32_t kMixSteps = 256u;

// Times the base, for the first bought step, the second, ...
constexpr float kPriceTimes[] = {1.5f, 2.5f, 4.0f, 6.0f};

// The RYNO II's: its four bought tiers (Alpha, Gamma, Epsilon, Eta), far up the ladder (the
// strongest weapon: about 2.5 to 2.8 times the other weapons' top ladder, 6 to 25 million on its 1
// million).
constexpr float kRynoPriceTimes[] = {6.0f, 10.0f, 16.0f, 25.0f};

// For a weapon that buys every tier (Kind::NoExp): the ladder above at Alpha, Gamma, Epsilon and
// Eta, the tiers between in between, Omega at the RYNO II's top.
constexpr float kPriceEvery[kTiers] = {1.5f, 2.0f, 2.5f, 3.25f, 4.0f, 5.0f, 6.0f, 9.0f};

// A price that would not rise above the one before rises by a tenth, or by this at the least.
constexpr float kPriceRise = 1.1f;
constexpr std::int32_t kPriceRiseLeast = 5'000;

// The smallest unit a price is rounded to, and how many units fit under the next unit: prices keep
// two leading digits and a half (562,500 is 565,000).
constexpr double kRoundUnit = 5.0;
constexpr double kRoundUnitsPerDecade = 200.0;

// By cycle from 2 on: the enemies take more of a beating than a player's tier can keep up with. The
// tier a player can have by then (cycle 2 Delta's x3, 3 Zeta's x4.5, 4 on Omega's x7) needs a
// quarter to a half more hits than V4 did in the first challenge mode (x3.75 1.25, x6 1.33, x9 1.29,
// x9.75 1.39, x10.25 1.46, then x10.5 1.5), so the later runs are clearly harder even with every
// weapon at Omega. The damage they deal rises more gently: a fight lasting longer already costs
// more health.
constexpr float kEnemyHealth[] = {1.0f, 1.0f, 3.75f, 6.0f, 9.0f, 9.75f, 10.25f};
constexpr float kEnemyDamage[] = {1.0f, 1.0f, 1.375f, 1.75f, 2.125f, 2.5f, 2.875f};

// Past the tables, each cycle adds this much health and damage, up to the most.
constexpr float kHealthStep = 0.25f;
constexpr float kDamageStep = 0.375f;
constexpr float kHealthMost = 10.5f;  // Omega's x7 times 1.5: at most half as many hits again
constexpr float kDamageMost = 4.0f;

// A boss's health on top of the curve, by cycle as kEnemyHealth: twice from cycle 2, a quarter more
// each run, up to kBossMost.
constexpr float kBossHealth[] = {1.0f, 1.0f, 2.0f, 2.25f, 2.5f, 2.75f, 3.0f};
constexpr float kBossStep = 0.0f;
constexpr float kBossMost = 3.0f;

// The first cycle of the curve.
constexpr int kFirstCurveCycle = 2;

// The RYNO II's hits on a tough enemy and on a boss, times the game's own RYNO II's (ryno_hits):
// the first run's, what each run adds, the most; and the most a tough enemy's size multiplies.
constexpr float kRynoToughFirst = 2.0f;
constexpr float kRynoToughStep = 0.5f;
constexpr float kRynoToughMost = 4.0f;
constexpr float kRynoSizeMost = 3.0f;
constexpr float kRynoBossFirst = 3.0f;
constexpr float kRynoBossStep = 1.0f;
constexpr float kRynoBossMost = 7.0f;

// The weapon experience of a kill by cycle from kFirstCurveCycle on; the last holds past it.
constexpr float kExperienceRun[] = {1.5f, 2.5f, 3.5f, 4.5f};

// An enemy's own bolts up to this give no more experience for its toughness; past it the fourth
// root of its bolts over this, at most kExperienceToughMost.
constexpr float kExperienceToughBolts = 20.0f;
constexpr float kExperienceToughMost = 2.449f;

// How many cycles the tables have.
constexpr int kCurveCycles = static_cast<int>(sizeof kEnemyHealth / sizeof kEnemyHealth[0]);

// Returns `x` rounded to its two leading digits and a half: 562,500 is 565,000.
std::int32_t round_price(double x) {
    double unit = kRoundUnit;

    // The unit grows by tens until `x` is under 200 of it.
    while (x >= unit * kRoundUnitsPerDecade) {
        unit *= 10.0;
    }

    const auto units = static_cast<std::int64_t>(std::floor(x / unit + 0.5));
    return static_cast<std::int32_t>(units * static_cast<std::int64_t>(unit));
}

// Returns the experience goals' multiplier of `kind`: kRynoExpScale for the RYNO II, else 1.
std::int32_t exp_scale(Kind kind) {
    return kind == Kind::Ryno ? kRynoExpScale : 1;
}

// Returns whether the version before the step at `level` was bought, so needs mastering. Never for
// a weapon that earns no experience: it could never master one.
bool previous_bought(Kind kind, int level) {
    // A weapon that earns nothing would wait for ever.
    if (kind == Kind::NoExp) {
        return false;
    }

    // Past the first step, the step before says.
    if (level > 0) {
        return step_at(kind, level - 1)->how == How::Buy;
    }

    // The last real version: bought for the two-version weapons, the Zodiac and the RYNO II.
    return kind == Kind::Two || kind == Kind::Single || kind == Kind::Ryno;
}

// Returns `a` and `b` mixed, `t` of kMixSteps toward `b`, per 8-bit channel.
std::uint32_t mix(std::uint32_t a, std::uint32_t b, std::uint32_t t) {
    std::uint32_t out = 0;

    // The three channels of 0x00BBGGRR, eight bits apart.
    for (std::uint32_t shift = 0; shift < 24u; shift += 8u) {
        const auto from = static_cast<std::int32_t>((a >> shift) & 0xFFu);
        const auto to = static_cast<std::int32_t>((b >> shift) & 0xFFu);
        const std::int32_t step = ((to - from) * static_cast<std::int32_t>(t)) / 256;
        out |= (static_cast<std::uint32_t>(from + step) & 0xFFu) << shift;
    }

    return out;
}

// Returns the curve's value of `table` at `cycle`, `step` more per cycle past it up to `most`, with
// `strength` times what it adds past 1.0.
float curve(const float* table, float step, float most, int cycle, float strength) {
    const int at = cycle < 0 ? 0 : cycle;
    const float last = table[kCurveCycles - 1];

    // Past the table each cycle adds its step to the last value, up to the most.
    const float past = last + step * static_cast<float>(at - (kCurveCycles - 1));
    const float k = at < kCurveCycles ? table[at] : (past < most ? past : most);
    const float scale = strength < 0.0f ? 0.0f : strength;

    return 1.0f + (k - 1.0f) * scale;
}

}  // namespace

Kind kind_of(int item) {
    // By item number.
    switch (item) {
        case 22:  // Chopper
        case 23:  // Pulse Rifle
        case 24:  // Seeker Gun
        case 25:  // Hoverbomb Gun
        case 26:  // Blitz Gun
        case 27:  // Minirocket Tube
        case 28:  // Plasma Coil
        case 29:  // Lava Gun
        case 30:  // Lancer
        case 31:  // Synthenoid
        case 32:  // Spiderbot Glove
        case 37:  // Bouncer
        case 41:  // Miniturret Glove
        case 42:  // Gravity Bomb
        case 45:  // Shield Charger
            return Kind::Four;
        case 12:  // Bomb Glove
        case 14:  // Visibomb Gun
        case 18:  // Tesla Claw
        case 53:  // Walloper
            return Kind::Two;
        // The Decoy Glove: the game credits a kill's experience to the weapon whose hit killed
        // (seen in game code: SetDeathBits, func_002A0430), and the decoy deals none (decoy_update,
        // func_L00_00398150: it only takes hits).
        case 17:
            return Kind::NoExp;
        case 9:  // Clank Zapper
            return Kind::Zapper;
        case 16:  // Sheepinator
            return Kind::Sheep;
        case 43:  // Zodiac
            return Kind::Single;
        case 44:  // RYNO II
            return Kind::Ryno;
        // Gadgets, the wrench and every other item: left to the game.
        default:
            return Kind::None;
    }
}

int step_count(Kind kind) {
    // An item the mod leaves alone has none.
    if (kind == Kind::None) {
        return 0;
    }

    return kind == Kind::Ryno ? kRynoStepCount : kTiers;
}

const Step* step_at(Kind kind, int level) {
    // Before the first or past the last.
    if (level < 0 || level >= step_count(kind)) {
        return nullptr;
    }

    // The RYNO II's own path: Heavy, the tiers, Ultima.
    if (kind == Kind::Ryno) {
        return &kRynoSteps[level];
    }

    if (kind == Kind::Two || kind == Kind::Single) {
        return &kMasterySteps[level];
    }

    return kind == Kind::NoExp ? &kBoughtSteps[level] : &kTierSteps[level];
}

int mega_version(Kind kind) {
    // By how the weapon reaches its Mega.
    switch (kind) {
        case Kind::Four:
            return 2;
        case Kind::Two:
        case Kind::NoExp:
            return 1;
        // Its one version, bought.
        case Kind::Single:
        case Kind::Ryno:
            return 0;
        // None: the Zapper's and the Sheepinator's versions are never sold.
        case Kind::Zapper:
        case Kind::Sheep:
        case Kind::None:
            return -1;
    }

    return -1;
}

int name_version(Kind kind) {
    const int mega = mega_version(kind);

    // Nothing to name.
    if (kind == Kind::None) {
        return -1;
    }

    // The version before the game's bought one. With one version, that one.
    if (mega >= 0) {
        return mega > 0 ? mega - 1 : 0;
    }

    // None bought (the Zapper, the Sheepinator): the second, the Zapper's last and the
    // Sheepinator's last by experience (challenge mode gives the rest).
    return 1;
}

std::int32_t price(Kind kind, int level, std::int32_t mega) {
    const Step* step = step_at(kind, level);

    // Steps by experience are never sold.
    if (step == nullptr || step->how != How::Buy) {
        return 0;
    }

    const std::int32_t floored = mega < kPriceFloor ? kPriceFloor : mega;
    const std::int32_t base = mega <= 0 ? kPriceLadder : floored;
    std::int32_t last = mega > 0 ? mega : base;
    // Alpha came by experience: the first bought step is the ladder's second.
    int bought = kind == Kind::Two || kind == Kind::Single ? 1 : 0;

    // Each bought step up to this one, so each is priced above the one before.
    for (int n = 0; n <= level; ++n) {
        const Step* earlier = step_at(kind, n);

        // Only the bought steps climb the ladder.
        if (earlier->how != How::Buy) {
            continue;
        }

        const float times = kind == Kind::NoExp ? kPriceEvery[n]
                            : kind == Kind::Ryno ? kRynoPriceTimes[bought]
                                                 : kPriceTimes[bought];
        ++bought;
        std::int32_t next = round_price(static_cast<double>(base) * times);

        // Each above the one before and above the Mega.
        if (next <= last) {
            const std::int32_t risen = round_price(static_cast<double>(last) * kPriceRise);
            next = risen > last ? risen : last + kPriceRiseLeast;
        }

        last = next;
    }

    return last;
}

bool can_buy(Kind kind, int level, bool at_last, std::int32_t exp) {
    const Step* step = step_at(kind, level);

    // Only a bought step, and only at the last real version.
    if (!at_last || step == nullptr || step->how != How::Buy) {
        return false;
    }

    return !previous_bought(kind, level) || exp >= kMasteryExp * exp_scale(kind);
}

bool exp_reached(Kind kind, int level, bool at_last, std::int32_t exp) {
    const Step* step = step_at(kind, level);
    return at_last && step != nullptr && step->how == How::Exp
           && exp >= step->exp * exp_scale(kind);
}

std::int32_t exp_goal(Kind kind, int level) {
    const Step* step = step_at(kind, level);

    // No step left: the game's full bar.
    if (step == nullptr) {
        return 0;
    }

    // A step by experience fills toward its own.
    if (step->how == How::Exp) {
        return step->exp * exp_scale(kind);
    }

    return previous_bought(kind, level) ? kMasteryExp * exp_scale(kind) : 0;
}

float damage(Kind kind, int level) {
    const Step* step = step_at(kind, level - 1);

    // Level 0 or an item the mod leaves alone: the game's own.
    if (step == nullptr) {
        return 1.0f;
    }

    return step->damage;
}

float ammo(Kind kind, int level) {
    const Step* step = step_at(kind, level - 1);

    // Level 0 or an item the mod leaves alone: the game's own.
    if (step == nullptr) {
        return 1.0f;
    }

    return step->ammo;
}

std::int32_t ammo_max(Kind kind, int level, std::int32_t base) {
    // The HUD's ammo count has room for three digits: at 1,000 its draw ran past the scratchpad
    // (a crash at 0x70009F00 with the Omega Heavy Lancer, 500 x 2). A game's own count past it
    // stays as it is.
    constexpr std::int32_t kMostAmmo = 999;

    // No capacity is endless ammo, and stays so.
    if (base <= 0) {
        return base;
    }

    // The Zodiac's capacity is small, so a multiplier barely moves it: each tier adds a flat
    // kZodiacAmmoPerTier rounds to the game's (Alpha base + 4, Beta base + 8, ... Omega base + 32).
    if (kind == Kind::Single) {
        const int tier = level < 0 ? 0 : level;
        return base + kZodiacAmmoPerTier * tier;
    }

    const float exact = static_cast<float>(base) * ammo(kind, level);
    const auto most = static_cast<std::int32_t>(std::floor(exact + 0.5f));
    const std::int32_t cap = base > kMostAmmo ? base : kMostAmmo;
    return most > cap ? cap : most;
}

std::optional<std::uint32_t> tint_rgb(Kind kind, int level, std::uint32_t tick) {
    const Step* step = step_at(kind, level - 1);

    // Level 0: the game's own colour.
    if (step == nullptr) {
        return std::nullopt;
    }

    // Each tint's colour, the animated ones by the tick.
    switch (step->tint) {
        // Each steady tint in turn, blended from one to the next.
        case Tint::Prismatic: {
            const std::uint32_t at = (tick / kPrismTicks) % kTintCount;
            const std::uint32_t t = (tick % kPrismTicks) * kMixSteps / kPrismTicks;
            return mix(kTintRgb[at], kTintRgb[(at + 1u) % kTintCount], t);
        }
        case Tint::Orange:
            return kOrangeRgb;
        // Red that sinks to near black and back.
        case Tint::Ultima: {
            const std::uint32_t phase = tick % kUltimaTicks;
            const std::uint32_t half = kUltimaTicks / 2u;
            const std::uint32_t rise = phase < half ? phase : kUltimaTicks - phase;
            return mix(kUltimaRed, kUltimaBlack, rise * kMixSteps / half);
        }
        case Tint::Lime:
        case Tint::Cyan:
        case Tint::Emerald:
        case Tint::Teal:
        case Tint::Violet:
        case Tint::Magenta:
        case Tint::Crimson:
            return kTintRgb[static_cast<std::size_t>(step->tint)];
    }

    return std::nullopt;
}

std::string display_name(std::string_view base, Kind kind, int level) {
    const Step* step = step_at(kind, level - 1);

    // No tier: the game's name as it is.
    if (step == nullptr) {
        return std::string(base);
    }

    // The tier in place of the game's version: "Alpha Lancer", "Heavy RYNO II", "Ultima RYNO II".
    std::string name = step->name;
    name += ' ';
    name += base;
    return name;
}

std::optional<std::uint32_t> effect_rgb(Kind kind, int level, std::uint32_t tick) {
    const Step* step = step_at(kind, level - 1);

    // Level 0: the game's own effects.
    if (step == nullptr) {
        return std::nullopt;
    }

    // Ultima: bright red beating to deep red, on the beat of its icon's red and black.
    if (step->tint == Tint::Ultima) {
        const std::uint32_t phase = tick % kUltimaTicks;
        const std::uint32_t half = kUltimaTicks / 2u;
        const std::uint32_t rise = phase < half ? phase : kUltimaTicks - phase;
        const std::uint32_t bgr = mix(kEffectUltimaBright, kEffectUltimaDeep, rise * kMixSteps / half);
        return (bgr & 0xFFu) << 16 | (bgr & 0xFF00u) | (bgr >> 16 & 0xFFu);
    }

    const std::optional<std::uint32_t> tint = tint_rgb(kind, level, tick);

    // Every other tint has a colour; none would be a step without one.
    if (!tint) {
        return std::nullopt;
    }

    return effect_of_tint(*tint);
}

std::uint32_t effect_of_tint(std::uint32_t bgr) {
    // 0x00BBGGRR to 0xRRGGBB, its brightest channel raised to 0xFF and the others with it.
    const std::uint32_t red = bgr & 0xFFu;
    const std::uint32_t green = bgr >> 8 & 0xFFu;
    const std::uint32_t blue = bgr >> 16 & 0xFFu;
    const std::uint32_t top = std::max({red, green, blue, 1u});
    const auto lift = [top](std::uint32_t channel) { return (channel * 0xFFu + top / 2u) / top; };
    return lift(red) << 16 | lift(green) << 8 | lift(blue);
}

int effect_strength(Kind kind, int level) {
    const int steps = step_count(kind);

    // No step, or none taken yet.
    if (steps == 0 || level <= 0) {
        return 0;
    }

    // From the first's strength, each step a share of the rest, the last at the top.
    const int clamped = level < steps ? level : steps;
    const int span = kEffectTop - kEffectFirst;
    return kEffectFirst + ((clamped - 1) * span + (steps - 1) / 2) / (steps - 1);
}

bool tint_moves(Kind kind, int level) {
    const Step* step = step_at(kind, level - 1);
    return step != nullptr
           && (step->tint == Tint::Prismatic || step->tint == Tint::Ultima);
}

float enemy_health(int cycle, float strength) {
    return curve(kEnemyHealth, kHealthStep, kHealthMost, cycle, strength);
}

float enemy_damage(int cycle, float strength) {
    return curve(kEnemyDamage, kDamageStep, kDamageMost, cycle, strength);
}

float boss_health(int cycle, float strength) {
    return curve(kBossHealth, kBossStep, kBossMost, cycle, strength);
}

const BossClass* boss_of(int moby_class) {
    // The fights' own classes.
    for (const BossClass& boss : kBossClasses) {
        // Found.
        if (boss.moby_class == moby_class) {
            return &boss;
        }
    }

    return nullptr;
}

float ryno_hits(int cycle, float strength, const Foe& foe) {
    // The runs the game balances itself.
    if (cycle < kFirstCurveCycle) {
        return 0.0f;
    }

    const float scale = strength < 0.0f ? 0.0f : strength;
    const auto runs = static_cast<float>(cycle - kFirstCurveCycle);

    // A boss: many hits, the final boss more.
    if (foe.boss) {
        const float hits = std::min(kRynoBossFirst + kRynoBossStep * runs, kRynoBossMost);
        return hits * (foe.last ? kFinalBoss : 1.0f) * scale;
    }

    // A small or medium enemy: as many hits as its level makes.
    if (foe.bolts < kRynoToughBolts) {
        return 0.0f;
    }

    const float hits = std::min(kRynoToughFirst + kRynoToughStep * runs, kRynoToughMost);
    const float size =
        std::sqrt(static_cast<float>(foe.bolts) / static_cast<float>(kRynoToughBolts));
    return hits * std::min(size, kRynoSizeMost) * scale;
}

float hit_divisor(Kind kind, int level, int cycle, float strength, const Foe& foe) {
    const float boss = foe.boss ? boss_health(cycle, strength) : 1.0f;

    // The final boss: kFinalBoss more, wherever a boss has more.
    const float extra = foe.last && boss > 1.0f ? boss * kFinalBoss : boss;

    // The RYNO II: none of the curve; on a tough enemy or a boss, the game's own hits times
    // ryno_hits, whatever its tier.
    if (kind == Kind::Ryno) {
        const float hits = ryno_hits(cycle, strength, foe) * damage(kind, level);
        return hits > 1.0f ? hits : 1.0f;
    }

    return enemy_health(cycle, strength) * extra;
}

float kill_experience(int cycle, const Foe& foe) {
    // The runs the game balances itself.
    if (cycle < kFirstCurveCycle) {
        return 1.0f;
    }

    constexpr int kRuns = static_cast<int>(sizeof kExperienceRun / sizeof kExperienceRun[0]);
    const int at = cycle - kFirstCurveCycle;
    const float run = kExperienceRun[at < kRuns ? at : kRuns - 1];

    // A boss: a large extra, the final boss's larger.
    if (foe.boss) {
        return run * (foe.last ? kFinalBossExperience : kBossExperience);
    }

    // Softened toughness: the fourth root of its bolts over the least.
    const float bolts = static_cast<float>(foe.bolts);
    const float tough =
        bolts > kExperienceToughBolts ? std::sqrt(std::sqrt(bolts / kExperienceToughBolts)) : 1.0f;
    return run * std::min(tough, kExperienceToughMost);
}

}  // namespace ng_plus_plus
