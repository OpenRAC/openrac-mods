// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The 1.29 perks (shots.h).

#include "shots.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "perks.h"
#include "weapon_visuals.h"

namespace ng_plus_plus {
namespace {

// A told shot with no speed of its own: the rockets' and seekers' speed then, units a tick.
constexpr float kDefaultSpeed = 0.6f;

// Mini bombs: how fast they scatter (units a tick) and how high they are thrown.
constexpr float kScatterSpeed = 0.22f;
constexpr float kScatterUp = 0.18f;

// How far a split rocket or seeker looks for its enemy.
constexpr float kHomingRadius = 40.0f;

// The yaw a rocket splitting in flight turns its half by.
constexpr float kSplitYaw = 0.35f;

// How far from where a shot ended the shots made there start, along their flight: a rocket's first
// update tests the line it moves along and skips only its gun and the hero, so one made inside the
// enemy just hit blows up at once, unseen under the first one's blast. Mini bombs start nearer.
constexpr float kEndClear = 2.0f;
constexpr float kScatterClear = 1.0f;

// The Gravity Bomb's mini bombs scatter a third of a second after its blast (README.md).
constexpr std::uint32_t kGravityScatterTicks = 20;

// How far (on the ground plane) from the hero a rocket of `oclass` lives: the game removes a
// Minirocket Tube rocket past 75 units on its update (func_L00_003ABEC8) and a RYNO II rocket past
// 60 (homing_l00_shot_update_3), so one made past that never shows. 0 for no limit.
float rocket_range(int oclass) {
    switch (oclass) {
        case kMiniRocket:
            return 75.0f;
        case kRynoRocket:
            return 60.0f;
        default:
            return 0.0f;
    }
}

// A rocket made this near its range's end is refused: it would last a few ticks at most.
constexpr float kRangeMargin = 10.0f;

// Returns `at` moved `length` along `direction` (none for a null one).
std::array<float, 3> moved(
    const std::array<float, 3>& at, const std::array<float, 3>& direction, float length
);

// Returns `v` turned `yaw` radians about the up axis (+z).
std::array<float, 3> turned(const std::array<float, 3>& v, float yaw) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    return {v[0] * c - v[1] * s, v[0] * s + v[1] * c, v[2]};
}

// Returns `v` scaled to `length` (none for a null vector).
std::array<float, 3> scaled(const std::array<float, 3>& v, float length) {
    const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return n > 1e-4f ? std::array<float, 3>{v[0] / n * length, v[1] / n * length, v[2] / n * length}
                     : std::array<float, 3>{0.0f, 0.0f, 0.0f};
}

std::array<float, 3> moved(
    const std::array<float, 3>& at, const std::array<float, 3>& direction, float length
) {
    const std::array<float, 3> step = scaled(direction, length);
    return {at[0] + step[0], at[1] + step[1], at[2] + step[2]};
}

// Packs a look for the comparison of what was sent.
std::uint64_t packed(std::uint32_t material, std::uint32_t rgb) {
    return static_cast<std::uint64_t>(material) << 32 | rgb;
}

}  // namespace

void Shots::start() {
    m_calls = weapon_calls_of(m_mod.api());

    // A port before 1.29: the perks of 1.28.
    if (m_calls == nullptr) {
        m_mod.info(
            "no extra shots, split rockets, shot colours or freezes in this build: they need "
            "OpenRAC with mod interface 1.29"
        );
        return;
    }

    const int fire = m_calls->on_weapon_fire(m_mod.handle(), &Shots::fired, this);
    const int end = m_calls->on_shot_end(m_mod.handle(), &Shots::ended, this);

    // Refused: the looks and statuses still work.
    if (fire != OPENRAC_HOOK_OK || end != OPENRAC_HOOK_OK) {
        m_mod.info("the port refused the weapon shot callbacks: no extra shots or splits");
    }
}

void Shots::fired(void* user, const WeaponShot* shot) {
    // The port tells each shot once, on the game's thread.
    if (user != nullptr && shot != nullptr && shot->size >= sizeof(WeaponShot)) {
        static_cast<Shots*>(user)->on_fire(*shot);
    }
}

void Shots::ended(void* user, const ShotEnd* end) {
    // The same.
    if (user != nullptr && end != nullptr && end->size >= sizeof(ShotEnd)) {
        static_cast<Shots*>(user)->on_end(*end);
    }
}

Stage Shots::stage(int item) const {
    return m_perks.stage_now(item);
}

int Shots::tier(int item) const {
    // Not an item.
    if (item < 0 || item >= kItems) {
        return 0;
    }

    const WeaponView weapon = m_weapons.weapon(item);
    return weapon.has_tier() ? tier_of(weapon.kind, weapon.level) : 0;
}

