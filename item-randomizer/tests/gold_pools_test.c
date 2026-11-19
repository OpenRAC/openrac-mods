/* RAC1 extra pools: deterministic codes, complete currency, no case self-locks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "reward_pools.h"
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    rando_options o = {"pool-test", 1, 1, 1, 1, 1, 1, "standard", 1};
    rando_settings s, copy;
    CHECK(rando_settings_of(&o, NULL, 1, &s) == SEED_FROM_WORD);
    CHECK(s.pools & POOL_GOLD_WEAPONS);
    char code[RANDO_CODE_SIZE];
    rando_encode(&s, code);
    CHECK(rando_decode(code, &copy) && copy.pools == s.pools);
    o.seed = code;
    o.gold_weapons = 0;
    rando_settings_of(&o, NULL, 1, &s);
    CHECK(s.pools == copy.pools);
    int solved = 0;
    for (int extra = 0; extra < 4; extra += 2) {
        for (int pools = 0; pools <= POOL_ALL; ++pools) {
            for (int d = 0; d < 3; ++d) {
                rando_settings settings =
                    {(uint64_t)(700 + solved), (uint8_t)(pools | (extra << 5)), 1, (uint8_t)d};
                rando_plan p;
                CHECK(rando_solve(&settings, &p) == 0);
                CHECK(rando_beatable(&p, NULL) && rando_stranded(&p) == 0);
                int counts[5][64] = {{0}}, cases = 0, npcs = 0, gold = 0;
                for (size_t i = 0; i < rando_location_count; ++i) {
                    const rando_location* l = &rando_locations[i];
                    rando_reward reward = p.reward[i];
                    if (reward.kind == REWARD_GOLD_BOLT) {
                        ++gold;
                    }
                    if (!p.shuffled[i]) {
                        continue;
                    }
                    ++counts[l->vanilla.kind][l->vanilla.value];
                    --counts[reward.kind][reward.value];
                    if (l->kind == LOC_GOLD_CASE) {
                        ++cases;
                        CHECK(reward.kind != REWARD_GOLD_BOLT);
                    }
                    if (l->kind == LOC_FIXED) {
                        ++npcs;
                        CHECK(l->vanilla.kind == REWARD_PLANET);
                    }
                }
                CHECK(cases == ((extra & 2) ? 10 : 0));
                CHECK(npcs == ((extra & 1) ? 13 : 0));
                CHECK(gold == 40);
                for (int k = 0; k < 5; ++k) {
                    for (int v = 0; v < 64; ++v) {
                        CHECK(counts[k][v] == 0);
                    }
                }
                rando_encode(&settings, code);
                CHECK(rando_decode(code, &copy));
                CHECK(copy.pools == settings.pools);
                ++solved;
            }
        }
    }
    printf("reward pools: %d seed/difficulty combinations passed\n", solved);
    return 0;
}
