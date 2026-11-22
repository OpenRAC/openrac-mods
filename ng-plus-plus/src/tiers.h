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

// The most steps a weapon has: the RYNO II's two of its own, the eight tiers and its Omega Prime.
inline constexpr int kMaxSteps = 11;

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
    Prime,      // RYNO II Omega Prime: white and gold, beating
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
// most 999 (the HUD's three digits), or the game's own count where it is more.
std::int32_t ammo_max(Kind kind, int level, std::int32_t base);

// Returns the tint at `level`, 0x00BBGGRR, at game tick `tick` (the animated ones move with it);
// none at level 0.
std::optional<std::uint32_t> tint_rgb(Kind kind, int level, std::uint32_t tick);

// The strongest a weapon's effects are drawn (OpenRAC's energy level, 0 to 15): at its last step.
inline constexpr int kEffectTop = 15;

// How strong they are at the first step: Alpha's glow has to show (at 2 of 15 the Alpha Heavy
// Lancer's shots looked like the game's own).
inline constexpr int kEffectFirst = 6;

// Returns the colour a weapon's shots, blasts and other effects take at `level`, 0xRRGGBB (red
// highest, as the port's materials take it), at full strength: the tier's tint with its brightest
// channel raised to 0xFF (Omega's rainbow cycling with the tick as its icon's), the RYNO II
// Ultima's red beating from bright to deep instead of to black, which effects drawn by adding
// light would lose; none at level 0.
std::optional<std::uint32_t> effect_rgb(Kind kind, int level, std::uint32_t tick);

// Returns how strong a weapon's effects are drawn at `level`: 0 for none, kEffectFirst at the first
// step, rising with each step to kEffectTop at the last (Omega; the RYNO II's Omega Prime).
int effect_strength(Kind kind, int level);

// Returns the colour a weapon's effects take from its icon's tint `bgr` (0x00BBGGRR), 0xRRGGBB:
// its brightest channel raised to 0xFF and the others with it.
std::uint32_t effect_of_tint(std::uint32_t bgr);

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

// One boss of Going Commando: its moby class and what it is (seen in game code: each class's update
// in the levels' moby dispatch tables, docs/game/moby-dispatch.tsv of rac2-decomp).
struct BossClass {
    int moby_class;     // The class.
    bool last;          // The final boss.
    const char* which;  // The fight.
};

// The bosses whose health grows past the curve's (boss_health): the fights' own mobys, not their
// helpers, turrets or the enemies they call in.
inline constexpr BossClass kBossClasses[] = {
    {2644, false, "Oozla: the Swamp Monster II"},
    {2701, false, "Oozla: the Swamp Monster's arms"},
    {2702, false, "Oozla: the Swamp Monster's eye"},
    {2552, false, "Maktar arena: Chainblade"},
    {2628, false, "Maktar arena: the B2 Brawler"},
    {3210, false, "Endako: the boss of the Megacorp building"},
    {3679, false, "Siberius: the boss"},
    {3973, false, "Hrugis Cloud: the boss"},
    {2874, false, "Joba arena: a boss"},
    {3402, false, "Joba arena: a boss"},
    {4785, false, "Todano: the flying boss"},
    {3961, false, "Snivelak: the boss"},
    {3145, false, "the Thugs-4-Less leader"},
    {3806, false, "Damosel: the leader's mech"},
    {3967, false, "Damosel orbit: the mothership's parts"},
    {4623, false, "Damosel orbit: the mothership's parts"},
    {4576, true, "Yeedil: the mutated Protopet, the final boss"},
};

// What a hit lands on, for hit_divisor.
struct Foe {
    bool boss = false;        // One of kBossClasses.
    bool last = false;        // The final boss.
    std::int32_t bolts = 0;   // Its own bolts (+0xB4), 0 for none: how tough an enemy is.
};

// The final boss's health on top of a boss's.
inline constexpr float kFinalBoss = 1.5f;

// An enemy with at least these bolts of its own (the top quarter of Going Commando's enemies) is a
// tough one for the RYNO II.
inline constexpr std::int32_t kRynoToughBolts = 110;

// Returns how much more health a boss has on top of enemy_health in cycle `cycle`: none up to
// cycle 1, then x2, growing a quarter a run up to x3. `strength` scales it as enemy_health's.
float boss_health(int cycle, float strength);

// Returns the boss record of moby class `moby_class`, or null for one that is no boss.
const BossClass* boss_of(int moby_class);

// Returns whether moby class `moby_class` is one of kBossClasses.
inline bool is_boss(int moby_class) {
    return boss_of(moby_class) != nullptr;
}

// Returns how many times the game's own RYNO II's hits (its first version, in the first game) the
// RYNO II at any level needs on `foe` in cycle `cycle`, from cycle 2 on: 0 for an ordinary enemy
// (its hits as strong as its level makes them); for a tough one (kRynoToughBolts or more) x2 at
// cycle 2, half more a run, up to x4, times the square root of its bolts over kRynoToughBolts (at
// most x3); for a boss x3 at cycle 2, one more a run, up to x7, the final boss kFinalBoss times
// that. Scaled by `strength` as the curve.
float ryno_hits(int cycle, float strength, const Foe& foe);

// Returns what a hit's damage is divided by in cycle `cycle` from a weapon of `kind` at tier
// `level` on `foe`: the enemies' health, times a boss's extra (the final boss's kFinalBoss more).
// The RYNO II is left out of the curve and stays the strongest weapon: an ordinary enemy takes its
// hits as the game made them, or stronger with its tier; a tough enemy or a boss needs ryno_hits
// times the game's own RYNO II's hits, whatever its tier.
float hit_divisor(Kind kind, int level, int cycle, float strength, const Foe& foe);

// The final boss's and a boss's weapon experience for a kill, times the run's (kill_experience).
inline constexpr float kBossExperience = 5.0f;
inline constexpr float kFinalBossExperience = 10.0f;

// Returns how much more weapon experience a kill of `foe` gives in cycle `cycle`, so a tier takes
// about as long to level as in the first challenge mode although enemies take more hits: 1.0 up to
// cycle 1, then the run's x1.5, x2.5, x3.5 and x4.5 (from cycle 5 on), times a boss's
// kBossExperience (the final boss's kFinalBossExperience) or else the enemy's toughness softened:
// the fourth root of its bolts over 20, from x1 up to x2.45 (the square root of the drops' x6).
float kill_experience(int cycle, const Foe& foe);

}  // namespace ng_plus_plus
