// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The tiers' look on the weapons' own mobys (weapon_visuals.h).

#include "weapon_visuals.h"

#include <string>
#include <string_view>

#include "bolt_api.h"
#include "discs.h"
#include "perk_rules.h"
#include "tiers.h"

namespace ng_plus_plus {
namespace {

// The level's moby table: the first moby's and past the last's words (g_mobyTableBase and
// g_mobyTableEnd, the level's copies through relocate; OpenRAC's
// port/game/rac2-<disc>/host/rac2_addresses.h). NTSC-U v1.01's and PAL's are at the same address.
constexpr std::uint32_t kMobyTable = 0x001B1B5Cu;
constexpr std::uint32_t kMobyTableV2 = 0x001B1ADCu;
constexpr std::uint32_t kMobyTableEnd = 0x08u;  // past the last's word, after the first's

// A moby record's size, and its state byte (+0x20: 0x80 and above for a dead or free slot; seen in
// game code: the moby loop, OpenRAC's rac2/host/map_markers.c).
constexpr std::uint32_t kMobyBytes = 0x100u;
constexpr std::uint32_t kMobyState = 0x20u;
constexpr std::uint8_t kMobyDead = 0x80u;

// The most mobys a level holds that the mod looks through: more is a broken table.
constexpr std::uint32_t kMostMobys = 8192u;

// Ticks between two looks of an animated tier (Omega's rainbow, Ultima's beat): every other tick
// keeps the calls few and the colour smooth.
constexpr std::uint32_t kAnimatedTicks = 2u;

// Returns the moby table's word on the disc `version`.
std::uint32_t moby_table(std::string_view version) {
    return version == "ntsc2" ? kMobyTableV2 : kMobyTable;
}

}  // namespace

const VisualClass* visual_class(int moby_class) {
    // The table is short: a plain look.
    for (const VisualClass& visual : kVisualClasses) {
        if (visual.moby_class == moby_class) {
            return &visual;
        }
    }

    return nullptr;
}

std::optional<MobyLook> look_for(
    const VisualClass& visual, Kind kind, int level, bool energy, std::uint32_t tick
) {
    const std::optional<std::uint32_t> rgb = weapon_effect_rgb(visual.item, level, tick);

    // No tier: the game's own look.
    if (!rgb) {
        return std::nullopt;
    }

    // Energy: the moby and what it makes, stronger with each tier.
    if (energy) {
        return MobyLook{energy_at(effect_strength(kind, level)), *rgb};
    }

    // Without it only a shot's, bomb's or bot's body takes the colour.
    if (visual.body) {
        return MobyLook{kMaterialTint, *rgb};
    }

    return std::nullopt;
}

void WeaponVisuals::start() {
    const openrac_api& api = m_mod.api();
    m_material = moby_material_of(api) != nullptr;
    m_energy = m_material && api.api_minor >= kEnergyMinor;

    // What this port lacks, for a player's log.
    if (!m_material) {
        m_mod.info("no weapon looks for the tiers in this build: they need OpenRAC 1.25");
    } else if (!m_energy) {
        m_mod.info(
            "tier colours on the weapons' shots and bots only in this build: their effects need "
            "OpenRAC 1.25"
        );
    }
}

void WeaponVisuals::tick(std::uint32_t tick) {
    // A port without the call draws the game's look.
    if (!m_material) {
        return;
    }

    const int level = m_view.loaded_level();

    // Another level (or none): the port has dropped the looks with the level's mobys.
    if (level != m_level) {
        m_drawn.clear();
        m_level = level;
    }

    // Outside a level there are no mobys.
    if (level < 0) {
        return;
    }

    const MobyMaterialFn material = moby_material_of(m_mod.api());
    const openrac::Memory memory = m_view.memory();
    const std::uint32_t table = moby_table(m_mod.version());
    const auto first = memory.read<std::uint32_t>(m_mod.relocate(table));
    const auto end = memory.read<std::uint32_t>(m_mod.relocate(table + kMobyTableEnd));
    const bool animated_tick = tick % kAnimatedTicks == 0;

    // A table not yet laid out, or broken: nothing to look through.
    if (first == 0 || end <= first || (end - first) / kMobyBytes > kMostMobys) {
        return;
    }

    std::unordered_map<std::uint32_t, Drawn> seen;

    // Each live moby of a weapon's class.
    for (std::uint32_t moby = first; moby + kMobyBytes <= end; moby += kMobyBytes) {
        // A dead or free slot.
        if (memory.read<std::uint8_t>(moby + kMobyState) >= kMobyDead) {
            continue;
        }

        const int moby_class = memory.read<std::uint16_t>(moby + kMobyClass);
        const VisualClass* visual = visual_class(moby_class);

        // A class no weapon owns.
        if (visual == nullptr) {
            continue;
        }

        const WeaponView weapon = m_weapons.weapon(visual->item);
        const int tier = weapon.has_tier() ? weapon.level : 0;
        const std::optional<MobyLook> look =
            look_for(*visual, weapon.kind, tier, m_energy, m_view.sixtieths(tick));
        const auto drawn = m_drawn.find(moby);
        const bool known = drawn != m_drawn.end() && drawn->second.moby_class == moby_class;

        // No tier: the game's look, taken back where the mod drew one.
        if (!look) {
            if (known) {
                material(m_mod.handle(), moby, kMaterialNone, kNoTint);
            }

            continue;
        }

        // The same look as sent: an animated one moves on every other tick only.
        const bool moves = weapon_tint_moves(visual->item, tier);
        const bool same = known && drawn->second.look == *look;
        const bool resend = !known || (!same && (!moves || animated_tick));

        // Sent, or kept as it was sent.
        if (resend && material(m_mod.handle(), moby, look->material, look->rgb) == OPENRAC_HOOK_OK) {
            seen[moby] = Drawn{moby_class, *look};
        } else if (known) {
            seen[moby] = drawn->second;
        }
    }

    // A moby no longer listed went with the game; the port dropped its look.
    m_drawn = std::move(seen);
}

}  // namespace ng_plus_plus
