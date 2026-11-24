/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "discs.h"
#include "rando.h"
static uint8_t memory[0x200000];
static const rando_disc* s_disc;
static int s_by_item[RANDO_ITEM_COUNT];
static int active, level, mode, rank, finish, awards, calls, stored;
static uint32_t s_menu_checks;
static int s_menu_save;
static int expected_zoom, expected_holo;

static uint8_t* byte_at(uint32_t at) {
    return memory + at;
}

static uint8_t* owned(int item) {
    return byte_at(s_disc->owned + item);
}

static int get32(const uint8_t* at) {
    int value;
    memcpy(&value, at, 4);
    return value;
}

static void put32(uint32_t at, int value) {
    memcpy(byte_at(at), &value, 4);
}

static void lap(int value) {
    int16_t v = (int16_t)value;
    memcpy(byte_at(s_disc->hero + 0x89A), &v, 2);
}

static uint8_t* menu_owned(int special) {
    assert(special == SPECIAL_ZOOMERATOR);
    return byte_at(!strcmp(s_disc->version, "pal") ? 0x13D490 : 0x13D388);
}

static int current_level(void) {
    return level;
}

static int kerwan_randomizer_active(void) {
    return active;
}

static int menu_mode(void) {
    return mode;
}

static int menu_location_row(int special) {
    assert(special == SPECIAL_ZOOMERATOR);
    return 0;
}

static void menu_refresh(void) {}

static int had(int row) {
    assert(row == 0 || row == 1);
    return row ? *byte_at(s_disc->obtained + ITEM_HOLOGUISE) : (s_menu_checks & 1);
}

static void menu_store(void) {
    ++stored;
}

static void menu_collected(int special) {
    assert(special == 0 && had(0));
    ++awards;
}

static void item_give(int item, int equip) {
    assert(item == ITEM_HOLOGUISE && equip == 0);
    *byte_at(s_disc->obtained + ITEM_HOLOGUISE) = 1;
    ++awards;
}

#include "hoverboard_races.inc"

/* Inventory must remain real during the race; native tricks use the Zoomerator. */
static void native_player(void) {
    ++calls;
    assert(*menu_owned(0) == expected_zoom && *owned(ITEM_HOLOGUISE) == expected_holo);
    if (finish) {
        lap(3);
        put32(s_disc->hero + 0x8C0, rank);
    }
}

static void reset(void) {
    memset(memory, 0, sizeof memory);
    active = mode = s_menu_save = rank = expected_zoom = expected_holo = 1;
    finish = awards = calls = stored = 0;
    s_menu_checks = 0;
    s_by_item[ITEM_HOLOGUISE] = 1;
    *menu_owned(0) = *owned(ITEM_HOLOGUISE) = 1;
    put32(s_disc->hero + 0x208C, 0x16);
    put32(s_disc->hero + 0x86C, 0x1000);
    lap(2);
}

int main(void) {
    for (int disc = 0; disc < 2; ++disc) {
        s_disc = &k_rando_discs[disc];
        for (int race = 0; race < 2; ++race) {
            reset();
            level = race ? LEVEL_KALEBO : LEVEL_RILGAR;
            hoverboard_race_player(native_player);
            assert(!awards); /* not finished */
            rank = 2;
            finish = 1;
            hoverboard_race_player(native_player);
            assert(!awards && !had(race));
            lap(2);
            rank = 1;
            hoverboard_race_player(native_player);
            assert(awards == 1 && had(race));
            hoverboard_race_player(native_player);
            assert(awards == 1); /* same finish */
            lap(2);
            hoverboard_race_player(native_player);
            assert(awards == 1); /* replay */
            /* Persisted completion survives a new runtime without inventory changes. */
            reset();
            if (race) {
                *byte_at(s_disc->obtained + ITEM_HOLOGUISE) = 1;
            } else {
                s_menu_checks = 1;
            }
            finish = 1;
            hoverboard_race_player(native_player);
            assert(!awards);
            reset();
            finish = 1;
            *menu_owned(0) = *owned(ITEM_HOLOGUISE) = 0;
            expected_zoom = expected_holo = 0;
            hoverboard_race_player(native_player);
            assert(!awards); /* original handoff still runs when prize was not owned */
            reset();
            finish = 1;
            lap(3);
            hoverboard_race_player(native_player);
            assert(!awards); /* stale finish data is not a new victory */
            reset();
            active = 0;
            finish = 1;
            hoverboard_race_player(native_player);
            assert(!awards && calls == 1);
            reset();
            finish = 1;
            if (race) {
                s_by_item[ITEM_HOLOGUISE] = -1;
            } else {
                mode = 0;
            }
            hoverboard_race_player(native_player);
            assert(!awards);
            reset();
            finish = 1;
            put32(s_disc->hero + 0x208C, 0);
            hoverboard_race_player(native_player);
            assert(!awards); /* not racing */
            reset();
            finish = 1;
            put32(s_disc->hero + 0x86C, 0);
            hoverboard_race_player(native_player);
            assert(!awards); /* no board */
        }
        reset();
        level = LEVEL_NOVALIS;
        finish = 1;
        hoverboard_race_player(native_player);
        assert(!awards);
        reset();
        level = LEVEL_RILGAR;
        s_menu_save = 0;
        finish = 1;
        hoverboard_race_player(native_player);
        assert(!awards);
    }
    puts("Both race wins award independently of prior prizes without hiding inventory (PAL/NTSC)");
    return 0;
}
