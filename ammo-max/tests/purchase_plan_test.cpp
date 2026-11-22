// SPDX-License-Identifier: MIT
// Keep test failures active in Release builds (where assert() would be disabled).
#include "purchase_plan.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

using namespace buy_all_ammo;

static void check(bool ok, const char* reason) {
    if (!ok) {
        std::cerr << "FAIL: " << reason << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main() {
    std::array<Candidate, kItems> entries{};
    entries[0] = {30, 40, 100, 2};
    entries[1] = {37, 0, 10, 10};
    entries[2] = {30, 40, 100, 2}; // duplicate vendor entry
    entries[3] = {25, 10, 10, 1}; // already full
    entries[4] = {45, 0, 10, 0}; // zero price is not an ammo purchase

    auto p = make_plan(entries, 5, 300);
    check(p.count == 2 && p.cost == 220, "normal fill cost");
    check(p.entries[0].item == 30 && p.entries[0].amount == 60, "first weapon");
    check(p.entries[1].item == 37 && p.entries[1].amount == 10, "second weapon");

    p = make_plan(entries, 5, 125);
    check(p.count == 1 && p.cost == 120 && p.entries[0].amount == 60,
          "partial funds spent in vendor order");

    p = make_plan(entries, 5, 125 + 40);
    check(p.count == 2 && p.cost == 160 && p.entries[1].amount == 4,
          "affordable partial ammo for later weapon");

    p = make_plan(entries, 5, 0);
    check(p.count == 0 && p.cost == 0, "no bolts");

    p = make_plan(entries, 5, 1);
    check(p.count == 0 && p.cost == 0, "not enough for a single round");

    p = make_plan(entries, -1, 300);
    check(p.count == 0 && p.cost == 0, "invalid candidate count");

    p = make_plan(entries, kItems + 1, 300);
    check(p.count == 0 && p.cost == 0, "oversized candidate count");

    entries[5] = {-1, 0, 10, 10};
    entries[6] = {56, 0, 10, 10};
    entries[7] = {23, -4, 10, 1};
    p = make_plan(entries, 8, 300);
    check(p.count == 2 && p.cost == 220, "invalid entries skipped");

    auto q = make_quote(entries, 8, 300);
    check(q.full_cost == 220 && q.refillable_types == 2 &&
          hint_for(q) == Hint::Buy, "HUD full-refill quote");
    q = make_quote(entries, 8, 125);
    check(q.affordable.cost == 120 && q.full_cost == 220 &&
          hint_for(q) == Hint::Partial, "HUD partial-refill quote");
    q = make_quote(entries, 8, 1);
    check(q.affordable.count == 0 && hint_for(q) == Hint::Insufficient,
          "HUD insufficient-bolts status");
    q = make_quote(entries, 8, 0);
    check(hint_for(q) == Hint::Insufficient, "HUD no-bolts status");
    entries[0].current = 100;
    entries[1].current = 10;
    q = make_quote(entries, 8, 300);
    check(q.refillable_types == 0 && hint_for(q) == Hint::Full,
          "HUD full-ammo status");

    // A heavily scaled ammo capacity and price cannot overflow the quote's full cost.
    entries[0] = {30, 0, 65535, 65535};
    entries[1] = {37, 0, 65535, 65535};
    q = make_quote(entries, 2, 100);
    check(q.full_cost == 8589672450ULL && q.affordable.cost == 0,
          "HUD full cost is 64-bit, purchase remains bounded");

    std::cout << "purchase_plan and HUD quote tests passed\n";
}
