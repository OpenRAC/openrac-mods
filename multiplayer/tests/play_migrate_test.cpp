// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: a connection lost and found again, the host's
// among them (migrate.hpp).
#include "takeover_sim.hpp"

namespace mpt {
namespace {

constexpr std::uint32_t kRuns = mp::kMobyUpdates | mp::kMobyPlaced;

/* A party of three in level 3, all given the world; offers RESUME as a server that keeps slots. */
struct Trio {
    Player host{1}, a{2}, b{3};
    std::vector<Player*> all{&host, &a, &b};
    const std::vector<mp::Member> list = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    SimWorld w{4, 4, 2.0f};

    Trio() {
        for (Player* p : all) {
            gate(*p, sample_list());
        }

        for (Player* p : all) {
            p->session.handle(info(list));
        }

        offer_resume(all);
        tick_mods(all, 16);
        tick_mods(all, 16);
        a.session.toggle_ready();
        b.session.toggle_ready();
        route(all);
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route(all);
        tick(all, 16);

        for (Player* p : all) {
            p->game.now = {mp::kPhasePlaying, 3, 2};
            p->game.level = w.made(0);
            p->game.hero_pose.position[0] = 5.0f * static_cast<float>(p->id);
            p->game.hero_pose.flags = mp::kPoseSteady;
        }

        run(all, 40);
        CHECK(a.world.synced() && b.world.synced());
    }

    void run(const std::vector<Player*>& who, int ticks, int ms = 50) {
        for (int i = 0; i < ticks; ++i) {
            tick_mods(who, ms);
        }
    }

    std::vector<Player*> without(const Player& gone) {
        std::vector<Player*> out;

        for (Player* p : all) {
            if (p != &gone) {
                out.push_back(p);
            }
        }

        return out;
    }

