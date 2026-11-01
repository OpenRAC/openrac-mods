// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: bolts, items and the party's progress.
#include "test_support.hpp"

namespace mpt {
namespace {


// Bolts are the host's, once in the world: its crates' and enemies' drops (also of a crate a
// member broke) come to every member from it, a member's game drops none, and a bolt is the first
// claimant's, gone from every game, its value given by the host only.
void bolts_are_the_hosts() {
    Player host(1), a(2), b(3);
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    for (Player* p : {&host, &a, &b}) {
        p->session.handle(info(all));
    }
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &a, &b});
    tick({&host, &a, &b}, 16);

    // A bolt crate (key 1), a placed hexnut (key 2, 5 bolts), another bolt crate (key 3), a placed
    // bolt (key 4) in every game.
    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced;
    for (Player* p : {&host, &a, &b}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        p->game.level = {moby(1, 1, 500, runs, 20.0f), moby(2, 2, 14, runs, 30.0f), moby(3, 3, 500, runs, 40.0f),
                         moby(4, 4, 13, runs, 50.0f)};
    }
    for (int i = 0; i < 20; ++i) {
        tick({&host, &a, &b}, 50);
    }

    // A member's game in the host's level drops nothing of its own; the host's drops.
    CHECK(a.world.quiet_drop(1) && b.world.quiet_drop(3) && !host.world.quiet_drop(1));

    // The host breaks crate 1: its game drops a bolt and a starnut (20), in flight (state 1).
    host.game.die(1);
    mp::MobyState bolt = moby(300, -1, 13, mp::kMobyUpdates, 21.0f);
    mp::MobyState starnut = moby(301, -1, 15, mp::kMobyUpdates, 22.0f);
    bolt.state = starnut.state = 1;
    host.game.level.push_back(bolt);
    host.game.level.push_back(starnut);
    tick({&host, &a, &b}, 50);
    tick({&host, &a, &b}, 50);

    // Every member has them once, from the host, flying out as the host's do (driven).
    for (Player* p : {&a, &b}) {
        CHECK(count_of_class(p->game, 13) == 2 && count_of_class(p->game, 15) == 1);  // placed 4 + the host's
        CHECK(p->world.spawned() == 2 && p->world.driven() == 2);  // made driven (moby_spawn's)
    }

    // They land in the host's game: each member's copy runs in its own game.
    for (mp::MobyState& m : host.game.level) {
        if (m.key < 0) {
            m.state = mp::kBoltHovering;
            m.pose.position[2] += 0.5f;
        }
    }
    tick({&host, &a, &b}, 100);
    tick({&host, &a, &b}, 100);
    for (Player* p : {&a, &b}) {
        CHECK(p->world.driven() == 0 && p->world.spawned() == 2);
    }

    // Both members' Ratchets reach the starnut: neither game adds it, both claim; A's claim reaches
    // the host first and only A is given its 20 bolts. In the host's game it flies to A's figure
    // (A's figure stands in for its update), and goes there, nothing added; then from every game.
    const int a_star = slot_of_class(a.game, 15);
    const int b_star = slot_of_class(b.game, 15);
    CHECK(!a.world.collect(a_star) && !b.world.collect(b_star));
    a.game.die(a_star);
    b.game.die(b_star);
    route({&host, &a});
    route({&host, &a, &b});
    tick({&host, &a, &b}, 50);
    tick({&host, &a, &b}, 50);
    CHECK(a.game.bolt_count == 20 && b.game.bolt_count == 0 && host.game.bolt_count == 0);
    const int host_star = slot_of_class(host.game, 15);
    CHECK(host_star >= 0);
    CHECK(!host.world.gate(host_star, mp::kMobyBefore));
    // Not yet off (it hovers): A stands within its reach, on the way to A (at x 1).
    CHECK(host.game.world_calls.back() == "stand 21 " + std::to_string(host.coop.figure_of(2)));
    CHECK(!host.world.collect(host_star));
    host.world.gate(host_star, mp::kMobyAfter);
    host.game.die(host_star);
    tick({&host, &a, &b}, 50);
    tick({&host, &a, &b}, 50);
    CHECK(a.game.bolt_count == 20 && b.game.bolt_count == 0 && host.game.bolt_count == 0);
    CHECK(count_of_class(host.game, 15) == 0 && count_of_class(a.game, 15) == 0 && count_of_class(b.game, 15) == 0);

