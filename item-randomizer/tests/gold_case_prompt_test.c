#include <stdlib.h>

#include "gold_case_prompt.h"
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    char text[120], formatted[128];
    gold_case_prompt(text, sizeof text, "Gold Bolt", "%d");
    CHECK(!strcmp(text, "Buy Gold Bolt for %d bolts + 4 Gold Bolts"));
    /* Match retail's conversion to a comma-separated price string. */
    char* price = strstr(text, "%d");
    CHECK(price);
    price[1] = 's';
    snprintf(formatted, sizeof formatted, text, "10,000");
    CHECK(!strcmp(formatted, "Buy Gold Bolt for 10,000 bolts + 4 Gold Bolts"));
    gold_case_prompt(text, sizeof text, "Aridia Coordinates", "%d");
    CHECK(strstr(text, "Aridia Coordinates"));
    gold_case_prompt(text, sizeof text, "Other Game Item for Player %d", "%d");
    CHECK(strstr(text, "Player percentd") && strstr(text, "%d") == strrchr(text, '%'));
    char long_name[300];
    memset(long_name, 'X', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = 0;
    gold_case_prompt(text, sizeof text, long_name, "%d");
    CHECK(strlen(text) < 110 && strstr(text, " + 4 Gold Bolts"));
    gold_case_prompt(text, sizeof text, "Devastator", "2,500");
    CHECK(!strcmp(text, "Buy Devastator for 2,500 bolts + 4 Gold Bolts"));
    return 0;
}
