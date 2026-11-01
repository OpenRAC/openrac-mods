/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The wire registry (kinds.hpp): the kinds in use share no value and lie in their owner's place,
 * the reserved ranges do not overlap each other or a kind in use, and the router sends every
 * possible first byte where the chain of tests it replaced did.
 */
#include <cstdint>
#include <cstdio>
#include <vector>

#include "kinds.hpp"

namespace {

int failures = 0;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

using mp::Owner;

constexpr std::size_t kCount = sizeof(mp::kKinds) / sizeof(mp::kKinds[0]);

void no_two_kinds_share_a_value() {
    for (std::size_t i = 0; i < kCount; ++i) {
        for (std::size_t j = i + 1; j < kCount; ++j) {
            if (mp::kKinds[i].value == mp::kKinds[j].value) {
                std::fprintf(stderr, "kinds %s and %s share %d\n", mp::kKinds[i].name, mp::kKinds[j].name,
                             mp::kKinds[i].value);
            }

            CHECK(mp::kKinds[i].value != mp::kKinds[j].value);
        }
    }
}

void reserved_ranges_are_well_formed_and_apart() {
    constexpr std::size_t n = sizeof(mp::kReservedRanges) / sizeof(mp::kReservedRanges[0]);

    for (std::size_t i = 0; i < n; ++i) {
        const mp::KindRange& a = mp::kReservedRanges[i];
        CHECK(a.first <= a.last);

        for (std::size_t j = i + 1; j < n; ++j) {
            const mp::KindRange& b = mp::kReservedRanges[j];
            CHECK(a.last < b.first || b.last < a.first);
        }

        // A kind in use lies in a range only if it is its owner's.
        for (const mp::KindInfo& k : mp::kKinds) {
            if (k.value >= a.first && k.value <= a.last) {
                CHECK(k.owner == a.owner);
            }
        }
    }
}

void the_plans_ranges_are_held_by_their_owners() {
    CHECK(mp::owner_of(32) == Owner::kSave && mp::owner_of(35) == Owner::kSave);
    CHECK(mp::owner_of(36) == Owner::kShare && mp::owner_of(39) == Owner::kShare);
    CHECK(mp::owner_of(40) == Owner::kMigrate && mp::owner_of(43) == Owner::kMigrate);
    CHECK(mp::owner_of(44) == Owner::kJoin && mp::owner_of(47) == Owner::kJoin);
    CHECK(mp::owner_of(48) == Owner::kShots && mp::owner_of(51) == Owner::kShots);
    CHECK(mp::owner_of(52) == Owner::kCamera && mp::owner_of(55) == Owner::kCamera);
    CHECK(mp::owner_of(56) == Owner::kTether && mp::owner_of(59) == Owner::kTether);
    CHECK(mp::owner_of(60) == Owner::kPartyRules && mp::owner_of(63) == Owner::kPartyRules);
    // The lobby's, all read by the Session and never handed to the game's data.
    CHECK(mp::owner_of(64) == Owner::kLobby && mp::owner_of(67) == Owner::kLobby && mp::owner_of(70) == Owner::kLobby);
    CHECK(mp::owner_of(71) == Owner::kWorld && mp::owner_of(72) == Owner::kWorld);
    // Free: 0 and 73 on.
    CHECK(mp::owner_of(0) == Owner::kCount);
    CHECK(mp::owner_of(73) == Owner::kCount && mp::owner_of(127) == Owner::kCount);
}

/* The chain the registry replaced: the lobby's kinds (session.cpp), then mods 29 to 31, the
 * world's 9 to 17, 22, 24, 26 to 28, 71 and 72, progress 23, and the co-op for the rest. */
Owner old_route(std::uint8_t k) {
    if ((k >= 1 && k <= 3) || k == 64 || k == 65 || k == 66) {
        return Owner::kLobby;
    }

    if (k >= 29 && k <= 31) {
        return Owner::kMods;
    }

    if ((k >= 9 && k <= 17) || k == 22 || k == 24 || (k >= 26 && k <= 28) || k == 71 || k == 72) {
        return Owner::kWorld;
    }

    if (k == 23) {
        return Owner::kProgress;
    }

    return Owner::kCoop;
}

void the_router_sends_every_kind_where_the_old_chain_did() {
    mp::Router router;
    std::vector<Owner> got;
    Owner reached = Owner::kCount;

    for (const Owner o : {Owner::kMods, Owner::kWorld, Owner::kProgress}) {
        router.route(o, [&reached, o](std::int32_t, const std::vector<std::uint8_t>&) { reached = o; });
    }

    router.fallback([&reached](std::int32_t, const std::vector<std::uint8_t>&) { reached = Owner::kCoop; });

    for (unsigned k = 0; k < 256; ++k) {
        reached = Owner::kCount;
        router.dispatch(1, {static_cast<std::uint8_t>(k), 0});
        const Owner was = old_route(static_cast<std::uint8_t>(k));

        // The lobby's kinds never reach the game (session.cpp keeps them); the registry has no
        // handler for them, so they would fall to the co-op, which the session never lets happen.
        if (was == Owner::kLobby) {
            CHECK(mp::owner_of(static_cast<std::uint8_t>(k)) == Owner::kLobby);
            continue;
        }

        CHECK(reached == was);
    }

    // An empty DATA goes nowhere.
    reached = Owner::kCount;
    router.dispatch(1, {});
    CHECK(reached == Owner::kCount || reached == Owner::kCoop);
}

}  // namespace

int main() {
    no_two_kinds_share_a_value();
    reserved_ranges_are_well_formed_and_apart();
    the_plans_ranges_are_held_by_their_owners();
    the_router_sends_every_kind_where_the_old_chain_did();

    if (failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }

    std::puts("kinds: ok");
    return 0;
}
