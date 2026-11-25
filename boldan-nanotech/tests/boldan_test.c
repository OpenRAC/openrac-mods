/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The mod (src/mod.c) against a host table and game memory of the test's own, on each of Going
 * Commando's discs: a Boldan moby array laid at that disc's g_mobyTableBase, with the clock (its
 * pvars naming its two hands and the Nanotech Upgrade), as UpdateBoldanClock leaves them. With the
 * clock's hook point, the upgrade the clock's own update hid on an odd hour is shown again once it
 * returns, and nothing else is taken; on an even hour or once collected it is left as it is.
 * Without the hook point, it is shown again after the clock's update (the gate) and on the tick;
 * on an even hour, once collected, off Boldan, without the clock's hands or at another disc's
 * address it is left as it is. A game or disc the mod does not know is refused, and with the gate
 * taken by another mod the tick still does it.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../src/mod.c"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define FIRST 0x00800000u   /* the moby array */
#define COUNT 8u
#define CLOCK 2u            /* its slots */
#define MINUTE 3u
#define HOUR 4u
#define UPGRADE 6u
#define CLASS_DATA 0x00900000u
#define COLLISION 0x00C0111Du
#define VARS 0x00A00000u
#define SLOT_BIAS 100       /* moby_slot_of's slots: the test's own numbering */

static uint8_t* s_memory;
static char s_failed[256];
static int s_lines, s_level, s_gate_result, s_hook_result, s_updates;
static void* s_clock_hook;
static openrac_frame_fn s_tick;
static openrac_moby_update_fn s_gate;

static void host_log(openrac_mod* m, int level, const char* text) {
    (void)m;
    ++s_lines;
    printf("[%d] boldan-nanotech: %s\n", level, text);
}
static void host_fail(openrac_mod* m, const char* reason) {
    (void)m;
    snprintf(s_failed, sizeof s_failed, "%s", reason);
}
static int host_on_frame(openrac_mod* m, openrac_frame_fn fn, void* user) {
    (void)m, (void)user;
    s_tick = fn;
    return OPENRAC_HOOK_OK;
}
static int host_on_moby_update(openrac_mod* m, openrac_moby_update_fn fn, void* user) {
    (void)m, (void)user;
    if (s_gate_result == OPENRAC_HOOK_OK) {
        s_gate = fn;
    }
    return s_gate_result;
}
/* The game's clock update, as the hook's original: it hides the upgrade (the test's odd hours). */
static void game_clock_update(uint32_t clock);
static int host_hook(openrac_mod* m, const char* name, const char* signature, void* replacement,
                     void** original) {
    (void)m;
    CHECK(strcmp(name, "rac2.moby.boldan_clock.update") == 0);
    CHECK(strcmp(signature, "void(u32)") == 0);
    if (s_hook_result == OPENRAC_HOOK_OK) {
        s_clock_hook = replacement;
        *original = (void*)game_clock_update;
    }
    return s_hook_result;
}
static int host_moby_slot_of(openrac_gaddr moby) {
    return moby >= FIRST && moby < FIRST + COUNT * BN_MOBY_SIZE
               ? (int)((moby - FIRST) / BN_MOBY_SIZE) + SLOT_BIAS
               : -1;
}
static openrac_gaddr identity(openrac_gaddr a) {
    return a;
}
static int host_loaded_level(void) {
    return s_level;
}

static openrac_api make_api(const char* game, const char* version) {
    openrac_api api;
    memset(&api, 0, sizeof api);
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = game;
    api.version = version;
    api.memory = s_memory;
    api.log = host_log;
    api.fail = host_fail;
    api.hook = host_hook;
    api.on_frame = host_on_frame;
    api.on_moby_update = host_on_moby_update;
    api.moby_slot_of = host_moby_slot_of;
    api.relocate_data = identity;
    api.loaded_level = host_loaded_level;
    return api;
}

static uint32_t moby(uint32_t slot) {
    return FIRST + slot * BN_MOBY_SIZE;
}
static uint16_t flags(void) {
    return bn_u16(s_memory, moby(UPGRADE) + BN_MOBY_FLAGS);
}
static uint32_t collision(void) {
    return bn_u32(s_memory, moby(UPGRADE) + BN_MOBY_COLLISION);
}
static int shown(void) {
    return (flags() & 0x43u) == 0 && (flags() & 0x1000u) && collision() == COLLISION;
}
/* As the clock hides it on an odd hour (flags |= 0x43, 0x1000 cleared, no collision). */
static void hide(void) {
    bn_put_u16(s_memory, moby(UPGRADE) + BN_MOBY_FLAGS,
               (uint16_t)((flags() & 0xEFFFu) | 0x43u));
    bn_put_u32(s_memory, moby(UPGRADE) + BN_MOBY_COLLISION, 0);
}
static void set_hour(int32_t hour) {
    memcpy(s_memory + VARS + BN_CLOCK_HOUR, &hour, 4);
}

