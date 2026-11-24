// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The homing rockets' rules (shots.cpp): which mobys a rocket may fly at, how far it turns in a
// tick, and when it gives a target up. Pure, so the tests can drive them.
//
// Why: the RYNO II's rocket update (homing_l00_shot_update_3) turns a rocket only by the lead its
// target's motion asks for, so a still target never turns it and a target that shuffles in place
// snaps it back and forth through the point it seeks. A target that cannot be hit there (an enemy
// still in the ground at its spawn, hidden, not yet out, dying) then holds the rocket hovering
// over that point until its timer runs out, and a rocket whose target goes flies off along the
// heading it was first given. So the mod steers each rocket it knows: at most `turn_limit` a tick
// towards a target that passes `targetable`, a turn that grows as the rocket closes so it can
// never circle; and it lets go (`judge`) of a target it cannot reach, the rocket flying straight on.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ng_plus_plus {

using Vec3 = std::array<float, 3>;

// What the mod reads of a moby to decide whether a shot may fly at it.
struct TargetFacts {
    std::uint8_t state = 0xFF;  // +0x20: 0x80 and over dead or free (0xFD, 0xFE going).
    bool enemy = false;         // Its class record says kind 5 (as the game's own pick).
    bool has_health = false;    // It has a health record (+0x34 bit 0x20, +0x68's first word).
    float health = 0.0f;        // Its health.
    std::uint16_t mode = 0;     // +0x34: 0x01 or 0x80 hidden (not drawn).
    std::uint8_t alpha = 0;     // +0x23: under 8 not drawn.
};

// Returns whether a shot may fly at a moby with `facts`: alive, an enemy, with health left and
// shown, as the game's own target pick (SelectBestTargetInCone) and drawn test (MobyProc) take one
// but on screen or not. A moby the game holds hidden at a spawn point, still underground or not yet
// out (Grelbin's Yetis), or one dying, is none.
bool targetable(const TargetFacts& facts);

// The most a rocket turns in a tick, radians: `base` far away, and enough near the point it seeks
// that its turning circle (speed over turn) stays under a third of the way left, so it can always
// reach the point instead of circling it.
float turn_limit(float speed, float distance, float base);

// Returns `forward` (a unit vector) turned towards `to` by at most `most` radians, a unit vector.
// `forward` itself when `to` is null; straight at `to` when within `most`.
Vec3 steer(const Vec3& forward, const Vec3& to, float most);

// Returns `v` at length 1 (`fallback` for a null one).
Vec3 unit(const Vec3& v, const Vec3& fallback);

// One rocket's chase of its target.
struct Chase {
    std::uint32_t target = 0;   // The moby it flies at, 0 none.
    int ticks = 0;              // Ticks at this target.
    int near_ticks = 0;         // Ticks within reach of it without a hit.
    int blocked = 0;            // Sight checks in a row a wall stood between them.
    std::uint32_t dropped[4]{};  // Targets it gave up: it does not take them again.
    std::uint32_t struck = 0;    // The target it went off on unhit (strike): its splits take it.
};

// What a rocket does with its target this tick.
enum class Verdict {
    Chase,    // Steer at it.
    Strike,   // Within reach and not hit yet: it goes off there, on the target.
    Drop,     // Unreachable (behind a wall, too long): no target, straight on.
};

// The limits `judge` holds a chase to, in ticks of the disc (shots.cpp scales them).
struct ChaseLimits {
    float reach = 2.5f;     // Within this of the point it seeks it is at the target.
    int near_ticks = 3;     // That many ticks within reach without the game's hit: strike.
    int most_ticks = 150;   // A chase this long is dropped.
    int blocked = 2;        // Sight checks in a row blocked: dropped.
};

// Judges `chase` after this tick: `distance` from the point the rocket seeks, `blocked` whether
// this tick's sight check (if `checked`) found a wall in between. Updates the chase's counts.
Verdict judge(Chase& chase, float distance, bool checked, bool blocked, const ChaseLimits& limits);

// Records that `chase` gave its target up (it is not taken again) and clears it.
void drop(Chase& chase);

// Records that `chase` went off on its target (a strike): this rocket does not take it again, but
// `inherited` still names it for the splits the rocket makes where it ended.
void strike(Chase& chase);

// Returns the moby a rocket that ended with `parent`'s chase passes on to its splits: the target it
// was flying at, or the one it struck, 0 for none. A target the parent let go of (walled, out of
// reach, gone) is none. The caller still checks it is live and in sight (`targetable`, a wall).
std::uint32_t inherited(const Chase& parent);

// Returns the targets for `count` splits of one rocket: all `parent` if it is nonzero and `usable`,
// else each takes its own candidate from `own` (in the rules' order; 0, none, past its end).
std::vector<std::uint32_t> split_targets(
    std::uint32_t parent, bool usable, const std::vector<std::uint32_t>& own, int count
);

// Returns whether `chase` gave up the moby at `address`.
bool dropped(const Chase& chase, std::uint32_t address);

// Points `chase` at `address` (0 for none), its counts started again.
void retarget(Chase& chase, std::uint32_t address);

}  // namespace ng_plus_plus