void Shots::on_fire(const WeaponShot& shot) {
    // Off, a guest, or a weapon with no shot moby (its rounds only).
    if (!m_perks.active() || (shot.flags & kShotRounds) != 0 || shot.item < 0
        || shot.item >= kItems) {
        return;
    }

    const int item = shot.item;
    m_speed[static_cast<std::size_t>(item)] = shot.speed > 0.0f ? shot.speed : kDefaultSpeed;

    // The shot kept for its end.
    Tracked tracked;
    tracked.oclass = shot.oclass;
    tracked.item = item;
    tracked.born = m_tick;
    tracked.root =
        shot.serial != 0 ? shot.serial : static_cast<std::uint32_t>(shot.slot) | 0x80000000u;
    tracked.direction = {shot.direction[0], shot.direction[1], shot.direction[2]};
    tracked.speed = m_speed[static_cast<std::size_t>(item)];

    if (shot.slot >= 0) {
        m_tracked[shot.slot] = tracked;
    }

    const ShotPerk perk = shot_perk(item, stage(item), tier(item));

    // More of it, fanned out, while this tick's shots last (a refire needs the serial).
    if (shot.serial == 0) {
        return;
    }

    for (int n = 0; n < perk.refires && m_budget > 0; ++n) {
        const int copy =
            m_calls->weapon_refire(m_mod.handle(), shot.serial, refire_yaw(n, perk.spread), 0.0f);

        // Refused (the cap, the pool): no more this shot.
        if (copy < 0) {
            break;
        }

        --m_budget;
        ++m_counts.refires;
        Tracked copied = tracked;
        copied.direction = turned(tracked.direction, refire_yaw(n, perk.spread));
        m_tracked[copy] = copied;
    }
}

void Shots::on_end(const ShotEnd& end) {
    const auto found = m_tracked.find(end.slot);
    Tracked tracked;

    // One the mod knows (its generation and root); else one the port tracked for it alone.
    if (found != m_tracked.end() && found->second.oclass == end.oclass) {
        tracked = found->second;
        m_tracked.erase(found);
    } else {
        tracked.oclass = end.oclass;
        tracked.item = end.item;
        tracked.root = end.serial;
        tracked.generation = (end.flags & kShotLive) != 0 ? 1 : 0;
    }

    // A level left, off or a guest.
    if (end.reason == kEndLevel || !m_perks.active() || tracked.item < 0
        || tracked.item >= kItems) {
        return;
    }

    const ShotPerk perk = shot_perk(tracked.item, stage(tracked.item), tier(tracked.item));
    const std::array<float, 3> at{end.position[0], end.position[1], end.position[2] + 0.5f};

    // The Minirocket Tube's cluster blasts, from the rockets the hero fired only.
    if (perk.end_blasts > 0 && tracked.generation == 0 && (end.flags & kShotLive) == 0) {
        for (int n = 0; n < perk.end_blasts; ++n) {
            const float angle =
                6.2831853f * static_cast<float>(n) / static_cast<float>(perk.end_blasts);
            const std::array<float, 3> point{
                at[0] + 1.8f * std::cos(angle), at[1] + 1.8f * std::sin(angle), at[2]
            };
            blast(point, tracked.item);
        }
    }

    // A second shot of its own class where it ended (the Gravity Bomb's second pull), once.
    if (perk.end_again_ticks > 0 && tracked.generation == 0 && end.oclass == kGravityBomb) {
        Pending again;
        again.when = m_tick + static_cast<std::uint32_t>(perk.end_again_ticks);
        again.oclass = kGravityBomb;
        again.item = tracked.item;
        again.at = at;
        again.generation = 1;
        again.root = tracked.root;
        m_pending.push_back(again);
    }

    // Shots made where it ended: splits, mini bombs, seekers seeking again.
    if (perk.end_shots <= 0 || tracked.generation > perk.end_generations) {
        return;
    }

    int& made = m_root_made[tracked.root];
    const int room = std::max(0, perk.end_most - made);
    const int count = std::min(perk.end_shots, room);

    // Their count from this first shot is spent.
    if (count <= 0) {
        return;
    }

    // At the nearest enemies, or scattered round.
    const std::vector<Perks::Target> targets =
        perk.end_homing ? m_perks.targets(at, kHomingRadius, count, end.target)
                        : std::vector<Perks::Target>{};
    const float speed = tracked.speed > 0.0f ? tracked.speed : kDefaultSpeed;

    for (int n = 0; n < count; ++n) {
        Pending shot;
        shot.when = m_tick + (tracked.item == 42 && !perk.end_homing ? kGravityScatterTicks : 0u);
        shot.oclass = perk.end_class;
        shot.item = tracked.item;
        shot.at = at;
        shot.generation = tracked.generation + 1;
        shot.root = tracked.root;
        const float angle = 6.2831853f * static_cast<float>(n) / static_cast<float>(count);

        // At an enemy; else round about.
        if (static_cast<std::size_t>(n) < targets.size()) {
            const Perks::Target& t = targets[static_cast<std::size_t>(n)];
            shot.target = t.slot;
            shot.velocity = scaled(
                {t.position[0] - at[0], t.position[1] - at[1], t.position[2] + 0.5f - at[2]}, speed
            );
        } else if (perk.end_homing) {
            shot.velocity = scaled({std::cos(angle), std::sin(angle), 0.15f}, speed);
        } else {
            shot.velocity =
                {kScatterSpeed * std::cos(angle), kScatterSpeed * std::sin(angle), kScatterUp};
        }

        // Out of the enemy or the ground it ended on.
        shot.at = moved(at, shot.velocity, perk.end_homing ? kEndClear : kScatterClear);
        m_pending.push_back(shot);
        ++made;
    }
}

