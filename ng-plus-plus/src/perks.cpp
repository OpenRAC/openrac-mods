// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The weapons' perks in the game (perks.h).

#include "perks.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "armour_api.h"
#include "discs.h"
#include "openrac/games/rac2.hpp"
#include "shots.h"
#include "tuning_rules.h"
#include "weapon_visuals.h"

namespace ng_plus_plus {
namespace {

// The most hits the mod posts in one tick: a long chain reaction waits for the next ticks.
constexpr int kPostsPerTick = 192;

// The most hits waiting for later at once.
constexpr std::size_t kLaterMost = 256;
constexpr std::size_t kFieldsMost = 96;

// The bytes of a hit request the mod copies (make_hit_descriptor's and its callers' fields).
constexpr std::uint32_t kRequestBytes = 0x40u;

// The watched mobys' classes and their weapons.
struct WatchedClass {
    int moby_class;
    int item;
};

constexpr WatchedClass kWatchedClasses[] = {
    {203, 17},   // the decoy
    {3709, 16},  // a sheep
    {3760, 16},  // a black sheep
    {2652, 45},  // the Shield Charger's shield
    {4498, 45},  // the Tesla Barrier
    {172, 14},   // the Visibomb's missile
    {3070, 42},  // the Gravity Bomb's blast
    {3301, 42},  // its other blast
    {177, 18},   // the Tesla Claw's beam
    {3784, 43},  // the Zodiac's gun (its pulse posts no hit: tuning.h)
};

// Returns the weapon of watched class `moby_class`, or -1.
int watched_item(int moby_class) {
    // The table is short.
    for (const WatchedClass& watched : kWatchedClasses) {
        if (watched.moby_class == moby_class) {
            return watched.item;
        }
    }

    return -1;
}

// The shield's zaps, the Visibomb's trail and the Tesla Claw's storm: ticks apart. Every time in
// NG++ is in sixtieths of a second (NTSC-U's ticks), turned into the disc's ticks where it is used
// (GameView::ticks): PAL's game ticks fifty times a second.
constexpr int kShieldZapTicks = 60;
constexpr int kTrailTicks = 15;
constexpr int kStormTicks = 20;

// The Zodiac's sparks (spark_effect_spawn, func_L00_00412928's file): their class, how long they
// last, and the most the reach makes in one pulse (the pool's room is the bolts' too).
constexpr int kSparkClass = 5022;
constexpr std::int32_t kSparkLife = 60;
constexpr int kReachSparksMost = 24;

// The most enemies the reach takes in one pulse.
constexpr int kReachMost = 96;

// A moby's fields the sparks set as spark_effect_spawn does: its update distance (+0x30, 255
// always run), its draw distance (+0x32), its state (+0x20), its scale (+0x1C), its three axes
// (+0xC0, +0xD0, +0xE0) and its pvars (+0x68: life, kind, parent).
constexpr std::uint32_t kMobyUpdateDistance = 0x30u;
constexpr std::uint32_t kMobyDrawDistance = 0x32u;
constexpr std::uint32_t kMobyScale = 0x1Cu;
constexpr std::uint32_t kMobyAxes = 0xC0u;
constexpr std::uint32_t kMobyPvars = 0x68u;

// The health record's aim height (+0x10): the pulse's sparks rise 1.5 times it.
constexpr std::uint32_t kHealthAim = 0x10u;

// Returns a × b.
std::array<float, 3> cross(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// The Zodiac's Omega fields: at most this many from one pulse, on the enemies nearest the gun.
constexpr int kZodiacFieldsMost = 8;

// A fire-path meteor's splash and storm count rest this long on one enemy (the lingering blobs
// hit every few ticks).
constexpr int kSplashRestTicks = 6;

// A freeze comes again only this long after the last one thawed: half a second, so a stream holds
// an enemy frozen most of the time, never for good.
constexpr int kRefreezeTicks = 30;

// The rim glow of a frozen enemy's paint (0 to 15).
constexpr int kFrozenPaintLevel = 12;

// The Ice Gun's shatter glow on a dying enemy.
constexpr std::uint32_t kShatterRgb = 0xD0F0FFu;

}  // namespace

void Perks::start() {
    m_material = moby_material_of(m_mod.api());
    m_energy = m_material != nullptr && m_mod.api().api_minor >= kEnergyMinor;
    m_paint = m_material != nullptr && m_mod.api().api_minor >= kPaintMinor;
    m_paint_effects = m_paint && m_mod.api().api_minor >= kPaintEffectsMinor;

    // SetDeathBits as the game calls it, for the Zodiac's reach (a table without the entry, as a
    // test's, has none).
    const bool entry = m_mod.has_hook_entry() && m_mod.api().hook_entry != nullptr;
    m_record_death = entry ? m_mod.hook_entry<openrac::rac2::Moby_RecordDeath>() : nullptr;

    // What the setting and the build leave out, for a player's log.
    if (!m_on) {
        m_mod.info("weapon perks are off (setting)");
    } else if (!m_energy) {
        m_mod.info("the Ice Gun's glow and the shatters' colour need OpenRAC 1.25; the perks act");
    }
}

bool Perks::active() const {
    return m_on && !m_weapons.tiers().party_on();
}

Stage Perks::stage_now(int item) const {
    // Not an item.
    if (item < 0 || item >= kItems) {
        return Stage::None;
    }

    const WeaponView weapon = m_weapons.weapon(item);

    // No tier worn now.
    if (!weapon.has_tier()) {
        return Stage::None;
    }

    const Stage stage = stage_of(weapon.kind, weapon.level);
    const bool overdrive = item == m_overdrive_item && m_tick < m_overdrive_until;
    return overdrive ? raised(stage) : stage;
}

std::array<float, 3> Perks::hero_position() const {
    const std::uint32_t hero = m_view.hero_moby();
    return GameView::is_memory(hero, kMobyRecordBytes) ? m_scan.position(hero)
                                                       : std::array<float, 3>{};
}

bool Perks::post(std::uint32_t target, float damage) {
    // Nothing to post with, or this tick's hits are spent.
    if (m_post == nullptr || m_budget <= 0 || damage <= 0.0f) {
        return false;
    }

    --m_budget;
    ++m_posted;
    const openrac::Memory memory = m_view.memory();

    // The kill is the weapon's whose perk this is: the game names the weapon in hand for a hit
    // posted later, so the victim is marked and the death recorded under the right item.
    const int credit_item = m_item_now >= 0 ? m_item_now : m_post_item;

    if (m_credit != nullptr && credit_item >= 0) {
        m_credit->mark(target, m_tick, credit_item, 0);
    }

    // Inside a hit: its own request, as the game posts one request to several victims.
    if (m_request != 0) {
        const auto before = memory.read<float>(m_request + kHitDamage);
        memory.write(m_request + kHitDamage, damage);
        m_post(target, m_request);
        memory.write(m_request + kHitDamage, before);
        return true;
    }

    // Later: a copy of the last request on the game's stack, from the hero (the shot that made it
    // may be gone).
    if (!m_have_snapshot) {
        return false;
    }

    openrac_gaddr saved = 0;
    const openrac_gaddr room = m_mod.api().stack_push(kRequestBytes, &saved);
    std::memcpy(memory.pointer(room), m_snapshot.data(), kRequestBytes);
    memory.write(room + kHitDamage, damage);
    memory.write(room + kHitSource, m_view.hero_moby());
    m_post(target, room);
    m_mod.api().stack_pop(saved);
    return true;
}

const ScannedMoby* Perks::nearest(
    const std::array<float, 3>& at, float radius, const std::vector<std::uint32_t>& visited
) const {
    const ScannedMoby* best = nullptr;
    float best_d2 = radius * radius;
    const std::uint32_t hero = m_view.hero_moby();

    // Each live enemy not hit yet.
    for (const ScannedMoby& moby : m_scan.mobys()) {
        // Not an enemy, the hero, or already hit.
        if (!moby.enemy || moby.address == hero
            || std::find(visited.begin(), visited.end(), moby.address) != visited.end()) {
            continue;
        }

        const float d2 = distance2(moby.position, at);

        // Nearer than the best so far, and one a shot may fly at: alive, health left, shown (not
        // held hidden at its spawn point, still underground or dying: homing.h).
        if (d2 <= best_d2 && m_scan.targetable(moby.address)) {
            best_d2 = d2;
            best = &moby;
        }
    }

    return best;
}

std::vector<std::uint32_t> Perks::burst(
    const std::array<float, 3>& at, float radius, float damage, std::uint32_t skip, int fan
) {
    std::vector<std::uint32_t> hit;

    // The fan nearest only.
    if (fan > 0) {
        std::vector<std::uint32_t> visited{skip};

        // One at a time, nearest first.
        for (int n = 0; n < fan; ++n) {
            const ScannedMoby* next = nearest(at, radius, visited);

            // No one left in reach.
            if (next == nullptr) {
                break;
            }

            visited.push_back(next->address);

            // Posted.
            if (post(next->address, damage)) {
                hit.push_back(next->address);
            }
        }

        return hit;
    }

    const float r2 = radius * radius;
    const std::uint32_t hero = m_view.hero_moby();

    // Every enemy within the radius.
    for (const ScannedMoby& moby : m_scan.mobys()) {
        // Not an enemy, the hero, the one left out, or out of reach.
        if (!moby.enemy || moby.address == hero || moby.address == skip
            || distance2(moby.position, at) > r2) {
            continue;
        }

        // Posted.
        if (post(moby.address, damage)) {
            hit.push_back(moby.address);
        }
    }

    return hit;
}

void Perks::hit(
    PostHitFn post_fn,
    std::uint32_t victim,
    std::uint32_t request,
    int item,
    std::uint32_t source,
    float damage
) {
    // Off, a guest, or no weapon's hit.
    if (!active() || post_fn == nullptr || request == 0 || item < 0 || item >= kItems
        || damage <= 0.0f) {
        return;
    }

    // Only the hero's own hits and his weapons' mobys' set perks off: an enemy's shot is neither.
    const std::uint32_t hero = m_view.hero_moby();
    const std::optional<int> source_class = m_view.moby_class(source);
    const bool from_hero =
        source == hero || source == 0 || (source_class && visual_class(*source_class) != nullptr);

    // Another's hit.
    if (!from_hero) {
        return;
    }

    m_post = post_fn;
    m_reference = damage;
    m_last_damage[static_cast<std::size_t>(item)] = damage;

    // The request kept for perks that act later.
    if (GameView::is_memory(request, kRequestBytes)) {
        std::memcpy(m_snapshot.data(), m_view.memory().pointer(request), kRequestBytes);
        m_have_snapshot = true;
    }

    m_scan.read(m_tick);
    const ScannedMoby* found = m_scan.find(victim);

    // Not an enemy: a crate, a door. No perks.
    if (found == nullptr || !found->enemy) {
        return;
    }

    const ScannedMoby target = *found;
    const WeaponView weapon = m_weapons.weapon(item);

    // No tier: the game's weapon.
    if (!weapon.has_tier()) {
        return;
    }

    // The Ice Gun from Alpha on, or the fire path's meteors (the "lava_gun" setting).
    if (item == kLavaGun) {
        const int tier = tier_of(weapon.kind, weapon.level);

        if (ice_gun_on()) {
            chill(target, tier, damage);
        } else {
            m_request = request;
            m_item_now = item;
            ignite(target, tier, damage, true);
            m_item_now = -1;
            m_request = 0;
        }

        return;
    }

    const Stage stage = stage_now(item);
    const bool overdrive = item == m_overdrive_item && m_tick < m_overdrive_until;
    const int ryno_tier = std::min(tier_of(weapon.kind, weapon.level) + (overdrive ? 3 : 0), 9);
    const bool real_shots = m_shots != nullptr && m_shots->active();

    // The RYNO II's split rockets are the game's own with 1.29 (shots.cpp): no hits stand for them.
    if (weapon.kind == Kind::Ryno && real_shots) {
        return;
    }

    const HitPerk perk = weapon.kind == Kind::Ryno ? ryno_perk(ryno_tier) : hit_perk(item, stage);
    const auto n = static_cast<std::size_t>(item);

    // Nothing at this tier, or the weapon's perks rest.
    if (!perk.any() || m_cooldown[n] > m_tick) {
        return;
    }

    m_cooldown[n] = m_tick + static_cast<std::uint32_t>(m_view.ticks(perk.cooldown));
    ++m_count[n];
    const bool nth = perk.nth > 0 && m_count[n] % static_cast<std::uint32_t>(perk.nth) == 0;

    // The posts go through this hit's own request.
    m_request = request;
    m_item_now = item;
    apply(perk, target, damage, nth);
    m_item_now = -1;
    m_request = 0;

    // Once, for a player's log.
    if (!m_logged) {
        m_logged = true;
        m_mod.info("first weapon perk: item " + std::to_string(item));
    }
}

void Perks::apply(const HitPerk& perk, const ScannedMoby& victim, float damage, bool nth) {
    std::vector<std::uint32_t> visited{victim.address};

    // The chain, hop by hop from the victim (a strong hit's longer).
    const int hops = nth && perk.nth_hops > 0 ? perk.nth_hops : perk.hops;
    float share = nth && perk.nth_hops > 0 ? perk.nth_share : perk.hop_share;
    std::array<float, 3> from = victim.position;

    for (int hop = 0; hop < hops; ++hop) {
        const ScannedMoby* next = nearest(from, perk.hop_radius, visited);

        // No one left in reach: the chain ends here.
        if (next == nullptr) {
            break;
        }

        visited.push_back(next->address);
        post(next->address, damage * share);

        // Slowed or stunned by it (1.29).
        if (perk.status != Status::None && m_shots != nullptr) {
            m_shots->status(next->slot, perk.status, m_view.ticks(perk.status_ticks));
        }

        from = next->position;
        share *= perk.hop_decay;
    }

    // The chain's last jump explodes.
    if (perk.end_radius > 0.0f && visited.size() > 1) {
        burst(from, perk.end_radius, damage * perk.end_share, visited.back());

        if (m_shots != nullptr) {
            m_shots->blast(from, m_item_now);
        }
    }

    // The fan, and the fan again from each it reached.
    if (perk.fan > 0) {
        const std::vector<std::uint32_t> first = burst(
            victim.position,
            perk.fan_radius,
            damage * perk.fan_share,
            victim.address,
            std::min(perk.fan, kFanMost)
        );
        int total = static_cast<int>(first.size());

        // Each split rocket splits again.
        for (const std::uint32_t split : first) {
            const ScannedMoby* at = m_scan.find(split);

            // Gone, or the fan is full.
            if (at == nullptr || perk.fan_again <= 0 || total >= kFanMost) {
                continue;
            }

            const int again = std::min(perk.fan_again, kFanMost - total);
            total += static_cast<int>(
                burst(at->position, perk.fan_radius, damage * perk.fan_share * 0.5f, split, again)
                    .size()
            );
        }
    }

    // The burst around the victim (on the strong hits only, for an explosive pulse).
    if (perk.burst_radius > 0.0f && (!perk.burst_on_nth || nth)) {
        const std::vector<std::uint32_t> hit =
            burst(victim.position, perk.burst_radius, damage * perk.burst_share, victim.address);

        // Staggered by it (1.29).
        for (const std::uint32_t moby : hit) {
            const ScannedMoby* at = m_scan.find(moby);

            if (perk.status != Status::None && m_shots != nullptr && at != nullptr) {
                m_shots->status(at->slot, perk.status, m_view.ticks(perk.status_ticks));
            }
        }

        // An explosive pulse is the game's blast (1.29).
        if (perk.burst_on_nth && m_shots != nullptr) {
            m_shots->blast(victim.position, m_item_now);
        }
    }

    // Through the victim, away from the hero.
    if (perk.pierce > 0 && (perk.nth == 0 || nth || perk.nth_hops == 0)) {
        const std::array<float, 3> hero = hero_position();
        std::array<float, 3> dir{
            victim.position[0] - hero[0], victim.position[1] - hero[1], victim.position[2] - hero[2]
        };
        const float length = std::sqrt(distance2(victim.position, hero));

        // The hero inside the victim: no line.
        if (length > 0.01f) {
            for (float& c : dir) {
                c /= length;
            }

            std::vector<std::pair<float, std::uint32_t>> behind;

            // Each enemy behind the victim, in the cone.
            for (const ScannedMoby& moby : m_scan.mobys()) {
                // Not an enemy, or the victim.
                if (!moby.enemy || moby.address == victim.address) {
                    continue;
                }

                const std::array<float, 3> d{
                    moby.position[0] - victim.position[0],
                    moby.position[1] - victim.position[1],
                    moby.position[2] - victim.position[2]
                };
                const float along = d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2];
                const float d_len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

                // Behind, within reach and in the cone.
                if (along > 0.0f && along <= perk.pierce_range && d_len > 0.0f
                    && along / d_len >= kPierceCone) {
                    behind.emplace_back(along, moby.address);
                }
            }

            std::sort(behind.begin(), behind.end());

            // The nearest behind first.
            for (std::size_t k = 0; k < behind.size() && k < static_cast<std::size_t>(perk.pierce);
                 ++k) {
                post(behind[k].second, damage * perk.pierce_share);
            }
        }
    }

