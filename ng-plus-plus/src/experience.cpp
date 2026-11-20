// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The steps and the rows of the last versions (experience.h).

#include "experience.h"

#include <string>

namespace ng_plus_plus {
namespace {

// Ticks the line at the top of the screen stays up: two and a half seconds at 60 Hz.
constexpr int kNoticeTicks = 150;

// Ticks an in-hand level-up waits for the game's popup at its animation's end: ten seconds, far
// past the animation, after which a lookup is the game's again.
constexpr int kPendingTicks = 600;

// The game's message line shows for its own default time when given this.
constexpr std::int32_t kMessageDefaultTime = -1;

}  // namespace

void Experience::take_step(int item, StepCause cause) {
    const GameView& view = m_weapons.view();
    m_tiers.step(item);

    // Its experience starts again, as the game's own Mega purchase does.
    view.clear_experience(item);

    // The ammo filled to the new most, from the last real version's as the mod found it.
    const RowWrite& written = m_rows[static_cast<std::size_t>(item)];
    const std::int32_t found = view.current_row(item).ammo_max();
    const std::int32_t base = written.ammo_base > 0 ? written.ammo_base : found;
    const std::int32_t most = ammo_max(kind_of(item), m_tiers.own_level(item), base);

    // Endless ammo has nothing to fill.
    if (most > 0) {
        view.set_ammo(item, most);
    }

    // Bought: the vendor's own feedback names it (the text resolver's named_entry).
    if (cause == StepCause::Earned) {
        show_earned(item);
    } else {
        notice(item);
    }

    view.mod().info(
        "item " + std::to_string(item) + ": level " + std::to_string(m_tiers.own_level(item))
    );
}

void Experience::show_earned(int item) {
    const GameView& view = m_weapons.view();

    // The weapon in hand: as the game's own level-up of it, the hero's upgrade animation morphing
    // into the row it has. At its end the game shows its popup and line, which name the weapon at
    // its tier through the text resolver.
    if (item == view.held() && view.upgrade_fx() != nullptr) {
        view.upgrade_fx()(item, view.slot(item));
        m_pending = item;
        m_pending_ticks = kPendingTicks;
        return;
    }

    const bool can_announce = view.banner() != nullptr && view.message() != nullptr;

    // As the game's own level-up of a weapon out of hand (func_00288508): its popup and sound,
    // then the line of the weapon's name, which the text resolver names at its tier.
    if (can_announce && m_text_hooked && view.has_slots()) {
        m_announcing = item;
        view.banner()(kLevelUpKey, kLevelUpSelection);
        view.message()(view.current_row(item).name_id(), kMessageDefaultTime);
        m_announcing = -1;
        return;
    }

    notice(item);
}

std::optional<int> Experience::announcing() const {
    // Only inside show_earned's call of the message line.
    if (m_announcing < 0) {
        return std::nullopt;
    }

    return m_announcing;
}

std::optional<int> Experience::level_up_pending() const {
    // None started, or its time is up.
    if (m_pending < 0) {
        return std::nullopt;
    }

    return m_pending;
}

void Experience::step_by_experience() {
    // A guest plays the host's levels; and with the clock all zeros (the title, a new game before
    // its stamp) the save's levels and started marks come back with the clock, so zeroing the
    // experience now would take what the weapons earned in the games before.
    if (m_tiers.party_on() || !m_tiers.clock_known()) {
        return;
    }

    const GameView& view = m_weapons.view();
    const int held = view.held();

    // Every item the tables know.
    for (int item = 0; item < kItems; ++item) {
        const WeaponView weapon = m_weapons.weapon(item);

        // Only an owned weapon the mod knows, at its last version.
        if (weapon.kind == Kind::None || !weapon.owned || !weapon.at_last) {
            continue;
        }

        // The game's count at a last version is all it ever had with the weapon (from before the
        // mod, and from past games): the mod's starts at 0 the first time it sees the weapon there.
        if (!m_tiers.started(item)) {
            m_tiers.start(item);

            // A tiered weapon's count already started with its tier.
            if (m_tiers.own_level(item) == 0) {
                view.clear_experience(item);
            }

            continue;
        }

        const int level = m_tiers.own_level(item);

        // Not yet enough for the next tier, or the next is bought.
        if (!exp_reached(weapon.kind, level, true, view.experience(item))) {
            continue;
        }

        // The weapon in hand levels when the hero can play its animation, as the game's.
        if (item == held && view.upgrade_fx() != nullptr && !view.hero_ready()) {
            continue;
        }

        take_step(item, StepCause::Earned);
        m_tiers.mark_changed();
    }
}

void Experience::write_rows() {
    // Every item the tables know.
    for (int item = 0; item < kItems; ++item) {
        const WeaponView weapon = m_weapons.weapon(item);

        // Only the last version's row of a weapon the mod knows.
        if (weapon.kind == Kind::None || !weapon.at_last) {
            continue;
        }

        RowWrite& written = m_rows[static_cast<std::size_t>(item)];
        const std::int32_t ammo_now = weapon.row.ammo_max();

        // Not what the mod wrote: the game's (or another mod's) own, which the level brought.
        if (ammo_now != written.ammo_written) {
            written.ammo_base = ammo_now;
        }

        const std::int32_t most = ammo_max(weapon.kind, weapon.level, written.ammo_base);

        // The game keeps it in a u16; ammo_max never goes past.
        if (most != ammo_now) {
            weapon.row.set_ammo_max(static_cast<std::uint16_t>(most));
        }

        written.ammo_written = most;
        const std::int32_t goal_now = weapon.row.exp_goal();

        // Not what the mod wrote: the game's (or a newly loaded level's copy's) own.
        if (!written.goal_seen || goal_now != written.goal_written) {
            written.goal_base = goal_now;
            written.goal_seen = true;
        }

        // Toward the next tier's goal, or the game's own (a full bar) when it has none.
        const std::int32_t goal = exp_goal(weapon.kind, weapon.level);
        const std::int32_t wanted = goal > 0 ? goal : written.goal_base;

        // Written only when it differs, so the game's own value stays recognisable.
        if (wanted != goal_now) {
            weapon.row.set_exp_goal(wanted);
        }

        written.goal_written = wanted;
    }
}

void Experience::count_down_notice() {
    // An in-hand level-up whose popup never came: forgotten.
    if (m_pending >= 0 && --m_pending_ticks <= 0) {
        m_pending = -1;
    }

    // No line up.
    if (m_notice_ticks == 0) {
        return;
    }

    --m_notice_ticks;

    // The line's time is up: cleared.
    if (m_notice_ticks == 0) {
        m_weapons.view().mod().api().play_notice(m_weapons.view().mod().handle(), "");
    }
}

void Experience::name_weapon_in_hand() {
    const int held = m_weapons.view().held();

    // Only when the hand changes.
    if (held == m_held) {
        return;
    }

    m_held = held;

    // A tiered weapon, and not over a step's line still up.
    if (held >= 0 && held < kItems && m_tiers.level(held) != 0 && m_notice_ticks == 0) {
        notice(held);
    }
}

void Experience::notice(int item) {
    // The player switched the line off.
    if (!m_notices) {
        return;
    }

    const openrac::Mod& mod = m_weapons.view().mod();
    const std::string name = m_weapons.name_at(item, m_tiers.level(item));
    mod.api().play_notice(mod.handle(), name.c_str());
    m_notice_ticks = kNoticeTicks;
}

}  // namespace ng_plus_plus
