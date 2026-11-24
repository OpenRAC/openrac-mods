// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++'s perk, armour and elite rules (perk_rules.h). The tables below are the mod's balance:
// change them here and in README.md's tables.

#include "perk_rules.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ng_plus_plus {
namespace {

// The RYNO II's own steps before its tiers (Heavy, Mega, Ultra, 0.6.18).
constexpr int kRynoOwn = 3;

// The tiers each stage starts at.
constexpr int kGammaTier = 3;
constexpr int kEpsilonTier = 5;
constexpr int kOmegaTier = 8;

// The RYNO II's Ultima, after Omega.
constexpr int kUltimaTier = 9;

// The RYNO II's splits by tier (0 none, 1 Alpha ... 8 Omega, 9 Ultima): the chance in 100 that a
// rocket the hero fired splits where it ends, into how many homing rockets, how many times those
// split again, and the most made from one rocket in all.
struct RynoSplit {
    int chance;
    int shots;
    int generations;
    int most;
};

// clang-format off: one tier per row.
constexpr RynoSplit kRynoSplits[kUltimaTier + 1] = {
    {0,   0, 0, 0},   // none
    {25,  1, 0, 1},   // Alpha
    {35,  1, 0, 1},   // Beta
    {50,  2, 0, 2},   // Gamma
    {65,  2, 0, 2},   // Delta
    {80,  3, 0, 3},   // Epsilon
    {100, 3, 0, 3},   // Zeta
    {100, 3, 0, 3},   // Eta
    {100, 4, 1, 12},  // Omega: 4, each splitting in 2
    {100, 4, 2, 16},  // Ultima: 4, each splitting in 2 twice, at most 16
};
// clang-format on

// A split rocket splitting again makes this many.
constexpr int kRynoSplitAgain = 2;

// Returns `rgb` (0xRRGGBB) as 0x00BBGGRR, or back.
constexpr std::uint32_t swap_rb(std::uint32_t rgb) {
    return (rgb & 0xFFu) << 16 | (rgb & 0xFF00u) | (rgb >> 16 & 0xFFu);
}

// Returns `a` and `b` mixed, `t` of 256 toward `b`, per 8-bit channel.
std::uint32_t mix(std::uint32_t a, std::uint32_t b, std::uint32_t t) {
    std::uint32_t out = 0;

    // The three channels, eight bits apart.
    for (std::uint32_t shift = 0; shift < 24u; shift += 8u) {
        const auto from = static_cast<std::int32_t>((a >> shift) & 0xFFu);
        const auto to = static_cast<std::int32_t>((b >> shift) & 0xFFu);
        const std::int32_t step = ((to - from) * static_cast<std::int32_t>(t)) / 256;
        out |= (static_cast<std::uint32_t>(from + step) & 0xFFu) << shift;
    }

    return out;
}

// Returns the colour of `cycle` (count colours) at `tick`, `ticks` from one to the next.
std::uint32_t cycle_rgb(
    const std::uint32_t* colours, std::uint32_t count, std::uint32_t ticks, std::uint32_t tick
) {
    const std::uint32_t at = (tick / ticks) % count;
    const std::uint32_t t = (tick % ticks) * 256u / ticks;
    return mix(colours[at], colours[(at + 1u) % count], t);
}

// The Ice Gun's colours, 0xRRGGBB, Alpha to Eta; Omega cycles the aurora.
constexpr std::uint32_t kIceRgb[7] = {
    0xE0F4FFu,  // frost
    0xA8E4FFu,  // glacier
    0x70D0FFu,  // ice
    0x40B8F0u,  // arctic
    0x2090FFu,  // deep ice
    0x30FFF0u,  // polar
    0x2050FFu,  // cobalt
};
constexpr std::uint32_t kAuroraRgb[] = {0x30FFE0u, 0x60C8FFu, 0xFFFFFFu, 0x4060FFu, 0x80FFB0u};

// The fire path's colours, 0xRRGGBB, Alpha to Eta; Omega cycles the supernova.
constexpr std::uint32_t kFireRgb[7] = {
    0xFF7A30u,  // ember
    0xFF9418u,  // flame
    0xFF5410u,  // magma
    0xFFB020u,  // blaze
    0xFF2E08u,  // inferno
    0xE01040u,  // scorch
    0xFFD040u,  // solar
};
constexpr std::uint32_t kSupernovaRgb[] = {0xFF2000u, 0xFF8000u, 0xFFE040u, 0xFFFFF0u, 0xFF3070u};

// The path the Lava Gun's tiers take (set_lava_path).
LavaPath g_lava_path = LavaPath::Ice;

// The armour's glows, 0xRRGGBB, Alpha to Eta; Omega cycles them.
constexpr std::uint32_t kArmourRgb[7] = {
    0xB8C8E0u,  // steel
    0x4090FFu,  // azure
    0x30E090u,  // jade
    0xFFC830u,  // gold
    0xA050FFu,  // amethyst
    0xFF4890u,  // rose
    0xFF3010u,  // ember
};

// The accents of those colours (the visor and the lamps), 0xRRGGBB, bright and each against its
// body's: Alpha keeps Carbonox's own red. Each channel is one the port keeps whole (it keeps the
// top 3 bits: 0x00, 0x20 ... 0xE0, 0xFF).
constexpr std::uint32_t kArmourAccent[7] = {
    0xFF2020u,  // red against steel
    0xFFA020u,  // orange against azure
    0xFF40C0u,  // magenta against jade
    0x20C0FFu,  // sky against gold
    0xC0FF20u,  // lime against amethyst
    0x20FFE0u,  // aqua against rose
    0xFFE040u,  // yellow against ember
};

// Ticks a cycling colour takes from one colour to the next.
constexpr std::uint32_t kCycleTicks = 45u;

// The tiers' names, Alpha to Omega.
constexpr const char* kTierNames[kArmourTiers] = {
    "Alpha",
    "Beta",
    "Gamma",
    "Delta",
    "Epsilon",
    "Zeta",
    "Eta",
    "Omega",
};

// The damage the hero takes by armour tier, Alpha to Omega.
constexpr float kArmourDamage[kArmourTiers] = {
    0.90f,
    0.82f,
    0.75f,
    0.68f,
    0.62f,
    0.56f,
    0.50f,
    0.45f,
};

// The first cycle with a modifier, and with elites.
constexpr int kModifierCycle = 2;
constexpr int kEliteCycle = 3;

// Elites: the share at the first cycle, what each run adds, the most; the Elite guard's most.
constexpr int kEliteFirst = 15;
constexpr int kEliteStep = 5;
constexpr int kEliteMost = 35;
constexpr int kEliteGuardMost = 70;

}  // namespace