    // A field left at the victim's place.
    if (perk.field_ticks > 0 && m_fields.size() < kFieldsMost) {
        m_fields.push_back(
            Field{
                victim.position,
                perk.field_radius,
                damage * perk.field_share,
                m_tick + static_cast<std::uint32_t>(m_view.ticks(perk.field_ticks)),
                m_tick + static_cast<std::uint32_t>(m_view.ticks(kFieldPulseTicks)),
                m_item_now
            }
        );
    }
}

void Perks::chill(const ScannedMoby& victim, int tier, float damage) {
    const IceEffect ice = ice_effect(tier);

    // No tier: the game's lava.
    if (ice.chill_ticks <= 0) {
        return;
    }

    Frozen& f = m_frozen[victim.address];

    // A moby new to the table: its class kept, a boss known.
    if (f.moby_class != victim.moby_class) {
        f = Frozen{};
        f.moby_class = victim.moby_class;
        f.boss = is_boss(victim.moby_class);
    }

    f.slot = victim.slot;
    f.tier = tier;
    f.last_hit = damage;
    f.chill_every = ice.chill_every;
    f.chill_until =
        std::max(f.chill_until, m_tick + static_cast<std::uint32_t>(m_view.ticks(ice.chill_ticks)));
    bool fresh = false;

    // Frozen solid from Epsilon, a fresh freeze only once the last one has thawed into its chill,
    // so a stream holds it slowed, frozen again and again, never for good.
    if (ice.freeze_ticks > 0
        && m_tick >= f.freeze_until + static_cast<std::uint32_t>(m_view.ticks(kRefreezeTicks))) {
        f.freeze_until = m_tick + static_cast<std::uint32_t>(m_view.ticks(ice.freeze_ticks));
        fresh = true;
    }

    const bool frozen = m_tick < f.freeze_until && !f.boss;  // A boss only glows.
    const bool statuses = m_shots != nullptr && m_shots->active() && !f.boss;

    // The status (OpenRAC 1.29): frozen solid, else slowed, renewed while the chill lasts.
    if (statuses && fresh) {
        m_shots->status(f.slot, Status::Freeze, m_view.ticks(ice.freeze_ticks));
        f.slowed_until = 0;
    } else if (
        statuses && !frozen
        && f.slowed_until < m_tick + static_cast<std::uint32_t>(m_view.ticks(ice.chill_ticks / 2))
    ) {
        slow(f);
    }

    // Its look: frozen solid in ice, a blue glow while chilled.
    if (!f.drawn || f.drawn_frozen != frozen) {
        draw_ice(victim.address, ice.energy, frozen);
        f.drawn = true;
        f.drawn_frozen = frozen;
    }
}

void Perks::slow(Frozen& f) {
    const Status kind = f.chill_every >= 3 ? Status::SlowThird : Status::SlowHalf;
    const auto ticks = static_cast<int>(f.chill_until - m_tick);

    // Sent for the chill left.
    if (ticks > 0 && m_shots->status(f.slot, kind, ticks)) {
        f.slowed_until = f.chill_until;
    }
}

void Perks::ignite(const ScannedMoby& victim, int tier, float damage, bool splash) {
    const FireEffect fire = fire_effect(tier);

    // No tier: the game's meteors.
    if (fire.burn_ticks <= 0 || damage <= 0.0f) {
        return;
    }

    auto [it, fresh] = m_burning.try_emplace(victim.address);
    Burning& b = it->second;

    // A moby new to the table, or a new one in its slot: its first pulse a pulse from now.
    if (fresh || b.moby_class != victim.moby_class) {
        b = Burning{};
        b.moby_class = victim.moby_class;
        b.next = m_tick + static_cast<std::uint32_t>(m_view.ticks(kBurnPulseTicks));
        draw(victim.address, fire.energy, kBurningRgb);
    }

    // Once, for a player's log.
    if (!m_logged_fire) {
        m_logged_fire = true;
        m_mod.info("first meteor burn: tier " + std::to_string(tier));
    }

    b.until = m_tick + static_cast<std::uint32_t>(m_view.ticks(fire.burn_ticks));
    b.pulse = std::max(b.pulse, damage * fire.burn_share);
    b.last_hit = std::max(b.last_hit, damage);
    b.tier = tier;

    // A meteor's own hit: the splash, and at Omega the storm; resting on this enemy a few ticks.
    if (!splash || m_tick < b.splash_next) {
        return;
    }

    b.splash_next = m_tick + static_cast<std::uint32_t>(m_view.ticks(kSplashRestTicks));

    // The splash of magma: the enemies around the impact, set on fire too.
    if (fire.splash_radius > 0.0f) {
        const std::vector<std::uint32_t> hit =
            burst(victim.position, fire.splash_radius, damage * fire.splash_share, victim.address);

        for (const std::uint32_t moby : hit) {
            const ScannedMoby* at = m_scan.find(moby);

            // Still there.
            if (at != nullptr) {
                ignite(*at, tier, damage * fire.splash_share, false);
            }
        }
    }

    const auto n = static_cast<std::size_t>(kLavaGun);
    ++m_count[n];

    // The meteor storm: blasts falling around the victim, a little apart, each the game's own in
    // the gun's colour (1.29), each setting fire to what it hits.
    if (fire.storm_every > 0 && m_count[n] % static_cast<std::uint32_t>(fire.storm_every) == 0) {
        for (int k = 0; k < fire.storm_meteors && m_later.size() < kLaterMost; ++k) {
            const float angle =
                6.2831853f * static_cast<float>(k) / static_cast<float>(fire.storm_meteors);
            Later later;
            later.at = victim.position;
            later.at[0] += 3.5f * std::cos(angle);
            later.at[1] += 3.5f * std::sin(angle);
            later.radius = fire.storm_radius;
            later.damage = damage * fire.storm_share;
            later.when = m_tick + 10u + 8u * static_cast<std::uint32_t>(k);
            later.blast_item = kLavaGun;
            later.ignite_tier = tier;
            m_later.push_back(later);
        }
    }
}

void Perks::burn() {
    // Each burning enemy.
    for (auto it = m_burning.begin(); it != m_burning.end();) {
        const std::uint32_t moby = it->first;
        Burning& b = it->second;

        // Gone, or its slot holds another moby now.
        if (!m_scan.alive(moby, b.moby_class)) {
            it = m_burning.erase(it);
            continue;
        }

        // Burnt out: its glow gone.
        if (m_tick >= b.until) {
            undraw(moby);
            m_elites.redraw(moby);
            it = m_burning.erase(it);
            continue;
        }

        // A pulse of the burn.
        if (m_tick >= b.next && m_budget > 0) {
            b.next = m_tick + static_cast<std::uint32_t>(m_view.ticks(kBurnPulseTicks));
            m_post_item = kLavaGun;
            post(moby, b.pulse);
            m_post_item = -1;
        }

        ++it;
    }
}

void Perks::thaw() {
    // Each chilled enemy.
    for (auto it = m_frozen.begin(); it != m_frozen.end();) {
        const std::uint32_t moby = it->first;
        Frozen& f = it->second;

        // Gone, or its slot holds another moby now.
        if (!m_scan.alive(moby, f.moby_class)) {
            it = m_frozen.erase(it);
            continue;
        }

        const bool frozen = m_tick < f.freeze_until && !f.boss;  // A boss only glows.

        // Thawed: its glow gone (the port ends the status by itself).
        if (m_tick >= f.chill_until) {
            undraw(moby);
            m_elites.redraw(moby);
            it = m_frozen.erase(it);
            continue;
        }

        // Out of the freeze into the chill: slowed for what is left.
        if (!frozen && !f.boss && m_shots != nullptr && m_shots->active()
            && f.slowed_until < f.chill_until) {
            slow(f);
        }

        // Its look follows the freeze.
        if (f.drawn_frozen != frozen) {
            draw_ice(moby, ice_effect(f.tier).energy, frozen);
            f.drawn_frozen = frozen;
        }

        ++it;
    }
}

void Perks::death(std::uint32_t moby) {
    // Off, or a guest.
    if (!active()) {
        return;
    }

    const bool enemy = m_view.enemy_bolts(moby).has_value();
    const std::array<float, 3> at = GameView::is_memory(moby, kMobyRecordBytes)
                                        ? m_scan.position(moby)
                                        : std::array<float, 3>{};

    // An Ice Gun's victim shatters from Epsilon, its glow flaring as it goes.
    const auto frozen = m_frozen.find(moby);

    if (frozen != m_frozen.end()) {
        const Frozen f = frozen->second;
        const IceEffect ice = ice_effect(f.tier);
        m_frozen.erase(frozen);

        // The shards: the game's own blast in the Ice Gun's colour (1.29), and their hits.
        if (ice.shatter_radius > 0.0f && m_later.size() < kLaterMost) {
            draw(moby, kEffectTop, kShatterRgb);

            if (m_shots != nullptr) {
                m_shots->blast(at, kLavaGun);
            }

            Later later;
            later.at = at;
            later.radius = ice.shatter_radius;
            later.damage =
                f.last_hit * ice.shatter_share * 4.0f;  // A shard is a burst of a stream.
            later.skip = moby;
            later.freeze_tier = ice.shatter_freezes ? f.tier : 0;
            later.item = kLavaGun;
            later.when = m_tick + 1;
            m_later.push_back(later);
        }
    }

    // A burning enemy erupts from Epsilon (the fire path), setting its neighbours on fire.
    const auto burning = m_burning.find(moby);

    if (burning != m_burning.end()) {
        const Burning b = burning->second;
        const FireEffect fire = fire_effect(b.tier);
        m_burning.erase(burning);

        if (fire.erupt_radius > 0.0f && m_later.size() < kLaterMost) {
            draw(moby, kEffectTop, kBurningRgb);

            if (m_shots != nullptr) {
                m_shots->blast(at, kLavaGun);
            }

            Later later;
            later.at = at;
            later.radius = fire.erupt_radius;
            later.damage = b.last_hit * fire.erupt_share;
            later.skip = moby;
            later.when = m_tick + 1;
            later.ignite_tier = b.tier;
            later.item = kLavaGun;
            m_later.push_back(later);
        }
    }

    // Overdrive: kills in a row with the tiered weapon in hand.
    if (enemy) {
        const int held = m_view.held();

        // Another weapon, or no perks on it: the count starts again.
        if (held != m_kill_item || stage_now(held) == Stage::None) {
            m_kill_item = held;
            m_kills = 0;
        }

        // A kill that counts.
        if (stage_now(held) != Stage::None && ++m_kills >= kOverdriveKills) {
            m_kills = 0;
            m_overdrive_item = held;
            m_overdrive_until = m_tick + static_cast<std::uint32_t>(m_view.ticks(kOverdriveTicks));
            const WeaponView weapon = m_weapons.weapon(held);
            const std::string text = "Overdrive: " + m_weapons.name_at(held, weapon.level);
            m_view.notice(text);
        }
    }

    // Volatile: every enemy bursts as it dies.
    if (enemy && m_elites.modifier() == Modifier::Volatile && m_reference > 0.0f
        && m_later.size() < kLaterMost) {
        Later later;
        later.at = at;
        later.radius = kVolatileRadius;
        later.damage = m_reference * kVolatileShare;
        later.skip = moby;
        later.when = m_tick + 2;
        later.blast_item = m_view.held();
        m_later.push_back(later);
    }

    m_elites.died(moby);
}

std::vector<Perks::Target> Perks::targets(
    const std::array<float, 3>& at, float radius, int count, int skip_slot
) {
    std::vector<Target> out;
    m_scan.read(m_tick);
    std::vector<std::uint32_t> visited;

    // One at a time, nearest first.
    for (int n = 0; n < count; ++n) {
        const ScannedMoby* next = nearest(at, radius, visited);

        // No one left in reach.
        if (next == nullptr) {
            break;
        }

        visited.push_back(next->address);

        // Left out.
        if (next->slot == skip_slot) {
            --n;
            continue;
        }

        out.push_back(Target{next->slot, next->address, next->position});
    }

    return out;
}

void Perks::zodiac_reach(const std::array<float, 3>& gun) {
    const openrac::Memory memory = m_view.memory();
    const WeaponView zodiac = m_weapons.weapon(kZodiac);
    const int tier = zodiac.has_tier() ? tier_of(zodiac.kind, zodiac.level) : 0;
    const float reach = ng_plus_plus::zodiac_reach(tier);
    const float drain = kZodiacDrain * static_cast<float>(kZodiacPulseFrames)
                        * (zodiac.has_tier() ? damage(zodiac.kind, zodiac.level) : 1.0f);
    const std::uint32_t hero = m_view.hero_moby();
    const std::array<float, 3> eye{gun[0], gun[1], gun[2] + 1.0f};

    // The enemies to take, chosen first: recording a death runs the hooks and deletes mobys. Each
    // with whether the moby loop ran it this tick (its update runs the death the gun leaves to it).
    std::vector<ScannedMoby> taken;
    std::vector<bool> taken_ran;

    for (const ScannedMoby& moby : m_scan.mobys()) {
        // Enough for one pulse.
        if (static_cast<int>(taken.size()) >= kReachMost) {
            break;
        }

        // Not an enemy, the hero, a boss (tuning.cpp drains those), out of reach, or not one a
        // shot may hit now (dead, dying, hidden, not out yet).
        if (!moby.enemy || moby.address == hero || is_boss(moby.moby_class)
            || distance2(moby.position, gun) > reach * reach || !m_scan.targetable(moby.address)) {
            continue;
        }

        // The game's own: one its moby loop ran (drawn or near the camera) within its range.
        openrac_moby_run run{};
        run.size = sizeof run;
        const bool known = m_mod.moby_run(moby.slot, run);
        const bool ran = known && run.ran > OPENRAC_MOBY_RUN_FAR;
        if (ran && distance2(moby.position, gun) <= kZodiacRange * kZodiacRange) {
            continue;
        }

        // A wall in between: another room.
        std::array<float, 3> aim = moby.position;
        const float height = m_scan.aim_height(moby.address);
        aim[2] += height > 0.0f ? height : 0.5f;
        bool blocked = false;
        if (m_mod.level_line(eye.data(), aim.data(), blocked) && blocked) {
            continue;
        }

        taken.push_back(moby);
        taken_ran.push_back(ran);
    }

    int sparks = 0;
    int killed = 0;

    // Each as the gun takes the ones it picks (charging_weapon_update): no hit is posted, so no
    // earlier weapon's hit and no death of the enemy's own come of it.
    for (std::size_t k = 0; k < taken.size(); ++k) {
        const ScannedMoby& moby = taken[k];
        const std::uint32_t record = m_scan.health_record(moby.address);
        const auto state = memory.read<std::uint8_t>(moby.address + kMobyStateByte);

        // Gone meanwhile (an earlier one's death took it with it), or no health.
        if (record == 0 || state >= 0x80u) {
            continue;
        }

        // Only what the gun itself could take (zodiac_reach_action): started, and, for a death
        // left to its owner, one whose owner runs.
        const bool ran = taken_ran[k];
        const auto health = memory.read<float>(record);
        const auto path = memory.read<std::uint8_t>(record + kHealthDeathPath);
        const ZodiacAction action =
            zodiac_reach_action(health, path, drain, state != 0 || ran, ran);

        // Over the line: a pulse's drain off its health. Its own death later is the Zodiac's
        // too, as the pulse's drained ones are (tuning.cpp), unless another weapon hits it since.
        if (action == ZodiacAction::Drain) {
            memory.write(record, health - drain);

            if (m_credit != nullptr
                && (!m_credit->marked(moby.address, m_tick)
                    || m_credit->marked_for(moby.address, m_tick, kZodiac))) {
                m_credit->mark(
                    moby.address,
                    m_tick,
                    kZodiac,
                    kZodiacDrainCreditTicks,
                    memory.read<std::uint8_t>(record + kHealthWeapon)
                );
            }

            ++zodiac_stats_or_scratch().reach_drained;
            continue;
        }

        // Death path 3 (the gun leaves it too), one already left to its owner (recording it again
        // would pay twice), or no way to record a death in this OpenRAC.
        if (action == ZodiacAction::Skip || path == 2 || m_record_death == nullptr) {
            ++zodiac_stats_or_scratch().reach_skipped;

            // Once, for a player's log.
            if (m_record_death == nullptr && !m_logged_no_record) {
                m_logged_no_record = true;
                m_mod.info("the Zodiac's reach kills nothing in this OpenRAC: no Moby_RecordDeath");
            }

            continue;
        }

        // The sparks, as the gun's: at the enemy, 1.5 times its aim height up.
        std::array<float, 3> at = moby.position;
        at[2] += memory.read<float>(record + kHealthAim) * 1.5f;
        if (sparks < kReachSparksMost && zodiac_spark(at, eye)) {
            ++sparks;
        }

        // Its death recorded, the Zodiac named (Combat::record_death's credit spends the mark).
        if (m_credit != nullptr) {
            m_credit->mark(moby.address, m_tick, kZodiac, 0);
        }

        // Recorded as the reach's, not the pulse's (tuning.cpp's count).
        if (m_credit != nullptr) {
            m_credit->set_reaching(true);
        }

        m_record_death(moby.address, 0, 3);

        if (m_credit != nullptr) {
            m_credit->set_reaching(false);
        }

        ++killed;

        // Its owner's (a part, a squad's): the gun leaves the death to it.
        if (action == ZodiacAction::Owner) {
            memory.write(record + kHealthDeathPath, std::uint8_t{2});
            ++zodiac_stats_or_scratch().reach_owner;
            continue;
        }

        // Vaporized: the moby goes, as the gun's DeleteMoby.
        m_mod.moby_kill(moby.slot);
        ++zodiac_stats_or_scratch().reach_vaporized;
    }

    // Once, for a player's log.
    if (killed > 0 && !m_logged_reach) {
        m_logged_reach = true;
        m_mod.info(
            "the Zodiac's pulse reaches " + std::to_string(static_cast<int>(reach))
            + " units: took " + std::to_string(killed) + " enemies the game's pick left out"
        );
    }
}

bool Perks::zodiac_spark(const std::array<float, 3>& at, const std::array<float, 3>& eye) {
    openrac_moby pose{};
    pose.size = sizeof pose;
    pose.position[0] = at[0];
    pose.position[1] = at[1];
    pose.position[2] = at[2];
    pose.scale = 1.0f;
    const int slot = m_mod.moby_spawn(kSparkClass, pose);

    // No room, or the level has no such class.
    if (slot < 0) {
        return false;
    }

    // The game's to update from here.
    m_mod.moby_drive(slot, false);
    const std::uint32_t spark = MobyScan::slot_address(m_view, slot);

    if (!GameView::is_memory(spark, kMobyRecordBytes)) {
        return false;
    }

    const openrac::Memory memory = m_view.memory();
    memory.write(spark + kMobyUpdateDistance, std::uint8_t{0xFF});
    memory.write(spark + kMobyDrawDistance, std::int16_t{0xFF});
    memory.write(spark + kMobyStateByte, std::uint8_t{0});
    memory.write(spark + kMobyScale, 1.0f);

    // Its axes: forward toward the eye, side and up square to it.
    const std::array<float, 3> forward = unit(
        {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]}, std::array<float, 3>{1.0f, 0.0f, 0.0f}
    );
    const std::array<float, 3> side =
        unit(cross(forward, {0.0f, 0.0f, 1.0f}), std::array<float, 3>{0.0f, 1.0f, 0.0f});
    const std::array<float, 3> up = cross(side, forward);
    const std::array<float, 3>* axes[] = {&forward, &side, &up};

