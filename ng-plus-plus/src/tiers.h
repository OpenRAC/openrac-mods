/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * NG++: the rules, apart from the game (README.md). Each weapon keeps its last real version
 * in the game; the mod adds steps on top of it, counted by its own level per weapon (0: none yet).
 * A step is bought at the vendor or reached by experience (the game's own, which keeps adding up
 * at a weapon's last version: the item's s32 >> 5, as the game compares it). Buy, then level: the
 * tiers alternate, as the game's Mega purchase is followed by its V4 by experience.
 *
 * Nothing here reads or writes game memory, so the tests (tests/tiers_test.c) run it alone.
 */
#ifndef WEAPON_TIERS_TIERS_H
#define WEAPON_TIERS_TIERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The items of Going Commando: a byte per item number in the game's tables. */
#define WT_ITEMS 56

/* The most steps a weapon has: the RYNO II's two of its own and the eight tiers. */
#define WT_MAX_STEPS 10

/* The eight tiers every weapon gets, V5 to V12. */
#define WT_TIERS 8

/* How a weapon reached its last real version, which decides how its first step is unlocked. */
typedef enum wt_kind {
    WT_KIND_NONE = 0, /* not a weapon the mod knows (gadgets, the wrench) */
    WT_KIND_FOUR,     /* four versions, V4 by experience: Alpha is for sale at V4 */
    WT_KIND_TWO,      /* two versions, the second bought in challenge mode (Bomb Glove, ...) */
    WT_KIND_ZAPPER,   /* the Clank Zapper: V2 by experience */
    WT_KIND_SHEEP,    /* the Sheepinator: V4 by experience, no Mega: Alpha is for sale at V4 */
    WT_KIND_SINGLE,   /* one version, bought (the Zodiac) */
    WT_KIND_RYNO,     /* the RYNO II: Heavy by experience and Ultima bought first */
} wt_kind;

/* How a step is reached. */
typedef enum wt_how {
    WT_BUY = 0, /* bought at the vendor */
    WT_EXP = 1, /* reached by experience */
} wt_how;

/* The tints a step can have. */
typedef enum wt_tint {
    WT_TINT_LIME = 0,
    WT_TINT_CYAN,
    WT_TINT_EMERALD,
    WT_TINT_TEAL,
    WT_TINT_VIOLET,
    WT_TINT_MAGENTA,
    WT_TINT_CRIMSON,
    WT_TINT_PRISMATIC, /* cycles through the seven above */
    WT_TINT_ORANGE,    /* RYNO II Heavy */
    WT_TINT_ULTIMA,    /* RYNO II Ultima: black and red */
} wt_tint;

/* One step: its name after the weapon's ("Alpha"), how it is reached, what it costs or needs,
 * and what it gives (the damage and the most ammo of the weapon's last real version, times). */
typedef struct wt_step {
    const char* name;
    wt_how how;
    int32_t price; /* unused: a bought step's price is wt_price's */
    int32_t exp;   /* experience (>> 5) since the last step, for a step by experience */
    float damage;  /* times the last real version's */
    float ammo;    /* times the last real version's most ammo */
    wt_tint tint;
} wt_step;

/* The experience (>> 5) a weapon needs at a bought version before the next purchase opens, where
 * the game has no experience step of its own there (the two-version weapons' Mega, the Zodiac,
 * the RYNO II Ultima, and each bought tier). */
#define WT_MASTERY_EXP 6000

/* The RYNO II's experience goals (its steps by experience and its mastery) are this many times the
 * others': it levels slower. */
#define WT_RYNO_EXP_SCALE 5

/* The kind of item `item`, WT_KIND_NONE for one the mod leaves alone. */
wt_kind wt_kind_of(int item);

/* How many steps a weapon of `kind` has (0 for none). */
int wt_step_count(wt_kind kind);

/* The step at `level` (0 the first) for a weapon of `kind`, or null past its last. */
const wt_step* wt_step_at(wt_kind kind, int level);

