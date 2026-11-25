#ifndef SHIP_SHORTCUT_H
#define SHIP_SHORTCUT_H
#include <ctype.h>
#include <stdint.h>
#include <string.h>
typedef struct ship_shortcut {
    uint16_t held, trigger, previous;
    int consuming;
} ship_shortcut;

/* The last named button is pressed after holding the others. */
static int ship_shortcut_parse(ship_shortcut* out, const char* text) {
    static const struct { const char* name; uint16_t bit; } buttons[] = {
        {"SELECT",1},{"L3",2},{"R3",4},{"START",8},{"UP",16},{"RIGHT",32},
        {"DOWN",64},{"LEFT",128},{"L2",256},{"R2",512},{"L1",1024},{"R1",2048},
        {"TRIANGLE",4096},{"CIRCLE",8192},{"CROSS",16384},{"SQUARE",32768}
    };
    ship_shortcut parsed = {0};
    const char* p = text;
    int count = 0;
    if (!p) p = "L1+R1+L2+R2+Start";
    while (*p && isspace((unsigned char)*p)) ++p;
    if (!*p) { *out = parsed; return 1; }
    for (;;) {
        char token[16]; unsigned n = 0;
        while (*p && *p != '+') {
            if (!isspace((unsigned char)*p)) {
                if (n + 1 >= sizeof token) return 0;
                token[n++] = (char)toupper((unsigned char)*p);
            }
            ++p;
        }
        token[n] = 0;
        if (!count && !strcmp(token,"OFF") && !*p) { *out = parsed; return 1; }
        uint16_t bit = 0;
        for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i)
            if (!strcmp(token,buttons[i].name)) bit = buttons[i].bit;
        if (!bit || ((parsed.held | parsed.trigger) & bit)) return 0;
        parsed.held |= parsed.trigger;
        parsed.trigger = bit;
        ++count;
        if (!*p) break;
        ++p;
    }
    if (count < 2) return 0;
    *out = parsed;
    return 1;
}

/* Console reports use active-low button bits. Consume the final button until
 * release, so a held Start does not become a delayed pause-menu press. */
static int ship_shortcut_report(ship_shortcut* key, uint8_t* report, int size, int allowed) {
    if (size < 2) {
        key->previous = 0; key->consuming = 0; return 0;
    }
    const uint16_t raw = (uint16_t)(report[0] | ((uint16_t)report[1] << 8));
    const uint16_t down = (uint16_t)~raw;
    const int match = allowed && key->trigger && (down & key->held) == key->held
        && (key->previous & key->held) == key->held
        && (down & key->trigger) && !(key->previous & key->trigger);
    key->previous = down;
    if (match) key->consuming = 1;
    if (!(down & key->trigger)) key->consuming = 0;
    if (key->consuming) {
        const uint16_t masked = raw | key->trigger;
        report[0] = (uint8_t)masked; report[1] = (uint8_t)(masked >> 8);
        static const unsigned pressure_bits[12] = {5,7,4,6,12,13,14,15,10,11,8,9};
        for (int i = 0; i < 12 && 6 + i < size; ++i)
            if (key->trigger & (1u << pressure_bits[i])) report[6 + i] = 0;
    }
    return match;
}
#endif