int tier_of(Kind kind, int level) {
    // No step taken, or an item the mod leaves alone.
    if (kind == Kind::None || level <= 0) {
        return 0;
    }

    // The RYNO II's Heavy comes first: no tier. Its Ultima, after Omega, is the 9th. The Zodiac's
    // Heavy, Mega and Ultra come first: no tier.
    const int own = kind == Kind::Ryno ? kRynoOwn : (kind == Kind::Single ? kZodiacOwn : 0);
    const int tier = level - own;
    return std::clamp(tier, 0, kOmegaTier + 1);
}

Stage stage_of_tier(int tier) {
    // From the stage's first tier up.
    if (tier >= kOmegaTier) {
        return Stage::Omega;
    }

    if (tier >= kEpsilonTier) {
        return Stage::Epsilon;
    }

    return tier >= kGammaTier ? Stage::Gamma : Stage::None;
}

Stage raised(Stage stage) {
    // Omega is the top.
    switch (stage) {
        case Stage::None:
            return Stage::Gamma;
        case Stage::Gamma:
            return Stage::Epsilon;
        case Stage::Epsilon:
        case Stage::Omega:
            return Stage::Omega;
    }

    return Stage::Omega;
}

HitPerk hit_perk(int item, Stage stage) {
    HitPerk perk;
    const int s = static_cast<int>(stage);

    // No stage: the tier's damage only.
    if (s == 0) {
        return perk;
    }

    // By weapon (the survey's rows: plans/ngpp/weapon-survey.md). Each case reads Gamma's, then
    // what Epsilon and Omega add.
    switch (item) {
        // Lancer: each shot ricochets to the nearest other enemy, then twice; at Omega every fifth
        // shot is a piercing round that ricochets four times.
        case 30:
            perk.hops = s >= 2 ? 2 : 1;
            perk.hop_radius = 8.0f;
            perk.hop_share = 0.5f;
            perk.hop_decay = 0.85f;
            if (s >= 3) {
                perk.nth = 5;
                perk.nth_hops = 4;
                perk.nth_share = 1.0f;
                perk.pierce = 2;
                perk.pierce_range = 10.0f;
                perk.pierce_share = 1.0f;
            }
            break;

        // Plasma Coil: the arc chains to 1, 3, then 6 more enemies, and the last jump explodes.
        case 28:
            perk.hops = s == 1 ? 1 : (s == 2 ? 3 : 6);
            perk.hop_radius = 9.0f;
            perk.hop_share = 0.6f;
            perk.hop_decay = 0.9f;
            if (s >= 2) {
                perk.status = Status::SlowHalf;
                perk.status_ticks = s >= 3 ? 150 : 90;
            }
            if (s >= 3) {
                perk.end_radius = 5.0f;
                perk.end_share = 0.8f;
            }
            break;

        // Tesla Claw: the beam arcs to 2, then 5 more (its Omega storm is perks.cpp's, by its
        // beam's moby).
        case 18:
            perk.hops = s == 1 ? 2 : 5;
            perk.hop_radius = 7.0f;
            perk.hop_share = 1.5f;  // A beam's tick is small: an arc is several ticks' worth.
            perk.hop_decay = 0.85f;
            perk.cooldown = 8;
            if (s >= 2) {
                perk.status = Status::Freeze;
                perk.status_ticks = s >= 3 ? 45 : 30;
            }
            break;

        // Clank Zapper: a wider zap, then two targets, then chain lightning.
        case 9:
            perk.burst_radius = 4.0f;
            perk.burst_share = 0.5f;
            if (s >= 2) {
                perk.fan = 1;
                perk.fan_radius = 8.0f;
                perk.fan_share = 0.8f;
            }
            if (s >= 3) {
                perk.hops = 5;
                perk.hop_radius = 8.0f;
                perk.hop_share = 0.7f;
                perk.hop_decay = 0.9f;
            }
            perk.cooldown = 4;
            break;

        // Walloper: a shockwave on each swing that lands, wider at Epsilon, chaining at Omega.
        case 53:
            perk.burst_radius = s >= 2 ? 6.0f : 4.0f;
            perk.burst_share = s >= 2 ? 0.8f : 0.5f;
            if (s >= 3) {
                perk.hops = 3;
                perk.hop_radius = 8.0f;
                perk.hop_share = 0.7f;
            }
            if (s >= 2) {
                perk.status = Status::Freeze;  // Staggered by the wave.
                perk.status_ticks = 20;
            }
            perk.cooldown = 10;
            break;

        // Blitz Gun: more pellets need the fire hook point; at Omega each pellet that hits leaves a
        // damaging spark for a second.
        case 26:
            if (s >= 3) {
                perk.field_ticks = 60;
                perk.field_radius = 2.5f;
                perk.field_share = 0.25f;  // Every pellet: its hits land in one tick.
            }
            break;

        // Pulse Rifle: every 4th pulse is an explosive one from Epsilon; at Omega pulses pierce and
        // chain. (Gamma's fire rate needs the weapon stat hook point.)
        case 23:
            if (s >= 2) {
                perk.nth = 4;
                perk.burst_on_nth = true;
                perk.burst_radius = 4.0f;
                perk.burst_share = 0.6f;
            }
            if (s >= 3) {
                perk.pierce = 2;
                perk.pierce_range = 12.0f;
                perk.pierce_share = 0.8f;
                perk.hops = 2;
                perk.hop_radius = 7.0f;
                perk.hop_share = 0.5f;
            }
            break;

        // Bomb Glove: bombs chain-blast from Epsilon, burst into four at Omega (more bombs per
        // throw need the fire hook point).
        case 12:
            if (s >= 2) {
                perk.burst_radius = 5.0f;
                perk.burst_share = 0.5f;
            }
            if (s >= 3) {
                perk.fan = 4;
                perk.fan_radius = 8.0f;
                perk.fan_share = 0.4f;
            }
            perk.cooldown = 4;
            break;

        // Bouncer: at Omega each blast drops a mini blast around it (more bomblets and bounces need
        // the fire and stat hook points).
        case 37:
            if (s >= 3) {
                perk.burst_radius = 4.0f;
                perk.burst_share = 0.4f;
                perk.cooldown = 4;
            }
            break;

        // Zodiac: at Omega its shot leaves a damaging field.
        case 43:
            if (s >= 3) {
                perk.field_ticks = 180;
                perk.field_radius = 6.0f;
                perk.field_share = 0.15f;
                perk.cooldown = 30;
            }
            break;

        // Gravity Bomb: the pull's wider reach as a wider blast from Gamma, its mini bombs as
        // bursts around the blast from Epsilon (perks.cpp: by its blast's moby); nothing per hit.
        // Every other weapon: its perks need a newer OpenRAC (the fire and spawn hook points), or
        // are perks.cpp's by moby (the Shield Charger, the Decoy Glove, the Sheepinator, the
        // Visibomb Gun), or the Ice Gun's (the Lava Gun).
        default:
            break;
    }

    return perk;
}