int Shots::spawn(const Pending& shot) {
    // This tick's shots are spent: the next tick's.
    if (m_budget <= 0) {
        return -1;
    }

    // A rocket past where the game keeps its rockets: it would go in its first update.
    if (const float range = rocket_range(shot.oclass);
        range > 0.0f && (shot.flags & kSpawnDetonate) == 0) {
        const std::array<float, 3> hero = m_perks.hero_position();
        const bool known = hero[0] != 0.0f || hero[1] != 0.0f || hero[2] != 0.0f;
        const float dx = shot.at[0] - hero[0];
        const float dy = shot.at[1] - hero[1];

        if (known && dx * dx + dy * dy > (range - kRangeMargin) * (range - kRangeMargin)) {
            return -1;
        }
    }

    SpawnLive live{};
    live.size = sizeof live;
    live.oclass = shot.oclass;
    live.item = shot.item;
    live.owner = -1;
    live.target = shot.target;
    live.flags = shot.flags;

    for (int c = 0; c < 3; ++c) {
        live.position[c] = shot.at[static_cast<std::size_t>(c)];
        live.velocity[c] = shot.velocity[static_cast<std::size_t>(c)];
    }

    --m_budget;
    const int slot = m_calls->moby_spawn_live(m_mod.handle(), &live);

    // Refused (the port's cap, the pool, a class it does not make).
    if (slot < 0) {
        return -1;
    }

    ++m_counts.spawns;

    // A shot that flies: kept for its end (a blast is not).
    if ((shot.flags & kSpawnDetonate) == 0) {
        Tracked tracked;
        tracked.oclass = shot.oclass;
        tracked.item = shot.item;
        tracked.born = m_tick;
        tracked.generation = shot.generation;
        tracked.root = shot.root;
        tracked.direction = scaled(shot.velocity, 1.0f);
        tracked.speed = std::sqrt(
            shot.velocity[0] * shot.velocity[0] + shot.velocity[1] * shot.velocity[1]
            + shot.velocity[2] * shot.velocity[2]
        );
        tracked.split = true;
        m_tracked[slot] = tracked;
    }

    return slot;
}

bool Shots::status(int slot, Status kind, int ticks) {
    // A port before 1.29, or no moby.
    if (m_calls == nullptr || slot < 0) {
        return false;
    }

    const bool ok =
        m_calls->moby_status(m_mod.handle(), slot, static_cast<std::uint32_t>(kind), ticks)
        == OPENRAC_HOOK_OK;
    m_counts.statuses += ok ? 1 : 0;
    return ok;
}

bool Shots::blast(const std::array<float, 3>& at, int item) {
    // A port before 1.29.
    if (m_calls == nullptr) {
        return false;
    }

    Pending shot;
    shot.oclass = kMiniRocket;
    shot.item = item;
    shot.at = at;
    shot.flags = kSpawnDetonate;
    const bool ok = spawn(shot) >= 0;
    m_counts.blasts += ok ? 1 : 0;
    return ok;
}

int Shots::rockets_at(
    const std::array<float, 3>& at, int oclass, int count, float radius, int item
) {
    // A port before 1.29.
    if (m_calls == nullptr) {
        return 0;
    }

    const std::vector<Perks::Target> targets = m_perks.targets(at, radius, count, -1);
    int made = 0;

    for (const Perks::Target& t : targets) {
        Pending shot;
        shot.oclass = oclass;
        shot.item = item;
        shot.at = at;
        shot.target = t.slot;
        shot.generation = 1;
        shot.root = 0;
        shot.velocity = scaled(
            {t.position[0] - at[0], t.position[1] - at[1], t.position[2] + 0.5f - at[2]},
            kDefaultSpeed
        );
        shot.at = moved(at, shot.velocity, kEndClear);
        made += spawn(shot) >= 0 ? 1 : 0;
    }

    return made;
}

