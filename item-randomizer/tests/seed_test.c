/* SPDX-License-Identifier: GPL-3.0-or-later
 * The item randomizer's seed code (src/seed.c): every setting survives a round trip, a code is
 * read whatever its case and hyphens, a mistyped code is refused, a word gives the same seed on
 * every system, and the random numbers are the same everywhere (fixed values).
 *
 * The fixed values (the code of a fixed setting, the hash of "ratchet", the generator's first
 * draw) are pinned from this implementation: a change to one is a change to every shared seed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rando.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define PINNED_HASH 0x8B545936u

static int same(const rando_settings* a, const rando_settings* b) {
    return a->seed == b->seed && a->pools == b->pools && a->keep_start == b->keep_start
           && a->difficulty == b->difficulty && a->start_weapons == b->start_weapons;
}

/* Random starting weapons: an N: or U: prefix (after R:) on the unchanged payload; a code with
 * one and a shuffled start is not a code. */
static void starting_weapon_codes(void) {
    rando_settings s = {0x123456789Aull, POOL_ALL, 1, DIFFICULTY_STANDARD, 0};
    char plain[RANDO_CODE_SIZE], code[RANDO_CODE_SIZE];
    rando_encode(&s, plain);
    for (int mode = RANDO_START_WEAPONS_NON_BROKEN; mode <= RANDO_START_WEAPONS_UNRESTRICTED; ++mode) {
        for (int random_start = 0; random_start <= 1; ++random_start) {
            s.start_weapons = (uint8_t)mode;
            s.random_start = (uint8_t)random_start;
            rando_encode(&s, code);
            const char* prefix = mode == RANDO_START_WEAPONS_NON_BROKEN
                                     ? (random_start ? "R:N:" : "N:")
                                     : (random_start ? "R:U:" : "U:");
            CHECK(strncmp(code, prefix, strlen(prefix)) == 0);
            CHECK(strcmp(code + strlen(prefix), plain) == 0 && strlen(code) < RANDO_CODE_SIZE);
            rando_settings back = {0};
            CHECK(rando_decode(code, &back) && same(&s, &back) && back.random_start == random_start);
            for (char* p = code; *p; ++p) *p = (char)(*p >= 'A' && *p <= 'Z' ? *p - 'A' + 'a' : *p);
            CHECK(rando_decode(code, &back) && same(&s, &back));
        }
    }
    /* A shuffled start writes no prefix; a prefix on a shuffled start's payload is refused. */
    s.keep_start = 0;
    s.random_start = 0;
    s.start_weapons = RANDO_START_WEAPONS_NON_BROKEN;
    rando_encode(&s, code);
    CHECK(code[1] != ':');
    char bad[RANDO_CODE_SIZE + 2];
    snprintf(bad, sizeof bad, "N:%s", code);
    rando_settings back = {0};
    CHECK(!rando_decode(bad, &back));
    /* A plain code has no starting weapon. */
    CHECK(rando_decode(plain, &back) && back.start_weapons == RANDO_START_WEAPONS_NONE);
}

/* A party's seed plays on any disc: the host's code gives every member its placement, and what
 * makes it (this core) knows no disc: the game's addresses are all in discs.h, which it never
 * includes. The placement of a fixed code is pinned, so a change that made it depend on anything
 * else shows here. */
static void a_code_places_the_same_everywhere(void) {
    rando_settings host = {0};
    host.seed = 0x12345678u;
    host.pools = POOL_ALL;
    host.legacy_items = 1;
    char code[RANDO_CODE_SIZE];
    rando_encode(&host, code);
    rando_settings a, b;
    CHECK(rando_decode(code, &a) == 1 && rando_decode(code, &b) == 1);
    rando_plan* one = calloc(1, sizeof *one);
    rando_plan* two = calloc(1, sizeof *two);
    CHECK(one != NULL && two != NULL);
    CHECK(rando_solve(&a, one) == 0 && rando_solve(&b, two) == 0);
    static char text_one[1 << 16], text_two[1 << 16];
    const size_t size = rando_spoiler(one, "1", text_one, sizeof text_one);
    CHECK(size > 0 && size < sizeof text_one);
    CHECK(rando_spoiler(two, "1", text_two, sizeof text_two) == size);
    CHECK(memcmp(text_one, text_two, size) == 0);
    unsigned hash = 2166136261u;

    for (size_t i = 0; i < size; ++i) {
        hash = (hash ^ (unsigned char)text_one[i]) * 16777619u;
    }

    printf("fixed spoiler hash %08x\n", hash);
    CHECK(hash == PINNED_HASH);
    free(one);
    free(two);
}

