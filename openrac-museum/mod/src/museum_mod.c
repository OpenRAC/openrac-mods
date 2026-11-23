/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The OpenRAC Museum's mod (README.md): what the museum's exhibits do in the game. Only on the
 * museum's planet (its pack folder is the map's), on the PAL disc the map is made for.
 *
 *   - The gold tiles: standing on one shows its exhibit's words in the HUD (museum_data.h).
 *   - The I-5 terminal: stepping on its pad puts the game in mode -1, Insomniac's debug menu; the
 *     game's own menu code leaves it (Select or R3) and goes back to play.
 *   - The effects case: on the teal pad the game's own particle effects start in the case, with a
 *     camera shake.
 *   - The 20-button codes: in the Decompilation room, a code that gives something says so.
 *   - Door 21/30: between 3 and 4 in the morning (the player's clock), it takes Ratchet to the
 *     tower's secret room.
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "museum_data.h"
#include "openrac/openrac_mod.h"

OPENRAC_MOD_DEFINE("openrac-museum", "1.0.0")

/* ---- The game's places (PAL, SCES-50916; rac1-decomp docs/kb/datamining.md) ---- */

/* The level the museum's planet is built on: Pokitaru's program. */
#define MUSEUM_LEVEL 11
/* Level 0's program's game mode (cinematic_mode): 0 play, 3 the pause menu, -1 the I-5 menu. */
#define GAME_MODE 0x0015F6A8u
#define GAME_MODE_I5 (-1)
#define GAME_MODE_PLAY 0
#define GAME_MODE_PAUSE 3
/* Level 0's program's debug menu; +0xA4 the mode the menu goes back to when it closes. */
#define DEBUG_MENU 0x0016C158u
#define DEBUG_MENU_RETURN_MODE 0xA4u
/* The flags the 20-button codes set: items owned and obtained (37 each), planets (19), special
 * items (6), skill points (30). */
static const struct { uint32_t address, size; } CODE_FLAGS[] = {
    {0x0013D5C8u, 37}, {0x0013D5F0u, 37}, {0x0013DE48u, 19}, {0x0013D490u, 6}, {0x0013D510u, 30},
};
#define CODE_FLAG_BYTES (37 + 37 + 19 + 6 + 30)

/* ---- The mod's own values ---- */

/* How far above or below a tile the hero may be and still stand on it (game units). */
#define FLOOR_SLACK 1.5f
/* A tile's reach from its middle (the tile is 1.6 units square). */
#define TILE_REACH 1.0f
/* Ticks between two effects while the hero stands on the effects pad. */
#define EFFECT_EVERY 25
/* Ticks a code's notice stays. */
#define NOTICE_TICKS 300
/* The HUD: the title's and the words' places (0 to 1 down the screen) and sizes. */
#define HUD_TOP 0.70f
#define HUD_STEP 0.045f

static const openrac_api* s_api;
static openrac_mod* s_mod;
/* Set when the running disc and port can run the museum (PAL, interface 1.35). */
static int s_ready;

/* Per level load (play_status.loads): whether this level is the museum. */
static uint32_t s_loads = 0xFFFFFFFFu;
static int s_in_museum;
/* The tile shown now, -1 none. */
static int s_station = -1;
/* The I-5 pad: set once the menu opened, until the hero steps off. */
static int s_i5_used;
/* The effects pad: ticks to the next effect, and which effect. */
static int s_effect_wait;
static int s_effect_kind;
/* The 20-button codes: the flags as last seen, whether they were read, the notice's ticks left. */
static uint8_t s_flags[CODE_FLAG_BYTES];
static int s_flags_read;
static int s_notice_ticks;
/* Door 21/30: set once it took the hero, until he leaves the door's place. */
static int s_door_used;

/* ---- Helpers ---- */

static uint8_t* mem(openrac_gaddr a) { return s_api->memory + a; }

static int32_t get_s32(openrac_gaddr a) {
    int32_t v;
    memcpy(&v, mem(a), 4);
    return v;
}

static void set_s32(openrac_gaddr a, int32_t v) { memcpy(mem(a), &v, 4); }

/* Whether the hero at P stands in zone Z (on its floor, within its radius plus SLACK). */
static int in_zone(const float p[3], const museum_zone* z, float slack) {
    const float dx = p[0] - z->x, dy = p[1] - z->y;
    return dx * dx + dy * dy <= (z->r + slack) * (z->r + slack) && p[2] > z->z - FLOOR_SLACK
           && p[2] < z->z + FLOOR_SLACK + 1.0f;
}

/* The tile the hero at P stands on, or -1. */
static int station_at(const float p[3]) {
    const int count = (int)(sizeof MUSEUM_STATIONS / sizeof MUSEUM_STATIONS[0]);
    for (int k = 0; k < count; ++k) {
        const museum_zone z = {MUSEUM_STATIONS[k].x, MUSEUM_STATIONS[k].y, MUSEUM_STATIONS[k].z, TILE_REACH};
        if (in_zone(p, &z, 0.0f)) {
            return k;
        }
    }
    return -1;
}

/* Shows tile K's words in the HUD, or takes them off for -1. */
static void show_station(int k) {
    if (k == s_station) {
        return;
    }
    s_station = k;
    if (k < 0) {
        s_api->hud_text_set(s_mod, NULL, 0);
        return;
    }
    const museum_station* st = &MUSEUM_STATIONS[k];
    openrac_hud_text lines[1 + MUSEUM_LINES_MAX];
    memset(lines, 0, sizeof lines);
    snprintf(lines[0].text, sizeof lines[0].text, "%s", st->title);
    lines[0].x = 0.5f;
    lines[0].y = HUD_TOP;
    lines[0].scale = 1.0f;
    lines[0].rgba[0] = 255, lines[0].rgba[1] = 200, lines[0].rgba[2] = 90, lines[0].rgba[3] = 255;
    lines[0].flags = OPENRAC_HUD_SHADOW;
    for (int i = 0; i < st->line_count; ++i) {
        openrac_hud_text* l = &lines[1 + i];
        snprintf(l->text, sizeof l->text, "%s", st->lines[i]);
        l->x = 0.5f;
        l->y = HUD_TOP + HUD_STEP * 1.3f + HUD_STEP * (float)i;
        l->scale = 1.0f;
        l->rgba[0] = 170, l->rgba[1] = 170, l->rgba[2] = 170, l->rgba[3] = 255;
        l->flags = OPENRAC_HUD_SMALL | OPENRAC_HUD_SHADOW;
    }
    s_api->hud_text_set(s_mod, lines, 1 + st->line_count);
}

/* Whether this level is the museum: its pack folder is the museum map's. */
static int is_museum(int level) {
    if (level != MUSEUM_LEVEL) {
        return 0;
    }
    char folder[1024];
    if (s_api->pack_folder(folder, sizeof folder) == 0) {
        return 0;
    }
    return strstr(folder, "museum") != NULL;
}

/* Takes the mod's words off the HUD. */
static void leave(void) { show_station(-1); }

/* ---- The stations ---- */

static void i5_terminal(const float p[3], int mode) {
    if (!in_zone(p, &MUSEUM_ZONE_I5, 0.0f)) {
        s_i5_used = 0;
        return;
    }
    if (s_i5_used || mode != GAME_MODE_PLAY) {
        return;
    }
    s_i5_used = 1;
    show_station(-1);
    set_s32(s_api->relocate_level_data(0, DEBUG_MENU) + DEBUG_MENU_RETURN_MODE, GAME_MODE_PLAY);
    set_s32(s_api->relocate_level_data(0, GAME_MODE), GAME_MODE_I5);
}

static void effects(const float p[3]) {
    if (!in_zone(p, &MUSEUM_ZONE_EFFECTS, 0.0f)) {
        s_effect_wait = 0;
        return;
    }
    if (s_effect_wait-- > 0) {
        return;
    }
    s_effect_wait = EFFECT_EVERY;
    const museum_zone* c = &MUSEUM_ZONE_EFFECTS_CASE;
    openrac_particle_spawn sp;
    memset(&sp, 0, sizeof sp);
    sp.size = sizeof sp;
    sp.texture = -1;
    sp.blend = -1;
    sp.position[0] = c->x, sp.position[1] = c->y, sp.position[2] = c->z + 1.2f;
    switch (s_effect_kind++ % 3) {
        case 0:   /* a burst of orange fading to red, and the camera shakes */
            sp.kind = OPENRAC_PARTICLE_BURST;
            sp.particle_size = 60000.0f;
            sp.life_ticks = 40;
            sp.color = 0x4F007FFFu;
            sp.color_end = 0x1FFFFFFFu;
            sp.velocity[2] = 0.1f;
            sp.flags = OPENRAC_PARTICLE_CHILDREN;
            s_api->particle_spawn_ex(s_mod, &sp);
            s_api->camera_shake(s_mod, 0, 0.4f, 20);
            break;
        case 1:   /* sparks falling from the top of the case */
            sp.kind = OPENRAC_PARTICLE_SPARK;
            sp.particle_size = 40000.0f;
            sp.life_ticks = 50;
            sp.color = 0x4F007FFFu;
            sp.color_end = 0x1F00007Fu;
            sp.flags = OPENRAC_PARTICLE_CHILDREN;
            for (int k = 0; k < 12; ++k) {
                const float a = (float)k * 0.5236f;
                sp.velocity[0] = cosf(a) * 0.08f, sp.velocity[1] = sinf(a) * 0.08f, sp.velocity[2] = 0.15f;
                s_api->particle_spawn_ex(s_mod, &sp);
            }
            break;
        default:  /* grey smoke rising */
            sp.kind = OPENRAC_PARTICLE_SMOKE;
            sp.particle_size = 40000.0f;
            sp.life_ticks = 90;
            sp.color = 0x7F606060u;
            sp.rot = -0.0004f;
            for (int k = 0; k < 4; ++k) {
                sp.position[2] = c->z + 0.6f + 0.3f * (float)k;
                sp.velocity[2] = 0.02f;
                s_api->particle_spawn_ex(s_mod, &sp);
            }
            break;
    }
}

/* Reads the flags the codes set into OUT, in CODE_FLAGS's order. */
static void read_flags(uint8_t* out) {
    for (size_t k = 0; k < sizeof CODE_FLAGS / sizeof CODE_FLAGS[0]; ++k) {
        memcpy(out, mem(s_api->relocate_data(CODE_FLAGS[k].address)), CODE_FLAGS[k].size);
        out += CODE_FLAGS[k].size;
    }
}

static void codes(const float p[3], int mode) {
    uint8_t now[CODE_FLAG_BYTES];
    if (s_notice_ticks > 0 && --s_notice_ticks == 0) {
        s_api->play_notice(s_mod, "");
    }
    if (!in_zone(p, &MUSEUM_ZONE_CODES, 0.0f)) {
        s_flags_read = 0;
        return;
    }
    read_flags(now);
    if (s_flags_read && mode == GAME_MODE_PAUSE) {
        for (int k = 0; k < CODE_FLAG_BYTES; ++k) {
            if (now[k] != 0 && s_flags[k] == 0) {
                s_api->play_notice(s_mod, "A 20-button code worked. What it gave is saved with your game.");
                s_notice_ticks = NOTICE_TICKS;
                break;
            }
        }
    }
    memcpy(s_flags, now, sizeof s_flags);
    s_flags_read = 1;
}

static void door21(const float p[3], int mode) {
    if (!in_zone(p, &MUSEUM_ZONE_DOOR21, 0.0f)) {
        s_door_used = 0;
        return;
    }
    if (s_door_used || mode != GAME_MODE_PLAY) {
        return;
    }
    const time_t t = time(NULL);
    const struct tm* local = localtime(&t);
    if (local == NULL || local->tm_hour != 3) {
        return;
    }
    s_door_used = 1;
    const float to[3] = {MUSEUM_ZONE_ARCHIVE.x, MUSEUM_ZONE_ARCHIVE.y, MUSEUM_ZONE_ARCHIVE.z};
    s_api->hero_place(s_mod, to, 0.0f, OPENRAC_PLACE_INVULNERABLE);
}

/* ---- Each tick ---- */

static void tick(void* user) {
    (void)user;
    openrac_play_status status;
    memset(&status, 0, sizeof status);
    status.size = sizeof status;
    if (s_api->play_status(&status) != OPENRAC_HOOK_OK) {
        return;
    }
    if (status.loads != s_loads) {
        s_loads = status.loads;
        s_in_museum = status.phase != OPENRAC_PLAY_LOADING && is_museum(status.level);
        s_station = -1;
        s_flags_read = 0;
        s_i5_used = 0;
    }
    if (!s_in_museum && status.level == MUSEUM_LEVEL && status.phase == OPENRAC_PLAY_PLAYING && s_loads == status.loads) {
        s_in_museum = is_museum(status.level);
    }
    if (!s_in_museum) {
        return;
    }
    openrac_pose pose;
    memset(&pose, 0, sizeof pose);
    pose.size = sizeof pose;
    if (s_api->hero_pose(&pose) != OPENRAC_HOOK_OK) {
        leave();
        return;
    }
    const int mode = get_s32(s_api->relocate_level_data(0, GAME_MODE));
    const float* p = pose.position;
    codes(p, mode);
    if (mode != GAME_MODE_PLAY) {
        show_station(-1);
        return;
    }
    show_station(station_at(p));
    i5_terminal(p, mode);
    effects(p);
    door21(p, mode);
}

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    if (strcmp(api->version, "pal") != 0) {
        api->fail(mod, "The OpenRAC Museum is made for the Europe disc (PAL).");
        return 1;
    }
    if (api->size < offsetof(openrac_api, pack_folder) + sizeof api->pack_folder) {
        api->fail(mod, "This OpenRAC is too old for the museum: update it.");
        return 1;
    }
    s_ready = 1;
    return api->on_frame(mod, tick, NULL);
}

OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    if (s_ready && s_station >= 0) {
        leave();
    }
}
