// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.5.0's rules past damage, ammo and colour (README.md, "What each tier does"): the weapons'
// perks at Gamma, Epsilon and Omega, the Lava Gun's Ice Gun, the armour tiers, the elite enemies,
// the challenge modes' modifiers, overdrive and the hero's health from Omega weapons. Each perk is
// cumulative and scales with the tier: subtle at Gamma, stronger at Epsilon, its signature at
// Omega.
//
// Nothing here reads or writes game memory, so tests/perks_test.cpp runs it alone; perks.h,
// armour.h and elites.h carry it out in the game.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "tiers.h"

namespace ng_plus_plus {

// ---- Tiers and stages ----

// Returns the tier a weapon of `kind` wears at `level`: 0 for none, 1 Alpha to 8 Omega, 9 the RYNO
// II's Ultima (its first step, Heavy, is no tier).
int tier_of(Kind kind, int level);

// A perk's stage: none, Gamma's (Gamma and Delta), Epsilon's (Epsilon to Eta), Omega's.
enum class Stage : std::uint8_t {
    None = 0,
    Gamma = 1,
    Epsilon = 2,
    Omega = 3
};

// Returns the stage of tier `tier` (tier_of): from Gamma (3), Epsilon (5) and Omega (8; the RYNO
// II's Ultima too).
Stage stage_of_tier(int tier);

// Returns the stage of a weapon of `kind` at `level`.
inline Stage stage_of(Kind kind, int level) {
    return stage_of_tier(tier_of(kind, level));
}

// Returns `stage` one step up (overdrive), Omega at most.
Stage raised(Stage stage);

// ---- Hit perks ----

// A status a perk puts an enemy under (OpenRAC 1.29's moby_status; the same numbers).
enum class Status : std::uint8_t {
    None = 0,
    Freeze = 1,
    SlowHalf = 2,
    SlowThird = 3
};

// What one hit of a weapon does besides its own damage: extra hits the mod posts to other enemies,
// each a share of the hit's damage as it landed (after the tier and the enemies' curve). Every
// field 0 is nothing.
struct HitPerk {
    // A chain: from the victim to the nearest enemy not hit yet within `hop_radius`, then from
    // there, `hops` times, each hop `hop_share` of the hit (the share falls by `hop_decay` a hop).
    int hops = 0;
    float hop_radius = 0.0f;
    float hop_share = 0.0f;
    float hop_decay = 1.0f;

    // The chain's last jump explodes: every enemy within `end_radius` of it, `end_share` each.
    float end_radius = 0.0f;
    float end_share = 0.0f;

    // A fan: the `fan` nearest enemies within `fan_radius` of the victim, `fan_share` each, and
    // from each of them `fan_again` more (a split rocket splitting again), at most kFanMost in all.
    int fan = 0;
    int fan_again = 0;
    float fan_radius = 0.0f;
    float fan_share = 0.0f;

    // A burst around the victim: every other enemy within `burst_radius`, `burst_share` each.
    float burst_radius = 0.0f;
    float burst_share = 0.0f;

    // Through the victim: up to `pierce` enemies behind it (away from the hero, within kPierceCone
    // of the line from the hero through it) within `pierce_range`, `pierce_share` each.
    int pierce = 0;
    float pierce_range = 0.0f;
    float pierce_share = 0.0f;

    // A field left at the victim's place for `field_ticks`: every kFieldPulseTicks each enemy
    // within `field_radius` takes `field_share`.
    int field_ticks = 0;
    float field_radius = 0.0f;
    float field_share = 0.0f;

    // Every `nth` hit of the weapon (counted by the mod) is a strong one: its chain has `nth_hops`
    // hops at `nth_share` instead.
    int nth = 0;
    int nth_hops = 0;
    float nth_share = 0.0f;
    bool burst_on_nth = false;  // The burst only on the strong hits (an explosive pulse).

    // The status the enemies the chain and the burst reach are put under, and for how long
    // (OpenRAC 1.29; none before): the Plasma Coil's slow, the Tesla Claw's stun.
    Status status = Status::None;
    int status_ticks = 0;

