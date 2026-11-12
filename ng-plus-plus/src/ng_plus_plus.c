/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * NG++ for Going Commando (README.md): the game keeps its own versions; the mod adds the
 * tiers of src/tiers.c on top of each weapon's last real version and keeps its own level per
 * weapon. Where it touches the game:
 *
 *   for sale       Vendor_BuildList (func_002F6608 on NTSC-U v1.01): an upgrade entry added for
 *                  each weapon whose next step is for sale; Vendor_Price (func_002F6510) prices it;
 *                  Item_Upgrade (func_002887B0), which the vendor's purchase calls for an upgrade
 *                  entry once it is paid, takes the step
 *   experience     each tick: a weapon at its last version whose experience (which the game keeps
 *                  adding up there) reaches its step's takes it; the experience starts again from 0,
 *                  as the game's Mega purchase does
 *   damage         Moby_PostHit (func_002A8E20): a hit on anyone but the hero, while the hero holds
 *                  a weapon with tiers, has its damage scaled for the call
 *   most ammo      the row of the weapon's last version (+0x8E), rewritten each tick from what the
 *                  game (or another mod) has there
 *   tint           Item_Colors (func_00336F20): the quick select's and the weapons screen's
 *   name           a line at the top of the picture when a step is taken or a tiered weapon taken
 *                  in hand: "<the game's name of its last version> <tier>"
 *   kept           a file in the mod's data folder (never the memory card's): the levels at each
 *                  save, by the save's clock (chunk 4, stamped when the game saves). A clock seen
 *                  that the file has is a save loaded; one it has not is a save just made; all
 *                  zeros is a new game
 *   enemies        past challenge mode's first cycle (the save's counter, +0x12 of its block), the
 *                  enemies' health (as the damage they take, in Moby_PostHit, divided) and the
 *                  damage they deal (Hero_TakeDamage, func_L00_002A4EF8) times a curve in step
 *                  with the tiers (src/tiers.c)
 *   party          the host publishes its levels (party_rules_publish); a guest plays with them
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "discs.h"
#include "hooks.h"
#include "openrac/openrac_mod.h"
#include "tiers.h"

/* Ticks a notice stays up. */
#define WT_NOTICE_TICKS 150
/* The file the levels are kept in, in the mod's data folder. */
#define WT_FILE "tiers.txt"

static const openrac_api* s_api;
static openrac_mod* s_mod;
static const wt_disc* s_disc;
static int s_notices;

/* The levels of this game, and the host's when a party gave them. */
static uint8_t s_level[WT_ITEMS];
static uint8_t s_party[WT_ITEMS];
static int s_party_on;

/* The book of saves, and the clock seen last (s_clock_seen 0: none yet). */
static wt_book s_book;
static uint8_t s_clock[8];
static int s_clock_seen;

/* The most ammo of each last version as found, and as the mod wrote it (0: not written). */
static int32_t s_ammo_base[WT_ITEMS];
static int32_t s_ammo_written[WT_ITEMS];

/* The entries the mod added to the vendor's list: their place and item. */
static int s_entry_first;
static int s_entry_count;
static int s_entry_item[WT_ITEMS];

static uint32_t s_tick;
static int s_notice_left;
static int s_held = -1;
static int s_published;

static wt_Vendor_BuildList s_build_list;
static wt_Vendor_Price s_price;
static wt_Item_Upgrade s_upgrade;
static wt_Item_Colors s_colors;
static wt_Moby_PostHit s_post_hit;
static int32_t (*s_text)(int32_t id);
static wt_Hero_TakeDamage s_take_damage;
static int s_enemies;           /* the "enemy_scaling" setting */
static float s_enemy_strength;  /* the "enemy_strength" setting */
static float s_damage_carry;    /* the fraction of a point carried to the next hit */

/* ---- Game memory ---- */

