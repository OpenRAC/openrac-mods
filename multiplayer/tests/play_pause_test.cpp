// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: the pause page and what a pause leaves whole.
#include "test_support.hpp"

namespace mpt {
namespace {


// The pause menu's Multiplayer page (menu.hpp): out of a party one line; in one the code, the
// host's level, the members with their links as the party heard them (LINK, LINKS: a member's
// reaches everyone through the host) and Leave party. Leaving from the pause menu in the host's
// game goes back to the title from under the menu (play_travel works there since 1.9).
void the_pause_page_shows_the_party_and_leaves() {
    mp::PartyPageInput none;
    std::vector<mp::PartyLine> lines = mp::party_lines(none);
    CHECK(lines.size() == 1 && lines[0].kind == mp::PartyLine::Kind::kText
          && lines[0].label == "Not in a party");
    CHECK(mp::link_text(38, true) == "38 ms UDP" && mp::link_text(-1, false) == "... TCP");
    CHECK(std::string(mp::pause_leave_question(false, false)) == "Leave the party?");
    CHECK(std::string(mp::pause_leave_question(false, true)) == "End the party?");
    CHECK(std::string(mp::pause_leave_question(true, true)) == "Stop hosting?");

    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    member.game.now = {mp::kPhasePlaying, 0, 2};
    host.game.now = {mp::kPhasePlaying, 0, 2};
    tick({&host, &member}, 16);

    // Nobody's link is known before it is reported.
    CHECK(host.session.link(2).ping_ms == -1 && member.session.link(1).ping_ms == -1);
    member.session.report_link(38, true);
    route({&host, &member});
    CHECK(host.session.link(2).ping_ms == 38 && host.session.link(2).udp);
    CHECK(member.session.link(2).ping_ms == 38);
    host.session.report_link(5, false);
    route({&host, &member});
    CHECK(member.session.link(1).ping_ms == 5 && !member.session.link(1).udp);
    CHECK(member.session.link(2).ping_ms == 38 && member.session.link(2).udp);

    // The page as the member sees it.
    mp::PartyPageInput in;
    in.in_party = member.session.in_party();
    in.code = member.session.code();
    in.host_level = "Novalis";
    for (const mp::Member& m : member.session.members()) {
        const mp::Link l = member.session.link(m.id);
        in.members.push_back({m.name, m.id == member.session.host(), m.id == member.session.id(),
                              l.ping_ms, l.udp});
    }
    lines = mp::party_lines(in);
    CHECK(lines.size() == 6);
    CHECK(lines[0].label == "Party code" && lines[0].value == "ABCDEF");
    CHECK(lines[1].label == "Host's level" && lines[1].value == "Novalis");
    CHECK(lines[2].label == "P1 (host)" && lines[2].value == "5 ms TCP");
    CHECK(lines[3].label == "P2 (you)" && lines[3].value == "38 ms UDP");
    CHECK(lines[4].kind == mp::PartyLine::Kind::kRules && lines[4].label == "Party rules");
    CHECK(lines[5].kind == mp::PartyLine::Kind::kLeave && lines[5].label == "Leave party");
    in.direct = true;
    in.where = "10.0.0.2:7770";
    in.host_level.clear();
    lines = mp::party_lines(in);
    CHECK(lines[0].label == "Direct" && lines[0].value == "10.0.0.2:7770" && lines[1].value == "None");

    // Leave party from the pause menu: back to the title from under the menu, once.
    member.game.now = {mp::kPhaseMenu, 0, 2};
    tick({&member}, 16);
    member.session.leave();
    CHECK(!member.session.in_party() && member.session.link(1).ping_ms == -1);
    tick({&member}, 16);
    CHECK(member.game.calls.back() == "travel -1" && member.coop.to_title());
    tick({&member}, 16);
    member.game.now = {mp::kPhaseLoading, -1, 2};
    tick({&member}, 16);
    CHECK(std::count(member.game.calls.begin(), member.game.calls.end(), "travel -1") == 1);
    // Out of a party nothing is reported.
    member.link.frames.clear();
    member.session.report_link(40, true);
    CHECK(member.link.frames.empty());
}


// A game in a menu (the pause menu's own mobys up, or a load) lists nothing: nothing is taken for
// dead, on either side; the world is asked to run on under menus while the party plays.
void a_menu_kills_nothing() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    CHECK(!host.game.overlay);
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK(host.game.overlay && member.game.overlay);
    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced;
    for (Player* p : {&host, &member}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        p->game.level = {moby(0, 0, 300, runs | mp::kMobyCreature, 10.0f), moby(1, 1, 500, runs, 20.0f),
                         moby(2, 2, 501, runs, 25.0f)};
    }
    member.game.hero_pose.position[0] = 12.0f;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    const std::size_t member_kills = member.game.world_calls.size();
    const std::size_t host_kills = host.game.world_calls.size();

    // The host pauses (its listing goes blank for a while), then the member does.
    host.game.now.phase = mp::kPhaseMenu;
    host.game.listing_blank = true;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    host.game.now.phase = mp::kPhasePlaying;
    host.game.listing_blank = false;
    member.game.now.phase = mp::kPhaseMenu;
    member.game.listing_blank = true;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    member.game.now.phase = mp::kPhasePlaying;
    member.game.listing_blank = false;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }

