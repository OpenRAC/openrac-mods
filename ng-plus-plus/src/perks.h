// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The weapons' perks in the game (README.md, "What each tier does"; the rules are perk_rules.h's).
//
// How a perk deals its damage: OpenRAC's moby_hit is rac1's only (rac2's port has no post_hit and
// never runs the update gate, as of develop 6825416d5), so the mod posts its extra hits as the game
// posts every hit: through Moby_PostHit's original (QueueMobyImpact), with the request of the hit
// that set the perk off, its damage set to the perk's share, as the game posts one request to
// several victims. A perk that acts later (a field, a shatter, a decoy's blast, the shield's zaps)
// posts a copy of the weapon's last request from room on the game's stack, its source the hero.
//
// The Ice Gun's freeze: the game keeps no status a mod can set, so a slowed enemy's update runs one
// tick in a few: on the others its update word (+0x64) is 0 (the moby loop runs nothing, as for
// OpenRAC's own figures) and, while frozen solid, its animation stands (FLAG_NO_ANIMATION). The game
// keeps one pending hit per enemy, the stronger, so a hit on a slowed enemy counts for the ticks it
// misses (ice_hit_scale). Bosses only take the glow.
//
// Only the game's thread uses it; nothing in a party guest's game (the host's perks are the
// party's).

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "bolt_api.h"
#include "elites.h"
#include "game_view.h"
#include "moby_scan.h"
#include "openrac/mod.hpp"
#include "perk_rules.h"
#include "weapons.h"

namespace ng_plus_plus {

// The perks.
class Perks {
public:
    // Moby_PostHit's original: (victim, request).
    using PostHitFn = void (*)(std::uint32_t, std::uint32_t);

    // The perks of `weapons` in `view`'s game through `mod`, when `on` (the "weapon_perks"
    // setting); elites drawn again by `elites` when the Ice Gun's glow leaves one.
    Perks(openrac::Mod& mod, const GameView& view, const Weapons& weapons, Elites& elites, bool on)
        : m_mod(mod),
          m_view(view),
          m_weapons(weapons),
          m_elites(elites),
          m_scan(view),
          m_on(on) {}

    // Finds moby_material and says what this build lacks.
    void start();

    // Returns whether perks act in this game now: the setting on, not a party's guest.
    bool active() const;

    // Returns what a hit's damage on `victim` is multiplied by before the game reads it: an Ice
    // Gun's slowed enemy reads one hit for the ticks it misses.
    float hit_scale(std::uint32_t victim) const;

    // A hit landed (Moby_PostHit, after the game's call): `post` the original, on `victim` through
    // the request at `request`, from `item` (-1 none) whose source moby is `source`, `damage` the
    // damage it dealt (after the tier and the curve). Sets off the weapon's perks.
    void hit(
        PostHitFn post,
        std::uint32_t victim,
        std::uint32_t request,
        int item,
        std::uint32_t source,
        float damage
    );

    // A moby's death (Moby_RecordDeath, before the game's): shatters, overdrive, Volatile.
    void death(std::uint32_t moby);

    // Hits every enemy within `radius` of `at` for `share` of the last weapon hit's damage, at the
    // next tick (the armour's thorns). Returns false with nothing to scale it by yet.
    bool burst_later(const std::array<float, 3>& at, float radius, float share);

    // Returns the hero's position now.
    std::array<float, 3> hero_position() const;

    // Once a game tick (`tick` the mod's count).
    void tick(std::uint32_t tick);

private:
    // An enemy the Ice Gun slowed or froze.
    struct Frozen {
        int moby_class = 0;
        std::uint32_t saved_update = 0;  // Its update word while the mod holds it at 0.
        std::uint32_t chill_until = 0;   // Ticks (the mod's count) it is slowed to.
        std::uint32_t freeze_until = 0;  // ... and frozen solid to.
        int chill_every = 1;
        int freeze_every = 1;
        bool anim_held = false;     // The mod set its FLAG_NO_ANIMATION.
        bool boss = false;          // A boss: the glow only.
        bool drawn = false;         // Its glow has been sent.
        bool drawn_frozen = false;  // Its glow is the frozen one now.
        int tier = 0;               // The Ice Gun's tier that hit it last.
        float last_hit = 0.0f;      // The damage of that hit.
    };