    for (std::uint32_t k = 0; k < 3; ++k) {
        const std::uint32_t row = spark + kMobyAxes + 0x10u * k;

        for (std::uint32_t c = 0; c < 3; ++c) {
            memory.write(row + 4u * c, (*axes[k])[c]);
        }

        memory.write(row + 12u, 0.0f);
    }

    // Its life, its kind (0, the pulse's) and no parent.
    const auto pvars = memory.read<std::uint32_t>(spark + kMobyPvars);

    if (GameView::is_memory(pvars, 12)) {
        memory.write(pvars, kSparkLife);
        memory.write(pvars + 4u, std::int32_t{0});
        memory.write(pvars + 8u, std::uint32_t{0});
    }

    return true;
}

bool Perks::strike(std::uint32_t target, int item) {
    const float last = item >= 0 && item < kItems ? m_last_damage[static_cast<std::size_t>(item)]
                                                  : 0.0f;
    const float damage = last > 0.0f ? last : m_reference;
    m_post_item = item;
    const bool posted = active() && damage > 0.0f && post(target, damage);
    m_post_item = -1;
    return posted;
}

bool Perks::burst_later(const std::array<float, 3>& at, float radius, float share) {
    // Nothing to scale it by yet, or too much waiting.
    if (!active() || m_reference <= 0.0f || m_later.size() >= kLaterMost) {
        return false;
    }

    Later later;
    later.at = at;
    later.radius = radius;
    later.damage = m_reference * share;
    later.skip = m_view.hero_moby();
    later.when = m_tick + 1;
    m_later.push_back(later);
    return true;
}