int main(void) {
    a_code_places_the_same_everywhere();
    starting_weapon_codes();
    /* Round trips: every pool set, both start choices, each difficulty, seeds across 40 bits. */
    rando_rng r = {1};
    int codes = 0;

    for (int pools = 0; pools <= (int)POOL_EXTENDED_ALL; ++pools) {
        for (int keep = 0; keep <= 1; ++keep) {
            for (int d = 0; d <= DIFFICULTY_EXPERT; ++d) {
                for (int k = 0; k < 40; ++k) {
                    const uint64_t seed = k == 0   ? 0
                                          : k == 1 ? RANDO_SEED_MASK
                                                   : rando_next(&r) & RANDO_SEED_MASK;
                    const rando_settings s = {seed, (uint8_t)pools, (uint8_t)keep, (uint8_t)d};
                    char code[RANDO_CODE_SIZE];
                    rando_encode(&s, code);
                    CHECK(strlen(code) == 14 && code[4] == '-' && code[9] == '-');
                    rando_settings back = {0, 0, 0, 0};
                    CHECK(rando_decode(code, &back) && same(&s, &back) && !back.legacy_items);
                    ++codes;
                }
            }
        }
    }

    CHECK(codes == 256 * 2 * 3 * 40);

    /* The format is fixed: this code is these settings on every system and in every later 1.x. */
    const rando_settings fixed = {0x123456789Aull, POOL_ALL, 1, DIFFICULTY_STANDARD, 1};
    char code[RANDO_CODE_SIZE];
    rando_encode(&fixed, code);
    printf("fixed code %s\n", code);
    CHECK(strcmp(code, "4ZRJ-6HB7-H6Q5") == 0);

    /* Read loosely: lower case, no hyphens, spaces, O for 0, I and L for 1. */
    rando_settings s;
    CHECK(rando_decode("4zrj6hb7h6q5", &s) && same(&s, &fixed));
    CHECK(rando_decode(" 4ZRJ 6HB7 H6Q5 ", &s) && same(&s, &fixed));
    rando_encode(&(rando_settings){0, 0, 0, 0}, code);
    char loose[RANDO_CODE_SIZE];
    memcpy(loose, code, sizeof loose);

    for (char* p = loose; *p; ++p) {
        if (*p == '0') {
            *p = 'o';
        } else if (*p == '1') {
            *p = 'l';
        }
    }

    CHECK(rando_decode(loose, &s) && s.seed == 0 && s.pools == 0);

    /* Refused: too short, too long, a letter outside the alphabet, a mistyped character (nearly
     * every one: the checksum is 8 bits). */
    CHECK(!rando_decode("4ZRJ-6HB7-H6Q", &s));
    CHECK(!rando_decode("4ZRJ-6HB7-H6Q5D", &s));
    CHECK(!rando_decode("4ZRJ-6HB7-H6QU", &s));
    CHECK(!rando_decode("", &s));
    int typos = 0, caught = 0;
    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    rando_encode(&fixed, code);

    for (int i = 0; i < 14; ++i) {
        if (code[i] == '-') {
            continue;
        }

        for (int a = 0; a < 32; ++a) {
            if (alphabet[a] == code[i]) {
                continue;
            }

            char typo[RANDO_CODE_SIZE];
            memcpy(typo, code, sizeof typo);
            typo[i] = alphabet[a];
            ++typos;
            caught += !rando_decode(typo, &s) || !same(&s, &fixed) ? 1 : 0;
            CHECK(!(rando_decode(typo, &s) && same(&s, &fixed)));
        }
    }

    CHECK(caught == typos);
    int refused = 0;

    for (int i = 0; i < 14; ++i) {
        for (int a = 0; a < 32 && code[i] != '-'; ++a) {
            char typo[RANDO_CODE_SIZE];
            memcpy(typo, code, sizeof typo);

            if (alphabet[a] == typo[i]) {
                continue;
            }

            typo[i] = alphabet[a];
            refused += !rando_decode(typo, &s);
        }
    }

    printf("typos refused: %d of %d\n", refused, typos);
    CHECK(refused * 100 >= typos * 97);

    /* A code of another format is refused. */
    {
        rando_settings any = {5, POOL_ALL, 0, 0};
        rando_encode(&any, code);
        code[0] = code[0] == '0' ? '2' : '0'; /* the format's bits */
        CHECK(!rando_decode(code, &s));
    }

    /* Words: the same seed everywhere, and a code typed in the seed setting wins. */
    CHECK(rando_seed_of_text("ratchet") == rando_seed_of_text("ratchet"));
    CHECK(rando_seed_of_text("ratchet") != rando_seed_of_text("Ratchet"));
    printf("seed of \"ratchet\": %llx\n", (unsigned long long)rando_seed_of_text("ratchet"));
    CHECK(rando_seed_of_text("ratchet") == 0xDD90E40830ull);
    rando_settings t = {0, POOL_GADGETS, 1, DIFFICULTY_EXPERT};
    CHECK(
        rando_parse_seed_setting("  ratchet  ", &t) == 1 && t.seed == rando_seed_of_text("ratchet")
        && t.pools == POOL_GADGETS && t.difficulty == DIFFICULTY_EXPERT
    );
    CHECK(rando_parse_seed_setting("4zrj-6hb7-h6q5", &t) == 2 && same(&t, &fixed));
    t.seed = 77;
    CHECK(
        rando_parse_seed_setting("", &t) == 0 && rando_parse_seed_setting("   ", &t) == 0
        && rando_parse_seed_setting(NULL, &t) == 0 && t.seed == 77
    );

    /* The random numbers are fixed values: the placement is the same on every system. */
    rando_rng q = {0};
    CHECK(rando_next(&q) == 0xE220A8397B1DCDAFull);
    rando_rng b = {42};
    int counts[3] = {0, 0, 0};

    for (int i = 0; i < 30000; ++i) {
        const uint32_t x = rando_below(&b, 3);
        CHECK(x < 3);
        ++counts[x];
    }

    CHECK(counts[0] > 9500 && counts[1] > 9500 && counts[2] > 9500);

    CHECK(strcmp(rando_difficulty_name(DIFFICULTY_RELAXED), "relaxed") == 0);
    CHECK(
        rando_difficulty_of_name("expert") == DIFFICULTY_EXPERT
        && rando_difficulty_of_name("hard") == -1
    );
    printf("seed_test: ok\n");
    return 0;
}
