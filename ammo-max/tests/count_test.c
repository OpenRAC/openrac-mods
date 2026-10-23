/* SPDX-License-Identifier: GPL-3.0-or-later
 * The count the vendor's buy dialog starts at (ammo_max.h): the rounds the weapon has room for,
 * or as many as the bolts pay for, whichever is fewer; none when no round can be bought.
 *
 * Expected counts are worked out by hand from that rule.
 */
#include <stdio.h>
#include <stdlib.h>

#include "ammo_max.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(void) {
    /* 0 of 160, bolts for all of it: 160. Bolts for 40: 40. */
    CHECK(ammo_max_count(1600, 10, 0, 160) == 160);
    CHECK(ammo_max_count(400, 10, 0, 160) == 40);
    CHECK(ammo_max_count(409, 10, 0, 160) == 40);
    /* Rich: the room the weapon has. */
    CHECK(ammo_max_count(9999999, 1, 150, 160) == 10);
    CHECK(ammo_max_count(9999999, 1, 159, 160) == 1);
    /* A round's price exactly. */
    CHECK(ammo_max_count(25, 25, 3, 8) == 1);
    /* Nothing to buy: full, short of a round, or free. */
    CHECK(ammo_max_count(1000, 10, 160, 160) == 0);
    CHECK(ammo_max_count(1000, 10, 170, 160) == 0);
    CHECK(ammo_max_count(9, 10, 0, 160) == 0);
    CHECK(ammo_max_count(1000, 0, 0, 160) == 0);
    printf("ammo_max_count_test: ok\n");
    return 0;
}
