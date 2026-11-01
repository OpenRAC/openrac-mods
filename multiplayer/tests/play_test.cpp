// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket:
//   - a pose's bytes (play.hpp): 65 of them, read back as written, a broken one refused, a
//     non-finite float read as 0, sequences compared across the wrap; the pieces worn after them
//     and a SHOT, read back as written, an older sender's pose wearing nothing;
//   - the interpolation buffer: a steady stream played back a delay behind, smoothly and in order;
//     repeated and late poses dropped; a teleport put there at once; a stall then a burst caught
//     up without a jump and the cursor never past the newest pose by more than the extrapolation;
//     past the newest pose, on along its velocity for 150 ms, then held, and at once when it was
//     slowing down; a sender back after a silence with its count started again; poses recorded
//     from two games replayed (tests/data): the seqs shown in the sender's order, the place in a
//     seq running on without jumps, no stop overshot, repeated and reordered copies changing
//     nothing;
//   - the co-op (coop.hpp) with a host and a member, their sessions on recorders and a router that
//     does what the server does with DATA, and fake games: the host's Start sends its save in
//     pieces and the member starts it from the title; a new game; the member follows the host's
//     level; a member back in the title asks again (WANT) and gets the host's progress; one who
//     joins late gets it unasked; poses pass through the host to the others and become figures in
//     the same level only, which go with the level, the silence or the party.
#include "test_support.hpp"

int g_failures = 0;

int main() {
    mpt::run_poses_tests();
    mpt::run_coop_tests();
    mpt::run_world_tests();
    mpt::run_mods_tests();
    mpt::run_pause_tests();
    mpt::run_bolts_tests();
    mpt::run_scenes_tests();
    mpt::run_party_tests();
    mpt::run_sim_tests();

    if (g_failures == 0) {
        std::puts("play: all passed");
    }

    return g_failures == 0 ? 0 : 1;
}
