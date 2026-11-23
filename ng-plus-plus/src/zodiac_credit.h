// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The Zodiac's kills earn the Zodiac experience (0.6.4). The game gives a kill's experience to the
// weapon named by the last-weapon byte (+0x17) of the dead enemy's health record, which a landed
// hit sets (SetDeathBits, func_002A7650: the byte indexes the experience table). The Zodiac's pulse
// posts no hit (tuning_rules.h), so what it kills outright left that byte at whatever weapon hit
// the enemy before, or none; and NG++'s reach kills (perks.cpp) post a copy of an earlier weapon's
// hit, which names that weapon. So each enemy the pulse or the reach takes is marked here, and when
// its death is recorded within kZodiacCreditTicks of the mark, the byte is set to the Zodiac
// first: the game's own award then gives the experience, once, in the same amount as for any
// weapon, and the level-up and tier logic see an ordinary gain. An enemy never marked, or killed
// after the window, keeps the weapon the game named.

#pragma once

#include <cstdint>
#include <unordered_map>

namespace ng_plus_plus {

// The ticks a mark stands: an enemy the pulse or the reach took dies within a few frames (the
// reach's big hit stands for 30 pulse frames at once).
inline constexpr std::uint32_t kZodiacCreditTicks = 20;

// The health record's last-weapon byte.
inline constexpr std::uint32_t kHealthWeapon = 0x17u;

// The marks of the enemies the Zodiac took. Only the game's thread uses it.
class ZodiacCredit {
public:
    // The pulse or the reach took the moby at `moby` at tick `tick`.
    void mark(std::uint32_t moby, std::uint32_t tick) { m_marks[moby] = tick; }

    // Returns whether the death of `moby` at `tick` is the Zodiac's, and spends the mark, so a
    // death is credited once.
    bool take(std::uint32_t moby, std::uint32_t tick);

    // Forgets the marks that ran out by `tick`.
    void prune(std::uint32_t tick);

    // Forgets every mark (another level).
    void clear() { m_marks.clear(); }

    // Returns how many marks stand.
    std::size_t size() const { return m_marks.size(); }

private:
    std::unordered_map<std::uint32_t, std::uint32_t> m_marks;  // Moby address to the mark's tick.
};

// The death of `moby` at `tick` with the health record's last-weapon byte `weapon`: sets it to the
// Zodiac's item when the Zodiac took it, else leaves it. Returns whether it did.
bool credit_zodiac(ZodiacCredit& credit, std::uint32_t moby, std::uint32_t tick, std::uint8_t& weapon);

}  // namespace ng_plus_plus