    // Ticks after a hit before the weapon's next hit can set off a perk: the streams (the Tesla
    // Claw's beam, the Lava Gun) hit every tick and would chain every tick.
    int cooldown = 0;

    // Returns whether the perk does anything.
    bool any() const {
        return hops > 0 || fan > 0 || burst_radius > 0.0f || pierce > 0 || field_ticks > 0
               || nth > 0;
    }
};

// The most enemies one fan reaches, its second fans included (the RYNO II's split rockets, at most
// nine as in the survey).
inline constexpr int kFanMost = 9;

// The half-angle of a pierce's cone, as its cosine (about 25 degrees).
inline constexpr float kPierceCone = 0.9f;

// Ticks between two pulses of a field.
inline constexpr int kFieldPulseTicks = 15;

// Returns the perk of a hit of item `item` at `stage` (none for a weapon whose perks at that stage
// need the fire, spawn or status hook points: README.md, "Needs a newer OpenRAC").
HitPerk hit_perk(int item, Stage stage);

// Returns the perk of a hit of the RYNO II at `tier` (tier_of: 9 Ultima): the survey's split
// rockets as hits on the nearest enemies (2 at Gamma, 3 at Epsilon, re-split at Omega, 3 and
// splitting again at Ultima), the real rockets once OpenRAC spawns them (H1).
HitPerk ryno_perk(int tier);

// ---- Shots (OpenRAC 1.29's weapon calls) ----

// What a weapon's shots do at a stage: more of each shot, fired again by the game's own spawn
// (weapon_refire), and what a shot does as it ends (on_shot_end, moby_spawn_live).
struct ShotPerk {
    int refires = 0;          // Copies of each shot, fanned out.
    float spread = 0.0f;      // Radians between two copies.
    int split_after = 0;      // A rocket splits in two this many ticks into its flight (0 never).
    int end_shots = 0;        // Shots of `end_class` made where a shot ends...
    int end_chance = 100;     // ...by a shot the hero fired, this often in 100 (a roll per shot,
                              // split_roll; the shots made from it always)...
    int end_again_shots = 0;  // ...and by a shot made so, this many (0: end_shots)...
    int end_class = 0;        // ...of this class (1498 rockets, 3799 RYNO rockets, 3329 seekers,
                              // 121 mini bombs, 3300 a gravity bomb)...
    int end_generations = 0;  // ...and the ones made so split again, this many times.
    int end_most = 0;         // At most this many made from one shot in all.
    bool end_homing = false;  // They fly at the nearest enemies (else they scatter).
    int end_blasts = 0;       // Small blasts where it ends (the game's rocket blast).
    int end_again_ticks = 0;  // A second shot of the shot's own class where it ended, this late.
};

// The classes moby_spawn_live makes (OpenRAC 1.29).
inline constexpr int kMiniRocket = 1498;
inline constexpr int kRynoRocket = 3799;
inline constexpr int kSeeker = 3329;
inline constexpr int kMiniBomb = 121;
inline constexpr int kGravityBomb = 3300;

// Returns what item `item`'s shots do at `stage` (`tier` the RYNO II's, 9 Ultima): the
// Minirocket Tube, Seeker Gun, Hoverbomb Gun, Bomb Glove, Gravity Bomb, Bouncer, Decoy Glove,
// Miniturret Glove and the RYNO II; nothing for the others (no shot moby of their own).
ShotPerk shot_perk(int item, Stage stage, int tier);

// Returns whether the shot with serial `serial` (the port's) makes its end shots at `chance` in 100:
// a fixed roll of the serial, so it never changes for one shot; 100 or more always, 0 or less never.
bool split_roll(std::uint32_t serial, int chance);

// Returns the yaw of copy `n` (from 0) of a shot fanned by `spread`: right, left, further right...
float refire_yaw(int n, float spread);

// Returns the shots a second the Pulse Rifle's record takes at `stage`, times its own (weapon
// stat: OpenRAC 1.29's weapon_record +0x54).
float fire_rate(int item, Stage stage);

// ---- The Ice Gun (the Lava Gun's tiers) ----

// The Lava Gun's item.
inline constexpr int kLavaGun = 29;

// The Ice Gun's name, in place of the game's Lava Gun's name in every tier's name ("Alpha Ice
// Gun"; rac3's Lava Gun V5 is its Liquid Nitrogen Gun).
inline constexpr const char* kIceGunName = "Ice Gun";

// What the Ice Gun does to an enemy it hits.
struct IceEffect {
    int chill_ticks = 0;          // Slowed this long after each hit.
    int chill_every = 1;          // While slowed, its update runs one tick in this many.
    int freeze_ticks = 0;         // Frozen this long after each hit (inside the chill).
    float shatter_radius = 0.0f;  // A frozen or chilled enemy killed shatters: enemies within it...
    float shatter_share = 0.0f;   // ...take this share of the killing hit.
    bool shatter_freezes = false;  // The shards freeze what they hit.
    int energy = 0;                // The icy glow's strength (OpenRAC's energy level).
};

// Returns the Ice Gun's effect at tier `tier` (1 Alpha to 8 Omega): from Alpha a light chill and
// the icy glow, a deeper chill at Gamma, a freeze from Epsilon that shatters on the kill, a long
// freeze at Omega whose shards freeze their neighbours.
IceEffect ice_effect(int tier);

// The Ice Gun's colours by tier, 0x00BBGGRR as tint_rgb's: frost, glacier, ice, arctic, deep ice,
// polar, cobalt, and an aurora cycling at Omega (in place of the tiers' lime to rainbow).
std::optional<std::uint32_t> ice_tint_rgb(int tier, std::uint32_t tick);

// ---- The Lava Gun's path: the Ice Gun or the Meteor Gun's fire (0.5.1) ----

// What the Lava Gun's tiers become: the Ice Gun (0.5.0's, the default) or the Meteor Gun's own
// fire path (the "lava_gun" setting).
enum class LavaPath : std::uint8_t {
    Ice = 0,
    Fire = 1
};

// Sets the path for the whole mod (at load, from the setting), and returns it.
void set_lava_path(LavaPath path);
LavaPath lava_path();

// Returns whether the Lava Gun's tiers are the Ice Gun now.
inline bool ice_gun_on() {
    return lava_path() == LavaPath::Ice;
}

// What a fire-path meteor does to an enemy it hits.
struct FireEffect {
    int burn_ticks = 0;          // It burns this long after each hit...
    float burn_share = 0.0f;     // ...taking this share of the hit every kBurnPulseTicks.
    float splash_radius = 0.0f;  // Enemies within this of the impact...
    float splash_share = 0.0f;   // ...take this share of the hit, and catch fire.
    float erupt_radius = 0.0f;   // A burning enemy killed erupts: enemies within this...
    float erupt_share = 0.0f;    // ...take this share of its last hit, and catch fire.
    int storm_every = 0;         // Every this many meteors calls down a meteor storm...
    int storm_meteors = 0;       // ...of this many blasts around the victim...
    float storm_radius = 0.0f;   // ...each hitting the enemies within this...
    float storm_share = 0.0f;    // ...for this share of the hit.
    int energy = 0;              // The fiery glow's strength (OpenRAC's energy level).
};

// Ticks between two pulses of a burn.
inline constexpr int kBurnPulseTicks = 15;

// Returns the fire path's effect at tier `tier` (1 Alpha to 8 Omega): from Alpha a burn, a splash
// of magma from Gamma, burning enemies that erupt as they die from Epsilon, and a meteor storm at
// Omega.
FireEffect fire_effect(int tier);

// The fire path's colours by tier, 0x00BBGGRR as tint_rgb's: ember, flame, magma, blaze, inferno,
// scorch, solar, and a supernova cycling at Omega.
std::optional<std::uint32_t> fire_tint_rgb(int tier, std::uint32_t tick);

// Returns the base name of the Lava Gun's tier `tier` on the fire path ("Meteor Gun" at Alpha and
// Beta, "Magma Gun" at Gamma and Delta, "Inferno Gun" at Epsilon to Eta, "Supernova Gun" at
// Omega); the Ice Gun's on the ice path.
const char* lava_name(int tier);

// The glow a burning enemy takes, 0xRRGGBB.
inline constexpr std::uint32_t kBurningRgb = 0xFF6018u;

// Returns item `item`'s icon tint at `level` (0x00BBGGRR): the Ice Gun's for the Lava Gun, else
// tiers.h's tint_rgb.
std::optional<std::uint32_t> weapon_tint_rgb(int item, int level, std::uint32_t tick);

// Returns the colour item `item`'s effects take at `level` (0xRRGGBB), as tiers.h's effect_rgb,
// the Ice Gun's for the Lava Gun.
std::optional<std::uint32_t> weapon_effect_rgb(int item, int level, std::uint32_t tick);

// Returns whether item `item`'s tint at `level` moves with the tick.
bool weapon_tint_moves(int item, int level);

// Returns the most levels the book keeps for item byte `item`: the armour's tiers for kArmourItem,
// else the weapon's steps.
int max_level(int item);

// The icy blue a frozen enemy glows in, 0xRRGGBB, and a chilled one's, paler.
inline constexpr std::uint32_t kFrozenRgb = 0x60C8FFu;
inline constexpr std::uint32_t kChilledRgb = 0xB0E8FFu;

// ---- Armour tiers ----

// The tiers of armour past the game's best (Carbonox, armour 4), sold at the armour vendor after
// Carbonox (armour_vendor.h): each is Carbonox repainted in its colour, its rim glowing brighter
// tier by tier. The game's own armour stays 4: its tables of five (names, packs, descriptions) end
// there, and its damage table reads it.
inline constexpr int kArmourTiers = 8;

// The game's best armour, Carbonox: the s16 armour level the hero's damage reads (g_player_stats
// +0x18, the save block's +0x20; AdjustPlayerHealth clamps it to 0..4), 0 none, 1 Tetrafiber,
// 2 Duraplate, 3 Electrosteel, 4 Carbonox (the armour shop lists the levels above it, 1 to 4).
inline constexpr int kBestArmour = 4;

// The game's mode in normal play (g_current_game_mode): a cutscene or scene is another value.
inline constexpr std::int32_t kPlayGameMode = 0;

// Returns whether the tier's paint is on the hero in the world: normal play (`mode`, not a
// cutscene), Carbonox worn (`worn_armour`, the save's armour) and a tier owned.
constexpr bool armour_paints_world(std::int32_t mode, int worn_armour, int tier) {
    return mode == kPlayGameMode && worn_armour >= kBestArmour && tier > 0;
}

// Returns whether a menu's picture of the hero is painted with the tier it shows: Carbonox worn
// and a tier shown (the vendor's preview is a menu, not play).
constexpr bool armour_paints_menu(int worn_armour, int shown_tier) {
    return worn_armour >= kBestArmour && shown_tier > 0;
}

// The item byte NG++'s book keeps the armour tier in: no item of the game's (none of its rows is
// item 55), so the weapons' rules never see it.
inline constexpr int kArmourItem = 55;

// The armour's price ladder's base: about what the game's best armour costs in challenge mode.
inline constexpr std::int32_t kArmourBase = 600'000;

// Returns the damage the hero takes at armour tier `tier` (1 Alpha to 8 Omega) on top of the
// game's armour: x0.9 at Alpha down to x0.45 at Omega; 1 at 0.
float armour_damage(int tier);

// Returns the price of armour tier `tier` (1 to 8): kArmourBase up the same ladder as a weapon
// whose every tier is bought (tiers.h's price for Kind::NoExp), each above the one before.
std::int32_t armour_price(int tier);

// Returns the name of armour tier `tier`: "Alpha Armor" ... "Omega Armor".
std::string armour_name(int tier);

// Returns the armour's glow at tier `tier`, 0xRRGGBB: steel, azure, jade, gold, amethyst, rose,
// ember and a prismatic cycle at Omega; none at 0.
std::optional<std::uint32_t> armour_rgb(int tier, std::uint32_t tick);

// Returns the armour's icon tint in the vendor's list, 0x00BBGGRR (armour_rgb's channels swapped).
std::optional<std::uint32_t> armour_tint(int tier, std::uint32_t tick);

// Returns the armour glow's strength (OpenRAC's energy level) at tier `tier`: a faint rim at Alpha,
// kEffectTop at Omega.
int armour_energy(int tier);

// Returns the armour paint's rim glow (OpenRAC's paint level, 0 to 15) at tier `tier`: none at
// Alpha, rising each tier to 15 at Omega.
int armour_paint_level(int tier);

// Returns the armour vendor's description of tier `tier`: what it takes off a hit and its perks,
// short enough for the port's text slot.
std::string armour_about(int tier);

// The armour's perks at a tier.
struct ArmourPerk {
    float thorns_radius = 0.0f;  // A hit on the hero shocks the enemies within it...
    float thorns_share = 0.0f;   // ...for this share of his last weapon hit's damage.
    int last_stand_ticks = 0;  // A hit that would end him leaves 1 point, once in this many ticks.
};

// Returns the armour's perks at tier `tier`: thorns from Gamma, a last stand from Epsilon (once a
// minute, Omega's every half minute), all stronger at Omega.
ArmourPerk armour_perk(int tier);

// ---- Elite enemies, modifiers and the rest ----

// The challenge mode's modifier of a run, rotating from cycle 2 (the second challenge mode).
enum class Modifier : std::uint8_t {
    None,
    Swarm,     // One more copy of each enemy.
    Elite,     // Twice as many elites.
    Volatile,  // Every enemy bursts when it dies, hurting the enemies around it.
    Bounty,    // Enemies hit a fifth harder and drop a quarter more.
};

// Returns cycle `cycle`'s modifier: none up to cycle 1, then Swarm, Elite guard, Volatile and
// Bounty in turn.
Modifier modifier_of(int cycle);

// Returns the modifier's name for the log and the notice ("Challenge: Swarm").
const char* modifier_name(Modifier modifier);

// The extra copies the Swarm modifier adds to each enemy.
inline constexpr int kSwarmCopies = 1;

// Volatile's burst: radius and its share of the reference damage (perks.h: the hero's last
// weapon hit).
inline constexpr float kVolatileRadius = 5.0f;
inline constexpr float kVolatileShare = 0.6f;

// Bounty's enemy damage and drops.
inline constexpr float kBountyDamage = 1.2f;
inline constexpr float kBountyDrops = 1.25f;

// The share of enemy copies made elite in cycle `cycle` under `modifier`, out of 100: none up to
// cycle 2, 15 at cycle 3, 5 more a run up to 35; twice that under the Elite guard (at most 70).
int elite_percent(int cycle, Modifier modifier);

// Returns whether the copy `index` of the enemy in `slot` of level `level` is an elite at
// `percent`: a fixed roll of the three, so a level loaded again makes the same elites.
bool is_elite(int level, int slot, int index, int percent);

// An elite's health, bolts and weapon experience, times an ordinary enemy's of its class.
inline constexpr float kEliteHealth = 2.5f;
inline constexpr float kEliteDrops = 3.0f;
inline constexpr float kEliteExperience = 2.0f;

// The arenas (Maktar's and Joba's levels): every kArenaEliteEvery-th enemy their waves spawn from
// cycle 2 is an elite.
inline constexpr int kArenaLevels[] = {2, 11};
inline constexpr int kArenaEliteEvery = 4;

// Returns whether level `level` holds an arena.
bool is_arena_level(int level);

// Overdrive: kills in a row with one tiered weapon (from Gamma) fill it; at kOverdriveKills its
// perks act a stage higher for kOverdriveTicks.
inline constexpr int kOverdriveKills = 20;
inline constexpr int kOverdriveTicks = 600;

// Returns the hero's extra health steps for `omega` weapons at Omega: one for every two, at most
// kOmegaHealthMost.
inline constexpr int kOmegaHealthMost = 12;
int omega_health(int omega);

}  // namespace ng_plus_plus
