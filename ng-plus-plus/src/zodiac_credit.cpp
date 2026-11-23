// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The Zodiac's kill credit (zodiac_credit.h).

#include "zodiac_credit.h"

#include <iterator>

#include "tuning_rules.h"

namespace ng_plus_plus {

bool ZodiacCredit::take(std::uint32_t moby, std::uint32_t tick) {
    const auto it = m_marks.find(moby);

    if (it == m_marks.end()) {
        return false;
    }

    const std::uint32_t marked = it->second;
    m_marks.erase(it);
    return tick >= marked && tick - marked <= kZodiacCreditTicks;
}

void ZodiacCredit::prune(std::uint32_t tick) {
    for (auto it = m_marks.begin(); it != m_marks.end();) {
        it = tick > it->second + kZodiacCreditTicks ? m_marks.erase(it) : std::next(it);
    }
}

bool credit_zodiac(
    ZodiacCredit& credit, std::uint32_t moby, std::uint32_t tick, std::uint8_t& weapon
) {
    if (!credit.take(moby, tick)) {
        return false;
    }

    weapon = static_cast<std::uint8_t>(kZodiac);
    return true;
}

}  // namespace ng_plus_plus
