/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Xbox buttons painted over the game's PlayStation 2 ones (paint.h).
 */
#include "paint.h"

#include <stdlib.h>
#include <string.h>

#include "letters.h"

#define W XBOX_ICONS_WIDTH
#define ROW XBOX_ICONS_ROW
#define CELL_W 24
#define CELL_H 16

typedef struct letter {
    unsigned u;                 /* its cell */
    const char* const* rows;    /* letters.h */
    int colour;                 /* k_colours */
} letter;

/* The letters' colours: the game's own green, blue and red of the Triangle, the Cross and the
 * Circle, a yellow as bright, and the shoulders' white. */
static const uint8_t k_colours[][3] = {
    {6, 189, 40}, {122, 125, 246}, {240, 200, 30}, {214, 46, 46}, {225, 225, 225},
};
#define COLOURS (sizeof k_colours / sizeof k_colours[0])

static const letter k_letters[] = {
    {0, k_letter_a, 0},    {24, k_letter_x, 1},   {48, k_letter_y, 2},   {72, k_letter_b, 3},
    {96, k_letter_lb, 4},  {120, k_letter_lt, 4}, {144, k_letter_rb, 4}, {168, k_letter_rt, 4},
};

static const uint8_t k_face_cells[4] = {0, 24, 48, 72};
static const uint8_t k_shoulder_cells[4] = {96, 120, 144, 168};
static const uint8_t k_sphere_grey[3] = {46, 46, 46}; /* what a letter's edge blends with */
static const uint8_t k_ring[4] = {30, 30, 30, 0x80};

/* A palette index's place in CSM1 order. */
static unsigned csm1(unsigned i) {
    return (i & ~0x18u) | ((i & 8u) << 1) | ((i & 0x10u) >> 1);
}
static const uint8_t* colour(const uint8_t* palette, uint8_t i) {
    return palette + csm1(i) * 4u;
}
static int saturation(const uint8_t* palette, uint8_t i) {
    const uint8_t* c = colour(palette, i);
    int hi = c[0], lo = c[0];
    for (int k = 1; k < 3; ++k) {
        hi = c[k] > hi ? c[k] : hi;
        lo = c[k] < lo ? c[k] : lo;
    }
    return hi - lo;
}
static int luminance(const uint8_t* palette, uint8_t i) {
    const uint8_t* c = colour(palette, i);
    return (c[0] + c[1] + c[2]) / 3;
}
static uint8_t* texel(uint8_t* font, unsigned u, unsigned y) {
    return font + (ROW + y) * W + u;
}
static int in_cells(unsigned u, unsigned v) {
    return v >= ROW && v < ROW + CELL_H && u < XBOX_ICONS_CELLS_WIDTH;
}

uint32_t xbox_icons_hash(uint8_t* const fonts[3], const uint8_t* palette) {
    uint32_t h = 2166136261u;
    for (int f = 0; f < 3; ++f) {
        for (unsigned y = 0; y < CELL_H; ++y) {
            for (unsigned u = 0; u < XBOX_ICONS_CELLS_WIDTH; ++u) {
                h = (h ^ *texel(fonts[f], u, y)) * 16777619u;
            }
        }
    }
    for (unsigned i = 0; i < 1024; ++i) {
        h = (h ^ palette[i]) * 16777619u;
    }
    return h;
}

/* The face buttons' sphere without a symbol, from one font's four buttons. */
static void sphere_of(uint8_t* font, const uint8_t* palette, uint8_t sphere[CELL_H][CELL_W]) {
    for (unsigned y = 0; y < CELL_H; ++y) {
        for (unsigned x = 0; x < CELL_W; ++x) {
            uint8_t best = *texel(font, k_face_cells[0] + x, y);
            for (int c = 1; c < 4; ++c) {
                const uint8_t i = *texel(font, k_face_cells[c] + x, y);
                const int si = saturation(palette, i), sb = saturation(palette, best);
                if (si < sb || (si == sb && luminance(palette, i) < luminance(palette, best))) {
                    best = i;
                }
            }
            sphere[y][x] = best;
        }
    }
    /* A texel every symbol covers: the nearest opaque texel of the sphere that is clean. */
    uint8_t fixed[CELL_H][CELL_W];
    memcpy(fixed, sphere, sizeof fixed);
    for (int y = 0; y < CELL_H; ++y) {
        for (int x = 0; x < CELL_W; ++x) {
            if (saturation(palette, sphere[y][x]) <= 20) {
                continue;
            }
            int best_d = 1 << 30;
            for (int yy = 0; yy < CELL_H; ++yy) {
                for (int xx = 0; xx < CELL_W; ++xx) {
                    const uint8_t i = sphere[yy][xx];
                    const int d = abs(xx - x) + abs(yy - y);
                    if (saturation(palette, i) <= 20 && colour(palette, i)[3] == 0x80 &&
                        (d < best_d || (d == best_d && i < fixed[y][x]))) {
                        best_d = d;
                        fixed[y][x] = i;
                    }
                }
            }
        }
    }
    memcpy(sphere, fixed, sizeof fixed);
}