    mp::Message party(std::int32_t host_id = 1, std::int32_t epoch = 0) {
        mp::Message m = info(list);
        m.host = host_id;
        m.epoch = epoch;
        return m;
    }
};

bool was_called(const FakeGame& game, const std::string& start) {
    return std::any_of(game.calls.begin(), game.calls.end(), [&](const std::string& c) { return c.rfind(start, 0) == 0; });
}

void a_member_dropped_keeps_its_place_and_goes_nowhere() {
    Trio t;
    const auto rest = t.without(t.a);
    lose(t.a, rest);

    // Everything of the party stands, its id with it; nothing it sends goes.
    CHECK(t.a.session.in_party() && t.a.session.dropped() && t.a.session.id() == 2);
    t.a.session.send_data(1, Bytes{mp::kind::kPose, 1});
    t.a.session.flush();
    CHECK(t.a.link.frames.empty());
    tick({&t.a}, 16);
    CHECK(t.a.migrate.banner(t.a.clock) == mp::kDroppedLine);
    CHECK(t.host.session.away(2) && t.b.session.away(2));

    // It stands where it was, past the time silence takes a figure away; and in nobody's way.
    t.run(rest, 140);
    CHECK(t.host.coop.figure_of(2) >= 0);
    mp::Pose pose;
    CHECK(!t.host.coop.latest(2, pose));
    CHECK(t.host.session.members().size() == 3);
}

void a_member_comes_back_as_a_late_joiner_does() {
    Trio t;
    const auto rest = t.without(t.a);
    t.a.game.placeable = true;
    lose(t.a, rest);
    t.run(rest, 40);

    // RESUME names its old session, with its token, and only once.
    t.a.session.connected();
    mp::Message hello;
    hello.kind = mp::Message::Kind::kHello;
    hello.id = 77;
    t.a.link.frames.clear();
    t.a.session.handle(hello);
    CHECK(!t.a.link.frames.empty() && t.a.link.frames[0] == mp::party_resume("ABCDEF", 2, 1002));
    CHECK(t.a.session.id() == 2 && t.a.session.dropped());
    t.a.link.frames.clear();
    hello.id = 2;
    t.a.session.handle(hello);
    t.a.session.handle(udp_offer_of(1002));
    CHECK(t.a.session.dropped());
    t.a.session.handle(t.party());
    server_away(rest, 2, true);
    t.a.link.frames.clear();

    CHECK(!t.a.session.dropped() && t.a.session.id() == 2 && t.a.session.in_party());
    CHECK(!t.host.session.away(2));

    // In its game still, it asks for the world and is held while it comes.
    tick(t.all, 16);
    CHECK(t.a.coop.joining() && t.a.coop.quiet_gifts());
    CHECK(!t.a.world.synced());
    t.run(t.all, 40);
    CHECK(t.a.world.synced());
    CHECK(!t.a.game.placed.empty());
}

void a_slot_refused_or_out_of_time_ends_the_party_here() {
    {
        Trio t;
        lose(t.a, t.without(t.a));
        t.a.session.connected();
        mp::Message error;
        error.kind = mp::Message::Kind::kError;
        error.reason = mp::ErrorReason::kResumeRefused;
        t.a.session.handle(error);
        CHECK(!t.a.session.in_party() && !t.a.session.dropped());
        CHECK(t.a.session.take_notice() == mp::kLostSlotNotice);
    }

    Trio t;
    lose(t.a, t.without(t.a));
    t.a.clock += mp::kResumeMs - 1;
    t.a.session.flush();
    CHECK(t.a.session.in_party());
    t.a.clock += 1;
    t.a.session.flush();
    CHECK(!t.a.session.in_party());
    CHECK(t.a.session.take_notice() == mp::kLostSlotNotice);
}

void a_server_that_keeps_no_slots_ends_the_party_as_ever() {
    Trio t;
    t.a.session.handle([] {
        mp::Message m;
        m.kind = mp::Message::Kind::kUdp;
        m.features = mp::kFeatureKick;
        return m;
    }());
    t.a.session.disconnected();
    CHECK(!t.a.session.in_party() && !t.a.session.dropped());
}

void the_party_waits_for_a_host_that_drops() {
    Trio t;
    const auto rest = t.without(t.host);
    lose(t.host, rest);
    tick(rest, 16);

    for (Player* p : rest) {
        CHECK(p->coop.hold_input());
        CHECK(p->migrate.host_gone());
        CHECK(p->migrate.banner(p->clock).find("The host lost its connection") != std::string::npos);
    }

    // Its enemies stand in the members' games; the figure stands too.
    t.run(rest, 100);
    CHECK(t.a.coop.hold_input());

    // Back in time: it is the host still, and every member asks for the world.
    t.run({&t.host}, 20);
    come_back(t.host, rest, t.party());
    CHECK(t.host.session.is_host());
    tick(t.all, 16);
    CHECK(!t.a.coop.hold_input() && !t.b.coop.hold_input());
    CHECK(t.a.migrate.banner(t.a.clock).empty());
    t.run(t.all, 40);
    CHECK(t.a.world.synced() && t.b.world.synced());
    CHECK(t.a.world.driven() > 0);
}

void what_went_to_a_cut_stream_comes_with_the_world_again() {
    Trio t;
    const auto rest = t.without(t.host);
    lose(t.host, rest);

    // The host's game goes on: a moby dies there, and nobody can be told.
    const int slot = t.host.game.moby_slot(1);
    t.host.game.die(slot);
    t.run({&t.host}, 20);
    CHECK(t.a.game.moby_slot(1) >= 0);
    come_back(t.host, rest, t.party());
    t.run(t.all, 40);
    CHECK(t.a.game.moby_slot(1) < 0);
}

void a_ledger_reads_back_as_written_and_a_broken_one_is_refused() {
    mp::Ledger l;
    l.epoch = 3;
    l.generation = 70000;
    l.play = -5;
    l.travel = 9;
    l.scene = 4;
    l.world.level = 7;
    l.world.revive = true;
    l.world.next_net = 300;
    l.world.dead = {1, 2, 900};
    l.world.dug = {5};
    l.world.spared = {6};
    mp::LedgerMoby m;
    m.net = 4;
    m.oclass = 1234;
    m.position[1] = 2.5f;
    m.yaw = 1.0f;
    l.world.spawned.push_back(m);
    const Bytes bytes = mp::encode_ledger(l);
    mp::Ledger back;
    CHECK(mp::decode_ledger(bytes, back));
    CHECK(back.epoch == 3 && back.generation == 70000 && back.play == -5 && back.travel == 9 && back.scene == 4);
    CHECK(back.world.level == 7 && back.world.revive && back.world.next_net == 300);
    CHECK(back.world.dead == l.world.dead && back.world.dug == l.world.dug && back.world.spared == l.world.spared);
    CHECK(back.world.spawned.size() == 1 && back.world.spawned[0].oclass == 1234 && back.world.spawned[0].position[1] == 2.5f);

    // Cut anywhere, it is none; of another version, it is none.
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CHECK(!mp::decode_ledger(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)), back));
    }

    Bytes other = bytes;
    other[1] = mp::kLedgerVersion + 1;
    CHECK(!mp::decode_ledger(other, back));

    // Newer by its epoch first, and a generation only within one.
    mp::Ledger a, b;
    a.epoch = 2;
    a.generation = 1;
    b.epoch = 1;
    b.generation = 900;
    CHECK(mp::ledger_newer(a, b) && !mp::ledger_newer(b, a));
    b.epoch = 2;
    CHECK(mp::ledger_newer(b, a));
    CHECK(!mp::ledger_newer(a, a));
}

