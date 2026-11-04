/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 * Font-bank discovery shared by the native Xbox and Steam Deck mods. */
#include "font_bank.h"

#include <string.h>

const controller_icons_disc k_controller_icons_discs[] = {
    {"pal", 0x0015F560u, 0x0018D540u},
    {"ntsc", 0x0015F460u, 0x0018D440u},
};
const uint32_t k_controller_icons_game_fonts[] = {0xA38230EAu};

const controller_icons_disc* controller_icons_disc_for(const char* version) {
    if (version == NULL) {
        return NULL;
    }
    for (unsigned i = 0; i < sizeof k_controller_icons_discs / sizeof k_controller_icons_discs[0];
         ++i) {
        if (strcmp(version, k_controller_icons_discs[i].version) == 0) {
            return &k_controller_icons_discs[i];
        }
    }
    return NULL;
}

int controller_icons_is_game_font(uint32_t hash) {
    for (unsigned i = 0;
         i < sizeof k_controller_icons_game_fonts / sizeof k_controller_icons_game_fonts[0];
         ++i) {
        if (hash == k_controller_icons_game_fonts[i]) {
            return 1;
        }
    }
    return 0;
}

int controller_icons_find_fonts(
    uint8_t* memory,
    uint32_t (*relocate_data)(uint32_t),
    const controller_icons_disc* disc,
    controller_icons_fonts* out
) {
    if (memory == NULL || relocate_data == NULL || disc == NULL || out == NULL) {
        return 0;
    }
    const uint32_t bank_word = relocate_data(disc->bank);
    const uint32_t table = relocate_data(disc->table);
    if (bank_word > CONTROLLER_ICONS_RAM - 4u || table > CONTROLLER_ICONS_RAM - 64u) {
        return 0;
    }
    uint32_t bank;
    memcpy(&bank, memory + bank_word, 4);
    if (bank == 0 || bank >= CONTROLLER_ICONS_RAM) {
        return 0;
    }
    uint32_t palette = 0;
    for (unsigned f = 0; f < 3; ++f) {
        uint16_t texel_offset, palette_offset;
        const uint32_t record = table + 16u * (f + 1u);
        memcpy(&texel_offset, memory + record + 8u, 2);
        memcpy(&palette_offset, memory + record + 10u, 2);
        const uint32_t texels = bank + (uint32_t)texel_offset * 16u;
        const uint32_t clut = bank + (uint32_t)palette_offset * 16u;
        if (texels > CONTROLLER_ICONS_RAM - CONTROLLER_ICONS_FONT_BYTES
            || clut > CONTROLLER_ICONS_RAM - 1024u || (f > 0 && clut != palette)) {
            return 0;
        }
        out->fonts[f] = memory + texels;
        palette = clut;
    }
    out->palette = memory + palette;
    out->bank = bank;
    return 1;
}