static void game_clock_update(uint32_t clock) {
    CHECK(clock == moby(CLOCK));
    ++s_updates;
    hide();
}
static void run_clock(uint32_t clock) {
    ((void (*)(uint32_t))s_clock_hook)(clock);
}

/* Boldan's mobys at the moby table `table`, the upgrade hidden at 3 o'clock. */
static void lay_boldan(uint32_t table) {
    bn_put_u32(s_memory, table, FIRST);
    bn_put_u32(s_memory, table + 8u, FIRST + COUNT * BN_MOBY_SIZE);
    for (uint32_t i = 0; i < COUNT; ++i) {
        bn_put_u16(s_memory, moby(i) + BN_MOBY_CLASS, (uint16_t)(0x100 + i)); /* other classes */
        s_memory[moby(i) + BN_MOBY_STATE] = 1;
    }
    bn_put_u16(s_memory, moby(CLOCK) + BN_MOBY_CLASS, 0x0E9E);
    bn_put_u32(s_memory, moby(CLOCK) + BN_MOBY_PVARS, VARS);
    bn_put_u16(s_memory, moby(MINUTE) + BN_MOBY_CLASS, BN_CLASS_MINUTE_HAND);
    bn_put_u16(s_memory, moby(HOUR) + BN_MOBY_CLASS, BN_CLASS_HOUR_HAND);
    bn_put_u16(s_memory, moby(UPGRADE) + BN_MOBY_CLASS, BN_CLASS_UPGRADE);
    bn_put_u32(s_memory, moby(UPGRADE) + BN_MOBY_CLASS_DATA, CLASS_DATA);
    bn_put_u32(s_memory, CLASS_DATA + 0x10u, COLLISION);
    bn_put_u16(s_memory, moby(UPGRADE) + BN_MOBY_FLAGS, 0x0400u | 0x1000u);
    bn_put_u32(s_memory, moby(UPGRADE) + BN_MOBY_COLLISION, COLLISION);
    bn_put_u32(s_memory, VARS + BN_CLOCK_MINUTE_HAND, moby(MINUTE));
    bn_put_u32(s_memory, VARS + BN_CLOCK_HOUR_HAND, moby(HOUR));
    bn_put_u32(s_memory, VARS + BN_CLOCK_UPGRADE, UPGRADE);
    set_hour(3);
    hide();
}

static void load_hooked(const char* game, const char* version, int hook_result, int gate_result,
                       int expect);
static void load(const char* game, const char* version, int gate_result, int expect) {
    load_hooked(game, version, OPENRAC_HOOK_UNKNOWN, gate_result, expect);
}
static void load_hooked(const char* game, const char* version, int hook_result, int gate_result,
                       int expect) {
    openrac_mod* m = (openrac_mod*)1;
    openrac_api api = make_api(game, version);
    static openrac_api kept;
    kept = api;
    s_failed[0] = 0;
    s_tick = NULL;
    s_gate = NULL;
    s_gate_result = gate_result;
    s_hook_result = hook_result;
    s_clock_hook = NULL;
    CHECK(openrac_mod_load(&kept, m) == expect);
    CHECK((s_failed[0] != 0) == (expect != 0));
}

/* With the clock's hook point: shown once the game's update returns; no tick, no gate. */
static void play_hooked(const bn_disc* d) {
    memset(s_memory, 0, BN_RAM);
    s_lines = 0;
    load_hooked("rac2", d->version, OPENRAC_HOOK_OK, OPENRAC_HOOK_OK, 0);
    CHECK(s_clock_hook != NULL && s_tick == NULL && s_gate == NULL && s_lines == 0);
    lay_boldan(d->moby_table);
    s_level = BN_LEVEL_BOLDAN;

    /* 3 o'clock, then 13:00 (1 on the dial): the game's update hid it, the mod shows it. */
    s_updates = 0;
    run_clock(moby(CLOCK));
    CHECK(s_updates == 1 && shown() && (flags() & 0x0400u));
    set_hour(13);
    run_clock(moby(CLOCK));
    CHECK(s_updates == 2 && shown());

    /* An even hour is the game's own: left as its update leaves it. */
    for (int32_t h = 0; h < 24; h += 2) {
        set_hour(h);
        run_clock(moby(CLOCK));
        CHECK(!shown());
    }

    /* Collected: left alone. */
    set_hour(5);
    s_memory[moby(UPGRADE) + BN_MOBY_STATE] = 2;
    run_clock(moby(CLOCK));
    CHECK(!shown());
    s_memory[moby(UPGRADE) + BN_MOBY_STATE] = 1;
    run_clock(moby(CLOCK));
    CHECK(shown());
    printf("%s: hooked, shown at odd hours, left alone otherwise\n", d->version);
}

