/* Gold-case prompts keep retail's one price argument and fit its HUD text buffer. */
#ifndef GOLD_CASE_PROMPT_H
#define GOLD_CASE_PROMPT_H
#include <stdio.h>
#include <string.h>

static void gold_case_prompt(char* out, size_t size, const char* label, const char* price) {
    char name[68];
    size_t n = 0;
    for (size_t i = 0; label[i] && n < sizeof name - 1; ++i) {
        /* Retail searches for %d before calling sprintf. Do not let an item or
         * recipient name introduce another placeholder, including inside %%. */
        const char* piece = label[i] == '%' ? "percent" : NULL;
        if (piece) {
            size_t len = strlen(piece);
            if (n + len >= sizeof name) {
                break;
            }
            memcpy(name + n, piece, len);
            n += len;
        } else {
            name[n++] = label[i];
        }
    }
    name[n] = 0;
    snprintf(out, size, "Buy %s for %s bolts + 4 Gold Bolts", name, price);
}
#endif
