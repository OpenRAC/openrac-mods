// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// More enemies each new game (extra_enemies.h).

#include "extra_enemies.h"

#include "elites.h"
#include "moby_scan.h"

#include <string>
#include <utility>
#include <vector>

#include "moby_pool.h"
#include "openrac/games/rac2.hpp"

namespace ng_plus_plus {

namespace {

// The cycles from which each enemy gets one copy, then two.
constexpr int kOneCopyCycle = 2;
constexpr int kTwoCopiesCycle = 4;

// The free spare slots a copy leaves: the bolts of a long streak and what the level spawns need
// them (the port keeps 128 itself; this is the mod's own, above it, with the pool's status).
constexpr int kKeepFree = 160;

// The per-planet table (README.md, "More enemies"; filled from hidden test runs of each level).
constexpr CopyEntry kCopyTable[] = {
    // Aranos.
    {0, 2460, 2, "Aranos: enemy (class 2460)"},
    {0, 2826, 2, "Aranos: enemy (class 2826)"},
    {0, 2827, 2, "Aranos: enemy (class 2827)"},
    // Oozla.
    {1, 2466, 2, "Oozla: Mutant Swamp Beast"},
    {1, 2633, 2, "Oozla: Mutant Firefly"},
    {1, 2859, 2, "Oozla: Mutant Muckdweller"},
    // Maktar Nebula.
    {2, 2501, 2, "Maktar Nebula: enemy (class 2501)"},
    {2, 2638, 2, "Maktar Nebula: Megacorp Chickenbot"},
    // Endako.
    {3, 4092, 2, "Endako: Megacorp Rivet Bot"},
    {3, 4093, 2, "Endako: Megacorp Laser Bot"},
    // Barlow.
    {4, 2234, 1, "Barlow: enemy (class 2234)"},
    {4, 4022, 2, "Barlow: Gadgetron Hound of Cuddly Death"},
    // Notak.
    {6, 3002, 2, "Notak: enemy (class 3002)"},
    {6, 3504, 2, "Notak: enemy (class 3504)"},
    // Siberius.
    {7, 3369, 2, "Siberius: enemy (class 3369)"},
    {7, 3370, 2, "Siberius: enemy (class 3370)"},
    // Dobbo.
    {9, 3306, 1, "Dobbo: enemy (class 3306)"},
    {9, 3343, 1, "Dobbo: Megacorp Trooper (class 3343)"},
    {9, 3415, 2, "Dobbo: enemy (class 3415)"},
    {9, 3472, 2, "Dobbo: enemy (class 3472)"},
    // Joba.
    {11, 3317, 1, "Joba: Joba Tribesman"},
    {11, 3335, 1, "Joba: Jobian Saur-Beast"},
    // Todano.
    {12, 4020, 2, "Todano: Mutant Test-Squirrel"},
    // Boldan.
    {13, 4587, 2, "Boldan: enemy (class 4587)"},
    // Aranos's prison.
    {14, 4395, 1, "Aranos's prison: enemy (class 4395)"},
    {14, 4396, 1, "Aranos's prison: enemy (class 4396)"},
    // Smolg.
    {17, 2952, 2, "Smolg: Grulch"},
    // Damosel.
    {18, 2921, 2, "Damosel: Protopet"},
    // Yeedil.
    {20, 2921, 2, "Yeedil: Protopet"},
    {20, 3809, 2, "Yeedil: Receiver Bot"},
    // Jamming array.
    {26, 2638, 2, "Jamming array: Megacorp Chickenbot"},
};

}  // namespace

const CopyEntry* copy_entry(int level, int oclass) {
    // Each entry.
    for (const CopyEntry& entry : kCopyTable) {
        // The level's class.
        if (entry.level == level && entry.oclass == oclass) {
            return &entry;
        }
    }

    return nullptr;
}

int copies_of(int level, int oclass, int cycle) {
    const CopyEntry* entry = copy_entry(level, oclass);
    const int each = extra_copies(cycle);

    // Not in the table: none.
    if (entry == nullptr) {
        return 0;
    }

    return each < entry->most ? each : entry->most;
}

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

