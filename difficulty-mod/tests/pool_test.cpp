// SPDX-License-Identifier: GPL-3.0-or-later
// The moby pool's guard (src/pool.h): on a pool laid out here as LoadLevelGameplay lays one out,
// the free slots are counted as CreateMoby would find them, the tail is reserved only while it was
// never used and gives the room it says, and the copies' plan never takes more than its budget,
// so the slots kept for the game (rules.h: k_pool_reserve) stay free.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "pool.h"
#include "rules.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

using namespace dm;

namespace {

constexpr std::uint32_t kGlobals = 0x1000;  // base, spare, end, (pad), pvars
constexpr std::uint32_t kBase = 0x10000;
std::vector<std::uint8_t> g_mem(0x80000);

void put(std::uint32_t at, std::uint32_t v) {
    std::memcpy(g_mem.data() + at, &v, 4);
}

std::uint32_t get(std::uint32_t at) {
    std::uint32_t v;
    std::memcpy(&v, g_mem.data() + at, 4);
    return v;
}

const PoolGlobals kAt = {kGlobals, kGlobals + 4, kGlobals + 8, kGlobals + 0x10};

// A level of `placed` mobys and n2 spare slots, as the load leaves it.
Pool lay_out(int placed, int n2) {
    std::fill(g_mem.begin(), g_mem.end(), 0);
    const std::uint32_t spare = kBase + static_cast<std::uint32_t>(placed) * k_moby_size;
    const std::uint32_t end =
        kBase + static_cast<std::uint32_t>(placed + n2) * k_moby_size - k_moby_size;
    put(kGlobals, kBase);
    put(kGlobals + 4, spare);
    put(kGlobals + 8, end);
    put(kGlobals + 0x10, end + k_moby_size);
    g_mem[spare + 0x20] = 0xFF;
    return Pool::read(g_mem.data(), kAt);
}

// CreateMoby's search: the first free slot from spare to end, the mark moved on past a new one.
std::uint32_t create() {
    const Pool p = Pool::read(g_mem.data(), kAt);
    for (std::uint32_t m = p.spare; m < p.end; m += k_moby_size) {
        if (g_mem[m + 0x20] >= 0xFE) {
            if (g_mem[m + 0x20] == 0xFF) {
                g_mem[m + k_moby_size + 0x20] = 0xFF;
            }
            g_mem[m + 0x20] = 0;
            return m;
        }
    }
    return 0;
}

}  // namespace

int main() {
    // An empty pool: every usable slot free (255 of n2 = 256).
    Pool p = lay_out(100, 256);
    CHECK(p.sane() && p.usable() == 255);
    CHECK(count_free(g_mem.data(), p) == 255);
    // Some in use, one freed again (DeleteMoby's 0xFE).
    const std::uint32_t a = create(), b = create(), c = create();
    CHECK(a == p.spare && b == a + k_moby_size && c == b + k_moby_size);
    CHECK(count_free(g_mem.data(), p) == 252);
    g_mem[b + 0x20] = 0xFE;
    CHECK(count_free(g_mem.data(), p) == 253);
    // Not sane: nothing counted.
    CHECK(count_free(g_mem.data(), Pool{}) == 0);

    // The tail: reserved before any of it was used, the end moved down by that many slots and the
    // room the pvars of the slots taken out (and of the end slot), right after the kept slots'.
    p = lay_out(100, 256);
    std::uint32_t room = 0, size = 0;
    CHECK(reserve_tail(g_mem.data(), kAt, 12, &room, &size));
    const Pool r = Pool::read(g_mem.data(), kAt);
    CHECK(r.end == p.end - 12 * k_moby_size && r.usable() == 243);
    CHECK(room == p.pvars + 243 * k_spare_pvars && size == 13 * k_spare_pvars);
    CHECK(room + size == p.pvars + 256 * k_spare_pvars);  // the end of the spare pvars
    CHECK(count_free(g_mem.data(), r) == 243);
    // CreateMoby never reaches the reserved slots: after every usable slot is taken it finds none.
    for (int i = 0; i < 243; ++i) {
        CHECK(create() != 0);
    }
    CHECK(create() == 0);
    // Refused once the mark has gone past where the new end would be, or for too many slots.
    p = lay_out(10, 256);
    for (int i = 0; i < 250; ++i) {
        create();
    }
    CHECK(!reserve_tail(g_mem.data(), kAt, 10, &room, &size));
    CHECK(Pool::read(g_mem.data(), kAt).end == p.end);
    p = lay_out(10, 256);
    CHECK(!reserve_tail(g_mem.data(), kAt, 255, &room, &size));
    CHECK(!reserve_tail(g_mem.data(), kAt, 0, &room, &size));

    // The plan: round by round, never past the budget.
    std::vector<CloneAsk> asks(200, CloneAsk{3, 0});
    ClonePlan plan = plan_clones(asks, clone_budget(255, 4));
    CHECK(plan.slots + plan.pvar_slots == 255 - k_pool_reserve);
    for (int n : plan.copies) {
        CHECK(n == 0 || n == 1);  // spread: one each before anyone gets a second
    }
    asks.assign(10, CloneAsk{3, 0});
    plan = plan_clones(asks, clone_budget(255, 4));
    CHECK(plan.slots == 30 && plan.pvar_slots == 0);
    // Large pvars cost their slots too.
    asks.assign(100, CloneAsk{1, 5});
    plan = plan_clones(asks, clone_budget(255, 4));
    CHECK(plan.slots == 15 && plan.pvar_slots == 75 && plan.slots + plan.pvar_slots <= 95);
    // Mixed: what fits is taken, the rest left.
    asks = {CloneAsk{2, 50}, CloneAsk{2, 0}, CloneAsk{2, 0}};
    plan = plan_clones(asks, 60);
    CHECK(
        plan.copies[0] == 1 && plan.copies[1] == 2 && plan.copies[2] == 2
        && plan.slots + plan.pvar_slots <= 60
    );
    // No budget, no copies; Gaspar has none.
    plan = plan_clones(asks, 0);
    CHECK(plan.slots == 0 && plan.pvar_slots == 0);
    CHECK(plan_clones(asks, clone_budget(255, 9)).slots == 0);

    // The whole guard on a level: free slots, the plan within the budget, the tail reserved for the
    // pvars, the copies created; what is left free is at least what the game keeps.
    for (const int pv : {0, 2, 6}) {
        p = lay_out(300, 256);
        asks.assign(300, CloneAsk{3, pv});
        const int free = count_free(g_mem.data(), p);
        plan = plan_clones(asks, clone_budget(free, 3));
        if (plan.pvar_slots > 0) {
            CHECK(reserve_tail(g_mem.data(), kAt, plan.pvar_slots, &room, &size));
            CHECK(size >= static_cast<std::uint32_t>(plan.pvar_slots) * k_spare_pvars);
        }
        for (int i = 0; i < plan.slots; ++i) {
            CHECK(create() != 0);
        }
        CHECK(count_free(g_mem.data(), Pool::read(g_mem.data(), kAt)) >= k_pool_reserve);
    }

    std::printf("pool_test: ok\n");
    return 0;
}