HitPerk ryno_perk(int tier) {
    HitPerk perk;
    const Stage stage = stage_of_tier(tier);

    // Below Gamma: the tier's damage only.
    if (stage == Stage::None) {
        return perk;
    }

    // Each rocket splits on impact into 2, then 3 homing rockets, which split once more at Omega,
    // twice at Ultima.
    perk.fan = stage == Stage::Gamma ? 2 : 3;
    perk.fan_again = stage == Stage::Omega ? (tier > kOmegaTier ? 2 : 1) : 0;
    perk.fan_radius = 15.0f;
    perk.fan_share = tier > kOmegaTier ? 0.6f : 0.45f;
    perk.cooldown = 6;
    return perk;
}

ShotPerk shot_perk(int item, Stage stage, int tier) {
    ShotPerk perk;
    const int s = static_cast<int>(stage);

    // By weapon; Gamma's first, then what Epsilon and Omega add.
    switch (item) {
        // Minirocket Tube: more rockets, rockets splitting in flight, a cluster blast each.
        case 27:
            if (s >= 1) {
                perk.refires = s >= 3 ? 2 : 1;
                perk.spread = 0.18f;
            }
            if (s >= 2) {
                perk.split_after = 30;
            }
            if (s >= 3) {
                perk.end_blasts = 2;
            }
            break;

        // Seeker Gun: two more seekers, re-seeking after a hit, four more and seeking twice at
        // Omega.
        case 24:
            if (s >= 1) {
                perk.refires = s >= 3 ? 4 : 2;
                perk.spread = 0.2f;
            }
            if (s >= 2) {
                perk.end_shots = 1;
                perk.end_class = kSeeker;
                perk.end_homing = true;
                perk.end_generations = s >= 3 ? 2 : 0;
                perk.end_most = 6;
            }
            break;

        // Hoverbomb Gun: one more, then four in all at Omega.
        case 25:
            if (s >= 1) {
                perk.refires = s >= 3 ? 3 : 1;
                perk.spread = 0.3f;
            }
            break;

        // Bomb Glove: one more bomb, bursting into four mini bombs at Omega.
        case 12:
            if (s >= 1) {
                perk.refires = 1;
                perk.spread = 0.2f;
            }
            if (s >= 3) {
                perk.end_shots = 4;
                perk.end_class = kMiniBomb;
                perk.end_most = 4;
            }
            break;

        // Gravity Bomb: mini bombs scatter from its blast from Epsilon, eight and a second bomb at
        // Omega, which also throws two.
        case 42:
            if (s >= 2) {
                perk.end_shots = s >= 3 ? 8 : 4;
                perk.end_class = kMiniBomb;
                perk.end_most = perk.end_shots;
            }
            if (s >= 3) {
                perk.refires = 1;
                perk.spread = 0.25f;
                perk.end_again_ticks = 60;
            }
            break;

        // Bouncer: two more bomblets, three at Epsilon.
        case 37:
            if (s >= 1) {
                perk.refires = s >= 2 ? 3 : 2;
                perk.spread = 0.25f;
            }
            break;

        // Decoy Glove: two decoys from Epsilon, three at Omega.
        case 17:
            if (s >= 2) {
                perk.refires = s >= 3 ? 2 : 1;
                perk.spread = 0.5f;
            }
            break;

        // Miniturret Glove: one more turret, three a throw at Omega.
        case 41:
            if (s >= 1) {
                perk.refires = s >= 3 ? 2 : 1;
                perk.spread = 0.6f;
            }
            break;

        // RYNO II: where a rocket ends it may split into homing rockets, by chance at the low
        // tiers and always from Zeta, more of them up the tiers (kRynoSplits); at Omega each
        // split splits once more, at Ultima twice. The stage counts too, so an overdrive raises it.
        case 44: {
            const int stage_tier =
                s >= 3 ? kOmegaTier : (s >= 2 ? kEpsilonTier : (s >= 1 ? kGammaTier : 0));
            const int t = std::min(std::max(tier, stage_tier), kUltimaTier);
            if (t >= 1) {
                const RynoSplit& split = kRynoSplits[t];
                perk.end_shots = split.shots;
                perk.end_chance = split.chance;
                perk.end_again_shots = kRynoSplitAgain;
                perk.end_class = kRynoRocket;
                perk.end_homing = true;
                perk.end_generations = split.generations;
                perk.end_most = split.most;
            }
            break;
        }

        // Every other weapon has no shot moby of its own.
        default:
            break;
    }

    return perk;
}

