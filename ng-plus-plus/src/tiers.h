// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++'s rules, apart from the game (README.md): the steps every weapon gets past its last real
// version, what each costs or needs and what it gives, and the enemies' curve across new games.
//
// Each weapon keeps its last real version in the game; the mod adds steps on top of it, counted by
// its own level per weapon (0: none yet, 1: the first step taken). A step is bought at the vendor or
// reached by experience (the game's own, which keeps adding up at a weapon's last version: the
// item's s32 >> 5, as the game compares it). Buy, then level: the tiers alternate, as the game's
// Mega purchase is followed by its V4 by experience.
//
// Nothing here reads or writes game memory, so tests/tiers_test.cpp runs it alone.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ng_plus_plus {

// The items of Going Commando: a byte per item number in the game's tables (seen in game code:
// g_itemEquippedSlot).
inline constexpr int kItems = 56;

// The most steps a weapon has: the RYNO II's two of its own and the eight tiers.
inline constexpr int kMaxSteps = 10;

// The eight tiers every weapon gets, V5 to V12.
inline constexpr int kTiers = 8;

// The experience (>> 5) a weapon needs at a bought version before the next purchase opens, where
// the game has no experience step of its own there (the two-version weapons' Mega, the Zodiac, the
// RYNO II Ultima, and each bought tier).
inline constexpr std::int32_t kMasteryExp = 6000;

// The RYNO II's experience goals (its steps by experience and its mastery) are this many times the
// others': it levels slower.
inline constexpr std::int32_t kRynoExpScale = 5;

// With no Mega price, the bought steps go from this; a Mega below the floor counts as the floor.
inline constexpr std::int32_t kPriceLadder = 1'500'000;
inline constexpr std::int32_t kPriceFloor = 150'000;

// How a weapon reached its last real version, which decides how its first step is unlocked.
enum class Kind : std::uint8_t {
    None,    // not a weapon the mod knows (gadgets, the wrench)
    Four,    // four versions, V4 by experience: Alpha is for sale at V4
    Two,     // two versions, the second bought in challenge mode (Bomb Glove, ...)
    NoExp,   // as Two, but it deals no damage, so never earns experience: every tier is bought
    Zapper,  // the Clank Zapper: V2 by experience
    Sheep,   // the Sheepinator: V4 by experience, no Mega: Alpha is for sale at V4
    Single,  // one version, bought (the Zodiac)
    Ryno,    // the RYNO II: Heavy by experience and Ultima bought first
};

// How a step is reached.
enum class How : std::uint8_t {
    Buy,  // bought at the vendor
    Exp,  // reached by experience
};

// The tints a step can have.
enum class Tint : std::uint8_t {
    Lime,
    Cyan,
    Emerald,
    Teal,
    Violet,
    Magenta,
    Crimson,
    Prismatic,  // cycles through the seven above
    Orange,     // RYNO II Heavy
    Ultima,     // RYNO II Ultima: black and red
};

// One step: its name, put before the weapon's ("Alpha"), how it is reached, what it needs and what
// it gives (the damage and the most ammo of the weapon's last real version, times).
struct Step {
    const char* name;  // The tier's own name, in every language.
    How how;           // Bought or reached by experience.
    std::int32_t exp;  // Experience (>> 5) since the last step, for a step by experience.
    float damage;      // Times the last real version's damage.
    float ammo;        // Times the last real version's most ammo.
    Tint tint;         // The colour of the weapon's icon and name.
};

// Returns the kind of item `item`, Kind::None for one the mod leaves alone.
Kind kind_of(int item);

// Returns how many steps a weapon of `kind` has (0 for none).
int step_count(Kind kind);

// Returns the step at `level` (0 the first) for a weapon of `kind`, or null past its last.
const Step* step_at(Kind kind, int level);

// Returns the version whose row has the weapon's Mega price (+0x80 of its row: the vendor's own
// Mega upgrade), counted from its first: 2 for the four-version weapons, 1 for the two-version
// ones, 0 for the Zodiac and the RYNO II (their one version); -1 for none (the Zapper, the
// Sheepinator).
int mega_version(Kind kind);

