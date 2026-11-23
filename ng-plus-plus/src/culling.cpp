// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The off-screen picks of the Zodiac and the Plasma Coil (culling.h).

#include "culling.h"

#include <cstddef>
#include <string>

#include "weapon_api.h"

namespace ng_plus_plus {

#if OPENRAC_MOD_API_MINOR >= 30
static_assert(
    offsetof(openrac_api, weapon_ignore_culling)
        == offsetof(openrac_api, weapon_record) + sizeof(void*),
    "weapon_ignore_culling right after the seven weapon calls"
);
#endif

IgnoreCullingFn ignore_culling_of(const openrac_api& api) {
    // The call follows 1.29's seven weapon calls in the table.
    const std::size_t at = weapon_calls_offset() + sizeof(WeaponCallsTable);

    // An older port: what follows its table is not its.
    if (api.api_minor < kCullingMinor || api.size < at + sizeof(IgnoreCullingFn)) {
        return nullptr;
    }

    return *reinterpret_cast<const IgnoreCullingFn*>(
        reinterpret_cast<const unsigned char*>(&api) + at
    );
}

int start_culling(openrac::Mod& mod, bool enabled) {
    // Switched off: the game's picks.
    if (!enabled) {
        return 0;
    }

    const IgnoreCullingFn fn = ignore_culling_of(mod.api());

    // A port before 1.30: the game's picks too.
    if (fn == nullptr) {
        mod.info(
            "the Zodiac and the Plasma Coil miss enemies off screen in this build: hitting them "
            "needs OpenRAC with mod interface 1.30"
        );
        return 0;
    }

    int taken = 0;
    std::string kept;
    for (const std::int32_t item : kCullingItems) {
        if (fn(mod.handle(), item, 1) == OPENRAC_HOOK_OK) {
            ++taken;
        } else {
            kept += (kept.empty() ? "" : ", ") + std::to_string(item);
        }
    }

    // Refused (a port before the drones' and the shield's picks): one line.
    if (!kept.empty()) {
        mod.info("the port keeps the game's on-screen targets for weapons " + kept);
    }
    return taken;
}

}  // namespace ng_plus_plus