bool split_roll(std::uint32_t serial, int chance) {
    if (chance >= 100) {
        return true;
    }
    if (chance <= 0) {
        return false;
    }

    // A fixed mix of the serial's bits (splitmix32's finish), so neighbouring serials roll apart.
    std::uint32_t x = serial + 0x9E3779B9u;
    x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
    x = (x ^ (x >> 13)) * 0xC2B2AE35u;
    x ^= x >> 16;
    return static_cast<int>(x % 100u) < chance;
}

float refire_yaw(int n, float spread) {
    const int step = n / 2 + 1;
    return static_cast<float>(n % 2 == 0 ? step : -step) * spread;
}

float fire_rate(int item, Stage stage) {
    // The Pulse Rifle only: a quarter faster at Gamma, a third at Epsilon, half at Omega.
    if (item != 23) {
        return 1.0f;
    }

    switch (stage) {
        case Stage::None:
            return 1.0f;
        case Stage::Gamma:
            return 1.25f;
        case Stage::Epsilon:
            return 1.35f;
        case Stage::Omega:
            return 1.5f;
    }

    return 1.0f;
}

IceEffect ice_effect(int tier) {
    IceEffect ice;

    // No tier: the game's lava.
    if (tier <= 0) {
        return ice;
    }

    const Stage stage = stage_of_tier(tier);
    ice.energy = std::min(kEffectTop, 4 + tier);

    // Alpha and Beta: a light chill.
    ice.chill_ticks = 60;
    ice.chill_every = 2;

    // Gamma: a deeper chill.
    if (stage >= Stage::Gamma) {
        ice.chill_ticks = 120;
        ice.chill_every = 3;
    }

    // Epsilon: frozen solid for a moment, shattering on the kill.
    if (stage >= Stage::Epsilon) {
        ice.freeze_ticks = 60;
        ice.shatter_radius = 5.0f;
        ice.shatter_share = 0.6f;
    }

    // Omega: a long freeze, and the shards freeze what they hit.
    if (stage >= Stage::Omega) {
        ice.chill_ticks = 180;
        ice.freeze_ticks = 120;
        ice.shatter_radius = 8.0f;
        ice.shatter_share = 1.0f;
        ice.shatter_freezes = true;
    }

    return ice;
}

