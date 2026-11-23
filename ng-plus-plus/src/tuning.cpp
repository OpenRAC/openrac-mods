// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.5.1's weapon tuning in the game (tuning.h).

#include "tuning.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>

#include "discs.h"
#include "perk_rules.h"
#include "tuning_rules.h"

namespace ng_plus_plus {
namespace {

// A moby's pointer to its variables (the health record's address is their first word).
constexpr std::uint32_t kMobyVars = 0x68u;

// A moby's flags (+0x34): 0x20 it has a health record (moby_get_private_value_0).
constexpr std::uint32_t kMobyFlags = 0x34u;
constexpr std::uint16_t kMobyHasHealth = 0x20u;

// The health record: the health (f32) and the last attacker (moby_update_aim_target_strength).
constexpr std::uint32_t kHealth = 0x00u;
constexpr std::uint32_t kHealthAttacker = 0x18u;
constexpr std::uint32_t kHealthRecordBytes = 0x20u;

// The Tesla Claw's beam (*(gun+0x68), tesla_claw_update): its two locks.
constexpr std::uint32_t kBeamLockA = 0x20u;
constexpr std::uint32_t kBeamLockB = 0x38u;
constexpr std::uint32_t kBeamBytes = 0x48u;

// A hit request's kind and flag (bytes +0x18, +0x19): the claw's own hit on a lock is 5 and 3,
// the Lancer's round 1 and 1 (hero_tether_beam_update, lancer_update).
constexpr std::uint32_t kHitKind = 0x18u;
constexpr std::uint32_t kHitFlag = 0x19u;
constexpr std::uint8_t kClawKind = 5;
constexpr std::uint8_t kClawFlag = 3;
constexpr std::uint8_t kRoundKind = 1;
constexpr std::uint8_t kRoundFlag = 1;
constexpr std::uint32_t kRequestBytes = 0x40u;

// Ticks a lock the claw let go of keeps its schedule (the game drops a lock and takes it again in
// the next frame every 50 frames).
constexpr std::uint32_t kLockGrace = 3u;

// The Zodiac's gun's state byte.
constexpr std::uint32_t kGunState = 0x20u;

// The decoy's health in its variables (decoy_create_projectile).
constexpr std::uint32_t kDecoyHealth = 0x24u;

}  // namespace

void WeaponTuning::start() {
    m_mod.info(
        m_steady_tesla ? "the Tesla Claw hits steadily (setting)"
                       : "the Tesla Claw hits as the game's (setting)"
    );
}

bool WeaponTuning::active() const {
    return m_view.loaded_level() >= 0 && !m_weapons.tiers().party_on();
}

std::uint32_t WeaponTuning::health_record(std::uint32_t moby) const {
    // Not a moby, or one without a health record.
    if (!GameView::is_memory(moby, kMobyRecordBytes)) {
        return 0;
    }

    const openrac::Memory memory = m_view.memory();

    if ((memory.read<std::uint16_t>(moby + kMobyFlags) & kMobyHasHealth) == 0) {
        return 0;
    }

    const auto vars = memory.read<std::uint32_t>(moby + kMobyVars);

    if (!GameView::is_memory(vars, 4)) {
        return 0;
    }

    const auto record = memory.read<std::uint32_t>(vars);
    return GameView::is_memory(record, kHealthRecordBytes) ? record : 0;
}

void WeaponTuning::credit_kill(std::uint32_t victim) {
    const std::uint32_t record = health_record(victim);

    // No health record: not a kill the game awards experience for (its mark is spent anyway).
    if (record == 0) {
        m_credit.take(victim, m_tick);
        return;
    }

    const openrac::Memory memory = m_view.memory();

    // Within a pulsing Zodiac's range: the pulse took it (it posts no hit, and the game's own
    // pick may run before or after the mod's tick, so the gun is read as the death is recorded).
    // The reach marks its own.
    for (const ScannedMoby& moby : m_scan.mobys()) {
        if (moby.address != victim) {
            continue;
        }

        for (const ScannedMoby& gun : m_scan.mobys()) {
            if (gun.moby_class == kZodiacClass
                && memory.read<std::uint8_t>(gun.address + kGunState) == kZodiacPulse
                && distance2(moby.position, gun.position) <= kZodiacRange * kZodiacRange) {
                // Not over a perk's mark: that names its own weapon.
                if (!m_credit.marked(victim, m_tick)) {
                    m_credit.mark(victim, m_tick);
                }

                break;
            }
        }

        break;
    }

    auto weapon = memory.read<std::uint8_t>(record + kHealthWeapon);

    if (credit_zodiac(m_credit, victim, m_tick, weapon)) {
        memory.write(record + kHealthWeapon, weapon);
    }
}

bool WeaponTuning::is_decoy(std::uint32_t victim) const {
    const std::optional<int> moby_class = m_view.moby_class(victim);
    return moby_class && *moby_class == kDecoyClass;
}

float WeaponTuning::decoy_hit_scale() const {
    const EnemySettings& enemies = m_combat.enemies();
    return enemies.scaling ? enemy_damage(m_view.cycle(), enemies.strength) : 1.0f;
}

float WeaponTuning::claw_hit(Combat::PostHitFn post, std::uint32_t victim, std::uint32_t request) {
    // Off, a guest, or no request to read.
    if (!m_steady_tesla || !active() || post == nullptr
        || !GameView::is_memory(request, kRequestBytes)) {
        return 1.0f;
    }

    const openrac::Memory memory = m_view.memory();
    const auto source = memory.read<std::uint32_t>(request + kHitSource);
    const std::optional<int> source_class = m_view.moby_class(source);

    // Only the claw's own hit on a lock (its beam's sweep hits crates and the like every frame).
    if (!source_class || *source_class != kTeslaClass
        || memory.read<std::uint8_t>(request + kHitKind) != kClawKind
        || memory.read<std::uint8_t>(request + kHitFlag) != kClawFlag) {
        return 1.0f;
    }

    // Kept for the steady hits: the request and the damage the game gave it.
    std::memcpy(m_claw_hit.data(), memory.pointer(request), kRequestBytes);
    m_claw_damage = memory.read<float>(request + kHitDamage);
    m_post = post;

    // This lock's next steady hit a full step from now.
    Lock& lock = m_locks[victim];
    lock.next = m_tick + static_cast<std::uint32_t>(m_view.ticks(kTeslaHitTicks));
    lock.seen = m_tick;
    return kTeslaShare;
}

void WeaponTuning::steady_hit(std::uint32_t lock, std::uint32_t claw) {
    const openrac::Memory memory = m_view.memory();
    const std::uint32_t hero = m_view.hero_moby();
    const std::uint32_t record = health_record(lock);

    // No health to hurt, or none left (a dying enemy the claw still holds).
    if (record == 0 || memory.read<float>(record + kHealth) <= 0.0f) {
        return;
    }

    // The attacker the victim did not see last, so its same-attacker rule never zeroes the hit.
    const auto last = memory.read<std::uint32_t>(record + kHealthAttacker);
    const std::uint32_t attacker = last == claw ? hero : claw;

    openrac_gaddr saved = 0;
    const openrac_gaddr room = m_mod.api().stack_push(kRequestBytes, &saved);
    std::memcpy(memory.pointer(room), m_claw_hit.data(), kRequestBytes);
    memory.write(room + kHitSource, attacker);
    memory.write(room + kHitKind, kRoundKind);
    memory.write(room + kHitFlag, kRoundFlag);
    memory.write(room + kHitDamage, m_claw_damage * kTeslaShare);
    m_combat.post_hit_with(m_post, lock, room);
    m_mod.api().stack_pop(saved);
    ++m_steady;

#ifdef NGPP_TEST_SETUP
    // Test runs: the lock's health at its first steady hits (each lands a frame later).
    if (m_steady <= 24) {
        m_mod.info(
            "test: steady hit " + std::to_string(m_steady) + " on " + std::to_string(lock)
            + " from " + (attacker == hero ? "hero" : "claw") + ", health "
            + std::to_string(memory.read<float>(record + kHealth))
        );
    }
#endif

    // Once, for a player's log.
    if (!m_logged_tesla) {
        m_logged_tesla = true;
        m_mod.info("first steady Tesla Claw hit: " + std::to_string(m_claw_damage * kTeslaShare));
    }
}

void WeaponTuning::tesla() {
    // Off, or no hit of the claw's own to post as yet.
    if (!m_steady_tesla || m_post == nullptr || m_claw_damage <= 0.0f) {
        return;
    }

    const openrac::Memory memory = m_view.memory();

    // Each claw (the hero's one gun moby of class 177).
    for (const ScannedMoby& gun : m_scan.mobys()) {
        if (gun.moby_class != kTeslaClass) {
            continue;
        }

        const auto beam = memory.read<std::uint32_t>(gun.address + kMobyVars);

        // No beam laid out.
        if (!GameView::is_memory(beam, kBeamBytes)) {
            continue;
        }

        // Its two locks (zero while the trigger is up or the ammo is out).
        for (const std::uint32_t at : {kBeamLockA, kBeamLockB}) {
            const auto target = memory.read<std::uint32_t>(beam + at);

            // No lock, or a dead one.
            if (target == 0 || !GameView::is_memory(target, kMobyRecordBytes)
                || memory.read<std::uint8_t>(target + kMobyStateByte) >= 0x80u) {
                continue;
            }

            auto [it, fresh] = m_locks.try_emplace(target);
            Lock& lock = it->second;

            // A lock new to the mod, or one let go of too long ago: the game's own hit is now.
            if (fresh || m_tick > lock.seen + kLockGrace) {
                lock.next = m_tick + static_cast<std::uint32_t>(m_view.ticks(kTeslaHitTicks));
            }

            lock.seen = m_tick;

            // Its steady hit.
            if (m_tick >= lock.next) {
                lock.next = m_tick + static_cast<std::uint32_t>(m_view.ticks(kTeslaHitTicks));
                steady_hit(target, gun.address);
            }
        }
    }

    // The locks let go of.
    for (auto it = m_locks.begin(); it != m_locks.end();) {
        it = m_tick > it->second.seen + kLockGrace ? m_locks.erase(it) : std::next(it);
    }
}

void WeaponTuning::zodiac() {
    const openrac::Memory memory = m_view.memory();
    const EnemySettings& enemies = m_combat.enemies();
    const int level = m_weapons.tiers().level(kZodiac);
    const int cycle = m_view.cycle();

    // Each Zodiac gun pulsing now.
    for (const ScannedMoby& gun : m_scan.mobys()) {
        if (gun.moby_class != kZodiacClass
            || memory.read<std::uint8_t>(gun.address + kGunState) != kZodiacPulse) {
            continue;
        }

        const float reach2 = kZodiacRange * kZodiacRange;

        // Each boss in its reach, in front of the camera or not.
        for (const ScannedMoby& moby : m_scan.mobys()) {
            if (!is_boss(moby.moby_class) || distance2(moby.position, gun.position) > reach2) {
                continue;
            }

            const std::uint32_t record = health_record(moby.address);

            // No health, or none left.
            if (record == 0) {
                continue;
            }

            float health = memory.read<float>(record + kHealth);

            if (health <= 0.0f) {
                continue;
            }

            Drained& d = m_drained[moby.address];

            // The game's own drain this frame (a boss in its cone and over 100 health).
            const bool game_drained =
                d.tick + 1u == m_tick && d.health - health >= kZodiacDrain * 0.99f;
            const float want =
                zodiac_boss_drain(moby.moby_class, level, cycle, enemies.scaling, enemies.strength);
            const float extra = want - (game_drained ? kZodiacDrain : 0.0f);

            // More than the game's: the rest off its health, down to 0 as the game's drain goes.
            if (extra > 0.0f) {
                health = std::max(0.0f, health - extra);
                memory.write(record + kHealth, health);

                // Once, for a player's log.
                if (!m_logged_zodiac) {
                    m_logged_zodiac = true;
                    m_mod.info(
                        "the Zodiac drains boss class " + std::to_string(moby.moby_class) + " by "
                        + std::to_string(want) + " a frame"
                    );
                }
            }

            d.tick = m_tick;
            d.health = health;
        }
    }

    m_credit.prune(m_tick);

    // Bosses not pulsed for a while are forgotten.
    for (auto it = m_drained.begin(); it != m_drained.end();) {
        it = m_tick > it->second.tick + 600u ? m_drained.erase(it) : std::next(it);
    }
}

void WeaponTuning::barrier() {
    const openrac::Memory memory = m_view.memory();
    bool up = false;

    // A barrier alive.
    for (const ScannedMoby& moby : m_scan.mobys()) {
        // Its class.
        if (moby.moby_class == kShieldClass || moby.moby_class == kBarrierClass) {
            up = true;
            break;
        }
    }

    const int tier = tier_of(kind_of(kBarrierItem), m_weapons.tiers().level(kBarrierItem));
    const float endurance = barrier_endurance(tier);

    // None, or no tier: the game's own.
    if (!up || endurance <= 1.0f) {
        m_barrier_up = false;
        return;
    }

    const openrac::gaddr value_at = m_view.address_of(m_view.disc().fade_value);
    const openrac::gaddr hits_at = m_view.address_of(m_view.disc().tesla_hits);
    const auto charge = memory.read<float>(value_at);
    const auto hits = memory.read<std::int32_t>(hits_at);

    // A new barrier: the charge it began with, no arcs.
    if (!m_barrier_up || charge > m_barrier_shown + 0.5f) {
        m_barrier_up = true;
        m_barrier_shown = charge;
        m_barrier_virtual = charge;
        m_barrier_hits = hits > 0 ? hits : 0;
        m_barrier_seen = hits;
        return;
    }

    // The charge the game took off since the mod left it: it runs down that much slower.
    if (charge < m_barrier_shown) {
        m_barrier_virtual =
            barrier_stretched_charge(m_barrier_virtual, m_barrier_shown - charge, endurance);
        m_barrier_shown = m_barrier_virtual;
        memory.write(value_at, m_barrier_shown);
    }

    // The arcs the game counted since: counted for real, shown over the endurance.
    if (hits > m_barrier_seen) {
        m_barrier_hits += hits - m_barrier_seen;
    } else if (hits < m_barrier_seen) {
        m_barrier_hits = hits > 0 ? hits : 0;
    }

    const int shown = barrier_shown_hits(m_barrier_hits, endurance);

    if (shown != hits) {
        memory.write(hits_at, static_cast<std::int32_t>(shown));
    }

    m_barrier_seen = shown;

    // Once, for a player's log.
    if (!m_logged_barrier) {
        m_logged_barrier = true;
        m_mod.info("barrier stretched x" + std::to_string(endurance));
    }
}

void WeaponTuning::decoys() {
    const openrac::Memory memory = m_view.memory();
    const float factor = decoy_health(m_weapons.tiers().level(kDecoyGlove));

    // Each decoy alive now.
    for (const ScannedMoby& moby : m_scan.mobys()) {
        if (moby.moby_class != kDecoyClass) {
            continue;
        }

        auto [it, fresh] = m_decoys.try_emplace(moby.address, moby.moby_class);

        // Seen already.
        if (!fresh) {
            continue;
        }

        const auto vars = memory.read<std::uint32_t>(moby.address + kMobyVars);

        // Its health raised once, as it appears.
        if (GameView::is_memory(vars, kDecoyHealth + 4u)) {
            const auto health = memory.read<float>(vars + kDecoyHealth);

            if (health > 0.0f) {
                memory.write(vars + kDecoyHealth, health * factor);

                // Once, for a player's log.
                if (!m_logged_decoy) {
                    m_logged_decoy = true;
                    m_mod.info("decoy health " + std::to_string(health * factor));
                }
            }
        }
    }

    // The decoys gone.
    for (auto it = m_decoys.begin(); it != m_decoys.end();) {
        it = m_scan.alive(it->first, kDecoyClass) ? std::next(it) : m_decoys.erase(it);
    }
}

void WeaponTuning::tick(std::uint32_t tick) {
    m_tick = tick;
    const int level = m_view.loaded_level();

    // Another level: what the mod held went with the old one's mobys.
    if (level != m_level) {
        m_level = level;
        m_locks.clear();
        m_drained.clear();
        m_credit.clear();
        m_decoys.clear();
        m_post = nullptr;
        m_claw_damage = 0.0f;
    }

    // Outside a level or a guest: nothing.
    if (!active() || !m_scan.read(tick)) {
        return;
    }

    tesla();
    zodiac();
    barrier();
    decoys();

    // A summary for a player's log, about once a minute when the claw hit steadily.
    if (tick >= m_summary_tick + static_cast<std::uint32_t>(m_view.ticks(3600))) {
        m_summary_tick = tick;

        if (m_steady > 0) {
            m_mod.info(
                "Tesla Claw: " + std::to_string(m_steady) + " steady hits in the last minute"
            );
        }

        m_steady = 0;
    }
}

}  // namespace ng_plus_plus
