#ifndef SPECIAL_ITEM_VISUAL_H
#define SPECIAL_ITEM_VISUAL_H
#include <string.h>

/* Native Items-menu grid: kind 1 entries 0,1,2,4,5. Presentation only;
 * this table neither grants these items nor adds them to a shuffle pool. */
static int special_item_visual(const char* name, int* icon, int* frame) {
    static const struct {
        const char* name;
        int model, icon, frame;
    } entries[] =
        {{"Platinum Zoomerator", 1002, 0xEAC8, 0},
         {"Raritanium", 37, 0xEAC8, 1},
         {"CodeBot", 1428, 0xEAC8, 2},
         {"Premium Nanotech", 645, 0xEAC9, 3},
         {"Ultra Nanotech", 1433, 0xEAC9, 4},
         {"Persuader", 407, 0xEAC9, 0}};

    if (!name) {
        return -1;
    }
    /* Older labels used these spellings; keep their native display identity too. */
    if (strcmp(name, "Zoomerator") == 0) name = "Platinum Zoomerator";
    else if (strcmp(name, "Codebot") == 0) name = "CodeBot";
    for (unsigned i = 0; i < sizeof entries / sizeof entries[0]; ++i) {
        if (strcmp(name, entries[i].name) != 0) {
            continue;
        }
        *icon = entries[i].icon;
        *frame = entries[i].frame;
        return entries[i].model;
    }
    return -1;
}

/* Use the same native identity for displays and receiving motions. */
static int special_item_acquisition(const char* name) {
    int icon, frame;
    return special_item_visual(name, &icon, &frame);
}
#endif