void a_successor_is_chosen_by_its_link() {
    mp::Link near{20, true}, far{200, true}, tcp{50, false}, unknown{};
    CHECK((mp::successor_order({{2, far}, {3, near}, {4, tcp}, {5, unknown}}) == std::vector<std::int32_t>{3, 4, 2, 5}));
    // The same link: who came first.
    CHECK((mp::successor_order({{7, near}, {6, near}}) == std::vector<std::int32_t>{7, 6}));
}

Bytes first_party_frame(Player& p) {
    p.session.flush();

    for (const Bytes& frame : p.link.frames) {
        if (frame.size() > 5 && frame[4] == static_cast<std::uint8_t>(mp::PacketId::kParty)
            && frame[5] == static_cast<std::uint8_t>(mp::PartyOp::kHostChanged)) {
            return frame;
        }
    }

    return {};
}

void the_host_names_its_followers_and_the_host_alone_is_heard() {
    Trio t;
    t.a.session.report_link(200, true);
    t.b.session.report_link(20, true);
    route(t.all);
    t.host.clock += mp::kSuccessorMs;
    t.host.link.frames.clear();
    t.host.migrate.tick(t.host.clock);
    CHECK(first_party_frame(t.host) == mp::party_successors({3, 2}));
    CHECK(t.host.migrate.followed());
    route(t.all);
    CHECK(t.a.migrate.banner(t.a.clock).empty());

    // Unchanged, it says nothing; one who drops comes off the list.
    t.host.clock += mp::kSuccessorMs;
    t.host.link.frames.clear();
    t.host.migrate.tick(t.host.clock);
    CHECK(first_party_frame(t.host).empty());
    server_away({&t.host}, 3);
    t.host.clock += mp::kSuccessorMs;
    t.host.migrate.tick(t.host.clock);
    t.host.migrate.tick(t.host.clock + mp::kSuccessorMs);
    CHECK(first_party_frame(t.host) == mp::party_successors({2}));
}

/* A party whose host dropped for good, and the one taking over told. */
void lose_host(Trio& t, Player& next) {
    auto rest = t.without(t.host);
    lose(t.host, rest);
    t.run(rest, 4);
    server_host(rest, next.id, 1);
}

void the_new_host_carries_the_party_on() {
    Trio t;
    t.run(t.all, 80);  // a ledger, and the list
    auto rest = t.without(t.host);

    // The host's world as the ledger has it: a moby dead, a spawned one.
    t.host.game.die(t.host.game.moby_slot(0));
    t.run(t.all, 40);
    CHECK(t.a.migrate.ledger().generation > 0);
    CHECK(t.a.migrate.ledger().world.dead.count(0) != 0);
    const std::size_t driven = t.a.world.driven();
    CHECK(driven > 0);
    lose_host(t, t.a);
    CHECK(t.a.session.is_host() && t.b.session.host() == 2);

    // It runs nothing of a host's until it knows what the party knows.
    tick(rest, 16);
    CHECK(t.a.coop.between_hosts() && t.a.coop.hold_input());
    t.run(rest, 40);
    CHECK(t.a.world.hosting() && !t.a.coop.between_hosts() && !t.a.coop.hold_input());
    CHECK(t.a.world.driven() == 0);
    CHECK(t.a.migrate.took_over());
    CHECK(t.a.migrate.banner(t.a.clock) == mp::kYouHostLine);

    // The other member gets the world from it, the one way a world arrives, and nobody is held.
    t.run(rest, 40);
    CHECK(t.b.world.synced() && !t.b.coop.hold_input());
    CHECK(t.b.world.driven() > 0);
    CHECK(t.b.game.moby_slot(0) < 0);
    CHECK(t.b.session.stale() == 0);
}

