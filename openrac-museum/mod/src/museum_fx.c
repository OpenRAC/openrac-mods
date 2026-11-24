/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The museum's drawn effects, near Ratchet only: the mod's own sprites (the .glb files in fx/,
 * written by tools/fx.py) as props, and heat haze.
 *
 *   - Water: foam that runs up and back on the waterline, sun glints on the sea.
 *   - Light: sunbeams through the house's windows with dust in them, a glow round each lamp and
 *     flame, fireflies by the trees, heat haze over the fires.
 *   - Exhibits: the gold tiles glow (brighter under Ratchet), the screens flicker and scan, bolts
 *     glint, and a crate flashes as it breaks.
 */
#include "museum_fx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "museum_data.h"

/* ---- The sprites ---- */

enum {
    SPR_GLOW,
    SPR_STAR,
    SPR_SHAFT,
    SPR_FOAM,
    SPR_RECT,
    SPR_TILE,
    SPR_COUNT
};

static const char* const MESH[SPR_COUNT] =
    {"fx/glow.glb", "fx/star.glb", "fx/shaft.glb", "fx/foam.glb", "fx/rect.glb", "fx/tile.glb"};
/* The most of each sprite at once: all mods share OPENRAC_PROPS_MAX props, so the museum keeps
 * under half of them. */
#define MOST_ANY 40
static const int MOST[SPR_COUNT] = {40, 10, 10, 22, 8, 4};

static const openrac_api* s_api;
static openrac_mod* s_mod;
static int s_ids[SPR_COUNT][MOST_ANY];
static int s_made[SPR_COUNT];  /* props made this level load */
static int s_used[SPR_COUNT];  /* posed this tick */
static int s_shown[SPR_COUNT]; /* posed last tick (the rest hidden already) */
static int s_ready;            /* the props of this level load are made */
static uint32_t s_ticks;
static float s_eye[3], s_fwd[3] = {1, 0, 0}, s_left[3] = {0, 1, 0}, s_up[3] = {0, 0, 1};

#define TICK (1.0f / 60.0f)
#define ALPHA_GAIN 2.0f
#define PI 3.14159265f

/* ---- Small helpers ---- */

static uint32_t s_seed = 777u;

static float rnd(void) {
    s_seed = s_seed * 1664525u + 1013904223u;
    return (float)(s_seed >> 8) / 16777216.0f;
}

/* A number from 0 to 1 that stays the same for one index. */
static float hash(int i) {
    uint32_t h = (uint32_t)i * 2654435761u;
    h ^= h >> 15;
    return (float)(h & 0xFFFF) / 65536.0f;
}

static float d2(const float a[3], float x, float y, float z) {
    const float dx = a[0] - x, dy = a[1] - y, dz = a[2] - z;
    return dx * dx + dy * dy + dz * dz;
}

