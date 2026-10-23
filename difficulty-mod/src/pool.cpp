// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The level's moby pool (pool.h): reading its four globals, counting the free slots as CreateMoby
// would find them, taking the tail's slots out of its reach for the copies' large pvars, and
// planning how many copies each enemy gets within the slots there are. It touches game memory only
// through the `mem` pointer it is given, so the tests lay a pool out in an array.
#include "pool.h"

#include <cstddef>
#include <cstring>

namespace dm {

namespace {

// A moby's state byte offset (documented: pool.h).
constexpr std::uint32_t kStateOffset = 0x20u;
// The state of a slot CreateMoby never handed out, which marks where the never-used ones begin
// (documented: pool.h).
constexpr std::uint8_t kNeverUsed = 0xFF;
// From this state up a slot is free: 0xFE was freed, 0xFF never used (documented: pool.h).
constexpr std::uint8_t kFreeFrom = 0xFE;
// The most spare slots believed in a pool (assumed).
constexpr std::uint32_t kMaxSpareSlots = 0x8000u;
// The size of a game word.
constexpr std::uint32_t kWord = 4u;

// The 32-bit word at game address `at`.
std::uint32_t word(const std::uint8_t* mem, std::uint32_t at) {
    std::uint32_t v;
    std::memcpy(&v, mem + at, kWord);
    return v;
}

// The state byte of the moby at game address `moby`.
std::uint8_t state(const std::uint8_t* mem, std::uint32_t moby) {
    return mem[moby + kStateOffset];
}

}  // namespace

Pool Pool::read(const std::uint8_t* mem, const PoolGlobals& at) {
    Pool p;
    p.base = word(mem, at.base);
    p.spare = word(mem, at.spare);
    p.end = word(mem, at.end);
    p.pvars = word(mem, at.pvars);
    return p;
}

bool Pool::sane() const {
    return base != 0 && spare >= base && end > spare && (spare - base) % k_moby_size == 0
           && (end - spare) % k_moby_size == 0 && (end - spare) / k_moby_size < kMaxSpareSlots
           && pvars != 0;
}

int Pool::usable() const {
    return sane() ? static_cast<int>((end - spare) / k_moby_size) : 0;
}

int count_free(const std::uint8_t* mem, const Pool& pool) {
    // A pool that is not as laid out: none is counted free.
    if (!pool.sane()) {
        return 0;
    }

    int free = 0;

    // Each slot from the first spawned one.
    for (std::uint32_t m = pool.spare; m < pool.end; m += k_moby_size) {
        const std::uint8_t s = state(mem, m);

        // The never-used mark: this slot and all after it are free.
        if (s == kNeverUsed) {
            return free + static_cast<int>((pool.end - m) / k_moby_size);
        }

        // A freed slot: free too.
        if (s >= kFreeFrom) {
            ++free;
        }
    }

    return free;
}

bool reserve_tail(
    std::uint8_t* mem,
    const PoolGlobals& at,
    int slots,
    std::uint32_t* room,
    std::uint32_t* room_size
) {
    const Pool pool = Pool::read(mem, at);
    const int usable = pool.usable();

    // Nothing to take, or more than the pool has.
    if (slots <= 0 || slots >= usable) {
        return false;
    }

    const std::uint32_t new_end = pool.end - static_cast<std::uint32_t>(slots) * k_moby_size;
    // The never-used mark must be before the new end, and every slot from there to the old end
    // (the end slot itself too) never used: zero, as the load left it.
    std::uint32_t mark = 0;

    // The first never-used slot.
    for (std::uint32_t m = pool.spare; m < pool.end; m += k_moby_size) {
        // This is the mark.
        if (state(mem, m) == kNeverUsed) {
            mark = m;
            break;
        }
    }

    // No mark, or one inside the tail: some of it may have been used.
    if (mark == 0 || mark >= new_end) {
        return false;
    }

    // Each slot of the tail and the end slot.
    for (std::uint32_t m = new_end; m <= pool.end; m += k_moby_size) {
        // One was used: its state is not the load's zero.
        if (state(mem, m) != 0) {
            return false;
        }
    }

    const std::uint32_t kept = static_cast<std::uint32_t>(usable - slots);
    std::memcpy(mem + at.end, &new_end, kWord);
    *room = pool.pvars + kept * k_spare_pvars;
    *room_size = static_cast<std::uint32_t>(slots + 1) * k_spare_pvars;
    return true;
}

ClonePlan plan_clones(const std::vector<CloneAsk>& asks, int budget) {
    ClonePlan plan;
    plan.copies.assign(asks.size(), 0);
    int left = budget > 0 ? budget : 0;

    // Rounds, until a whole round gives no copy.
    for (bool more = true; more;) {
        more = false;

        // Each enemy in turn.
        for (std::size_t i = 0; i < asks.size(); ++i) {
            // It has all the copies it asked for.
            if (plan.copies[i] >= asks[i].want) {
                continue;
            }

            const int cost = 1 + (asks[i].pvslots > 0 ? asks[i].pvslots : 0);

            // Its copy no longer fits the budget.
            if (cost > left) {
                continue;
            }

            left -= cost;
            ++plan.copies[i];
            ++plan.slots;
            plan.pvar_slots += asks[i].pvslots > 0 ? asks[i].pvslots : 0;
            more = true;
        }
    }

    return plan;
}

}  // namespace dm
