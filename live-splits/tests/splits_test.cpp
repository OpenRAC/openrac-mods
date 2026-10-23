// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The live-splits mod's state machine alone: the planets the game plays, and the commands the mod
// sends LiveSplit for them.
//
// The two ways to split, the two ways to start the timer, and the game's own ways of surprising
// the mod: Ratchet dies and the level is loaded again, the player goes back to a planet he left,
// and the run ends when the game leaves Veldin for the last time.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "splits.hpp"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

using livesplit::Action;
using livesplit::Splits;
using livesplit::SplitWhen;
using livesplit::StartWho;

bool has(const std::vector<Action>& actions, Action action) {
    for (const Action a : actions) {
        if (a == action) {
            return true;
        }
    }

    return false;
}

std::size_t count(const std::vector<Action>& actions, Action action) {
    std::size_t n = 0;

    for (const Action a : actions) {
        if (a == action) {
            ++n;
        }
    }

    return n;
}

/* The planets of the route, in the order the game plays them. */
const int kRoute[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
constexpr std::size_t kRouteLength = sizeof(kRoute) / sizeof(kRoute[0]);

void test_reached() {
    Splits splits(SplitWhen::kReached, StartWho::kMod);
    // The first planet: the run begins.
    std::vector<Action> actions = splits.reached(0);
    CHECK(count(actions, Action::kReset) == 1);
    CHECK(count(actions, Action::kStart) == 1);
    CHECK(splits.started());
    // The same planet again (Ratchet died and the level was loaded again): nothing.
    actions = splits.reached(0);
    CHECK(actions.empty());

    // Every planet after it: a split.
    for (std::size_t i = 1; i < kRouteLength; ++i) {
        actions = splits.reached(kRoute[i]);
        CHECK(count(actions, Action::kSplit) == 1);
    }

    // Leaving a planet in the middle: nothing (the split was taken when it was reached).
    actions = splits.left(5);
    CHECK(actions.empty());
    // Leaving the last one: the run ends.
    actions = splits.left(18);
    CHECK(count(actions, Action::kSplit) == 1);
    // And once more: nothing.
    actions = splits.left(18);
    CHECK(actions.empty());
}

void test_left() {
    Splits splits(SplitWhen::kLeft, StartWho::kMod);
    // The first planet: the run begins.
    std::vector<Action> actions = splits.reached(0);
    CHECK(count(actions, Action::kReset) == 1);
    CHECK(count(actions, Action::kStart) == 1);
    // Reaching the next planet: nothing (the split is taken when the planet is left).
    actions = splits.reached(1);
    CHECK(actions.empty());
    // Leaving the first planet: the split.
    actions = splits.left(0);
    CHECK(count(actions, Action::kSplit) == 1);
    // Leaving it again (the player went back and left once more): nothing.
    actions = splits.left(0);
    CHECK(actions.empty());

    // The rest of the route: a split a planet.
    for (std::size_t i = 1; i < kRouteLength; ++i) {
        actions = splits.reached(kRoute[i]);
        CHECK(actions.empty());
        actions = splits.left(kRoute[i]);
        CHECK(count(actions, Action::kSplit) == 1);
    }
}

void test_by_hand() {
    Splits splits(SplitWhen::kReached, StartWho::kRunner);
    // The first planet: the run begins, but the runner starts the timer.
    std::vector<Action> actions = splits.reached(0);
    CHECK(actions.empty());
    CHECK(splits.started());
    // The planets after it: a split.
    actions = splits.reached(1);
    CHECK(count(actions, Action::kSplit) == 1);
    // Leaving the last one: the run ends.
    actions = splits.left(18);
    CHECK(count(actions, Action::kSplit) == 1);
}

void test_odd_levels() {
    Splits splits(SplitWhen::kReached, StartWho::kMod);
    // A level that is not a planet: nothing.
    CHECK(splits.reached(-1).empty());
    CHECK(splits.reached(19).empty());
    CHECK(splits.reached(1000).empty());
    CHECK(!splits.started());
    // Leaving a level that is not a planet: nothing.
    CHECK(splits.left(-1).empty());
    CHECK(splits.left(19).empty());
    // The first planet still starts the run.
    CHECK(count(splits.reached(0), Action::kStart) == 1);
}

void test_a_run_from_a_save() {  // The player loads a save and starts on another planet: the run begins there.
    Splits splits(SplitWhen::kReached, StartWho::kMod);
    std::vector<Action> actions = splits.reached(9);
    CHECK(count(actions, Action::kReset) == 1);
    CHECK(count(actions, Action::kStart) == 1);
    actions = splits.reached(10);
    CHECK(count(actions, Action::kSplit) == 1);
}

void test_a_new_game() {
    // The player goes back to the title and begins a new game: the run begins again.
    Splits splits(SplitWhen::kReached, StartWho::kMod);
    std::vector<Action> actions = splits.reached(0);
    CHECK(count(actions, Action::kStart) == 1);
    actions = splits.reached(3);
    CHECK(count(actions, Action::kSplit) == 1);
    // A new game: Veldin again, the first planet of the run.
    actions = splits.reached(0);
    CHECK(count(actions, Action::kReset) == 1);
    CHECK(count(actions, Action::kStart) == 1);
    // And the route goes on from there.
    actions = splits.reached(1);
    CHECK(count(actions, Action::kSplit) == 1);
    // The same planet again (Ratchet died): nothing.
    actions = splits.reached(1);
    CHECK(actions.empty());
}

void test_the_same_planet_again() {
    // The player leaves a planet and comes back to it: the run goes on.
    Splits splits(SplitWhen::kReached, StartWho::kMod);
    CHECK(count(splits.reached(0), Action::kStart) == 1);
    CHECK(count(splits.reached(1), Action::kSplit) == 1);
    CHECK(splits.left(1).empty());
    // Back to the first planet: the run has already been there, so it begins again. (The player
    // went back to the title and began a new game.)
    std::vector<Action> actions = splits.reached(0);
    CHECK(count(actions, Action::kReset) == 1);
    CHECK(count(actions, Action::kStart) == 1);
}

void test_format_time() {
    /* The time as LiveSplit's parser reads it. */
    CHECK(livesplit::format_time(0.0) == "0:00.00");
    CHECK(livesplit::format_time(1.5) == "0:01.50");
    CHECK(livesplit::format_time(61.25) == "1:01.25");
    CHECK(livesplit::format_time(3661.5) == "61:01.50");
    CHECK(livesplit::format_time(3661.999) == "61:02.00");
    CHECK(livesplit::format_time(-1.0) == "0:00.00");
}

}  // namespace

int main() {
    test_reached();
    test_left();
    test_by_hand();
    test_odd_levels();
    test_a_run_from_a_save();
    test_a_new_game();
    test_the_same_planet_again();
    test_format_time();
    std::puts("live splits: all passed");
    return 0;
}
