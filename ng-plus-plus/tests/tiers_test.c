/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * NG++: the rules alone (src/tiers.c).
 */
#include <stdio.h>
#include <string.h>

#include "tiers.h"

static int s_failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            ++s_failures;                                                                          \
        }                                                                                          \
    } while (0)

static void kinds(void) {
    CHECK(wt_kind_of(30) == WT_KIND_FOUR);  /* Lancer */
    CHECK(wt_kind_of(12) == WT_KIND_TWO);   /* Bomb Glove */
    CHECK(wt_kind_of(53) == WT_KIND_TWO);   /* Walloper */
    CHECK(wt_kind_of(9) == WT_KIND_ZAPPER); /* Clank Zapper */
    CHECK(wt_kind_of(16) == WT_KIND_SHEEP);
    CHECK(wt_kind_of(43) == WT_KIND_SINGLE); /* Zodiac */
    CHECK(wt_kind_of(44) == WT_KIND_RYNO);
    CHECK(wt_kind_of(13) == WT_KIND_NONE); /* Swingshot */
    CHECK(wt_kind_of(10) == WT_KIND_NONE); /* the wrench */
    CHECK(wt_step_count(WT_KIND_FOUR) == 8);
    CHECK(wt_step_count(WT_KIND_RYNO) == 10);
    CHECK(wt_step_count(WT_KIND_NONE) == 0);
}

/* A V4 weapon: Alpha for sale at once, Beta by experience, then Gamma for sale. */
static void buy_then_level(void) {
    CHECK(!wt_can_buy(WT_KIND_FOUR, 0, 0, 0)); /* not at its last version */
    CHECK(wt_can_buy(WT_KIND_FOUR, 0, 1, 0));
    CHECK(strcmp(wt_step_at(WT_KIND_FOUR, 0)->name, "Alpha") == 0);
    CHECK(!wt_exp_reached(WT_KIND_FOUR, 0, 1, 1000000)); /* Alpha is bought */
    CHECK(!wt_can_buy(WT_KIND_FOUR, 1, 1, 1000000));     /* Beta is not for sale */
    CHECK(!wt_exp_reached(WT_KIND_FOUR, 1, 1, 7999));
    CHECK(wt_exp_reached(WT_KIND_FOUR, 1, 1, 8000));
    CHECK(wt_can_buy(WT_KIND_FOUR, 2, 1, 0)); /* Gamma: Beta was reached */
    CHECK(strcmp(wt_step_at(WT_KIND_FOUR, 7)->name, "Omega") == 0);
    CHECK(wt_step_at(WT_KIND_FOUR, 8) == NULL);
    CHECK(!wt_exp_reached(WT_KIND_FOUR, 8, 1, 1000000));
}

/* Bought last versions need mastering first. */
static void mastery(void) {
    CHECK(!wt_can_buy(WT_KIND_TWO, 0, 1, WT_MASTERY_EXP - 1));
    CHECK(wt_can_buy(WT_KIND_TWO, 0, 1, WT_MASTERY_EXP));
    CHECK(!wt_can_buy(WT_KIND_SINGLE, 0, 1, 0));
    CHECK(wt_can_buy(WT_KIND_ZAPPER, 0, 1, 0)); /* V2 came by experience */
}

static void sheepinator(void) {
    /* No Mega, never sold by the game: bought and levelled as the others, from V4 on. */
    CHECK(wt_can_buy(WT_KIND_SHEEP, 0, 1, 0));
    CHECK(wt_step_at(WT_KIND_SHEEP, 1)->how == WT_EXP);
    CHECK(wt_mega_version(WT_KIND_SHEEP) < 0);
    CHECK(wt_price(WT_KIND_SHEEP, 0, 0) == 2250000);
    CHECK(wt_price(WT_KIND_SHEEP, 6, 0) == 9000000);
}

