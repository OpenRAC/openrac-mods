// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The count the vendor's buy dialog starts at (ammo_max.hpp): the rounds the weapon has room for,
// or as many as the bolts pay for, whichever is fewer; none when no round can be bought.
//
// Expected counts are worked out by hand from that rule.

#include <cstdio>
#include <cstdlib>

#include "ammo_max.hpp"

// Stops the test at the first failed condition and says where. It is a macro for the line number
// and the text of the condition, which a function cannot see.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

// Checks that bolts for all the room, or for part of it, give the fewer of the two.
void test_the_count_is_the_fewer_of_the_bolts_and_the_room() {
    // 0 of 160, bolts for all of it: 160. Bolts for 40: 40.
    CHECK(ammo_max::start_count(1600, 10, 0, 160) == 160);
    CHECK(ammo_max::start_count(400, 10, 0, 160) == 40);
    CHECK(ammo_max::start_count(409, 10, 0, 160) == 40);

    // Rich: the room the weapon has.
    CHECK(ammo_max::start_count(9999999, 1, 150, 160) == 10);
    CHECK(ammo_max::start_count(9999999, 1, 159, 160) == 1);

    // A round's price exactly.
    CHECK(ammo_max::start_count(25, 25, 3, 8) == 1);
}

// Checks that a full weapon, too few bolts and a free round all leave the game's 1.
void test_nothing_to_buy_gives_no_count() {
    // Nothing to buy: full, short of a round, or free.
    CHECK(ammo_max::start_count(1000, 10, 160, 160) == 0);
    CHECK(ammo_max::start_count(1000, 10, 170, 160) == 0);
    CHECK(ammo_max::start_count(9, 10, 0, 160) == 0);
    CHECK(ammo_max::start_count(1000, 0, 0, 160) == 0);
}

}  // namespace

int main() {
    test_the_count_is_the_fewer_of_the_bolts_and_the_room();
    test_nothing_to_buy_gives_no_count();
    std::printf("ammo_max_count_test: ok\n");

    return 0;
}