static void play(const bn_disc* d) {
    memset(s_memory, 0, BN_RAM);
    load("rac2", d->version, OPENRAC_HOOK_OK, 0);
    CHECK(s_tick != NULL && s_gate != NULL);
    lay_boldan(d->moby_table);

    /* Off Boldan: nothing. */
    s_level = 12;
    s_tick(NULL);
    CHECK(!shown());

    /* On Boldan at 3 o'clock: shown on the tick, the clock said once. */
    s_level = BN_LEVEL_BOLDAN;
    s_lines = 0;
    s_tick(NULL);
    CHECK(shown() && (flags() & 0x0400u) && s_lines == 1);
    s_tick(NULL);
    CHECK(shown() && s_lines == 1);

    /* The clock hides it again a second on: shown right after its update (the gate, under the
     * slot moby_slot_of gives), not after another's or before. */
    hide();
    s_gate(NULL, (int)UPGRADE + SLOT_BIAS, OPENRAC_MOBY_AFTER);
    s_gate(NULL, (int)CLOCK + SLOT_BIAS, OPENRAC_MOBY_BEFORE);
    CHECK(!shown());
    s_gate(NULL, (int)CLOCK + SLOT_BIAS, OPENRAC_MOBY_AFTER);
    CHECK(shown());

    /* 13:00 is 1 on the dial: odd. */
    set_hour(13);
    hide();
    s_tick(NULL);
    CHECK(shown());

    /* An even hour is the game's own (it shows the upgrade itself): left as it is. */
    for (int32_t h = 0; h <= 24; h += 2) {
        set_hour(h == 24 ? 14 : h);
        hide();
        s_tick(NULL);
        CHECK(!shown());
    }

    /* Collected (state 2, then removed): left as it is. */
    set_hour(5);
    s_memory[moby(UPGRADE) + BN_MOBY_STATE] = 2;
    s_tick(NULL);
    CHECK(!shown());
    s_memory[moby(UPGRADE) + BN_MOBY_STATE] = 1;

    /* No minute hand: not Boldan's clock. */
    bn_put_u16(s_memory, moby(MINUTE) + BN_MOBY_CLASS, 0x0123);
    s_tick(NULL);
    CHECK(!shown());
    bn_put_u16(s_memory, moby(MINUTE) + BN_MOBY_CLASS, BN_CLASS_MINUTE_HAND);
    s_tick(NULL);
    CHECK(shown());

    /* Laid at another disc's address only: not seen. */
    for (size_t i = 0; i < sizeof k_bn_discs / sizeof k_bn_discs[0]; ++i) {
        if (k_bn_discs[i].moby_table != d->moby_table) {
            memset(s_memory, 0, BN_RAM);
            lay_boldan(k_bn_discs[i].moby_table);
            s_tick(NULL);
            CHECK(!shown());
        }
    }

    /* The gate another mod's: the tick still shows it. */
    memset(s_memory, 0, BN_RAM);
    s_lines = 0;
    load("rac2", d->version, OPENRAC_HOOK_TAKEN, 0);
    CHECK(s_gate == NULL && s_lines == 2);
    lay_boldan(d->moby_table);
    s_tick(NULL);
    CHECK(shown());
    printf("%s: shown at odd hours, left alone otherwise\n", d->version);
}

int main(void) {
    s_memory = calloc(1, BN_RAM);
    CHECK(s_memory != NULL);
    for (size_t i = 0; i < sizeof k_bn_discs / sizeof k_bn_discs[0]; ++i) {
        play(&k_bn_discs[i]);
        play_hooked(&k_bn_discs[i]);
    }
    load("rac2", "jp", OPENRAC_HOOK_OK, 1);
    load("rac1", "ntsc", OPENRAC_HOOK_OK, 1);
    puts("boldan-nanotech: every disc");
    free(s_memory);
    return 0;
}
