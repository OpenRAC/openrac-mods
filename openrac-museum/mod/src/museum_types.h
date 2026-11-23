/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shapes of the museum's data (museum_data.h, written by tools/mod_data.py). */
#ifndef MUSEUM_TYPES_H
#define MUSEUM_TYPES_H

/* An exhibit's gold tile: where it lies (its middle, on the floor) and the words it shows. */
typedef struct museum_station {
    const char* key;
    float x, y, z;
    const char* title;
    int line_count;
    const char* lines[5];
} museum_station;

/* A round place on the floor: its middle, the floor's height and its radius. */
typedef struct museum_zone {
    float x, y, z, r;
} museum_zone;

/* A place of a live effect: a torch's embers, a campfire's embers and smoke, a waterfall's mist. */
enum { MUSEUM_FLAME_TORCH, MUSEUM_FLAME_FIRE, MUSEUM_FLAME_MIST };

typedef struct museum_flame {
    int kind;
    float x, y, z;
} museum_flame;

#endif
