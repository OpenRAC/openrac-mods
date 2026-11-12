/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * NG++: the rules (tiers.h). The tables below are the mod's balance: change them here.
 */
#include "tiers.h"

#include <stdio.h>
#include <string.h>

/* The eight tiers: buy, then level. A bought tier's price is wt_price's, from the weapon's Mega. */
static const wt_step k_tiers[WT_TIERS] = {
    {"Alpha", WT_BUY, 0, 0, 1.25f, 1.10f, WT_TINT_LIME},
    {"Beta", WT_EXP, 0, 8000, 1.50f, 1.20f, WT_TINT_CYAN},
    {"Gamma", WT_BUY, 0, 0, 1.75f, 1.30f, WT_TINT_EMERALD},
    {"Delta", WT_EXP, 0, 12000, 2.00f, 1.40f, WT_TINT_TEAL},
    {"Epsilon", WT_BUY, 0, 0, 2.40f, 1.50f, WT_TINT_VIOLET},
    {"Zeta", WT_EXP, 0, 16000, 2.80f, 1.60f, WT_TINT_MAGENTA},
    {"Eta", WT_BUY, 0, 0, 3.30f, 1.80f, WT_TINT_CRIMSON},
    {"Omega", WT_EXP, 0, 24000, 4.00f, 2.00f, WT_TINT_PRISMATIC},
};

/* The RYNO II's two versions of its own, before the tiers. */
static const wt_step k_ryno[2] = {
    {"Heavy", WT_EXP, 0, 6000, 1.40f, 1.50f, WT_TINT_ORANGE},
    {"Ultima", WT_BUY, 0, 0, 2.20f, 2.00f, WT_TINT_ULTIMA},
};

/* The tints, 0x00BBGGRR as the game's own (D_001ADAF8). */
static const int32_t k_rgb[] = {
    0x20FFB0, /* lime */
    0xFFE000, /* cyan */
    0x50A010, /* emerald */
    0x909000, /* teal */
    0xFF4090, /* violet */
    0xD020FF, /* magenta */
    0x3C14DC, /* crimson */
};
#define WT_RGB_COUNT ((int)(sizeof k_rgb / sizeof k_rgb[0]))

/* Ticks a prismatic tint takes from one colour to the next. */
#define WT_PRISM_TICKS 45u
/* Ticks of one beat of Ultima's black and red. */
#define WT_ULTIMA_TICKS 60u

wt_kind wt_kind_of(int item) {
    switch (item) {
        case 22: /* Chopper */
        case 23: /* Pulse Rifle */
        case 24: /* Seeker Gun */
        case 25: /* Hoverbomb Gun */
        case 26: /* Blitz Gun */
        case 27: /* Minirocket Tube */
        case 28: /* Plasma Coil */
        case 29: /* Lava Gun */
        case 30: /* Lancer */
        case 31: /* Synthenoid */
        case 32: /* Spiderbot Glove */
        case 37: /* Bouncer */
        case 41: /* Miniturret Glove */
        case 42: /* Gravity Bomb */
        case 45: /* Shield Charger */
            return WT_KIND_FOUR;
        case 12: /* Bomb Glove */
        case 14: /* Visibomb Gun */
        case 17: /* Decoy Glove */
        case 18: /* Tesla Claw */
        case 53: /* Walloper */
            return WT_KIND_TWO;
        case 9:
            return WT_KIND_ZAPPER;
        case 16:
            return WT_KIND_SHEEP;
        case 43:
            return WT_KIND_SINGLE;
        case 44:
            return WT_KIND_RYNO;
        default:
            return WT_KIND_NONE;
    }
}

int wt_step_count(wt_kind kind) {
    if (kind == WT_KIND_NONE) {
        return 0;
    }
    return kind == WT_KIND_RYNO ? WT_TIERS + 2 : WT_TIERS;
}

