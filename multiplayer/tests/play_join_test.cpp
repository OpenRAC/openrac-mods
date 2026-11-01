// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: a late member put beside the host, Go to host,
// the warning before the ship, votes on waits, and a host that starts again.
#include "test_support.hpp"

#include <cmath>

namespace mpt {
namespace {

bool logged(const std::vector<std::string>& log, const std::string& line) {
    return std::find(log.begin(), log.end(), line) != log.end();
}

// A party of a host and a member in level 3, the hero of the host steady on the ground; `late` in
// the title once `come()` is called, and in control after `in_level()`.
struct Party {
    Player host{1}, member{2}, late{3};
    std::vector<std::string> host_log, member_log, late_log;

    std::vector<Player*> all() { return {&host, &member, &late}; }

    Party() {
        host.coop.on_log([this](const std::string& l) { host_log.push_back(l); });
        member.coop.on_log([this](const std::string& l) { member_log.push_back(l); });
        late.coop.on_log([this](const std::string& l) { late_log.push_back(l); });
        const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
        host.session.handle(info(both));
        member.session.handle(info(both));
        host.game.placeable = member.game.placeable = late.game.placeable = true;
        host.game.live_save = Bytes(100, 7);
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route({&host, &member});
        tick({&host, &member}, 16);
        host.game.now = member.game.now = {mp::kPhasePlaying, 3, 2};
        // The world's answer waits for a listing with something in it.
        host.game.level = member.game.level = late.game.level = {moby(5, 5, 13, mp::kMobyUpdates, 3.0f)};
        host.game.hero_pose.flags = mp::kPoseSteady;
        host.game.hero_pose.position[0] = 10.0f;
        host.game.hero_pose.position[1] = 20.0f;
        host.game.hero_pose.position[2] = 5.0f;
        host.game.hero_pose.rotation[2] = 0.5f;
        run(1000);
    }

    void run(std::uint64_t ms) {
        for (std::uint64_t t = 0; t < ms; t += 50) {
            tick(all(), 50);
        }
    }

    void come() {
        mp::Message came;
        came.kind = mp::Message::Kind::kPartyJoined;
        came.id = 3;
        came.text = "P3";
        host.session.handle(came);
        member.session.handle(came);
        late.session.handle(info({{1, "P1"}, {2, "P2"}, {3, "P3"}}));
        late.game.now = {mp::kPhaseFrontEnd, -1, 0};
        late.game.hero_pose.position[0] = -40.0f;
        run(150);
        CHECK(late.coop.joining());
        CHECK(!late.game.calls.empty() && late.game.calls[0].rfind("start 3", 0) == 0);
    }

