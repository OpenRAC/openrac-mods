/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's data folder (paths.h): finding and making the folder, joining a file name
 * onto it, and reading and writing the two small files kept there, seed.txt and the spoiler logs.
 * The folder differs per platform, so the platform-specific parts sit behind the guards below.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* setenv, mkdir, rmdir */
#endif
#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The permissions of a folder the mod makes (rwxr-xr-x); Windows has none. */
#define DATA_DIR_MODE 0755

#if defined(_WIN32)
#include <direct.h>
#define SEP '\\'

/* Makes one folder; 0 on success, nonzero when it exists already or cannot be made. */
static int make_dir(const char* p) {
    return _mkdir(p);
}
#else
#include <sys/stat.h>
#define SEP '/'

/* Makes one folder; 0 on success, nonzero when it exists already or cannot be made. */
static int make_dir(const char* p) {
    return mkdir(p, DATA_DIR_MODE);
}
#endif

/* dir, the platform's separator and name, cut to size. */
void rando_join(char* out, size_t size, const char* dir, const char* name) {
    snprintf(out, size, "%s%c%s", dir, SEP, name);
}

/* Each folder of path, made in turn (those there already are kept). */
static void make_dirs(char* path) {
    // The first character is skipped so that a leading separator is not made as a folder.
    for (char* p = path + 1; *p != 0; ++p) {
        // At each separator, the path up to it is a folder to make (either kind on every system).
        if (*p == '/' || *p == '\\') {
            const char c = *p;
            *p = 0;

            // A bare drive letter is no folder to make.
            if (!(p == path + 2 && path[1] == ':')) {
                make_dir(path);
            }

            *p = c;
        }
    }

    make_dir(path);
}

/* The mod's data folder into out, made when missing; 0, or -1 when there is no home for it. */
int rando_data_dir(char* out, size_t size) {
    const char* own = getenv("OPENRAC_MOD_DATA_DIR");

    // The environment variable names the folder outright (tests and portable installs use it).
    if (own != NULL && *own != 0) {
        snprintf(out, size, "%s", own);
    } else {
#if defined(_WIN32)
        const char* base = getenv("LOCALAPPDATA");

        // Without a per-user data folder there is nowhere to put it.
        if (base == NULL || *base == 0) {
            return -1;
        }

        snprintf(out, size, "%s\\dev.openrac.launcher\\mods\\item-randomizer", base);
#elif defined(__APPLE__)
        const char* home = getenv("HOME");

        // Without a home folder there is nowhere to put it.
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

        // XDG_DATA_HOME when set, else the standard ~/.local/share, else nowhere.
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

/* Reads a whole small file into out, terminated and with trailing line ends and spaces cut; 0,
 * or -1 when the file cannot be opened. Anything past size - 1 bytes is not read. */
int rando_read_file(const char* path, char* out, size_t size) {
    FILE* f = fopen(path, "rb");

    // No file is no remembered text; the caller falls back.
    if (f == NULL) {
        return -1;
    }

    const size_t n = fread(out, 1, size - 1, f);
    fclose(f);
    out[n] = 0;

    // Trailing whitespace (an editor's newline) is not part of the text.
    for (size_t i = n; i > 0 && (out[i - 1] == '\n' || out[i - 1] == '\r' || out[i - 1] == ' ');
         --i) {
        out[i - 1] = 0;
    }

    return 0;
}

/* Writes length bytes of text to path, replacing the file; 0, or -1 when it cannot be written. */
int rando_write_file(const char* path, const char* text, size_t length) {
    FILE* f = fopen(path, "wb");

    // A folder that cannot be written to is reported to the caller, which carries on without.
    if (f == NULL) {
        return -1;
    }

    const size_t n = fwrite(text, 1, length, f);
    return fclose(f) == 0 && n == length ? 0 : -1;
}
