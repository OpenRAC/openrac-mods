/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's seed code and random numbers (rando.h).
 *
 * A code is 60 bits written as 12 characters of Crockford's base 32 (0-9 and the letters but
 * I, L, O and U), in three groups: "7KQ2-9XMB-4TRA". From the top bit down:
 *
 *   4 bits  the format (RANDO_CODE_FORMAT): a later mod refuses a code it does not know
 *   2 bits  the difficulty
 *   1 bit   keep the starting gear
 *   5 bits  the pools
 *   40 bits the seed
 *   8 bits  a checksum of the 52 above (a mistyped character is caught)
 *
 * So a code says the whole game: two players with the same code get the same placement, whatever
 * their other settings.
 */
#include <string.h>

#include "rando.h"

static const char k_alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

uint64_t rando_next(rando_rng* r) {
    uint64_t z = (r->state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

uint32_t rando_below(rando_rng* r, uint32_t n) {
    /* The largest multiple of n below 2^32: draws above it are drawn again. */
    const uint32_t limit = (uint32_t)(0x100000000ull - (0x100000000ull % n));
    for (;;) {
        const uint32_t x = (uint32_t)(rando_next(r) >> 32);
        if (limit == 0 || x < limit) {
            return x % n;
        }
    }
}

static uint8_t checksum(uint64_t payload) {
    rando_rng r = {payload ^ 0x52414331u}; /* "RAC1" */
    return (uint8_t)(rando_next(&r) >> 56);
}

static uint64_t payload_of(const rando_settings* s) {
    return ((uint64_t)RANDO_CODE_FORMAT << 48) | ((uint64_t)(s->difficulty & 3u) << 46) |
           ((uint64_t)(s->keep_start & 1u) << 45) | ((uint64_t)(s->pools & POOL_ALL) << 40) |
           (s->seed & RANDO_SEED_MASK);
}

void rando_encode(const rando_settings* s, char out[RANDO_CODE_SIZE]) {
    const uint64_t payload = payload_of(s);
    const uint64_t bits = (payload << 8) | checksum(payload);
    int o = 0;
    for (int i = 0; i < 12; ++i) {
        if (i == 4 || i == 8) {
            out[o++] = '-';
        }
        out[o++] = k_alphabet[(bits >> (55 - 5 * i)) & 31u];
    }
    out[o] = 0;
}

static int digit(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c == 'O') {
        return 0;
    }
    if (c == 'I' || c == 'L') {
        return 1;
    }
    const char* p = c != 0 ? strchr(k_alphabet, c) : 0;
    return p != 0 ? (int)(p - k_alphabet) : -1;
}

int rando_decode(const char* text, rando_settings* s) {
    uint64_t bits = 0;
    int n = 0;
    for (const char* p = text; *p != 0; ++p) {
        if (*p == '-' || *p == ' ') {
            continue;
        }
        const int d = digit(*p);
        if (d < 0 || n == 12) {
            return 0;
        }
        bits = (bits << 5) | (uint64_t)d;
        ++n;
    }
    if (n != 12) {
        return 0;
    }
    const uint64_t payload = bits >> 8;
    if ((uint8_t)(bits & 0xFFu) != checksum(payload) || (payload >> 48) != RANDO_CODE_FORMAT) {
        return 0;
    }
    const int difficulty = (int)((payload >> 46) & 3u);
    if (difficulty > DIFFICULTY_EXPERT) {
        return 0;
    }
    s->difficulty = (uint8_t)difficulty;
    s->keep_start = (uint8_t)((payload >> 45) & 1u);
    s->pools = (uint8_t)((payload >> 40) & POOL_ALL);
    s->seed = payload & RANDO_SEED_MASK;
    return 1;
}

uint64_t rando_seed_of_text(const char* text) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (const unsigned char* p = (const unsigned char*)text; *p != 0; ++p) {
        h = (h ^ *p) * 0x100000001B3ull;
    }
    return (h ^ (h >> RANDO_SEED_BITS)) & RANDO_SEED_MASK;
}

int rando_parse_seed_setting(const char* text, rando_settings* s) {
    if (text == 0) {
        return 0;
    }
    while (*text == ' ') {
        ++text;
    }
    size_t n = strlen(text);
    while (n > 0 && text[n - 1] == ' ') {
        --n;
    }
    if (n == 0) {
        return 0;
    }
    char word[64];
    if (n >= sizeof word) {
        n = sizeof word - 1;
    }
    memcpy(word, text, n);
    word[n] = 0;
    if (rando_decode(word, s)) {
        return 2;
    }
    s->seed = rando_seed_of_text(word);
    return 1;
}

static const char* const k_difficulties[] = {"relaxed", "standard", "expert"};

const char* rando_difficulty_name(int difficulty) {
    return difficulty >= 0 && difficulty <= DIFFICULTY_EXPERT ? k_difficulties[difficulty] : "standard";
}

int rando_difficulty_of_name(const char* name) {
    for (int i = 0; name != 0 && i <= DIFFICULTY_EXPERT; ++i) {
        if (strcmp(name, k_difficulties[i]) == 0) {
            return i;
        }
    }
    return -1;
}
