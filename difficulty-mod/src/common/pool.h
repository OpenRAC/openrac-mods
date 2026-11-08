// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The level's moby pool, as LoadLevelGameplay lays it out (OpenRAC docs/port/KNOWN_ISSUES.md, "the
// moby pool fills up"; games/rac1 src/overlays/shared/loaders_00240398.c):
//
//   base             the placed mobys, 0x100 bytes each, in the gameplay file's order
//   spare            the first spawned moby's slot, the end of the placed ones; CreateMoby hands
//                    out slots from here to `end`, the first never used one marked state 0xFF
//   end              the pool's end (its last slot, not handed out); spare..end is the spare count
//                    n2 less one: 255 on every level but Drek's fleet (399)
//   pvars            the spawned mobys' pvars, 0x80 bytes for each slot from spare on
//
// These are four globals of the level's program (discs.h: dm_disc.pool: base, spare at +4, end
// at +8, pvars at +0x10 in level 0's; each relocated on its own for the level loaded). What it
// reads and writes is game memory through `mem` (the game address a is mem + a), so that the tests
// can lay a pool out in an array.
#ifndef DIFFICULTY_POOL_H
#define DIFFICULTY_POOL_H

#include <cstdint>
#include <vector>

namespace dm {

constexpr std::uint32_t k_moby_size = 0x100;
constexpr std::uint32_t k_spare_pvars = 0x80;

// The four globals' addresses.
struct PoolGlobals {
    std::uint32_t base, spare, end, pvars;
};

struct Pool {
    std::uint32_t base = 0, spare = 0, end = 0, pvars = 0;
    static Pool read(const std::uint8_t* mem, const PoolGlobals& at);
    // The slots CreateMoby may hand out, spare to end.
    int usable() const;
    bool sane() const;
};

// The free slots: from spare, each slot free (state 0xFE or 0xFF) counted, and every slot from the
// never-used mark on.
int count_free(const std::uint8_t* mem, const Pool& pool);

// Takes the pool's last `slots` slots out of CreateMoby's reach (its end moved down that far), so
// that their pvars, together with the end slot's, are room of the mod's own for pvars larger than
// 0x80: (slots + 1) * 0x80 bytes from *room. Only while none of them was ever used (the mark is
// before them); false, and nothing changed, otherwise.
bool reserve_tail(
    std::uint8_t* mem,
    const PoolGlobals& at,
    int slots,
    std::uint32_t* room,
    std::uint32_t* room_size
);

// How many copies each enemy gets within a budget of slots: round by round, a copy for each
// enemy that wants another, while its cost (one slot, and pvar_slots for pvars over 0x80) fits.
struct CloneAsk {
    int want;     // copies asked for
    int pvslots;  // extra slots each copy's pvars take
};

struct ClonePlan {
    std::vector<int> copies;  // copies given, per ask
    int slots = 0;            // slots taken by the copies themselves
    int pvar_slots = 0;       // slots reserved for their pvars
};

ClonePlan plan_clones(const std::vector<CloneAsk>& asks, int budget);

}  // namespace dm

#endif  // DIFFICULTY_POOL_H
