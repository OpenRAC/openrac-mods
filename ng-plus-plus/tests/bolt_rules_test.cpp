// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++'s bolt rules alone (src/bolt_rules.cpp): the setting, the run scale, an enemy's toughness,
// the platinum share, the scaled amount and the split into gold and platinum coins.
//
// Expected values are the README's tables ("Platinum bolts and bolt scaling") and their arithmetic.

#include <cmath>

#include "bolt_rules.h"
#include "check.h"

namespace {

using namespace ng_plus_plus;

// The classes from the 1-bolt coin: 1, 5, 20, 50, 100, 500, 1,000 (platinum 5 to 5,000).
constexpr int kOne = 0;
constexpr int kFive = 1;
constexpr int kTwenty = 2;
constexpr int kFifty = 3;
constexpr int kHundred = 4;
constexpr int kFiveHundred = 5;
constexpr int kThousand = 6;

// Returns whether `a` and `b` are the same to a float's rounding.
bool near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

// Checks the setting's values, and the default for one the mod does not know.
void test_setting() {
    CHECK(bolt_scaling_of("off") == BoltScaling::Off);
    CHECK(bolt_scaling_of("normal") == BoltScaling::Normal);
    CHECK(bolt_scaling_of("generous") == BoltScaling::Generous);
    CHECK(bolt_scaling_of("") == BoltScaling::Normal);
    CHECK(bolt_scaling_of("lots") == BoltScaling::Normal);
}

// Checks the run scale: nothing before cycle 2, then x1.5, x2.5, x4 and x6 on; generous x1.5.
void test_run_scale() {
    CHECK(run_scale(0, BoltScaling::Normal) == 1.0f);
    CHECK(run_scale(1, BoltScaling::Normal) == 1.0f);
    CHECK(run_scale(2, BoltScaling::Normal) == 1.5f);
    CHECK(run_scale(3, BoltScaling::Normal) == 2.5f);
    CHECK(run_scale(4, BoltScaling::Normal) == 4.0f);
    CHECK(run_scale(5, BoltScaling::Normal) == 6.0f);
    CHECK(run_scale(9, BoltScaling::Normal) == 6.0f);
    CHECK(run_scale(1, BoltScaling::Generous) == 1.0f);
    CHECK(near(run_scale(2, BoltScaling::Generous), 2.25f));
    CHECK(near(run_scale(5, BoltScaling::Generous), 9.0f));
    CHECK(run_scale(5, BoltScaling::Off) == 1.0f);
}

// Checks an enemy's toughness from its own bolts: x1 up to 20, the square root past it, at most x2.
void test_toughness() {
    CHECK(toughness(0) == 1.0f);
    CHECK(toughness(-4) == 1.0f);
    CHECK(toughness(20) == 1.0f);
    CHECK(near(toughness(45), 1.5f));
    CHECK(near(toughness(80), 2.0f));
    CHECK(toughness(500) == 2.0f);
}

// Checks the platinum share: none before cycle 2, from the run's least at x1 to its most at x20.
void test_share() {
    CHECK(platinum_share(0, 20) == 0.0f);
    CHECK(near(platinum_share(1, 1), 0.05f));   // the first challenge mode: a taste
    CHECK(near(platinum_share(1, 20), 0.25f));
    CHECK(near(platinum_share(2, 1), 0.10f));
    CHECK(near(platinum_share(2, 20), 0.50f));
    CHECK(near(platinum_share(3, 20), 0.65f));
    CHECK(near(platinum_share(4, 1), 0.20f));
    CHECK(near(platinum_share(5, 1), 0.25f));
    CHECK(near(platinum_share(5, 20), 0.90f));
    CHECK(near(platinum_share(8, 20), 0.90f));
    CHECK(platinum_share(3, 0) == platinum_share(3, 1));    // before the first kill
    CHECK(platinum_share(3, 99) == platinum_share(3, 20));  // past the top
    // Halfway along the streak (x10 is 9 of 19 steps), halfway between 15% and 65%.
    CHECK(near(platinum_share(3, 10), 0.15f + 0.50f * 9.0f / 19.0f));
}

// Checks the scaled amount: rounded, nothing for none, at most the cap.
void test_scale_amount() {
    CHECK(scale_amount(1500, 5.0f) == 7500);
    CHECK(scale_amount(3, 1.5f) == 5);  // 4.5 rounds up
    CHECK(scale_amount(0, 6.0f) == 0);
    CHECK(scale_amount(-3, 6.0f) == 0);
    CHECK(scale_amount(1'000'000, 6.0f) == kMostDrop);
}

// Checks the split: the platinum part as platinum coins, the rest gold, the biggest first.
void test_split() {
    // 15,000 at 65%: 9,750 in platinum (5,000 + 2,500 + four 500s + 250), 5,250 in gold.
    CoinPlan plan = split(15'000, 0.65f, 64);
    CHECK(plan.platinum[kThousand] == 1);
    CHECK(plan.platinum[kFiveHundred] == 1);
    CHECK(plan.platinum[kHundred] == 4);
    CHECK(plan.platinum[kFifty] == 1);
    CHECK(plan.platinum[kTwenty] == 0);
    CHECK(plan.gold[kThousand] == 5);
    CHECK(plan.gold[kHundred] == 2);
    CHECK(plan.gold[kFifty] == 1);
    CHECK(plan.coins() == 15);
    CHECK(plan.worth() == 15'000);
    CHECK(plan.leftover == 0);

    // 1,000 at 10%: one platinum 100 and 900 in gold.
    plan = split(1000, 0.10f, -1);
    CHECK(plan.platinum[kTwenty] == 1);
    CHECK(plan.gold[kFiveHundred] == 1);
    CHECK(plan.gold[kHundred] == 4);
    CHECK(plan.worth() == 1000);

    // 7 at 50%: 3 is no whole platinum coin, so all gold.
    plan = split(7, 0.5f, -1);
    CHECK(plan.platinum[kOne] == 0);
    CHECK(plan.gold[kFive] == 1);
    CHECK(plan.gold[kOne] == 2);
    CHECK(plan.worth() == 7);

    // A share over 1 is all platinum.
    plan = split(100, 2.0f, -1);
    CHECK(plan.platinum[kTwenty] == 1);
    CHECK(plan.worth() == 100);

    // Three coins at most: 5,000, 2,500 and one 1,000; the rest does not fit.
    plan = split(15'000, 0.65f, 3);
    CHECK(plan.coins() == 3);
    CHECK(plan.platinum[kThousand] == 1);
    CHECK(plan.platinum[kFiveHundred] == 1);
    CHECK(plan.gold[kThousand] == 1);
    CHECK(plan.leftover == 6500);

    // Nothing.
    plan = split(0, 0.5f, -1);
    CHECK(plan.coins() == 0);
    CHECK(plan.leftover == 0);
}

}  // namespace

int main() try {
    test_setting();
    test_run_scale();
    test_toughness();
    test_share();
    test_scale_amount();
    test_split();
    return ng_plus_plus::test::result("ng_plus_plus_bolt_rules");
} catch (...) {
    // A test that throws has failed.
    return 1;
}