static uint8_t get_u8(openrac_gaddr a) {
    return s_api->memory[a];
}
static uint16_t get_u16(openrac_gaddr a) {
    uint16_t v;
    memcpy(&v, s_api->memory + a, 2);
    return v;
}
static void put_u16(openrac_gaddr a, uint16_t v) {
    memcpy(s_api->memory + a, &v, 2);
}
static int32_t get_s32(openrac_gaddr a) {
    int32_t v;
    memcpy(&v, s_api->memory + a, 4);
    return v;
}
static void put_s32(openrac_gaddr a, int32_t v) {
    memcpy(s_api->memory + a, &v, 4);
}
static float get_f32(openrac_gaddr a) {
    float v;
    memcpy(&v, s_api->memory + a, 4);
    return v;
}
static void put_f32(openrac_gaddr a, float v) {
    memcpy(s_api->memory + a, &v, 4);
}
static openrac_gaddr at(uint32_t address) {
    return s_api->relocate_data(address);
}

/* Item `item`'s row now. */
static openrac_gaddr row_of(int item) {
    const uint8_t r = get_u8(at(s_disc->slot) + (uint32_t)item);
    return at(s_disc->weapon_table) + (uint32_t)r * WT_ROW_SIZE;
}

/* Whether item `item` is owned and at its last real version. */
static int owned(int item) {
    return get_u8(at(s_disc->owned) + (uint32_t)item) != 0;
}
static int at_last(int item) {
    return get_u16(row_of(item) + WT_ROW_NEXT) == 0;
}
static int32_t exp_of(int item) {
    return get_s32(at(s_disc->exp) + 4u * (uint32_t)item) >> 5;
}

/* The levels the game plays with: the party host's when there are some. */
static const uint8_t* levels(void) {
    return s_party_on ? s_party : s_level;
}

/* ---- Names and notices ---- */

/* "<the game's name of its last version> <tier>" for item `item`, into `out`. */
static void name_of(int item, char* out, int size) {
    char base[48] = "";

    if (s_text != NULL) {
        const int32_t text = s_text((int16_t)get_u16(row_of(item) + WT_ROW_NAME));
        if (text > 0 && (uint32_t)text < 0x02000000u - sizeof base) {
            for (size_t i = 0; i + 1 < sizeof base; ++i) {
                const char c = (char)get_u8((openrac_gaddr)text + (uint32_t)i);
                base[i] = c;
                base[i + 1] = 0;
                if (c == 0) {
                    break;
                }
            }
        }
    }
    wt_display_name(out, size, base[0] != 0 ? base : "Weapon", wt_kind_of(item), levels()[item]);
}

static void notice(int item, const char* prefix) {
    char name[96];
    char line[128];

    if (!s_notices) {
        return;
    }
    name_of(item, name, sizeof name);
    snprintf(line, sizeof line, "%s%s", prefix, name);
    s_api->play_notice(s_mod, line);
    s_notice_left = WT_NOTICE_TICKS;
}

/* ---- Steps ---- */

/* Item `item` takes its next step: its experience starts again and its ammo is filled to the new
 * most. */
static void step(int item) {
    ++s_level[item];
    put_s32(at(s_disc->exp) + 4u * (uint32_t)item, 0);
    {
        const openrac_gaddr row = row_of(item);
        const int32_t base = s_ammo_base[item] > 0 ? s_ammo_base[item]
                                                   : (int32_t)get_u16(row + WT_ROW_AMMO);
        const int32_t most = wt_ammo_max(wt_kind_of(item), s_level[item], base);
        if (most > 0) {
            put_s32(at(s_disc->ammo) + 4u * (uint32_t)item, most);
        }
    }
    notice(item, "");
    {
        char line[64];
        snprintf(line, sizeof line, "item %d: level %d", item, s_level[item]);
        s_api->log(s_mod, OPENRAC_LOG_INFO, line);
    }
}

/* Whether item `item`'s next step is for sale now. */
static int for_sale(int item) {
    const wt_kind kind = wt_kind_of(item);
    return !s_party_on && kind != WT_KIND_NONE && owned(item) && at_last(item)
           && wt_can_buy(kind, s_level[item], 1, exp_of(item));
}

