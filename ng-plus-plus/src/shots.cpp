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

// A told shot with no speed of its own: the rockets' and seekers' speed then, units a sixtieth of a
// second (GameView::per_tick turns it into the disc's ticks, as the other speeds here).
constexpr float kDefaultSpeed = 0.6f;

// Mini bombs: how fast they scatter (units a sixtieth of a second) and how high they are thrown.
constexpr float kScatterSpeed = 0.22f;
constexpr float kScatterUp = 0.18f;

// How far a split rocket or seeker looks for its enemy.
constexpr float kHomingRadius = 40.0f;

// Where a split rocket starts from a rocket that hit: this far back along the rocket's way and up,
// clear of what it hit, so that its first step is not into the same wall or enemy (it would go off
// at once, unseen in the first rocket's blast).
constexpr float kSplitBack = 1.2f;
constexpr float kSplitUp = 1.0f;

// A split rocket's size beside the RYNO II's own rockets, and the moby's scale word.
constexpr float kSplitScale = 0.6f;
constexpr std::uint32_t kMobyScale = 0x2C;

// An enemy this near where a rocket hit is the one it hit: its splits seek the others.
constexpr float kVictimReach = 3.0f;

// After a hit, the enemies a split leaves alone as behind what it hit: the cosine of their angle
// off the shot's way above which they count as ahead (an enemy hit, a wall or the ground hit).
constexpr float kBlockedAhead = 0.85f;
constexpr float kWallAhead = 0.2f;

// A homing RYNO II rocket (homing_l00_shot_update_3's state, its pvars): the point it seeks, its
// target, the target's aim height, its speed, its wobble, its life (an s16), its homing byte, the
// heading it keeps with no target, and the offset from the target it seeks. The moby's forward,
// right and up rows.
constexpr std::uint32_t kMobyVars = 0x68u;
constexpr std::uint32_t kRocketSeek = 0x00u;
constexpr std::uint32_t kRocketTarget = 0x10u;
constexpr std::uint32_t kRocketHeight = 0x14u;
constexpr std::uint32_t kRocketSpeed = 0x18u;
constexpr std::uint32_t kRocketYaw = 0x1Cu;
constexpr std::uint32_t kRocketLife = 0x24u;
constexpr std::uint32_t kRocketHoming = 0x26u;
constexpr std::uint32_t kRocketHeading = 0x30u;
constexpr std::uint32_t kRocketOffset = 0x40u;
constexpr std::uint32_t kRocketVarsBytes = 0x50u;
constexpr std::uint32_t kMobyForward = 0xC0u;
constexpr std::uint32_t kMobyRight = 0xD0u;
constexpr std::uint32_t kMobyUp = 0xE0u;

// The life past which the game's update steers a rocket (it steers under 295).
constexpr std::int16_t kHomingLife = 294;

// The aim height on an enemy with none of its own.
constexpr float kDefaultAimHeight = 0.5f;

// A homing rocket's least turn a tick (radians, a sixtieth of a second's), how near the point it
// seeks counts as there (at least, or this many of its steps), how long it chases one target, and
// how often it looks whether a wall stands in between (ticks).
constexpr float kBaseTurn = 0.09f;
constexpr float kReach = 2.0f;
constexpr float kReachSteps = 2.5f;
constexpr int kChaseTicks = 150;
constexpr int kSightEvery = 6;

// The yaw a rocket splitting in flight turns its half by.
constexpr float kSplitYaw = 0.35f;

// How far from where a shot ended the shots made there start, along their flight: a rocket's first
// update tests the line it moves along and skips only its gun and the hero, so one made inside the
// enemy just hit blows up at once, unseen under the first one's blast. Mini bombs start nearer.
constexpr float kEndClear = 2.0f;
constexpr float kScatterClear = 1.0f;

// The Gravity Bomb's mini bombs scatter a third of a second after its blast (README.md).
constexpr int kGravityScatterTicks = 20;

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
    m_speed[static_cast<std::size_t>(item)] =
        shot.speed > 0.0f ? shot.speed : m_view.per_tick(kDefaultSpeed);

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

