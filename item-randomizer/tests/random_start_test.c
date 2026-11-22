#include "rando.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    int seen[LEVEL_COUNT] = {0}, planets = 0, vendor_starts = 0;
    for (int d=0; d<3; ++d) for (int mode=0; mode<3; ++mode) for (int seed=0; seed<30; ++seed) {
        rando_settings s = {0}; s.seed=seed; s.difficulty=d; s.random_start=1;
        s.keep_start=seed&1; s.pools=mode==0 ? POOL_EXTENDED_ALL : mode==1 ? POOL_ALL : 0;
        rando_plan a,b; char code[RANDO_CODE_SIZE]; rando_settings decoded={0};
        rando_encode(&s,code); assert(strncmp(code,"R:",2)==0);
        assert(rando_decode(code,&decoded) && decoded.random_start);
        assert(rando_solve(&s,&a)==0 && rando_solve(&decoded,&b)==0);
        assert(a.start_level==b.start_level && !memcmp(a.reward,b.reward,sizeof a.reward));
        assert(a.start_level>0 && a.start_level<LEVEL_VELDIN2);
        assert(rando_start_safe(&a) && rando_beatable(&a,0) && !rando_stranded(&a));
        seen[a.start_level]=1;
        if (a.start_level == LEVEL_GASPAR) ++vendor_starts;
        int coords[LEVEL_COUNT] = {0};
        for (size_t i=0; i<rando_location_count; ++i) {
            if (a.reward[i].kind==REWARD_PLANET) ++coords[a.reward[i].value];
        }
        for (int i=1; i<LEVEL_COUNT; ++i) assert(coords[i]==1);
        assert(a.reward[1].kind==REWARD_PLANET && a.reward[1].value==a.start_level);
    }
    for (int i=1;i<LEVEL_COUNT;++i) planets+=seen[i];
    assert(planets>2 && vendor_starts>0);
    rando_settings s={0}; rando_plan plan; s.pools=POOL_ALL;
    assert(rando_solve(&s,&plan)==0 && plan.start_level==LEVEL_VELDIN);
    puts("random starts: safe, beatable, deterministic across 270 seeds/settings");
    return 0;
}