/* The mod's entry under the vendor's cursor: its item, or -1. */
static int entry_under_cursor(void) {
    const openrac_gaddr ui = at(s_disc->vendor);
    const int cursor = get_s32(ui + WT_VENDOR_CURSOR);
    const int k = cursor - s_entry_first;

    if (k < 0 || k >= s_entry_count) {
        return -1;
    }
    const openrac_gaddr e = ui + WT_VENDOR_ENTRIES + (uint32_t)cursor * WT_VENDOR_ENTRY_SIZE;
    const int item = get_s32(e + WT_ENTRY_ITEM);
    if (item != s_entry_item[k] || get_s32(e + WT_ENTRY_UPGRADE) != 1) {
        return -1;
    }
    return item;
}

/* ---- Hooks ---- */

static void build_list(int32_t hide_unowned) {
    s_build_list(hide_unowned);

    const openrac_gaddr ui = at(s_disc->vendor);
    int count = get_s32(ui + WT_VENDOR_COUNT);
    s_entry_first = count;
    s_entry_count = 0;
    if (count < 0 || count > WT_VENDOR_MAX_ENTRIES) {
        return;
    }
    for (int item = 0; item < WT_ITEMS && count < WT_VENDOR_MAX_ENTRIES; ++item) {
        if (!for_sale(item)) {
            continue;
        }
        const openrac_gaddr e = ui + WT_VENDOR_ENTRIES + (uint32_t)count * WT_VENDOR_ENTRY_SIZE;
        put_s32(e + WT_ENTRY_ITEM, item);
        put_s32(e + WT_ENTRY_KIND, 0);
        put_s32(e + WT_ENTRY_MARKED, 0);
        put_s32(e + WT_ENTRY_PRICE, WT_PRICE_TEXT);
        put_s32(e + WT_ENTRY_CLASS, get_s32(row_of(item) + WT_ROW_CLASS));
        put_s32(e + WT_ENTRY_UPGRADE, 1);
        s_entry_item[s_entry_count++] = item;
        ++count;
    }
    put_s32(ui + WT_VENDOR_COUNT, count);
}

static int32_t price(void) {
    const int item = entry_under_cursor();

    if (item >= 0 && for_sale(item)) {
        return wt_step_at(wt_kind_of(item), s_level[item])->price;
    }
    return s_price();
}

static int32_t upgrade(int32_t id) {
    // The vendor's purchase of the mod's entry, paid: the step.
    if (id >= 0 && id < WT_ITEMS && entry_under_cursor() == id && for_sale(id)) {
        step(id);
        return 1;
    }
    return s_upgrade(id);
}

static void colors(int32_t id, uint32_t first, uint32_t second, uint32_t tint) {
    s_colors(id, first, second, tint);

    if (id < 0 || id >= WT_ITEMS || tint == 0) {
        return;
    }
    const int32_t rgb = wt_tint_rgb(wt_kind_of(id), levels()[id], s_tick);
    if (rgb >= 0) {
        const uint32_t old = (uint32_t)get_s32(tint);
        put_s32(tint, (int32_t)((old & 0xFF000000u) | (uint32_t)rgb));
    }
}

/* The challenge mode cycle of the game in progress. */
static int cycle(void) {
    return (int16_t)get_u16(at(s_disc->progress) + WT_PROGRESS_CYCLE);
}

static void post_hit(uint32_t victim, uint32_t request) {
    const openrac_gaddr hero = at(s_disc->hero);
    const int held = get_s32(hero + WT_HERO_HELD);
    const uint32_t me = (uint32_t)get_s32(hero + WT_HERO_MOBY);
    float k = 1.0f;

    if (victim != me && request != 0) {
        if (held >= 0 && held < WT_ITEMS && levels()[held] != 0) {
            k *= wt_damage(wt_kind_of(held), levels()[held]);
        }
        // Tougher enemies: their health times the cycle's, as the damage they take divided by it.
        if (s_enemies) {
            k /= wt_enemy_health(cycle(), s_enemy_strength);
        }
    }
    if (k == 1.0f) {
        s_post_hit(victim, request);
        return;
    }
    const float damage = get_f32(request + WT_HIT_DAMAGE);
    put_f32(request + WT_HIT_DAMAGE, damage * k);
    s_post_hit(victim, request);
    // The caller may post the same request to the next victim.
    put_f32(request + WT_HIT_DAMAGE, damage);
}

