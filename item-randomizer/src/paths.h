/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's data folder: its remembered seed and its spoiler logs.
 */
#ifndef ITEM_RANDOMIZER_PATHS_H
#define ITEM_RANDOMIZER_PATHS_H

#include <stddef.h>

/* The folder, made when missing: OPENRAC_MOD_DATA_DIR when set, else the launcher's per-user data
 * folder's mods/item-randomizer (never beside the game or the launcher's own files):
 *   macOS    ~/Library/Application Support/dev.openrac.launcher/mods/item-randomizer
 *   Linux    $XDG_DATA_HOME (or ~/.local/share)/dev.openrac.launcher/mods/item-randomizer
 *   Windows  %LOCALAPPDATA%\dev.openrac.launcher\mods\item-randomizer
 * 0 and the path in out; -1 when there is no home to put it in. */
int rando_data_dir(char* out, size_t size);
/* dir + the separator + name. */
void rando_join(char* out, size_t size, const char* dir, const char* name);
/* A whole small file into out (terminated, trailing whitespace cut); 0, or -1 when there is none. */
int rando_read_file(const char* path, char* out, size_t size);
/* text into path, replacing it; 0, or -1. */
int rando_write_file(const char* path, const char* text, size_t length);

#endif /* ITEM_RANDOMIZER_PATHS_H */
