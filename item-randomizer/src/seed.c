/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's seed code and random numbers (rando.h). The code packs the settings and
 * the seed into text so that a shared code reproduces a whole game; the random numbers are
 * SplitMix64, written out here so that a seed gives the same placement on every system. Both use
 * integer arithmetic only, with no dependence on the platform's libc random functions.
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

/* ---- constants ---- */

/* The code's layout (see the table above), counted in bits from the lowest bit of the code. */
#define CODE_CHARS 12            /* characters in a code, not counting hyphens */
#define CODE_GROUP 4             /* characters between hyphens */
#define CODE_CHAR_BITS 5         /* one base 32 character */
#define CODE_CHAR_MASK 31u       /* the value of one base 32 character */
#define CODE_BITS 60             /* CODE_CHARS * CODE_CHAR_BITS */
#define CODE_CHECKSUM_BITS 8     /* the checksum below the payload */
#define CODE_CHECKSUM_MASK 0xFFu /* the checksum's own bits */
#define CODE_FORMAT_SHIFT 48     /* the format sits at 48..51 of the payload */
#define CODE_DIFFICULTY_SHIFT 46 /* the difficulty at 46..47 */
#define CODE_DIFFICULTY_MASK 3u  /* two bits */
#define CODE_KEEP_SHIFT 45       /* keep the starting gear, one bit at 45 */
#define CODE_KEEP_MASK 1u        /* one bit */
#define CODE_POOLS_SHIFT 40      /* the pools at 40..44; the seed fills 0..39 */

/* The checksum's salt: "RAC1" in ASCII, mixed with the payload before the hash (assumed). */
#define CODE_CHECKSUM_SALT 0x52414331u

/* SplitMix64's constants (documented: Steele, Lea and Flood, "Fast splittable pseudorandom number
 * generators", 2014, as the reference implementation writes them). */
#define SPLITMIX_GAMMA 0x9E3779B97F4A7C15ull
#define SPLITMIX_MUL_1 0xBF58476D1CE4E5B9ull
#define SPLITMIX_MUL_2 0x94D049BB133111EBull

/* 64-bit FNV-1a's offset basis and prime (documented: the FNV hash's published parameters). */
#define FNV64_OFFSET 0xCBF29CE484222325ull
#define FNV64_PRIME 0x100000001B3ull

/* The longest "seed" word read; a longer one is cut here. */
#define WORD_MAX 64

/* Crockford's base 32: the digits and the letters but I, L, O and U. */
static const char k_alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

/* The difficulties' names, indexed by DIFFICULTY_*. */
static const char* const k_difficulties[] = {"relaxed", "standard", "expert"};

/* ---- random numbers ---- */

/* The next 64 random bits of the generator, advancing its state. */
uint64_t rando_next(rando_rng* r) {
    uint64_t z = (r->state += SPLITMIX_GAMMA);
    z = (z ^ (z >> 30)) * SPLITMIX_MUL_1;
    z = (z ^ (z >> 27)) * SPLITMIX_MUL_2;
    return z ^ (z >> 31);
}

/* A number in 0..n-1 (n > 0) without modulo bias. */
uint32_t rando_below(rando_rng* r, uint32_t n) {
    // The largest multiple of n below 2^32: draws above it are drawn again.
    const uint32_t limit = (uint32_t)(0x100000000ull - (0x100000000ull % n));

    // Draw until a draw is below the limit; a limit of 0 means every draw is fine.
    for (;;) {
        const uint32_t x = (uint32_t)(rando_next(r) >> 32);

        // A limit of 0 means n divides 2^32, so every draw is unbiased already.
        if (limit == 0 || x < limit) {
            return x % n;
        }
    }
}

/* ---- the seed code ---- */

/* The code's checksum byte for a payload: the top byte of one generator draw seeded with it. */
static uint8_t checksum(uint64_t payload) {
    rando_rng r = {payload ^ CODE_CHECKSUM_SALT};
    return (uint8_t)(rando_next(&r) >> 56);
}

/* The 52 bits above the checksum: format, difficulty, starting gear, pools and seed. */
static uint64_t payload_of(const rando_settings* s) {
    return ((uint64_t)RANDO_CODE_FORMAT << CODE_FORMAT_SHIFT)
           | ((uint64_t)(s->difficulty & CODE_DIFFICULTY_MASK) << CODE_DIFFICULTY_SHIFT)
           | ((uint64_t)(s->keep_start & CODE_KEEP_MASK) << CODE_KEEP_SHIFT)
           | ((uint64_t)(s->pools & POOL_ALL) << CODE_POOLS_SHIFT) | (s->seed & RANDO_SEED_MASK);
}

/* Writes the settings' code, "XXXX-XXXX-XXXX", into out. */
void rando_encode(const rando_settings* s, char out[RANDO_CODE_SIZE]) {
    const uint64_t payload = payload_of(s);
    const uint64_t bits = (payload << CODE_CHECKSUM_BITS) | checksum(payload);
    int o = 0;

    // One base 32 character at a time, from the top of the code.
    for (int i = 0; i < CODE_CHARS; ++i) {
        // A hyphen before the second and third group makes the code easier to read and copy.
        if (i == CODE_GROUP || i == 2 * CODE_GROUP) {
            out[o++] = '-';
        }

        out[o++] = k_alphabet[(bits >> (CODE_BITS - CODE_CHAR_BITS * (i + 1))) & CODE_CHAR_MASK];
    }

    out[o] = 0;
}

