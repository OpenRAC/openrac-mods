// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The weapons' perks in the game (README.md, "What each tier does"; the rules are perk_rules.h's).
//
// How a perk deals its damage: OpenRAC's moby_hit is rac1's only before 1.29 (rac2's port had no
// post_hit and never ran the update gate, as of develop 6825416d5), so the mod posts its extra hits as the game
// posts every hit: through Moby_PostHit's original (QueueMobyImpact), with the request of the hit
// that set the perk off, its damage set to the perk's share, as the game posts one request to
// several victims. A perk that acts later (a field, a shatter, a decoy's blast, the shield's zaps)
// posts a copy of the weapon's last request from room on the game's stack, its source the hero.
//
// The Ice Gun's freeze and the other statuses (the Plasma Coil's slow, the Tesla Claw's and the
// Walloper's stun) are OpenRAC 1.29's moby_status (shots.h): the port's moby loop leaves the
// enemy's update out, and runs it once for each new hit, so it is hurt and dies. Before 1.29 an
// Ice Gun's enemy only glows and shatters. Bosses only take the glow. With 1.29 the RYNO II's split
// rockets, the Gravity Bomb's mini bombs and the Visibomb's minis are the game's own shots, and
// the blasts (a shatter, a decoy, a sheep, the shield, Volatile) the game's own rocket blast in the
// weapon's colour.
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
#include "zodiac_credit.h"

namespace ng_plus_plus {

class Shots;

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

    // The 1.29 perks (shots.h): statuses, the game's blasts, real shots in place of the hits that
    // stood for them.
    void set_shots(Shots* shots) { m_shots = shots; }

    // Where the Zodiac's reach marks the enemies it takes (zodiac_credit.h): their kills earn the
    // Zodiac's experience.
    void set_zodiac_credit(ZodiacCredit* credit) { m_credit = credit; }

    // Where the reach counts what it does, for the Zodiac's summary (zodiac_credit.h).
    void set_zodiac_stats(ZodiacStats* stats) { m_zodiac_stats = stats; }

    // The share of the game's hit each of the Tesla Claw's hits deals now (0.5.1's steady hits,
    // tuning.h): its Omega storm stays as strong as the game's hits made it.
    void set_claw_share(float share) { m_claw_share = share > 0.0f ? share : 1.0f; }

    // Returns `item`'s stage now (overdrive raises it), Stage::None with no tier.
    Stage stage_now(int item) const;

    // An enemy a shot may fly at: its slot, address and place.
    struct Target {
        int slot = -1;
        std::uint32_t address = 0;
        std::array<float, 3> position{};
    };

    // Returns the `count` nearest enemies to `at` within `radius`, nearest first, leaving out the
    // one in `skip_slot`.
    std::vector<Target> targets(
        const std::array<float, 3>& at, float radius, int count, int skip_slot
    );

    // Returns whether the moby at `address` is alive and of class `moby_class`, and where it is.
    bool alive(std::uint32_t address, int moby_class) const {
        return m_scan.alive(address, moby_class);
    }

    std::array<float, 3> position_of(std::uint32_t address) const {
        return m_scan.position(address);
    }

    // Posts `item`'s last hit (else the last weapon hit) on the enemy at `target`: a homing shot
    // that reached it unhit (shots.cpp). False with nothing to post.
    bool strike(std::uint32_t target, int item);

    // Returns whether a shot may fly at the moby at `address` now (homing.h), and the height above
    // its position a shot aims at.
    bool targetable(std::uint32_t address) const { return m_scan.targetable(address); }
    float aim_height(std::uint32_t address) const { return m_scan.aim_height(address); }

private:
    // An enemy the Ice Gun slowed or froze.
    struct Frozen {
        int moby_class = 0;
        int slot = -1;
        std::uint32_t chill_until = 0;   // Ticks (the mod's count) it is slowed to.
        std::uint32_t freeze_until = 0;  // ... and frozen solid to.
        std::uint32_t slowed_until = 0;  // The slow sent lasts to.
        int chill_every = 1;             // Its slow: one tick in this many.
        bool boss = false;               // A boss: the glow only.
        bool drawn = false;              // Its glow has been sent.
        bool drawn_frozen = false;       // Its glow is the frozen one now.
        int tier = 0;                    // The Ice Gun's tier that hit it last.
        float last_hit = 0.0f;           // The damage of that hit.
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
        int blast_item = -1;     // The game's blast there in this weapon's look (1.29), -1 none.
        int ignite_tier = 0;     // The fire path's: it sets what it hits on fire at this tier.
        int item = -1;           // The weapon whose hit it is, for the kill's experience (-1: blast_item's).
    };