/* The version whose row has the weapon's Mega price (+0x80 of its row; the vendor's own Mega
 * upgrade), counted from its first: 2 for the four-version weapons, 1 for the two-version ones, 0
 * for the Zodiac and the RYNO II (their one version); -1 for none (the Zapper, the Sheepinator). */
int wt_mega_version(wt_kind kind);

/* With no Mega price, the bought steps go from this; a Mega below the floor counts as the floor. */
#define WT_PRICE_LADDER 1500000
#define WT_PRICE_FLOOR 150000

/* The price of the step at `level` when it is bought (0 for one by experience), from the weapon's
 * Mega price `mega` (0 for none): the base (`mega`, at least WT_PRICE_FLOOR; WT_PRICE_LADDER with
 * none) times 1.5, 2.5, 4, 6 (and 9, the RYNO II's fifth), rounded, each strictly above the one
 * before and above `mega`. */
int32_t wt_price(wt_kind kind, int level, int32_t mega);

/* Whether the step at `level` is for sale now: the weapon at its last real version (`at_last`), the
 * step bought, and the version before it mastered (reached by experience, or `exp` at least
 * WT_MASTERY_EXP after a purchase). */
int wt_can_buy(wt_kind kind, int level, int at_last, int32_t exp);

/* Whether the step at `level` is reached by `exp` now (the weapon at its last real version). */
int wt_exp_reached(wt_kind kind, int level, int at_last, int32_t exp);

/* The experience (>> 5) the weapon's bar fills toward at `level` (its last real version): the step's
 * own for one by experience, WT_MASTERY_EXP for a bought one after a bought one, 0 for none (the
 * next is for sale at once, or no step is left: the game's full bar). */
int32_t wt_exp_goal(wt_kind kind, int level);

/* The damage and the most ammo at `level` (0: the game's own, 1.0). */
float wt_damage(wt_kind kind, int level);
float wt_ammo(wt_kind kind, int level);

/* The most ammo at `level` from the last real version's `base` (0 stays 0: endless). */
int32_t wt_ammo_max(wt_kind kind, int level, int32_t base);

/* The tint at `level`, 0x00BBGGRR, at game tick `tick` (the animated ones move with it); -1 for
 * none (level 0). */
int32_t wt_tint_rgb(wt_kind kind, int level, uint32_t tick);

/* "<step> <base>" into `out` ("Alpha Lancer", `base` the first version's name), or `base` at
 * level 0. */
void wt_display_name(char* out, int size, const char* base, wt_kind kind, int level);

/* ---- Enemies across new games ---- */

/* How much tougher the enemies are in challenge mode cycle `cycle` (the save's counter: 0 the first
 * game, 1 the first challenge mode, which the game balances itself): their health and the damage
 * they deal, times. 1.0 up to cycle 1; past it a curve in step with the tiers a player has by then
 * (README.md). `strength` scales what is added past 1.0 (1.0 the curve as it is, 0 none). */
float wt_enemy_health(int cycle, float strength);
float wt_enemy_damage(int cycle, float strength);

/* ---- The levels kept per save ---- */

/* A save's clock (the save's chunk 4, which the game stamps when it saves) and the levels it had
 * then. */
typedef struct wt_record {
    uint8_t clock[8];
    uint8_t level[WT_ITEMS];
} wt_record;

/* The records the mod keeps, newest last. */
#define WT_RECORDS 64

typedef struct wt_book {
    int count;
    wt_record records[WT_RECORDS];
} wt_book;

/* The record with `clock`, or null. */
const wt_record* wt_book_find(const wt_book* book, const uint8_t clock[8]);

/* Keeps `levels` under `clock`, replacing its record or dropping the oldest when full. */
void wt_book_put(wt_book* book, const uint8_t clock[8], const uint8_t levels[WT_ITEMS]);

/* The book as text (a line a record: 16 hex digits of clock, a space, 112 of levels), and back.
 * wt_book_write returns the bytes written (out may be null to ask); wt_book_read the records read
 * (lines it cannot read are skipped). */
int wt_book_write(const wt_book* book, char* out, int size);
int wt_book_read(wt_book* book, const char* text);

#ifdef __cplusplus
}
#endif

#endif /* WEAPON_TIERS_TIERS_H */