void an_old_hosts_traffic_is_refused() {
    Trio t;
    t.run(t.all, 80);
    lose_host(t, t.a);
    auto rest = t.without(t.host);
    t.run(rest, 40);
    const std::size_t stale = t.b.session.stale();

    // Sent as the first host would, and as a host of the old epoch would, however it came to arrive.
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = 2;
    m.bytes = Bytes{mp::kind::kReset, 3};
    t.b.session.handle(m);
    CHECK(t.b.session.stale() == stale + 1);
    m.bytes = Bytes{mp::kind::kEpoch, 0, 0, mp::kind::kReset, 3};
    t.b.session.handle(m);
    CHECK(t.b.session.stale() == stale + 2);
    CHECK(t.b.world.synced());

    // The new host's own says the new epoch.
    t.a.session.send_data(0, Bytes{mp::kind::kReset, 3});
    t.a.session.flush();
    t.run(rest, 1);
    CHECK(t.b.session.stale() == stale + 2);

    // An old ledger, however it arrives, puts nothing back.
    mp::Ledger old = t.b.migrate.ledger();
    --old.epoch;
    old.generation += 1000;
    old.world.dead.clear();
    const std::uint32_t kept = t.b.migrate.ledger().generation;
    t.b.migrate.handle(2, mp::encode_ledger(old));
    CHECK(t.b.migrate.ledger().generation == kept);
}

void a_successor_that_cannot_declines_and_the_next_one_goes_on() {
    Trio t;
    t.run(t.all, 80);
    auto rest = t.without(t.host);
    // The first in line went back to the title: it has no world to carry on.
    t.a.game.now.phase = mp::kPhaseFrontEnd;
    t.a.coop.rejoin();
    tick(rest, 16);
    lose_host(t, t.a);
    t.a.link.frames.clear();
    t.a.migrate.tick(t.a.clock);
    CHECK(first_party_frame(t.a) == mp::party_decline());
    CHECK(!t.a.migrate.took_over());
    server_host(rest, 3, 2);
    t.run(rest, 40);
    CHECK(t.b.migrate.took_over() && t.b.world.hosting());
}

void a_newer_ledger_held_by_another_member_is_taken() {
    Trio t;
    t.run(t.all, 80);
    auto rest = t.without(t.host);

    // The last ledger reached the second member only.
    mp::Ledger newer = t.b.migrate.ledger();
    ++newer.generation;
    newer.world.dead.insert(2);
    t.b.migrate.handle(1, mp::encode_ledger(newer));
    CHECK(t.b.migrate.ledger().generation == newer.generation);
    CHECK(t.a.game.moby_slot(2) >= 0);
    lose_host(t, t.a);
    t.run(rest, 40);
    CHECK(t.a.world.hosting());
    CHECK(t.a.game.moby_slot(2) < 0);
    CHECK(t.b.game.moby_slot(2) < 0);
}

void what_the_old_host_spawned_goes_on_under_its_id() {
    Trio t;
    t.host.game.level.push_back(moby(60, -1, 700, mp::kMobyUpdates | mp::kMobyCreature, 9.0f));
    t.run(t.all, 40);
    CHECK(t.a.world.spawned() == 1 && t.b.world.spawned() == 1);
    t.run(t.all, 40);
    auto rest = t.without(t.host);

    // (The port says what a class is; this game's copy is made as the host's was.)
    t.a.game.level.back().flags |= mp::kMobyCreature;
    lose_host(t, t.a);
    t.run(rest, 80);

    // One copy in each game, not a second under a new id, and the new host's id goes on from there.
    CHECK(count_of_class(t.a.game, 700) == 1);
    CHECK(count_of_class(t.b.game, 700) == 1);
    CHECK(t.b.world.spawned() == 1);
}

