// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The weapons' perks in the game (perks.h).

#include "perks.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "discs.h"
#include "shots.h"
#include "weapon_visuals.h"

namespace ng_plus_plus {
namespace {

// The most hits the mod posts in one tick: a long chain reaction waits for the next ticks.
constexpr int kPostsPerTick = 192;

// The most hits waiting for later at once.
constexpr std::size_t kLaterMost = 256;
constexpr std::size_t kFieldsMost = 48;

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

// The shield's zaps, the Visibomb's trail and the Tesla Claw's storm: ticks apart.
constexpr std::uint32_t kShieldZapTicks = 60;
constexpr std::uint32_t kTrailTicks = 15;
constexpr std::uint32_t kStormTicks = 20;

// The Ice Gun's shatter glow on a dying enemy.
constexpr std::uint32_t kShatterRgb = 0xD0F0FFu;

}  // namespace

void Perks::start() {
    m_material = moby_material_of(m_mod.api());
    m_energy = m_material != nullptr && m_mod.api().api_minor >= kEnergyMinor;

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

        // Nearer than the best so far.
        if (d2 <= best_d2) {
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

    // The Ice Gun from Alpha on.
    if (item == kLavaGun) {
        chill(target, tier_of(weapon.kind, weapon.level), damage);
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

    m_cooldown[n] = m_tick + static_cast<std::uint32_t>(perk.cooldown);
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
            m_shots->status(next->slot, perk.status, perk.status_ticks);
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
                m_shots->status(at->slot, perk.status, perk.status_ticks);
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
                m_tick + static_cast<std::uint32_t>(perk.field_ticks),
                m_tick + static_cast<std::uint32_t>(kFieldPulseTicks)
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
    f.chill_until = std::max(f.chill_until, m_tick + static_cast<std::uint32_t>(ice.chill_ticks));
    bool fresh = false;

    // Frozen solid from Epsilon, a fresh freeze only once the last one has thawed into its chill,
    // so a stream holds it slowed, frozen again and again, never for good.
    if (ice.freeze_ticks > 0 && m_tick >= f.freeze_until + 60u) {
        f.freeze_until = m_tick + static_cast<std::uint32_t>(ice.freeze_ticks);
        fresh = true;
    }

    const bool frozen = m_tick < f.freeze_until;
    const bool statuses = m_shots != nullptr && m_shots->active() && !f.boss;

    // The status (OpenRAC 1.29): frozen solid, else slowed, renewed while the chill lasts.
    if (statuses && fresh) {
        m_shots->status(f.slot, Status::Freeze, ice.freeze_ticks);
        f.slowed_until = 0;
    } else if (
        statuses && !frozen
        && f.slowed_until < m_tick + static_cast<std::uint32_t>(ice.chill_ticks / 2)
    ) {
        slow(f);
    }

    // Its glow: icy blue while frozen, paler while chilled.
    if (!f.drawn || f.drawn_frozen != frozen) {
        draw(victim.address, ice.energy, frozen ? kFrozenRgb : kChilledRgb);
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

        const bool frozen = m_tick < f.freeze_until;

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

        // Its glow follows the freeze.
        if (f.drawn_frozen != frozen) {
            draw(moby, ice_effect(f.tier).energy, frozen ? kFrozenRgb : kChilledRgb);
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
            later.when = m_tick + 1;
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
            m_overdrive_until = m_tick + static_cast<std::uint32_t>(kOverdriveTicks);
            const WeaponView weapon = m_weapons.weapon(held);
            const std::string text = "Overdrive: " + m_weapons.name_at(held, weapon.level);
            m_mod.api().play_notice(m_mod.handle(), text.c_str());
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
                    }
                }

                if (stage == Stage::Omega && (m_shots == nullptr || !m_shots->active())) {
                    m_later.push_back(Later{moby.position, 9.0f, base, 0, 0, m_tick + 60, 0});
                }
            }
        }

        w.at = moby.position;
        w.seen = m_tick;
        const Stage stage = stage_now(item);

        // The shield zaps the nearest enemies every second from Epsilon.
        if (item == 45 && stage >= Stage::Epsilon && m_tick >= w.next && m_reference > 0.0f) {
            w.next = m_tick + kShieldZapTicks;
            burst(w.at, 8.0f, m_reference * 0.6f, 0, stage == Stage::Omega ? 3 : 1);
        }

        // The Visibomb's missile burns a trail from Gamma.
        if (item == 14 && stage != Stage::None && m_tick >= w.next && m_reference > 0.0f) {
            w.next = m_tick + kTrailTicks;
            burst(w.at, 3.0f, m_reference * 0.3f, 0);
        }

        // The Tesla Claw's storm at Omega: an arc to anything near the hero.
        if (item == 18 && stage == Stage::Omega && m_tick >= w.next && m_reference > 0.0f) {
            w.next = m_tick + kStormTicks;
            burst(hero_position(), 10.0f, m_reference * 1.5f, 0, 1);
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
        m_watched.clear();
        m_arena_seen.clear();
        m_later.clear();
        m_fields.clear();
        m_have_snapshot = false;
        m_reference = 0.0f;
        m_last_damage = {};
    }

    // Outside a level, off or a guest: nothing.
    if (level < 0 || !active() || !m_scan.read(tick)) {
        return;
    }

    thaw();
    watch();

    // A summary for a player's log, about once a minute when the perks acted.
    if (tick >= m_summary_tick + 3600u) {
        m_summary_tick = tick;
        const Shots::Counts shots = m_shots != nullptr ? m_shots->take_counts() : Shots::Counts{};

        if (m_posted > 0 || shots.refires > 0 || shots.spawns > 0 || shots.statuses > 0) {
            m_mod.info(
                "perks in the last minute: " + std::to_string(m_posted) + " extra hits, "
                + std::to_string(shots.refires) + " extra shots, " + std::to_string(shots.spawns)
                + " shots made (" + std::to_string(shots.blasts) + " blasts), "
                + std::to_string(shots.statuses) + " freezes and slows"
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

        const std::vector<std::uint32_t> hit =
            burst(later.at, later.radius, later.damage, later.skip, later.fan);

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
            it->next = tick + static_cast<std::uint32_t>(kFieldPulseTicks);
            burst(it->at, it->radius, it->damage, 0);
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

void Perks::undraw(std::uint32_t moby) const {
    // A port without energy drew nothing.
    if (!m_energy) {
        return;
    }

    m_material(m_mod.handle(), moby, kMaterialNone, kNoTint);
}

}  // namespace ng_plus_plus
