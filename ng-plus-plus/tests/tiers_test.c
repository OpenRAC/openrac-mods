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
    CHECK(wt_kind_of(18) == WT_KIND_TWO);   /* Tesla Claw: its beam hits */
    CHECK(wt_kind_of(17) == WT_KIND_NO_EXP); /* Decoy Glove: deals no damage */
    CHECK(wt_kind_of(31) == WT_KIND_FOUR);  /* Synthenoid: its kills count, as the game's V2 step */
    CHECK(wt_kind_of(32) == WT_KIND_FOUR);  /* Spiderbot Glove */
    CHECK(wt_step_count(WT_KIND_NO_EXP) == 8);
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

/* The Decoy Glove never earns experience: every tier bought, none waiting on experience, the bar
 * full; the prices on the others' ladder with the tiers between in between. */
static void no_experience(void) {
    int32_t last = 150000;
    for (int level = 0; level < 8; ++level) {
        const wt_step* s = wt_step_at(WT_KIND_NO_EXP, level);
        CHECK(s != NULL && s->how == WT_BUY);
        CHECK(strcmp(s->name, wt_step_at(WT_KIND_FOUR, level)->name) == 0);
        CHECK(wt_damage(WT_KIND_NO_EXP, level + 1) == wt_damage(WT_KIND_FOUR, level + 1));
        CHECK(wt_can_buy(WT_KIND_NO_EXP, level, 1, 0)); /* at once, no experience */
        CHECK(!wt_can_buy(WT_KIND_NO_EXP, level, 0, 0));
        CHECK(!wt_exp_reached(WT_KIND_NO_EXP, level, 1, 1000000));
        CHECK(wt_exp_goal(WT_KIND_NO_EXP, level) == 0);
        const int32_t p = wt_price(WT_KIND_NO_EXP, level, 150000);
        CHECK(p > last);
        last = p;
    }
    CHECK(wt_step_at(WT_KIND_NO_EXP, 8) == NULL);
    CHECK(wt_mega_version(WT_KIND_NO_EXP) == 1);
    /* Alpha, Gamma, Epsilon and Eta as the others'; Omega at 9 times. */
    for (int level = 0; level < 8; level += 2) {
        CHECK(wt_price(WT_KIND_NO_EXP, level, 150000) == wt_price(WT_KIND_FOUR, level, 150000));
    }
    CHECK(wt_price(WT_KIND_NO_EXP, 1, 150000) == 300000);
    CHECK(wt_price(WT_KIND_NO_EXP, 3, 150000) == 490000);
    CHECK(wt_price(WT_KIND_NO_EXP, 5, 150000) == 750000);
    CHECK(wt_price(WT_KIND_NO_EXP, 7, 150000) == 1350000);
    CHECK(wt_price(WT_KIND_NO_EXP, 0, 1000) == 225000); /* the floor */
}