std::optional<std::uint32_t> ice_tint_rgb(int tier, std::uint32_t tick) {
    // No tier: the game's own colour.
    if (tier <= 0) {
        return std::nullopt;
    }

    // Omega: the aurora.
    if (tier >= kOmegaTier) {
        constexpr auto count = static_cast<std::uint32_t>(sizeof kAuroraRgb / sizeof kAuroraRgb[0]);
        return swap_rb(cycle_rgb(kAuroraRgb, count, kCycleTicks, tick));
    }

    return swap_rb(kIceRgb[static_cast<std::size_t>(tier - 1)]);
}

void set_lava_path(LavaPath path) {
    g_lava_path = path;
}

LavaPath lava_path() {
    return g_lava_path;
}

FireEffect fire_effect(int tier) {
    FireEffect fire;

    // No tier: the game's meteors.
    if (tier <= 0) {
        return fire;
    }

    const Stage stage = stage_of_tier(tier);
    fire.energy = std::min(kEffectTop, 4 + tier);

    // Alpha and Beta: a burn, two seconds of a tenth of the hit every quarter second.
    fire.burn_ticks = 120;
    fire.burn_share = 0.1f;

    // Gamma (the Magma Gun): a hotter, longer burn, and a splash of magma that sets fire to the
    // enemies around the impact.
    if (stage >= Stage::Gamma) {
        fire.burn_ticks = 180;
        fire.burn_share = 0.12f;
        fire.splash_radius = 4.0f;
        fire.splash_share = 0.4f;
    }

    // Epsilon (the Inferno Gun): a burning enemy killed erupts, setting its neighbours on fire.
    if (stage >= Stage::Epsilon) {
        fire.burn_share = 0.15f;
        fire.splash_radius = 5.0f;
        fire.splash_share = 0.5f;
        fire.erupt_radius = 6.0f;
        fire.erupt_share = 1.5f;
    }

    // Omega (the Supernova Gun): every fourth meteor calls down a storm of three more around its
    // victim, the eruptions wider and stronger.
    if (stage >= Stage::Omega) {
        fire.burn_ticks = 240;
        fire.burn_share = 0.18f;
        fire.splash_radius = 6.0f;
        fire.splash_share = 0.6f;
        fire.erupt_radius = 8.0f;
        fire.erupt_share = 2.0f;
        fire.storm_every = 4;
        fire.storm_meteors = 3;
        fire.storm_radius = 4.0f;
        fire.storm_share = 0.8f;
    }

    return fire;
}