    // An OpenRAC before 1.26 makes no copies that wake.
    if (m_calls.check == nullptr || m_calls.copy == nullptr) {
        m_mod.info("no extra enemies in this OpenRAC: they need mod interface 1.26");
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
    const int swarm =
        m_elites != nullptr && m_elites->modifier() == Modifier::Swarm ? kSwarmCopies : 0;
    const int each = extra_copies(cycle) + (extra_copies(cycle) > 0 ? swarm : 0);

    // No copies this cycle, none in this OpenRAC, or a party guest's game (the host's copies are
    // its).
    if (each == 0 || m_calls.copy == nullptr || level < 0 || m_tiers.party_on()) {
        m_pending.clear();
        return 0;
    }

    // The placed enemies of the table's classes the port lets go, and how many copies each: first,
    // so copies made in between are not asked about.
    std::vector<std::pair<int, int>> enemies;

    // Each live moby of the level.
    for (const openrac_moby& moby : m_mod.moby_list()) {
        const int base = moby.key >= 0 ? copies_of(level, moby.oclass, cycle) : 0;
        const int copies = base > 0 ? base + swarm : 0;

        // A class of the table that OpenRAC may copy.
        if (copies > 0 && m_calls.check(m_mod.handle(), moby.slot) == kCopyOk) {
            enemies.emplace_back(moby.slot, copies);
        }
    }

    int made = 0;
    int left_alone = 0;
    m_pending.clear();
    m_pending_level = level;
    m_retries = 0;
    m_wait = kRetryTicks;

    // Every enemy's first copy, then the second ones, until the pool is near its end or the port
    // has no room or spot for one: the port's room for the copies' larger pvars (64 KB) runs out in
    // a crowded level (Barlow's 91 hounds fill it with one copy each), and a second copy never
    // takes the place of a first.
    for (int index = 0; index < each; ++index) {
        // Each enemy that gets this many.
        for (const auto& [slot, copies] : enemies) {
            // Its copy at its own spot.
            if (index < copies) {
                const int result = make_copy(slot, index);
                made += result > 0 ? 1 : 0;
                left_alone += result < 0 ? 1 : 0;

                // Not made: tried again later.
                if (result <= 0) {
                    m_pending.emplace_back(slot, index);
                }
            }
        }
    }

    PoolStatus pool;
    const bool known = pool_status(m_mod, pool);
    m_mod.info(
        "level " + std::to_string(level) + ": " + std::to_string(made) + " extra enemies beside "
        + std::to_string(enemies.size()) + " (new game " + std::to_string(cycle) + ")"
        + (known ? ", pool " + std::to_string(pool.live) + " alive of " + std::to_string(pool.capacity)
                       + " (" + std::to_string(pool.free) + " free, " + std::to_string(pool.extra)
                       + " added)"
                 : std::string(", pool unknown (" + std::to_string(m_mod.moby_list().size())
                               + " mobys alive)"))
        + (left_alone > 0 ? ", " + std::to_string(left_alone) + " copies left out to keep the pool's room"
                          : std::string())
    );
    return made;
}

int ExtraEnemies::make_copy(int slot, int index) {
    PoolStatus pool;

    // Near the pool's end: the bolts and the level need the last slots more.
    if (pool_status(m_mod, pool) && pool.free < kKeepFree) {
        return -1;
    }

    const int copy = m_calls.copy(m_mod.handle(), slot, index);

    // Made: an elite at the run's share.
    if (copy >= 0 && m_elites != nullptr) {
        m_elites->copy_made(slot, index, MobyScan::slot_address(m_view, copy));
    }

    return copy >= 0 ? 1 : 0;
}

void ExtraEnemies::tick() {
    // Nothing missed, or a level left.
    if (m_pending.empty() || m_retries >= kRetries || m_wait-- > 0) {
        return;
    }

    // Another level: its pending copies are gone with it.
    if (m_view.loaded_level() != m_pending_level) {
        m_pending.clear();
        return;
    }

    ++m_retries;
    m_wait = kRetryTicks;
    std::vector<std::pair<int, int>> still;
    int made = 0;

    // Each missed copy again, while the pool has room.
    for (const auto& [slot, index] : m_pending) {
        const int result = make_copy(slot, index);
        made += result > 0 ? 1 : 0;

        // Still missing.
        if (result <= 0) {
            still.emplace_back(slot, index);
        }
    }

    m_pending = std::move(still);

    // Said when it made some.
    if (made > 0) {
        m_mod.info(
            "level " + std::to_string(m_pending_level) + ": " + std::to_string(made)
            + " more extra enemies on try " + std::to_string(m_retries) + ", "
            + std::to_string(m_pending.size()) + " still missing"
        );
    }
}

}  // namespace ng_plus_plus
