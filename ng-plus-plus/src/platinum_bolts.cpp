// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Platinum bolts and bolt scaling (platinum_bolts.h).

#include "platinum_bolts.h"

#include <cmath>
#include <string>

#include "hooks.h"

namespace ng_plus_plus {
namespace {

// A burst's flag: at most `maximum` coins, the rest to the game's word (moby_spawn_bolt_pickups).
constexpr std::int32_t kFlagLimit = 8;

// The first cycle with platinum bolts (the first challenge mode, a few), and the first whose drops
// pay more (bolt_rules.cpp's tables start there).
constexpr int kPlatinumCycle = 1;
constexpr int kFirstCycle = 2;

// How many drops a session writes to the log.
constexpr int kDropsLogged = 5;

// Obsidian's glow, a shade per coin size (0xRRGGBB, from the 25-bolt coin to the 25,000 one):
// violet on the small ones, through magenta, to ember red on the big ones.
constexpr std::uint32_t kObsidianGlow[kCoinKinds] = {
    0x8A4DFFu, 0x9A3CFFu, 0xB030F0u, 0xC828D0u, 0xE02090u, 0xF01850u, 0xFF1028u,
};

// A coin's start and speed, four floats each, on the game's stack for the coin maker.
constexpr std::uint32_t kVectorBytes = 16u;

// A coin past the burst's own count reuses one of its places, its speed turned by this much more
// each time round (the golden angle, in radians), so coins on one place fly apart.
constexpr float kExtraTurn = 2.39996323f;

// Returns the class (from kCoinClass) the game makes for a coin worth `value`: the one worth it,
// else the 1-bolt one (moby_spawn_bolt_pickup).
int kind_of_value(std::int32_t value) {
    // The seven worths.
    for (int kind = 0; kind < kCoinKinds; ++kind) {
        // Found.
        if (kCoinValues[static_cast<std::size_t>(kind)] == value) {
            return kind;
        }
    }

    return 0;
}

}  // namespace

void PlatinumBolts::start() {
    PlatinumBolts* self = this;

    // Off: the game's drops, and no hook point taken.
    if (m_scaling == BoltScaling::Off) {
        return;
    }

    m_material = moby_material_of(m_mod.api());
    m_obsidian = has_obsidian(m_mod.api());

    // An OpenRAC before 1.23 cannot draw a platinum coin: the game's drops, said once.
    if (m_material == nullptr) {
        m_mod.info("no platinum bolts or bolt scaling in this OpenRAC: they need interface 1.23");
        return;
    }

    // Each of the three, on the game's thread; a port that lacks one keeps the game's drops (a
    // hook point taken before it goes on to the game's, m_enabled being false).
    try {
        m_mod.hook<hooks::Bolt_SpawnPickups>([self](
                                                 auto original,
                                                 std::uint32_t moby,
                                                 std::int32_t low,
                                                 std::int32_t high,
                                                 std::int32_t flags,
                                                 std::int32_t surface,
                                                 std::int32_t maximum,
                                                 float height
                                             ) {
            self->spawn_pickups(original, moby, low, high, flags, surface, maximum, height);
        });
        m_mod.hook<hooks::Bolt_SpawnPickup>([self](
                                                auto original,
                                                std::uint32_t source,
                                                std::uint32_t position,
                                                std::uint32_t velocity,
                                                std::uint32_t flags,
                                                std::int32_t value,
                                                std::int32_t counted
                                            ) {
            return self->spawn_pickup(original, source, position, velocity, flags, value, counted);
        });
        m_mod.hook<hooks::Bolt_Collect>([self](auto original, std::uint32_t moby) {
            self->collect(original, moby);
        });
        m_enabled = true;

        // An OpenRAC before 1.24 cannot draw obsidian: platinum only, said once.
        if (!m_obsidian) {
            m_mod.info("no obsidian bolts in this OpenRAC: they need interface 1.24");
        }
    } catch (const openrac::Error& error) {
        m_mod.info(std::string("no platinum bolts or bolt scaling: ") + error.what());
    }
}

void PlatinumBolts::spawn_pickups(
    openrac::Original<BurstFn> original,
    std::uint32_t moby,
    std::int32_t low,
    std::int32_t high,
    std::int32_t flags,
    std::int32_t surface,
    std::int32_t maximum,
    float height
) {
    const int cycle = m_view.cycle();

    // Off, an older port, the first game, or a burst inside one: the game's.
    if (!m_enabled || cycle < kPlatinumCycle || m_recording) {
        original(moby, low, high, flags, surface, maximum, height);
        return;
    }

    // The run's scale, and an enemy's toughness from its own bolts (a crate or a scripted burst:
    // none); the first challenge mode only some platinum, its drops the game's.
    const float tough = toughness(m_view.enemy_bolts(moby).value_or(0));
    const float scale = cycle < kFirstCycle ? 1.0f : run_scale(cycle, m_scaling) * tough;

    // The game's burst with the scaled amount: its roll, doubling, pacing and coin limit decide the
    // total; its coins are written down (spawn_pickup), not made.
    m_burst.clear();
    m_recording = true;
    original(
        moby, scale_amount(low, scale), scale_amount(high, scale), flags, surface, maximum, height
    );
    m_recording = false;

    // No coins: nothing to pay (the game spawned none, or put it all in its word).
    if (m_burst.empty()) {
        return;
    }

    std::int32_t total = 0;

    // What the game's coins were worth together.
    for (const Recorded& coin : m_burst) {
        total += coin.value;
    }

    // The game's coin limit, when the burst has one.
    const int limit = (flags & kFlagLimit) != 0 ? maximum : -1;

    const float share = platinum_share(cycle, m_view.streak());
    const float obsidian = m_obsidian ? obsidian_share(cycle, m_view.streak()) : 0.0f;
    const CoinPlan plan = split(total, share, obsidian, limit);

    // The first few drops of a session in the log, to show what the drops pay.
    if (m_drops_logged < kDropsLogged) {
        ++m_drops_logged;
        int platinum = 0;
        int obsidian_coins = 0;

        // The rare coins of each class.
        for (int kind = 0; kind < kCoinKinds; ++kind) {
            platinum += plan.platinum[static_cast<std::size_t>(kind)];
            obsidian_coins += plan.obsidian[static_cast<std::size_t>(kind)];
        }

        m_mod.info(
            "drop of " + std::to_string(total) + " bolts (challenge mode " + std::to_string(cycle)
            + ", x" + std::to_string(m_view.streak()) + " streak, scale "
            + std::to_string(scale).substr(0, 4) + "): " + std::to_string(obsidian_coins)
            + " obsidian and " + std::to_string(platinum) + " platinum coins of "
            + std::to_string(plan.coins())
        );
    }

    pay(plan);
}

std::uint32_t PlatinumBolts::spawn_pickup(
    openrac::Original<CoinFn> original,
    std::uint32_t source,
    std::uint32_t position,
    std::uint32_t velocity,
    std::uint32_t flags,
    std::int32_t value,
    std::int32_t counted
) {
    // Inside the mod's burst: written down for pay(), none made (the burst reads no result).
    if (m_recording) {
        const openrac::Memory memory = m_view.memory();
        Recorded coin;
        coin.source = source;
        coin.position = memory.read<std::array<float, 4>>(position);
        coin.velocity = memory.read<std::array<float, 4>>(velocity);
        coin.flags = flags;
        coin.counted = counted;
        coin.value = kCoinValues[static_cast<std::size_t>(kind_of_value(value))];
        m_burst.push_back(coin);
        return 0;
    }

    const std::uint32_t moby = original(source, position, velocity, flags, value, counted);

    // A gold coin in a moby a rare one had: no longer rare.
    m_platinum.erase(moby);
    return moby;
}

void PlatinumBolts::collect(
    openrac::Original<void(std::uint32_t)> original, std::uint32_t moby
) {
    const auto found = m_platinum.find(moby);

    // A gold coin: the game's.
    if (found == m_platinum.end()) {
        original(moby);
        return;
    }

    const Marked marked = found->second;
    m_platinum.erase(found);

    // Still the coin the mod made (a moby of its class), so worth its tier's.
    const bool same = m_view.moby_class(moby) == kCoinClass + marked.kind;
    original(moby);

    // The game added its class's worth; platinum is kPlatinumTimes as much, obsidian
    // kObsidianTimes.
    if (same) {
        m_mod.bolts(
            kCoinValues[static_cast<std::size_t>(marked.kind)] * (tier_times(marked.tier) - 1)
        );
    }

    m_material(m_mod.handle(), moby, kMaterialNone, kNoTint);
}

void PlatinumBolts::tick() {
    const int level = m_view.loaded_level();

    // A level left: its coins went with it.
    if (level != m_level) {
        m_platinum.clear();
        m_level = level;
    }
}

void PlatinumBolts::pay(const CoinPlan& plan) {
    int index = 0;

    // The most valuable class first, as the game makes its own; obsidian, then platinum, then gold
    // in each.
    for (int kind = kCoinKinds - 1; kind >= 0; --kind) {
        const auto at = static_cast<std::size_t>(kind);

        // Each obsidian coin of the class.
        for (int n = 0; n < plan.obsidian[at]; ++n) {
            make_coin(index++, kind, CoinTier::Obsidian);
        }

        // Each platinum coin of the class.
        for (int n = 0; n < plan.platinum[at]; ++n) {
            make_coin(index++, kind, CoinTier::Platinum);
        }

        // Each gold coin of the class.
        for (int n = 0; n < plan.gold[at]; ++n) {
            make_coin(index++, kind, CoinTier::Gold);
        }
    }

    // What no coin holds goes to the game's word, divided by the streak as the game's burst does.
    if (plan.leftover > 0) {
        const int streak = m_view.streak();

        // No streak: whole.
        m_view.add_bolt_credit(streak > 0 ? plan.leftover / streak : plan.leftover);
    }
}

std::uint32_t PlatinumBolts::make_coin(int index, int kind, CoinTier tier) {
    const auto count = static_cast<int>(m_burst.size());
    const Recorded& place = m_burst[static_cast<std::size_t>(index % count)];
    const float turn = kExtraTurn * static_cast<float>(index / count);
    const float cos_turn = std::cos(turn);
    const float sin_turn = std::sin(turn);
    std::array<float, 4> velocity = place.velocity;
    velocity[0] = place.velocity[0] * cos_turn - place.velocity[1] * sin_turn;
    velocity[1] = place.velocity[0] * sin_turn + place.velocity[1] * cos_turn;

    // The start and speed on the game's stack, as the game's burst passes its own.
    const openrac::StackFrame frame(m_mod, 2 * kVectorBytes);
    const openrac::Memory memory = m_view.memory();
    memory.write(frame.address(), place.position);
    memory.write(frame.address() + kVectorBytes, velocity);

    // The game's coin maker past the mod's own replacement (and any other mod's before it).
    const auto make = m_mod.original<hooks::Bolt_SpawnPickup>();
    const std::uint32_t moby = make(
        place.source,
        frame.address(),
        frame.address() + kVectorBytes,
        place.flags,
        kCoinValues[static_cast<std::size_t>(kind)],
        place.counted
    );

    // A rare coin is marked; a gold one in a moby a rare one had is not any more.
    if (tier != CoinTier::Gold) {
        mark(moby, kind, tier);
    } else {
        m_platinum.erase(moby);
    }

    return moby;
}

void PlatinumBolts::mark(std::uint32_t moby, int kind, CoinTier tier) {
    const auto at = static_cast<std::size_t>(kind);
    const std::int32_t extra = kCoinValues[at] * (tier_times(tier) - 1);

    // No coin made (the game put its class's worth in its word): the extra goes there too.
    if (moby == 0) {
        m_view.add_bolt_credit(extra);
        return;
    }

    // Drawn as its tier, within the port's room for one mod's materials: obsidian with its size's
    // glow.
    const bool obsidian = tier == CoinTier::Obsidian;
    const std::uint32_t material = obsidian ? kMaterialObsidian : kMaterialPlatinum;
    const std::uint32_t rgb = obsidian ? kObsidianGlow[at] : kNoTint;
    const bool room = m_platinum.size() < kMaterialsMax || m_platinum.count(moby) != 0;
    const bool drawn =
        room && m_material(m_mod.handle(), moby, material, rgb) == OPENRAC_HOOK_OK;

    // Not drawn as its tier: a gold coin to see, so its extra worth is paid at once.
    if (!drawn) {
        m_platinum.erase(moby);
        m_mod.bolts(extra);

        // Said once.
        if (!m_mark_logged) {
            m_mod.info(
                "a platinum or obsidian bolt could not be drawn; its worth was added at once"
            );
            m_mark_logged = true;
        }

        return;
    }

    m_platinum[moby] = Marked{kind, tier};
}

}  // namespace ng_plus_plus