std::optional<std::uint32_t> fire_tint_rgb(int tier, std::uint32_t tick) {
    // No tier: the game's own colour.
    if (tier <= 0) {
        return std::nullopt;
    }

    // Omega: the supernova.
    if (tier >= kOmegaTier) {
        constexpr auto count =
            static_cast<std::uint32_t>(sizeof kSupernovaRgb / sizeof kSupernovaRgb[0]);
        return swap_rb(cycle_rgb(kSupernovaRgb, count, kCycleTicks, tick));
    }

    return swap_rb(kFireRgb[static_cast<std::size_t>(tier - 1)]);
}

const char* lava_name(int tier) {
    // The ice path: one name for every tier.
    if (ice_gun_on()) {
        return kIceGunName;
    }

    switch (stage_of_tier(tier)) {
        case Stage::None:
            return "Meteor Gun";
        case Stage::Gamma:
            return "Magma Gun";
        case Stage::Epsilon:
            return "Inferno Gun";
        case Stage::Omega:
            return "Supernova Gun";
    }

    return "Meteor Gun";
}

namespace {

// Returns the Lava Gun's tint at tier `tier` on the path in play.
std::optional<std::uint32_t> lava_tint_rgb(int tier, std::uint32_t tick) {
    return ice_gun_on() ? ice_tint_rgb(tier, tick) : fire_tint_rgb(tier, tick);
}

}  // namespace