/* A hit on the hero: its points times the cycle's, the fraction carried over to the next hit. */
static void take_damage(int32_t amount) {
    const float k = s_enemies ? wt_enemy_damage(cycle(), s_enemy_strength) : 1.0f;

    if (amount <= 0 || k == 1.0f) {
        s_take_damage(amount);
        return;
    }
    const float exact = (float)amount * k + s_damage_carry;
    const int32_t points = (int32_t)exact;
    s_damage_carry = exact - (float)points;
    s_take_damage(points);
}

/* ---- The file ---- */

static void file_path(char* out, size_t size) {
    const char* dir = s_api->data_dir(s_mod);
    snprintf(out, size, "%s/%s", dir != NULL ? dir : ".", WT_FILE);
}

static void book_load(void) {
    char path[1024];
    file_path(path, sizeof path);
    s_book.count = 0;

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    static char text[WT_RECORDS * 160 + 1];
    const size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    wt_book_read(&s_book, text);
}

static void book_save(void) {
    char path[1024];
    char temp[1040];
    static char text[WT_RECORDS * 160 + 1];

    if (s_api->data_dir(s_mod) == NULL) {
        return;
    }
    file_path(path, sizeof path);
    snprintf(temp, sizeof temp, "%s.new", path);
    const int n = wt_book_write(&s_book, text, (int)sizeof text);
    FILE* f = fopen(temp, "wb");
    if (f == NULL) {
        return;
    }
    const int ok = fwrite(text, 1, (size_t)n, f) == (size_t)n;
    if (fclose(f) != 0 || !ok) {
        remove(temp);
        return;
    }
    remove(path);
    rename(temp, path);
}

/* The save's clock moved: a save loaded (the book has it), a save made (it has not), or a new game
 * (zeros). */
static void clock_moved(const uint8_t clock[8]) {
    static const uint8_t zero[8];
    const wt_record* r = wt_book_find(&s_book, clock);

    if (memcmp(clock, zero, 8) == 0) {
        memset(s_level, 0, sizeof s_level);
    } else if (r != NULL) {
        memcpy(s_level, r->level, WT_ITEMS);
    } else {
        wt_book_put(&s_book, clock, s_level);
        book_save();
    }
}

/* ---- The tick ---- */

/* The most ammo of each weapon with a level: its last version's row, from what is there. */
static void write_ammo(void) {
    for (int item = 0; item < WT_ITEMS; ++item) {
        const wt_kind kind = wt_kind_of(item);
        if (kind == WT_KIND_NONE || !at_last(item)) {
            continue;
        }
        const openrac_gaddr a = row_of(item) + WT_ROW_AMMO;
        const int32_t now = get_u16(a);

        // Not what the mod wrote: the game's (or another mod's) own, which the level brought.
        if (now != s_ammo_written[item]) {
            s_ammo_base[item] = now;
        }
        const int32_t most = wt_ammo_max(kind, levels()[item], s_ammo_base[item]);
        if (most != now) {
            put_u16(a, (uint16_t)most);
        }
        s_ammo_written[item] = most;
    }
}

static void publish(void) {
    if (s_party_on || s_published) {
        return;
    }
    int tiered = 0;
    for (int i = 0; i < WT_ITEMS; ++i) {
        tiered += s_level[i] != 0;
    }
    char text[64];
    snprintf(text, sizeof text, "NG++: %d weapons", tiered);
    s_api->party_rules_publish(s_mod, s_level, WT_ITEMS, text);
    s_published = 1;
}

