// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: the mods and rules a party agrees on.
#include "test_support.hpp"

namespace mpt {
namespace {


void mod_lists_read_back_and_digest() {
    const mp::ModList list = sample_list();
    Bytes bytes = mp::encode_mod_list(list);
    mp::Reader r(bytes.data(), bytes.size());
    mp::ModList back;
    CHECK(mp::decode_mod_list(r, back) && r.left() == 0);
    CHECK(back.mods.size() == 4 && back.mods[0].settings.size() == 6);
    CHECK(back.mods[0].settings[4].party == mp::kSettingHost && back.mods[2].party == mp::kModInput);
    CHECK(mp::mods_digest(back) == mp::mods_digest(list));
    // Cut short: refused.
    for (std::size_t cut = 0; cut < bytes.size(); cut += 37) {
        mp::Reader short_one(bytes.data(), cut);
        mp::ModList none;
        CHECK(!mp::decode_mod_list(short_one, none));
    }

    // The digest: the order the mods load in, what is a player's own, and the host's own settings
    // leave it as it is.
    mp::ModList same = list;
    std::swap(same.mods[0], same.mods[1]);
    CHECK(mp::mods_digest(same) == mp::mods_digest(list));
    same = list;
    setting_of(same, "seed").value = "ABCD";
    setting_of(same, "trace").value = "true";
    same.mods[2].version = "9.9.9";
    same.mods.pop_back();
    CHECK(mp::mods_digest(same) == mp::mods_digest(list));
    // Anything compared changes it.
    mp::ModList other = list;
    setting_of(other, "bolts").value = "2";
    CHECK(mp::mods_digest(other) != mp::mods_digest(list));
    other = list;
    other.mods[1].version = "1.1.1";
    CHECK(mp::mods_digest(other) != mp::mods_digest(list));
    other = list;
    other.mods.erase(other.mods.begin() + 1);
    CHECK(mp::mods_digest(other) != mp::mods_digest(list));
    other = list;
    other.family = "rac1-60-2";
    CHECK(mp::mods_digest(other) != mp::mods_digest(list));
    // The disc alone is no difference: only the world it belongs to.
    other = list;
    other.disc = "ntsc";
    CHECK(mp::mods_digest(other) == mp::mods_digest(list));
    other = list;
    other.openrac = "0.22.0";
    CHECK(mp::mods_digest(other) != mp::mods_digest(list));
    other = list;
    other.multiplayer = "0.8.0";
    CHECK(mp::mods_digest(other) != mp::mods_digest(list));
    // A game's mark in the list, and what is left of its description.
    const std::string listed = mp::tagged_description("Pokitaru, 12 gold bolts", 0x12343FA9u);
    CHECK(listed == "Pokitaru, 12 gold bolts|3fa9");
    CHECK(mp::plain_description(listed) == "Pokitaru, 12 gold bolts" && mp::description_tag(listed) == "3fa9");
    CHECK(mp::plain_description("New game") == "New game" && mp::description_tag("New game").empty());
    CHECK(mp::tagged_description(std::string(60, 'x'), 7).size() <= mp::kMaxDescriptionLength);
}


void differences_say_how() {
    using Kind = mp::Difference::Kind;
    const mp::ModList host = sample_list();
    CHECK(mp::compare_mods(host, host).empty());

    // Each compared setting of a gameplay mod refuses, each exempt one does not; the proof that the
    // check does refuse, setting by setting.
    for (const mp::ModSettingInfo& s : host.mods[0].settings) {
        mp::ModList mine = host;
        setting_of(mine, s.key).value = "changed";
        const std::vector<mp::Difference> d = mp::compare_mods(mine, host);

        if (s.party == mp::kSettingShared) {
            CHECK(d.size() == 1 && d[0].kind == Kind::kSettings && d[0].settings == std::vector<std::string>{s.name});
        } else {
            CHECK(d.empty());
        }
    }

    // Input and cosmetic mods do not count, on either side, whatever their version or settings.
    mp::ModList mine = host;
    mine.mods[2].version = "0.0.1";
    mine.mods[2].settings[0].value = "3";
    mine.mods.pop_back();
    CHECK(mp::compare_mods(mine, host).empty());

    // A version.
    mine = host;
    mine.mods[0].version = "1.2.2";
    std::vector<mp::Difference> d = mp::compare_mods(mine, host);
    CHECK(d.size() == 1 && d[0].kind == Kind::kVersion);
    CHECK(mp::refusal_lines(d)[0] == "Difficulty: the host has 1.2.3, you have 1.2.2");
    CHECK(mp::refusal_short(d) == "Difficulty version");

    // Settings, by their names, wrapped.
    mine = host;
    setting_of(mine, "enemy_count").value = "2";
    setting_of(mine, "damage_taken").value = "2";
    d = mp::compare_mods(mine, host);
    CHECK(mp::refusal_lines(d) == (std::vector<std::string>{"Your Difficulty settings differ:", "Enemy count, Damage taken"}));
    CHECK(mp::refusal_short(d) == "Difficulty settings");

    // One the host has and this game has not, one the other way round.
    mine = host;
    mine.mods.erase(mine.mods.begin() + 1);
    d = mp::compare_mods(mine, host);
    CHECK(d.size() == 1 && d[0].kind == Kind::kHostOnly);
    CHECK(mp::refusal_lines(d)[0] == "Host has Item Randomizer, you don't");
    CHECK(mp::refusal_short(d) == "no Item Randomizer");
    d = mp::compare_mods(host, mine);
    CHECK(d.size() == 1 && d[0].kind == Kind::kYouOnly);
    CHECK(mp::refusal_lines(d)[0] == "You have Item Randomizer, the host doesn't");
    CHECK(mp::refusal_short(d) == "extra Item Randomizer");

    // The build; never a region's name.
    mine = host;
    mine.family = "another";
    mine.openrac = "0.20.0";
    mine.multiplayer = "0.8.0";
    d = mp::compare_mods(mine, host);
    const std::vector<std::string> lines = mp::refusal_lines(d);
    CHECK(lines[0] == "You have a different disc from the host");
    CHECK(lines[1] == "OpenRAC: the host has 0.21.0, you have 0.20.0");
    CHECK(lines[2] == "Multiplayer: the host has 0.9.0, you have 0.8.0");
    CHECK(mp::refusal_short(d) == "a different disc, 2 more");

    for (const std::string& line : lines) {
        CHECK(line.find("pal") == std::string::npos && line.find("ntsc") == std::string::npos);
    }

    // A mod that cannot be played in a party: on either side, whatever else fits.
    mine = host;
    mine.mods.push_back({"archipelago", "Archipelago", "1.0.1", mp::kModSolo, {}});
    d = mp::compare_mods(mine, mine);
    CHECK(d.size() == 1 && d[0].kind == Kind::kSolo);
    CHECK(mp::refusal_lines(d)[0] == "Archipelago can't be played in a party");

    // The longest words still fit a page's lines, and nothing is lost.
    mp::ModList long_names = host;
    long_names.mods[0].name = "A Mod With Quite A Very Long Name Indeed Over There";
    long_names.mods[0].version = "10.20.30";
    d = mp::compare_mods(host, long_names);

    for (const std::string& line : mp::refusal_lines(d)) {
        CHECK(line.size() <= mp::kTitleLine);
    }

    CHECK(mp::wrap_text(std::string(100, 'x'), 47).size() == 3);
    const std::vector<std::string> names(30, "Damage to enemies");

    for (const std::size_t width : {mp::kTitleLine, mp::kPauseLine}) {
        std::size_t items = 0;

        for (const std::string& line : mp::wrap_list(names, width)) {
            CHECK(line.size() <= width);
            items += static_cast<std::size_t>(std::count(line.begin(), line.end(), ',')) + 1;
        }

        CHECK(items >= 30);
    }
}


void the_rules_are_said_in_words() {
    mp::ModList host = sample_list();
    const std::vector<mp::PartyRules> rules = {{"difficulty", {1}, "Bolts x2\nEnemy count x1.5\nChaos on"}};
    setting_of(host, "trace").value = "true";
    host.mods[1].settings.push_back({"logic", "Logic", "expert", "Expert", mp::kSettingShared, false});
    const std::vector<std::string> lines = mp::rules_lines(host, rules, sample_list(), mp::kTitleLine);
    const std::vector<std::string> want = {
        "Difficulty 1.2.3",
        "Bolts x2, Enemy count x1.5, Chaos on",
        "Item Randomizer 1.1.0",
        "Logic: Expert",
        "Stays yours: your controls and looks",
        "Detailed log, Keyboard and Mouse, Xbox Icons",
    };
    CHECK(lines == want);
    mp::ModList none;
    CHECK(mp::rules_lines(none, {}, none, mp::kPauseLine)
          == (std::vector<std::string>{"No gameplay mods: the game as it is", "Stays yours: your controls and looks"}));

    for (const std::string& line : mp::rules_lines(host, rules, sample_list(), mp::kPauseLine)) {
        CHECK(line.size() <= mp::kPauseLine);
    }
}


void a_differing_member_is_refused() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    mp::ModList mine = sample_list();
    setting_of(mine, "bolts").value = "3";
    gate(host, sample_list());
    gate(member, mine);
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    // Nothing heard yet: not known, no Ready, no Start.
    CHECK(host.mods.fit(2) == mp::Fit::kUnknown && !host.mods.may_play(2));
    host.session.handle(ready_of(2));
    CHECK(!host.session.ready(2) && !host.session.all_ready());
    tick_mods({&host, &member}, 16);
    tick_mods({&host, &member}, 16);
    CHECK(host.mods.fit(2) == mp::Fit::kDiffers && host.mods.why(2) == "Difficulty settings");
    CHECK(member.mods.fit(2) == mp::Fit::kDiffers && member.mods.why(2) == "Difficulty settings");
    CHECK(member.mods.heard_host() && !member.mods.differences().empty());
    CHECK(mp::refusal_lines(member.mods.differences())[1] == "Bolt drops");
    // Its Ready is not taken, at either end; Start stays refused.
    member.session.toggle_ready();
    CHECK(!member.session.ready(2));
    host.session.handle(ready_of(2));
    CHECK(!host.session.ready(2) && !host.session.all_ready() && !host.coop.start());
    CHECK(host.game.calls.empty());
    // It gets no rules.
    CHECK(member.game.delivered.empty() && member.mods.rules().empty());

