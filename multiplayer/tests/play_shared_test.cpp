// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The shared progress past the items (shared.hpp): gold weapons, skill points, the story's flags and
// the special items, merged upward through the host; the flags a player's own scene sets kept from a
// game still to play it; the mission rows the same in every game after a scripted mission, over a
// bad link too; and a late joiner taking what it lacks without a banner (progress.hpp).
#include "test_support.hpp"

namespace mpt {

namespace {

constexpr int kReadMs = static_cast<int>(mp::kProgressReadMs);

// A party of a host and two members, all in `level`.
struct Three : Party {
    explicit Three(int level = 4) {
        for (Player* p : players()) {
            p->game.now = {mp::kPhasePlaying, level, 2};
        }
    }

    void settle() {
        for (int i = 0; i < 4; ++i) {
            tick(players(), kReadMs);
        }
    }
};

std::uint8_t& flag(mp::SharedBits& bits, int level, int index) {
    return bits.flags[static_cast<std::size_t>(level) * mp::kFlagsPerLevel + static_cast<std::size_t>(index)];
}

void wire_round_trip() {
    mp::SharedBits bits;
    bits.gold_weapons[0] = bits.gold_weapons[39] = 1;
    bits.skills[31] = 1;
    flag(bits, 19, 15) = 1;
    flag(bits, 0, 0) = 1;
    bits.specials[127] = 1;
    mp::SharedBits out;
    const Bytes wire = mp::encode_shared(bits);
    CHECK(mp::decode_shared(wire, out) && out == bits);
    // Bits, so a full update stays small: 4 sections, 65 bytes of them.
    CHECK(wire.size() == 2 + 4 * 3 + 5 + 4 + 40 + 16);

    // Cut short anywhere: nothing taken.
    for (std::size_t n = 0; n < wire.size(); ++n) {
        mp::SharedBits kept;
        CHECK(!mp::decode_shared(Bytes(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(n)), kept));
    }

    // A section of the wrong length: refused. A tag unknown here: passed over.
    Bytes wrong = wire;
    wrong[4] = 9;
    CHECK(!mp::decode_shared(wrong, out));
    const Bytes unknown = {mp::kSharedKind, 2, 200, 0, 1, 0xFF, 2, 0, 4, 0xFF, 0, 0, 0};
    mp::SharedBits with_unknown;
    CHECK(mp::decode_shared(unknown, with_unknown));
    CHECK(with_unknown.skills[0] == 1 && with_unknown.skills[8] == 0);

    std::array<std::uint8_t, mp::kFlagsPerLevel> row{};
    row[3] = 0xFF;
    int level = -1;
    std::array<std::uint8_t, mp::kFlagsPerLevel> back{};
    CHECK(mp::decode_mission(mp::encode_mission(7, row.data()), level, back) && level == 7 && back == row);
    Bytes off_the_end = mp::encode_mission(7, row.data());
    off_the_end[1] = 20;
    CHECK(!mp::decode_mission(off_the_end, level, back));
    CHECK(!mp::decode_shared_ask({mp::kSharedAskKind, 20}, level));
}

void a_gold_weapon_and_a_skill_point_reach_everyone() {
    Three party;
    party.a.game.sections.gold_weapons[10] = 1;
    party.host.game.sections.skills[3] = 1;
    party.settle();

    for (Player* p : party.players()) {
        CHECK(p->game.sections.gold_weapons[10] == 1);
        CHECK(p->game.sections.skills[3] == 1);
    }

    // Silent: nothing was given as the game gives things.
    CHECK(party.a.game.given.empty() && party.host.game.given.empty() && party.b.game.given.empty());
}

void nothing_is_taken_and_a_spent_item_stays_spent() {
    Three party;
    party.a.game.sections.specials[7] = 1;
    party.settle();
    CHECK(party.host.game.sections.specials[7] == 1);
    CHECK(party.b.game.sections.specials[7] == 1);

    // The member spends it in a talk: the others keep theirs, and it does not come back to it.
    party.a.game.sections.specials[7] = 0;
    party.settle();
    CHECK(party.a.game.sections.specials[7] == 0);
    CHECK(party.host.game.sections.specials[7] == 1);

    // The host spends its own: a member's old word does not put it back.
    party.host.game.sections.specials[7] = 0;
    party.settle();
    CHECK(party.host.game.sections.specials[7] == 0);

    // What a game has and the host lacks is never cleared.
    party.b.game.sections.skills[1] = 1;
    party.host.game.sections.skills[2] = 1;
    party.settle();
    CHECK(party.b.game.sections.skills[1] == 1 && party.b.game.sections.skills[2] == 1);
    CHECK(party.a.game.sections.skills[1] == 1 && party.a.game.sections.skills[2] == 1);
}

void nanotech_upgrades_stay_each_players() {
    Three party;
    party.host.game.sections.specials[4] = 1;
    party.a.game.sections.specials[5] = 1;
    party.settle();
    CHECK(party.a.game.sections.specials[4] == 0);
    CHECK(party.host.game.sections.specials[5] == 0);
    CHECK(party.b.game.sections.specials[4] == 0 && party.b.game.sections.specials[5] == 0);
}

void a_flag_goes_up_and_comes_down_as_the_game_writes_it() {
    Three party;
    // A checkpoint a member reached: not a scene's. It is the party's, as 0xFF in every game.
    CHECK(!mp::scene_flag(4, 0));
    flag(party.a.game.sections, 4, 0) = 0xFF;
    party.settle();
    CHECK(flag(party.host.game.sections, 4, 0) == 0xFF);
    CHECK(flag(party.b.game.sections, 4, 0) == 0xFF);
}

void a_members_scene_flag_never_reaches_the_host() {
    // Level 3's Helga, talked to by a member first.
    Three party(3);
    CHECK(mp::scene_flag(3, 2));
    flag(party.a.game.sections, 3, 2) = 0xFF;
    party.settle();
    CHECK(flag(party.host.game.sections, 3, 2) == 0);
    CHECK(flag(party.b.game.sections, 3, 2) == 0);
    // Not from another level either: the host is still to play it there.
    for (Player* p : party.players()) {
        p->game.now = {mp::kPhasePlaying, 4, 3};
    }
    party.settle();
    CHECK(flag(party.host.game.sections, 3, 2) == 0);
    // Its own game keeps it.
    CHECK(flag(party.a.game.sections, 3, 2) == 0xFF);
}

void the_hosts_scene_flag_waits_for_a_member_to_leave_the_level() {
    Three party(3);
    flag(party.host.game.sections, 3, 2) = 0xFF;
    flag(party.host.game.sections, 3, 1) = 0xFF;
    party.settle();
    // The plain flag at once; the scene's not under a member still in the level.
    CHECK(flag(party.a.game.sections, 3, 1) == 0xFF);
    CHECK(flag(party.a.game.sections, 3, 2) == 0);

    // Scene flags of other levels are no danger to it.
    flag(party.host.game.sections, 8, 4) = 0xFF;
    party.settle();
    CHECK(flag(party.a.game.sections, 8, 4) == 0xFF);

    party.a.game.now = {mp::kPhasePlaying, 4, 3};
    party.settle();
    CHECK(flag(party.a.game.sections, 3, 2) == 0xFF);
    // The other is still in the level: still waiting.
    CHECK(flag(party.b.game.sections, 3, 2) == 0);
}

void a_change_in_the_hosts_level_goes_out_at_once() {
    Three party(4);
    party.settle();
    // Past the resend's reach: only the row's MISSION can bring it within a reading.
    party.a.shared.on_log([](const std::string&) {});
    flag(party.host.game.sections, 4, 5) = 0xFF;
    std::vector<int> rows;
    party.a.session.on_data([&](std::int32_t sender, const Bytes& bytes) {
        if (!bytes.empty() && bytes[0] == mp::kMissionKind) {
            rows.push_back(bytes[1]);
        }

        party.a.router.dispatch(sender, bytes);
    });
    tick(party.players(), kReadMs);
    tick(party.players(), kReadMs);
    CHECK(!rows.empty() && rows.back() == 4);
    CHECK(flag(party.a.game.sections, 4, 5) == 0xFF);
}

void a_member_arriving_asks_for_its_levels_row() {
    Three party(4);
    party.settle();
    std::vector<int> asked;
    party.host.session.on_data([&](std::int32_t sender, const Bytes& bytes) {
        if (!bytes.empty() && bytes[0] == mp::kSharedAskKind) {
            asked.push_back(bytes[1]);
        }

        party.host.router.dispatch(sender, bytes);
    });
    // A new load: a new level, the same one again the next time.
    party.a.game.now = {mp::kPhasePlaying, 6, 3};
    party.settle();
    CHECK(asked == std::vector<int>{6});
    party.a.game.now = {mp::kPhasePlaying, 6, 4};
    party.settle();
    CHECK((asked == std::vector<int>{6, 6}));
}

// The G15 check: a scripted mission in the host's game, with a member walking ahead of it over a
// checkpoint, ends with every game's flags the same but for what it is told to keep.
void a_scripted_mission_ends_the_same_everywhere() {
    Three party(5);
    const int script[][2] = {{5, 0}, {5, 1}, {5, 3}, {5, 5}, {2, 0}};

    for (const auto& step : script) {
        flag(party.host.game.sections, step[0], step[1]) = 0xFF;
        party.settle();
    }

    flag(party.b.game.sections, 5, 2) = 0xFF;  // a member's checkpoint
    party.settle();
    CHECK(party.a.game.sections.flags == party.host.game.sections.flags);
    CHECK(party.b.game.sections.flags == party.host.game.sections.flags);
}

void the_same_over_a_bad_link() {
    for (unsigned seed = 1; seed <= 5; ++seed) {
        Player host(1), member(2), other(3);
        std::vector<Player*> all = {&host, &member, &other};
        join_party(all, {{1, "P1"}, {2, "P2"}, {3, "P3"}});
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route(all);
        SimLink link;
        link.latency = 120;
        link.jitter = 80;
        link.loss = 0.15;
        link.reorder = 0.1;
        link.duplicate = 0.05;
        SimNet net(all, link, seed);
        net.shared = true;

        for (Player* p : all) {
            p->game.now = {mp::kPhasePlaying, 5, 2};
        }

        std::mt19937 random(seed);

        for (int i = 0; i < 40; ++i) {
            Player* who = all[random() % all.size()];
            const int level = static_cast<int>(random() % 19);
            const int index = static_cast<int>(random() % 16);

            // Scene flags are the exception on purpose; the rest must be the same.
            if (!mp::scene_flag(level, index)) {
                flag(who->game.sections, level, index) = 0xFF;
            }

            who->game.sections.gold_weapons[random() % mp::kGoldWeapons] = 1;
            who->game.sections.skills[random() % 30] = 1;

            for (int s = 0; s < 40; ++s) {
                net.step(16);
            }
        }

        for (int s = 0; s < 1500; ++s) {
            net.step(16);
        }

        CHECK(net.kicked == 0);
        CHECK(member.game.sections == host.game.sections);
        CHECK(other.game.sections == host.game.sections);
    }
}

void a_game_without_it_changes_nothing() {
    Three party;
    party.a.game.has_shared = false;
    party.host.game.sections.skills[1] = 1;
    party.settle();
    CHECK(party.a.game.sections.skills[1] == 0);
    CHECK(party.b.game.sections.skills[1] == 1);
}

// A late joiner: what the host got between its save and its arrival comes without a banner, through
// the one quiet path (Game::quiet_gifts), and no scene holds it up.
void a_late_joiner_catches_up_silently() {
    Player host(1), member(2), late(3);
    std::vector<Player*> all = {&host, &member, &late};
    join_party(all, {{1, "P1"}, {2, "P2"}});
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    host.game.now = member.game.now = {mp::kPhasePlaying, 4, 2};
    tick({&host, &member}, 16);
    host.game.owned.items[12] = 1;
    host.game.owned.planets[3] = 1;
    host.game.sections.gold_weapons[12] = 1;
    host.game.sections.skills[9] = 1;

    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, kReadMs);
    }

