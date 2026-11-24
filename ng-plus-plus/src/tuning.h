// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.5.1's weapon tuning in the game (the rules are tuning_rules.h's):
//
//   - the Tesla Claw's steady hits: on each of the claw's locks a small hit every kTeslaHitTicks,
//     posted as the game's own (through Combat, so the tiers, the enemies' curve and the perks
//     see it) from the attacker the victim did not see last, and the game's own hit on a lock
//     made as small; each lands and makes the enemy react, as the Heavy Lancer's rounds do;
//   - the Zodiac's boss damage: while its gun pulses, each boss within its reach loses its
//     vulnerability times the game's 20 a frame (the tier's damage, over NG++'s boss curve);
//   - the decoy: its health raised as it appears, by kDecoyBase and the Decoy Glove's tier, and
//     the enemies' hits on it scaled as their hits on the hero.
//
// Only the game's thread uses it; nothing in a party guest's game.

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "combat.h"
#include "game_view.h"
#include "moby_scan.h"
#include "openrac/mod.hpp"
#include "weapons.h"
#include "zodiac_credit.h"

namespace ng_plus_plus {

// The weapon tuning.
class WeaponTuning {
public:
    // The tuning of `weapons` in `view`'s game through `mod`, its hits through `combat`; the
    // Tesla Claw's steady hits when `steady_tesla` (the "tesla_steady" setting).
    WeaponTuning(
        openrac::Mod& mod,
        const GameView& view,
        const Weapons& weapons,
        Combat& combat,
        bool steady_tesla
    )
        : m_mod(mod),
          m_view(view),
          m_weapons(weapons),
          m_combat(combat),
          m_scan(view),
          m_steady_tesla(steady_tesla) {}

    // Says what is on, for a player's log.
    void start();

    // Once a game tick (`tick` the mod's count).
    void tick(std::uint32_t tick);

    // 0.6.13: moves the clock to `tick` before the other parts' ticks, so the marks the perks
    // make in this tick (the reach's) are read with the same clock.
    void set_tick(std::uint32_t tick) { m_tick = tick; }

    // Returns whether the moby at `victim` is a decoy.
    bool is_decoy(std::uint32_t victim) const;

    // Returns what an enemy's hit on a decoy is multiplied by: the enemies' damage on the curve.
    float decoy_hit_scale() const;

    // A hit posted through `post` to `victim` from the request at `request` (Combat::post_hit):
    // returns what its damage is multiplied by, kTeslaShare for the claw's own hit on a lock (and
    // keeps it, for the steady hits), else 1.
    float claw_hit(Combat::PostHitFn post, std::uint32_t victim, std::uint32_t request);

    // The marks of the enemies the Zodiac's pulse and reach took (zodiac_credit.h); the perks'
    // reach adds to them.
    ZodiacCredit& zodiac_credit() { return m_credit; }

    // What the Zodiac did since the last summary (zodiac_credit.h, 0.6.12); the reach and the
    // deaths' awards add to it, NgPlusPlus logs and clears it once a minute.
    ZodiacStats& zodiac_stats() { return m_stats; }

    // A death recorded (Combat::record_death): when the Zodiac took the moby at `victim`, its health
    // record's last-weapon byte names the Zodiac, so the game's own award gives it the experience.
    void credit_kill(std::uint32_t victim);

private:
    // One of the claw's locks.
    struct Lock {
        std::uint32_t next = 0;  // The tick of its next steady hit.
        std::uint32_t seen = 0;  // The tick it was last a lock.
    };

    // A boss the Zodiac drains.
    struct Drained {
        std::uint32_t tick = 0;  // The tick it was last seen pulsed.
        float health = 0.0f;     // Its health then, after the mod's drain.
    };

    // Returns whether the tuning acts now: in a level, not a party's guest.
    bool active() const;

    // Returns the game address of the health record of the moby at `moby` (its +0x68's first
    // word, as moby_get_private_value_0, func_002AB1F0), 0 for none.
    std::uint32_t health_record(std::uint32_t moby) const;

    // The claw's locks and their steady hits.
    void tesla();

    // Posts one steady hit on the lock at `lock` from the claw at `claw`.
    void steady_hit(std::uint32_t lock, std::uint32_t claw);

    // The Zodiac's pulse on the bosses.
    void zodiac();

    // Returns whether the moby at `victim` is within a pulsing Zodiac gun's reach now.
    bool near_pulse(std::uint32_t victim);

    // The Shield Charger's and Tesla Barrier's charge and arc count, stretched by the tier.
    void barrier();

    // The decoys that appeared.
    void decoys();

    openrac::Mod& m_mod;
    const GameView& m_view;
    const Weapons& m_weapons;
    Combat& m_combat;
    MobyScan m_scan;
    bool m_steady_tesla;

    std::uint32_t m_tick = 0;
    int m_level = -1;
    ZodiacCredit m_credit;  // The enemies the Zodiac took.
    ZodiacStats m_stats;    // What it did since the last summary.
    std::vector<std::uint32_t> m_pulsing;  // The guns pulsing last tick, for the count.

    Combat::PostHitFn m_post = nullptr;           // Moby_PostHit's original, from the claw's hit.
    std::array<std::uint8_t, 0x40> m_claw_hit{};  // The claw's last own hit's request.
    float m_claw_damage = 0.0f;                   // Its damage as the game posted it.
    std::unordered_map<std::uint32_t, Lock> m_locks;
    std::unordered_map<std::uint32_t, Drained> m_drained;
    std::unordered_map<std::uint32_t, int> m_decoys;  // Decoys seen: address to class.
    // The barrier being stretched.
    bool m_barrier_up = false;
    float m_barrier_shown = 0.0f;    // The charge as the mod last left it.
    float m_barrier_virtual = 0.0f;  // The charge it would have had with no stretch.
    int m_barrier_hits = 0;          // The arcs counted, unstretched.
    int m_barrier_seen = 0;          // The count as the mod last left it.
    bool m_logged_barrier = false;
    int m_steady = 0;                                 // Steady hits since the last summary.
    bool m_logged_tesla = false;                      // The first of each was logged.
    bool m_logged_zodiac = false;
    bool m_logged_decoy = false;
    std::uint32_t m_summary_tick = 0;
};

}  // namespace ng_plus_plus
