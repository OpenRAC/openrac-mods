// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The Zodiac's kill credit (zodiac_credit.h).

#include "zodiac_credit.h"

#include <iterator>

#include "tuning_rules.h"

namespace ng_plus_plus {

static_assert(kZodiacItem == kZodiac, "the Zodiac's item");

void ZodiacCredit::mark(
    std::uint32_t moby, std::uint32_t tick, int item, std::uint32_t ticks, int expect
) {
    const auto [it, fresh] = m_marks.try_emplace(moby);
    Mark& mark = it->second;
    const std::uint32_t length = ticks > 0 ? ticks : kZodiacCreditTicks;
    const bool same = !fresh && mark.item == item && stands(mark, tick);

    // A mark of another item that still stands is replaced; the same item's only grows.
    if (same && mark.tick + mark.ticks > tick + length) {
        return;
    }

    // The same item's standing mark keeps the byte it expects; a new one takes this one's.
    if (!same) {
        mark.expect = expect;
    }

    mark.tick = tick;
    mark.ticks = length;
    mark.item = item;
}

bool ZodiacCredit::marked(std::uint32_t moby, std::uint32_t tick) const {
    const auto it = m_marks.find(moby);
    return it != m_marks.end() && stands(it->second, tick);
}

bool ZodiacCredit::marked_for(std::uint32_t moby, std::uint32_t tick, int item) const {
    const auto it = m_marks.find(moby);
    return marked(moby, tick) && it->second.item == item;
}

int ZodiacCredit::take_item(std::uint32_t moby, std::uint32_t tick, int weapon) {
    const auto it = m_marks.find(moby);

    if (it == m_marks.end()) {
        return -1;
    }

    const Mark mark = it->second;
    m_marks.erase(it);

    // Run out, or a drain's mark on an enemy another weapon has hit since.
    if (!stands(mark, tick) || (mark.expect >= 0 && weapon >= 0 && weapon != mark.expect)) {
        return -1;
    }

    return mark.item;
}

void ZodiacCredit::prune(std::uint32_t tick) {
    for (auto it = m_marks.begin(); it != m_marks.end();) {
        it = tick > it->second.tick + it->second.ticks ? m_marks.erase(it) : std::next(it);
    }
}

bool credit_zodiac(
    ZodiacCredit& credit, std::uint32_t moby, std::uint32_t tick, std::uint8_t& weapon
) {
    const int item = credit.take_item(moby, tick, weapon);

    if (item < 0) {
        return false;
    }

    weapon = static_cast<std::uint8_t>(item);
    return true;
}

ZodiacAction zodiac_action(float health, std::uint8_t path, float drain) {
    // Over the line, and still alive after the drain.
    if (health > kZodiacKillHealth && health - drain > 0.0f) {
        return ZodiacAction::Drain;
    }

    // The gun's own test, in its order: 1 or 2 its owner's, then anything but 3 vaporized.
    if (path == 1 || path == 2) {
        return ZodiacAction::Owner;
    }

    return path != 3 ? ZodiacAction::Vaporize : ZodiacAction::Skip;
}

ZodiacAction zodiac_reach_action(
    float health, std::uint8_t path, float drain, bool started, bool owner_runs
) {
    // Not started: its start-up would undo whatever the pulse did.
    if (!started) {
        return ZodiacAction::Skip;
    }

    const ZodiacAction action = zodiac_action(health, path, drain);

    // Its owner's, with no update to run the death: left until the loop runs it.
    if (action == ZodiacAction::Owner && !owner_runs) {
        return ZodiacAction::Skip;
    }

    return action;
}

ZodiacDeath zodiac_death(float health, std::uint8_t path) {
    // The gun records a death with health left; a drained or hit enemy has none.
    if (health <= 0.0f) {
        return ZodiacDeath::Own;
    }

    return path == 1 || path == 2 ? ZodiacDeath::Owner : ZodiacDeath::Vaporized;
}

std::string zodiac_summary(const ZodiacStats& stats, const std::string& step) {
    std::string line = "the Zodiac in the last minute: " + std::to_string(stats.pulses)
                       + " pulses; kills " + std::to_string(stats.vaporized) + " vaporized, "
                       + std::to_string(stats.owner) + " by their owner's explosion, "
                       + std::to_string(stats.own) + " own deaths after the drain; reach "
                       + std::to_string(stats.reach_vaporized) + " vaporized, "
                       + std::to_string(stats.reach_owner) + " to their owner, "
                       + std::to_string(stats.reach_drained) + " drained, "
                       + std::to_string(stats.reach_skipped) + " left; "
                       + std::to_string(stats.credited) + " deaths credited to item "
                       + std::to_string(kZodiacItem) + ", " + std::to_string(stats.not_credited)
                       + " near a pulse not";

    // The weapon the last of those named instead.
    if (stats.not_credited > 0) {
        line += " (last named " + std::to_string(stats.last_other) + ")";
    }

    // The experience in the game's units (raw / 32, as the bar shows it).
    line += "; experience +" + std::to_string(stats.experience / 32) + " in "
            + std::to_string(stats.awards) + " awards; next step: " + step;
    return line;
}

}  // namespace ng_plus_plus