/* The value of one code character, with the look-alikes folded (o is 0, i and l are 1); -1 when
 * c is not a digit of the alphabet. */
static int digit(char c) {
    // Lower case reads as upper case.
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    // The alphabet has no O, so a typed O is the digit zero.
    if (c == 'O') {
        return 0;
    }

    // The alphabet has no I or L, so a typed one is the digit one.
    if (c == 'I' || c == 'L') {
        return 1;
    }

    const char* p = c != 0 ? strchr(k_alphabet, c) : 0;
    return p != 0 ? (int)(p - k_alphabet) : -1;
}

/* Reads a code into *s; 1 when text is one, 0 when it is not or its checksum or format is wrong.
 * *s is left alone on 0. */
int rando_decode(const char* text, rando_settings* s) {
    uint64_t bits = 0;
    int n = 0;

    // Each character of the text, read as a base 32 digit.
    for (const char* p = text; *p != 0; ++p) {
        // Hyphens and spaces only group the code for reading.
        if (*p == '-' || *p == ' ') {
            continue;
        }

        const int d = digit(*p);

        // A character that is no digit, or a thirteenth one, means this is not a code.
        if (d < 0 || n == CODE_CHARS) {
            return 0;
        }

        bits = (bits << CODE_CHAR_BITS) | (uint64_t)d;
        ++n;
    }

    // A code is exactly twelve characters.
    if (n != CODE_CHARS) {
        return 0;
    }

    const uint64_t payload = bits >> CODE_CHECKSUM_BITS;

    // A wrong checksum is a mistyped character; a wrong format is a code of a later mod.
    if ((uint8_t)(bits & CODE_CHECKSUM_MASK) != checksum(payload)
        || (payload >> CODE_FORMAT_SHIFT) != RANDO_CODE_FORMAT) {
        return 0;
    }

    const int difficulty = (int)((payload >> CODE_DIFFICULTY_SHIFT) & CODE_DIFFICULTY_MASK);

    // Two bits hold a fourth value that no difficulty uses.
    if (difficulty > DIFFICULTY_EXPERT) {
        return 0;
    }

    s->difficulty = (uint8_t)difficulty;
    s->keep_start = (uint8_t)((payload >> CODE_KEEP_SHIFT) & CODE_KEEP_MASK);
    s->pools = (uint8_t)((payload >> CODE_POOLS_SHIFT) & POOL_ALL);
    s->seed = payload & RANDO_SEED_MASK;
    return 1;
}

/* A word's seed: 64-bit FNV-1a of its bytes, folded to the seed's 40 bits. */
uint64_t rando_seed_of_text(const char* text) {
    uint64_t h = FNV64_OFFSET;

    // Each byte of the word goes into the hash.
    for (const unsigned char* p = (const unsigned char*)text; *p != 0; ++p) {
        h = (h ^ *p) * FNV64_PRIME;
    }

    return (h ^ (h >> RANDO_SEED_BITS)) & RANDO_SEED_MASK;
}

/* Reads the "seed" setting: 2 for a code (*s is the code's), 1 for a word (s->seed its hash, the
 * other fields as given), 0 for a blank setting (*s unchanged). */
int rando_parse_seed_setting(const char* text, rando_settings* s) {
    // A null setting is blank.
    if (text == 0) {
        return 0;
    }

    // Spaces around the setting are cut, so a pasted code or word with them reads the same.
    while (*text == ' ') {
        ++text;
    }

    size_t n = strlen(text);

    // Trailing spaces are cut too.
    while (n > 0 && text[n - 1] == ' ') {
        --n;
    }

    // A blank setting asks for a random seed.
    if (n == 0) {
        return 0;
    }

    char word[WORD_MAX];

    // A word longer than the buffer is cut, so a very long setting still gives a seed.
    if (n >= sizeof word) {
        n = sizeof word - 1;
    }

    memcpy(word, text, n);
    word[n] = 0;

    // A code carries its own settings and wins over the word reading.
    if (rando_decode(word, s)) {
        return 2;
    }

    s->seed = rando_seed_of_text(word);
    return 1;
}

/* ---- the difficulties ---- */

/* The difficulty's name for the log and the spoiler; "standard" for a value out of range. */
const char* rando_difficulty_name(int difficulty) {
    return difficulty >= 0 && difficulty <= DIFFICULTY_EXPERT ? k_difficulties[difficulty]
                                                              : "standard";
}

/* The difficulty named name (DIFFICULTY_*), or -1 when name is null or none. */
int rando_difficulty_of_name(const char* name) {
    // The difficulty whose name matches.
    for (int i = 0; name != 0 && i <= DIFFICULTY_EXPERT; ++i) {
        // The name matches this difficulty's: its index is the value.
        if (strcmp(name, k_difficulties[i]) == 0) {
            return i;
        }
    }

    return -1;
}