static int coverage(const char* const* rows, int x, int y) {
    if (x < 0 || y < 0 || x >= CELL_W || y >= CELL_H) {
        return 0;
    }
    switch (rows[y][x]) {
    case '-': return 1;
    case '+': return 2;
    case '*': return 3;
    case '#': return 4;
    default: return 0;
    }
}

int xbox_icons_paint(uint8_t* const fonts[3], uint8_t* palette) {
    /* The palette entries some texel outside the cells uses: they stay as they are. */
    uint8_t used[256] = {0};
    for (int f = 0; f < 3; ++f) {
        for (unsigned v = 0; v < XBOX_ICONS_HEIGHT; ++v) {
            for (unsigned u = 0; u < W; ++u) {
                if (!in_cells(u, v)) {
                    used[fonts[f][v * W + u]] = 1;
                }
            }
        }
    }

    /* The spheres and the pills, and what they use. */
    uint8_t cells[3][CELL_H][XBOX_ICONS_CELLS_WIDTH];
    for (int f = 0; f < 3; ++f) {
        uint8_t sphere[CELL_H][CELL_W];
        sphere_of(fonts[f], palette, sphere);
        for (unsigned y = 0; y < CELL_H; ++y) {
            for (int c = 0; c < 4; ++c) {
                for (unsigned x = 0; x < CELL_W; ++x) {
                    const unsigned sx = x < 8 ? 2 + x : x < 16 ? 9 : x - 6;
                    cells[f][y][k_face_cells[c] + x] = sphere[y][x];
                    cells[f][y][k_shoulder_cells[c] + x] = sphere[y][sx];
                }
            }
            for (unsigned u = 0; u < XBOX_ICONS_CELLS_WIDTH; ++u) {
                used[cells[f][y][u]] = 1;
            }
        }
    }

    /* The letters' entries: the ring, and each colour at each of 4 coverages. */
    uint8_t slot[1 + COLOURS * 4];
    unsigned n = 0;
    for (unsigned i = 0; i < 256 && n < sizeof slot; ++i) {
        if (!used[i]) {
            slot[n++] = (uint8_t)i;
        }
    }
    if (n < sizeof slot) {
        return 1;
    }
    const uint8_t ring = slot[0];
    memcpy(palette + csm1(ring) * 4u, k_ring, 4);
    for (unsigned c = 0; c < COLOURS; ++c) {
        for (unsigned lv = 1; lv <= 4; ++lv) {
            uint8_t* e = palette + csm1(slot[1 + c * 4 + lv - 1]) * 4u;
            for (int k = 0; k < 3; ++k) {
                e[k] = (uint8_t)((k_colours[c][k] * lv + k_sphere_grey[k] * (4 - lv) + 2) / 4);
            }
            e[3] = 0x80;
        }
    }

    for (int f = 0; f < 3; ++f) {
        for (size_t l = 0; l < sizeof k_letters / sizeof k_letters[0]; ++l) {
            const letter* L = &k_letters[l];
            for (int y = 0; y < CELL_H; ++y) {
                for (int x = 0; x < CELL_W; ++x) {
                    uint8_t* t = &cells[f][y][L->u + x];
                    const int cov = coverage(L->rows, x, y);
                    if (cov > 0) {
                        *t = slot[1 + L->colour * 4 + cov - 1];
                        continue;
                    }
                    int near = 0;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            near |= coverage(L->rows, x + dx, y + dy) >= 2;
                        }
                    }
                    if (near && colour(palette, *t)[3] == 0x80 && luminance(palette, *t) < 70) {
                        *t = ring;
                    }
                }
            }
        }
        for (unsigned y = 0; y < CELL_H; ++y) {
            memcpy(texel(fonts[f], 0, y), cells[f][y], XBOX_ICONS_CELLS_WIDTH);
        }
    }
    return 0;
}
