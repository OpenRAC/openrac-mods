// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// More enemies each new game (extra_enemies.h).

#include "extra_enemies.h"

#include <string>
#include <vector>

#include "openrac/games/rac2.hpp"

namespace ng_plus_plus {

namespace {

// The cycles from which each enemy gets one copy, then two.
constexpr int kOneCopyCycle = 2;
constexpr int kTwoCopiesCycle = 4;

}  // namespace

int extra_copies(int cycle) {
    // The fourth challenge mode on: two.
    if (cycle >= kTwoCopiesCycle) {
        return 2;
    }

    return cycle >= kOneCopyCycle ? 1 : 0;
}

void ExtraEnemies::start() {
    // The setting off: the game's enemies as they are.
    if (!m_on) {
        return;
    }

    m_calls = copy_calls_of(m_mod.api());

    // An OpenRAC before 1.25 makes no copies.
    if (m_calls.check == nullptr || m_calls.copy == nullptr) {
        m_mod.info("no extra enemies in this OpenRAC: they need mod interface 1.25");
        return;
    }

    ExtraEnemies* self = this;

    // The level's load: an OpenRAC without the hook point makes no copies either.
    try {
        m_mod.hook<openrac::rac2::Level_Load>(
            [self](auto original, std::int32_t load, std::int32_t resume) {
                const std::int32_t next = original(load, resume);
                self->level_loaded();
                return next;
            }
        );
    } catch (const openrac::Error& error) {
        m_calls = {};
        m_mod.info(std::string("no extra enemies: ") + error.what());
    }
}

int ExtraEnemies::level_loaded() {
    const int level = m_view.loaded_level();
    const int cycle = m_view.cycle();
    const int each = extra_copies(cycle);

    // No copies this cycle, none in this OpenRAC, or a party guest's game (the host's copies are
    // its).
    if (each == 0 || m_calls.copy == nullptr || level < 0 || m_tiers.party_on()) {
        return 0;
    }

    // The placed enemies the port lets go first: copies made in between are not asked about.
    std::vector<int> enemies;

    // Each live moby of the level.
    for (const openrac_moby& moby : m_mod.moby_list()) {
        // One OpenRAC may copy.
        if (moby.key >= 0 && m_calls.check(m_mod.handle(), moby.slot) == kCopyOk) {
            enemies.push_back(moby.slot);
        }
    }

    int made = 0;

    // Each enemy's copies, until the port has no room or spot for one.
    for (const int slot : enemies) {
        // Its copies, each at its own spot.
        for (int index = 0; index < each; ++index) {
            made += m_calls.copy(m_mod.handle(), slot, index) >= 0 ? 1 : 0;
        }
    }

    m_mod.info(
        "level " + std::to_string(level) + ": " + std::to_string(made) + " extra enemies beside "
        + std::to_string(enemies.size()) + " (new game " + std::to_string(cycle) + ")"
    );
    return made;
}

}  // namespace ng_plus_plus
