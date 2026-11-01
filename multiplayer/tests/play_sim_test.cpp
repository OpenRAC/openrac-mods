// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: the world over simulated links: respawns, deaths, late members and bad networks.
#include "test_support.hpp"

namespace mpt {
namespace {


void id_mapping_survives_a_respawn() {
    // The member's level is packed differently after its death: the same key is another slot.
    Player host(1), member(2);
    std::vector<Player*> all = {&host, &member};
    join_party(all, {{1, "P1"}, {2, "P2"}});
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route(all);
    SimWorld w{4, 4};  // keys 0 to 3 enemies, 4 to 7 crates
    host.game.now = member.game.now = {mp::kPhasePlaying, 3, 2};
    host.game.level = w.made(0);
    member.game.level = w.made(0);
    host.game.hero_pose.position[0] = 100.0f;
    member.game.hero_pose.position[0] = 12.0f;

    for (int i = 0; i < 20; ++i) {
        tick(all, 50);
    }

    CHECK(member.world.synced() && member.game.drives.size() == 4);
    // Two crates go: one by the host, one by the member.
    host.game.die(host.game.moby_slot(5));
    member.game.die(member.game.moby_slot(6));

    for (int i = 0; i < 80; ++i) {
        tick(all, 50);

        for (Player* p : all) {
            for (const mp::Hit& h : p->game.hits) {
                p->game.die(h.victim);
            }

            p->game.hits.clear();
        }
    }

    CHECK(by_key(host.game, 6) != nullptr && (by_key(host.game, 6)->flags & mp::kMobyDead) != 0);
    CHECK((by_key(member.game, 5)->flags & mp::kMobyDead) != 0);
    // A spawned moby of the host's is known to the member.
    host.game.level.push_back(moby(300, -1, 13, mp::kMobyUpdates, 11.0f));
    tick(all, 50);
    tick(all, 50);
    CHECK(member.world.spawned() == 1);

    // The member dies: record 0 skipped, so every later record sits a slot lower, everything back
    // but that, its slots' old drives gone, the load's count up, the level the same.
    const std::size_t kills = member.game.world_calls.size();
    remake(member, w, {0});
    CHECK(member.game.moby_slot(1) == 0 && member.game.moby_slot(5) == 4);
    // A message read before the tick has noticed: the spawned slot it names is gone, and is left
    // alone (in the game, a deleted moby past the array's end stops the program).
    mp::Writer despawn;
    despawn.u8(static_cast<std::uint8_t>(mp::WorldKind::kDespawn)).u8(3).u8(0).u8(1).u8(0).u8(1);
    mp::Message early;
    early.kind = mp::Message::Kind::kData;
    early.id = 1;
    early.bytes = despawn.bytes();
    member.session.handle(early);
    CHECK(member.game.world_calls.size() == kills);
    CHECK(member.world.spawned() == 0);
    tick(all, 50);
    tick(all, 50);
    CHECK(member.world.spawned() == 1);

    for (int i = 0; i < 40; ++i) {
        tick(all, 50);
    }

    // Driven again, by the new slots: keys 1 to 3 at 0 to 2.
    CHECK((member.game.drives == std::set<int>{0, 1, 2}));
    // The crates dead for the party are dead here again, without a BROKE back.
    CHECK(member.game.moby_slot(5) == -1 && member.game.moby_slot(6) == -1);
    CHECK(member.game.moby_slot(4) >= 0 && member.game.moby_slot(7) >= 0);
    CHECK(member.world.synced() && member.world.spawned() == 1);
    // A key the host poses reaches the moby of that key, not the slot it used to be.
    for (mp::MobyState& m : host.game.level) {
        if (m.key == 2) {
            m.pose.position[1] = 50.0f;
        }
    }

    for (int i = 0; i < 20; ++i) {
        tick(all, 50);
    }

    CHECK(std::fabs(by_key(member.game, 2)->pose.position[1] - 50.0f) < 1.0f);
    CHECK(std::fabs(by_key(member.game, 3)->pose.position[1]) < 1.0f);
}


void a_host_death_loses_the_party_nothing() {
    Player host(1), member(2);
    std::vector<Player*> all = {&host, &member};
    join_party(all, {{1, "P1"}, {2, "P2"}});
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route(all);
    SimWorld w{2, 4};
    host.game.now = member.game.now = {mp::kPhasePlaying, 3, 2};
    host.game.level = w.made(0);
    member.game.level = w.made(0);
    host.game.level.push_back(moby(300, -1, 13, mp::kMobyUpdates, 11.0f));

    for (int i = 0; i < 20; ++i) {
        tick(all, 50);
    }

    host.game.die(host.game.moby_slot(3));

    for (int i = 0; i < 80; ++i) {
        tick(all, 50);

        for (const mp::Hit& h : member.game.hits) {
            member.game.die(h.victim);
        }

        member.game.hits.clear();
    }

    CHECK(member.game.moby_slot(3) == -1 && member.world.spawned() == 1);
    // The host dies: its crate is back in its own game, its bolt gone.
    remake(host, w);
    CHECK(host.game.moby_slot(3) >= 0);

    for (int i = 0; i < 40; ++i) {
        tick(all, 50);
    }

    // Dead for the party: deleted again there. What the host spawned is gone from the member too.
    CHECK(host.game.moby_slot(3) == -1);
    CHECK(member.world.spawned() == 0);
    CHECK(member.world.synced());

    // A member's break that reaches the host just as its level is made again is not lost.
    host.game.hits.clear();
    const int crate = host.game.moby_slot(5);
    CHECK(crate >= 0);
    remake(host, w);
    mp::Message broke;
    broke.kind = mp::Message::Kind::kData;
    broke.id = 2;
    broke.bytes = mp::Writer().u8(static_cast<std::uint8_t>(mp::WorldKind::kBroke)).u8(3).u8(0).u8(5).bytes();
    host.session.handle(broke);
    tick(all, 50);
    tick(all, 50);
    CHECK(host.game.hits.size() == 1 && host.game.hits[0].victim == host.game.moby_slot(5));
}


void a_late_member_gets_the_world_and_waits_for_nobody() {
    Player host(1), late(3);
    std::vector<Player*> all = {&host, &late};
    host.session.handle(info({{1, "P1"}}));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    SimWorld w{2, 4};
    host.game.now = {mp::kPhasePlaying, 3, 2};
    host.game.level = w.made(0);
    host.game.level.push_back(moby(300, -1, 13, mp::kMobyUpdates, 11.0f));
    host.game.die(host.game.moby_slot(3));
    host.game.live_save = Bytes(100, 7);

    for (int i = 0; i < 20; ++i) {
        tick({&host}, 50);
    }

    // The host plays a scene when the member comes.
    host.game.now = {mp::kPhaseScene, 3, 2, 7};

    for (int i = 0; i < 4; ++i) {
        tick({&host}, 50);
    }

    mp::Message came;
    came.kind = mp::Message::Kind::kPartyJoined;
    came.id = 3;
    came.text = "P3";
    host.session.handle(came);
    late.session.handle(info({{1, "P1"}, {3, "P3"}}));
    late.game.now = {mp::kPhaseFrontEnd, -1, 0};
    tick(all, 50);
    tick(all, 50);
    tick(all, 50);
    CHECK(late.game.calls.size() >= 1 && late.game.calls[0] == "start 3 100");
    CHECK(late.coop.joining());

    // On its way, the game's own movies: Start goes through (and the pad's own is not taken out).
    late.game.now = {mp::kPhaseLoading, 3, 0};
    CHECK(!late.coop.start_blocked(mp::kPhaseLoading));
    int presses = 0;

    for (int i = 0; i < 20; ++i) {
        presses += late.coop.movie_press(late.clock) ? 1 : 0;
    }

    CHECK(presses == 10);
    // Its load: the ship's landing or a story scene of its own, no scene of the party's.
    late.game.level = w.made(0);
    late.game.level.push_back(moby(77, -1, 806, mp::kMobyUpdates, 3.0f));
    late.game.now = {mp::kPhaseScene, 3, 3, -1};
    tick(all, 50);
    CHECK(count_of(late.game, "skip") >= 1);
    const std::size_t asked = count_of(late.game, "scene 7");
    // The host's own barrier does not see it: it plays no scene as far as anyone waits.
    host.game.now = {mp::kPhasePlaying, 3, 2};
    late.game.now = {mp::kPhaseScene, 3, 3, -1};

    for (int i = 0; i < 20; ++i) {
        tick(all, 50);
        CHECK(!host.coop.hold_input());
    }

    // In control, in the host's level, with the host's world.
    late.game.now = {mp::kPhasePlaying, 3, 3};

    for (int i = 0; i < 40; ++i) {
        tick(all, 50);

        for (const mp::Hit& h : late.game.hits) {
            late.game.die(h.victim);
        }

        late.game.hits.clear();
    }

    CHECK(late.world.synced() && !late.coop.joining() && !late.coop.hold_input());
    CHECK(late.game.moby_slot(3) == -1);   // dead for the party
    CHECK(late.world.spawned() == 1);      // the host's bolt
    CHECK(count_of(late.game, "scene 7") == asked);  // never started the scene it missed
    CHECK(mp::owner_of(static_cast<std::uint8_t>(mp::GameKind::kPoses)) != mp::Owner::kWorld);
}


void a_crates_own_loot_is_not_the_hosts() {
    // A nanotech crate makes its pickup at its first update: every game's load has its own.
    Player host(1), member(2);
    std::vector<Player*> all = {&host, &member};
    join_party(all, {{1, "P1"}, {2, "P2"}});
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route(all);
    host.game.now = member.game.now = {mp::kPhasePlaying, 3, 2};
    host.game.level = {moby(0, 0, 501, mp::kMobyUpdates | mp::kMobyPlaced, 1.0f), moby(301, -1, 806, mp::kMobyUpdates, 1.0f)};
    member.game.level = host.game.level;

    for (int i = 0; i < 20; ++i) {
        tick(all, 50);
    }

    const auto spawns = [&member] {
        return std::count(member.game.world_calls.begin(), member.game.world_calls.end(), "spawn 806");
    };
    CHECK(spawns() == 0);
    // One an enemy drops later is shared as before.
    host.game.level.push_back(moby(302, -1, 806, mp::kMobyUpdates, 2.0f));
    tick(all, 50);
    tick(all, 50);
    CHECK(spawns() == 1);
}


void a_hello_ahead_of_the_first_pose_is_kept() {
    // UDP's pose is lost or late, TCP's HELLO is not: the world still goes once the pose comes.
    Player host(1), member(2);
    std::vector<Player*> all = {&host, &member};
    join_party(all, {{1, "P1"}, {2, "P2"}});
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route(all);
    host.game.now = {mp::kPhasePlaying, 3, 2};
    host.game.level = {moby(0, 0, 300, SimWorld::kRuns, 1.0f)};

    for (int i = 0; i < 4; ++i) {
        tick({&host}, 50);
    }

    // The member's HELLO reaches a host that has heard no pose from it.
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = 2;
    m.bytes = mp::Writer().u8(static_cast<std::uint8_t>(mp::WorldKind::kHello)).u8(3).i32(2).bytes();
    host.session.handle(m);
    tick({&host}, 50);
    host.link.frames.clear();
    // Then it stands in the level, and nothing asks again.
    member.game.now = {mp::kPhasePlaying, 3, 2};
    member.game.level = host.game.level;

    for (int i = 0; i < 20; ++i) {
        tick(all, 50);
    }

    for (mp::MobyState& e : host.game.level) {
        e.pose.position[1] = 30.0f;
    }

    for (int i = 0; i < 20; ++i) {
        tick(all, 50);
    }

    CHECK(std::fabs(by_key(member.game, 0)->pose.position[1] - 30.0f) < 1.0f);
}


void a_late_datagram_changes_nothing() {
    Player host(1), member(2);
    std::vector<Player*> all = {&host, &member};
    join_party(all, {{1, "P1"}, {2, "P2"}});
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route(all);
    host.game.now = member.game.now = {mp::kPhasePlaying, 3, 2};
    host.game.level = {moby(0, 0, 300, SimWorld::kRuns, 1.0f)};
    member.game.level = host.game.level;

    for (int i = 0; i < 10; ++i) {
        tick(all, 50);
    }

    mp::WorldEntry e;
    e.id = 0;
    e.pose.level = 3;
    e.hidden = true;
    const Bytes newer = mp::encode_world(3, 200000, {e});
    e.hidden = false;
    const Bytes older = mp::encode_world(3, 199000, {e});
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = 1;
    m.bytes = newer;
    member.session.handle(m);
    m.bytes = older;
    member.session.handle(m);
    member.world.tick(member.clock += 16);
    CHECK((by_key(member.game, 0)->flags & mp::kMobyHidden) != 0);
}


void bursts_stay_within_the_servers_rate() {
    Player host(1), member(2);
    std::vector<Player*> all = {&host, &member};
    join_party(all, {{1, "P1"}, {2, "P2"}});
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route(all);
    SimLink link;
    link.latency = 15;
    SimNet net(all, link, 3);
    host.game.now = member.game.now = {mp::kPhasePlaying, 3, 2};

    for (int i = 0; i < 60; ++i) {
        net.step(16);
    }

    // Everything an explosion can do in one tick: 400 bolts and creatures made, each a message.
    for (int i = 0; i < 400; ++i) {
        host.game.level.push_back(moby(1000 + i, -1, 13, mp::kMobyUpdates, static_cast<float>(i)));
    }

    for (int i = 0; i < 600; ++i) {
        net.step(16);
    }

    CHECK(net.data_drops == 0 && net.kicked == 0);
    CHECK(member.world.spawned() == 400);
    CHECK(host.session.counts().data_frames < 100);  // batched, far below one a message
}


void events_that_cannot_batch_wait_their_turn() {
    // One message each for another target than the last: no two share a DATA, so 600 are 600
    // frames at once, five times the server's burst. They arrive all the same, in order, at its rate.
    Player host(1), member(2), other(3);
    std::vector<Player*> all = {&host, &member, &other};
    join_party(all, {{1, "P1"}, {2, "P2"}, {3, "P3"}});
    SimLink link;
    link.latency = 15;
    SimNet net(all, link, 5);
    std::vector<int> heard;
    member.session.on_data([&heard](std::int32_t, const Bytes& b) { heard.push_back(b[1] << 8 | b[2]); });

    for (int i = 0; i < 600; ++i) {
        const auto high = static_cast<std::uint8_t>(i >> 8);
        host.session.send_data(i % 2 == 0 ? 2 : 3, {30, high, static_cast<std::uint8_t>(i)});
    }

    for (int i = 0; i < 1500; ++i) {
        net.step(16);
    }

    CHECK(net.data_drops == 0 && net.kicked == 0);
    CHECK(heard.size() == 300);

    for (std::size_t i = 0; i < heard.size(); ++i) {
        CHECK(heard[i] == static_cast<int>(2 * i));
    }
}


void blocked_udp_never_floods_tcp() {
    SimLink link;
    link.udp = false;
    link.latency = 30;
    SimNet* net = nullptr;
    const SimResult r = run_sim(link, 11, 3, 12.0, &net);
    CHECK(net->data_drops == 0 && net->kicked == 0);
    CHECK(r.wrong_crates == 0);
    std::printf("no UDP, 4 players: %ld DATA dropped, kicked %ld, %d crates %d hidden wrong\n",
                net->data_drops, net->kicked, r.wrong_crates, r.wrong_hidden);
}


void the_world_survives_a_bad_link() {
    std::printf("%-6s %-6s %-8s %-8s %-8s %-8s %-8s %-8s\n", "loss%", "ms", "place", "hidden", "crates", "stateDrop", "dataDrop", "lost");

    for (const double loss : {0.0, 1.0, 5.0, 15.0}) {
        for (const double latency : {30.0, 100.0, 250.0}) {
            SimLink link;
            link.loss = loss / 100.0;
            link.latency = latency / 2.0;
            link.jitter = latency / 5.0;
            link.reorder = 0.02;
            link.duplicate = 0.01;
            SimNet* net = nullptr;
            const SimResult r = run_sim(link, 1000 + static_cast<unsigned>(loss * 10 + latency), 3, 14.0, &net);
            std::printf("%-6.0f %-6.0f %-8d %-8d %-8d %-8ld %-8ld %-8ld\n", loss, latency, r.wrong_place, r.wrong_hidden,
                        r.wrong_crates, net->state_drops, net->data_drops, net->datagrams_lost);
            CHECK(r.wrong_place == 0 && r.wrong_hidden == 0 && r.wrong_crates == 0);
            CHECK(net->state_drops == 0 && net->data_drops == 0 && net->kicked == 0);
        }
    }
}
}  // namespace

void run_sim_tests() {
    id_mapping_survives_a_respawn();
    a_host_death_loses_the_party_nothing();
    a_late_member_gets_the_world_and_waits_for_nobody();
    a_crates_own_loot_is_not_the_hosts();
    a_hello_ahead_of_the_first_pose_is_kept();
    a_late_datagram_changes_nothing();
    bursts_stay_within_the_servers_rate();
    events_that_cannot_batch_wait_their_turn();
    blocked_udp_never_floods_tcp();
    the_world_survives_a_bad_link();
}

}  // namespace mpt
