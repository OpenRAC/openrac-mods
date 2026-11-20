#include <assert.h>
#include <stdio.h>
#include "rando.h"
#include "special_item_visual.h"
int main(void) {
    static const int models[SPECIAL_COUNT] = {1002, 37, 1428, 645, 1433};
    static const int icons[SPECIAL_COUNT] = {0xEAC8, 0xEAC8, 0xEAC8, 0xEAC9, 0xEAC9};
    static const int frames[SPECIAL_COUNT] = {0, 1, 2, 3, 4};
    for (int i = 0; i < SPECIAL_COUNT; ++i) {
        const char* name = rando_special_name(i);
        int icon = -1, frame = -1;
        assert(special_item_visual(name, &icon, &frame) == models[i]);
        assert(icon == icons[i] && frame == frames[i]);
        assert(special_item_acquisition(name) == models[i]);
    }
    int icon, frame;
    assert(special_item_visual("Zoomerator", &icon, &frame) == 1002);
    assert(icon == 0xEAC8 && frame == 0);
    assert(special_item_visual("Codebot", &icon, &frame) == 1428);
    assert(icon == 0xEAC8 && frame == 2);
    assert(special_item_visual(rando_item_name(ITEM_PERSUADER), &icon, &frame) == 407);
    assert(special_item_visual(NULL, &icon, &frame) == -1);
    assert(special_item_visual("Gold Bolt", &icon, &frame) == -1);
    puts("Canonical Items-menu names resolve to native models, icons and pickup animations.");
    return 0;
}