static void cross(float out[3], const float a[3], const float b[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float norm(float v[3]) {
    const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) {
        v[0] /= l, v[1] /= l, v[2] /= l;
    }
    return l;
}

/* Poses the next free sprite of kind SPR: its square's x and y edges are X and Y (lengths
 * included), its middle AT, tinted RGBA and blended as BLEND. */
static void put(
    int spr,
    const float at[3],
    const float x[3],
    const float y[3],
    float r,
    float g,
    float b,
    float a,
    uint32_t blend
) {
    if (s_used[spr] >= s_made[spr] || a <= 0.003f) {
        return;
    }
    float z[3];
    cross(z, x, y);
    norm(z);
    openrac_prop_pose p;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.flags = OPENRAC_PROP_MATRIX | OPENRAC_PROP_TINT;
    p.scale = 1.0f;
    p.rotation[3] = 1.0f;
    for (int k = 0; k < 3; ++k) {
        p.matrix[k] = x[k];
        p.matrix[4 + k] = y[k];
        p.matrix[8 + k] = z[k];
        p.matrix[12 + k] = at[k];
        p.position[k] = at[k];
    }
    p.matrix[15] = 1.0f;
    /* ALPHA_GAIN: an added sprite's alpha goes up to 2 (0x100 as the GS's), a blended one's to 1. */
    const float alpha = a * ALPHA_GAIN;
    p.tint[0] = r, p.tint[1] = g, p.tint[2] = b, p.tint[3] = alpha > 4.0f ? 4.0f : alpha;
    p.blend = blend;
    const int id = s_ids[spr][s_used[spr]];
    s_api->prop_pose(s_mod, id, &p);
    if (s_used[spr] >= s_shown[spr]) {
        s_api->prop_hide(s_mod, id, 0); /* hidden since an earlier tick */
    }
    ++s_used[spr];
}

/* A sprite facing the camera, SIZE across. */
static void billboard(int spr, const float at[3], float size, float r, float g, float b, float a) {
    float x[3], y[3];
    for (int k = 0; k < 3; ++k) {
        x[k] = -s_left[k] * size;
        y[k] = s_up[k] * size;
    }
    put(spr, at, x, y, r, g, b, a, OPENRAC_PROP_ADD);
}

/* A sprite lying flat (on the floor or the water), its edges X and Y. */
static void flat(
    int spr,
    const float at[3],
    float xx,
    float xy,
    float yx,
    float yy,
    float r,
    float g,
    float b,
    float a,
    uint32_t blend
) {
    const float x[3] = {xx, xy, 0.0f}, y[3] = {yx, yy, 0.0f};
    put(spr, at, x, y, r, g, b, a, blend);
}

/* A long sprite from TOP along the unit way DIR, LENGTH long and WIDTH wide, turned about its
 * length to face the camera as well as it can. */
static void beam(
    int spr,
    const float top[3],
    const float dir[3],
    float width,
    float length,
    float r,
    float g,
    float b,
    float a
) {
    float at[3], view[3], x[3], y[3];
    for (int k = 0; k < 3; ++k) {
        at[k] = top[k] + dir[k] * length * 0.5f;
        view[k] = at[k] - s_eye[k];
        y[k] = dir[k] * length;
    }
    cross(x, dir, view);
    if (norm(x) < 1e-4f) {
        return;
    }
    for (int k = 0; k < 3; ++k) {
        x[k] *= width;
    }
    put(spr, at, x, y, r, g, b, a, OPENRAC_PROP_ADD);
}

/* ---- Picking the nearest ---- */

typedef struct near_item {
    int index;
    float d2;
} near_item;

static int by_distance(const void* a, const void* b) {
    const float da = ((const near_item*)a)->d2, db = ((const near_item*)b)->d2;
    return da < db ? -1 : (da > db ? 1 : 0);
}

#define FLAMES_COUNT ((int)(sizeof MUSEUM_FLAMES / sizeof MUSEUM_FLAMES[0]))
static near_item s_near[FLAMES_COUNT];

/* The flames of KIND within RANGE of P, nearest first, at most MAX: their count, in s_near. */
static int nearest(int kind, const float p[3], float range, int max) {
    int n = 0;
    for (int i = 0; i < FLAMES_COUNT; ++i) {
        const museum_flame* f = &MUSEUM_FLAMES[i];
        if (f->kind != kind) {
            continue;
        }
        const float d = d2(p, f->x, f->y, f->z);
        if (d < range * range) {
            s_near[n].index = i;
            s_near[n].d2 = d;
            ++n;
        }
    }
    qsort(s_near, (size_t)n, sizeof s_near[0], by_distance);
    return n < max ? n : max;
}

/* ---- Water ---- */

#define FOAM_RANGE 55.0f
#define GLINTS 6

typedef struct glint {
    float at[3];
    float age, life, size;
} glint;

static glint s_glints[GLINTS + 4]; /* the last 4: bolt glints */

static void water(const float p[3], float t) {
    const int n = nearest(MUSEUM_FLAME_FOAM, p, FOAM_RANGE, MOST[SPR_FOAM]);
    for (int k = 0; k < n; ++k) {
        const int i = s_near[k].index;
        const museum_flame* f = &MUSEUM_FLAMES[i];
        const float ox = cosf(f->a), oy = sinf(f->a);
        const float phase = hash(i) * 2.0f * PI;
        /* A wave runs up the shore and back: the foam follows it, thinning as it runs back. */
        const float w = sinf(t * 0.9f + phase);
        const float out = 0.4f + 0.8f * w;
        /* Over the water's own waves (they reach about half a unit up). */
        const float at[3] = {f->x + ox * out, f->y + oy * out, f->z + 0.55f};
        const float steep = f->b;
        const float len = 7.0f + hash(i + 7) * 3.0f, wide = steep > 0.5f ? 2.8f : 2.4f;
        const float alpha =
            (steep > 0.5f ? 0.75f : 0.55f) * (0.55f + 0.45f * cosf(t * 0.9f + phase - 0.6f));
        flat(
            SPR_FOAM,
            at,
            -oy * len,
            ox * len,
            ox * wide,
            oy * wide,
            1.4f,
            1.4f,
            1.4f,
            alpha,
            OPENRAC_PROP_ALPHA
        );
    }
    /* Sun glints on the sea out from the shore: each a short twinkle, then another somewhere. */
    for (int g = 0; g < GLINTS; ++g) {
        glint* s = &s_glints[g];
        s->age += TICK;
        if (s->age >= s->life) {
            const int m = nearest(MUSEUM_FLAME_FOAM, p, 40.0f, 24);
            if (m == 0) {
                continue;
            }
            const museum_flame* f = &MUSEUM_FLAMES[s_near[(int)(rnd() * (float)m) % m].index];
            const float out = 3.0f + rnd() * 12.0f, side = (rnd() - 0.5f) * 6.0f;
            s->at[0] = f->x + cosf(f->a) * out - sinf(f->a) * side;
            s->at[1] = f->y + sinf(f->a) * out + cosf(f->a) * side;
            s->at[2] = f->z + 0.12f;
            s->age = 0.0f;
            s->life = 0.35f + rnd() * 0.6f;
            s->size = 0.5f + rnd() * 0.6f;
        }
        const float a = sinf(PI * s->age / s->life);
        billboard(SPR_STAR, s->at, s->size, 1.0f, 0.98f, 0.9f, 0.8f * a);
    }
}

/* ---- Light and air ---- */

#define HAZE_MOST 8

static void light(const float p[3], float t) {
    /* The lamps' and flames' glow, the lamps steady with a slight breath, the flames flickering. */
    int n = nearest(MUSEUM_FLAME_LAMP, p, 60.0f, 12);
    for (int k = 0; k < n; ++k) {
        const museum_flame* f = &MUSEUM_FLAMES[s_near[k].index];
        const float at[3] = {f->x, f->y, f->z};
        billboard(
            SPR_GLOW,
            at,
            2.4f,
            1.0f,
            0.78f,
            0.45f,
            0.45f + 0.05f * sinf(t * 2.0f + hash(s_near[k].index) * 6.0f)
        );
    }
    openrac_haze haze[HAZE_MOST];
    int hazes = 0;
    for (int kind = MUSEUM_FLAME_TORCH; kind <= MUSEUM_FLAME_FIRE; ++kind) {
        n = nearest(kind, p, 45.0f, 6);
        for (int k = 0; k < n; ++k) {
            const museum_flame* f = &MUSEUM_FLAMES[s_near[k].index];
            const int fire = kind == MUSEUM_FLAME_FIRE;
            const float flick =
                0.8f + 0.2f * sinf(t * 13.0f + (float)k) * sinf(t * 7.3f + (float)k * 2.0f);
            const float at[3] = {f->x, f->y, f->z + (fire ? 0.6f : 0.25f)};
            billboard(
                SPR_GLOW, at, fire ? 3.6f : 1.8f, 1.0f, 0.55f, 0.2f, (fire ? 0.6f : 0.5f) * flick
            );
            if (hazes < HAZE_MOST && s_near[k].d2 < 30.0f * 30.0f) {
                haze[hazes].position[0] = f->x, haze[hazes].position[1] = f->y;
                haze[hazes].position[2] = f->z + (fire ? 1.8f : 0.9f);
                haze[hazes].radius = fire ? 1.5f : 0.7f;
                haze[hazes].strength = fire ? 0.07f : 0.05f;
                ++hazes;
            }
        }
    }
    s_api->haze_set(s_mod, haze, hazes);
    /* Fireflies: three round each place, wandering and blinking. */
    n = nearest(MUSEUM_FLAME_FIREFLY, p, 35.0f, 4);
    for (int k = 0; k < n; ++k) {
        const int i = s_near[k].index;
        const museum_flame* f = &MUSEUM_FLAMES[i];
        for (int j = 0; j < 3; ++j) {
            const float s = hash(i * 3 + j) * 20.0f;
            const float at[3] =
                {f->x + sinf(t * 0.37f + s) * 2.4f + sinf(t * 0.9f + s * 2.0f) * 0.5f,
                 f->y + cosf(t * 0.29f + s * 1.3f) * 2.4f,
                 f->z + sinf(t * 0.51f + s) * 0.7f};
            const float blink = sinf(t * 1.9f + s * 3.0f);
            billboard(
                SPR_GLOW, at, 0.28f, 0.75f, 1.0f, 0.35f, blink > 0.0f ? blink * blink * 0.95f : 0.0f
            );
        }
    }
    /* Sunbeams through the windows, breathing a little, with dust drifting in the nearest. */
    const int beams = (int)(sizeof MUSEUM_BEAMS / sizeof MUSEUM_BEAMS[0]);
    near_item b[sizeof MUSEUM_BEAMS / sizeof MUSEUM_BEAMS[0]];
    int nb = 0;
    for (int i = 0; i < beams; ++i) {
        const museum_beam* m = &MUSEUM_BEAMS[i];
        const float mx = m->x + m->dx * m->length * 0.5f, my = m->y + m->dy * m->length * 0.5f,
                    mz = m->z + m->dz * m->length * 0.5f;
        const float d = d2(p, mx, my, mz);
        if (d < 70.0f * 70.0f) {
            b[nb].index = i, b[nb].d2 = d, ++nb;
        }
    }
    qsort(b, (size_t)nb, sizeof b[0], by_distance);
    for (int k = 0; k < nb; ++k) {
        const museum_beam* m = &MUSEUM_BEAMS[b[k].index];
        const float top[3] = {m->x, m->y, m->z}, dir[3] = {m->dx, m->dy, m->dz};
        beam(
            SPR_SHAFT,
            top,
            dir,
            m->width,
            m->length,
            1.0f,
            0.9f,
            0.68f,
            0.2f + 0.04f * sinf(t * 0.4f + (float)k)
        );
        if (k < 2 && b[k].d2 < 30.0f * 30.0f) {
            for (int j = 0; j < 6; ++j) {
                const float s = hash(b[k].index * 11 + j);
                float u = s + t * 0.012f;
                u -= floorf(u);
                u = 0.15f + 0.7f * u;
                float side[3];
                const float upv[3] = {0.0f, 0.0f, 1.0f};
                cross(side, dir, upv);
                norm(side);
                const float off = sinf(t * 0.3f + s * 9.0f) * m->width * 0.3f;
                const float at[3] =
                    {top[0] + dir[0] * m->length * u + side[0] * off,
                     top[1] + dir[1] * m->length * u + side[1] * off,
                     top[2] + dir[2] * m->length * u + sinf(t * 0.5f + s * 4.0f) * 0.3f};
                billboard(
                    SPR_GLOW,
                    at,
                    0.09f,
                    1.0f,
                    0.95f,
                    0.8f,
                    0.55f * (0.5f + 0.5f * sinf(t * 1.3f + s * 7.0f))
                );
            }
        }
    }
}

/* ---- The exhibits ---- */

#define SCAN_MOBYS 768
static openrac_moby s_mobys[SCAN_MOBYS];
/* The crates near Ratchet at the last look: their slot and place, alive. */
#define CRATES_MOST 48

typedef struct crate {
    int slot;
    float at[3];
} crate;

static crate s_crates[CRATES_MOST];
static int s_crate_count;
/* A crate's flash: where, and its age (seconds; past FLASH_LIFE none). */
#define FLASHES 4
#define FLASH_LIFE 0.3f

static struct {
    float at[3];
    float age;
} s_flashes[FLASHES];

static int is_crate(int oclass) {
    return oclass == 500 || oclass == 501 || oclass == 511;
}

static int is_bolt(int oclass) {
    return oclass == 13 || oclass == 14 || oclass == 1134;
}

static void crate_breaks(const float at[3]) {
    for (int k = 0; k < FLASHES; ++k) {
        if (s_flashes[k].age >= FLASH_LIFE) {
            memcpy(s_flashes[k].at, at, sizeof s_flashes[k].at);
            s_flashes[k].at[2] += 0.5f;
            s_flashes[k].age = 0.0f;
            break;
        }
    }
    openrac_particle_spawn sp;
    memset(&sp, 0, sizeof sp);
    sp.size = sizeof sp;
    sp.texture = -1;
    sp.blend = -1;
    sp.kind = OPENRAC_PARTICLE_SPARK;
    sp.particle_size = 26000.0f;
    sp.life_ticks = 28;
    sp.color = 0x4F40A0FFu;
    sp.color_end = 0x1F00307Fu;
    for (int k = 0; k < 10; ++k) {
        const float a = (float)k * 0.628f + rnd() * 0.3f;
        sp.position[0] = at[0], sp.position[1] = at[1], sp.position[2] = at[2] + 0.5f;
        sp.velocity[0] = cosf(a) * 0.1f, sp.velocity[1] = sinf(a) * 0.1f,
        sp.velocity[2] = 0.08f + rnd() * 0.08f;
        s_api->particle_spawn_ex(s_mod, &sp);
    }
}

static void look_at_mobys(const float p[3]) {
    const int n = s_api->moby_list(s_mobys, SCAN_MOBYS);
    const int count = n < SCAN_MOBYS ? n : SCAN_MOBYS;
    crate now[CRATES_MOST];
    int now_count = 0;
    int bolts[16], bolt_count = 0;
    for (int i = 0; i < count; ++i) {
        const openrac_moby* m = &s_mobys[i];
        if ((m->flags & OPENRAC_MOBY_DEAD) || m->state >= 0x80u) {
            continue;
        }
        const float d = d2(p, m->position[0], m->position[1], m->position[2]);
        if (is_crate(m->oclass) && d < 25.0f * 25.0f && now_count < CRATES_MOST) {
            now[now_count].slot = m->slot;
            memcpy(now[now_count].at, m->position, sizeof now[now_count].at);
            ++now_count;
        } else if (is_bolt(m->oclass) && d < 18.0f * 18.0f && bolt_count < 16
                   && !(m->flags & OPENRAC_MOBY_HIDDEN)) {
            bolts[bolt_count++] = i;
        }
    }
    /* A crate seen last time and gone now broke (Ratchet is near: it did not go out of reach). */
    for (int k = 0; k < s_crate_count; ++k) {
        int still = 0;
        for (int j = 0; j < now_count && !still; ++j) {
            still = now[j].slot == s_crates[k].slot;
        }
        if (!still
            && d2(p, s_crates[k].at[0], s_crates[k].at[1], s_crates[k].at[2]) < 20.0f * 20.0f) {
            crate_breaks(s_crates[k].at);
        }
    }
    memcpy(s_crates, now, sizeof now[0] * (size_t)now_count);
    s_crate_count = now_count;
    /* Now and then a bolt glints. */
    if (bolt_count > 0) {
        for (int g = GLINTS; g < GLINTS + 4; ++g) {
            glint* s = &s_glints[g];
            if (s->age < s->life || rnd() > 0.35f) {
                continue;
            }
            const openrac_moby* m = &s_mobys[bolts[(int)(rnd() * (float)bolt_count) % bolt_count]];
            s->at[0] = m->position[0], s->at[1] = m->position[1], s->at[2] = m->position[2] + 0.3f;
            s->age = 0.0f;
            s->life = 0.3f + rnd() * 0.2f;
            s->size = 0.7f + rnd() * 0.4f;
            break;
        }
    }
}

static void exhibits(const float p[3], int station, float t) {
    /* The gold tiles: a slow glow, bright and quick under Ratchet. */
    const int stations = (int)(sizeof MUSEUM_STATIONS / sizeof MUSEUM_STATIONS[0]);
    for (int i = 0; i < stations; ++i) {
        const museum_station* st = &MUSEUM_STATIONS[i];
        if (d2(p, st->x, st->y, st->z) > 22.0f * 22.0f) {
            continue;
        }
        const float at[3] = {st->x, st->y, st->z + 0.12f};
        const float a =
            i == station ? 0.7f + 0.25f * sinf(t * 6.0f) : 0.25f + 0.1f * sinf(t * 2.0f + (float)i);
        flat(SPR_TILE, at, 2.1f, 0.0f, 0.0f, 2.1f, 1.0f, 0.85f, 0.45f, a, OPENRAC_PROP_ADD);
    }
    /* The screens: a faint flicker, and a band of light running down. */
    const int screens = (int)(sizeof MUSEUM_SCREENS / sizeof MUSEUM_SCREENS[0]);
    for (int i = 0; i < screens; ++i) {
        const museum_screen* s = &MUSEUM_SCREENS[i];
        if (d2(p, s->x, s->y, s->z) > 20.0f * 20.0f) {
            continue;
        }
        const float nx = cosf(s->face * PI / 180.0f), ny = sinf(s->face * PI / 180.0f);
        const float x[3] = {-ny * s->w, nx * s->w, 0.0f};
        const float y[3] = {0.0f, 0.0f, s->h};
        const float at[3] = {s->x + nx * 0.1f, s->y + ny * 0.1f, s->z};
        const float flicker = (s_ticks / 4u + (uint32_t)i * 7u) % 9u == 0u
                                  ? 0.16f
                                  : 0.06f + 0.02f * hash((int)(s_ticks / 3u) + i);
        put(SPR_RECT, at, x, y, 0.7f, 0.9f, 1.0f, flicker, OPENRAC_PROP_ADD);
        float u = t * 0.22f + hash(i);
        u -= floorf(u);
        const float band[3] = {0.0f, 0.0f, s->h * 0.09f};
        const float at2[3] =
            {at[0] + nx * 0.01f, at[1] + ny * 0.01f, s->z + s->h * (0.45f - 0.9f * u)};
        put(SPR_RECT, at2, x, band, 0.6f, 0.85f, 1.0f, 0.25f, OPENRAC_PROP_ADD);
    }
    /* Bolt glints, and a crate's flash as it breaks. */
    for (int g = GLINTS; g < GLINTS + 4; ++g) {
        glint* s = &s_glints[g];
        if (s->age >= s->life) {
            continue;
        }
        s->age += TICK;
        billboard(SPR_STAR, s->at, s->size, 1.0f, 0.85f, 0.4f, 0.9f * sinf(PI * s->age / s->life));
    }
    for (int k = 0; k < FLASHES; ++k) {
        if (s_flashes[k].age >= FLASH_LIFE) {
            continue;
        }
        s_flashes[k].age += TICK;
        const float a = 1.0f - s_flashes[k].age / FLASH_LIFE;
        billboard(SPR_GLOW, s_flashes[k].at, 3.0f + 2.0f * (1.0f - a), 1.0f, 0.8f, 0.45f, 0.8f * a);
    }
}

/* ---- The props' life ---- */

static void make_props(void) {
    for (int s = 0; s < SPR_COUNT; ++s) {
        while (s_made[s] < MOST[s]) {
            const int id = s_api->prop_create(s_mod, MESH[s]);
            if (id < 0) {
                break;
            }
            s_ids[s][s_made[s]++] = id;
        }
    }
    /* Not in a level yet: nothing could be made; the next tick tries again. */
    s_ready = s_made[SPR_GLOW] > 0;
}

void museum_fx_init(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
}

void museum_fx_new_level(void) {
    /* The last level's props went with it. */
    memset(s_made, 0, sizeof s_made);
    memset(s_shown, 0, sizeof s_shown);
    s_ready = 0;
    s_crate_count = 0;
    for (int k = 0; k < FLASHES; ++k) {
        s_flashes[k].age = FLASH_LIFE;
    }
    for (int g = 0; g < GLINTS + 4; ++g) {
        s_glints[g].age = s_glints[g].life = 1.0f;
    }
}

void museum_fx_tick(const float hero[3], int station) {
    if (!s_ready) {
        make_props();
        if (!s_ready) {
            return;
        }
    }
    ++s_ticks;
    const float t = (float)s_ticks * TICK;
    openrac_camera cam;
    memset(&cam, 0, sizeof cam);
    cam.size = sizeof cam;
    if (s_api->camera_view(&cam) == OPENRAC_HOOK_OK) {
        memcpy(s_eye, cam.eye, sizeof s_eye);
        memcpy(s_fwd, cam.forward, sizeof s_fwd);
        memcpy(s_left, cam.left, sizeof s_left);
        memcpy(s_up, cam.up, sizeof s_up);
    }
    memset(s_used, 0, sizeof s_used);
    if (s_ticks % 4u == 0u) {
        look_at_mobys(hero);
    }
    water(hero, t);
    light(hero, t);
    exhibits(hero, station, t);
    /* Sprites not posed this tick hide. */
    for (int s = 0; s < SPR_COUNT; ++s) {
        for (int k = s_used[s]; k < s_shown[s]; ++k) {
            s_api->prop_hide(s_mod, s_ids[s][k], 1);
        }
        s_shown[s] = s_used[s];
    }
}

void museum_fx_hide(void) {
    if (s_api == NULL) {
        return;
    }
    for (int s = 0; s < SPR_COUNT; ++s) {
        for (int k = 0; k < s_shown[s]; ++k) {
            s_api->prop_hide(s_mod, s_ids[s][k], 1);
        }
        s_shown[s] = 0;
    }
    if (s_ready) {
        s_api->haze_set(s_mod, NULL, 0);
    }
}
