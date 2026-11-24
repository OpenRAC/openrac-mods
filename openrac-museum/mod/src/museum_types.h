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

/* A place of a live effect: a torch's or a campfire's flames, a waterfall's mist, foam on the
 * waterline (a: the way out to sea in radians, b: 1 on steep rock), a lamp's head, fireflies. */
enum {
    MUSEUM_FLAME_TORCH, MUSEUM_FLAME_FIRE, MUSEUM_FLAME_MIST, MUSEUM_FLAME_FOAM, MUSEUM_FLAME_LAMP,
    MUSEUM_FLAME_FIREFLY
};

typedef struct museum_flame {
    int kind;
    float x, y, z;
    float a, b, c, d;
} museum_flame;

/* A sunbeam through a window: the window's middle, the beam's unit way, its width and length. */
typedef struct museum_beam {
    float x, y, z;
    float dx, dy, dz;
    float width, length;
} museum_beam;

/* An exhibit's screen: its middle, the way it faces (degrees about up), its width and height. */
typedef struct museum_screen {
    float x, y, z;
    float face, w, h;
} museum_screen;

#endif