    // Mutation: the same two games with the check's gates off: the host starts, and only the
    // digest in the build stops the member at PLAY, the check's last wall.
    Player host2(1), member2(2);
    host2.session.handle(info(both));
    member2.session.handle(info(both));
    host2.coop.set_build("mp " + std::to_string(mp::mods_digest(sample_list())));
    member2.coop.set_build("mp " + std::to_string(mp::mods_digest(mine)));
    host2.coop.choose({}, "New game");
    CHECK(start_ready(host2));
    route({&host2, &member2});
    tick({&host2, &member2}, 16);
    CHECK(member2.game.calls.empty() && member2.coop.take_notice().find("You can't join") == 0);
}


void a_mid_game_joiner_is_checked_too() {
    Player host(1), member(2), late(3);
    const std::vector<mp::Member> two = {{1, "P1"}, {2, "P2"}};
    const std::vector<mp::Member> three = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    mp::ModList wrong = sample_list();
    wrong.mods.erase(wrong.mods.begin());
    host.game.published = {{"difficulty", {7}, "Bolts x3"}};
    host.game.live_save = Bytes(100, 1);
    member.game.takes = {"difficulty"};
    late.game.takes = {"difficulty"};
    gate(host, sample_list());
    gate(member, sample_list());
    gate(late, wrong);
    host.session.handle(info(two));
    member.session.handle(info(two));
    host.coop.choose({}, "New game");
    tick_mods({&host, &member}, 16);
    tick_mods({&host, &member}, 16);
    CHECK(start_ready(host));
    route({&host, &member});
    tick_mods({&host, &member}, 16);
    CHECK(member.game.calls.size() == 1);
    host.game.now = {mp::kPhasePlaying, 3, 2};
    member.game.now = {mp::kPhasePlaying, 3, 2};
    tick_mods({&host, &member}, 16);
    // One that differs comes while the game is on: no game, asked or not.
    host.session.handle(info(three));
    member.session.handle(info(three));
    late.session.handle(info(three));
    tick_mods({&host, &member, &late}, 16);
    tick_mods({&host, &member, &late}, 16);
    CHECK(host.mods.fit(3) == mp::Fit::kDiffers);
    CHECK(mp::refusal_short(late.mods.differences()).find("no Difficulty") == 0);
    CHECK(late.game.calls.empty() && late.game.delivered.empty());
    tick_mods({&host, &member, &late}, mp::kWantMs + 16);
    tick_mods({&host, &member, &late}, mp::kWantMs + 16);
    CHECK(late.game.calls.empty() && late.game.delivered.empty());

    // One that matches: the rules, then the game, in that order.
    Player good(4);
    const std::vector<mp::Member> others = {{1, "P1"}, {2, "P2"}, {4, "P4"}};
    good.game.takes = {"difficulty"};
    gate(good, sample_list());
    host.session.handle(info(others));
    member.session.handle(info(others));
    good.session.handle(info(others));
    tick_mods({&host, &member, &good}, 16);
    tick_mods({&host, &member, &good}, 16);
    tick_mods({&host, &member, &good}, 16);
    CHECK(host.mods.fit(4) == mp::Fit::kMatch);
    CHECK(good.game.calls.size() == 1 && good.game.calls[0].find("start 3") == 0);
    CHECK(good.game.delivered.size() == 1 && good.game.delivered[0].data == Bytes{7});
    CHECK(good.game.delivered[0].calls == 0);
}


void the_hosts_rules_reach_the_members() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.game.published = {{"difficulty", {1, 2}, "Bolts x3"}, {"not-here", {5}, ""}};
    member.game.takes = {"difficulty"};
    // Alone, nothing reaches a mod.
    gate(host, sample_list());
    gate(member, sample_list());
    tick_mods({&host, &member}, 16);
    CHECK(member.game.delivered.empty());
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    tick_mods({&host, &member}, 16);
    tick_mods({&host, &member}, 16);
    // Before any level: its one mod that takes them, the host's bytes.
    CHECK(host.mods.fit(2) == mp::Fit::kMatch && member.mods.differences().empty());
    CHECK(member.game.delivered.size() == 1 && member.game.delivered[0].data == (Bytes{1, 2}));
    CHECK(member.game.calls.empty());
    CHECK(member.mods.rules().size() == 2 && member.mods.rules()[0].text == "Bolts x3");
    member.session.toggle_ready();
    route({&host, &member});
    CHECK(host.session.ready(2) && host.coop.start());
    route({&host, &member});
    tick_mods({&host, &member}, 16);
    CHECK(member.game.calls.size() == 1 && member.game.delivered[0].calls == 0);
    // An event starts in the host's game: the party has it at once.
    host.game.now = {mp::kPhasePlaying, 3, 2};
    member.game.now = {mp::kPhasePlaying, 3, 2};
    host.game.published[0].data = {1, 9};
    tick_mods({&host, &member}, mp::kRulesPollMs + 16);
    CHECK(member.game.delivered.size() == 2 && member.game.delivered[1].data == (Bytes{1, 9}));
    // The same rules again: nothing new.
    tick_mods({&host, &member}, mp::kRulesPollMs + 16);
    CHECK(member.game.delivered.size() == 2);
    // A respawn makes the level again: the mod keeps what it has, nothing is lost or sent again.
    member.game.now.loads = 3;
    tick_mods({&host, &member}, mp::kRulesPollMs + 16);
    member.game.now = {mp::kPhaseLoading, -1, 3};
    tick_mods({&host, &member}, mp::kRulesPollMs + 16);
    member.game.now = {mp::kPhasePlaying, 3, 4};
    tick_mods({&host, &member}, mp::kRulesPollMs + 16);
    CHECK(member.game.delivered.size() == 2);
    CHECK(member.mods.rules()[0].data == (Bytes{1, 9}));
    // The party goes: the mods hear there are none any more, once.
    mp::Message end;
    end.kind = mp::Message::Kind::kPartyEnd;
    member.session.handle(end);
    tick_mods({&member}, 16);
    tick_mods({&member}, 16);
    CHECK(member.game.delivered.size() == 3 && member.game.delivered[2].data.empty());
    CHECK(member.mods.rules().empty() && !member.mods.heard_host());
}