    // The host's Ratchet takes the bolt: its own credit (its game adds it), gone from the members';
    // B's claim of it, late, is refused.
    CHECK(host.world.collect(300));
    host.game.bolt_count += 1;
    int b_spawned = -1;
    for (const mp::MobyState& m : b.game.level) {
        if (m.oclass == 13 && m.key < 0) {
            b_spawned = m.slot;
        }
    }
    CHECK(b_spawned >= 0 && !b.world.collect(b_spawned));
    b.game.die(b_spawned);
    host.game.die(300);
    tick({&host, &a, &b}, 50);
    tick({&host, &a, &b}, 50);
    CHECK(host.game.bolt_count == 1 && b.game.bolt_count == 0 && a.game.bolt_count == 20);
    // In A's game, where it hovered by itself, it flies to the host's figure and goes there.
    int a_spawned = -1;
    for (const mp::MobyState& m : a.game.level) {
        if (m.oclass == 13 && m.key < 0) {
            a_spawned = m.slot;
        }
    }
    CHECK(a_spawned >= 0 && !a.world.gate(a_spawned, mp::kMobyBefore));
    CHECK(a.game.world_calls.back().rfind("stand ", 0) == 0
          && a.game.world_calls.back().substr(a.game.world_calls.back().rfind(' ') + 1)
                 == std::to_string(a.coop.figure_of(1)));
    CHECK(!a.world.collect(a_spawned));
    a.game.die(a_spawned);
    tick({&host, &a, &b}, 50);
    CHECK(host.game.bolt_count == 1 && b.game.bolt_count == 0 && a.game.bolt_count == 20);
    CHECK(a.world.spawned() == 0 && b.world.spawned() == 0);
    CHECK(std::none_of(a.game.level.begin(), a.game.level.end(),
                       [](const mp::MobyState& m) { return m.key < 0 && mp::bolt_class(m.oclass); }));

    // B breaks crate 3: its game drops nothing of its own, the host's copy breaks (a wrench's hit)
    // and drops a hexnut, which every member gets once, from the host.
    const std::size_t hits = host.game.hits.size();
    CHECK(b.world.quiet_drop(3));
    b.game.die(3);
    tick({&host, &a, &b}, 50);
    tick({&host, &a, &b}, 50);
    CHECK(host.game.hits.size() == hits + 1 && host.game.hits.back().victim == 3);
    CHECK(!host.world.quiet_drop(3));
    host.game.die(3);
    host.game.level.push_back(moby(302, -1, 14, mp::kMobyUpdates, 41.0f));
    tick({&host, &a, &b}, 50);
    tick({&host, &a, &b}, 50);
    for (Player* p : {&a, &b}) {
        CHECK(std::count(p->game.world_calls.begin(), p->game.world_calls.end(), "spawn 14") == 1);
        CHECK(count_of_class(p->game, 14) == 2);  // the placed hexnut (key 2) and the host's
    }

    // The placed hexnut (key 2): B and A both take theirs; B's claim reaches the host first. B gets
    // 5, A nothing, and it is dead in the host's game (and so in everyone's); no BROKE for it.
    CHECK(!b.world.collect(2) && !a.world.collect(2));
    a.game.die(2);
    b.game.die(2);
    const std::size_t host_hits = host.game.hits.size();
    route({&host, &b});
    route({&host, &a, &b});
    tick({&host, &a, &b}, 50);
    tick({&host, &a, &b}, 50);
    CHECK(b.game.bolt_count == 5 && a.game.bolt_count == 20);
    // The host's flies to B's figure first, then is dead there (and so in everyone's).
    CHECK(host.game.moby_slot(2) == 2 && !host.world.gate(2, mp::kMobyBefore));
    CHECK(!host.world.collect(2));
    host.game.die(2);
    tick({&host, &a, &b}, 50);
    tick({&host, &a, &b}, 50);
    CHECK(b.game.bolt_count == 5 && a.game.bolt_count == 20 && host.game.bolt_count == 1);
    CHECK(host.game.moby_slot(2) == -1 && host.game.hits.size() == host_hits);