/* Prices: from the Mega, rising, each above the one before and the Mega. */
static void prices(void) {
    CHECK(wt_price(WT_KIND_FOUR, 0, 150000) == 225000);
    CHECK(wt_price(WT_KIND_FOUR, 1, 150000) == 0); /* Beta is by experience */
    CHECK(wt_price(WT_KIND_FOUR, 2, 150000) == 375000);
    CHECK(wt_price(WT_KIND_FOUR, 4, 150000) == 600000);
    CHECK(wt_price(WT_KIND_FOUR, 6, 150000) == 900000);
    CHECK(wt_price(WT_KIND_FOUR, 0, 375000) == 565000);
    CHECK(wt_price(WT_KIND_TWO, 0, 1000) == 225000); /* the floor */
    for (int mega = 1000; mega <= 2000000; mega += 7919) {
        int32_t last = mega;
        for (int level = 0; level < 8; ++level) {
            const int32_t p = wt_price(WT_KIND_FOUR, level, mega);
            if (p != 0) {
                CHECK(p > last);
                last = p;
            }
        }
    }
    CHECK(wt_price(WT_KIND_RYNO, 1, 1000000) == 1500000); /* Ultima */
    CHECK(wt_price(WT_KIND_RYNO, 8, 1000000) == 9000000); /* Eta */
}

static void ryno(void) {
    char name[64];
    CHECK(strcmp(wt_step_at(WT_KIND_RYNO, 0)->name, "Heavy") == 0);
    CHECK(wt_exp_reached(WT_KIND_RYNO, 0, 1, 6000));
    CHECK(!wt_can_buy(WT_KIND_RYNO, 0, 1, 1000000));
    CHECK(wt_can_buy(WT_KIND_RYNO, 1, 1, 0));  /* Ultima: Heavy reached */
    CHECK(!wt_can_buy(WT_KIND_RYNO, 2, 1, 0)); /* Alpha: Ultima must be mastered */
    CHECK(wt_can_buy(WT_KIND_RYNO, 2, 1, WT_MASTERY_EXP));
    /* The bar's goal: Heavy's experience, none for Ultima (for sale at once), Ultima's mastery for
     * Alpha, a step's own by experience, none past the last. */
    CHECK(wt_exp_goal(WT_KIND_RYNO, 0) == 6000);
    CHECK(wt_exp_goal(WT_KIND_RYNO, 1) == 0);
    CHECK(wt_exp_goal(WT_KIND_RYNO, 2) == WT_MASTERY_EXP);
    CHECK(wt_exp_goal(WT_KIND_FOUR, 0) == 0);
    CHECK(wt_exp_goal(WT_KIND_FOUR, 1) == wt_step_at(WT_KIND_FOUR, 1)->exp);
    CHECK(wt_exp_goal(WT_KIND_FOUR, 2) == 0);
    CHECK(wt_exp_goal(WT_KIND_TWO, 0) == WT_MASTERY_EXP);
    CHECK(wt_exp_goal(WT_KIND_RYNO, wt_step_count(WT_KIND_RYNO)) == 0);
    CHECK(wt_exp_goal(WT_KIND_NONE, 0) == 0);
    CHECK(wt_damage(WT_KIND_RYNO, 2) > wt_damage(WT_KIND_RYNO, 1));
    CHECK(wt_damage(WT_KIND_RYNO, 3) > wt_damage(WT_KIND_RYNO, 2));
    wt_display_name(name, sizeof name, "RYNO II", WT_KIND_RYNO, 1);
    CHECK(strcmp(name, "RYNO II Heavy") == 0);
    wt_display_name(name, sizeof name, "RYNO II", WT_KIND_RYNO, 2);
    CHECK(strcmp(name, "RYNO II Ultima") == 0);
    wt_display_name(name, sizeof name, "RYNO II", WT_KIND_RYNO, 10);
    CHECK(strcmp(name, "RYNO II Omega") == 0);
}