/* The tiers take the name of the version before the game's bought top. */
static void naming(void) {
    CHECK(wt_name_version(WT_KIND_FOUR) == 1);   /* Meteor Gun, not Lava Gun nor its Mega */
    CHECK(wt_name_version(WT_KIND_TWO) == 0);    /* Bomb Glove, not its Mega */
    CHECK(wt_name_version(WT_KIND_NO_EXP) == 0); /* Decoy Glove */
    CHECK(wt_name_version(WT_KIND_ZAPPER) == 1); /* Clank Shocker */
    CHECK(wt_name_version(WT_KIND_SHEEP) == 1);  /* Black Sheepinator */
    CHECK(wt_name_version(WT_KIND_SINGLE) == 0);
    CHECK(wt_name_version(WT_KIND_RYNO) == 0);
    CHECK(wt_name_version(WT_KIND_NONE) == -1);
    for (int kind = WT_KIND_FOUR; kind <= WT_KIND_RYNO; ++kind) {
        const int mega = wt_mega_version((wt_kind)kind);
        CHECK(mega < 0 || wt_name_version((wt_kind)kind) <= mega);
    }
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
    CHECK(!wt_exp_reached(WT_KIND_RYNO, 0, 1, 29999));
    CHECK(wt_exp_reached(WT_KIND_RYNO, 0, 1, 30000));
    CHECK(!wt_can_buy(WT_KIND_RYNO, 0, 1, 1000000));
    CHECK(wt_can_buy(WT_KIND_RYNO, 1, 1, 0));  /* Ultima: Heavy reached */
    CHECK(!wt_can_buy(WT_KIND_RYNO, 2, 1, 0)); /* Alpha: Ultima must be mastered */
    CHECK(!wt_can_buy(WT_KIND_RYNO, 2, 1, WT_MASTERY_EXP));
    CHECK(wt_can_buy(WT_KIND_RYNO, 2, 1, 30000));
    /* The bar's goal: Heavy's experience, none for Ultima (for sale at once), Ultima's mastery for
     * Alpha, a step's own by experience, none past the last. */
    CHECK(wt_exp_goal(WT_KIND_RYNO, 0) == 30000);
    CHECK(wt_exp_goal(WT_KIND_RYNO, 1) == 0);
    CHECK(wt_exp_goal(WT_KIND_RYNO, 2) == 30000);
    /* Beta, Delta, Zeta and Omega: five times the others'. */
    CHECK(wt_exp_goal(WT_KIND_RYNO, 3) == 40000);
    CHECK(wt_exp_goal(WT_KIND_RYNO, 5) == 60000);
    CHECK(wt_exp_goal(WT_KIND_RYNO, 7) == 80000);
    CHECK(wt_exp_goal(WT_KIND_RYNO, 9) == 120000);
    CHECK(!wt_exp_reached(WT_KIND_RYNO, 3, 1, 39999));
    CHECK(wt_exp_reached(WT_KIND_RYNO, 3, 1, 40000));
    CHECK(wt_exp_goal(WT_KIND_FOUR, 0) == 0);
    CHECK(wt_exp_goal(WT_KIND_FOUR, 1) == wt_step_at(WT_KIND_FOUR, 1)->exp);
    CHECK(wt_exp_goal(WT_KIND_FOUR, 2) == 0);
    CHECK(wt_exp_goal(WT_KIND_TWO, 0) == WT_MASTERY_EXP);
    CHECK(wt_exp_goal(WT_KIND_RYNO, wt_step_count(WT_KIND_RYNO)) == 0);
    CHECK(wt_exp_goal(WT_KIND_NONE, 0) == 0);
    CHECK(wt_damage(WT_KIND_RYNO, 2) > wt_damage(WT_KIND_RYNO, 1));
    CHECK(wt_damage(WT_KIND_RYNO, 3) > wt_damage(WT_KIND_RYNO, 2));
    wt_display_name(name, sizeof name, "RYNO II", WT_KIND_RYNO, 1);
    CHECK(strcmp(name, "Heavy RYNO II") == 0);
    wt_display_name(name, sizeof name, "RYNO II", WT_KIND_RYNO, 2);
    CHECK(strcmp(name, "Ultima RYNO II") == 0);
    wt_display_name(name, sizeof name, "RYNO II", WT_KIND_RYNO, 10);
    CHECK(strcmp(name, "Omega RYNO II") == 0);
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
    wt_display_name(name, sizeof name, "Lancer", WT_KIND_FOUR, 1);
    CHECK(strcmp(name, "Alpha Lancer") == 0);
    wt_display_name(name, sizeof name, "Lancer", WT_KIND_FOUR, 0);
    CHECK(strcmp(name, "Lancer") == 0);
}

