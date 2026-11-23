// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The Zodiac's kill credit (zodiac_credit.h).

#include "zodiac_credit.h"

#include <iterator>

#include "tuning_rules.h"

namespace ng_plus_plus {

static_assert(kZodiacItem == kZodiac, "the Zodiac's item");

void ZodiacCredit::mark(std::uint32_t moby, std::uint32_t tick, int item, std::uint32_t ticks) {
    Mark& mark = m_marks[moby];
    const std::uint32_t length = ticks > 0 ? ticks : kZodiacCreditTicks;

    // A mark of another item that still stands is replaced; the same item's only grows.
    if (mark.item == item && mark.tick + mark.ticks > tick + length) {
        return;
    }

    mark.tick = tick;
    mark.ticks = length;
    mark.item = item;
}

bool ZodiacCredit::marked(std::uint32_t moby, std::uint32_t tick) const {
    const auto it = m_marks.find(moby);
    return it != m_marks.end() && tick >= it->second.tick
           && tick - it->second.tick <= it->second.ticks;
}

int ZodiacCredit::take_item(std::uint32_t moby, std::uint32_t tick) {
    const auto it = m_marks.find(moby);

    if (it == m_marks.end()) {
        return -1;
    }

    const Mark mark = it->second;
    m_marks.erase(it);
    return tick >= mark.tick && tick - mark.tick <= mark.ticks ? mark.item : -1;
}

void ZodiacCredit::prune(std::uint32_t tick) {
    for (auto it = m_marks.begin(); it != m_marks.end();) {
        it = tick > it->second.tick + it->second.ticks ? m_marks.erase(it) : std::next(it);
    }
}

bool credit_zodiac(
    ZodiacCredit& credit, std::uint32_t moby, std::uint32_t tick, std::uint8_t& weapon
) {
    const int item = credit.take_item(moby, tick);

    if (item < 0) {
        return false;
    }

    weapon = static_cast<std::uint8_t>(item);
    return true;
}

}  // namespace ng_plus_plus
