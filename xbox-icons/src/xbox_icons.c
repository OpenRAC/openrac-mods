/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: Xbox button icons, a mod of OpenRAC's native port (README.md). The game prints
 * the controller's buttons as glyphs of its three fonts; each tick (on_frame), the mod finds the
 * fonts in game memory (xbox_icons.h) and, when they are the game's own, paints Xbox buttons over
 * the PlayStation 2 ones (paint.h). Painted fonts are no longer the game's, so they are painted once
 * a level, and fonts the mod does not know are left alone.
 */
#include <stdio.h>
#include <string.h>

#include "openrac/openrac_mod.h"

#include "paint.h"
#include "xbox_icons.h"

OPENRAC_MOD_DEFINE("xbox-icons", "1.0.0")

static const openrac_api* s_api;
static openrac_mod* s_mod;
static const xbox_icons_disc* s_disc;
static int s_trace;
static uint32_t s_unknown; /* the last fonts not known, said once */

static uint32_t get_u32(openrac_gaddr a) {
    uint32_t v;
    memcpy(&v, s_api->memory + a, 4);
    return v;
}
static uint16_t get_u16(openrac_gaddr a) {
    uint16_t v;
    memcpy(&v, s_api->memory + a, 2);
    return v;
}

static void say(const char* text) {
    if (s_trace) {
        s_api->log(s_mod, OPENRAC_LOG_INFO, text);
    }
}

static void tick(void* user) {
    (void)user;
    const uint32_t bank = get_u32(s_api->relocate_data(s_disc->bank));
    const openrac_gaddr table = s_api->relocate_data(s_disc->table);
    if (bank == 0 || bank >= XBOX_ICONS_RAM) {
        return;
    }
    uint8_t* fonts[3];
    uint32_t palette = 0;
    for (int f = 0; f < 3; ++f) {
        const openrac_gaddr record = table + 16u * (uint32_t)(f + 1);
        const uint32_t texels = bank + get_u16(record + 8u) * 16u;
        const uint32_t clut = bank + get_u16(record + 0xAu) * 16u;
        if (texels + XBOX_ICONS_FONT_BYTES > XBOX_ICONS_RAM || clut + 1024u > XBOX_ICONS_RAM ||
            (f > 0 && clut != palette)) {
            return;
        }
        fonts[f] = s_api->memory + texels;
        palette = clut;
    }
    const uint32_t hash = xbox_icons_hash(fonts, s_api->memory + palette);
    int known = 0;
    for (size_t i = 0; i < sizeof k_xbox_icons_game_fonts / sizeof k_xbox_icons_game_fonts[0]; ++i) {
        known |= hash == k_xbox_icons_game_fonts[i];
    }
    if (!known) {
        if (hash != s_unknown) {
            s_unknown = hash;
            char line[96];
            snprintf(line, sizeof line, "fonts %08x at 0x%08x: not the game's, left as they are",
                     (unsigned)hash, (unsigned)bank);
            say(line);
        }
        return;
    }
    if (xbox_icons_paint(fonts, s_api->memory + palette) != 0) {
        if (hash != s_unknown) {
            s_unknown = hash;
            s_api->log(s_mod, OPENRAC_LOG_WARN, "too few free palette entries for the letters");
        }
        return;
    }
    s_unknown = xbox_icons_hash(fonts, s_api->memory + palette); /* what was painted: no word */
    char line[64];
    snprintf(line, sizeof line, "painted the fonts at 0x%08x", (unsigned)bank);
    say(line);
}

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    s_disc = NULL;
    s_unknown = 0;
    if (strcmp(api->game, "rac1") != 0) {
        api->fail(mod, "xbox-icons is for Ratchet & Clank (rac1)");
        return 1;
    }
    for (size_t i = 0; i < sizeof k_xbox_icons_discs / sizeof k_xbox_icons_discs[0]; ++i) {
        if (strcmp(api->version, k_xbox_icons_discs[i].version) == 0) {
            s_disc = &k_xbox_icons_discs[i];
        }
    }
    if (s_disc == NULL) {
        char reason[96];
        snprintf(reason, sizeof reason, "xbox-icons knows the PAL and NTSC-U discs, not %s",
                 api->version);
        api->fail(mod, reason);
        return 1;
    }
    s_trace = api->setting_bool(mod, "trace");
    if (api->on_frame(mod, tick, NULL) != 0) {
        api->fail(mod, "no tick callback in this OpenRAC");
        return 1;
    }
    return 0;
}
