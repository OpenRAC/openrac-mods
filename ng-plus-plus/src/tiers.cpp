// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++'s rules (tiers.h). The tables below are the mod's balance: change them here.

#include "tiers.h"

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

// The RYNO II's own steps before the tiers, as many as kRynoSteps says.
constexpr int kRynoSteps = 2;

// clang-format off: one step per row, as kTierSteps.
// The RYNO II's two versions of its own, before the tiers.
constexpr Step kRynoOwnSteps[kRynoSteps] = {
    {"Heavy",  How::Exp, 6'000, 1.40f, 1.50f, Tint::Orange},
    {"Ultima", How::Buy, 0,     2.20f, 2.00f, Tint::Ultima},
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

// Ticks a prismatic tint takes from one colour to the next.
constexpr std::uint32_t kPrismTicks = 45u;

// Ticks of one beat of Ultima's black and red.
constexpr std::uint32_t kUltimaTicks = 60u;

// The steps of a mix of two colours: 256 is all of the second.
constexpr std::uint32_t kMixSteps = 256u;

// Times the base, for the first bought step, the second, ... (the RYNO II has five: Ultima first).
constexpr float kPriceTimes[] = {1.5f, 2.5f, 4.0f, 6.0f, 9.0f};

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

// By cycle from 2 on, growing half as fast again as 0.3's curve (x2, x3, x4, x5): the health stays
// just behind the damage of the tier a player can have by then (cycle 2 Delta's x3 over x2.5, 3
// Zeta's x4.5 over x4, 4 Omega's x7 over x5.5), so a weapon at that tier still kills faster than V4
// did at cycle 1, by an eighth to a quarter; past Omega the health closes in, half a step a cycle,
// and stops at kHealthMost, still behind Omega's x7. The damage they deal rises more gently: a
// fight lasting longer already costs more health.
constexpr float kEnemyHealth[] = {1.0f, 1.0f, 2.5f, 4.0f, 5.5f, 6.0f, 6.5f};
constexpr float kEnemyDamage[] = {1.0f, 1.0f, 1.375f, 1.75f, 2.125f, 2.5f, 2.875f};

// Past the tables, each cycle adds this much health and damage, up to the most.
constexpr float kHealthStep = 0.25f;
constexpr float kDamageStep = 0.375f;
constexpr float kHealthMost = 6.75f;  // under Omega's x7: a full weapon stays ahead
constexpr float kDamageMost = 4.0f;

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

// Returns Ultima's `ultima` with a tier's gain `tier` added: the RYNO II's tiers build on Ultima.
float on_ultima(float ultima, float tier) {
    return ultima + (tier - 1.0f);
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

    return kind == Kind::Ryno ? kTiers + kRynoSteps : kTiers;
}

const Step* step_at(Kind kind, int level) {
    // Before the first or past the last.
    if (level < 0 || level >= step_count(kind)) {
        return nullptr;
    }

    // The RYNO II's own two come first, then the tiers.
    if (kind == Kind::Ryno) {
        return level < kRynoSteps ? &kRynoOwnSteps[level] : &kTierSteps[level - kRynoSteps];
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
    int bought = 0;

    // Each bought step up to this one, so each is priced above the one before.
    for (int n = 0; n <= level; ++n) {
        const Step* earlier = step_at(kind, n);

        // Only the bought steps climb the ladder.
        if (earlier->how != How::Buy) {
            continue;
        }

        const float times = kind == Kind::NoExp ? kPriceEvery[n] : kPriceTimes[bought];
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

    // The RYNO II's tiers add their gain to Ultima's.
    const bool past_ultima = kind == Kind::Ryno && level > kRynoSteps;
    return past_ultima ? on_ultima(kRynoOwnSteps[1].damage, step->damage) : step->damage;
}

float ammo(Kind kind, int level) {
    const Step* step = step_at(kind, level - 1);

    // Level 0 or an item the mod leaves alone: the game's own.
    if (step == nullptr) {
        return 1.0f;
    }

    // The RYNO II's tiers add their gain to Ultima's.
    const bool past_ultima = kind == Kind::Ryno && level > kRynoSteps;
    return past_ultima ? on_ultima(kRynoOwnSteps[1].ammo, step->ammo) : step->ammo;
}

std::int32_t ammo_max(Kind kind, int level, std::int32_t base) {
    // The game's most ammo is a u16.
    constexpr std::int32_t kMostAmmo = 0xFFFF;

    // No capacity is endless ammo, and stays so.
    if (base <= 0) {
        return base;
    }

    const float exact = static_cast<float>(base) * ammo(kind, level);
    const auto most = static_cast<std::int32_t>(std::floor(exact + 0.5f));
    return most > kMostAmmo ? kMostAmmo : most;
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

float enemy_health(int cycle, float strength) {
    return curve(kEnemyHealth, kHealthStep, kHealthMost, cycle, strength);
}

float enemy_damage(int cycle, float strength) {
    return curve(kEnemyDamage, kDamageStep, kDamageMost, cycle, strength);
}

}  // namespace ng_plus_plus