    // The placed bolt (key 4) taken by the host: its own, dead in the members' games too, and a
    // member's claim of it after is refused.
    CHECK(host.world.collect(4));
    host.game.bolt_count += 1;
    host.game.die(4);
    tick({&host, &a, &b}, 50);
    tick({&host, &a, &b}, 50);
    // In each member's game it flies to the host's figure, the host's KILLED left to that flight,
    // and goes on arrival, nothing added.
    for (Player* p : {&a, &b}) {
        CHECK(p->game.moby_slot(4) == 4 && !p->world.gate(4, mp::kMobyBefore));
        CHECK(!p->world.collect(4));
        p->game.die(4);
    }
    tick({&host, &a, &b}, 50);
    CHECK(a.game.moby_slot(4) == -1 && b.game.moby_slot(4) == -1);
    CHECK(b.game.bolt_count == 5 && a.game.bolt_count == 20);
    mp::Writer late;
    late.u8(static_cast<std::uint8_t>(mp::WorldKind::kClaim)).u8(0).u8(0).u8(4);
    host.world.handle(3, late.bytes());
    route({&host, &a, &b});
    CHECK(b.game.bolt_count == 5 && host.game.bolt_count == 2);

}


// A bolt in the host's game flies to the player nearest it: a member nearer stands in for the
// hero during its update, and what that figure collects is the member's (BOLTS), nothing added in
// the host's game; the host nearer collects it itself. The world's log line counts its spawns.
void bolts_go_to_the_nearest() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    std::vector<std::string> host_log;
    host.world.on_log([&](const std::string& line) { host_log.push_back(line); });

    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced;
    for (Player* p : {&host, &member}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        p->game.level = {moby(1, 1, 500, runs, 20.0f), moby(2, 2, 77, mp::kMobyPlaced, 90.0f)};
    }
    // The member stands by the crate (x 20), the host far off (x 100).
    host.game.hero_pose.position[0] = 100.0f;
    member.game.hero_pose.position[0] = 21.0f;
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }

    // The member breaks the crate: the host's copy breaks and drops a starnut at the crate.
    member.game.die(1);
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    host.game.die(1);
    host.game.level.push_back(moby(300, -1, 15, mp::kMobyUpdates, 20.5f));
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);

    // Its update on the host: the member is nearer, so its figure stands in; the bolt reaches it
    // and is collected there: the member's 20 bolts, none for the host.
    const int stood = host.game.stood;
    CHECK(!host.world.gate(300, mp::kMobyBefore));
    CHECK(host.game.stood == stood + 1 && host.game.world_calls.back().rfind("stand 21", 0) == 0);
    CHECK(!host.world.collect(300));
    CHECK(!host.world.gate(300, mp::kMobyAfter));
    host.game.die(300);
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(member.game.bolt_count == 20 && host.game.bolt_count == 0);
    CHECK(member.world.spawned() == 0);

    // Another bolt, the host now nearer than the member: its own, no stand-in.
    host.game.hero_pose.position[0] = 30.0f;
    member.game.hero_pose.position[0] = 60.0f;
    host.game.level.push_back(moby(301, -1, 13, mp::kMobyUpdates, 31.0f));
    for (int i = 0; i < 5; ++i) {
        tick({&host, &member}, 50);
    }
    const int stood_again = host.game.stood;
    CHECK(!host.world.gate(301, mp::kMobyBefore));
    CHECK(host.game.stood == stood_again);
    CHECK(host.world.collect(301));
    host.game.bolt_count += 1;
    host.world.gate(301, mp::kMobyAfter);
    host.game.die(301);
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(host.game.bolt_count == 1 && member.game.bolt_count == 20);

    // The world's line in the log counts the two spawns and their ends.
    tick({&host, &member}, mp::kWorldLogMs);
    CHECK(std::any_of(host_log.begin(), host_log.end(), [](const std::string& l) {
        return l.find(" 2 spawns, 2 despawns") != std::string::npos;
    }));
}