static void tick(void* user) {
    (void)user;
    ++s_tick;

    if (s_notice_left > 0 && --s_notice_left == 0) {
        s_api->play_notice(s_mod, "");
    }
    if (s_api->loaded_level() < 0) {
        return;
    }

    // The save's clock: a load, a save or a new game.
    {
        uint8_t clock[8];
        memcpy(clock, s_api->memory + at(s_disc->clock), 8);
        if (!s_clock_seen || memcmp(clock, s_clock, 8) != 0) {
            memcpy(s_clock, clock, 8);
            s_clock_seen = 1;
            clock_moved(clock);
            s_published = 0;
        }
    }

    // Steps by experience (not for a guest: the host's levels come with its rules).
    if (!s_party_on) {
        for (int item = 0; item < WT_ITEMS; ++item) {
            const wt_kind kind = wt_kind_of(item);
            if (kind != WT_KIND_NONE && owned(item) && at_last(item)
                && wt_exp_reached(kind, s_level[item], 1, exp_of(item))) {
                step(item);
                s_published = 0;
            }
        }
    }
    write_ammo();
    publish();

    // A tiered weapon taken in hand: its name.
    {
        const int held = get_s32(at(s_disc->hero) + WT_HERO_HELD);
        if (held != s_held) {
            s_held = held;
            // Not over a step's notice still up.
            if (held >= 0 && held < WT_ITEMS && levels()[held] != 0 && s_notice_left == 0) {
                notice(held, "");
            }
        }
    }
}

static void party_rules(void* user, const void* data, uint32_t size) {
    (void)user;
    if (data == NULL || size < WT_ITEMS) {
        s_party_on = 0;
        return;
    }
    memcpy(s_party, data, WT_ITEMS);
    for (int i = 0; i < WT_ITEMS; ++i) {
        const int most = wt_step_count(wt_kind_of(i));
        if (s_party[i] > most) {
            s_party[i] = (uint8_t)most;
        }
    }
    s_party_on = 1;
}

/* ---- Entry point ---- */

static int take(const char* id, const char* sig, void* replacement, void** original) {
    const int r = s_api->hook(s_mod, id, sig, replacement, original);
    if (r != OPENRAC_HOOK_OK) {
        char reason[160];
        snprintf(reason, sizeof reason, "NG++ needs the hook point %s (OpenRAC with SDK 1.14.6)", id);
        s_api->fail(s_mod, reason);
        return 1;
    }
    return 0;
}

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    memset(s_level, 0, sizeof s_level);
    memset(s_ammo_written, 0, sizeof s_ammo_written);
    memset(s_ammo_base, 0, sizeof s_ammo_base);
    s_party_on = 0;
    s_clock_seen = 0;
    s_entry_count = 0;
    s_tick = 0;
    s_notice_left = 0;
    s_held = -1;
    s_published = 0;

    if (strcmp(api->game, "rac2") != 0) {
        api->fail(mod, "NG++ is for Going Commando (rac2)");
        return 1;
    }
    s_disc = wt_disc_of(api->version);
    if (s_disc == NULL) {
        char reason[128];
        snprintf(reason, sizeof reason, "NG++ knows NTSC-U, PAL and NTSC-U v2.00, not %s", api->version);
        api->fail(mod, reason);
        return 1;
    }
    s_notices = api->setting_bool(mod, "notices");
    s_enemies = api->setting_bool(mod, "enemy_scaling");
    s_enemy_strength = (float)api->setting_float(mod, "enemy_strength");
    s_damage_carry = 0.0f;
    s_text = (int32_t(*)(int32_t))api->function(s_disc->text);

    if (take(WT_HOOK_Vendor_BuildList, WT_SIG_Vendor_BuildList, (void*)build_list, (void**)&s_build_list)
        || take(WT_HOOK_Vendor_Price, WT_SIG_Vendor_Price, (void*)price, (void**)&s_price)
        || take(WT_HOOK_Item_Upgrade, WT_SIG_Item_Upgrade, (void*)upgrade, (void**)&s_upgrade)
        || take(WT_HOOK_Item_Colors, WT_SIG_Item_Colors, (void*)colors, (void**)&s_colors)
        || take(WT_HOOK_Moby_PostHit, WT_SIG_Moby_PostHit, (void*)post_hit, (void**)&s_post_hit)
        || take(WT_HOOK_Hero_TakeDamage, WT_SIG_Hero_TakeDamage, (void*)take_damage, (void**)&s_take_damage)) {
        return 1;
    }
    if (api->on_frame(mod, tick, NULL) != 0) {
        api->fail(mod, "no tick callback in this OpenRAC");
        return 1;
    }
    // A party's rules: optional (a game with no party never calls it).
    api->on_party_rules(mod, party_rules, NULL);
    book_load();
    return 0;
}
