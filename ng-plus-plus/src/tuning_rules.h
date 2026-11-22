// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.5.1's weapon tuning rules (README.md, "Weapon tuning"): the Tesla Claw's steady hits, the
// Zodiac's boss damage and the decoy's health. The game's code behind each is in
// plans/ngpp/051-research.md of the swarm notes, summed up at each rule.
//
// Nothing here reads or writes game memory, so tests/perks_test.cpp runs it alone; tuning.h
// carries it out in the game.

#pragma once

#include "tiers.h"

namespace ng_plus_plus {

// ---- Tesla Claw (item 18, its gun class 177) ----
//
// The game's claw posts one hit on each lock when it takes it, then none until the lock ages out
// at 50 frames and is taken again (hero_tether_beam_update), while it spends ammo every 10 frames:
// about one hit a second, at an uneven moment. A repeat from the same attacker inside 10 of the
// victim's updates is zeroed (moby_update_aim_target_strength, func_002A96A0), so the mod's hits
// come from the attacker the victim did not see last.

// The Tesla Claw's item and its gun's class.
inline constexpr int kTeslaClaw = 18;
inline constexpr int kTeslaClass = 177;

// Ticks between two of the mod's hits on one lock.
inline constexpr int kTeslaHitTicks = 6;

// The share of the claw's own hit each steady hit deals: one a tenth of a second at 0.15 is about a
// quarter more damage a second than the game's one hit about every 51 frames.
inline constexpr float kTeslaShare = 0.15f;

// Returns the damage a second of the steady claw over the game's claw (one hit of 1 every
// `game_frames`), for the README's and the tests' numbers.
float tesla_gain(float game_frames);

// ---- Zodiac (item 43, its gun class 3784) ----
//
// The Zodiac's pulse (charging_weapon_update, func_L00_00408E58) posts no hit: each frame of the
// pulse it kills what has 100 health or less outright and takes a flat 20 off anything with more,
// so a boss loses 20 a frame whatever the tier, and nothing when the cone or its class record
// leaves it out.

// The Zodiac's item, its gun's class and the gun's pulse state (+0x20).
inline constexpr int kZodiac = 43;
inline constexpr int kZodiacClass = 3784;
inline constexpr int kZodiacPulse = 3;

// The game's flat drain a pulse frame, and the pulse's reach.
inline constexpr float kZodiacDrain = 20.0f;
inline constexpr float kZodiacRange = 40.0f;

// Returns how much more a boss of class `moby_class` takes from the Zodiac than the game's drain
// (1.0 for a class that is no boss).
float zodiac_vulnerability(int moby_class);

// Returns the health a boss of class `moby_class` loses each pulse frame from the Zodiac at
// `level`, in cycle `cycle` with the curve's `strength` (`scaling` the curve on): the game's drain
// times the boss's vulnerability and the tier's damage, divided by the boss's health on the curve,
// as every other weapon's hits on it are.
float zodiac_boss_drain(int moby_class, int level, int cycle, bool scaling, float strength);

// ---- Decoy Glove (item 17, the decoy's class 203) ----
//
// decoy_create_projectile gives a decoy 60 health with the upgraded glove, else 6 (at
// *(moby+0x68)+0x24); NG++ raises it as the decoy appears.

inline constexpr int kDecoyGlove = 17;
inline constexpr int kDecoyClass = 203;

// The decoy's health over the game's with the mod on, before its tier.
inline constexpr float kDecoyBase = 2.5f;

// Returns the decoy's health over the game's at the Decoy Glove's `level`: kDecoyBase times a fifth
// more than the tier's damage factor (x1.8 at Alpha to x8.4 at Omega).
float decoy_health(int level);

}  // namespace ng_plus_plus