    // Nothing killed in either game, every moby still there, and the member still shown.
    CHECK(member.game.world_calls.size() == member_kills);
    CHECK(std::none_of(host.game.world_calls.begin() + static_cast<std::ptrdiff_t>(host_kills),
                       host.game.world_calls.end(), [](const std::string& c) { return c.rfind("kill", 0) == 0; }));
    for (Player* p : {&host, &member}) {
        CHECK(p->game.moby_slot(0) == 0 && p->game.moby_slot(1) == 1 && p->game.moby_slot(2) == 2);
    }
    CHECK(host.coop.figures() == 1 && member.coop.figures() == 1);

    // The party over: the game is the game again.
    mp::Message end;
    end.kind = mp::Message::Kind::kPartyEnd;
    member.session.handle(end);
    tick({&member}, 16);
    CHECK(!member.game.overlay);
}


// A pause as the port has it in a party (2026-10-28): the paused game's world goes on under its
// menu and lists the level's mobys as before (phase Menu), its mobys sit still for a while (no
// snapshot for them). Host pause, then member pause: nothing is killed or despawned on the
// watching side, the spawned creature and the placed mobys keep their slots,
// and a creature the host spawns while the member is paused comes to the member then.
void a_pause_keeps_the_world_whole() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced;
    for (Player* p : {&host, &member}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        p->game.level = {moby(0, 0, 300, runs | mp::kMobyCreature, 10.0f), moby(1, 1, 500, runs, 20.0f),
                         moby(2, 2, 501, runs, 25.0f)};
    }
    // An enemy the host spawned (a creature, no key).
    host.game.level.push_back(moby(300, -1, 301, mp::kMobyUpdates | mp::kMobyCreature, 12.0f));
    member.game.hero_pose.position[0] = 12.0f;
    for (int i = 0; i < 10; ++i) {
        host.game.level[0].pose.position[1] = static_cast<float>(i);
        tick({&host, &member}, 50);
    }
    CHECK(member.world.driven() == 2 && member.world.spawned() == 1);
    const auto listed = [](const Player& p) { return p.world.mobys(); };
    const std::size_t member_mobys = listed(member);
    const std::size_t member_calls = member.game.world_calls.size();
    const std::size_t host_calls = host.game.world_calls.size();

    // The host pauses: its world runs on, listed as before; its enemy stands still a second.
    host.game.now.phase = mp::kPhaseMenu;
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
        CHECK(listed(member) == member_mobys);
    }
    host.game.now.phase = mp::kPhasePlaying;

    // The member pauses; the host spawns another enemy meanwhile.
    member.game.now.phase = mp::kPhaseMenu;
    host.game.level.push_back(moby(301, -1, 302, mp::kMobyUpdates | mp::kMobyCreature, 14.0f));
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    member.game.now.phase = mp::kPhasePlaying;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }

    // Nothing killed or despawned in either game; the new enemy came to the member.
    const auto removals = [](const std::vector<std::string>& calls, std::size_t from) {
        return std::count_if(calls.begin() + static_cast<std::ptrdiff_t>(from), calls.end(),
                             [](const std::string& c) { return c.rfind("kill", 0) == 0; });
    };
    CHECK(removals(member.game.world_calls, member_calls) == 0);
    CHECK(removals(host.game.world_calls, host_calls) == 0);
    CHECK(member.world.spawned() == 2 && member.world.driven() == 3);
    CHECK(std::count(member.game.world_calls.begin(), member.game.world_calls.end(), "spawn 302") == 1);
    for (Player* p : {&host, &member}) {
        CHECK(p->game.moby_slot(0) == 0 && p->game.moby_slot(1) == 1 && p->game.moby_slot(2) == 2);
    }
    CHECK(host.coop.figures() == 1 && member.coop.figures() == 1);
}


// Each class as the inventory has it: a vendor is each game's own (not driven), a gold bolt a member
// collects is gone in the host's game too (removed, no hit), ammo an enemy of the host drops is the
// member's own to collect (made undriven, kept when the host's is taken).
void classes_are_shared_as_they_should_be() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced;
    for (Player* p : {&host, &member}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        p->game.level = {moby(0, 0, 11, runs, 5.0f), moby(1, 1, 1134, runs, 6.0f),
                         moby(2, 2, 300, runs | mp::kMobyCreature, 7.0f)};
    }
    member.game.hero_pose.position[0] = 6.0f;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK((member.game.drives == std::set<int>{2}));  // the enemy only

    // The member takes the gold bolt: the host's is removed, without a hit.
    const std::size_t hits = host.game.hits.size();
    member.game.die(1);
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(host.game.moby_slot(1) == -1 && host.game.hits.size() == hits);

    // An enemy of the host drops ammo (class 204): the member gets its own, undriven.
    host.game.level.push_back(moby(301, -1, 204, mp::kMobyUpdates, 8.0f));
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    const auto ammo = std::find_if(member.game.level.begin(), member.game.level.end(),
                                   [](const mp::MobyState& m) { return m.oclass == 204; });
    CHECK(ammo != member.game.level.end() && member.game.drives.count(ammo->slot) == 0);
    // The host collects its own: the member keeps its copy.
    host.game.level.pop_back();
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(std::any_of(member.game.level.begin(), member.game.level.end(),
                      [](const mp::MobyState& m) { return m.oclass == 204; }));
}
}  // namespace

void run_pause_tests() {
    the_pause_page_shows_the_party_and_leaves();
    a_menu_kills_nothing();
    a_pause_keeps_the_world_whole();
    classes_are_shared_as_they_should_be();
}

}  // namespace mpt
