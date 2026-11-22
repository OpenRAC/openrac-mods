// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Interface 1.29's weapon calls (OpenRAC's sdk/include/openrac/openrac_mod.h, "Weapons and their
// shots"), read from the port's table while the mods build against an older released SDK
// (sdk.lock), like bolt_api.h's and moby_pool.h's: on_weapon_fire, weapon_refire, on_shot_end,
// moby_spawn_live, weapon_look, moby_status and weapon_record. A port older than 1.29 has none, and
// NG++ goes on with what 1.28 gives (perks.h).

#pragma once

#include <cstddef>
#include <cstdint>

#include "moby_pool.h"
#include "openrac/openrac_mod.h"

namespace ng_plus_plus {

// The interface minor with the weapon calls.
inline constexpr std::uint16_t kWeaponMinor = 29;

// openrac_weapon_shot.flags (OPENRAC_WEAPON_SHOT_*).
inline constexpr std::uint32_t kShotRefired = 0x1u;
inline constexpr std::uint32_t kShotLive = 0x2u;
inline constexpr std::uint32_t kShotRounds = 0x4u;

// openrac_shot_end.reason (OPENRAC_SHOT_END_*).
inline constexpr std::int32_t kEndGone = 0;
inline constexpr std::int32_t kEndImpact = 1;
inline constexpr std::int32_t kEndExpired = 2;
inline constexpr std::int32_t kEndLevel = 3;

// openrac_spawn_live.flags: the shot goes off where it is made (OPENRAC_SPAWN_LIVE_DETONATE).
inline constexpr std::uint32_t kSpawnDetonate = 0x1u;

// moby_status's statuses (OPENRAC_MOBY_STATUS_*).
inline constexpr std::uint32_t kStatusNone = 0u;
inline constexpr std::uint32_t kStatusFreeze = 1u;
inline constexpr std::uint32_t kStatusSlowHalf = 2u;
inline constexpr std::uint32_t kStatusSlowThird = 3u;

// The shots the port makes again or anew in a tick, all mods together (OPENRAC_SHOTS_PER_TICK_MAX).
inline constexpr int kShotsPerTickMax = 128;

// A shot the hero's weapon fired: openrac_weapon_shot's layout.
struct WeaponShot {
    std::uint32_t size;
    std::int32_t item;
    std::int32_t slot;
    std::int32_t oclass;
    std::int32_t owner;
    std::int32_t target;
    std::uint32_t serial;
    std::uint32_t flags;
    float position[3];
    float direction[3];
    float speed;
};

// A shot that ended: openrac_shot_end's layout.
struct ShotEnd {
    std::uint32_t size;
    std::int32_t item;
    std::int32_t slot;
    std::int32_t oclass;
    std::int32_t owner;
    std::int32_t target;
    std::uint32_t serial;
    std::uint32_t flags;
    std::int32_t reason;
    float position[3];
};

// A shot the game's own spawn makes: openrac_spawn_live's layout.
struct SpawnLive {
    std::uint32_t size;
    std::int32_t oclass;
    std::int32_t item;
    std::int32_t owner;
    std::int32_t target;
    std::uint32_t flags;
    float position[3];
    float velocity[3];
};

#if OPENRAC_MOD_API_MINOR >= 29
static_assert(sizeof(WeaponShot) == sizeof(openrac_weapon_shot), "openrac_weapon_shot's layout");
static_assert(sizeof(ShotEnd) == sizeof(openrac_shot_end), "openrac_shot_end's layout");
static_assert(sizeof(SpawnLive) == sizeof(openrac_spawn_live), "openrac_spawn_live's layout");
#endif

// The calls' signatures.
using WeaponFireFn = void (*)(void*, const WeaponShot*);
using ShotEndFn = void (*)(void*, const ShotEnd*);
using OnWeaponFireFn = int (*)(openrac_mod*, WeaponFireFn, void*);
using WeaponRefireFn = int (*)(openrac_mod*, std::uint32_t, float, float);
using OnShotEndFn = int (*)(openrac_mod*, ShotEndFn, void*);
using SpawnLiveFn = int (*)(openrac_mod*, const SpawnLive*);
using WeaponLookFn = int (*)(openrac_mod*, std::int32_t, std::uint32_t, std::uint32_t);
using MobyStatusFn = int (*)(openrac_mod*, std::int32_t, std::uint32_t, std::int32_t);
using WeaponRecordFn = openrac_gaddr (*)(openrac_mod*, std::int32_t);

// The port's table up to 1.29's weapon calls, as released: 1.27's pool calls (moby_pool.h), 1.28's
// developer_menu_look, then the seven.
struct WeaponCallsTable {
    OnWeaponFireFn on_weapon_fire;
    WeaponRefireFn weapon_refire;
    OnShotEndFn on_shot_end;
    SpawnLiveFn moby_spawn_live;
    WeaponLookFn weapon_look;
    MobyStatusFn moby_status;
    WeaponRecordFn weapon_record;
};

#if OPENRAC_MOD_API_MINOR < 28
struct WeaponApi {
    PoolApi upto_pool;              // Up to 1.27's moby_pool_status.
    void (*developer_menu_look)();  // 1.28.
    WeaponCallsTable calls;         // 1.29.
};
#elif OPENRAC_MOD_API_MINOR < 29
struct WeaponApi {
    openrac_api upto_menu_look;  // Up to 1.28's developer_menu_look.
    WeaponCallsTable calls;      // 1.29.
};
#endif

// Returns the byte offset of the seven calls in the port's table.
inline std::size_t weapon_calls_offset() {
#if OPENRAC_MOD_API_MINOR < 29
    return offsetof(WeaponApi, calls);
#else
    return offsetof(openrac_api, on_weapon_fire);
#endif
}

#if OPENRAC_MOD_API_MINOR >= 29
static_assert(
    offsetof(openrac_api, weapon_record) - offsetof(openrac_api, on_weapon_fire)
        == offsetof(WeaponCallsTable, weapon_record),
    "the seven weapon calls in order"
);
static_assert(
    offsetof(openrac_api, on_weapon_fire)
        == offsetof(openrac_api, developer_menu_look) + sizeof(void*),
    "the weapon calls right after developer_menu_look"
);
#endif

// Returns `api`'s weapon calls, or null on a port older than 1.29 (or whose table ends first).
inline const WeaponCallsTable* weapon_calls_of(const openrac_api& api) {
    const std::size_t end = weapon_calls_offset() + sizeof(WeaponCallsTable);

    // An older port: what follows its table is not its.
    if (api.api_minor < kWeaponMinor || api.size < end) {
        return nullptr;
    }

    return reinterpret_cast<const WeaponCallsTable*>(
        reinterpret_cast<const unsigned char*>(&api) + weapon_calls_offset()
    );
}

}  // namespace ng_plus_plus
