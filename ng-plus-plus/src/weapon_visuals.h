// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The tiers' look on the weapons' own mobys (README.md, "How each weapon looks"): the gun in hand,
// its shots, bombs, blasts, bots and turrets, and the Shield Charger's shield. Each tick, every live
// moby of a weapon's classes (kVisualClasses) whose weapon wears a tier is drawn with OpenRAC's
// energy material (interface 1.25) in the tier's colour (tiers.h's effect_rgb, Omega's rainbow and
// Ultima's beat moving with the tick) at the tier's strength (effect_strength): the moby's body
// keeps its colours with a little of the tier's and a glowing rim, and the effects it makes (its
// glow, the sparks, arcs, trails and flashes tied to it) are drawn in the tier's colour, brighter
// and larger with each tier. A port of 1.23 or 1.24 has no energy: there the bodies of the shots,
// bombs and bots (VisualClass::body) are tinted in the tier's colour instead, and nothing else
// changes. A port older than 1.23 draws everything as the game does.
//
// Render-only: no game memory is written, so the shots fly and hit as they did.

#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "game_view.h"
#include "openrac/mod.hpp"
#include "weapons.h"

namespace ng_plus_plus {

// moby_material's energy (OPENRAC_MOBY_MATERIAL_ENERGY of interface 1.25) and its level's place in
// the material (OPENRAC_MOBY_MATERIAL_ENERGY_AT), and the interface minor that draws it.
inline constexpr std::uint32_t kMaterialTint = 1;
inline constexpr std::uint32_t kMaterialEnergy = 4;
inline constexpr int kEnergyLevelShift = 8;
inline constexpr std::uint16_t kEnergyMinor = 25;

// Returns moby_material's energy at strength `level` (0 to kEffectTop).
constexpr std::uint32_t energy_at(int level) {
    return kMaterialEnergy | (static_cast<std::uint32_t>(level) & 0xFu) << kEnergyLevelShift;
}

// A moby class of one weapon's: the gun, a shot, a blast, a bot. `body` marks a shot, bomb, bot or
// turret whose own model is worth tinting where the port has no energy; a gun, a blast or the
// shield, whose look is mostly the effects they make, is left as the game draws it there.
struct VisualClass {
    std::uint16_t moby_class;
    std::uint8_t item;
    bool body;
};

// The weapons' mobys, by class (moby +0xAA). Sources: rac2-decomp's docs/game/moby-dispatch.tsv
// (the classes' labels and update functions) and src/levels/shared (each update's item, as
// discs.h's kHitSources).
// clang-format off: one class per row.
inline constexpr VisualClass kVisualClasses[] = {
    {1490u, 30u, false},  // Lancer
    {3077u, 30u, false},  // Heavy Lancer, its Mega
    {1489u, 26u, false},  // Blitz Gun
    {2801u, 26u, false},  // Blitz Cannon, its Mega (UpdateSpreadWeapon)
    {1481u, 29u, false},  // Lava Gun
    {3161u, 29u, false},  // Meteor Gun, its Mega
    {4406u, 29u, true},   // Lava Gun's lingering blobs (lava_gun_update_blobs)
    {3146u, 28u, false},  // Plasma Coil
    {1482u, 28u, false},  // Plasma Storm, its Mega
    {1487u, 27u, false},  // Minirocket Tube
    {4100u, 27u, false},  // Megarocket Cannon, its Mega
    {1498u, 27u, true},   // Minirocket Tube's homing rocket (homing_l00_shot_update)
    {3799u, 44u, true},   // RYNO II's rocket (OpenRAC 1.29's weapon shots)
    {3300u, 42u, true},   // the gravity bomb in flight (the same)
    {121u,  12u, true},   // Bomb Glove's bomb, the mini bombs (the same)
    {3684u, 37u, true},   // Bouncer's bomblet (the same)
    {3299u, 42u, true},   // Gravity Bomb
    {2650u, 42u, true},   // Mini-Nuke, its Mega
    {3079u, 42u, true},   // Mini-Nuke's projectile
    {3070u, 42u, false},  // its blast (mini_nuke_blast_update)
    {3301u, 42u, false},  // its blast (blast_l00_update)
    {2169u, 22u, true},   // Chopper
    {2806u, 22u, true},   // Multi-Star, its Mega
    {2311u, 32u, false},  // Spiderbot Glove
    {3706u, 32u, false},  // Tankbot Glove, its Mega
    {2904u, 32u, true},   // the Spider Bot
    {3705u, 32u, true},   // the Tankbot (Turret Spider Bot)
    {3755u, 32u, false},  // its exploding trail (UpdateExplodingTrailProjectile)
    {3604u, 37u, false},  // Bouncer
    {3742u, 37u, false},  // Heavy Bouncer, its Mega
    {3790u, 37u, true},   // its bouncing blast (UpdateBouncingBlastProjectile)
    {3162u, 41u, false},  // Miniturret Glove
    {2661u, 41u, false},  // Megaturret Glove, its Mega
    {4966u, 41u, true},   // the Miniturret
    {4967u, 41u, true},   // the Megaturret
    {2651u, 25u, false},  // Hoverbomb Gun
    {3631u, 25u, false},  // Tetrabomb Gun, its Mega
    {1496u, 25u, true},   // the hoverbomb
    {2652u, 45u, false},  // Shield Charger's shield (tesla_barrier_update)
    {4498u, 45u, false},  // Tesla Barrier, its Mega, the same update
    {3784u, 43u, false},  // Zodiac
    {2663u, 44u, false},  // RYNO II
    {1486u, 24u, false},  // Seeker Gun
    {1497u, 24u, true},   // its seeker (seeker_apply_projectile_hit)
    {3329u, 24u, true},   // its Mega's seeker
    {1483u, 23u, false},  // Pulse Rifle
    {3076u, 23u, false},  // Vaporizer, its Mega
    {163u,  14u, false},  // Visibomb Gun
    {172u,  14u, true},   // its missile
    {192u,  12u, true},   // Bomb Glove's bomb
    {562u,  17u, false},  // Decoy Glove
    {203u,  17u, true},   // the decoy
    {3250u, 31u, true},   // Synthenoid
    {177u,  18u, false},  // Tesla Claw (hero_tether_beam_update)
    {180u,  53u, false},  // Walloper
    {2653u, 16u, false},  // Sheepinator
    {3757u, 16u, false},  // Black Sheepinator, its upgrade
    {3800u, 9u,  false},  // Clank Zapper
};
// clang-format on

// Returns the weapon of moby class `moby_class`, or none for a class no weapon owns.
const VisualClass* visual_class(int moby_class);

// What one moby is drawn with: moby_material's material and rgb (0xRRGGBB).
struct MobyLook {
    std::uint32_t material = 0;
    std::uint32_t rgb = 0xFFFFFFu;

