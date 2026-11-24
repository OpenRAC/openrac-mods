// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The one entry of interface 1.23 NG++ uses, moby_material (a render-only material on one moby),
// read from the port's table while the mods build against an older released SDK (sdk.lock). The
// table keeps every released entry in order: 1.19's three developer menu calls (in SDK 1.20),
// 1.21's hook_entry, 1.22's moby_visuals, guest_text, sprite_tint and pickup_animation, then
// 1.23's moby_material (OpenRAC's sdk/include/openrac/openrac_mod.h). An SDK of 1.23 or later has
// them all, and the shim is its own table.

#pragma once

#include <cstddef>
#include <cstdint>

#include "openrac/openrac_mod.h"

namespace ng_plus_plus {

// moby_material's signature: (mod, moby, material, rgb), OPENRAC_HOOK_OK when it is drawn so.
using MobyMaterialFn = int (*)(openrac_mod*, openrac_gaddr, std::uint32_t, std::uint32_t);

#if OPENRAC_MOD_API_MINOR < 23
// The port's table up to 1.23's moby_material, as released. Only the last entry is called; the
// others hold its place.
struct BoltApi {
    openrac_api base;  // The table this SDK knows.
#if OPENRAC_MOD_API_MINOR < 21
    void (*hook_entry)(void);  // 1.21: hook_entry.
#endif
#if OPENRAC_MOD_API_MINOR < 22
    void (*visuals[4])(void);  // 1.22: moby_visuals, guest_text, sprite_tint, pickup_animation.
#endif
    MobyMaterialFn moby_material;  // 1.23.
};

// Returns the table this SDK knows within `api`.
inline openrac_api& bolt_base(BoltApi& api) {
    return api.base;
}
#else
// An SDK of 1.23 or later: its own table has moby_material.
using BoltApi = openrac_api;

// Returns `api` itself.
inline openrac_api& bolt_base(BoltApi& api) {
    return api;
}
#endif

// The interface minor that has moby_material.
inline constexpr std::uint16_t kMaterialMinor = 23;

// What moby_material draws (OPENRAC_MOBY_MATERIAL_NONE and _PLATINUM of interface 1.23, and
// _OBSIDIAN of 1.24, whose rgb is its glow's colour).
inline constexpr std::uint32_t kMaterialNone = 0;
inline constexpr std::uint32_t kMaterialPlatinum = 2;
inline constexpr std::uint32_t kMaterialObsidian = 3;

// The interface minor whose moby_material draws obsidian (the same entry; a 1.23 port refuses it).
inline constexpr std::uint16_t kObsidianMinor = 24;

// The rgb that leaves a material's colours as they are (0xRRGGBB, all channels full).
inline constexpr std::uint32_t kNoTint = 0xFFFFFFu;

// The mobys one mod has a material on at once (OPENRAC_MOBY_MATERIALS_MAX of interface 1.23).
inline constexpr std::size_t kMaterialsMax = 1024;

// Returns the port's moby_material, or null on a port older than 1.23 (or whose table ends before
// the entry).
inline MobyMaterialFn moby_material_of(const openrac_api& api) {
    const std::size_t end = offsetof(BoltApi, moby_material) + sizeof(MobyMaterialFn);

    // An older port has no such entry: what follows its table is not its.
    if (api.api_minor < kMaterialMinor || api.size < end) {
        return nullptr;
    }

    return reinterpret_cast<const BoltApi*>(&api)->moby_material;
}

// Returns whether the port's moby_material draws obsidian (interface 1.24 or later).
inline bool has_obsidian(const openrac_api& api) {
    return moby_material_of(api) != nullptr && api.api_minor >= kObsidianMinor;
}

}  // namespace ng_plus_plus