// New rules published just as a member comes: the ones already in have them too, not only it.
void a_comer_does_not_hide_new_rules() {
    Player host(1), member(2), late(3);
    const std::vector<mp::Member> two = {{1, "P1"}, {2, "P2"}};
    const std::vector<mp::Member> three = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    host.game.published = {{"difficulty", {1}, "Chaos on"}};
    member.game.takes = {"difficulty"};
    late.game.takes = {"difficulty"};
    gate(host, sample_list());
    gate(member, sample_list());
    gate(late, sample_list());
    host.session.handle(info(two));
    member.session.handle(info(two));
    tick_mods({&host, &member}, 16);
    tick_mods({&host, &member}, 16);
    CHECK(member.game.delivered.size() == 1);
    host.game.published[0].data = {2};
    host.session.handle(info(three));
    member.session.handle(info(three));
    late.session.handle(info(three));
    // Within one poll: the late member's MODS is what finds the new rules.
    late.mods.tick(late.clock + 1);
    route({&host, &member, &late});
    CHECK(member.game.delivered.size() == 2 && member.game.delivered[1].data == Bytes{2});
    CHECK(late.game.delivered.size() == 1 && late.game.delivered[0].data == Bytes{2});
}


// Another party with no tick out of one between: the last one's host's word does not stand.
void another_party_starts_clean() {
    Player member(2), other(3);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    gate(member, sample_list());
    member.session.handle(info(both));
    member.mods.tick(member.clock);
    mp::Writer w;
    w.u8(static_cast<std::uint8_t>(mp::ModsKind::kMods)).raw(mp::encode_mod_list(sample_list()));
    member.mods.handle(1, w.bytes());
    CHECK(member.mods.heard_host());
    mp::Message moved = info({{3, "P3"}, {2, "P2"}});
    moved.host = 3;
    moved.code = "GHJKLM";
    member.session.handle(moved);
    member.mods.tick(member.clock + 1);
    CHECK(!member.mods.heard_host());
}