static void enemies(void) {
    CHECK(wt_enemy_health(0, 1.0f) == 1.0f);
    CHECK(wt_enemy_health(1, 1.0f) == 1.0f);
    CHECK(wt_enemy_health(2, 1.0f) == 2.5f);
    CHECK(wt_enemy_health(3, 1.0f) == 4.0f);
    CHECK(wt_enemy_health(4, 1.0f) == 6.0f);
    CHECK(wt_enemy_health(5, 1.0f) == 8.0f);
    CHECK(wt_enemy_health(7, 1.0f) == 12.0f);
    CHECK(wt_enemy_damage(1, 1.0f) == 1.0f);
    CHECK(wt_enemy_damage(2, 1.0f) == 1.25f);
    CHECK(wt_enemy_damage(7, 1.0f) == 2.5f);
    CHECK(wt_enemy_health(2, 0.5f) == 1.75f);
    CHECK(wt_enemy_health(9, 0.0f) == 1.0f);
    /* Ahead of the tiers: from cycle 2, even a weapon at the tier a player can have by then
     * (cycle 2 Delta, 3 Zeta, 4 Omega) takes longer than V4 at cycle 1, and more so each cycle. */
    CHECK(wt_enemy_health(2, 1.0f) > wt_damage(WT_KIND_FOUR, 4));
    CHECK(wt_enemy_health(3, 1.0f) > wt_damage(WT_KIND_FOUR, 6));
    CHECK(wt_enemy_health(4, 1.0f) > wt_damage(WT_KIND_FOUR, 8));
    for (int cycle = 2; cycle < 12; ++cycle) {
        CHECK(wt_enemy_health(cycle + 1, 1.0f) > wt_enemy_health(cycle, 1.0f));
        CHECK(wt_enemy_health(cycle, 1.0f) > wt_enemy_damage(cycle, 1.0f));
    }
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

/* Zeros never over a record with levels; the latest is the last written with any level. */
static void book_keeps(void) {
    static wt_book a;
    uint8_t one[8] = {0, 1, 2, 3, 0, 12, 11, 0x26};
    uint8_t two[8] = {0, 9, 2, 3, 0, 12, 11, 0x26};
    uint8_t levels[WT_ITEMS] = {0};
    uint8_t none[WT_ITEMS] = {0};

    CHECK(wt_book_latest(&a) == NULL);
    levels[28] = 0x81;
    wt_book_put(&a, one, levels);
    wt_book_put(&a, one, none);
    CHECK(wt_book_find(&a, one)->level[28] == 0x81);
    wt_book_put(&a, two, none); /* a new clock with nothing */
    CHECK(wt_book_latest(&a) == wt_book_find(&a, one));
}

/* Which levels a save gets when its clock moves: only its own. */
static void saves(void) {
    static wt_book book;
    const wt_record* r;
    uint8_t a[8] = {0, 1, 2, 3, 0, 12, 11, 0x26}; /* save A, tiered */
    uint8_t a2[8] = {0, 5, 2, 3, 0, 12, 11, 0x26}; /* A stamped again */
    uint8_t b[8] = {0, 7, 9, 3, 0, 12, 11, 0x26}; /* save B, never tiered */
    uint8_t zero[8] = {0};
    uint8_t levels[WT_ITEMS] = {0};
    wt_print one, two;

    memset(&one, 0, sizeof one);
    one.bolts = 1000;
    one.exp[29] = 100 << 5;
    one.row[29] = 96;
    two = one;
    CHECK(wt_print_changes(&one, &two) == 0);
    two.exp[29] += 32; /* a kill in the tick */
    CHECK(wt_print_changes(&one, &two) == 1);
    two.bolts = 5;
    two.row[30] = 80;
    CHECK(wt_print_changes(&one, &two) == 3);

    levels[29] = 2 | 0x80;
    wt_book_put(&book, a, levels);
    /* A loaded (the title shows its clock): its record, from nothing or from another save. */
    CHECK(wt_clock_case_of(&book, a, 0, 99, 0, 99, 0, &r) == WT_CLOCK_RECORD);
    CHECK(r != NULL && r->level[29] == 0x82);
    CHECK(wt_clock_case_of(&book, a, 1, 99, 0, 99, 0, &r) == WT_CLOCK_RECORD);
    /* A stamped again as its level starts (or at a save): nothing else changed, its levels go
     * on. */
    CHECK(wt_clock_case_of(&book, a2, 1, 0, 0, 99, 0, &r) == WT_CLOCK_SAME && r == NULL);
    CHECK(wt_clock_case_of(&book, a2, 1, 1, 0, 99, 0, &r) == WT_CLOCK_SAME);
    /* B loaded while A was in play (the save's data replaced): none of A's, nothing started. */
    CHECK(wt_clock_case_of(&book, b, 1, WT_LOAD_CHANGES, 0, 99, 3, &r) == WT_CLOCK_NEW);
    /* B loaded from nothing (the title after zeros), even in challenge mode: none. */
    CHECK(wt_clock_case_of(&book, b, 0, 99, 1, 40, 3, &r) == WT_CLOCK_NEW);
    CHECK(wt_clock_case_of(&book, b, 0, 99, 0, 99, 3, &r) == WT_CLOCK_NEW);
    /* Challenge mode's next game: zeros, then a new clock with the weapons as they were. */
    CHECK(wt_clock_case_of(&book, zero, 1, 0, 0, 99, 1, &r) == WT_CLOCK_ZERO);
    CHECK(wt_clock_case_of(&book, b, 0, 99, 1, 0, 2, &r) == WT_CLOCK_CARRY);
    /* A new game (counter 0, weapons gone): none. */
    CHECK(wt_clock_case_of(&book, b, 0, 99, 1, 0, 0, &r) == WT_CLOCK_NEW);
    /* A, then B (never tiered: no record kept), then A again: A's. */
    CHECK(wt_book_find(&book, b) == NULL);
    CHECK(wt_clock_case_of(&book, a, 1, 50, 0, 99, 0, &r) == WT_CLOCK_RECORD);
    CHECK(r != NULL && r->level[29] == 0x82);
}

int main(void) {
    kinds();
    buy_then_level();
    mastery();
    no_experience();
    naming();
    sheepinator();
    prices();
    ryno();
    scaling();
    enemies();
    book();
    book_keeps();
    saves();
    if (s_failures == 0) {
        printf("ng_plus_plus_rules: ok\n");
    }
    return s_failures != 0;
}
