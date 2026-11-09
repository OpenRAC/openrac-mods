// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Going Commando's parties: the same pages, session and co-op as Ratchet & Clank's (the play test's
// other files test those), and a party's description that names the game, so each game keeps to its
// own parties: Ratchet & Clank's untagged and digest-tagged ones are its own, Going Commando's only
// those tagged rac2.
#include "../test_support.hpp"

namespace mpt {
namespace {

void test_tags() {
    const std::string rac2 = mp::tagged_description("Going Commando", 0x12343FA9u, mp::kGameRac2);
    const std::string rac1 = mp::tagged_description("Pokitaru, 12 gold bolts", 0x12343FA9u);
    CHECK(rac2 == "Going Commando|3fa9/rac2");
    CHECK(mp::plain_description(rac2) == "Going Commando" && mp::description_tag(rac2) == "3fa9");
    CHECK(mp::description_game(rac2) == "rac2" && mp::description_game(rac1) == "rac1");
    CHECK(mp::description_game("New game").empty() && mp::description_game("").empty());
    CHECK(
        mp::tagged_description(std::string(60, 'x'), 7, mp::kGameRac2).size()
        <= mp::kMaxDescriptionLength
    );
    CHECK(
        mp::description_game(mp::tagged_description(std::string(60, 'x'), 7, mp::kGameRac2))
        == "rac2"
    );
    // Ratchet & Clank: its own tags and an older host's untagged party; never Going Commando's.
    CHECK(!mp::other_game(rac1, mp::kGameRac1) && !mp::other_game("Kerwan", mp::kGameRac1));
    CHECK(mp::other_game(rac2, mp::kGameRac1));
    // Going Commando: only its own.
    CHECK(!mp::other_game(rac2, mp::kGameRac2));
    CHECK(
        mp::other_game(rac1, mp::kGameRac2) && mp::other_game("Kerwan", mp::kGameRac2)
        && mp::other_game("", mp::kGameRac2)
    );
}

}  // namespace

void run_rac2_tests() {
    test_tags();
}

}  // namespace mpt