std::optional<std::uint32_t> weapon_tint_rgb(int item, int level, std::uint32_t tick) {
    const Kind kind = kind_of(item);

    // The Lava Gun's tiers are the Ice Gun's, or the fire path's.
    if (item == kLavaGun) {
        return lava_tint_rgb(tier_of(kind, level), tick);
    }

    return tint_rgb(kind, level, tick);
}

std::optional<std::uint32_t> weapon_effect_rgb(int item, int level, std::uint32_t tick) {
    const Kind kind = kind_of(item);

    // The path's colour, lifted as every tier's.
    if (item == kLavaGun) {
        const std::optional<std::uint32_t> tint = lava_tint_rgb(tier_of(kind, level), tick);
        return tint ? std::optional<std::uint32_t>(effect_of_tint(*tint)) : std::nullopt;
    }

    return effect_rgb(kind, level, tick);
}

bool weapon_tint_moves(int item, int level) {
    const Kind kind = kind_of(item);
    return item == kLavaGun ? tier_of(kind, level) >= kOmegaTier : tint_moves(kind, level);
}

int max_level(int item) {
    return item == kArmourItem ? kArmourTiers : step_count(kind_of(item));
}

float armour_damage(int tier) {
    // No tier: the game's armour alone.
    if (tier <= 0) {
        return 1.0f;
    }

    return kArmourDamage[static_cast<std::size_t>(std::min(tier, kArmourTiers) - 1)];
}

std::int32_t armour_price(int tier) {
    // Past the last, or none.
    if (tier <= 0 || tier > kArmourTiers) {
        return 0;
    }

    // A weapon that buys every tier: the same ladder.
    return price(Kind::NoExp, tier - 1, kArmourBase);
}

std::string armour_name(int tier) {
    // No tier: the game's armour.
    if (tier <= 0 || tier > kArmourTiers) {
        return "Armor";
    }

    return std::string(kTierNames[static_cast<std::size_t>(tier - 1)]) + " Armor";
}

std::optional<std::uint32_t> armour_rgb(int tier, std::uint32_t tick) {
    // No tier: the game's look.
    if (tier <= 0) {
        return std::nullopt;
    }

    // Omega: every colour in turn.
    if (tier >= kArmourTiers) {
        return cycle_rgb(kArmourRgb, 7u, kCycleTicks, tick);
    }

    return kArmourRgb[static_cast<std::size_t>(tier - 1)];
}

std::optional<std::uint32_t> armour_accent(int tier, std::uint32_t tick) {
    // No tier: the game's look.
    if (tier <= 0) {
        return std::nullopt;
    }

    // Omega: every accent in turn, with its body.
    if (tier >= kArmourTiers) {
        return cycle_rgb(kArmourAccent, 7u, kCycleTicks, tick);
    }

    return kArmourAccent[static_cast<std::size_t>(tier - 1)];
}

std::optional<std::uint32_t> armour_tint(int tier, std::uint32_t tick) {
    const std::optional<std::uint32_t> rgb = armour_rgb(tier, tick);
    return rgb ? std::optional<std::uint32_t>(swap_rb(*rgb)) : std::nullopt;
}

int armour_energy(int tier) {
    // No tier: no glow.
    if (tier <= 0) {
        return 0;
    }

    // Faint at Alpha, the top at Omega.
    const int clamped = std::min(tier, kArmourTiers);
    return 1 + (clamped - 1) * (kEffectTop - 1) / (kArmourTiers - 1);
}

int armour_paint_level(int tier) {
    constexpr int kTop = 15;

    // No tier: no paint.
    if (tier <= 0) {
        return 0;
    }

    // Alpha a clean coat, Omega the brightest rim.
    const int clamped = std::min(tier, kArmourTiers);
    return (clamped - 1) * kTop / (kArmourTiers - 1);
}

