// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The ship's looks (ship_looks.h).

#include "ship_looks.h"

#include "space_missions.h"
#include "tiers.h"
#include "weapon_visuals.h"

namespace ng_plus_plus {

namespace {

// A setting's value and its look.
struct LookName {
    const char* value;
    ShipLook look;
    int tier;           // The ship tier it needs.
    std::uint32_t rgb;  // Its colour (the energy looks').
};

constexpr LookName kLooks[] = {
    {"off", ShipLook::Off, 0, 0xFFFFFFu},
    {"tier", ShipLook::Tier, 0, 0xFFFFFFu},
    {"ion", ShipLook::Ion, 1, 0x20E0FFu},
    {"plasma", ShipLook::Plasma, 2, 0xA040FFu},
    {"solar", ShipLook::Solar, 3, 0xFFC030u},
    {"inferno", ShipLook::Inferno, 5, 0xFF3020u},
    {"obsidian", ShipLook::Obsidian, 7, 0xFFFFFFu},
    {"prismatic", ShipLook::Prismatic, 8, 0xFFFFFFu},
};

// The energy the chosen looks glow with, and the ticks the prismatic one takes round.
constexpr int kLookEnergy = 11;
constexpr std::uint32_t kPrismTicks = 240u;

// The hub's hull (profile.cpp's kShipClass).
constexpr int kHullClass = 3412;

// A moby's pvars, and a shot's owner in them.
constexpr std::uint32_t kLookPvars = 0x68u;
constexpr std::uint32_t kLookOwner = 0x04u;

// Returns the look `look`'s entry.
const LookName& entry_of(ShipLook look) {
    // Each look.
    for (const LookName& entry : kLooks) {
        // This one.
        if (entry.look == look) {
            return entry;
        }
    }

    return kLooks[1];
}

// Returns the rainbow's colour at `tick`, 0xRRGGBB at full brightness.
std::uint32_t prism_rgb(std::uint32_t tick) {
    const std::uint32_t phase = (tick % kPrismTicks) * 6u * 255u / kPrismTicks;
    const std::uint32_t part = phase % 255u;
    const std::uint32_t up = part;
    const std::uint32_t down = 255u - part;

    // The six sides of the colour wheel.
    switch (phase / 255u) {
        case 0:
            return 0xFF0000u | up << 8;
        case 1:
            return down << 16 | 0x00FF00u;
        case 2:
            return 0x00FF00u | up;
        case 3:
            return down << 8 | 0x0000FFu;
        case 4:
            return up << 16 | 0x0000FFu;
        default:
            return 0xFF0000u | down;
    }
}

}  // namespace

ShipLook ship_look_of(std::string_view value) {
    // Each look's value.
    for (const LookName& entry : kLooks) {
        // This one.
        if (value == entry.value) {
            return entry.look;
        }
    }

    return ShipLook::Tier;
}

int ship_look_tier(ShipLook look) {
    return entry_of(look).tier;
}

std::optional<ShipPaint> ship_paint(ShipLook look, int tier, std::uint32_t tick) {
    // The game's paint alone.
    if (look == ShipLook::Off || tier <= 0) {
        return std::nullopt;
    }

    // Not unlocked yet, or the tier's own: its colour as the weapons' tiers have it.
    if (look == ShipLook::Tier || tier < ship_look_tier(look)) {
        const int level = tier < kTiers ? tier : kTiers;
        const std::optional<std::uint32_t> rgb = effect_rgb(Kind::Four, level, tick);

        // A tier with no colour: none.
        if (!rgb) {
            return std::nullopt;
        }

        return ShipPaint{energy_at(effect_strength(Kind::Four, level)), *rgb};
    }

    // Obsidian.
    if (look == ShipLook::Obsidian) {
        return ShipPaint{kMaterialObsidian, kNoTint};
    }

    // The rainbow.
    if (look == ShipLook::Prismatic) {
        return ShipPaint{energy_at(kLookEnergy + 2), prism_rgb(tick)};
    }

    return ShipPaint{energy_at(kLookEnergy), entry_of(look).rgb};
}

bool is_ship_moby(int moby_class) {
    return moby_class == kHullClass || moby_class == kShipShotClass
           || (is_player_side(moby_class) && moby_class != 3262);
}

void ShipLooks::start() {
    m_material = moby_material_of(m_mod.api());
    m_energy = m_material != nullptr && m_mod.api().api_minor >= kEnergyMinor;
    m_obsidian = m_material != nullptr && m_mod.api().api_minor >= 24;

    // Said once, for a player's log.
    if (m_look != ShipLook::Off && !m_energy) {
        m_mod.info("no ship looks in this OpenRAC: they need mod interface 1.25");
    }
}

void ShipLooks::tick(std::uint32_t tick) {
    // No look, or no energy to draw it with.
    if (m_look == ShipLook::Off || !m_energy || m_view.loaded_level() < 0) {
        return;
    }

    std::optional<ShipPaint> paint = ship_paint(m_look, m_ship.tier(), tick);

    // Obsidian refused by this port: the tier's colour.
    if (paint && paint->material == kMaterialObsidian && !m_obsidian) {
        paint = ship_paint(ShipLook::Tier, m_ship.tier(), tick);
    }

    // Nothing to draw yet (before Alpha), or no mobys to draw.
    if (!paint || !m_scan.read(tick)) {
        return;
    }

    const openrac::Memory memory = m_view.memory();

    // Each of the ship's mobys, every tick (a material lasts until the moby goes, and the
    // animated looks move).
    for (const ScannedMoby& moby : m_scan.mobys()) {
        // Not the ship's.
        if (!is_ship_moby(moby.moby_class)) {
            continue;
        }

        // A shot: only the ones his ship fired (its pvars +4, UpdateFeltzinProjectile's owner).
        if (moby.moby_class == kShipShotClass) {
            const auto pvars = memory.read<std::uint32_t>(moby.address + kLookPvars);
            const std::optional<int> owner =
                GameView::is_memory(pvars, 8u)
                    ? m_view.moby_class(memory.read<std::uint32_t>(pvars + kLookOwner))
                    : std::nullopt;

            // Another's shot.
            if (!owner || *owner != kShipClass) {
                continue;
            }
        }

        m_material(m_mod.handle(), moby.address, paint->material, paint->rgb);
    }
}

}  // namespace ng_plus_plus