void Perks::watch() {
    // Each watched moby alive now.
    for (const ScannedMoby& moby : m_scan.mobys()) {
        const int item = watched_item(moby.moby_class);

        // Not a watched class.
        if (item < 0) {
            continue;
        }

        auto [it, fresh] = m_watched.try_emplace(moby.address);
        Watched& w = it->second;

        // New, or a new moby in its slot.
        if (fresh || w.moby_class != moby.moby_class) {
            w = Watched{moby.moby_class, item, moby.position, m_tick, m_tick, m_tick};
            const Stage stage = stage_now(item);
            const float base = m_last_damage[static_cast<std::size_t>(item)] > 0.0f
                                   ? m_last_damage[static_cast<std::size_t>(item)]
                                   : m_reference;

            // The Gravity Bomb's blast: a wider reach from Gamma, mini bombs around it from
            // Epsilon, eight and a second pull at Omega.
            if (item == 42 && stage != Stage::None && base > 0.0f) {
                m_later.push_back(Later{moby.position, 9.0f, base * 0.5f, 0, 0, m_tick + 1, 0});
                m_later.back().item = item;

                // With 1.29 the mini bombs and the second pull are the game's own (shots.cpp).
                if (stage >= Stage::Epsilon && (m_shots == nullptr || !m_shots->active())) {
                    const int minis = stage == Stage::Omega ? 8 : 4;

                    // Each mini bomb a little apart, a third of a second on.
                    for (int k = 0; k < minis; ++k) {
                        const float angle =
                            6.2831853f * static_cast<float>(k) / static_cast<float>(minis);
                        std::array<float, 3> at = moby.position;
                        at[0] += 5.0f * std::cos(angle);
                        at[1] += 5.0f * std::sin(angle);
                        m_later.push_back(Later{at, 4.0f, base * 0.5f, 0, 0, m_tick + 20, 0});
                        m_later.back().item = item;
                    }
                }

                if (stage == Stage::Omega && (m_shots == nullptr || !m_shots->active())) {
                    m_later.push_back(Later{moby.position, 9.0f, base, 0, 0, m_tick + 60, 0});
                    m_later.back().item = item;
                }
            }
        }

        w.at = moby.position;
        w.seen = m_tick;
        const Stage stage = stage_now(item);

        // The shield zaps the nearest enemies every second from Epsilon.
        if (item == 45 && stage >= Stage::Epsilon && m_tick >= w.next && m_reference > 0.0f) {
            w.next = m_tick + static_cast<std::uint32_t>(m_view.ticks(kShieldZapTicks));
            m_post_item = item;
            burst(w.at, 8.0f, m_reference * 0.6f, 0, stage == Stage::Omega ? 3 : 1);
            m_post_item = -1;
        }

        // The Visibomb's missile burns a trail from Gamma.
        if (item == 14 && stage != Stage::None && m_tick >= w.next && m_reference > 0.0f) {
            w.next = m_tick + static_cast<std::uint32_t>(m_view.ticks(kTrailTicks));
            m_post_item = item;
            burst(w.at, 3.0f, m_reference * 0.3f, 0);
            m_post_item = -1;
        }

        // The Zodiac at Omega: its pulse posts no hit (tuning.h), so its field is left here, on
        // the enemies nearest the gun as the pulse starts. "The hit" is the drain the pulse takes
        // off an enemy in a field's pulse, times the Zodiac's tier.
        if (item == kZodiac) {
            const bool pulsing = m_view.memory().read<std::uint8_t>(moby.address + 0x20u)
                                 == static_cast<std::uint8_t>(kZodiacPulse);

            // NG++'s reach, at every tier and without one: the enemies the game's pick left out,
            // after the watch (recording their deaths runs the hooks).
            if (pulsing && !w.pulsing) {
                m_reach_guns.push_back(moby.position);
            }

            if (pulsing && !w.pulsing && stage == Stage::Omega) {
                const WeaponView zodiac = m_weapons.weapon(kZodiac);
                const HitPerk perk = hit_perk(kZodiac, stage);
                const float hit = kZodiacDrain * static_cast<float>(kFieldPulseTicks)
                                  * damage(zodiac.kind, zodiac.level);
                std::vector<std::uint32_t> visited;

                for (int n = 0; n < kZodiacFieldsMost && m_fields.size() < kFieldsMost; ++n) {
                    const ScannedMoby* next = nearest(moby.position, kZodiacRange, visited);

                    // No one left in reach.
                    if (next == nullptr) {
                        break;
                    }

                    visited.push_back(next->address);
                    m_fields.push_back(
                        Field{
                            next->position,
                            perk.field_radius,
                            hit * perk.field_share,
                            m_tick + static_cast<std::uint32_t>(m_view.ticks(perk.field_ticks)),
                            m_tick + static_cast<std::uint32_t>(m_view.ticks(kFieldPulseTicks)),
                            kZodiac
                        }
                    );
                }
            }

            w.pulsing = pulsing;
        }

        // The Tesla Claw's storm at Omega: an arc to anything near the hero.
        // Its own last hit, back to a whole claw hit (the steady hits are a share of one); a
        // share of another weapon's hit would be that weapon's times 1/share.
        const float claw_hit = m_last_damage[18] > 0.0f ? m_last_damage[18] / m_claw_share
                                                        : m_reference;

        if (item == 18 && stage == Stage::Omega && m_tick >= w.next && claw_hit > 0.0f) {
            w.next = m_tick + static_cast<std::uint32_t>(m_view.ticks(kStormTicks));
            m_post_item = item;
            const std::vector<std::uint32_t> hit =
                burst(hero_position(), 10.0f, claw_hit * 1.5f, 0, 1);
            m_post_item = -1;

            // The arcs' stun (1.29).
            for (const std::uint32_t struck : hit) {
                const ScannedMoby* at = m_scan.find(struck);

                if (m_shots != nullptr && at != nullptr) {
                    m_shots->status(at->slot, Status::Freeze, 45);
                }
            }
        }
    }

    // Each watched moby gone this tick: its end.
    for (auto it = m_watched.begin(); it != m_watched.end();) {
        const Watched w = it->second;

        // Still here.
        if (w.seen == m_tick) {
            ++it;
            continue;
        }

        it = m_watched.erase(it);
        const Stage stage = stage_now(w.item);

        // Nothing to scale it by, or no tier.
        if (stage == Stage::None || m_reference <= 0.0f) {
            continue;
        }

        // The decoy explodes from Epsilon, twice at Omega.
        if (w.item == 17 && stage >= Stage::Epsilon) {
            const float share = stage == Stage::Omega ? 3.0f : 2.0f;
            const float radius = stage == Stage::Omega ? 9.0f : 6.0f;
            m_later.push_back(Later{w.at, radius, m_reference * share, 0, 0, m_tick, 0, 17});

            if (stage == Stage::Omega) {
                m_later.push_back(
                    Later{w.at, radius, m_reference * share, 0, 0, m_tick + 30, 0, 17}
                );
            }
        }

        // A sheep explodes when it goes, from Gamma.
        if (w.item == 16) {
            const float radius =
                stage == Stage::Omega ? 8.0f : (stage == Stage::Epsilon ? 6.0f : 4.0f);
            const float share =
                stage == Stage::Omega ? 2.0f : (stage == Stage::Epsilon ? 1.5f : 1.0f);
            m_later.push_back(Later{w.at, radius, m_reference * share, 0, 0, m_tick, 0, 16});
        }

        // The shield bursts as it ends, at Omega.
        if (w.item == 45 && stage == Stage::Omega) {
            m_later.push_back(Later{w.at, 8.0f, m_reference * 2.0f, 0, 0, m_tick, 0, 45});
        }

        // The Visibomb's missile releases its minis from Epsilon: homing rockets in its colour with
        // 1.29, else hits on the nearest.
        if (w.item == 14 && stage >= Stage::Epsilon) {
            const int minis = stage == Stage::Omega ? 6 : 3;
            const bool real =
                m_shots != nullptr && m_shots->rockets_at(w.at, kMiniRocket, minis, 12.0f, 14) > 0;

            if (!real) {
                m_later.push_back(Later{w.at, 12.0f, m_reference * 0.8f, 0, 0, m_tick, minis});
                m_later.back().item = 14;
            }
        }
    }

    // Too many waiting: the oldest go.
    if (m_later.size() > kLaterMost) {
        m_later.erase(
            m_later.begin(),
            m_later.begin() + static_cast<std::ptrdiff_t>(m_later.size() - kLaterMost)
        );
    }
}