void long_names_keep_what_the_line_says() {
    mp::LobbyMemberInput m;
    m.name = std::string(32, 'x');
    m.you = true;
    const std::string line = mp::lobby_member_line(m);
    CHECK(line.size() <= mp::kWhyLineMax && line.find("(not ready, mods match) - you") != std::string::npos);
    m.name = "Clank";
    m.differs = true;
    CHECK(mp::lobby_member_line(m) == "Clank (mods differ) - you");
}


void an_older_game_is_not_taken() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    gate(host, sample_list());
    host.session.handle(info(both));
    tick_mods({&host}, 16);
    // A member that never says MODS (0.8.0): never Ready.
    host.session.handle(ready_of(2));
    CHECK(host.mods.fit(2) == mp::Fit::kUnknown && !host.session.all_ready());
    tick_mods({&host}, mp::kModsQuietMs + 16);
    tick_mods({&host}, 16);
    CHECK(host.mods.fit(2) == mp::Fit::kDiffers && host.mods.why(2) == "an older Multiplayer");
    // A host that never answers: the member's lobby says so, after a while.
    gate(member, sample_list());
    member.session.handle(info(both));
    tick_mods({&member}, 16);
    CHECK(!member.mods.host_silent());
    member.clock += mp::kModsQuietMs + 16;
    tick_mods({&member}, 16);
    CHECK(member.mods.host_silent());
    member.session.toggle_ready();
    CHECK(!member.session.ready(2));
}