void Shots::tick(std::uint32_t tick) {
    m_tick = tick;
    m_budget = kShotsPerTick;

    // A port before 1.29: none of it.
    if (m_calls == nullptr) {
        return;
    }

    const int level = m_view.loaded_level();

    // Another level: its shots went with it.
    if (level != m_level) {
        m_level = level;
        m_tracked.clear();
        m_root_made.clear();
        m_pending.clear();
    }

    looks(tick);

    // Outside a level, off or a guest.
    if (level < 0 || !m_perks.active()) {
        return;
    }

    fire_rates();

    // The Minirocket Tube's rockets split in flight from Epsilon.
    const ShotPerk mini = shot_perk(27, stage(27), tier(27));

    if (mini.split_after > 0) {
        for (auto& [slot, tracked] : m_tracked) {
            // Not a rocket the hero fired, split already, or not yet.
            if (tracked.oclass != kMiniRocket || tracked.split || tracked.generation != 0
                || m_tick < tracked.born + static_cast<std::uint32_t>(mini.split_after)) {
                continue;
            }

            tracked.split = true;
            const std::uint32_t address = MobyScan::slot_address(m_view, slot);

            // Gone meanwhile.
            if (address == 0 || !m_perks.alive(address, kMiniRocket)) {
                continue;
            }

            Pending half;
            half.when = m_tick;
            half.oclass = kMiniRocket;
            half.item = tracked.item;
            half.at = m_perks.position_of(address);
            half.velocity = scaled(turned(tracked.direction, kSplitYaw), tracked.speed);
            half.generation = 1;
            half.root = tracked.root;
            const std::vector<Perks::Target> target =
                m_perks.targets(half.at, kHomingRadius, 1, -1);
            half.target = target.empty() ? -1 : target.front().slot;
            m_pending.push_back(half);
        }
    }

    // The shots waiting for this tick, while its shots last.
    std::vector<Pending> still;

    for (const Pending& shot : m_pending) {
        // Not yet, or none left this tick.
        if (shot.when > m_tick || m_budget <= 0) {
            still.push_back(shot);
            continue;
        }

        spawn(shot);
    }

    m_pending = std::move(still);
}

void Shots::looks(std::uint32_t tick) {
    const bool animated_tick = tick % 2u == 0;

    // Each weapon: its tier's colour and glow on every shot and what it makes, or the game's.
    for (int item = 0; item < kItems; ++item) {
        const Kind kind = kind_of(item);

        // Not a weapon.
        if (kind == Kind::None) {
            continue;
        }

        const WeaponView weapon = m_weapons.weapon(item);
        const int level = weapon.has_tier() ? weapon.level : 0;
        const std::optional<std::uint32_t> rgb = weapon_effect_rgb(item, level, tick);
        const std::uint32_t material =
            rgb ? energy_at(effect_strength(kind, level)) : kMaterialNone;
        const std::uint64_t look = packed(material, rgb.value_or(kNoTint));
        std::uint64_t& sent = m_looks[static_cast<std::size_t>(item)];

        // The same, or an animated one between its steps.
        if (look == sent
            || (sent != 0 && rgb && weapon_tint_moves(item, level) && !animated_tick)) {
            continue;
        }

        // Nothing sent yet and none to send.
        if (sent == 0 && !rgb) {
            continue;
        }

        // Sent.
        if (m_calls->weapon_look(m_mod.handle(), item, material, rgb.value_or(kNoTint))
            == OPENRAC_HOOK_OK) {
            sent = rgb ? look : 0;
        }
    }
}

void Shots::fire_rates() {
    constexpr int kPulseRifle = 23;
    constexpr std::uint32_t kRecordRate = 0x54u;
    const openrac_gaddr record = m_calls->weapon_record(m_mod.handle(), kPulseRifle);

    // No record outside a level.
    if (record == 0 || !GameView::is_memory(record, 0xE0u)) {
        return;
    }

    const openrac::Memory memory = m_view.memory();

    const auto now = memory.read<float>(record + kRecordRate);

    // A level's load writes the table again (a new level, or the same one again after a death):
    // its rate is the game's once more.
    if (record != m_rate.record || m_view.loaded_level() != m_rate.level
        || std::fabs(now - m_rate.base * m_rate.applied) > 1e-3f) {
        m_rate = Rate{m_view.loaded_level(), record, now, 1.0f};
    }

    const float want = fire_rate(kPulseRifle, stage(kPulseRifle));

    // The rate as the tier wants it.
    if (want != m_rate.applied && m_rate.base > 0.0f) {
        memory.write(record + kRecordRate, m_rate.base * want);
        m_rate.applied = want;
    }
}

Shots::Counts Shots::take_counts() {
    const Counts out = m_counts;
    m_counts = Counts{};
    return out;
}

}  // namespace ng_plus_plus
