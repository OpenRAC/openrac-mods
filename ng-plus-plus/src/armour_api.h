// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The entries of interface 1.34 the armour tiers use, read from the port's table while the mods
// build against an older released SDK (sdk.lock): the paint material for moby_material
// (OPENRAC_MOBY_MATERIAL_PAINT, with its rim glow's level) and moby_material_class, a material on
// every moby of a class in the world and the menus' own pictures (the armour vendor's Ratchet).
// moby_material_class is the 17th entry after 1.30's weapon_ignore_culling (culling.h); the table
// keeps every released entry in order (OpenRAC's sdk/api/openrac_api.lock).

#pragma once

#include <cstddef>
#include <cstdint>

#include "openrac/openrac_mod.h"
#include "weapon_api.h"

namespace ng_plus_plus {

// moby_material_class's signature: (mod, class, scope, material, rgb), OPENRAC_HOOK_OK when set.
using MaterialClassFn = int (*)(openrac_mod*, int, std::uint32_t, std::uint32_t, std::uint32_t);

// The interface minor with paint and moby_material_class.
inline constexpr std::uint16_t kPaintMinor = 34;

// OPENRAC_MOBY_MATERIAL_PAINT and its rim glow's level in bits 8 to 11 (0 to kPaintTop).
inline constexpr std::uint32_t kMaterialPaint = 5;
inline constexpr int kPaintTop = 15;

// moby_material_class's scopes: the level's world, and the menus' own pictures.
inline constexpr std::uint32_t kScopeWorld = 1;
inline constexpr std::uint32_t kScopeMenu = 2;

// Ratchet's class: the hero, and the armour vendor's picture of him in the armour it shows.
inline constexpr int kHeroClass = 0;

// The entries from weapon_ignore_culling to moby_material_class in the table.
inline constexpr std::size_t kMaterialClassAfterCulling = 17;

// Returns the paint material with its rim glowing at `level` (0 to kPaintTop).
constexpr std::uint32_t paint_at(int level) {
    return kMaterialPaint | (static_cast<std::uint32_t>(level) & 0xFu) << 8;
}

#if OPENRAC_MOD_API_MINOR >= 34
static_assert(
    offsetof(openrac_api, moby_material_class)
        == offsetof(openrac_api, weapon_ignore_culling)
               + kMaterialClassAfterCulling * sizeof(void*),
    "moby_material_class 17 entries after weapon_ignore_culling"
);
#endif

// Returns `api`'s moby_material_class, or null on a port older than 1.34 (or whose table ends
// first).
inline MaterialClassFn material_class_of(const openrac_api& api) {
    // weapon_ignore_culling follows 1.29's seven weapon calls (culling.cpp).
    const std::size_t at = weapon_calls_offset() + sizeof(WeaponCallsTable)
                           + kMaterialClassAfterCulling * sizeof(void*);

    // An older port: what follows its table is not its.
    if (api.api_minor < kPaintMinor || api.size < at + sizeof(MaterialClassFn)) {
        return nullptr;
    }

    return *reinterpret_cast<const MaterialClassFn*>(
        reinterpret_cast<const unsigned char*>(&api) + at
    );
}

}  // namespace ng_plus_plus