void Perks::tick(std::uint32_t tick) {
    m_tick = tick;
    m_budget = kPostsPerTick;
    const int level = m_view.loaded_level();

    // Another level: what the mod held went with the old one's mobys.
    if (level != m_level) {
        m_level = level;
        m_frozen.clear();
        m_burning.clear();
        m_watched.clear();
        m_arena_seen.clear();
        m_later.clear();
        m_fields.clear();
        m_reach_guns.clear();
        m_have_snapshot = false;
        m_reference = 0.0f;
        m_last_damage = {};
    }

    // Outside a level, off or a guest: nothing.
    if (level < 0 || !active() || !m_scan.read(tick)) {
        return;
    }

    thaw();
    burn();
    watch();

    // The Zodiac's reach for each pulse that began.
    for (const std::array<float, 3>& gun : m_reach_guns) {
        zodiac_reach(gun);
    }

    m_reach_guns.clear();

    // A summary for a player's log, about once a minute when the perks acted.
    if (tick >= m_summary_tick + static_cast<std::uint32_t>(m_view.ticks(3600))) {
        m_summary_tick = tick;
        const Shots::Counts shots = m_shots != nullptr ? m_shots->take_counts() : Shots::Counts{};

        if (m_posted > 0 || shots.refires > 0 || shots.spawns > 0 || shots.statuses > 0) {
            m_mod.info(
                "perks in the last minute: " + std::to_string(m_posted) + " extra hits, "
                + std::to_string(shots.refires) + " extra shots, " + std::to_string(shots.spawns)
                + " shots made (" + std::to_string(shots.blasts) + " blasts), "
                + std::to_string(shots.statuses) + " freezes and slows; homing rockets "
                + std::to_string(shots.retargets) + " retargeted, " + std::to_string(shots.dropped)
                + " let go, " + std::to_string(shots.strikes) + " went off on a target unhit"
            );
        }

        m_posted = 0;
    }

    // The arenas' spawned enemies, each seen once.
    if (is_arena_level(level)) {
        for (const ScannedMoby& moby : m_scan.mobys()) {
            // Not a spawned enemy.
            if (!moby.enemy || !moby.spawned) {
                continue;
            }

            auto [it, fresh] = m_arena_seen.try_emplace(moby.address, moby.moby_class);

            // New, or a new moby in its slot.
            if (fresh || it->second != moby.moby_class) {
                it->second = moby.moby_class;
                m_elites.arena_spawn(moby.address);
            }
        }
    }

    // The hits waiting for this tick.
    std::vector<Later> still;

    for (const Later& later : m_later) {
        // Not yet, or this tick's hits are spent.
        if (later.when > tick || m_budget <= 0) {
            still.push_back(later);
            continue;
        }

        m_post_item = later.item >= 0 ? later.item : later.blast_item;
        const std::vector<std::uint32_t> hit =
            burst(later.at, later.radius, later.damage, later.skip, later.fan);
        m_post_item = -1;

        // The game's blast there, in the weapon's look (1.29).
        if (later.blast_item >= 0 && m_shots != nullptr) {
            m_shots->blast(later.at, later.blast_item);
        }

        // The Ice Gun's shards freeze what they hit.
        if (later.freeze_tier > 0) {
            for (const std::uint32_t moby : hit) {
                const ScannedMoby* at = m_scan.find(moby);

                // Still there.
                if (at != nullptr) {
                    chill(*at, later.freeze_tier, later.damage * 0.25f);
                }
            }
        }

        // The fire path's eruptions and storm set fire to what they hit.
        if (later.ignite_tier > 0) {
            for (const std::uint32_t moby : hit) {
                const ScannedMoby* at = m_scan.find(moby);

                // Still there.
                if (at != nullptr) {
                    ignite(*at, later.ignite_tier, later.damage, false);
                }
            }
        }
    }

    m_later = std::move(still);

    // The fields' pulses.
    for (auto it = m_fields.begin(); it != m_fields.end();) {
        // Ended.
        if (tick >= it->until) {
            it = m_fields.erase(it);
            continue;
        }

        // A pulse.
        if (tick >= it->next) {
            it->next = tick + static_cast<std::uint32_t>(m_view.ticks(kFieldPulseTicks));
            m_post_item = it->item;
            burst(it->at, it->radius, it->damage, 0);
            m_post_item = -1;
        }

        ++it;
    }
}

void Perks::draw(std::uint32_t moby, int level, std::uint32_t rgb) const {
    // A port without energy: the game's look.
    if (!m_energy) {
        return;
    }

    m_material(m_mod.handle(), moby, energy_at(std::clamp(level, 0, kEffectTop)), rgb);
}

void Perks::draw_ice(std::uint32_t moby, int energy, bool frozen) const {
    // Chilled, or a port without paint: the glow.
    if (!frozen || !m_paint) {
        draw(moby, energy, frozen ? kFrozenRgb : kChilledRgb);
        return;
    }

    // Frozen solid: painted ice, what it makes (the meteors' fire on it) blue too with 1.39.
    const std::uint32_t material =
        paint_at(kFrozenPaintLevel) | (m_paint_effects ? kPaintEffectsBit : 0u);
    m_material(m_mod.handle(), moby, material, kIcePaintRgb);
}

void Perks::undraw(std::uint32_t moby) const {
    // A port without energy drew nothing.
    if (!m_energy) {
        return;
    }

    m_material(m_mod.handle(), moby, kMaterialNone, kNoTint);
}

}  // namespace ng_plus_plus