    // A hit the mod posts later, at a place: every enemy within `radius` of `at`.
    struct Later {
        std::array<float, 3> at{};
        float radius = 0.0f;
        float damage = 0.0f;
        std::uint32_t skip = 0;  // A moby left out (the one that set it off).
        int freeze_tier = 0;     // The Ice Gun's shards: they freeze what they hit at this tier.
        std::uint32_t when = 0;  // The tick it goes off.
        int fan = 0;             // Not every enemy within it: the `fan` nearest only.
    };

    // A field left at a place.
    struct Field {
        std::array<float, 3> at{};
        float radius = 0.0f;
        float damage = 0.0f;
        std::uint32_t until = 0;
        std::uint32_t next = 0;
    };

    // A weapon's moby the perks watch (a decoy, a sheep, the shield, the Visibomb's missile, the
    // Gravity Bomb's blast, the Tesla Claw's beam).
    struct Watched {
        int moby_class = 0;
        int item = 0;
        std::array<float, 3> at{};
        std::uint32_t born = 0;
        std::uint32_t seen = 0;
        std::uint32_t next = 0;  // Its next zap or pulse.
    };

    // Returns `item`'s stage now (overdrive raises it), Stage::None with no tier.
    Stage stage_now(int item) const;

    // Posts a hit of `damage` on `target`: through the hit under way when there is one, else a
    // copy of the last request. False with no budget, no request or no original.
    bool post(std::uint32_t target, float damage);

    // Returns the nearest enemy to `at` within `radius` not in `visited`, or null.
    const ScannedMoby* nearest(
        const std::array<float, 3>& at, float radius, const std::vector<std::uint32_t>& visited
    ) const;

    // Hits every enemy within `radius` of `at` but `skip` for `damage`; the fan nearest only when
    // `fan` is above 0. Returns those hit.
    std::vector<std::uint32_t> burst(
        const std::array<float, 3>& at, float radius, float damage, std::uint32_t skip, int fan = 0
    );

    // The perks of a weapon hit on `victim` (`perk` its rules, `nth` a strong hit).
    void apply(const HitPerk& perk, const ScannedMoby& victim, float damage, bool nth);

    // The Ice Gun's hit on `victim` at tier `tier`.
    void chill(const ScannedMoby& victim, int tier, float damage);

    // Each slowed enemy's update held or let run this tick.
    void hold_frozen();

    // The watched mobys: what appeared, what went, the zaps.
    void watch();

    // Draws `moby` with energy `level` in `rgb`, or as it was (`rgb` none).
    void draw(std::uint32_t moby, int level, std::uint32_t rgb) const;
    void undraw(std::uint32_t moby) const;

    openrac::Mod& m_mod;
    const GameView& m_view;
    const Weapons& m_weapons;
    Elites& m_elites;
    MobyScan m_scan;
    bool m_on;
    MobyMaterialFn m_material = nullptr;
    bool m_energy = false;

    PostHitFn m_post = nullptr;                   // Moby_PostHit's original, from the first hit.
    std::uint32_t m_request = 0;                  // The hit under way's request, 0 outside one.
    std::array<std::uint8_t, 0x40> m_snapshot{};  // The last weapon hit's request.
    bool m_have_snapshot = false;
    float m_reference = 0.0f;  // The last weapon hit's damage.
    std::array<float, kItems> m_last_damage{};
    std::array<std::uint32_t, kItems> m_cooldown{};
    std::array<std::uint32_t, kItems> m_count{};
    int m_budget = 0;  // Hits the mod may still post this tick.

    std::uint32_t m_tick = 0;
    int m_level = -1;
    std::unordered_map<std::uint32_t, Frozen> m_frozen;
    std::unordered_map<std::uint32_t, Watched> m_watched;
    std::unordered_map<std::uint32_t, int> m_arena_seen;  // Arena enemies seen: address to class.
    std::vector<Later> m_later;
    std::vector<Field> m_fields;

    int m_kill_item = -1;  // The weapon of the kills in a row, and how many.
    int m_kills = 0;
    int m_overdrive_item = -1;  // The weapon in overdrive, and until when.
    std::uint32_t m_overdrive_until = 0;
    bool m_logged = false;  // The first perk hit was logged.
};

}  // namespace ng_plus_plus
