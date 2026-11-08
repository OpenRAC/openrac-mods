/* SPDX-License-Identifier: GPL-3.0-or-later The xbox-icons library as the port loads it into
 * Ratchet & Clank, against a host table made here (tests/common/fake_api.h): it reports its id and
 * interface, refuses another game and an unknown disc, takes no hook point but a tick callback;
 * with no effect bank, a bank out of the console's memory or fonts that are not the game's, it
 * changes nothing. With OPENRAC_RAC1_LEVEL naming a level that openrac-extractor --decompile wrote
 * (decompiler_out/rac1/levels/NN), the game's own fonts are laid at each disc's addresses too:
 * painted on the first tick, and left alone after.
 *
 * Run with the library's path: load_test <xbox_icons.dylib|.so|.dll>.
 */
#include "fake_api.h"

static uint8_t* s_bank;

static void play(const char* version, openrac_mod* mod, openrac_mod_load_fn load) {
    const xbox_icons_disc* d = NULL;
    for (size_t i = 0; i < xbox_icons_rac1.disc_count; ++i) {
        if (strcmp(xbox_icons_rac1.discs[i].version, version) == 0) {
            d = &xbox_icons_rac1.discs[i];
        }
    }
    CHECK(d != NULL);
    memset(s_memory, 0, MEMORY_SIZE);
    openrac_api api = make_api("rac1", version);
    s_failed[0] = 0;
    s_hooks = 0;
    s_tick = NULL;
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hooks == 0 && s_tick != NULL);

    /* No bank yet (a level loading), then one past the console's memory: nothing read. */
    s_tick(s_tick_user);
    lay_table(d, XBOX_ICONS_RAM - 0x1000u);
    s_tick(s_tick_user);

    /* Fonts that are not the game's (here all zero): left as they are, and said once. */
    lay_table(d, BANK);
    memset(s_memory + BANK + 0x800u, 7, 0x400);
    const uint32_t other = fonts_hash();
    s_lines = 0;
    s_tick(s_tick_user);
    s_tick(s_tick_user);
    CHECK(fonts_hash() == other && s_lines == 1);

    if (s_bank != NULL) {
        /* The game's fonts: painted on the first tick, then left as painted. */
        memcpy(s_memory + BANK, s_bank, LEVEL_BANK_BYTES);
        CHECK(fonts_hash() == xbox_icons_rac1.fonts[0]);
        s_tick(s_tick_user);
        const uint32_t painted = fonts_hash();
        CHECK(painted != xbox_icons_rac1.fonts[0]);
        s_tick(s_tick_user);
        CHECK(fonts_hash() == painted);
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

    /* Another game, an unknown disc: refused, with a reason, before any callback. */
    openrac_api api = make_api("rac3", "ntsc");
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "rac1") != NULL && s_tick == NULL);
    api = make_api("rac1", "jp");
    s_failed[0] = 0;
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "jp") != NULL && s_tick == NULL);
    api = make_api("rac1", "ntsc2"); /* a disc of the other game */
    s_failed[0] = 0;
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "ntsc2") != NULL && s_tick == NULL);

    s_bank = read_level("OPENRAC_RAC1_LEVEL");
    if (s_bank == NULL) {
        printf("xbox_icons_rac1_load_test: no OPENRAC_RAC1_LEVEL, the game's fonts not tried\n");
    }
    play("pal", mod, load);
    play("ntsc", mod, load);

    free(s_bank);
    free(s_memory);
    printf("xbox_icons_rac1_load_test: ok\n");
    return 0;
}