/* The step at `level` as the tables have it (a Sheepinator's purchases not yet turned). */
static const wt_step* table_step(wt_kind kind, int level) {
    if (level < 0 || level >= wt_step_count(kind)) {
        return NULL;
    }
    if (kind == WT_KIND_RYNO) {
        return level < 2 ? &k_ryno[level] : &k_tiers[level - 2];
    }
    return &k_tiers[level];
}

const wt_step* wt_step_at(wt_kind kind, int level) {
    return table_step(kind, level);
}

int wt_mega_version(wt_kind kind) {
    switch (kind) {
        case WT_KIND_FOUR:
            return 2;
        case WT_KIND_TWO:
            return 1;
        case WT_KIND_SINGLE:
        case WT_KIND_RYNO:
            return 0; /* its one version, bought */
        default:
            return -1; /* none: the Zapper's and the Sheepinator's versions are never sold */
    }
}

/* Times the base, for the first bought step, the second, ... (the RYNO II has five: Ultima first). */
static const float k_price_times[] = {1.5f, 2.5f, 4.0f, 6.0f, 9.0f};

/* `x` rounded to its two leading digits and a half: 562,500 is 565,000. */
static int32_t nice(double x) {
    double unit = 5.0;
    while (x >= unit * 200.0) {
        unit *= 10.0;
    }
    return (int32_t)((long long)(x / unit + 0.5) * (long long)unit);
}

int32_t wt_price(wt_kind kind, int level, int32_t mega) {
    const wt_step* s = table_step(kind, level);
    if (s == NULL || s->how != WT_BUY) {
        return 0;
    }
    const int32_t base = mega <= 0               ? WT_PRICE_LADDER
                         : mega < WT_PRICE_FLOOR ? WT_PRICE_FLOOR
                                                 : mega;
    int32_t last = mega > 0 ? mega : base;
    int bought = 0;
    for (int i = 0; i <= level; ++i) {
        const wt_step* t = table_step(kind, i);
        if (t->how != WT_BUY) {
            continue;
        }
        int32_t p = nice((double)base * k_price_times[bought++]);
        // Each above the one before and above the Mega.
        if (p <= last) {
            p = nice((double)last * 1.1) > last ? nice((double)last * 1.1) : last + 5000;
        }
        last = p;
    }
    return last;
}

/* Whether the version before the step at `level` was bought (so needs mastering). */
/* The experience goals' multiplier of kind `kind`: WT_RYNO_EXP_SCALE for the RYNO II, else 1. */
static int32_t exp_scale(wt_kind kind) {
    return kind == WT_KIND_RYNO ? WT_RYNO_EXP_SCALE : 1;
}

static int previous_bought(wt_kind kind, int level) {
    if (level > 0) {
        return table_step(kind, level - 1)->how == WT_BUY;
    }
    // The last real version: bought for the two-version weapons, the Zodiac and the RYNO II.
    return kind == WT_KIND_TWO || kind == WT_KIND_SINGLE || kind == WT_KIND_RYNO;
}

int wt_can_buy(wt_kind kind, int level, int at_last, int32_t exp) {
    const wt_step* s = wt_step_at(kind, level);

    if (!at_last || s == NULL || s->how != WT_BUY) {
        return 0;
    }
    return !previous_bought(kind, level) || exp >= WT_MASTERY_EXP * exp_scale(kind);
}

int wt_exp_reached(wt_kind kind, int level, int at_last, int32_t exp) {
    const wt_step* s = wt_step_at(kind, level);
    return at_last && s != NULL && s->how == WT_EXP && exp >= s->exp * exp_scale(kind);
}

int32_t wt_exp_goal(wt_kind kind, int level) {
    const wt_step* s = wt_step_at(kind, level);

    if (s == NULL) {
        return 0;
    }
    if (s->how == WT_EXP) {
        return s->exp * exp_scale(kind);
    }
    return previous_bought(kind, level) ? WT_MASTERY_EXP * exp_scale(kind) : 0;
}

/* The RYNO II's tiers add their gain to Ultima's: Alpha is Ultima and a quarter more. */
static float ryno_on_ultima(float ultima, float tier) {
    return ultima + (tier - 1.0f);
}

