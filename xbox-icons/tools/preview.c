/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Paints a level's fonts as the mod does (paint.h) and writes the pad icons' row of each, before
 * and after, as a PAM picture (RGBA, 192 x 96: the three fonts as the game has them, then
 * painted), with the fonts' hash before and after. From a level that openrac-extractor --decompile
 * wrote (decompiler_out/rac1/levels/NN):
 *
 *   preview <level folder> <out.pam>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paint.h"

static uint8_t* slurp(const char* dir, const char* name, long* size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* b = malloc((size_t)*size);
    if (fread(b, 1, (size_t)*size, f) != (size_t)*size) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(1);
    }
    fclose(f);
    return b;
}
static int32_t s32(const uint8_t* p) {
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}
static unsigned csm1(unsigned i) {
    return (i & ~0x18u) | ((i & 8u) << 1) | ((i & 0x10u) >> 1);
}

static void row(FILE* out, uint8_t* const fonts[3], const uint8_t* palette) {
    for (int f = 0; f < 3; ++f) {
        for (unsigned y = 0; y < 16; ++y) {
            for (unsigned u = 0; u < XBOX_ICONS_CELLS_WIDTH; ++u) {
                const uint8_t* c = palette + csm1(fonts[f][(XBOX_ICONS_ROW + y) * 256 + u]) * 4;
                const uint8_t px[4] = {c[0], c[1], c[2], (uint8_t)(c[3] >= 0x80 ? 255 : c[3] * 2)};
                fwrite(px, 1, 4, out);
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: preview <level folder> <out.pam>\n");
        return 2;
    }
    long index_size, core_size;
    const uint8_t* index = slurp(argv[1], "core_index.bin", &index_size);
    uint8_t* core = slurp(argv[1], "unpacked/core_data.bin", &core_size);
    /* The core header: the FX textures' {count, offset} at 0x58, the FX bank at 0x68. */
    const int32_t fx = s32(index + 0x5C), bank = s32(index + 0x68);
    uint8_t* fonts[3];
    const uint8_t* palette = NULL;
    for (int f = 0; f < 3; ++f) {
        const uint8_t* e = index + fx + 16 * (f + 1);
        fonts[f] = core + bank + s32(e + 4);
        palette = core + bank + s32(e);
    }
    FILE* out = fopen(argv[2], "wb");
    fprintf(out, "P7\nWIDTH 192\nHEIGHT 96\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n");
    printf("game's  %08x\n", xbox_icons_hash(fonts, palette));
    row(out, fonts, palette);
    if (xbox_icons_paint(fonts, (uint8_t*)palette) != 0) {
        fprintf(stderr, "too few free palette entries\n");
        return 1;
    }
    printf("painted %08x\n", xbox_icons_hash(fonts, palette));
    row(out, fonts, palette);
    fclose(out);
    return 0;
}