    // An enemy the fire path's Lava Gun set burning.
    struct Burning {
        int moby_class = 0;
        std::uint32_t until = 0;  // Ticks (the mod's count) it burns to.
        std::uint32_t next = 0;   // Its next pulse.
        float pulse = 0.0f;       // The damage of a pulse.
        float last_hit = 0.0f;    // The meteor's hit that set it burning last.
        int tier = 0;             // The Lava Gun's tier then.
        std::uint32_t splash_next = 0;  // Its next meteor hit that splashes.
    };

    // A field left at a place.
    struct Field {
        std::array<float, 3> at{};
        float radius = 0.0f;
        float damage = 0.0f;
        std::uint32_t until = 0;
        std::uint32_t next = 0;
        int item = -1;  // The weapon whose hit left it, for the kill's experience.
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
        bool pulsing = false;    // The Zodiac's gun: its pulse under way.
    };

    // The Zodiac's pulse beyond the game's: every enemy within zodiac_reach of the gun at
    // `gun` in sight of it that the game's pick left out (tuning_rules.h), taken the gun's own way
    // (0.6.12): one at 100 health or less, or drained to nothing, has its death recorded with the
    // Zodiac named and is vaporized (or left to its owner, by its death path); the rest lose a
    // pulse's drain off their health. No hit is posted.
    void zodiac_reach(const std::array<float, 3>& gun);

    // The pulse's sparks on an enemy it vaporizes at `at`, facing `eye` (spark_effect_spawn's
    // class 5022, kind 0, as the gun makes them). False when the pool or the class has none.
    bool zodiac_spark(const std::array<float, 3>& at, const std::array<float, 3>& eye);

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

    // Each chilled enemy: its slow after the freeze, its glow, its thaw.
    void thaw();

    // The fire path's meteor hit on `victim` at tier `tier` (0.5.1): its burn, the splash, the
    // storm. `splash` false for an enemy set on fire by a splash or an eruption (no splash again).
    void ignite(const ScannedMoby& victim, int tier, float damage, bool splash);

    // Each burning enemy: its pulses, its end.
    void burn();

    // Slows `f`'s enemy for the chill it has left (OpenRAC 1.29).
    void slow(Frozen& f);

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
    Shots* m_shots = nullptr;   // The 1.29 perks, or none.
    ZodiacCredit* m_credit = nullptr;  // The Zodiac's kill marks, or none.
    float m_claw_share = 1.0f;  // The Tesla Claw's hits' share of the game's (set_claw_share).
    int m_item_now = -1;        // The weapon whose hit's perks are being dealt.
    int m_post_item = -1;       // The weapon a later post (field, blast, pulse) is for, or -1.
    int m_posted = 0;           // Hits posted since the last summary.
    std::uint32_t m_summary_tick = 0;
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
    std::unordered_map<std::uint32_t, Burning> m_burning;
    std::unordered_map<std::uint32_t, Watched> m_watched;
    std::unordered_map<std::uint32_t, int> m_arena_seen;  // Arena enemies seen: address to class.
    std::vector<Later> m_later;
    std::vector<Field> m_fields;
    std::vector<std::array<float, 3>> m_reach_guns;    // Guns whose pulse began this tick.
    bool m_logged_reach = false;
    bool m_logged_no_record = false;
    // Moby_RecordDeath's entry (SetDeathBits as the game calls it, the mods' hooks first).
    void (*m_record_death)(std::uint32_t, std::int32_t, std::int32_t) = nullptr;
    ZodiacStats* m_zodiac_stats = nullptr;  // The Zodiac's summary (tuning.h).
    ZodiacStats m_zodiac_scratch;           // Where the counts go without one (the tests).

    // Returns the Zodiac's summary to count in.
    ZodiacStats& zodiac_stats_or_scratch() {
        return m_zodiac_stats != nullptr ? *m_zodiac_stats : m_zodiac_scratch;
    }

    int m_kill_item = -1;  // The weapon of the kills in a row, and how many.
    int m_kills = 0;
    int m_overdrive_item = -1;  // The weapon in overdrive, and until when.
    std::uint32_t m_overdrive_until = 0;
    bool m_logged = false;       // The first perk hit was logged.
    bool m_logged_fire = false;  // The first burn was logged.
};

}  // namespace ng_plus_plus
