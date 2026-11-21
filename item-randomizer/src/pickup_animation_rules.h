#ifndef PICKUP_ANIMATION_RULES_H
#define PICKUP_ANIMATION_RULES_H
/* Kalebo's rail Gold Bolt is save index 1 (Gold Bolt 2). Keep the player
 * grinding: reward presentation must not interrupt movement or reposition him. */
static int pickup_without_animation(const rando_location* location) {
    return location->kind == LOC_GOLD && location->level == LEVEL_KALEBO && location->key == 1;
}
#endif
