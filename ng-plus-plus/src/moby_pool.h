// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The game's pool of spawned mobys (README.md, "Why 0.4.4 asks for more room"): every bolt, shot,
// effect and enemy copy is a moby from a pool of spare slots the level's file sets (200 to 400), and
// a spawn with none free is lost. Interface 1.27's moby_pool_raise adds slots to the levels loaded
// from then on, moby_pool_status counts them. Both are read from the port's table while the mods
// build against an older released SDK (sdk.lock), like bolt_api.h's.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "extra_enemies.h"
#include "openrac/mod.hpp"
#include "openrac/openrac_mod.h"

namespace ng_plus_plus {

// The interface minor with the pool calls.
inline constexpr std::uint16_t kPoolMinor = 27;

// How many slots NG++ asks for: Barlow's 79 enemy copies and the bolts of a long streak fit in 400.
inline constexpr int kPoolExtra = 400;

// moby_pool_status's answer, openrac_moby_pool's layout of 1.27.
struct PoolStatus {
    std::uint32_t size = 0;
    std::int32_t capacity = 0;  // The spare slots in all.
    std::int32_t live = 0;      // The ones alive.
    std::int32_t free = 0;      // The free ones.
    std::int32_t extra = 0;     // The slots this level's load added.
    std::int32_t requested = 0; // The slots the mods asked for.
};

using PoolRaiseFn = int (*)(openrac_mod*, int);
using PoolStatusFn = int (*)(openrac_mod*, PoolStatus*);

#if OPENRAC_MOD_API_MINOR < 27
// The port's table up to 1.27's pool calls, as released.
struct PoolApi {
    CopyApi upto_copy;  // Up to 1.26's moby_copy.
    PoolRaiseFn moby_pool_raise;
    PoolStatusFn moby_pool_status;
};
#else
using PoolApi = openrac_api;
#endif

// The port's pool calls, or nulls on a port older than 1.27.
struct PoolCalls {
    PoolRaiseFn raise = nullptr;
    PoolStatusFn status = nullptr;
};

// Returns `api`'s pool calls.
inline PoolCalls pool_calls_of(const openrac_api& api) {
    const std::size_t end = offsetof(PoolApi, moby_pool_status) + sizeof(PoolStatusFn);

    // An older port has no such entries; what follows its table is not its.
    if (api.api_minor < kPoolMinor || api.size < end) {
        return {};
    }

    const auto* table = reinterpret_cast<const PoolApi*>(&api);
    return {
        reinterpret_cast<PoolRaiseFn>(table->moby_pool_raise),
        reinterpret_cast<PoolStatusFn>(table->moby_pool_status),
    };
}

// Asks the port for kPoolExtra more spare slots in the levels loaded from now on. Returns the slots
// granted, 0 on a port without the call. Ask in the mod's load, before the first level.
inline int request_pool(openrac::Mod& mod) {
    const PoolCalls calls = pool_calls_of(mod.api());
    return calls.raise != nullptr ? std::max(0, calls.raise(mod.handle(), kPoolExtra)) : 0;
}

// Fills `out` with the level's pool as it is now. False outside a level or on a port without the
// call.
inline bool pool_status(const openrac::Mod& mod, PoolStatus& out) {
    const PoolCalls calls = pool_calls_of(mod.api());
    out = PoolStatus{};
    out.size = sizeof out;
    return calls.status != nullptr && calls.status(mod.handle(), &out) == OPENRAC_HOOK_OK;
}

}  // namespace ng_plus_plus