// Returns the version whose name the tiers take ("Beta Meteor Gun"): the last one the weapon
// reaches before the game's own bought top version, counted from its first. 1 for the four-version
// weapons (the experience version before the Mega: Heavy Lancer, Meteor Gun), 0 for the two-version
// ones, the Zodiac and the RYNO II; 1 for the Clank Zapper (its last, Clank Shocker) and the
// Sheepinator (Black Sheepinator, before the versions challenge mode gives it); -1 for none.
int name_version(Kind kind);

// Returns the price of the step at `level` when it is bought (0 for one by experience), from the
// weapon's Mega price `mega` (0 for none): the base (`mega`, at least kPriceFloor; kPriceLadder with
// none) times 1.5, 2.5, 4, 6 (and 9, the RYNO II's fifth), rounded, each strictly above the one
// before and above `mega`. A Kind::NoExp weapon buys all eight: the same ladder at Alpha, Gamma,
// Epsilon and Eta, with Beta, Delta and Zeta between (2, 3.25, 5) and Omega at 9.
std::int32_t price(Kind kind, int level, std::int32_t mega);

// Returns whether the step at `level` is for sale now: the weapon at its last real version
// (`at_last`), the step bought, and the version before it mastered (reached by experience, or `exp`
// at least kMasteryExp after a purchase).
bool can_buy(Kind kind, int level, bool at_last, std::int32_t exp);

// Returns whether the step at `level` is reached by `exp` now (the weapon at its last version).
bool exp_reached(Kind kind, int level, bool at_last, std::int32_t exp);

// Returns the experience (>> 5) the weapon's bar fills toward at `level` (its last real version):
// the step's own for one by experience, kMasteryExp for a bought one after a bought one, 0 for none
// (the next is for sale at once, or no step is left: the game's full bar).
std::int32_t exp_goal(Kind kind, int level);

// Returns the damage at `level`, times the last real version's (level 0: the game's own, 1.0).
float damage(Kind kind, int level);

// Returns the most ammo at `level`, times the last real version's (level 0: 1.0).
float ammo(Kind kind, int level);

// Returns the most ammo at `level` from the last real version's `base` (0 stays 0: endless), at
// most what the game's u16 holds.
std::int32_t ammo_max(Kind kind, int level, std::int32_t base);

// Returns the tint at `level`, 0x00BBGGRR, at game tick `tick` (the animated ones move with it);
// none at level 0.
std::optional<std::uint32_t> tint_rgb(Kind kind, int level, std::uint32_t tick);

// The strongest a weapon's effects are drawn (OpenRAC's energy level, 0 to 15): at its last step.
inline constexpr int kEffectTop = 15;

// Returns the colour a weapon's shots, blasts and other effects take at `level`, 0xRRGGBB (red
// highest, as the port's materials take it), at full strength: the tier's tint with its brightest
// channel raised to 0xFF (Omega's rainbow cycling with the tick as its icon's), the RYNO II
// Ultima's red beating from bright to deep instead of to black, which effects drawn by adding
// light would lose; none at level 0.
std::optional<std::uint32_t> effect_rgb(Kind kind, int level, std::uint32_t tick);

// Returns how strong a weapon's effects are drawn at `level`: 0 for none, rising with each step to
// kEffectTop at the last (Omega; the RYNO II's too).
int effect_strength(Kind kind, int level);

// Returns whether the tint at `level` moves with the tick (Omega's rainbow, Ultima's beat).
bool tint_moves(Kind kind, int level);

// Returns "<step> <base>" ("Beta Meteor Gun", `base` the name of the version name_version gives), or
// `base` at level 0.
std::string display_name(std::string_view base, Kind kind, int level);

// Returns how much more health the enemies have in challenge mode cycle `cycle` (the save's
// counter: 0 the first game, 1 the first challenge mode, which the game balances itself): 1.0 up to
// cycle 1, past it a curve in step with the tiers a player has by then (README.md). `strength`
// scales what is added past 1.0 (1.0 the curve as it is, 0 none).
float enemy_health(int cycle, float strength);

// Returns how much more damage the enemies deal in cycle `cycle`, as enemy_health.
float enemy_damage(int cycle, float strength);

}  // namespace ng_plus_plus
