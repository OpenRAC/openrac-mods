/* SPDX-License-Identifier: GPL-3.0-or-later The xbox-icons library as the port loads it into Going
 * Commando, against a host table made here (tests/common/fake_api.h), on each of its discs (NTSC-U,
 * PAL, NTSC-U v2.00): it loads with no hook point but a tick callback and refuses a disc it does
 * not know. With no effect bank, a bank out of the console's memory, or fonts that are not the
 * game's, it changes nothing; fonts laid at another disc's addresses it does not see, at its own
 * disc's it does (said once, with Trace). With OPENRAC_RAC2_LEVEL naming a level folder of the same
 * layout as rac1's decompiled ones (core_index.bin and unpacked/core_data.bin), the game's own
 * fonts are laid at each disc's addresses too: painted on the first tick, and left alone after.
 *
 * Run with the library's path: load_test <xbox_icons.dylib|.so|.dll>.
 */
#include "fake_api.h"

static uint8_t* s_bank;

static int known(uint32_t hash) {
    for (size_t i = 0; i < xbox_icons_rac2.font_count; ++i) {
        if (xbox_icons_rac2.fonts[i] == hash) {
            return 1;
        }
    }
    return 0;
}

static void play(const xbox_icons_disc* d, openrac_mod* mod, openrac_mod_load_fn load) {
    memset(s_memory, 0, MEMORY_SIZE);
    openrac_api api = make_api("rac2", d->version);
    s_failed[0] = 0;
    s_hooks = 0;
    s_tick = NULL;
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hooks == 0 && s_tick != NULL);

    /* No bank yet (a level loading), then one past the console's memory: nothing read. */
    s_tick(s_tick_user);
    lay_table(d, XBOX_ICONS_RAM - 0x1000u);
    s_tick(s_tick_user);
    put_u32(d->bank, 0);

    /* Fonts that are not the game's (here all zero), laid at another disc's addresses: not seen. */
    memset(s_memory + BANK + 0x800u, 7, 0x400);
    for (size_t i = 0; i < xbox_icons_rac2.disc_count; ++i) {
        const xbox_icons_disc* o = &xbox_icons_rac2.discs[i];
        if (o->bank != d->bank && o->table != d->table) {
            lay_table(o, BANK);
            s_lines = 0;
            s_tick(s_tick_user);
            CHECK(s_lines == 0);
            put_u32(o->bank, 0);
        }
    }
    /* At its own: seen, left as they are, and said once. */
    lay_table(d, BANK);
    const uint32_t other = fonts_hash();
    s_lines = 0;
    s_tick(s_tick_user);
    s_tick(s_tick_user);
    CHECK(fonts_hash() == other && s_lines == 1);
    /* The title's layout: no third font (its record zero). The two are seen (said once), not
     * passed over as a bank still loading. */
    put_u16(d->table + 16u * 3u + 8u, 0);
    put_u16(d->table + 16u * 3u + 0xAu, 0);
    s_lines = 0;
    s_tick(s_tick_user);
    s_tick(s_tick_user);
    CHECK(fonts_hash() == other && s_lines == 1);
    lay_table(d, BANK);

    if (s_bank != NULL) {
        /* The game's fonts: painted on the first tick, then left as painted. */
        memcpy(s_memory + BANK, s_bank, LEVEL_BANK_BYTES);
        CHECK(known(fonts_hash()));
        s_lines = 0;
        s_tick(s_tick_user);
        const uint32_t painted = fonts_hash();
        CHECK(!known(painted) && s_lines == 1);
        s_tick(s_tick_user);
        CHECK(fonts_hash() == painted && s_lines == 1);
        /* The next level loads the game's fonts again: painted again, the same. */
        memcpy(s_memory + BANK, s_bank, LEVEL_BANK_BYTES);
        s_tick(s_tick_user);
        CHECK(fonts_hash() == painted);
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    openrac_mod_load_fn load = open_mod(argv[1]);
    openrac_mod* mod = (openrac_mod*)&s_info; /* any handle: the mod only passes it back */

    /* A disc it does not know: refused, with a reason, before any callback. */
    openrac_api api = make_api("rac2", "jp");
    s_failed[0] = 0;
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "jp") != NULL && s_tick == NULL);

    /* The three discs, as OpenRAC's games/rac2/game.json names them. */
    CHECK(xbox_icons_rac2.disc_count == 3);
    s_bank = read_level("OPENRAC_RAC2_LEVEL");
    if (s_bank == NULL) {
        printf("xbox_icons_rac2_load_test: no OPENRAC_RAC2_LEVEL, the game's fonts not tried\n");
    }
    for (size_t i = 0; i < xbox_icons_rac2.disc_count; ++i) {
        play(&xbox_icons_rac2.discs[i], mod, load);
    }

    free(s_bank);
    free(s_memory);
    printf("xbox_icons_rac2_load_test: ok\n");
    return 0;
}