    bool operator==(const MobyLook& other) const {
        return material == other.material && rgb == other.rgb;
    }

    bool operator!=(const MobyLook& other) const { return !(*this == other); }
};

// Returns the look of a moby of `visual` whose weapon is of `kind` at tier `level`, at game tick
// `tick`: energy in the tier's colour at its strength where the port has it (`energy`), else a
// tint in the tier's colour on a body (`visual.body`); none at level 0, or for a gun without
// energy.
std::optional<MobyLook> look_for(
    const VisualClass& visual, Kind kind, int level, bool energy, std::uint32_t tick
);

// The weapons' mobys drawn in their tiers.
//
// Holds the mod, the view and the weapons by reference; the mod's state owns them. Only the game's
// thread uses it.
class WeaponVisuals {
public:
    // Draws the weapons of `weapons` in the game `view` of `mod` shows.
    WeaponVisuals(openrac::Mod& mod, const GameView& view, const Weapons& weapons)
        : m_mod(mod), m_view(view), m_weapons(weapons) {}

    // Finds the port's moby_material and whether it draws energy, and says what this port lacks.
    void start();

    // Once a game tick (`tick` the mod's count): each weapon moby's look, sent where it changed.
    void tick(std::uint32_t tick);

private:
    // A moby the mod drew: its class then and the look sent.
    struct Drawn {
        int moby_class;
        MobyLook look;
    };

    openrac::Mod& m_mod;
    const GameView& m_view;
    const Weapons& m_weapons;
    bool m_material = false;  // The port has moby_material (1.23).
    bool m_energy = false;    // It draws energy (1.25).
    int m_level = -1;         // The level the drawn mobys are in.
    std::unordered_map<std::uint32_t, Drawn> m_drawn;  // By moby address.
};

}  // namespace ng_plus_plus
