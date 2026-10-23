/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's data folder (paths.h).
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* setenv, mkdir, rmdir */
#endif
#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define SEP '\\'

static int make_dir(const char* p) {
    return _mkdir(p);
}
#else
#include <sys/stat.h>
#define SEP '/'

static int make_dir(const char* p) {
    return mkdir(p, 0755);
}
#endif

void rando_join(char* out, size_t size, const char* dir, const char* name) {
    snprintf(out, size, "%s%c%s", dir, SEP, name);
}

/* Each folder of path, made in turn (those there already are kept). */
static void make_dirs(char* path) {
    for (char* p = path + 1; *p != 0; ++p) {
        if (*p == '/' || *p == '\\') {
            const char c = *p;
            *p = 0;

            if (!(p == path + 2 && path[1] == ':')) { /* not a bare drive, "C:" */
                make_dir(path);
            }

            *p = c;
        }
    }

    make_dir(path);
}

int rando_data_dir(char* out, size_t size) {
    const char* own = getenv("OPENRAC_MOD_DATA_DIR");

    if (own != NULL && *own != 0) {
        snprintf(out, size, "%s", own);
    } else {
#if defined(_WIN32)
        const char* base = getenv("LOCALAPPDATA");

        if (base == NULL || *base == 0) {
            return -1;
        }

        snprintf(out, size, "%s\\dev.openrac.launcher\\mods\\item-randomizer", base);
#elif defined(__APPLE__)
        const char* home = getenv("HOME");

        if (home == NULL || *home == 0) {
            return -1;
        }

        snprintf(
            out,
            size,
            "%s/Library/Application Support/dev.openrac.launcher/mods/item-randomizer",
            home
        );
#else
        const char* xdg = getenv("XDG_DATA_HOME");
        const char* home = getenv("HOME");

        if (xdg != NULL && *xdg != 0) {
            snprintf(out, size, "%s/dev.openrac.launcher/mods/item-randomizer", xdg);
        } else if (home != NULL && *home != 0) {
            snprintf(out, size, "%s/.local/share/dev.openrac.launcher/mods/item-randomizer", home);
        } else {
            return -1;
        }
#endif
    }

    make_dirs(out);
    return 0;
}

int rando_read_file(const char* path, char* out, size_t size) {
    FILE* f = fopen(path, "rb");

    if (f == NULL) {
        return -1;
    }

    const size_t n = fread(out, 1, size - 1, f);
    fclose(f);
    out[n] = 0;

    for (size_t i = n; i > 0 && (out[i - 1] == '\n' || out[i - 1] == '\r' || out[i - 1] == ' ');
         --i) {
        out[i - 1] = 0;
    }

    return 0;
}

int rando_write_file(const char* path, const char* text, size_t length) {
    FILE* f = fopen(path, "wb");

    if (f == NULL) {
        return -1;
    }

    const size_t n = fwrite(text, 1, length, f);
    return fclose(f) == 0 && n == length ? 0 : -1;
}