static void scaling(void) {
    char name[64];
    CHECK(wt_damage(WT_KIND_FOUR, 0) == 1.0f);
    for (int level = 1; level < 8; ++level) {
        CHECK(wt_damage(WT_KIND_FOUR, level + 1) > wt_damage(WT_KIND_FOUR, level));
        CHECK(wt_ammo(WT_KIND_FOUR, level + 1) > wt_ammo(WT_KIND_FOUR, level));
    }
    CHECK(wt_ammo_max(WT_KIND_FOUR, 0, 50) == 50);
    CHECK(wt_ammo_max(WT_KIND_FOUR, 1, 50) == 55);
    CHECK(wt_ammo_max(WT_KIND_FOUR, 8, 50) == 100);
    CHECK(wt_ammo_max(WT_KIND_SHEEP, 3, 0) == 0); /* endless stays endless */
    CHECK(wt_ammo_max(WT_KIND_FOUR, 8, 60000) == 0xFFFF);
    CHECK(wt_tint_rgb(WT_KIND_FOUR, 0, 0) == -1);
    CHECK(wt_tint_rgb(WT_KIND_FOUR, 1, 0) == 0x20FFB0);
    CHECK(wt_tint_rgb(WT_KIND_FOUR, 2, 0) == 0xFFE000);
    /* Omega moves; Alpha does not. */
    CHECK(wt_tint_rgb(WT_KIND_FOUR, 8, 0) != wt_tint_rgb(WT_KIND_FOUR, 8, 100));
    CHECK(wt_tint_rgb(WT_KIND_FOUR, 1, 0) == wt_tint_rgb(WT_KIND_FOUR, 1, 100));
    CHECK(wt_tint_rgb(WT_KIND_RYNO, 2, 0) != wt_tint_rgb(WT_KIND_RYNO, 2, 30));
    wt_display_name(name, sizeof name, "Heavy Lancer", WT_KIND_FOUR, 1);
    CHECK(strcmp(name, "Heavy Lancer Alpha") == 0);
    wt_display_name(name, sizeof name, "Heavy Lancer", WT_KIND_FOUR, 0);
    CHECK(strcmp(name, "Heavy Lancer") == 0);
}

static void enemies(void) {
    CHECK(wt_enemy_health(0, 1.0f) == 1.0f);
    CHECK(wt_enemy_health(1, 1.0f) == 1.0f);
    CHECK(wt_enemy_health(2, 1.0f) == 1.5f);
    CHECK(wt_enemy_health(5, 1.0f) == 4.0f);
    CHECK(wt_enemy_health(7, 1.0f) == 6.0f);
    CHECK(wt_enemy_damage(2, 1.0f) == 1.25f);
    CHECK(wt_enemy_damage(7, 1.0f) == 2.5f);
    CHECK(wt_enemy_health(3, 0.5f) == 1.5f);
    CHECK(wt_enemy_health(9, 0.0f) == 1.0f);
    /* In step: at the expected tier, the time to kill is cycle 1's. */
    CHECK(wt_damage(WT_KIND_FOUR, 2) == wt_enemy_health(2, 1.0f));
    CHECK(wt_damage(WT_KIND_FOUR, 8) == wt_enemy_health(5, 1.0f));
}

static void book(void) {
    static wt_book a, b;
    uint8_t clock[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t levels[WT_ITEMS] = {0};
    char text[WT_RECORDS * 160 + 1];

    levels[30] = 2;
    levels[44] = 10;
    a.count = 0;
    wt_book_put(&a, clock, levels);
    levels[30] = 3;
    wt_book_put(&a, clock, levels); /* replaces */
    CHECK(a.count == 1);
    CHECK(wt_book_find(&a, clock)->level[30] == 3);
    clock[0] = 9;
    CHECK(wt_book_find(&a, clock) == NULL);
    for (int i = 0; i < WT_RECORDS + 5; ++i) {
        clock[1] = (uint8_t)i;
        wt_book_put(&a, clock, levels);
    }
    CHECK(a.count == WT_RECORDS);
    const int n = wt_book_write(&a, text, (int)sizeof text);
    CHECK(n == wt_book_write(&a, NULL, 0));
    CHECK(wt_book_read(&b, text) == WT_RECORDS);
    CHECK(memcmp(&a.records, &b.records, sizeof a.records) == 0);
    CHECK(wt_book_read(&b, "nonsense\n0102\n") == 0);
}

int main(void) {
    kinds();
    buy_then_level();
    mastery();
    sheepinator();
    prices();
    ryno();
    scaling();
    enemies();
    book();
    if (s_failures == 0) {
        printf("ng_plus_plus_rules: ok\n");
    }
    return s_failures != 0;
}