// A bolt that landed and hovers runs by itself in each game. Taken in one, it flies in the others
// to whoever took it, as the game's own pickup flies (the taker's figure standing in for its
// update: near it while it hovers, so it sets off, then where the figure is), and goes on arrival
// with nothing added; the DESPAWN that follows leaves it to its flight. One that never arrives is
// deleted after kFlightMs.
void taken_bolts_fly_to_the_taker() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);

    for (Player* p : {&host, &member}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        p->game.level = {moby(1, 1, 77, mp::kMobyPlaced, 90.0f)};
    }
    host.game.hero_pose.position[0] = 10.0f;
    member.game.hero_pose.position[0] = 40.0f;
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }

    // The host's game drops three bolts, which land: each member's copy runs by itself.
    for (int i = 0; i < 3; ++i) {
        mp::MobyState b = moby(300 + i, -1, 13, mp::kMobyUpdates, 25.0f + static_cast<float>(i));
        b.state = 1;
        host.game.level.push_back(b);
    }
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    for (mp::MobyState& m : host.game.level) {
        if (m.key < 0) {
            m.state = mp::kBoltHovering;
        }
    }
    tick({&host, &member}, 100);
    tick({&host, &member}, 100);
    CHECK(member.world.spawned() == 3 && member.world.driven() == 0);
    std::vector<int> copies;
    for (const mp::MobyState& m : member.game.level) {
        if (m.key < 0 && m.oclass == 13) {
            copies.push_back(m.slot);
        }
    }
    CHECK(copies.size() == 3);
    const int member_figure = host.coop.figure_of(2);
    const int host_figure = member.coop.figure_of(1);
    CHECK(member_figure >= 0 && host_figure >= 0);

    // The member takes the first (x 25): its claim, granted. In the host's game it hovers on: the
    // member's figure stands within its reach on the way to it (x 25.5), it sets off, then the
    // figure stands where it is (x 40), and it goes on arrival, nothing added there.
    CHECK(!member.world.collect(copies[0]));
    member.game.die(copies[0]);
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK(member.game.bolt_count == 1 && slot_of_class(host.game, 13) == 300);
    CHECK(!host.world.gate(300, mp::kMobyBefore));
    CHECK(host.game.world_calls.back() == "stand 25 " + std::to_string(member_figure));
    host.world.gate(300, mp::kMobyAfter);
    for (mp::MobyState& m : host.game.level) {
        if (m.slot == 300) {
            m.state = mp::kBoltFlying;
            m.pose.position[0] = 30.0f;
        }
    }
    tick({&host, &member}, 16);
    CHECK(!host.world.gate(300, mp::kMobyBefore));
    CHECK(host.game.world_calls.back() == "stand 40 " + std::to_string(member_figure));
    CHECK(!host.world.collect(300));
    host.world.gate(300, mp::kMobyAfter);
    host.game.die(300);
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(member.game.bolt_count == 1 && host.game.bolt_count == 0);

    // The host takes the second (its own credit). In the member's game it flies to the host's
    // figure: TAKEN, then the DESPAWN, which leaves it be; it goes on arrival, unclaimed.
    CHECK(host.world.collect(301));
    host.game.bolt_count += 1;
    host.game.die(301);
    tick({&host, &member}, 16);
    tick({&host, &member}, 16);
    CHECK(std::any_of(member.game.level.begin(), member.game.level.end(),
                      [&](const mp::MobyState& m) { return m.slot == copies[1]; }));
    // An eye on it, so it runs (flies) wherever the member's camera is.
    CHECK(member.game.eyes_at.size() == 3 && member.game.eyes_at[0] == 26.0f);
    CHECK(!member.world.gate(copies[1], mp::kMobyBefore));
    CHECK(member.game.world_calls.back() == "stand 25 " + std::to_string(host_figure));
    for (mp::MobyState& m : member.game.level) {
        if (m.slot == copies[1]) {
            m.state = mp::kBoltFlying;
        }
    }
    tick({&host, &member}, 16);
    CHECK(!member.world.gate(copies[1], mp::kMobyBefore));
    CHECK(member.game.world_calls.back() == "stand 10 " + std::to_string(host_figure));
    const std::size_t sent = member.link.frames.size();
    CHECK(!member.world.collect(copies[1]));
    CHECK(member.link.frames.size() == sent);  // no claim
    member.game.die(copies[1]);
    route({&host, &member});
    tick({&host, &member}, 50);
    CHECK(member.game.bolt_count == 1 && host.game.bolt_count == 1);
    CHECK(member.game.eyes_at.empty());

    // The host takes the third, and the member's copy is never updated: deleted after kFlightMs.
    CHECK(host.world.collect(302));
    host.game.bolt_count += 1;
    host.game.die(302);
    tick({&host, &member}, 16);
    tick({&host, &member}, 16);
    CHECK(std::any_of(member.game.level.begin(), member.game.level.end(),
                      [&](const mp::MobyState& m) { return m.slot == copies[2]; }));
    tick({&host, &member}, mp::kFlightMs);
    CHECK(std::none_of(member.game.level.begin(), member.game.level.end(),
                       [&](const mp::MobyState& m) { return m.slot == copies[2]; }));
    CHECK(member.game.bolt_count == 1 && host.game.bolt_count == 2 && member.world.spawned() == 0);
}