#ifdef NGPP_TEST_SETUP
    // Hidden test runs: each RYNO II rocket's life, for the log's lifetimes.
    if (tracked.oclass == kRynoRocket && found != m_tracked.end()) {
        m_mod.info(
            "test: ryno rocket gen " + std::to_string(tracked.generation) + " lived "
            + std::to_string(m_tick - tracked.born) + " ticks, end " + std::to_string(end.reason)
            + (tracked.homing ? ", homing" : "") + ", chase " + std::to_string(tracked.chase.ticks)
            + " ticks"
        );
    }
#endif

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
        again.when = m_tick + static_cast<std::uint32_t>(m_view.ticks(perk.end_again_ticks));
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

    // A shot the hero fired splits by its own roll of its serial (only the host's NG++ splits).
    if (tracked.generation == 0 && !split_roll(end.serial, perk.end_chance)) {
        return;
    }

    int& made = m_root_made[tracked.root];
    const int room = std::max(0, perk.end_most - made);
    const int wanted =
        tracked.generation > 0 && perk.end_again_shots > 0 ? perk.end_again_shots : perk.end_shots;
    const int count = std::min(wanted, room);

    // Their count from this first shot is spent.
    if (count <= 0) {
        return;
    }

    // Homing shots start clear of what the shot hit: back along its way and up.
    std::array<float, 3> from = at;
    if (perk.end_homing && end.reason == kEndImpact) {
        for (int c = 0; c < 3; ++c) {
            from[static_cast<std::size_t>(c)] -=
                tracked.direction[static_cast<std::size_t>(c)] * kSplitBack;
        }
        from[2] += kSplitUp;
    }

    // At the nearest enemies but the one it hit, or scattered round. After a hit, an enemy ahead
    // along the shot's way is behind what it hit (the wall, or the enemy it hit): a split sent
    // there goes off on it at once. Ahead is within kBlockedAhead of the way when the shot hit an
    // enemy, anywhere in front when it hit a wall or the ground.
    const bool impact = end.reason == kEndImpact;
    const std::array<float, 3>& way = tracked.direction;
    const bool has_way = way[0] != 0.0f || way[1] != 0.0f || way[2] != 0.0f;
    std::vector<Perks::Target> targets;
    if (perk.end_homing) {
        const std::array<float, 3> hit{end.position[0], end.position[1], end.position[2]};
        const std::vector<Perks::Target> near =
            m_perks.targets(from, kHomingRadius, count + 4, end.target);
        bool hit_enemy = false;
        for (const Perks::Target& t : near) {
            hit_enemy = hit_enemy || distance2(t.position, hit) < kVictimReach * kVictimReach;
        }
        const float ahead = hit_enemy ? kBlockedAhead : kWallAhead;
        for (const Perks::Target& t : near) {
            const bool victim = impact && distance2(t.position, hit) < kVictimReach * kVictimReach;
            const std::array<float, 3> to = scaled(
                {t.position[0] - from[0], t.position[1] - from[1], t.position[2] - from[2]}, 1.0f
            );
            const bool blocked =
                impact && has_way && to[0] * way[0] + to[1] * way[1] + to[2] * way[2] > ahead;
            if (!victim && !blocked && static_cast<int>(targets.size()) < count
                && homes_on(t.address) && !walled(from, aim_point(t.address))) {
                targets.push_back(t);
            }
        }
    }

    // A rocket that had a live target in sight passes it on: all its splits fly at it, so one shot
    // deals the damage of all of them (the others pick their own only with no such target).
    if (perk.end_homing) {
        const std::uint32_t parent = inherited(tracked.chase);
        const int parent_slot = parent != 0 ? MobyScan::slot_of(m_view, parent) : -1;
        const bool usable = parent != 0 && parent_slot >= 0 && homes_on(parent)
                            && !walled(from, aim_point(parent));
        if (usable) {
            Perks::Target focus;
            focus.slot = parent_slot;
            focus.address = parent;
            focus.position = m_perks.position_of(parent);
            targets.assign(static_cast<std::size_t>(count), focus);
        }
    }
    const float speed = tracked.speed > 0.0f ? tracked.speed : m_view.per_tick(kDefaultSpeed);

    for (int n = 0; n < count; ++n) {
        Pending shot;
        shot.when = m_tick
                    + (tracked.item == 42 && !perk.end_homing
                           ? static_cast<std::uint32_t>(m_view.ticks(kGravityScatterTicks))
                           : 0u);
        shot.oclass = perk.end_class;
        shot.item = tracked.item;
        shot.at = from;
        shot.generation = tracked.generation + 1;
        shot.root = tracked.root;
        const float angle = 6.2831853f * static_cast<float>(n) / static_cast<float>(count);

        // At an enemy; else round about.
        if (static_cast<std::size_t>(n) < targets.size()) {
            const Perks::Target& t = targets[static_cast<std::size_t>(n)];
            shot.target = t.slot;
            const std::array<float, 3> aim = aim_point(t.address);
            shot.velocity =
                scaled({aim[0] - from[0], aim[1] - from[1], aim[2] - from[2]}, speed);
        } else if (perk.end_homing && impact && has_way) {
            // No enemy to seek: fanned out back from what it hit, a little up.
            const float side =
                (static_cast<float>(n) - 0.5f * static_cast<float>(count - 1)) * 0.6f;
            const std::array<float, 3> back = turned({-way[0], -way[1], 0.0f}, side);
            shot.velocity = scaled({back[0], back[1], 0.35f}, speed);
        } else if (perk.end_homing) {
            shot.velocity = scaled({std::cos(angle), std::sin(angle), 0.15f}, speed);
        } else {
            shot.velocity =
                {m_view.per_tick(kScatterSpeed) * std::cos(angle),
                 m_view.per_tick(kScatterSpeed) * std::sin(angle), m_view.per_tick(kScatterUp)};
        }

        // Out of the enemy or the ground it ended on.
        shot.at = moved(
            perk.end_homing ? from : at, shot.velocity, perk.end_homing ? kEndClear : kScatterClear
        );
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

    // A RYNO II split rocket is a smaller one: its moby's scale (+0x2C), which the game's matrix
    // step multiplies its rows by each tick.
    if (shot.oclass == kRynoRocket && shot.generation > 0 && (shot.flags & kSpawnDetonate) == 0) {
        const std::uint32_t moby = MobyScan::slot_address(m_view, slot);
        if (moby != 0) {
            const openrac::Memory memory = m_view.memory();
            const float scale = memory.read<float>(moby + kMobyScale);
            if (scale > 0.0f && scale < 100.0f) {
                memory.write(moby + kMobyScale, scale * kSplitScale);
            }
        }
    }

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
            m_view.per_tick(kDefaultSpeed)
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
    steer_rockets();

    // The Minirocket Tube's rockets split in flight from Epsilon.
    const ShotPerk mini = shot_perk(27, stage(27), tier(27));

    if (mini.split_after > 0) {
        for (auto& [slot, tracked] : m_tracked) {
            // Not a rocket the hero fired, split already, or not yet.
            if (tracked.oclass != kMiniRocket || tracked.split || tracked.generation != 0
                || m_tick < tracked.born + static_cast<std::uint32_t>(m_view.ticks(mini.split_after))) {
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
        const std::optional<std::uint32_t> rgb = weapon_effect_rgb(item, level, m_view.sixtieths(tick));
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

bool Shots::walled(const std::array<float, 3>& from, const std::array<float, 3>& to) const {
    bool blocked = false;
    return m_mod.level_line(from.data(), to.data(), blocked) && blocked;
}

bool Shots::homes_on(std::uint32_t target) const {
    // Dead, dying, hidden, no health.
    if (!m_perks.targetable(target)) {
        return false;
    }

    // Asleep: the moby loop left it out (a port without the call: taken as running).
    openrac_moby_run run{};
    run.size = sizeof run;
    const int slot = MobyScan::slot_of(m_view, target);
    return slot >= 0
           && (!m_mod.moby_run(slot, run)
               || (run.ran != OPENRAC_MOBY_RUN_FAR && run.ran != OPENRAC_MOBY_RUN_NONE));
}

std::array<float, 3> Shots::aim_point(std::uint32_t target) const {
    std::array<float, 3> at = m_perks.position_of(target);
    const float height = m_perks.aim_height(target);
    at[2] += height > 0.0f ? height : kDefaultAimHeight;
    return at;
}

void Shots::lock(std::uint32_t pvars, std::uint32_t target, bool split) const {
    const openrac::Memory memory = m_view.memory();
    const std::array<float, 3> aim = aim_point(target);
    const float height = m_perks.aim_height(target);

    // Its target, the height and point it seeks (no lead yet), no offset, homing, under the gate.
    memory.write(pvars + kRocketTarget, target);
    memory.write(pvars + kRocketHeight, height > 0.0f ? height : kDefaultAimHeight);
    for (std::uint32_t c = 0; c < 3; ++c) {
        memory.write(pvars + kRocketSeek + 4u * c, aim[c]);
        memory.write(pvars + kRocketOffset + 4u * c, 0.0f);
    }
    memory.write(pvars + kRocketHoming, std::uint8_t{1});
    if (memory.read<std::int16_t>(pvars + kRocketLife) > kHomingLife) {
        memory.write(pvars + kRocketLife, kHomingLife);
    }

    // A split's wobble is off (the hero's own rockets keep their spiral).
    if (split) {
        memory.write(pvars + kRocketYaw, 0.0f);
    }
}

void Shots::steer_rockets() {
    const openrac::Memory memory = m_view.memory();

#ifdef NGPP_TEST_SETUP
    // Hidden test runs: what a homing rocket did with a target, and what the target was.
    const auto note = [&](const char* what, std::uint32_t target, float distance) {
        const std::optional<int> moby_class = m_view.moby_class(target);
        m_mod.info(
            std::string("test: ryno rocket ") + what + " target class "
            + std::to_string(moby_class.value_or(-1)) + " state "
            + std::to_string(memory.read<std::uint8_t>(target + kMobyStateByte)) + " at "
            + std::to_string(distance)
        );
    };
#else
    const auto note = [](const char*, std::uint32_t, float) {};
#endif

    for (auto& [slot, tracked] : m_tracked) {
        // Not a RYNO II rocket.
        if (tracked.oclass != kRynoRocket) {
            continue;
        }

        const std::uint32_t moby = MobyScan::slot_address(m_view, slot);

        // Gone (its end comes through on_end).
        if (moby == 0 || !m_perks.alive(moby, kRynoRocket)) {
            continue;
        }

        const auto pvars = memory.read<std::uint32_t>(moby + kMobyVars);

        // No variables to read.
        if (!GameView::is_memory(pvars, kRocketVarsBytes)) {
            continue;
        }

        auto target = memory.read<std::uint32_t>(pvars + kRocketTarget);

        // A split homes; a rocket the hero fired homes once the game locked it on.
        tracked.homing = tracked.homing || tracked.generation > 0 || target != 0;

        // The hero's rockets fired with no lock fly as the game sends them.
        if (!tracked.homing) {
            continue;
        }

        // A split's wobble off: the game's spawn gives a rocket made with no target a pitch of 4 to
        // 10 degrees a tick (the hero's unaimed rockets' corkscrew), which loops a split in place
        // until its time runs out.
        if (tracked.generation > 0 && memory.read<float>(pvars + kRocketYaw) != 0.0f) {
            memory.write(pvars + kRocketYaw, 0.0f);
        }

        const std::array<float, 3> at = m_perks.position_of(moby);
        const float speed = memory.read<float>(pvars + kRocketSpeed);
        const Vec3 forward = unit(
            {memory.read<float>(moby + kMobyForward), memory.read<float>(moby + kMobyForward + 4u),
             memory.read<float>(moby + kMobyForward + 8u)},
            {tracked.direction[0], tracked.direction[1], tracked.direction[2]}
        );
        Chase& chase = tracked.chase;

        // A target the game (or the port) gave it: the chase's, unless given up before.
        if (target != chase.target) {
            if (target != 0 && dropped(chase, target)) {
                memory.write(pvars + kRocketTarget, std::uint32_t{0});
                target = 0;
            }
            retarget(chase, target);
        }

        // Its target gone, dying, hidden or not out yet: let go, straight on.
        if (target != 0 && !homes_on(target)) {
            note("let go of gone or hidden", target, 0.0f);
            drop(chase);
            memory.write(pvars + kRocketTarget, std::uint32_t{0});
            target = 0;
            ++m_counts.dropped;
        }

        // None: straight on (the heading the game keeps for a rocket with no target is its own),
        // and the nearest enemy in reach and in sight taken at once.
        if (target == 0) {
            for (std::uint32_t c = 0; c < 3; ++c) {
                memory.write(pvars + kRocketHeading + 4u * c, forward[c]);
            }

            for (const Perks::Target& t : m_perks.targets(at, kHomingRadius, 6, -1)) {
                if (!dropped(chase, t.address) && homes_on(t.address)
                    && !walled(at, aim_point(t.address))) {
                    target = t.address;
                    break;
                }
            }

            if (target == 0) {
                continue;
            }

            lock(pvars, target, tracked.generation > 0);
            note("retargeted to", target, 0.0f);
            retarget(chase, target);
            ++m_counts.retargets;
        }

        const std::array<float, 3> aim = aim_point(target);
        const Vec3 to{aim[0] - at[0], aim[1] - at[1], aim[2] - at[2]};
        const float distance = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        const bool checked = (m_tick + static_cast<std::uint32_t>(slot)) % kSightEvery == 0;
        ChaseLimits limits;
        limits.reach = std::max(kReach, kReachSteps * speed);
        limits.most_ticks = m_view.ticks(kChaseTicks);
        const Verdict verdict = judge(chase, distance, checked, checked && walled(at, aim), limits);

        // Unreachable: no target, straight on.
        if (verdict == Verdict::Drop) {
            note(chase.blocked > 0 ? "let go of walled" : "let go of unreached", target, distance);
            drop(chase);
            memory.write(pvars + kRocketTarget, std::uint32_t{0});
            for (std::uint32_t c = 0; c < 3; ++c) {
                memory.write(pvars + kRocketHeading + 4u * c, forward[c]);
            }
            ++m_counts.dropped;
            continue;
        }

        // At its target and the game's hit has not come (the point it seeks is off what can be
        // hit): it goes off there, and its time ends.
        if (verdict == Verdict::Strike) {
            note("went off unhit on", target, distance);
            if (!blast(aim, tracked.item)) {
                m_perks.strike(target, tracked.item);
            }
            memory.write(pvars + kRocketLife, std::int16_t{1});
            strike(chase);
            memory.write(pvars + kRocketTarget, std::uint32_t{0});
            ++m_counts.strikes;
            continue;
        }

        // Steered at it: a turn that grows as it closes, so it never circles (homing.h). The
        // game's lead then turns it no further from the target, and no offset takes it off it.
        const Vec3 heading =
            steer(forward, to, turn_limit(speed, distance, m_view.per_tick(kBaseTurn)));
        const Vec3 right = unit(
            {heading[1] * 1.0f - heading[2] * 0.0f, heading[2] * 0.0f - heading[0] * 1.0f, 0.0f},
            {0.0f, -1.0f, 0.0f}
        );
        const Vec3 up{
            right[1] * heading[2] - right[2] * heading[1],
            right[2] * heading[0] - right[0] * heading[2],
            right[0] * heading[1] - right[1] * heading[0]
        };
        for (std::uint32_t c = 0; c < 3; ++c) {
            memory.write(moby + kMobyForward + 4u * c, heading[c]);
            memory.write(moby + kMobyRight + 4u * c, right[c]);
            memory.write(moby + kMobyUp + 4u * c, up[c]);
            memory.write(pvars + kRocketHeading + 4u * c, heading[c]);
            memory.write(pvars + kRocketOffset + 4u * c, 0.0f);
        }
    }
}

Shots::Counts Shots::take_counts() {
    const Counts out = m_counts;
    m_counts = Counts{};
    return out;
}

}  // namespace ng_plus_plus
