// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The run's prizes and prices (rewards.h).

#include "rewards.h"

#include <cmath>
#include <limits>
#include <string>

#include "discs.h"

namespace ng_plus_plus {

float price_scale(float run) {
    // The sellers ask half the rise of the run scale as it was before the bolt boost.
    const float before = run / kBoltBoost;
    return before > 1.0f ? 1.0f + (before - 1.0f) * 0.5f : 1.0f;
}

std::int32_t scaled(std::int32_t value, float scale) {
    // Nothing to scale.
    if (value <= 0) {
        return value;
    }

    const double total = std::floor(static_cast<double>(value) * scale + 0.5);
    constexpr double kMost = std::numeric_limits<std::int32_t>::max();
    return total >= kMost ? std::numeric_limits<std::int32_t>::max()
                          : static_cast<std::int32_t>(total);
}

Rewards::Rewards(openrac::Mod& mod, const GameView& view, BoltScaling scaling)
    : m_mod(mod),
      m_view(view),
      m_scaling(scaling),
      m_disc(reward_disc_of(mod.api().version != nullptr ? mod.api().version : "")),
      m_items(disc_of(mod.api().version)) {}

float Rewards::run() const {
    return run_scale(m_view.cycle(), m_scaling);
}

void Rewards::start() {
    Rewards* self = this;

    // Off: the game's prizes, and no hook point taken.
    if (m_scaling == BoltScaling::Off) {
        return;
    }

    // The arena's prize: an OpenRAC without the hook point keeps the game's, said once.
    try {
        m_mod.hook<hooks::Arena_Prize>([self](auto original, std::int32_t id) {
            return self->arena_prize(original, id);
        });
    } catch (const openrac::Error& error) {
        m_mod.info(std::string("the arenas pay as the game does: ") + error.what());
    }
}

std::int32_t Rewards::arena_prize(
    openrac::Original<std::int32_t(std::int32_t)> original, std::int32_t id
) {
    return scaled(original(id), run());
}

void Rewards::scale_word(openrac::gaddr address, float scale) {
    const openrac::Memory memory = m_view.memory();
    const auto now = memory.read<std::int32_t>(address);
    auto found = m_words.find(address);

    // A word not written yet, or no longer what the mod wrote (the level loaded anew): the game's.
    if (found == m_words.end() || found->second.written != now) {
        found = m_words.insert_or_assign(address, Word{now, now}).first;
    }

    const std::int32_t want = scaled(found->second.game, scale);

    // Already so.
    if (want == now) {
        return;
    }

    memory.write(address, want);
    found->second.written = want;
}

void Rewards::tick() {
    const int level = m_view.loaded_level();

    // Another level: its own words.
    if (level != m_level) {
        m_words.clear();
        m_level = level;
        m_logged = false;
    }

    // No level, or a disc without the tables. Off leaves the game's (none was ever written).
    if (level < 0 || m_disc == nullptr || m_items == nullptr || m_scaling == BoltScaling::Off) {
        return;
    }

    const float run_now = run();
    const float price = price_scale(run_now);

    // The level's race table: each race's three prizes.
    for (const LevelTable& table : m_disc->races) {
        // Only the level loaded.
        if (table.level != level) {
            continue;
        }

        const openrac::gaddr base = m_mod.relocate_level(level, table.address);

        // Each race, each prize.
        for (int race = 0; race < kRaces; ++race) {
            // The first, second and later wins.
            for (int prize = 0; prize < kRacePrizes; ++prize) {
                const auto offset = static_cast<std::uint32_t>(race) * kRaceBytes + kRacePrize
                                    + static_cast<std::uint32_t>(prize) * 4u;
                scale_word(base + offset, run_now);
            }
        }

        // Once a level: what the second race pays now (the first's first win pays nothing).
        if (!m_logged && run_now != 1.0f) {
            m_logged = true;
            const auto first = m_words.find(base + kRaceBytes + kRacePrize);
            m_mod.info(
                "level " + std::to_string(level) + ": the races pay x"
                + std::to_string(run_now).substr(0, 4) + " (the second race "
                + std::to_string(first != m_words.end() ? first->second.game : 0) + " -> "
                + std::to_string(first != m_words.end() ? first->second.written : 0) + ")"
            );
        }
    }

    // The level's space missions: each mission's three rewards.
    for (const LevelTable& table : m_disc->missions) {
        // Only the level loaded.
        if (table.level != level) {
            continue;
        }

        const openrac::gaddr base = m_mod.relocate_level(level, table.address);

        // Each mission, each reward.
        for (int word = 0; word < kMissions * kMissionRewards; ++word) {
            scale_word(base + static_cast<std::uint32_t>(word) * 4u, run_now);
        }

        // Once a level: what the second mission pays now (the first's first win pays nothing).
        if (!m_logged && run_now != 1.0f) {
            m_logged = true;
            const auto first = m_words.find(base + static_cast<std::uint32_t>(kMissionRewards) * 4u);
            m_mod.info(
                "level " + std::to_string(level) + ": the space missions pay x"
                + std::to_string(run_now).substr(0, 4) + " (the second mission "
                + std::to_string(first != m_words.end() ? first->second.game : 0) + " -> "
                + std::to_string(first != m_words.end() ? first->second.written : 0) + ")"
            );
        }
    }

    // The level's price table: the bolt prices.
    for (const LevelTable& table : m_disc->prices) {
        // Only the level loaded.
        if (table.level != level) {
            continue;
        }

        const openrac::gaddr base = m_mod.relocate_level(level, table.address);

        // The game's price and challenge mode's of each seller.
        for (const int word : kPriceWords) {
            scale_word(base + static_cast<std::uint32_t>(word) * 4u, price);
        }
    }

    // The gadget sellers' rows: the row the seller reads (the item's slot), never row 0.
    for (const GadgetSeller& seller : kGadgetSellers) {
        const int slot = level == seller.level ? m_view.slot(seller.item) : 0;

        // Another level, or no row.
        if (slot <= 0) {
            continue;
        }

        const openrac::gaddr row = m_mod.relocate(m_items->weapon_table)
                                   + static_cast<std::uint32_t>(slot) * kRowBytes;
        scale_word(row + kRowPrice, price);
    }
}

}  // namespace ng_plus_plus