// The party's progress: a gold bolt one player collects counts for everyone (its flag set in every
// game) and is gone from every game; its pickup scene is the collector's alone (its game stays in
// play: nothing shared, nobody held, no figure hidden). An item or a planet one player gets, every
// player has; given while in control only; a late member gets it all too.
void progress_is_the_partys() {
    Player host(1), a(2), b(3);
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    for (Player* p : {&host, &a, &b}) {
        p->session.handle(info(all));
    }
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &a, &b});
    tick({&host, &a, &b}, 16);

    // Level 1's two gold bolts (keys 1 and 2: gold bolt flags 4 and 5) in every game.
    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced;
    for (Player* p : {&host, &a, &b}) {
        p->game.now = {mp::kPhasePlaying, 1, 2};
        p->game.level = {moby(1, 1, 1134, runs, 20.0f), moby(2, 2, 1134, runs, 30.0f),
                         moby(3, 3, 77, mp::kMobyPlaced, 40.0f)};  // and a rock (a listing never empty)
        p->game.calls.clear();
    }
    for (int i = 0; i < 20; ++i) {
        tick({&host, &a, &b}, 50);
    }
    CHECK(host.game.given.empty() && a.game.given.empty() && b.game.given.empty());

    // A collects the first: its scene plays in its game alone (its game in play, no scene of the
    // level's), then the flag is set and the bolt gone there.
    for (int i = 0; i < 10; ++i) {
        tick({&host, &a, &b}, 50);
    }
    a.game.owned.gold[4] = 1;
    a.game.die(1);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &a, &b}, 50);
    }
    CHECK(was_given(host.game, "gold 4") && was_given(b.game, "gold 4") && !was_given(a.game, "gold 4"));
    CHECK(host.game.owned.gold[4] == 1 && b.game.owned.gold[4] == 1);
    CHECK(host.game.moby_slot(1) == -1 && b.game.moby_slot(1) == -1);
    for (Player* p : {&host, &a, &b}) {
        CHECK(!asked_scene(p->game) && !p->coop.hold_input() && p->game.hidden_figures.empty());
    }

    // The host collects the second: the same, from the host.
    host.game.owned.gold[5] = 1;
    host.game.die(2);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &a, &b}, 50);
    }
    CHECK(was_given(a.game, "gold 5") && was_given(b.game, "gold 5") && !was_given(host.game, "gold 5"));
    CHECK(a.game.moby_slot(2) == -1 && b.game.moby_slot(2) == -1);
    for (Player* p : {&host, &a, &b}) {
        CHECK(!asked_scene(p->game) && !p->coop.hold_input());
    }

    // B gets a weapon (item 10) and the host a planet (3): every game has both. A's menu is up as
    // the host's arrive: given once it is back in play.
    a.game.now.phase = mp::kPhaseMenu;
    b.game.owned.items[10] = 1;
    host.game.owned.planets[3] = 1;
    for (int i = 0; i < 20; ++i) {
        tick({&host, &a, &b}, 50);
    }
    CHECK(was_given(host.game, "item 10") && was_given(b.game, "planet 3") && !was_given(b.game, "item 10"));
    CHECK(a.game.owned.items[10] == 0 && a.game.owned.planets[3] == 0);
    a.game.now.phase = mp::kPhasePlaying;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &a, &b}, 50);
    }
    CHECK(a.game.owned.items[10] == 1 && a.game.owned.planets[3] == 1);
    CHECK(host.progress.here() == a.progress.here() && a.progress.here() == b.progress.here());

    // A fourth player joins while the game is on: started with the host's live save, and given
    // what the party has once it plays.
    Player late(4);
    const std::vector<mp::Member> four = {{1, "P1"}, {2, "P2"}, {3, "P3"}, {4, "P4"}};
    late.session.handle(info(four));
    mp::Message joined;
    joined.kind = mp::Message::Kind::kPartyJoined;
    joined.id = 4;
    joined.text = "P4";
    for (Player* p : {&host, &a, &b}) {
        p->session.handle(joined);
    }
    host.game.live_save = Bytes(300, 7);
    tick({&host, &a, &b, &late}, 16);
    tick({&host, &a, &b, &late}, 16);
    CHECK(late.game.started_save == host.game.live_save);
    late.game.now = {mp::kPhasePlaying, 1, 2};
    for (int i = 0; i < 20; ++i) {
        tick({&host, &a, &b, &late}, 50);
    }
    CHECK(late.game.owned.gold[4] == 1 && late.game.owned.gold[5] == 1);
    CHECK(late.game.owned.items[10] == 1 && late.game.owned.planets[3] == 1);
    // Nothing given twice, and nothing taken.
    CHECK(std::count(host.game.given.begin(), host.game.given.end(), "item 10") == 1);
    CHECK(b.game.owned.items[10] == 1 && host.game.owned.gold[5] == 1);
}


// A long session: the server's clock is asked again every kClockMs (TIME frames), the buffers stay
// bounded however long poses come.
void a_long_session() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = member.game.now = {mp::kPhasePlaying, 0, 2};
    int times = 0;
    for (int i = 0; i < 2000; ++i) {  // 100 seconds
        host.game.hero_pose.position[0] = static_cast<float>(i) * 0.05f;
        for (Player* p : {&host, &member}) {
            p->clock += 50;
            p->coop.tick(p->clock);
            for (const Bytes& f : p->link.frames) {
                times += f.size() > 4 && f[4] == static_cast<std::uint8_t>(mp::PacketId::kTime) ? 1 : 0;
            }
        }
        route({&host, &member});
    }
    CHECK(times >= 6);  // each game about 3 times in 100 s
    CHECK(member.coop.remotes().at(1).poses.size() <= mp::kBufferSamples);
    CHECK(member.coop.figures() == 1);
}
}  // namespace

void run_bolts_tests() {
    bolts_are_the_hosts();
    bolts_go_to_the_nearest();
    taken_bolts_fly_to_the_taker();
    progress_is_the_partys();
    a_long_session();
}

}  // namespace mpt