void a_grabbed_player_is_let_go_by_a_change_of_host() {
    Trio t;
    t.run(t.all, 80);
    auto rest = t.without(t.host);
    t.a.game.holds_ok = true;
    t.a.world.handle(1, Bytes{mp::kind::kHold, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(was_called(t.a.game, "grab"));
    t.a.game.calls.clear();
    lose_host(t, t.a);
    t.run(rest, 40);
    CHECK(count_of(t.a.game, "let go") == 1);
}

void the_rules_outlive_their_host() {
    Trio t;
    mp::PartyRules rule;
    rule.id = "difficulty-mod";
    rule.data = {1, 2, 3};
    rule.text = "Hard";
    t.host.game.published = {rule};
    t.a.game.published.clear();
    t.b.game.published.clear();
    tick_mods(t.all, 2000);
    tick_mods(t.all, 2000);
    CHECK(t.a.mods.rules().size() == 1);
    t.run(t.all, 80);
    auto rest = t.without(t.host);
    lose_host(t, t.a);
    t.run(rest, 40);
    tick_mods(rest, 2000);
    tick_mods(rest, 2000);

    // What it was given stands, and is passed on though its own mods publish nothing of the kind.
    CHECK(t.a.mods.rules().size() == 1 && t.a.mods.rules()[0].data == rule.data);
    CHECK(t.b.mods.rules().size() == 1);
    CHECK(t.b.mods.fit(3) == mp::Fit::kMatch || t.b.mods.fit(2) == mp::Fit::kMatch);
    CHECK(t.a.mods.may_play(3));
}

void a_save_on_its_way_is_dropped_and_the_slot_keeps_its_last_whole_one() {
    Trio t;
    t.run(t.all, 80);
    auto rest = t.without(t.host);
    const std::uint8_t tag = 9;
    Bytes piece = mp::Writer().u8(mp::kind::kCoSave).u8(tag).i32(2).i32(0).raw(Bytes(mp::kChunkBytes, 5)).bytes();
    const Bytes before = t.b.game.card_read(t.b.cosave.slot());
    t.b.cosave.handle(1, piece);
    lose_host(t, t.a);
    t.run(rest, 40);

    // The rest of that save, had it come, would make nothing whole.
    t.b.cosave.handle(2, mp::Writer().u8(mp::kind::kCoSave).u8(tag).i32(2).i32(1).raw(Bytes(10, 5)).bytes());
    CHECK(t.b.game.card_read(t.b.cosave.slot()) == before);
}

void an_old_host_back_late_comes_in_as_anyone() {
    Trio t;
    t.run(t.all, 80);
    auto rest = t.without(t.host);
    t.host.game.placeable = true;
    lose_host(t, t.a);
    t.run(rest, 40);
    come_back(t.host, rest, t.party(2, 1));
    CHECK(!t.host.session.is_host() && t.host.session.host() == 2);
    t.host.game.calls.clear();
    t.run(t.all, 4);
    CHECK(count_of(t.host.game, "travel -1") >= 1);
    t.host.game.now = {mp::kPhaseFrontEnd, -1, 3};
    t.run(t.all, 120);
    CHECK(was_called(t.host.game, "start"));
}

void a_host_that_named_nobody_ends_the_party_and_the_slot_goes_on() {
    Trio t;
    t.run(t.all, 80);
    mp::Message end;
    end.kind = mp::Message::Kind::kPartyEnd;
    t.a.session.handle(end);
    CHECK(!t.a.session.in_party());
    CHECK(t.a.session.take_notice().find("over") != std::string::npos);
    tick({&t.a}, 16);
    CHECK(!t.a.coop.hold_input());
}

void the_host_going_at_any_moment_leaves_one_world() {
    for (const HostGone how : kHostGoneAll) {
        for (unsigned seed = 1; seed <= 2; ++seed) {
            SimLink link;
            link.loss = 0.05;
            link.latency = 40.0;
            link.jitter = 20.0;
            const TakeoverResult r = run_takeover(link, seed, 2, how);

            if (r.bad()) {
                std::printf("%s seed %u: hosted %d stuck %d crates %d dead %d place %d saves %d spawned %d held %d\n",
                            host_gone_name(how), seed, r.hosted, r.stuck, r.wrong_crates, r.wrong_dead, r.wrong_place,
                            r.torn_saves, r.spawned_apart, r.held_on);
            }

            CHECK(!r.bad());
        }
    }
}

}  // namespace

void run_migrate_tests() {
    a_member_dropped_keeps_its_place_and_goes_nowhere();
    a_member_comes_back_as_a_late_joiner_does();
    a_slot_refused_or_out_of_time_ends_the_party_here();
    a_server_that_keeps_no_slots_ends_the_party_as_ever();
    the_party_waits_for_a_host_that_drops();
    what_went_to_a_cut_stream_comes_with_the_world_again();
    a_ledger_reads_back_as_written_and_a_broken_one_is_refused();
    a_successor_is_chosen_by_its_link();
    the_host_names_its_followers_and_the_host_alone_is_heard();
    the_new_host_carries_the_party_on();
    an_old_hosts_traffic_is_refused();
    a_successor_that_cannot_declines_and_the_next_one_goes_on();
    a_newer_ledger_held_by_another_member_is_taken();
    what_the_old_host_spawned_goes_on_under_its_id();
    a_grabbed_player_is_let_go_by_a_change_of_host();
    the_rules_outlive_their_host();
    a_save_on_its_way_is_dropped_and_the_slot_keeps_its_last_whole_one();
    an_old_host_back_late_comes_in_as_anyone();
    a_host_that_named_nobody_ends_the_party_and_the_slot_goes_on();
    the_host_going_at_any_moment_leaves_one_world();
}

}  // namespace mpt
