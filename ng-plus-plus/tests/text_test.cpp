// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The text resolver's substitution alone (src/text.cpp: substitute and retell): a game's message,
// the version's name in it and the tier's name give the message at the tier.
//
// The messages are made-up texts in the shape of the game's (a name alone, an ammo pickup's format
// with its count's placeholder, a description), in more than one language, not the game's own.

#include <string>

#include "check.h"
#include "text.h"

namespace {

using namespace ng_plus_plus;

// The room of the port's text slots, with the terminator.
constexpr std::size_t kRoom = 96;

// Checks that a name alone becomes the tier's whole name.
void test_a_name_becomes_the_tiers_name() {
    CHECK(substitute("Mega Lancer", "Mega Lancer", "Beta Heavy Lancer") == "Beta Heavy Lancer");
    CHECK(retell("Mega", {"Mega"}, "Alpha", kRoom) == "Alpha");
}

// Checks that the name inside a message is replaced and the rest kept, placeholders included.
void test_a_message_keeps_its_placeholders() {
    CHECK(
        substitute("%d rounds of Ultra Meteor Gun ammo", "Ultra Meteor Gun", "Beta Meteor Gun")
        == "%d rounds of Beta Meteor Gun ammo"
    );
    CHECK(
        substitute("Munitions pour Lance-Lave Ultra : %d", "Lance-Lave Ultra", "Beta Lance-Lave")
        == "Munitions pour Beta Lance-Lave : %d"
    );
    CHECK(
        substitute("1 Mega Lancer round", "Mega Lancer", "Alpha Heavy Lancer")
        == "1 Alpha Heavy Lancer round"
    );
}

// Checks the game's own messages: the name wrapped in colour codes, between its words too, and a
// name that ends in a space its message does not repeat (the English disc's texts).
void test_the_games_codes_inside_the_name() {
    CHECK(
        substitute("Got %d Ultra \x0cHeavy \x0cLancer\x08\x08 ammo", "Ultra Heavy Lancer",
                   "Beta Heavy Lancer")
        == "Got %d \x0c\x0c" "Beta Heavy Lancer\x08\x08 ammo"
    );
    CHECK(
        substitute("Got %d Ultra \x0cMeteor Gun\x08 ammo", "Ultra Meteor Gun", "Beta Meteor Gun")
        == "Got %d \x0c" "Beta Meteor Gun\x08 ammo"
    );
    CHECK(
        substitute("Got %d Ultra \x0cVaporizer\x08 ammo", "Ultra Vaporizer ", "Beta Vaporizer")
        == "Got %d \x0c" "Beta Vaporizer\x08 ammo"
    );
    CHECK(
        substitute("Got %d \x0cUltra Megaturret \x08" "ammo", "Ultra Megaturret ",
                   "Beta Megaturret Glove")
        == "Got %d \x0c" "Beta Megaturret Glove \x08" "ammo"
    );
    // A code never starts the name, and a name that is not there stays not there.
    CHECK(!substitute("Got %d Ultra \x0cMegarocket Cannon\x08 ammo", "Ultra Mega Rocket", "Beta"));
    CHECK(!substitute("Ultra\x0c", "Ultra Lancer", "Beta Lancer"));
    CHECK(!substitute("Mega Lancer", "   ", "Alpha"));
}

// Checks that only the first mention is replaced.
void test_only_the_first_mention_is_replaced() {
    CHECK(substitute("Lancer, Lancer", "Lancer", "Alpha Lancer") == "Alpha Lancer, Lancer");
}

// Checks that a message without the version's name is left alone, and an empty name names nothing.
void test_a_message_without_the_name_is_left_alone() {
    CHECK(!substitute("Ammo full", "Mega Lancer", "Alpha Heavy Lancer"));
    CHECK(!substitute("Mega Lancer", "", "Alpha Heavy Lancer"));
    CHECK(!substitute("", "Mega Lancer", "Alpha Heavy Lancer"));
    CHECK(!retell("Ammo full", {"Mega Lancer", "Lancer"}, "Alpha Heavy Lancer", kRoom));
    CHECK(!retell("", {""}, "Alpha", kRoom));
}

// Checks that the longest of the version's names is tried first, so a name inside another does not
// split it.
void test_the_longest_name_wins() {
    CHECK(
        retell("Heavy Lancer: %d", {"Lancer", "Heavy Lancer"}, "Alpha Heavy Lancer", kRoom)
        == "Alpha Heavy Lancer: %d"
    );
    CHECK(
        retell("A Lancer", {"Heavy Lancer", "Lancer"}, "Alpha Heavy Lancer", kRoom)
        == "A Alpha Heavy Lancer"
    );
}

// Checks that a result the slot cannot hold leaves the game's text.
void test_a_result_too_long_is_left_alone() {
    const std::string description = "The Lancer " + std::string(80, 'x');
    CHECK(!retell(description, {"Lancer"}, "Alpha Heavy Lancer", kRoom));
    CHECK(
        retell("The Lancer", {"Lancer"}, "Alpha Heavy Lancer", kRoom) == "The Alpha Heavy Lancer"
    );
}

}  // namespace

int main() try {
    test_a_name_becomes_the_tiers_name();
    test_a_message_keeps_its_placeholders();
    test_the_games_codes_inside_the_name();
    test_only_the_first_mention_is_replaced();
    test_a_message_without_the_name_is_left_alone();
    test_the_longest_name_wins();
    test_a_result_too_long_is_left_alone();
    return ng_plus_plus::test::result("ng_plus_plus_text");
} catch (...) {
    // A test that throws has failed.
    return 1;
}