float wt_damage(wt_kind kind, int level) {
    const wt_step* s = table_step(kind, level - 1);

    if (s == NULL) {
        return 1.0f;
    }
    return kind == WT_KIND_RYNO && level > 2 ? ryno_on_ultima(k_ryno[1].damage, s->damage)
                                             : s->damage;
}

float wt_ammo(wt_kind kind, int level) {
    const wt_step* s = table_step(kind, level - 1);

    if (s == NULL) {
        return 1.0f;
    }
    return kind == WT_KIND_RYNO && level > 2 ? ryno_on_ultima(k_ryno[1].ammo, s->ammo) : s->ammo;
}

int32_t wt_ammo_max(wt_kind kind, int level, int32_t base) {
    const float k = wt_ammo(kind, level);
    int32_t n = (int32_t)((float)base * k + 0.5f);

    // The game keeps the most ammo in a u16.
    if (n > 0xFFFF) {
        n = 0xFFFF;
    }
    return base <= 0 ? base : n;
}

/* `a` and `b` mixed, `t` of 256 toward `b`, per channel. */
static int32_t mix(int32_t a, int32_t b, uint32_t t) {
    int32_t out = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        const int32_t ca = (a >> shift) & 0xFF;
        const int32_t cb = (b >> shift) & 0xFF;
        out |= ((ca + (int32_t)(((cb - ca) * (int32_t)t) / 256)) & 0xFF) << shift;
    }
    return out;
}

int32_t wt_tint_rgb(wt_kind kind, int level, uint32_t tick) {
    const wt_step* s = table_step(kind, level - 1);

    if (s == NULL) {
        return -1;
    }
    switch (s->tint) {
        case WT_TINT_PRISMATIC: {
            const uint32_t i = (tick / WT_PRISM_TICKS) % (uint32_t)WT_RGB_COUNT;
            const uint32_t t = (tick % WT_PRISM_TICKS) * 256u / WT_PRISM_TICKS;
            return mix(k_rgb[i], k_rgb[(i + 1u) % (uint32_t)WT_RGB_COUNT], t);
        }
        case WT_TINT_ORANGE:
            return 0x0080FF;
        case WT_TINT_ULTIMA: {
            // Red that sinks to near black and back.
            const uint32_t phase = tick % WT_ULTIMA_TICKS;
            const uint32_t half = WT_ULTIMA_TICKS / 2u;
            const uint32_t t = (phase < half ? phase : WT_ULTIMA_TICKS - phase) * 256u / half;
            return mix(0x0000E0, 0x080808, t);
        }
        default:
            return k_rgb[s->tint];
    }
}

void wt_display_name(char* out, int size, const char* base, wt_kind kind, int level) {
    const wt_step* s = table_step(kind, level - 1);

    if (size <= 0) {
        return;
    }
    // The RYNO II's own versions take the name the design gives them; its tiers follow Ultima.
    if (kind == WT_KIND_RYNO && level >= 1) {
        snprintf(out, (size_t)size, "RYNO II %s", s->name);
        return;
    }
    if (s == NULL) {
        snprintf(out, (size_t)size, "%s", base);
        return;
    }
    snprintf(out, (size_t)size, "%s %s", base, s->name);
}

/* ---- Enemies ---- */

/* By cycle from 2 on: the health matches the damage of the tier expected by then (cycle 2 Beta,
 * 3 Delta, 4 Zeta, 5 Omega), so a weapon at that tier kills in about the time V4 did at cycle 1;
 * past the table each cycle adds the last step again. The damage they deal rises more gently. */