std::string armour_about(int tier) {
    // No tier: nothing to describe.
    if (tier <= 0 || tier > kArmourTiers) {
        return {};
    }

    const int off = static_cast<int>(std::lround((1.0f - armour_damage(tier)) * 100.0f));
    const std::string head = armour_name(tier) + ": " + std::to_string(off) + "% less damage";
    const Stage stage = stage_of_tier(tier);

    // Omega: everything, at its strongest.
    if (stage >= Stage::Omega) {
        return head + ", strong thorns, a last stand every 30 s.";
    }

    // Epsilon to Eta: thorns and the last stand.
    if (stage >= Stage::Epsilon) {
        return head + ", thorns and a last stand.";
    }

    // Gamma and Delta: thorns.
    if (stage >= Stage::Gamma) {
        return head + ", and thorns shock enemies near you.";
    }

    return head + ", on top of Carbonox.";
}

ArmourPerk armour_perk(int tier) {
    ArmourPerk perk;
    const Stage stage = stage_of_tier(tier);

    // Gamma: thorns.
    if (stage >= Stage::Gamma) {
        perk.thorns_radius = 5.0f;
        perk.thorns_share = 0.5f;
    }

    // Epsilon: a last stand once a minute, and wider thorns.
    if (stage >= Stage::Epsilon) {
        perk.thorns_radius = 7.0f;
        perk.thorns_share = 1.0f;
        perk.last_stand_ticks = 3600;
    }

    // Omega: every half minute, and thorns twice as strong.
    if (stage >= Stage::Omega) {
        perk.thorns_radius = 9.0f;
        perk.thorns_share = 2.0f;
        perk.last_stand_ticks = 1800;
    }

    return perk;
}

Modifier modifier_of(int cycle) {
    // The first game and the first challenge mode: none.
    if (cycle < kModifierCycle) {
        return Modifier::None;
    }

    // In turn from the second challenge mode.
    constexpr Modifier kTurn[] = {
        Modifier::Swarm,
        Modifier::Elite,
        Modifier::Volatile,
        Modifier::Bounty,
    };
    return kTurn[static_cast<std::size_t>(cycle - kModifierCycle) % 4u];
}

const char* modifier_name(Modifier modifier) {
    // For the log and the notice.
    switch (modifier) {
        case Modifier::None:
            return "none";
        case Modifier::Swarm:
            return "Swarm";
        case Modifier::Elite:
            return "Elite guard";
        case Modifier::Volatile:
            return "Volatile";
        case Modifier::Bounty:
            return "Bounty";
    }

    return "none";
}

int elite_percent(int cycle, Modifier modifier) {
    // Elites from the third challenge mode.
    if (cycle < kEliteCycle) {
        return modifier == Modifier::Elite ? kEliteFirst : 0;
    }

    const int percent = std::min(kEliteFirst + kEliteStep * (cycle - kEliteCycle), kEliteMost);
    return modifier == Modifier::Elite ? std::min(percent * 2, kEliteGuardMost) : percent;
}

bool is_elite(int level, int slot, int index, int percent) {
    // None, or every one.
    if (percent <= 0) {
        return false;
    }

    // A fixed roll: the three mixed (a small integer hash), out of 100.
    auto x = static_cast<std::uint32_t>(level) * 0x9E3779B1u;
    x ^= static_cast<std::uint32_t>(slot) * 0x85EBCA77u;
    x ^= static_cast<std::uint32_t>(index + 1) * 0xC2B2AE3Du;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    return static_cast<int>(x % 100u) < percent;
}

bool is_arena_level(int level) {
    // Maktar's and Joba's.
    return std::find(std::begin(kArenaLevels), std::end(kArenaLevels), level)
           != std::end(kArenaLevels);
}

int omega_health(int omega) {
    return std::clamp(omega / 2, 0, kOmegaHealthMost);
}

}  // namespace ng_plus_plus