    void in_level() {
        late.game.now = {mp::kPhasePlaying, 3, 3};
        run(3000);  // the world's HELLO and SYNCED
        CHECK(late.world.synced());
    }
};


// The ordinary case: held on the joining line until the host's word, then beside it, and safe.
void a_joiner_is_put_beside_the_host() {
    Party p;
    p.come();
    p.late.game.place_works = false;
    p.host.game.hero_pose.flags = 0;  // the host jumping
    p.run(mp::kSafeFreshMs + 100);    // its last steady moment is too old now
    p.late.game.now = {mp::kPhasePlaying, 3, 3};
    p.run(3000);
    CHECK(p.late.world.synced());
    CHECK(p.late.coop.joining() && p.late.coop.hold_input());
    CHECK(p.late.coop.banner(p.late.clock) == "Joining the game");
    CHECK(p.late.game.placed.empty());

    // The host lands.
    p.late.game.place_works = true;
    p.host.game.hero_pose.flags = mp::kPoseSteady;
    p.run(1000);
    CHECK(p.late.game.placed.size() == 1);
    CHECK(p.late.game.placed[0].beside);
    CHECK(p.late.game.placed[0].at[0] == 10.0f && p.late.game.placed[0].at[1] == 20.0f);
    // The third player of the party: a quarter of the way around from the first member's spot.
    CHECK(std::fabs(p.late.game.placed[0].yaw - (0.5f + 1.5707964f)) < 0.001f);
    CHECK(!p.late.coop.joining() && !p.late.coop.hold_input());
    CHECK(p.late.coop.banner(p.late.clock).empty());
    CHECK(logged(p.late_log, "place: there"));
    CHECK(p.late.coop.shielded());
    p.run(mp::kShieldMs + 100);
    CHECK(!p.late.coop.shielded());
}

// The host never safe: its last safe spot after the wait; none: where the level put the joiner.
void a_host_never_safe_gives_its_last_spot_or_none() {
    {
        Party p;
        p.come();
        p.host.game.hero_pose.flags = 0;
        p.host.game.hero_pose.position[0] = 99.0f;  // in the air, somewhere else by now
        p.run(mp::kSafeFreshMs + 100);
        p.in_level();
        CHECK(p.late.game.placed.empty());
        p.run(mp::kPlaceWaitMs + 1000);
        CHECK(p.late.game.placed.size() == 1 && p.late.game.placed[0].at[0] == 10.0f);
        CHECK(logged(p.late_log, "place: at the host's last safe spot"));
        CHECK(!p.late.coop.joining());
    }
    {
        Party p;
        // Never steady in this level load.
        p.host.game.hero_pose.flags = 0;
        p.host.game.now.loads = 9;
        p.run(200);
        p.come();
        p.in_level();
        p.run(mp::kPlaceWaitMs + 1000);
        CHECK(p.late.game.placed.empty());
        CHECK(!p.late.coop.joining() && !p.late.coop.hold_input());
        CHECK(logged(p.late_log, "place: no spot beside the host: staying where the level put this game"));
    }
}

// A port that finds no spot leaves the hero where he is, and the joiner goes on.
void a_spot_the_port_cannot_find_leaves_the_joiner_there() {
    Party p;
    p.come();
    p.late.game.place_works = false;
    p.in_level();
    p.run(mp::kPlaceMoveMs + 500);
    CHECK(p.late.game.placed.size() == 1);
    CHECK(!p.late.coop.joining() && !p.late.coop.hold_input());
}

// A port without the call: as before.
void without_the_port_a_joiner_goes_straight_in() {
    Party p;
    p.late.game.placeable = false;
    p.come();
    p.in_level();
    CHECK(!p.late.coop.joining() && !p.late.coop.hold_input() && p.late.game.placed.empty());
}

// A host's scene is waited out, not replayed, and does not use up the wait for a safe spot.
void a_scene_in_progress_is_waited_out() {
    Party p;
    p.host.game.now = {mp::kPhaseScene, 3, 2, 7};
    p.member.game.now = {mp::kPhaseScene, 3, 2, 7};
    p.run(200);
    p.come();
    p.in_level();
    const auto scene_asked = [&] {
        return std::count_if(p.late.game.calls.begin(), p.late.game.calls.end(), [](const std::string& c) {
            return c.rfind("scene", 0) == 0;
        });
    };
    p.run(mp::kPlaceWaitMs + 2000);
    CHECK(p.late.coop.joining() && p.late.coop.hold_input() && p.late.game.placed.empty());
    CHECK(p.late.coop.banner(p.late.clock).find("scene") != std::string::npos);
    CHECK(scene_asked() == 0);
    CHECK(!p.host.coop.hold_input());

    // Over: the host's first steady moment puts it beside the host, and nothing of the scene plays.
    p.host.game.now = p.member.game.now = {mp::kPhasePlaying, 3, 2};
    p.run(1500);
    CHECK(p.late.game.placed.size() == 1 && !p.late.coop.joining());
    CHECK(scene_asked() == 0);
}

// The joiner's own Start ends its wait where it stands, the scene still not its to play.
void start_ends_a_joiners_wait() {
    Party p;
    p.host.game.now = p.member.game.now = {mp::kPhaseScene, 3, 2, 7};
    p.run(200);
    p.come();
    p.in_level();
    CHECK(p.late.coop.joining());
    p.late.coop.own_skip(mp::kPhasePlaying, p.late.clock);
    CHECK(!p.late.coop.joining() && p.late.game.placed.empty());
    p.run(1000);
    CHECK(p.late.game.placed.empty());
}

// Only the word of the host, on this ticket, of this game, moves anyone.
void a_place_needs_the_hosts_word_for_this_asking() {
    Party p;
    p.come();
    p.host.game.hero_pose.flags = 0;
    p.run(mp::kSafeFreshMs + 100);
    p.in_level();
    const auto place = [](std::int32_t play, std::uint16_t ticket) {
        mp::Writer w;
        w.u8(static_cast<std::uint8_t>(mp::GameKind::kPlace)).i32(play);
        w.u8(static_cast<std::uint8_t>(ticket >> 8)).u8(static_cast<std::uint8_t>(ticket)).u8(mp::kPlaceHere);
        for (int i = 0; i < 4; ++i) {
            w.i32(0);
        }
        return w.bytes();
    };
    const Bytes stale = place(1, 0);
    p.late.coop.handle(1, stale);  // no such game, no such ticket
    p.late.coop.handle(2, place(p.late.coop.playing() ? 1 : 0, 1));  // a member's
    p.late.coop.handle(1, Bytes{static_cast<std::uint8_t>(mp::GameKind::kPlace), 1, 2});  // cut short
    CHECK(p.late.game.placed.empty());
    CHECK(mp::owner_of(mp::kind::kPlace) == mp::Owner::kJoin);
    CHECK(mp::owner_of(mp::kind::kPlaceAsk) == mp::Owner::kJoin);
    CHECK(mp::owner_of(mp::kind::kWaitVote) == mp::Owner::kJoin);
}

// Enemies leave a joiner and a hero just placed alone.
void a_joiner_takes_no_damage() {
    Party p;
    p.come();
    p.in_level();
    const auto damage = [&] {
        mp::Writer w;
        w.u8(static_cast<std::uint8_t>(mp::WorldKind::kDamage)).u8(0xFF).u8(0xFF).i32(1);
        for (int i = 0; i < 7; ++i) {
            w.i32(0);
        }
        return w.bytes();
    };
    // Just placed.
    CHECK(p.late.coop.shielded());
    p.late.world.handle(1, damage());
    CHECK(p.late.game.hits.empty());
    p.run(mp::kShieldMs + 100);
    p.late.world.handle(1, damage());
    CHECK(p.late.game.hits.size() == 1);
}

// What the party's progress gives a joiner comes quietly, and for a while after.
void a_joiner_takes_progress_quietly() {
    Party p;
    p.come();
    CHECK(p.late.game.quiet.size() == 1 && p.late.game.quiet[0]);
    p.in_level();
    CHECK(p.late.game.quiet.size() == 1);
    p.run(mp::kQuietMs + 100);
    CHECK(p.late.game.quiet.size() == 2 && !p.late.game.quiet[1]);
}

// Go to host, for any member who wants it; the hero moves with the menu shut.
void go_to_host_from_the_pause_page() {
    Party p;
    CHECK(!p.host.coop.can_go());
    CHECK(p.member.coop.can_go());
    p.member.game.now.phase = mp::kPhaseMenu;
    p.member.game.place_works = false;
    CHECK(p.member.coop.go_to_host(p.member.clock));
    CHECK(p.member.coop.banner(p.member.clock) == "Going to the host");
    // The answer comes under the menu: asked of the port, which waits; no time runs out.
    p.run(mp::kPlaceMoveMs + 2000);
    CHECK(p.member.game.placed.size() == 1 && p.member.coop.going());
    // Shut: there.
    p.member.game.now.phase = mp::kPhasePlaying;
    std::copy(p.host.game.hero_pose.position, p.host.game.hero_pose.position + 3,
              p.member.game.hero_pose.position);
    p.run(200);
    CHECK(!p.member.coop.going() && p.member.coop.shielded());

    // Not again at once.
    CHECK(!p.member.coop.go_to_host(p.member.clock));
    p.run(mp::kGoAgainMs + 100);
    CHECK(p.member.coop.can_go());

    // Not from another level.
    p.member.game.now.level = 4;
    CHECK(!p.member.coop.can_go());
}

// No spot near the host: the place this game landed; none there either: said so.
void go_to_host_falls_back_to_the_landing() {
    Party p;
    // Landing at x = 1 (the fake's), then walked off.
    p.member.game.hero_pose.position[0] = 77.0f;
    p.member.game.place_works = false;
    CHECK(p.member.coop.go_to_host(p.member.clock));
    p.run(mp::kPlaceMoveMs + 600);
    CHECK(p.member.game.placed.size() == 2);
    CHECK(p.member.game.placed[0].beside && !p.member.game.placed[1].beside);
    CHECK(p.member.game.placed[1].at[0] != 10.0f);
    p.run(mp::kPlaceMoveMs + 600);
    CHECK(!p.member.coop.going());
    CHECK(p.member.coop.banner(p.member.clock) == "No safe spot near the host");
}

// The ship's warning: named on the banner, and the member still here until it is up.
void the_ship_warns_before_it_takes_a_member() {
    Party p;
    CHECK(p.host.coop.ship_travel(5, 3));
    p.run(100);
    CHECK(p.member.coop.banner(p.member.clock) == "The host is leaving for Planet5");
    CHECK(std::count(p.member.game.calls.begin(), p.member.game.calls.end(), "travel 5") == 0);
    p.run(mp::kTravelWarnMs - 600);
    CHECK(std::count(p.member.game.calls.begin(), p.member.game.calls.end(), "travel 5") == 0);
    p.run(700);
    CHECK(std::count(p.member.game.calls.begin(), p.member.game.calls.end(), "travel 5") == 1);
}

// A slow player: the others vote to go on, or the host just does.
void a_wait_can_be_voted_away() {
    for (const bool host_goes : {false, true}) {
        Player host(1), a(2), b(4), slow(3);
        const std::vector<mp::Member> everyone = {{1, "P1"}, {2, "P2"}, {3, "P3"}, {4, "P4"}};
        std::vector<Player*> all = {&host, &a, &b, &slow};
        for (Player* p : all) {
            p->session.handle(info(everyone));
        }
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route(all);
        tick(all, 16);
        for (Player* p : all) {
            p->game.now = {mp::kPhasePlaying, 3, 2};
        }
        for (int i = 0; i < 20; ++i) {
            tick(all, 50);
        }
        CHECK(host.coop.ship_travel(5, 3));
        CHECK(host.coop.waiting_for() == 3);
        route(all);
        for (int i = 0; i < 70; ++i) {
            tick(all, 50);
        }
        host.game.now = a.game.now = b.game.now = {mp::kPhasePlaying, 5, 4};
        slow.game.now = {mp::kPhaseLoading, 5, 3};
        for (int i = 0; i < 40; ++i) {
            tick(all, 50);
        }
        CHECK(host.coop.waiting_for() == 1);
        CHECK(a.coop.hold_input() && b.coop.hold_input() && host.coop.hold_input());
        CHECK(a.coop.wait_kind() == mp::kWaitArrival && a.coop.wait_voters() == 3);
        CHECK(a.coop.banner(a.clock).find("(0/3)") != std::string::npos);

        if (host_goes) {
            host.coop.own_skip(mp::kPhasePlaying, host.clock);
            tick(all, 50);
        } else {
            // One of three is no majority, however often it presses.
            a.coop.own_skip(mp::kPhasePlaying, a.clock);
            a.coop.own_skip(mp::kPhasePlaying, a.clock);
            for (int i = 0; i < 4; ++i) {
                tick(all, 50);
            }
            CHECK(host.coop.arriving() && a.coop.wait_votes() == 1);
            CHECK(a.coop.banner(a.clock).find("(1/3)") != std::string::npos);
            b.coop.own_skip(mp::kPhasePlaying, b.clock);
            tick(all, 50);
        }

        for (int i = 0; i < 10; ++i) {
            tick(all, 50);
        }
        CHECK(!host.coop.arriving() && !host.coop.hold_input());
        CHECK(!a.coop.hold_input() && !b.coop.hold_input());
        CHECK(a.coop.wait_kind() == mp::kWaitNone);
    }
}

// Out of a scene before a member, the host goes on: the member's scene plays out.
void the_host_goes_on_from_a_scene_wait() {
    Party p;
    p.host.game.now = p.member.game.now = {mp::kPhaseScene, 3, 2, 7};
    p.run(500);
    p.host.game.now = {mp::kPhasePlaying, 3, 2};
    p.run(300);
    CHECK(p.host.coop.hold_input());
    CHECK(p.host.coop.wait_kind() == mp::kWaitScene);
    CHECK(p.host.coop.banner(p.host.clock).find("Start goes on") != std::string::npos);
    const auto skips = std::count(p.member.game.calls.begin(), p.member.game.calls.end(), "skip");
    p.host.coop.own_skip(mp::kPhasePlaying, p.host.clock);
    p.run(200);
    CHECK(!p.host.coop.hold_input());
    CHECK(std::count(p.member.game.calls.begin(), p.member.game.calls.end(), "skip") == skips);
}

// A member held by the host's scene votes itself free; the host's scene plays on.
void a_vote_ends_a_members_wait_for_the_hosts_scene() {
    Party p;
    p.host.game.now = {mp::kPhaseScene, 3, 2, 7};
    p.member.game.now = {mp::kPhaseScene, 3, 2, 7};
    p.run(300);
    // The member's ended first.
    p.member.game.now = {mp::kPhasePlaying, 3, 2};
    p.run(1500);
    CHECK(p.member.coop.hold_input());
    CHECK(p.member.coop.wait_kind() == mp::kWaitScene && p.member.coop.wait_voters() == 1);
    const auto skips = std::count(p.host.game.calls.begin(), p.host.game.calls.end(), "skip");
    p.member.coop.own_skip(mp::kPhasePlaying, p.member.clock);
    p.run(200);
    CHECK(!p.member.coop.hold_input());
    CHECK(p.host.game.now.phase == mp::kPhaseScene);
    CHECK(std::count(p.host.game.calls.begin(), p.host.game.calls.end(), "skip") == skips);
}

// The host back in the title starts again: a member in a level leaves for the title and starts the
// new game; a save that does not pass is not loaded, whichever disc sent it.
void a_host_starts_again() {
    for (const bool bad_save : {false, true}) {
        Party p;
        CHECK(!p.host.coop.starts_again() || p.host.game.now.phase != mp::kPhaseFrontEnd);
        CHECK(!p.host.coop.start());  // not from a level
        p.host.game.now = {mp::kPhaseFrontEnd, -1, 3};
        p.run(200);
        CHECK(p.host.coop.starts_again());
        CHECK(p.host.coop.can_start());
        p.host.coop.choose(Bytes(3000, 1), "A save");
        p.member.game.saves_usable = !bad_save;
        p.member.game.calls.clear();
        CHECK(p.host.coop.start());
        p.host.game.now = {mp::kPhaseLoading, 0, 4};
        p.run(100);

        CHECK(p.member.coop.playing());
        CHECK(std::count(p.member.game.calls.begin(), p.member.game.calls.end(), "travel -1") == 1);

        if (bad_save) {
            CHECK(p.member.coop.take_notice() == mp::kBadSaveMember);
            p.member.game.now = {mp::kPhaseFrontEnd, -1, 3};
            p.run(200);
            CHECK(std::count_if(p.member.game.calls.begin(), p.member.game.calls.end(), [](const std::string& c) {
                      return c.rfind("start", 0) == 0;
                  }) == 0);
            continue;
        }

        // Its way out, then the title, then the host's new game, from its save.
        p.member.game.now = {mp::kPhaseLoading, 3, 3};
        p.run(100);
        CHECK(std::count(p.member.game.calls.begin(), p.member.game.calls.end(), "start -1 3000") == 0);
        p.member.game.now = {mp::kPhaseFrontEnd, -1, 3};
        p.run(100);
        CHECK(std::count(p.member.game.calls.begin(), p.member.game.calls.end(), "start -1 3000") == 1);
        CHECK(p.member.game.started_save == Bytes(3000, 1));
        CHECK(!p.member.coop.joining());
    }
}

// The title not up yet on the way back: asked again, not given up.
void a_member_starting_again_waits_for_the_title() {
    Party p;
    p.host.game.now = {mp::kPhaseFrontEnd, -1, 3};
    p.run(200);
    CHECK(p.host.coop.start());
    p.run(100);
    CHECK(std::count(p.member.game.calls.begin(), p.member.game.calls.end(), "travel -1") >= 1);
    p.run(mp::kTravelMs + 100);
    CHECK(std::count(p.member.game.calls.begin(), p.member.game.calls.end(), "travel -1") >= 2);
}

// The pause page's item.
void the_pause_page_has_go_to_host() {
    mp::PartyPageInput in;
    in.in_party = true;
    in.code = "ABCD";
    const auto has_go = [&] {
        const std::vector<mp::PartyLine> lines = mp::party_lines(in);
        return std::any_of(lines.begin(), lines.end(), [](const mp::PartyLine& l) {
            return l.kind == mp::PartyLine::Kind::kGoToHost && l.label == std::string("Go to host");
        });
    };
    CHECK(!has_go());
    in.go_to_host = true;
    CHECK(has_go());
}

}  // namespace

void run_join_tests() {
    a_joiner_is_put_beside_the_host();
    a_host_never_safe_gives_its_last_spot_or_none();
    a_spot_the_port_cannot_find_leaves_the_joiner_there();
    without_the_port_a_joiner_goes_straight_in();
    a_scene_in_progress_is_waited_out();
    start_ends_a_joiners_wait();
    a_place_needs_the_hosts_word_for_this_asking();
    a_joiner_takes_no_damage();
    a_joiner_takes_progress_quietly();
    go_to_host_from_the_pause_page();
    go_to_host_falls_back_to_the_landing();
    the_ship_warns_before_it_takes_a_member();
    a_wait_can_be_voted_away();
    the_host_goes_on_from_a_scene_wait();
    a_vote_ends_a_members_wait_for_the_hosts_scene();
    a_host_starts_again();
    a_member_starting_again_waits_for_the_title();
    the_pause_page_has_go_to_host();
}

}  // namespace mpt
