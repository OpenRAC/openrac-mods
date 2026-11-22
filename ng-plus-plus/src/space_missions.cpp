// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The space combat missions (space_missions.h).

#include "space_missions.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "discs.h"
#include "elites.h"
#include "tiers.h"
#include "weapon_visuals.h"

namespace ng_plus_plus {

namespace {

// The cycles from which a fight gets one, two and three more waves.
constexpr int kOneWaveCycle = 2;
constexpr int kTwoWavesCycle = 4;
constexpr int kThreeWavesCycle = 6;

// The elites' share: from cycle 2, its first share, its step a run, its most, Elite guard's most.
constexpr int kSpaceEliteCycle = 2;
constexpr int kSpaceEliteFirst = 20;
constexpr int kSpaceEliteStep = 10;
constexpr int kSpaceEliteMost = 60;
constexpr int kSpaceEliteGuardMost = 80;

// The ticks a fight has none alive before it is over (the next mission is a new one).
constexpr int kFightOverTicks = 90;

// A moby's fields: its flags (+0x34: 0x20 it has a motion record), its pvars.
constexpr std::uint32_t kMobyFlags = 0x34u;
constexpr std::uint16_t kMobyHasMotion = 0x20u;
constexpr std::uint32_t kMobyPvars = 0x68u;

// The motion record (moby_get_motion_data: pvars +0x14): alive at +0x110, its health at +0x148.
constexpr std::uint32_t kPvarsMotion = 0x14u;
constexpr std::uint32_t kMotionAlive = 0x110u;
constexpr std::uint32_t kMotionHealth = 0x148u;
constexpr std::uint32_t kMotionBytes = 0x150u;

// A shot's pvars +4: the moby that fired it (UpdateFeltzinProjectile's owner test).
constexpr std::uint32_t kShotOwner = 0x04u;

// Feltzin's target spawner: its class and timer (pvars +0x26C, s32).
constexpr int kTargetSpawnerClass = 4378;
constexpr int kFeltzin = 5;
constexpr std::uint32_t kSpawnerTimer = 0x26Cu;

// The elites' look where obsidian is refused: violet energy, as elites.cpp's.
constexpr std::uint32_t kEliteGlow = 0xA040FFu;
constexpr int kEliteEnergy = 10;

// The space levels' enemy ships (rac2-decomp: each class's init in its level's code).
// clang-format off: one class per row.
constexpr ShipClass kShipClasses[] = {
    {5, 3955, true, "Feltzin: fighter (20 health)"},
    {5, 3958, true, "Feltzin: pod carrier (5 health)"},
    {5, 4341, true, "Feltzin: fast target (5 health)"},
    {10, 3973, false, "Hrugis: the boss (30 health)"},
    {10, 3982, true, "Hrugis: target ship (10 health)"},
    {10, 3987, true, "Hrugis: attacker (10 health)"},
    {15, 3987, true, "Gorn: attacker (10 health)"},
    {15, 4625, false, "Gorn: the capital ship (150 health)"},
    {15, 4630, false, "Gorn: path mover (100 health)"},
};
// clang-format on

// moby_init_spawned_sub_object in each space level, per disc (the decomp's catalogues: one
// function, copied in each level's code).
struct ReviveAt {
    const char* version;
    std::uint32_t at[4];  // Levels 5, 10, 15, 25.
};

constexpr ReviveAt kRevive[] = {
    {"ntsc", {0x0037AEC0u, 0x0037CCB0u, 0x00381708u, 0x00374DF8u}},
    {"pal", {0x0037D610u, 0x0037F400u, 0x00383F28u, 0x00377548u}},
    {"ntsc2", {0x0037C130u, 0x0037DF20u, 0x00382980u, 0x00376068u}},
};

// The game's call: (moby, flag), flag 1 as a mission's start passes it.
using ReviveFn = void (*)(std::uint32_t, std::int32_t);

}  // namespace

bool is_space_level(int level) {
    return std::find(std::begin(kSpaceLevels), std::end(kSpaceLevels), level)
           != std::end(kSpaceLevels);
}

int extra_waves(int cycle, Modifier modifier) {
    const int base = cycle >= kThreeWavesCycle ? 3
                     : cycle >= kTwoWavesCycle ? 2
                     : cycle >= kOneWaveCycle  ? 1
                                               : 0;
    return base > 0 && modifier == Modifier::Swarm ? base + kSwarmCopies : base;
}

int space_elite_percent(int cycle, Modifier modifier, int wave) {
    // None before the second challenge mode.
    if (cycle < kSpaceEliteCycle) {
        return 0;
    }

    int percent =
        std::min(kSpaceEliteFirst + kSpaceEliteStep * (cycle - kSpaceEliteCycle), kSpaceEliteMost);

    // Elite guard: twice as many.
    if (modifier == Modifier::Elite) {
        percent = std::min(percent * 2, kSpaceEliteGuardMost);
    }

    return std::min(percent + (wave > 0 ? kWaveElitePercent : 0), 100);
}

bool wave_due(int peak, int alive, int done, int extra) {
    return done < extra && peak >= kWaveFloor && alive > 0 && alive * 4 <= peak;
}

float target_spawn_rate(int cycle) {
    return cycle >= kTwoWavesCycle ? 2.0f : cycle >= kOneWaveCycle ? 1.5f : 1.0f;
}

const ShipClass* ship_class_of(int level, int moby_class) {
    // Each listed class.
    for (const ShipClass& ship : kShipClasses) {
        // This level's.
        if (ship.level == level && ship.moby_class == moby_class) {
            return &ship;
        }
    }

    return nullptr;
}

bool is_player_side(int moby_class) {
    // His ship, the hub's hull, Hrugis's freighters.
    if (moby_class == kShipClass || moby_class == 3412 || moby_class == 3262) {
        return true;
    }

    // The ship's parts (equipment_spawn_part_moby's classes).
    return (moby_class >= 4305 && moby_class <= 4325) || (moby_class >= 4742 && moby_class <= 4744);
}

std::uint32_t revive_address(std::string_view version, int level) {
    const int index = level == 5 ? 0 : level == 10 ? 1 : level == 15 ? 2 : level == 25 ? 3 : -1;

    // Not a space level.
    if (index < 0) {
        return 0;
    }

    // The disc's.
    for (const ReviveAt& disc : kRevive) {
        // The disc named.
        if (version == disc.version) {
            return disc.at[index];
        }
    }

    return 0;
}

void SpaceMissions::start() {
    m_material = moby_material_of(m_mod.api());
    m_obsidian = m_material != nullptr && m_mod.api().api_minor >= 24;
}

std::uint32_t SpaceMissions::motion_of(std::uint32_t moby) const {
    // No moby there.
    if (!GameView::is_memory(moby, kMobyRecordBytes)) {
        return 0;
    }

    const openrac::Memory memory = m_view.memory();

    // No motion record: moby_get_motion_data's 0.
    if ((memory.read<std::uint16_t>(moby + kMobyFlags) & kMobyHasMotion) == 0) {
        return 0;
    }

    const auto pvars = memory.read<std::uint32_t>(moby + kMobyPvars);

    // No pvars.
    if (!GameView::is_memory(pvars, kPvarsMotion + 4u)) {
        return 0;
    }

    const auto motion = memory.read<std::uint32_t>(pvars + kPvarsMotion);
    return GameView::is_memory(motion, kMotionBytes) ? motion : 0u;
}

bool SpaceMissions::ship_alive(std::uint32_t moby) const {
    const std::uint32_t motion = motion_of(moby);
    return motion != 0 && m_view.memory().read<std::int32_t>(motion + kMotionAlive) == 1;
}

float SpaceMissions::health_of(std::uint32_t moby, int moby_class) const {
    const int cycle = m_view.cycle();
    float health = m_settings.scaling ? enemy_health(cycle, m_settings.strength) : 1.0f;
    const ShipClass* ship = ship_class_of(m_view.loaded_level(), moby_class);

    // A mission's boss or capital ship, as a boss on foot.
    if (m_settings.scaling && ship != nullptr && !ship->fighter) {
        health *= boss_health(cycle, m_settings.strength);
    }

    const auto it = m_fight.find(moby);

    // An elite.
    if (it != m_fight.end() && it->second.elite) {
        health *= kShipEliteHealth;
    }

    return health;
}

bool SpaceMissions::players_hit(std::uint32_t request) const {
    const openrac::Memory memory = m_view.memory();
    const auto source = memory.read<std::uint32_t>(request + kHitSource);
    const std::optional<int> source_class = m_view.moby_class(source);

    // No source moby: not his.
    if (!source_class) {
        return false;
    }

    // His ship itself.
    if (*source_class == kShipClass) {
        return true;
    }

    const auto pvars = memory.read<std::uint32_t>(source + kMobyPvars);

    // A shot whose owner is his ship (the twin guns' and, as their code keeps it, the specials').
    if (GameView::is_memory(pvars, kShotOwner + 4u)) {
        const std::optional<int> owner =
            m_view.moby_class(memory.read<std::uint32_t>(pvars + kShotOwner));

        // Fired by his ship.
        if (owner && *owner == kShipClass) {
            return true;
        }

        // Fired by an enemy ship.
        if (owner && ship_class_of(m_view.loaded_level(), *owner) != nullptr) {
            return false;
        }
    }

    // His ship's own shot class, else whatever is not an enemy ship.
    return *source_class == kShipShotClass
           || ship_class_of(m_view.loaded_level(), *source_class) == nullptr;
}

void SpaceMissions::post_hit(PostHitFn original, std::uint32_t victim, std::uint32_t request) {
    const openrac::Memory memory = m_view.memory();
    const std::optional<int> victim_class = m_view.moby_class(victim);

    // Nothing NG++ scales: the hero, his side, or no moby.
    if (request == 0 || victim == m_view.hero_moby() || !victim_class
        || is_player_side(*victim_class)) {
        original(victim, request);
        return;
    }

    const auto damage_now = memory.read<float>(request + kHitDamage);
    const bool his = players_hit(request);
    const int tier = m_ship.tier();
    float damage = damage_now / health_of(victim, *victim_class);

    // His hit: the ship's tier, and its perks.
    if (his) {
        damage *= ship_damage(tier);
        ++m_hits;

        // Gamma: every fourth hit overcharged.
        if (tier >= kShipOverchargeTier && m_hits % kShipOverchargeEvery == 0) {
            damage *= kShipOvercharge;
        }

        const ShipClass* ship = ship_class_of(m_view.loaded_level(), *victim_class);
        const std::uint32_t motion = motion_of(victim);
        const auto pvars = memory.read<std::uint32_t>(victim + kMobyPvars);
        const auto health_cell =
            GameView::is_memory(pvars, 4u) ? memory.read<std::uint32_t>(pvars) : 0u;

        // Epsilon: a ship (not a boss) left under a fifth of its health is destroyed.
        if (tier >= kShipExecuteTier && ship != nullptr && ship->fighter && motion != 0
            && GameView::is_memory(health_cell, 4u)) {
            const auto now = memory.read<float>(health_cell);
            const auto most = static_cast<float>(memory.read<std::int32_t>(motion + kMotionHealth));

            // Left under the share: the rest of it.
            if (now - damage < most * kShipExecuteShare && now > damage) {
                damage = now + 1.0f;
            }
        }
    }

    memory.write(request + kHitDamage, damage);
    original(victim, request);

    // Omega: a share arcs to the nearest other enemy ship in range.
    if (his && tier >= kShipArcTier && m_scan.read(m_tick)) {
        const std::array<float, 3> at = m_scan.position(victim);
        const ScannedMoby* nearest = nullptr;
        float best = kShipArcRange * kShipArcRange;

        // Each other alive enemy ship.
        for (const ScannedMoby& moby : m_scan.mobys()) {
            // Not a listed ship, the victim, or a dead one.
            if (moby.address == victim
                || ship_class_of(m_view.loaded_level(), moby.moby_class) == nullptr
                || !ship_alive(moby.address)) {
                continue;
            }

            const float d2 = distance2(at, moby.position);

            // Nearer.
            if (d2 < best) {
                best = d2;
                nearest = &moby;
            }
        }

        // One in range: its share, divided by its own health.
        if (nearest != nullptr) {
            const float arc = damage_now * ship_damage(tier) * kShipArcShare
                              / health_of(nearest->address, nearest->moby_class);
            memory.write(request + kHitDamage, arc);
            original(nearest->address, request);
        }
    }

    // The caller may post the same request to the next victim.
    memory.write(request + kHitDamage, damage_now);
}

void SpaceMissions::roll_elite(std::uint32_t moby, Ship& ship) {
    const Modifier modifier = m_elites != nullptr ? m_elites->modifier() : Modifier::None;
    const ShipClass* listed = ship_class_of(m_view.loaded_level(), ship.moby_class);
    const int percent = space_elite_percent(m_view.cycle(), modifier, ship.wave);
    ship.elite = m_settings.on && listed != nullptr && listed->fighter
                 && is_elite(m_view.loaded_level(), ship.slot, ship.wave, percent);

    // No materials in this port: the elite is tougher only.
    if (m_material == nullptr) {
        return;
    }

    // Drawn so, or as the game draws it.
    if (!ship.elite) {
        m_material(m_mod.handle(), moby, kMaterialNone, kNoTint);
    } else if (
        !m_obsidian
        || m_material(m_mod.handle(), moby, kMaterialObsidian, kNoTint) != OPENRAC_HOOK_OK
    ) {
        m_material(m_mod.handle(), moby, energy_at(kEliteEnergy), kEliteGlow);
    }
}

void SpaceMissions::enter(int level) {
    m_level = level;
    m_fight.clear();
    m_pending.clear();
    m_peak = 0;
    m_waves = 0;
    m_quiet = 0;
    m_spawner_carry = 0.0f;
}

void SpaceMissions::tick(std::uint32_t tick) {
    m_tick = tick;
    const int level = m_view.loaded_level();

    // Another level: its fight is gone.
    if (level != m_level) {
        enter(level);
    }

    // Not a space level, the setting off, or a party guest's game (the host's waves are its).
    if (!is_space_level(level) || !m_settings.on || m_tiers.party_on() || !m_scan.read(tick)) {
        return;
    }

#ifdef NGPP_TEST_SETUP
    test_revive(level);
#endif
    run_fight(level);
    speed_spawner();
}

void SpaceMissions::run_fight(int level) {
    int alive = 0;

    // New ships alive: the fight's.
    for (const ScannedMoby& moby : m_scan.mobys()) {
        const ShipClass* listed = ship_class_of(level, moby.moby_class);

        // Not an enemy ship, or one not alive.
        if (listed == nullptr || !ship_alive(moby.address)) {
            continue;
        }

        auto [it, added] = m_fight.try_emplace(moby.address);

        // First seen: its wave and its roll.
        if (added) {
            it->second.moby_class = moby.moby_class;
            it->second.slot = moby.slot;
            it->second.wave = 0;
            roll_elite(moby.address, it->second);
        }
    }

    bool boss = false;

    // Each ship of the fight: alive now?
    for (auto& [address, ship] : m_fight) {
        const bool now = m_view.moby_class(address) == ship.moby_class && ship_alive(address);

        // Died: drawn as the game draws it again.
        if (ship.alive && !now && ship.elite && m_material != nullptr) {
            m_material(m_mod.handle(), address, kMaterialNone, kNoTint);
        }

        ship.alive = now;
        alive += now ? 1 : 0;
        const ShipClass* listed = ship_class_of(level, ship.moby_class);
        boss = boss || (listed != nullptr && !listed->fighter);
    }

    // None alive for a while: the fight is over, the next mission's is new.
    if (alive == 0 && m_pending.empty()) {
        if (!m_fight.empty() && ++m_quiet > kFightOverTicks) {
            m_fight.clear();
            m_peak = 0;
            m_waves = 0;
            m_quiet = 0;
        }

        return;
    }

    m_quiet = 0;
    m_peak = std::max(m_peak, alive);
    const Modifier modifier = m_elites != nullptr ? m_elites->modifier() : Modifier::None;

    // The next wave: every dead fighter of the fight back.
    if (!boss && m_pending.empty()
        && wave_due(m_peak, alive, m_waves, extra_waves(m_view.cycle(), modifier))) {
        ++m_waves;

        // Each dead fighter.
        for (const auto& [address, ship] : m_fight) {
            const ShipClass* listed = ship_class_of(level, ship.moby_class);

            // Dead, its record still its class.
            if (!ship.alive && listed != nullptr && listed->fighter
                && m_view.moby_class(address) == ship.moby_class) {
                m_pending.push_back(address);
            }
        }

        m_mod.info(
            "level " + std::to_string(level) + ": wave " + std::to_string(m_waves + 1) + " of "
            + std::to_string(extra_waves(m_view.cycle(), modifier) + 1) + ", "
            + std::to_string(m_pending.size()) + " ships back beside " + std::to_string(alive)
            + " (new game " + std::to_string(m_view.cycle()) + ")"
        );
        m_peak = 0;
    }

    revive_pending(level);
}

void SpaceMissions::revive_pending(int level) {
    // Nothing to bring back.
    if (m_pending.empty()) {
        return;
    }

    const std::uint32_t address = revive_address(m_mod.version(), level);
    const auto revive =
        address != 0 ? reinterpret_cast<ReviveFn>(m_mod.api().function_at(address)) : nullptr;

    // This port has no such function: no waves.
    if (revive == nullptr) {
        if (!m_logged_revive) {
            m_logged_revive = true;
            m_mod.info("no space waves in this OpenRAC: the game's ship init is not found");
        }

        m_pending.clear();
        return;
    }

    int done = 0;

    // Up to the tick's most, the rest next tick.
    while (!m_pending.empty() && done < kReviveEachTick) {
        const std::uint32_t moby = m_pending.back();
        m_pending.pop_back();
        auto it = m_fight.find(moby);

        // Gone, or alive again by the game's own hand.
        if (it == m_fight.end() || m_view.moby_class(moby) != it->second.moby_class
            || motion_of(moby) == 0 || ship_alive(moby)) {
            continue;
        }

        revive(moby, 1);
        it->second.wave = m_waves;
        it->second.alive = ship_alive(moby);
        roll_elite(moby, it->second);
        ++done;
    }
}

void SpaceMissions::speed_spawner() {
    const float rate = target_spawn_rate(m_view.cycle());

    // Only Feltzin's, past the first challenge mode.
    if (m_level != kFeltzin || rate <= 1.0f) {
        return;
    }

    const openrac::Memory memory = m_view.memory();

    // The spawner.
    for (const ScannedMoby& moby : m_scan.mobys()) {
        // Another moby.
        if (moby.moby_class != kTargetSpawnerClass) {
            continue;
        }

        const auto pvars = memory.read<std::uint32_t>(moby.address + kMobyPvars);

        // No pvars.
        if (!GameView::is_memory(pvars, kSpawnerTimer + 4u)) {
            continue;
        }

        const auto left = memory.read<std::int32_t>(pvars + kSpawnerTimer);
        const int extra = extra_timer_ticks(rate, left, m_spawner_carry);
        memory.write(pvars + kSpawnerTimer, left - extra);
    }
}

#ifdef NGPP_TEST_SETUP
void SpaceMissions::test_revive(int level) {
    ++m_test_ticks;

    // Not asked for (NGPP_SPACE_REVIVE=1), or not the tick.
    if (std::getenv("NGPP_SPACE_REVIVE") == nullptr || m_test_ticks % 600 != 300) {
        return;
    }

    int placed = 0;
    int alive = 0;

    // Each listed fighter with a motion record: brought to life as a wave would.
    for (const ScannedMoby& moby : m_scan.mobys()) {
        const ShipClass* listed = ship_class_of(level, moby.moby_class);

        // Not a fighter.
        if (listed == nullptr || !listed->fighter || motion_of(moby.address) == 0) {
            continue;
        }

        ++placed;
        alive += ship_alive(moby.address) ? 1 : 0;

        // The first time: every dead one back.
        if (m_test_ticks == 300 && !ship_alive(moby.address)) {
            m_fight[moby.address] = Ship{moby.moby_class, moby.slot, 1, false, false};
            m_pending.push_back(moby.address);
        }
    }

    m_mod.info(
        "test: level " + std::to_string(level) + " fighters " + std::to_string(placed) + ", alive "
        + std::to_string(alive) + ", bringing back " + std::to_string(m_pending.size())
    );
}
#endif

}  // namespace ng_plus_plus
