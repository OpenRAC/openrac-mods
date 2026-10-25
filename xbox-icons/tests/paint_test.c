/* SPDX-License-Identifier: GPL-3.0-or-later
 * Painting (paint.h) on fonts made here as the game's are laid out: letters in grey outside the
 * cells, four face buttons of one dark sphere with a coloured symbol each, four tabs. After it: the
 * texels and palette entries outside the cells are as they were; no symbol is left; each letter is
 * there in its colour; and with too few free palette entries nothing changes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paint.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define FONT (256 * 128)

static uint8_t s_fonts[3][FONT];
static uint8_t s_palette[1024];

static unsigned csm1(unsigned i) {
    return (i & ~0x18u) | ((i & 8u) << 1) | ((i & 0x10u) >> 1);
}
static void set(unsigned i, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t* e = s_palette + csm1(i) * 4u;
    e[0] = r, e[1] = g, e[2] = b, e[3] = a;
}
static const uint8_t* colour(uint8_t i) {
    return s_palette + csm1(i) * 4u;
}
static uint8_t* at(int f, unsigned u, unsigned v) {
    return &s_fonts[f][v * 256u + u];
}

/* Entries: 0 clear, 1..40 the letters' greys, 41..48 the sphere's greys, 50..53 the symbols'
 * colours, 54 the tabs' text white, the rest unused (a colour no texel has). */
static void lay(void) {
    for (unsigned i = 0; i < 256; ++i) {
        set(i, 200, 0, 200, 0x80);
    }
    set(0, 128, 128, 128, 0);
    for (unsigned i = 1; i <= 40; ++i) {
        set(i, (uint8_t)(i * 6), (uint8_t)(i * 6), (uint8_t)(i * 6), 0x80);
    }
    for (unsigned i = 41; i <= 48; ++i) {
        set(i, (uint8_t)(30 + (i - 41) * 8), (uint8_t)(30 + (i - 41) * 8), (uint8_t)(30 + (i - 41) * 8),
            0x80);
    }
    set(50, 100, 104, 230, 0x80); /* Cross: colours none of the letters has */
    set(51, 232, 135, 135, 0x80); /* Square */
    set(52, 12, 150, 60, 0x80);   /* Triangle */
    set(53, 174, 45, 45, 0x80);   /* Circle */
    set(54, 255, 255, 255, 0x80);
    for (int f = 0; f < 3; ++f) {
        memset(s_fonts[f], 0, FONT);
        for (unsigned v = 0; v < 96; ++v) {
            for (unsigned u = 0; u < 256; ++u) {
                *at(f, u, v) = (uint8_t)(1 + (u * 7 + v * 3 + (unsigned)f) % 40);
            }
        }
        for (unsigned c = 0; c < 4; ++c) {
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 24; ++x) {
                    const int dx = x - 10, dy = y - 8, d2 = dx * dx + dy * dy;
                    uint8_t i = d2 <= 64 ? (uint8_t)(41 + (dy + 8) / 2) : 0;
                    /* Each symbol covers its own texels: a cross, a square, a triangle, a ring. */
                    const int on = d2 <= 30 && ((c == 0 && (dx == dy || dx == -dy)) ||
                                                (c == 1 && (dx == 3 || dy == 3)) ||
                                                (c == 2 && dy == 4) || (c == 3 && d2 >= 20));
                    if (on) {
                        i = (uint8_t)(50 + c);
                    }
                    *at(f, c * 24u + (unsigned)x, 96u + (unsigned)y) = i;
                    *at(f, 96u + c * 24u + (unsigned)x, 96u + (unsigned)y) =
                        (x >= 3 && y >= 2 && y < 15) ? (x == 6 + (int)c ? 54 : 44) : 0;
                }
            }
        }
        /* The sticks and the rest of the row, kept. */
        for (unsigned v = 96; v < 112; ++v) {
            for (unsigned u = 192; u < 256; ++u) {
                *at(f, u, v) = (uint8_t)(1 + u % 40);
            }
        }
    }
}

static int counts_colour(int f, unsigned cell, const uint8_t rgb[3]) {
    int n = 0;
    for (unsigned y = 0; y < 16; ++y) {
        for (unsigned x = 0; x < 24; ++x) {
            n += memcmp(colour(*at(f, cell + x, 96 + y)), rgb, 3) == 0;
        }
    }
    return n;
}

int main(void) {
    lay();
    uint8_t* fonts[3] = {s_fonts[0], s_fonts[1], s_fonts[2]};
    static uint8_t before[3][FONT];
    uint8_t palette_before[1024];
    memcpy(before, s_fonts, sizeof before);
    memcpy(palette_before, s_palette, sizeof palette_before);
    const uint32_t game = xbox_icons_hash(fonts, s_palette);

    CHECK(xbox_icons_paint(fonts, s_palette) == 0);
    CHECK(xbox_icons_hash(fonts, s_palette) != game);

    for (int f = 0; f < 3; ++f) {
        /* Outside the cells, every texel as it was, and each entry those texels use. */
        for (unsigned v = 0; v < 128; ++v) {
            for (unsigned u = 0; u < 256; ++u) {
                if (v >= 96 && v < 112 && u < 192) {
                    continue;
                }
                CHECK(*at(f, u, v) == before[f][v * 256 + u]);
                CHECK(memcmp(colour(*at(f, u, v)), palette_before + csm1(*at(f, u, v)) * 4, 4) == 0);
            }
        }
        /* No symbol left: no texel of the cells has a symbol's colour, nor the tabs' white (their
         * entries, free now, may hold the letters'). */
        for (unsigned v = 96; v < 112; ++v) {
            for (unsigned u = 0; u < 192; ++u) {
                for (unsigned s = 50; s <= 54; ++s) {
                    CHECK(memcmp(colour(*at(f, u, v)), palette_before + csm1(s) * 4, 3) != 0);
                }
            }
        }
        /* Each letter in its colour: A green, X blue, Y yellow, B red, the shoulders white. */
        static const uint8_t green[3] = {6, 189, 40}, blue[3] = {122, 125, 246},
                             yellow[3] = {240, 200, 30}, red[3] = {214, 46, 46},
                             white[3] = {225, 225, 225};
        CHECK(counts_colour(f, 0, green) > 15 && counts_colour(f, 24, green) == 0);
        CHECK(counts_colour(f, 24, blue) > 15);
        CHECK(counts_colour(f, 48, yellow) > 15);
        CHECK(counts_colour(f, 72, red) > 15);
        for (unsigned c = 96; c < 192; c += 24) {
            CHECK(counts_colour(f, c, white) > 30);
        }
        /* The shoulders are pills of the sphere: their corners clear, their middle the sphere's. */
        CHECK(colour(*at(f, 96, 96))[3] == 0);
        CHECK(*at(f, 96 + 12, 96 + 15) == *at(f, 9, 96 + 15));
    }

    /* Too few free entries (each used outside the cells): nothing changes. */
    lay();
    for (unsigned i = 0; i < 256; ++i) {
        *at(0, i, 0) = (uint8_t)i;
    }
    memcpy(before, s_fonts, sizeof before);
    memcpy(palette_before, s_palette, sizeof palette_before);
    CHECK(xbox_icons_paint(fonts, s_palette) == 1);
    CHECK(memcmp(before, s_fonts, sizeof before) == 0);
    CHECK(memcmp(palette_before, s_palette, sizeof palette_before) == 0);

    printf("xbox_icons_paint_test: ok\n");
    return 0;
}