static const float k_enemy_health[] = {1.0f, 1.0f, 1.5f, 2.0f, 2.8f, 4.0f};
static const float k_enemy_damage[] = {1.0f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
#define WT_CURVE ((int)(sizeof k_enemy_health / sizeof k_enemy_health[0]))

static float curve(const float* table, float step, int cycle, float strength) {
    float k;
    if (cycle < 0) {
        cycle = 0;
    }
    if (cycle < WT_CURVE) {
        k = table[cycle];
    } else {
        k = table[WT_CURVE - 1] + step * (float)(cycle - (WT_CURVE - 1));
    }
    if (strength < 0.0f) {
        strength = 0.0f;
    }
    return 1.0f + (k - 1.0f) * strength;
}

float wt_enemy_health(int cycle, float strength) {
    return curve(k_enemy_health, 1.0f, cycle, strength);
}

float wt_enemy_damage(int cycle, float strength) {
    return curve(k_enemy_damage, 0.25f, cycle, strength);
}

/* ---- The book ---- */

const wt_record* wt_book_find(const wt_book* book, const uint8_t clock[8]) {
    for (int i = book->count - 1; i >= 0; --i) {
        if (memcmp(book->records[i].clock, clock, 8) == 0) {
            return &book->records[i];
        }
    }
    return NULL;
}

void wt_book_put(wt_book* book, const uint8_t clock[8], const uint8_t levels[WT_ITEMS]) {
    int at = -1;

    for (int i = 0; i < book->count; ++i) {
        if (memcmp(book->records[i].clock, clock, 8) == 0) {
            at = i;
        }
    }
    // A record of that clock already: drop it, the new one goes last.
    if (at >= 0) {
        memmove(
            &book->records[at],
            &book->records[at + 1],
            (size_t)(book->count - at - 1) * sizeof book->records[0]
        );
        --book->count;
    }
    // Full: the oldest goes.
    if (book->count == WT_RECORDS) {
        memmove(&book->records[0], &book->records[1], (WT_RECORDS - 1) * sizeof book->records[0]);
        --book->count;
    }
    memcpy(book->records[book->count].clock, clock, 8);
    memcpy(book->records[book->count].level, levels, WT_ITEMS);
    ++book->count;
}

int wt_book_write(const wt_book* book, char* out, int size) {
    static const char hex[] = "0123456789abcdef";
    const int line = 16 + 1 + 2 * WT_ITEMS + 1;
    const int need = book->count * line;

    if (out == NULL || size < need + 1) {
        return need;
    }
    char* p = out;
    for (int i = 0; i < book->count; ++i) {
        const wt_record* r = &book->records[i];
        for (int k = 0; k < 8; ++k) {
            *p++ = hex[r->clock[k] >> 4];
            *p++ = hex[r->clock[k] & 15];
        }
        *p++ = ' ';
        for (int k = 0; k < WT_ITEMS; ++k) {
            *p++ = hex[r->level[k] >> 4];
            *p++ = hex[r->level[k] & 15];
        }
        *p++ = '\n';
    }
    *p = 0;
    return need;
}

/* The value of hex digit `c`, -1 for none. */
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Reads `n` bytes of hex at `p` into `out`; 1 when all were digits. */
static int read_hex(const char* p, uint8_t* out, int n) {
    for (int i = 0; i < n; ++i) {
        const int hi = hex_digit(p[2 * i]);
        const int lo = hi < 0 ? -1 : hex_digit(p[2 * i + 1]);
        if (lo < 0) {
            return 0;
        }
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    return 1;
}

int wt_book_read(wt_book* book, const char* text) {
    book->count = 0;
    const char* p = text;

    while (p != NULL && *p != 0) {
        const char* end = strchr(p, '\n');
        const size_t length = end != NULL ? (size_t)(end - p) : strlen(p);
        uint8_t clock[8];
        uint8_t levels[WT_ITEMS];

        // A line of the right length and digits is a record; anything else is skipped.
        if (length >= 16 + 1 + 2 * WT_ITEMS && p[16] == ' ' && read_hex(p, clock, 8)
            && read_hex(p + 17, levels, WT_ITEMS)) {
            wt_book_put(book, clock, levels);
        }
        p = end != NULL ? end + 1 : NULL;
    }
    return book->count;
}
