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
 *   experience bar the row of the weapon's last version (+0x6C, the experience to the next version,
 *                  which the game leaves at 0 there: a full bar) set each tick to the next step's
 *                  goal, so the weapons screen and the quick select fill toward it. The game's own
 *                  level check (func_L00_002D5EB8) never reads it at a last version (it skips a V4
 *                  row, a two-version weapon's second and a one-version weapon), and Weapon_LevelUp
 *                  (func_00287EB0) stops at a row with no next; the row is the game's static table,
 *                  never saved, and gets back what was there when no step is left
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
/* Whether the mod counts each weapon's experience yet: from the first tick it is seen at its last
 * version, when the game's own count (all it ever had with it) is set back to 0. Kept in the book
 * as the level's top bit (WT_STARTED). */
static uint8_t s_started[WT_ITEMS];
#define WT_STARTED 0x80u
static int s_party_on;

/* The book of saves, and the clock seen last (s_clock_seen 0: none yet). */
static wt_book s_book;
static uint8_t s_clock[8];
static int s_clock_seen;

/* The most ammo of each last version as found, and as the mod wrote it (0: not written). */
static int32_t s_ammo_base[WT_ITEMS];
static int32_t s_ammo_written[WT_ITEMS];
/* The same for the experience to the next version (+0x6C of the last version's row). */
static int32_t s_goal_base[WT_ITEMS];
static int32_t s_goal_written[WT_ITEMS];
static int s_goal_seen[WT_ITEMS];

/* The entries the mod added to the vendor's list: their place and item. */
static int s_entry_count;
static int s_entry_at[WT_ITEMS];
static int s_entry_item[WT_ITEMS];

/* The vendor's list being drawn: the icons asked for so far (-1: not drawing), and the entry the
 * last one was for (-1: none), whose label the game asks next. */
static int s_drawn = -1;
static int s_drawn_row = -1;

static uint32_t s_tick;
static int s_notice_left;
static int s_held = -1;
#define WT_TRACE_TICKS 1800 /* 30 s at 60 ticks a second */
static int s_trace;         /* ticks left of the vendor's state logged after a purchase */
static int32_t s_traced[5]; /* the vendor's state last logged */
static int s_published;

static wt_Vendor_BuildList s_build_list;
static wt_Vendor_Price s_price;
static wt_Item_Upgrade s_upgrade;
static wt_Item_Colors s_colors;
static wt_Vendor_DrawList s_draw_list;
static wt_Item_VersionColor s_version_color;
static wt_Moby_PostHit s_post_hit;
static int32_t (*s_text)(int32_t id);
static wt_Text_ById s_text_by_id;
static int s_in_text;     /* in a text lookup of the mod's own: the game's texts as they are */
static int s_text_logged; /* the first vendor text the mod gave, logged */
static wt_Hero_TakeDamage s_take_damage;
static int s_enemies;          /* the "enemy_scaling" setting */
static float s_enemy_strength; /* the "enemy_strength" setting */
static float s_damage_carry;   /* the fraction of a point carried to the next hit */

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

/* Whether `a` holds `size` bytes of game memory: main RAM, or the port's own region (0xE0000000,
 * 16 MB), where the port keeps the slots (its statics: negative as an i32). */
static int in_memory(uint32_t a, uint32_t size) {
    return (a != 0 && a <= 0x02000000u - size) || (a >= 0xE0000000u && a <= 0xE1000000u - size);
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

static int version_row(int item, int version);

/* "<tier> <the game's name of its first version>" for item `item` at `level`, into `out`: the tier
 * takes the place of the version ("Alpha Lancer", not "Heavy Lancer Alpha"). */
static void name_at(int item, int level, char* out, int size) {
    char base[48] = "";

    if (s_text != NULL) {
        const int first = version_row(item, 0);
        const openrac_gaddr named =
            first >= 0 ? at(s_disc->weapon_table) + (uint32_t)first * WT_ROW_SIZE : row_of(item);
        ++s_in_text;
        const int32_t text = s_text((int16_t)get_u16(named + WT_ROW_NAME));
        if (in_memory((uint32_t)text, sizeof base)) {
            for (size_t i = 0; i + 1 < sizeof base; ++i) {
                const char c = (char)get_u8((openrac_gaddr)text + (uint32_t)i);
                base[i] = c;
                base[i + 1] = 0;
                if (c == 0) {
                    break;
                }
            }
        }
        --s_in_text;
    }
    wt_display_name(out, size, base[0] != 0 ? base : "Weapon", wt_kind_of(item), level);
}

static void name_of(int item, char* out, int size) {
    name_at(item, levels()[item], out, size);
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
        const int32_t base =
            s_ammo_base[item] > 0 ? s_ammo_base[item] : (int32_t)get_u16(row + WT_ROW_AMMO);
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

/* Row `r`'s s16 at `field` (the links between an item's versions). */
static int link_of(int r, uint32_t field) {
    return (int16_t)get_u16(at(s_disc->weapon_table) + (uint32_t)r * WT_ROW_SIZE + field);
}

/* The row of item `item`'s version `version`, as the game finds it (func_00288870: from the item's
 * first row down its links), or -1 for none. */
static int version_row(int item, int version) {
    int r = item;
    for (int n = 0; n < 16 && link_of(r, WT_ROW_PARENT) > 0; ++n) {
        r = link_of(r, WT_ROW_PARENT);
    }
    for (int n = 0; n < version && link_of(r, WT_ROW_NEXT) > 0; ++n) {
        r = link_of(r, WT_ROW_NEXT);
    }
    const openrac_gaddr row = at(s_disc->weapon_table) + (uint32_t)r * WT_ROW_SIZE;
    return get_u8(row + WT_ROW_VERSION) == version ? r : -1;
}

/* Item `item`'s Mega price, 0 for none. */
static int32_t mega_price(int item) {
    const int version = wt_mega_version(wt_kind_of(item));
    const int r = version < 0 ? -1 : version_row(item, version);
    if (r < 0) {
        return 0;
    }
    const int32_t p = get_s32(at(s_disc->weapon_table) + (uint32_t)r * WT_ROW_SIZE + WT_ROW_PRICE);
    return p > 0 ? p : 0;
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

    for (int k = 0; k < s_entry_count; ++k) {
        if (s_entry_at[k] != cursor) {
            continue;
        }
        const openrac_gaddr e = ui + WT_VENDOR_ENTRIES + (uint32_t)cursor * WT_VENDOR_ENTRY_SIZE;
        const int item = get_s32(e + WT_ENTRY_ITEM);
        if (item != s_entry_item[k] || get_s32(e + WT_ENTRY_UPGRADE) != 1) {
            return -1;
        }
        return item;
    }
    return -1;
}

/* ---- Hooks ---- */

/* Entry `i` of the vendor's list. */
static openrac_gaddr entry(int i) {
    return at(s_disc->vendor) + WT_VENDOR_ENTRIES + (uint32_t)i * WT_VENDOR_ENTRY_SIZE;
}

static void build_list(int32_t hide_unowned) {
    s_build_list(hide_unowned);

    const openrac_gaddr ui = at(s_disc->vendor);
    int count = get_s32(ui + WT_VENDOR_COUNT);
    s_entry_count = 0;
    if (count < 0 || count > WT_VENDOR_MAX_ENTRIES) {
        return;
    }
    for (int item = 0; item < WT_ITEMS && count < WT_VENDOR_MAX_ENTRIES; ++item) {
        if (!for_sale(item)) {
            continue;
        }
        // Right after the weapon's own entry (the last of its item), else at the end.
        int place = count;
        for (int i = 0; i < count; ++i) {
            if (get_s32(entry(i) + WT_ENTRY_ITEM) == item) {
                place = i + 1;
            }
        }
        memmove(
            s_api->memory + entry(place + 1),
            s_api->memory + entry(place),
            (size_t)(count - place) * WT_VENDOR_ENTRY_SIZE
        );
        for (int k = 0; k < s_entry_count; ++k) {
            if (s_entry_at[k] >= place) {
                ++s_entry_at[k];
            }
        }
        // As the game makes its own Mega entry (func_002F6608).
        const openrac_gaddr e = entry(place);
        put_s32(e + WT_ENTRY_ITEM, item);
        put_s32(e + WT_ENTRY_KIND, 0);
        put_s32(e + WT_ENTRY_CLASS, get_s32(row_of(item) + WT_ROW_CLASS));
        put_s32(e + WT_ENTRY_PRICE, WT_PRICE_TEXT);
        put_s32(e + WT_ENTRY_MARKED, 0);
        put_s32(e + WT_ENTRY_UPGRADE, 1);
        s_entry_at[s_entry_count] = place;
        s_entry_item[s_entry_count] = item;
        ++s_entry_count;
        ++count;
    }
    put_s32(ui + WT_VENDOR_COUNT, count);
}

/* The vendor's list drawn: each icon's colour asked in the order of the rows drawn. */
static void draw_list(void) {
    s_drawn = 0;
    s_drawn_row = -1;
    s_draw_list();
    s_drawn = -1;
    s_drawn_row = -1;
}

/* The mod's entry at row `row` of the vendor's list: its item, or -1. */
static int entry_item_at(int row) {
    for (int k = 0; row >= 0 && k < s_entry_count; ++k) {
        if (s_entry_at[k] == row && get_s32(entry(row) + WT_ENTRY_ITEM) == s_entry_item[k]
            && get_s32(entry(row) + WT_ENTRY_UPGRADE) == 1) {
            return s_entry_item[k];
        }
    }
    return -1;
}

/* `text` into the port's slot `slot`: its game address, 0 when there is none. */
static uint32_t slot_text(int slot, const char* text) {
    const uint32_t a = s_text != NULL ? (uint32_t)s_text(WT_TEXT_SLOT + slot) : 0;
    if (!in_memory(a, WT_TEXT_SLOT_BYTES)) {
        return 0;
    }
    char* out = (char*)s_api->memory + a;
    snprintf(out, WT_TEXT_SLOT_BYTES, "%s", text);
    if (!s_text_logged) {
        char line[160];
        snprintf(line, sizeof line, "vendor text: \"%s\" in slot %d (0x%08X)", text, slot, a);
        s_api->log(s_mod, OPENRAC_LOG_INFO, line);
        s_text_logged = 1;
    }
    return a;
}

/* The vendor's texts for the mod's entries: the tier under the icon instead of "Mega", and the
 * weapon's name at that tier ("Lancer Alpha") instead of its Mega's in the dialog and preview. */
static uint32_t tier_text(int32_t id) {
    if (id == WT_TEXT_UPGRADE && s_drawn > 0) {
        const int item = entry_item_at(s_drawn_row);
        const wt_step* step = item >= 0 ? wt_step_at(wt_kind_of(item), levels()[item]) : NULL;
        return step != NULL ? slot_text(0, step->name) : 0;
    }
    const int item = entry_under_cursor();
    if (item < 0 || !for_sale(item)) {
        return 0;
    }
    // What the vendor names an upgrade entry by: its version 2's row (1 for the two-version weapons).
    // The RYNO II and the Zodiac have no such row: the preview's lookup (func_00288870) fails there
    // and the game names it by its text left from before, so the item's own row's text is taken
    // too (what the game names it by when the lookup is given a fallback).
    const wt_kind kind = wt_kind_of(item);
    const int r = version_row(item, kind == WT_KIND_TWO ? 1 : 2);
    const int32_t named =
        r >= 0 ? get_s32(at(s_disc->weapon_table) + (uint32_t)r * WT_ROW_SIZE + WT_ROW_TEXT) : -1;
    if (id != named && (r >= 0 || id != get_s32(row_of(item) + WT_ROW_TEXT))) {
        return 0;
    }
    char name[WT_TEXT_SLOT_BYTES];
    name_at(item, s_level[item] + 1, name, sizeof name);
    return slot_text(1, name);
}

/* The tiered name of the weapon whose last version's name is text `id` (the ammo pickup's message,
 * the weapons menu, the quick select, ...), or 0: only a weapon with a tier, and only when no other
 * item's row now names itself by `id`. The game prints a name at once (its text drawn or copied when
 * looked up), so the slots 2..7 are taken in turn: six names in a row before one is written over. */
static uint32_t weapon_name(int32_t id) {
    int found = -1;
    int found_row = -1;
    for (int item = 0; item < WT_ITEMS; ++item) {
        const int r = get_u8(at(s_disc->slot) + (uint32_t)item);
        const openrac_gaddr row = row_of(item);
        if (r == found_row
            || ((int16_t)get_u16(row + WT_ROW_NAME) != id && get_s32(row + WT_ROW_TEXT) != id)) {
            continue;
        }
        if (found >= 0) {
            return 0; /* shared by two rows: left as it is */
        }
        found = item;
        found_row = r;
    }
    if (found < 0 || wt_kind_of(found) == WT_KIND_NONE || levels()[found] == 0 || !owned(found)
        || !at_last(found)) {
        return 0;
    }
    static int next;
    char name[WT_TEXT_SLOT_BYTES];
    name_of(found, name, sizeof name);
    const uint32_t a = slot_text(2 + next, name);
    next = (next + 1) % 6;
    return a;
}

static int32_t text_by_id(int32_t id) {
    if (s_in_text == 0) {
        ++s_in_text;
        uint32_t text = tier_text(id);
        if (text == 0) {
            text = weapon_name(id);
        }
        --s_in_text;
        if (text != 0) {
            return (int32_t)text;
        }
    }
    return s_text_by_id(id);
}

static uint32_t version_color(int32_t kind) {
    const uint32_t color = s_version_color(kind);

    if (s_drawn < 0) {
        return color;
    }
    // The row this call is for: the s_drawn-th of -5..+6 around the cursor in the list.
    const openrac_gaddr ui = at(s_disc->vendor);
    const int cursor = get_s32(ui + WT_VENDOR_CURSOR);
    const int count = get_s32(ui + WT_VENDOR_COUNT);
    int row = -1;
    for (int i = -5, n = 0; i < 7; ++i) {
        const int idx = cursor + i;
        if (idx >= 0 && idx < count && n++ == s_drawn) {
            row = idx;
            break;
        }
    }
    ++s_drawn;
    s_drawn_row = row;
    for (int k = 0; row >= 0 && k < s_entry_count; ++k) {
        if (s_entry_at[k] == row && get_s32(entry(row) + WT_ENTRY_ITEM) == s_entry_item[k]
            && get_s32(entry(row) + WT_ENTRY_UPGRADE) == 1) {
            const int item = s_entry_item[k];
            // The tier for sale: the one after the weapon's level now.
            const int32_t rgb = wt_tint_rgb(wt_kind_of(item), levels()[item] + 1, s_tick);
            if (rgb >= 0) {
                return (color & 0xFF000000u) | (uint32_t)rgb;
            }
        }
    }
    // The weapon's own entry (its ammo): the tier it has now.
    if (row >= 0 && entry_item_at(row) < 0) {
        const int item = get_s32(entry(row) + WT_ENTRY_ITEM);
        if (item >= 0 && item < WT_ITEMS && wt_kind_of(item) != WT_KIND_NONE && levels()[item] != 0
            && owned(item) && at_last(item)) {
            const int32_t rgb = wt_tint_rgb(wt_kind_of(item), levels()[item], s_tick);
            if (rgb >= 0) {
                return (color & 0xFF000000u) | (uint32_t)rgb;
            }
        }
    }
    return color;
}

static int32_t price(void) {
    const int item = entry_under_cursor();

    if (item >= 0 && for_sale(item)) {
        return wt_price(wt_kind_of(item), s_level[item], mega_price(item));
    }
    return s_price();
}

static int32_t upgrade(int32_t id) {
    // The vendor's purchase of the mod's entry, paid: the step.
    const int under = entry_under_cursor();
    char line[128];
    if (id >= 0 && id < WT_ITEMS && under == id && for_sale(id)) {
        const int old = s_level[id];
        step(id);
        snprintf(line, sizeof line, "purchase: item %d, tier %d -> %d", id, old, s_level[id]);
        s_api->log(s_mod, OPENRAC_LOG_INFO, line);
        s_trace = WT_TRACE_TICKS;
        s_traced[0] = -2;
        return 1;
    }
    if (under >= 0 || (id >= 0 && id < WT_ITEMS && wt_kind_of(id) != WT_KIND_NONE)) {
        snprintf(
            line,
            sizeof line,
            "purchase: item %d left to the game (entry under the cursor %d, for sale %d)",
            id,
            under,
            id >= 0 && id < WT_ITEMS ? for_sale(id) : 0
        );
        s_api->log(s_mod, OPENRAC_LOG_INFO, line);
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
        memset(s_started, 0, sizeof s_started);
    } else if (r != NULL) {
        for (int i = 0; i < WT_ITEMS; ++i) {
            const int most = wt_step_count(wt_kind_of(i));
            const int level = r->level[i] & ~WT_STARTED;
            s_level[i] = (uint8_t)(level > most ? most : level);
            s_started[i] = (r->level[i] & WT_STARTED) != 0 || level != 0;
        }
    } else {
        uint8_t kept[WT_ITEMS];
        for (int i = 0; i < WT_ITEMS; ++i) {
            kept[i] = (uint8_t)(s_level[i] | (s_started[i] ? WT_STARTED : 0));
        }
        wt_book_put(&s_book, clock, kept);
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

/* The experience bar of each weapon at its last version: toward its next step's goal, or the
 * game's own (a full bar) when it has none. */
static void write_goal(void) {
    for (int item = 0; item < WT_ITEMS; ++item) {
        const wt_kind kind = wt_kind_of(item);
        if (kind == WT_KIND_NONE || !at_last(item)) {
            continue;
        }
        const openrac_gaddr a = row_of(item) + WT_ROW_EXP;
        const int32_t now = get_s32(a);

        // Not what the mod wrote: the game's (or a newly loaded level's copy's) own.
        if (!s_goal_seen[item] || now != s_goal_written[item]) {
            s_goal_base[item] = now;
            s_goal_seen[item] = 1;
        }
        const int32_t goal = wt_exp_goal(kind, levels()[item]);
        const int32_t want = goal > 0 ? goal : s_goal_base[item];
        if (want != now) {
            put_s32(a, want);
        }
        s_goal_written[item] = want;
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

/* For a while after a purchase, the vendor's list and preview state each time they change, into the
 * log: the cursor, the preview's row (+0x70), its model and resource flags (+0x88, +0x8C), and the
 * entry under the cursor. */
static void trace_vendor(void) {
    if (s_trace <= 0) {
        return;
    }
    --s_trace;
    const openrac_gaddr ui = at(s_disc->vendor);
    const int cursor = get_s32(ui + WT_VENDOR_CURSOR);
    const int count = get_s32(ui + WT_VENDOR_COUNT);
    const int32_t now[5] = {
        cursor,
        get_s32(ui + WT_VENDOR_PREVIEWED),
        get_s32(ui + WT_VENDOR_MODEL),
        get_s32(ui + WT_VENDOR_RESOURCE),
        count,
    };
    if (memcmp(now, s_traced, sizeof now) == 0) {
        return;
    }
    memcpy(s_traced, now, sizeof now);
    char line[200];
    int item = -1, kind = -1, cls = -1, up = -1;
    if (cursor >= 0 && cursor < count && count <= WT_VENDOR_MAX_ENTRIES) {
        item = get_s32(entry(cursor) + WT_ENTRY_ITEM);
        kind = get_s32(entry(cursor) + WT_ENTRY_KIND);
        cls = get_s32(entry(cursor) + WT_ENTRY_CLASS);
        up = get_s32(entry(cursor) + WT_ENTRY_UPGRADE);
    }
    snprintf(
        line,
        sizeof line,
        "vendor: cursor %d of %d, previewed %d, model %d, resource %d; entry item %d kind %d "
        "class %d upgrade %d",
        cursor,
        count,
        now[1],
        now[2],
        now[3],
        item,
        kind,
        cls,
        up
    );
    s_api->log(s_mod, OPENRAC_LOG_INFO, line);
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

    // Steps by experience (not for a guest: the host's levels come with its rules). The game's count
    // at a weapon's last version is everything it ever had with the weapon (from before the mod, and
    // from past games in challenge mode), so the mod's starts at 0 the first time it sees the weapon
    // there: a step by experience is earned from then on, never at once from a save's old count.
    if (!s_party_on) {
        for (int item = 0; item < WT_ITEMS; ++item) {
            const wt_kind kind = wt_kind_of(item);
            if (kind == WT_KIND_NONE || !owned(item) || !at_last(item)) {
                continue;
            }
            if (!s_started[item]) {
                s_started[item] = 1;
                if (s_level[item] == 0) {
                    put_s32(at(s_disc->exp) + 4u * (uint32_t)item, 0);
                }
                continue;
            }
            if (wt_exp_reached(kind, s_level[item], 1, exp_of(item))) {
                step(item);
                s_published = 0;
            }
        }
    }
    write_ammo();
    write_goal();
    publish();
    trace_vendor();

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
        snprintf(
            reason, sizeof reason, "NG++ needs the hook point %s (OpenRAC with SDK 1.14.6)", id
        );
        s_api->fail(s_mod, reason);
        return 1;
    }
    return 0;
}

OPENRAC_MOD_DEFINE("ng-plus-plus", "0.1.8")

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    memset(s_level, 0, sizeof s_level);
    memset(s_started, 0, sizeof s_started);
    s_in_text = 0;
    s_text_logged = 0;
    s_drawn_row = -1;
    memset(s_ammo_written, 0, sizeof s_ammo_written);
    memset(s_ammo_base, 0, sizeof s_ammo_base);
    memset(s_goal_base, 0, sizeof s_goal_base);
    memset(s_goal_written, 0, sizeof s_goal_written);
    memset(s_goal_seen, 0, sizeof s_goal_seen);
    s_party_on = 0;
    s_clock_seen = 0;
    s_entry_count = 0;
    s_drawn = -1;
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
        snprintf(
            reason, sizeof reason, "NG++ knows NTSC-U, PAL and NTSC-U v2.00, not %s", api->version
        );
        api->fail(mod, reason);
        return 1;
    }
    s_notices = api->setting_bool(mod, "notices");
    s_enemies = api->setting_bool(mod, "enemy_scaling");
    s_enemy_strength = (float)api->setting_float(mod, "enemy_strength");
    s_damage_carry = 0.0f;
    s_text = (int32_t (*)(int32_t))api->function(s_disc->text);

    if (take(
            WT_HOOK_Vendor_BuildList,
            WT_SIG_Vendor_BuildList,
            (void*)build_list,
            (void**)&s_build_list
        )
        || take(WT_HOOK_Vendor_Price, WT_SIG_Vendor_Price, (void*)price, (void**)&s_price)
        || take(WT_HOOK_Item_Upgrade, WT_SIG_Item_Upgrade, (void*)upgrade, (void**)&s_upgrade)
        || take(WT_HOOK_Item_Colors, WT_SIG_Item_Colors, (void*)colors, (void**)&s_colors)
        || take(WT_HOOK_Moby_PostHit, WT_SIG_Moby_PostHit, (void*)post_hit, (void**)&s_post_hit)
        || take(
            WT_HOOK_Hero_TakeDamage,
            WT_SIG_Hero_TakeDamage,
            (void*)take_damage,
            (void**)&s_take_damage
        )) {
        return 1;
    }
    // The tier's tint in the vendor's list: optional (an OpenRAC without these keeps the gold).
    if (api->hook(
            mod,
            WT_HOOK_Vendor_DrawList,
            WT_SIG_Vendor_DrawList,
            (void*)draw_list,
            (void**)&s_draw_list
        ) == OPENRAC_HOOK_OK
        && api->hook(
               mod,
               WT_HOOK_Item_VersionColor,
               WT_SIG_Item_VersionColor,
               (void*)version_color,
               (void**)&s_version_color
           ) != OPENRAC_HOOK_OK) {
        s_version_color = NULL;
    }
    // The tier's name in the vendor: optional (an OpenRAC without it shows the game's "Mega").
    if (api->hook(
            mod, WT_HOOK_Text_ById, WT_SIG_Text_ById, (void*)text_by_id, (void**)&s_text_by_id
        )
        != OPENRAC_HOOK_OK) {
        s_text_by_id = NULL;
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