    // The member who was there gets the items as the game gives them.
    CHECK(member.game.owned.items[12] == 1);

    // In comes the third, its level on the way, then in a scene the moment it arrives: no menu or
    // scene holds what it is owed, and no banner comes out with it.
    host.game.live_save = Bytes(500, 3);
    late.session.handle(info({{1, "P1"}, {2, "P2"}, {3, "P3"}}));
    mp::Message joined;
    joined.kind = mp::Message::Kind::kPartyJoined;
    joined.id = 3;
    joined.text = "P3";
    host.session.handle(joined);
    member.session.handle(joined);
    tick(all, 16);
    tick(all, 16);
    CHECK(late.game.started_save == host.game.live_save);
    late.game.now = {mp::kPhaseScene, 4, 2};

    for (int i = 0; i < 4; ++i) {
        tick(all, kReadMs);
    }

    CHECK(late.game.owned.items[12] == 1 && late.game.owned.planets[3] == 1);
    CHECK(late.game.sections.gold_weapons[12] == 1 && late.game.sections.skills[9] == 1);
    CHECK((late.game.given_quietly == std::vector<std::string>{"item 12", "planet 3"}));
    CHECK(late.game.given.empty());

    // What comes once the quiet is over is news, given once it has control.
    late.game.now = {mp::kPhasePlaying, 4, 2};
    tick(all, mp::kJoiningMs + 1000);
    tick(all, mp::kQuietMs + 1000);
    tick(all, 16);
    CHECK(late.game.quiet.back() == false);
    host.game.owned.items[13] = 1;

    for (int i = 0; i < 4; ++i) {
        tick(all, kReadMs);
    }

    CHECK((late.game.given == std::vector<std::string>{"item 13"}));
}

}  // namespace

void run_shared_tests() {
    wire_round_trip();
    a_gold_weapon_and_a_skill_point_reach_everyone();
    nothing_is_taken_and_a_spent_item_stays_spent();
    nanotech_upgrades_stay_each_players();
    a_flag_goes_up_and_comes_down_as_the_game_writes_it();
    a_members_scene_flag_never_reaches_the_host();
    the_hosts_scene_flag_waits_for_a_member_to_leave_the_level();
    a_change_in_the_hosts_level_goes_out_at_once();
    a_member_arriving_asks_for_its_levels_row();
    a_scripted_mission_ends_the_same_everywhere();
    the_same_over_a_bad_link();
    a_game_without_it_changes_nothing();
    a_late_joiner_catches_up_silently();
}

}  // namespace mpt
