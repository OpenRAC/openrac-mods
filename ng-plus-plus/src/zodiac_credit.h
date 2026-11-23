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
#include <string>
#include <unordered_map>

namespace ng_plus_plus {

// The ticks a mark stands: an enemy the pulse or the reach took dies within a few frames (the
// reach's big hit stands for 30 pulse frames at once).
inline constexpr std::uint32_t kZodiacCreditTicks = 20;

// The Zodiac's item (tuning_rules.h's kZodiac).
inline constexpr int kZodiacItem = 43;

// The health record's last-weapon byte.
inline constexpr std::uint32_t kHealthWeapon = 0x17u;

// 0.6.13: how many ticks a mark may be ahead of the clock that reads it. The parts keep their own
// tick, and the reach marks with the perks' before the tuning's has moved on to the same tick; a
// mark from that tick must still stand.
inline constexpr std::uint32_t kZodiacCreditSkew = 2;

// The health record's death path byte (charging_weapon_update, func_L00_00408E58): 0 (or any value
// but 1 to 3) the pulse vaporizes the enemy (SetDeathBits, then the moby deleted); 1 or 2 it records
// the death, sets 2 and leaves the moby to its owner or squad, which runs the death (the plain
// explosion); 3 the pulse never kills it.
inline constexpr std::uint32_t kHealthDeathPath = 0x0Eu;

// 0.6.12: how long a mark stands on an enemy the pulse drained to nothing: its own death (its
// animation, then the explosion that records it) comes well after the 20 ticks of a kill's.
inline constexpr std::uint32_t kZodiacDrainCreditTicks = 600;

// The pulse's health line: what has this much or less dies, more loses the drain.
inline constexpr float kZodiacKillHealth = 100.0f;

// What the pulse does to an enemy (the game's charging_weapon_update, and NG++'s reach the same).
enum class ZodiacAction {
    Skip,      // Death path 3: nothing.
    Vaporize,  // Recorded and deleted, in the pulse's sparks.
    Owner,     // Recorded, death path set to 2, its owner runs the death.
    Drain,     // Over the line: health off, still alive.
};

// Returns what a pulse taking `drain` does to an enemy with `health` and death path byte `path`.
// The game's pulse drains 20 a frame; NG++'s reach takes a whole pulse's drain at once, so an enemy
// drained to nothing dies here, the gun's way, instead of later by its own death.
ZodiacAction zodiac_action(float health, std::uint8_t path, float drain);

// How a death recorded near a pulsing Zodiac came (Combat::record_death, as WeaponTuning sees it).
enum class ZodiacDeath {
    Vaporized,  // The gun's kill, death path 0: the sparks, the moby deleted.
    Owner,      // The gun's kill, death path 1 or 2: its owner's plain explosion follows.
    Own,        // Health gone (the drain, or a hit): the enemy's own death.
};

// Returns how a death near a pulsing gun came, from the health record's `health` and `path` as the
// death is recorded (the gun records it before it changes either).
ZodiacDeath zodiac_death(float health, std::uint8_t path);

// What the Zodiac did since the last summary, for a player's log (0.6.12): each count since then.
struct ZodiacStats {
    int pulses = 0;             // Pulses started.
    int vaporized = 0;          // The gun's kills, vaporized.
    int owner = 0;              // The gun's kills left to their owner (the plain explosion).
    int own = 0;                // Deaths of their own near a pulse (drained, or hit to nothing).
    int reach_vaporized = 0;    // NG++'s reach: kills vaporized.
    int reach_owner = 0;        // NG++'s reach: kills left to their owner.
    int reach_drained = 0;      // NG++'s reach: drained, still alive.
    int reach_skipped = 0;      // NG++'s reach: death path 3, or no way to record the death.
    int credited = 0;           // Deaths recorded with the Zodiac named (item 43).
    int not_credited = 0;       // Deaths near a pulse recorded for another weapon or none.
    int last_other = -1;        // The weapon byte of the last of those.
    std::int64_t experience = 0;  // The Zodiac's raw experience gained in deaths' awards.
    int awards = 0;             // Deaths whose award raised the Zodiac's experience.

    // Returns whether anything happened.
    bool any() const {
        return pulses + vaporized + owner + own + reach_vaporized + reach_owner + reach_drained
                   + reach_skipped + credited + not_credited
               > 0;
    }
};

// Returns the summary line of `stats`, with `step` saying where the Zodiac's next step stands.
std::string zodiac_summary(const ZodiacStats& stats, const std::string& step);

// The marks of the enemies the Zodiac took. Only the game's thread uses it.
class ZodiacCredit {
public:
    // The pulse or the reach took the moby at `moby` at tick `tick`.
    void mark(std::uint32_t moby, std::uint32_t tick) { mark(moby, tick, kZodiacItem, 0); }

    // 0.6.11: a hit NG++ posts for `item` (a field, a blast, a chain hop, the reach) took it. The
    // game names the weapon in hand for such a hit, so the mark names the right one. The mark
    // stands `ticks` (0: kZodiacCreditTicks), and a later mark of another item replaces it.
    // 0.6.13: `expect`, when not -1, is the last-weapon byte as the mark was made (a drain's): a
    // death whose byte changed since was another weapon's hit, and the mark credits nothing.
    void mark(
        std::uint32_t moby, std::uint32_t tick, int item, std::uint32_t ticks, int expect = -1
    );

    // Whether a mark of the moby stands at `tick`.
    bool marked(std::uint32_t moby, std::uint32_t tick) const;

    // Whether a mark of the moby for `item` stands at `tick`.
    bool marked_for(std::uint32_t moby, std::uint32_t tick, int item) const;

    // Returns whether the death of `moby` at `tick` is the Zodiac's, and spends the mark, so a
    // death is credited once.
    bool take(std::uint32_t moby, std::uint32_t tick) { return take_item(moby, tick) >= 0; }

    // Returns the item the death of `moby` at `tick` is credited to and spends the mark, or -1.
    // `weapon` is the death's last-weapon byte (-1 unknown), checked against a drain's mark.
    int take_item(std::uint32_t moby, std::uint32_t tick, int weapon = -1);

    // Forgets the marks that ran out by `tick`.
    void prune(std::uint32_t tick);

    // Forgets every mark (another level).
    void clear() { m_marks.clear(); }

    // Returns how many marks stand.
    std::size_t size() const { return m_marks.size(); }

    // 0.6.13: the reach records the deaths it takes (set around each); the deaths recorded
    // meanwhile are its, not the game's pulse's.
    void set_reaching(bool reaching) { m_reaching = reaching; }
    bool reaching() const { return m_reaching; }

private:
    struct Mark {
        std::uint32_t tick = 0;
        std::uint32_t ticks = 0;  // How long it stands.
        int item = kZodiacItem;
        int expect = -1;  // The last-weapon byte as a drain marked it, -1 any.
    };

    // Returns whether `mark` stands at `tick` (a mark up to kZodiacCreditSkew ticks ahead too).
    static bool stands(const Mark& mark, std::uint32_t tick) {
        return tick + kZodiacCreditSkew >= mark.tick && tick <= mark.tick + mark.ticks;
    }

    std::unordered_map<std::uint32_t, Mark> m_marks;  // Moby address to its mark.
    bool m_reaching = false;
};

// The death of `moby` at `tick` with the health record's last-weapon byte `weapon`: sets it to the
// marked item (the Zodiac's, or the weapon whose posted hit took it), else leaves it. Returns
// whether it did.
bool credit_zodiac(ZodiacCredit& credit, std::uint32_t moby, std::uint32_t tick, std::uint8_t& weapon);

}  // namespace ng_plus_plus