// A member of another build refuses the host's game; a member in the host's game goes back to the
// title when the party ends.
void builds_and_the_end() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.set_build("mp/0.5.0 1.0 pal");
    member.coop.set_build("mp/0.5.0 1.0 ntsc");
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK(member.game.calls.empty() && member.coop.take_notice().find("You can't join") == 0);
    // Not asked for again.
    tick({&host, &member}, mp::kWantMs + 16);
    CHECK(member.game.calls.empty());

    // The same build: it plays, and when the party ends in a level it goes back to the title.
    Player host2(1), member2(2);
    host2.session.handle(info(both));
    member2.session.handle(info(both));
    host2.coop.choose({}, "New game");
    CHECK(start_ready(host2));
    route({&host2, &member2});
    tick({&host2, &member2}, 16);
    member2.game.now = {mp::kPhasePlaying, 0, 2};
    tick({&host2, &member2}, 16);
    mp::Message end;
    end.kind = mp::Message::Kind::kPartyEnd;
    member2.session.handle(end);
    tick({&member2}, 16);
    CHECK(member2.game.calls.back() == "travel -1" && !member2.coop.playing());
}
}  // namespace

void run_mods_tests() {
    builds_and_the_end();
    mod_lists_read_back_and_digest();
    differences_say_how();
    the_rules_are_said_in_words();
    a_differing_member_is_refused();
    a_mid_game_joiner_is_checked_too();
    the_hosts_rules_reach_the_members();
    a_comer_does_not_hide_new_rules();
    another_party_starts_clean();
    long_names_keep_what_the_line_says();
    an_older_game_is_not_taken();
}

}  // namespace mpt
