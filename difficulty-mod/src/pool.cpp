// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The level's moby pool (pool.h).
#include "pool.h"

#include <cstring>

namespace dm {

namespace {

std::uint32_t word(const std::uint8_t* mem, std::uint32_t at) {
    std::uint32_t v;
    std::memcpy(&v, mem + at, 4);
    return v;
}

std::uint8_t state(const std::uint8_t* mem, std::uint32_t moby) {
    return mem[moby + 0x20u];
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
           && (end - spare) % k_moby_size == 0 && (end - spare) / k_moby_size < 0x8000u
           && pvars != 0;
}

int Pool::usable() const {
    return sane() ? static_cast<int>((end - spare) / k_moby_size) : 0;
}

int count_free(const std::uint8_t* mem, const Pool& pool) {
    if (!pool.sane()) {
        return 0;
    }
    int free = 0;
    for (std::uint32_t m = pool.spare; m < pool.end; m += k_moby_size) {
        const std::uint8_t s = state(mem, m);
        if (s == 0xFF) {
            return free + static_cast<int>((pool.end - m) / k_moby_size);
        }
        if (s >= 0xFE) {
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
    if (slots <= 0 || slots >= usable) {
        return false;
    }
    const std::uint32_t new_end = pool.end - static_cast<std::uint32_t>(slots) * k_moby_size;
    // The never-used mark must be before the new end, and every slot from there to the old end
    // (the end slot itself too) never used: zero, as the load left it.
    std::uint32_t mark = 0;
    for (std::uint32_t m = pool.spare; m < pool.end; m += k_moby_size) {
        if (state(mem, m) == 0xFF) {
            mark = m;
            break;
        }
    }
    if (mark == 0 || mark >= new_end) {
        return false;
    }
    for (std::uint32_t m = new_end; m <= pool.end; m += k_moby_size) {
        if (state(mem, m) != 0) {
            return false;
        }
    }
    const std::uint32_t kept = static_cast<std::uint32_t>(usable - slots);
    std::memcpy(mem + at.end, &new_end, 4);
    *room = pool.pvars + kept * k_spare_pvars;
    *room_size = static_cast<std::uint32_t>(slots + 1) * k_spare_pvars;
    return true;
}

ClonePlan plan_clones(const std::vector<CloneAsk>& asks, int budget) {
    ClonePlan plan;
    plan.copies.assign(asks.size(), 0);
    int left = budget > 0 ? budget : 0;
    for (bool more = true; more;) {
        more = false;
        for (std::size_t i = 0; i < asks.size(); ++i) {
            if (plan.copies[i] >= asks[i].want) {
                continue;
            }
            const int cost = 1 + (asks[i].pvslots > 0 ? asks[i].pvslots : 0);
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
