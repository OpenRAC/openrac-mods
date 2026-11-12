/* SPDX-License-Identifier: GPL-3.0-or-later
 * The item randomizer's data folder (src/paths.c): the port's folder (data_dir) wins over the one
 * older versions worked out, and a seed remembered in the old folder is carried over once.
 *
 *   paths_test   (run in a scratch folder: it makes folders below the current one)
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* setenv, unsetenv */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paths.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* An environment variable set, or removed for an empty value. */
static void set_env(const char* k, const char* v) {
#if defined(_WIN32)
    _putenv_s(k, v);
#else
    if (*v != 0) {
        setenv(k, v, 1);
    } else {
        unsetenv(k);
    }
#endif
}

int main(void) {
    char dir[1024], path[1300], text[64];
    static const char port[] = "item-randomizer-paths-test/port/item-randomizer";

    // A seed a run before left in the port's folder is gone.
    remove("item-randomizer-paths-test/port/item-randomizer/seed.txt");

    // The old folder, under a home of the test's own on every system.
    set_env("LOCALAPPDATA", "item-randomizer-paths-test/old");
    set_env("XDG_DATA_HOME", "item-randomizer-paths-test/old");
    set_env("HOME", "item-randomizer-paths-test/old");

    // The port's folder, made as the port makes it (here through the variable, which makes it).
    set_env("OPENRAC_MOD_DATA_DIR", port);
    CHECK(rando_data_dir(dir, sizeof dir) == 0 && strcmp(dir, port) == 0);
    set_env("OPENRAC_MOD_DATA_DIR", "");

    // No port folder: the old one, holding a remembered seed.
    rando_set_port_dir(NULL);
    CHECK(rando_data_dir(dir, sizeof dir) == 0 && strcmp(dir, port) != 0);
    rando_join(path, sizeof path, dir, "seed.txt");
    CHECK(rando_write_file(path, "4ZRJ-6HB7-H6Q5\n", 15) == 0);

    // The port's folder wins, and the seed comes with it.
    rando_set_port_dir(port);
    CHECK(rando_data_dir(dir, sizeof dir) == 0 && strcmp(dir, port) == 0);
    rando_join(path, sizeof path, dir, "seed.txt");
    CHECK(rando_read_file(path, text, sizeof text) == 0 && strcmp(text, "4ZRJ-6HB7-H6Q5") == 0);

    // A seed the new folder has is kept: the old one never overwrites it.
    CHECK(rando_write_file(path, "AAAA-BBBB-CCCC", 14) == 0);
    CHECK(rando_data_dir(dir, sizeof dir) == 0);
    CHECK(rando_read_file(path, text, sizeof text) == 0 && strcmp(text, "AAAA-BBBB-CCCC") == 0);

    // The variable still names the folder outright.
    set_env("OPENRAC_MOD_DATA_DIR", "item-randomizer-paths-test/own");
    CHECK(
        rando_data_dir(dir, sizeof dir) == 0 && strcmp(dir, "item-randomizer-paths-test/own") == 0
    );

    puts("paths: ok");
    return 0;
}
