// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: figures, ship travel and players who leave.
#include "test_support.hpp"

namespace mpt {
namespace {


// What a player holds reaches its figure with its pose; its shots are fired by its figure, a delay
// behind, in its level only.
void held_items_and_shots_reach_the_figure() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = {mp::kPhasePlaying, 0, 2};
    member.game.now = {mp::kPhasePlaying, 0, 2};
    host.game.hero_pose.held_count = 2;
    host.game.hero_pose.held[0] = piece(0, 0, 8, 0x47, 5);
    host.game.hero_pose.held[1] = piece(3, 1, 2, 0x3D9, 2);
    member.game.hero_pose.held_count = 1;
    member.game.hero_pose.held[0] = piece(0, 0, 0x0F, 0xA8, 5);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }

    // Each figure holds what its player holds.
    CHECK(member.game.figures.size() == 1 && host.game.figures.size() == 1);
    const mp::Pose& on_member = member.game.figures.begin()->second;
    CHECK(on_member.held_count == 2 && on_member.held[0].oclass == 0x47);
    CHECK(on_member.held[1].slot == 3 && on_member.held[1].oclass == 0x3D9);
    const mp::Pose& on_host = host.game.figures.begin()->second;
    CHECK(on_host.held_count == 1 && on_host.held[0].item == 0x0F);

    // The host takes out the Blaster: the member's figure of it holds it after its delay.
    host.game.hero_pose.held_count = 1;
    host.game.hero_pose.held[0] = piece(0, 0, 0x0F, 0xA8, 5);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    const mp::Pose& now_on_member = member.game.figures.begin()->second;
    CHECK(now_on_member.held_count == 1 && now_on_member.held[0].oclass == 0xA8);

    // The Blaster's recoil (its seq 2, a key every 4 ticks) plays on the member's figure of it, its
    // key and time running on between the poses.
    std::uint32_t seen_keys = 0;
    double last_place = -1.0;
    bool forward = true;
    for (int i = 0; i < 40; ++i) {
        const double place = i * 16.0 * 60.0 / 1000.0 * 0.25;
        host.game.hero_pose.held[0].seq = 2;
        host.game.hero_pose.held[0].frame = static_cast<std::uint32_t>(place);
        host.game.hero_pose.held[0].next_frame = host.game.hero_pose.held[0].frame + 1;
        host.game.hero_pose.held[0].anim_time = static_cast<float>(place - host.game.hero_pose.held[0].frame);
        host.game.hero_pose.held[0].anim_speed = 1.0f;
        tick({&host, &member}, 16);
        const mp::HeldPiece& shown = member.game.figures.begin()->second.held[0];
        if (shown.seq == 2) {
            const double at = shown.frame + static_cast<double>(shown.anim_time);
            forward = forward && at >= last_place - 0.02;
            seen_keys |= 1u << std::min<std::uint32_t>(shown.frame, 31);
            last_place = at;
        }
    }
    CHECK(forward && last_place > 1.0 && (seen_keys & 0x3u) == 0x3u);

    // The member fires: the host's figure of it fires the same shot, not at once (its poses'
    // delay), once.
    mp::Shot shot;
    shot.item = 0x0F;
    shot.position[0] = 12.0f;
    shot.aim[0] = 0.5f;
    member.game.shots_out.push_back(shot);
    tick({&host, &member}, 16);
    CHECK(member.coop.shots_sent() == 1 && host.game.shots_in.empty());
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(host.game.shots_in.size() == 1 && host.coop.shots_fired() == 1);
    CHECK(host.game.shots_in[0].first == host.game.figures.begin()->first);
    CHECK(host.game.shots_in[0].second.position[0] == 12.0f && host.game.shots_in[0].second.aim[0] == 0.5f);
    // The member's own game never fires its own shot again.
    CHECK(member.game.shots_in.empty());

    // The host's shot reaches the member the same way.
    host.game.shots_out.push_back(shot);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.shots_in.size() == 1);

    // A shot fired in another level than this game's is dropped; and the figure goes with a
    // level change, the pieces with it.
    host.game.now = {mp::kPhasePlaying, 1, 3};
    host.game.shots_out.push_back(shot);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.shots_in.size() == 1 && member.game.figures.empty());

    // Shots outside a level are taken and never sent.
    const std::uint64_t sent = member.coop.shots_sent();
    member.game.now = {mp::kPhaseFrontEnd, -1, 3};
    member.game.shots_out.push_back(shot);
    tick({&host, &member}, 50);
    CHECK(member.game.shots_out.empty() && member.coop.shots_sent() == sent);
}


// While a scene plays in this game the other players' figures are hidden, not removed, their
// pieces hidden with them, and their shots dropped; once it is over they are shown again.
void figures_hide_in_a_scene() {
    Player host(1), member(2);
    std::vector<std::string> member_log;
    member.coop.on_log([&](const std::string& l) { member_log.push_back(l); });
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = {mp::kPhasePlaying, 0, 2};
    member.game.now = {mp::kPhasePlaying, 0, 2};
    host.game.hero_pose.held_count = 2;
    host.game.hero_pose.held[0] = piece(0, 0, 8, 0x47, 5);
    host.game.hero_pose.held[1] = piece(3, 1, 2, 0x3D9, 2);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.figures.size() == 1);
    const int figure = member.game.figures.begin()->first;
    CHECK(member.game.hidden_figures.empty());
    CHECK((member.game.figures[figure].held[0].flags & mp::kHeldHidden) == 0);

    // A scene in the member's game: the host's figure hidden, kept, its pieces hidden too.
    member.game.now = {mp::kPhaseScene, 0, 2, 5};
    tick({&host, &member}, 50);
    CHECK(member.game.hidden_figures.count(figure) == 1 && member.game.figures.count(figure) == 1);
    CHECK(SceneParty::logged(member_log, "player 1 hidden for the scene"));
    CHECK(!SceneParty::logged(member_log, "player 1 no longer shown"));
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    const mp::Pose& hidden = member.game.figures[figure];
    CHECK(hidden.held_count == 2);
    CHECK((hidden.held[0].flags & mp::kHeldHidden) != 0 && (hidden.held[1].flags & mp::kHeldHidden) != 0);

    // The host fires meanwhile: dropped here, then and later.
    mp::Shot shot;
    shot.item = 0x0F;
    host.game.shots_out.push_back(shot);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.shots_in.empty());

    // The scene over: the same figure shown again, its pieces with it; no shot fired late.
    member.game.now = {mp::kPhasePlaying, 0, 2};
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.hidden_figures.empty() && member.game.figures.count(figure) == 1);
    CHECK(member.game.figures.size() == 1);
    CHECK(SceneParty::logged(member_log, "player 1 shown again"));
    CHECK((member.game.figures[figure].held[0].flags & mp::kHeldHidden) == 0);
    CHECK(member.game.shots_in.empty());
    // A shot now reaches the figure again.
    host.game.shots_out.push_back(shot);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.shots_in.size() == 1);
}


// The host's ship travel: a member's own is refused; the host's takes every member along at once,
// from a menu too, and the party arrives together: nobody goes on, no figure is shown and none of
// the host's world arrives until all are in the new level; a slow member is waited for, a member
// that never comes is not waited for ever.
void a_ship_travel_takes_the_party_along() {
    Player host(1), member(2);
    std::vector<std::string> host_log, member_log;
    host.coop.on_log([&](const std::string& l) { host_log.push_back(l); });
    member.coop.on_log([&](const std::string& l) { member_log.push_back(l); });
    const auto has = [](const std::vector<std::string>& log, const std::string& start) {
        return std::any_of(log.begin(), log.end(), [&](const std::string& l) { return l.rfind(start, 0) == 0; });
    };
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = member.game.now = {mp::kPhasePlaying, 11, 2};
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.coop.figures() == 1 && host.coop.figures() == 1);
    member.game.calls.clear();

    // A member's planet select: only the host flies; the same planet (a landing again) is the game's.
    CHECK(!member.coop.ship_travel(5, 11));
    CHECK(member.coop.take_notice() == "Only the host flies the ship to another planet");
    CHECK(member.coop.banner(member.clock) == "Only the host flies the ship to another planet");
    CHECK(member.coop.banner(member.clock + mp::kRefusedMs).empty());
    CHECK(member.coop.ship_travel(11, 11) && host.coop.ship_travel(11, 11));
    CHECK(!host.coop.arriving());
    tick({&host, &member}, 50);
    CHECK(member.game.calls.empty());

    // The member is in the pause menu when the host's ship leaves for 5: it leaves at once (asked
    // again a while later if its game has not gone), and the host's flight out pulls nobody back.
    member.game.now.phase = mp::kPhaseMenu;
    CHECK(host.coop.ship_travel(5, 11));
    CHECK(host.coop.arriving() && host.coop.waiting_for() == 1);
    host.game.now.phase = mp::kPhaseScene;
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK((member.game.calls == std::vector<std::string>{"travel 5"}));
    CHECK(member.coop.arriving() && has(member_log, "leaving level 11 for 5 (phase 5)"));
    tick({&host, &member}, 500);
    CHECK(member.game.calls.size() == 1);
    tick({&host, &member}, mp::kLeaveMs);
    CHECK(member.game.calls.size() == 2 && member.game.calls.back() == "travel 5");
    member.game.now = {mp::kPhaseLoading, 5, 3};
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(has(member_log, "left for level 5"));

    // The member lands first, and is out of its landing while the host still flies out: it waits,
    // with no figure and no travel back to the host's old level.
    member.game.now = {mp::kPhasePlaying, 5, 3};
    member.game.calls.clear();
    for (int i = 0; i < 100; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.calls.empty() && member.coop.hold_input() && member.coop.figures() == 0);
    CHECK(member.game.figures.empty());

    // The host on its way, then in 5 with a bolt it spawned there: everyone is in, so all go on,
    // the figures come back, and the host's world arrives only now.
    host.game.now = {mp::kPhaseLoading, 5, 2};
    tick({&host, &member}, 50);
    host.game.level = {moby(300, -1, 13, mp::kMobyUpdates, 11.0f)};
    host.game.now = {mp::kPhasePlaying, 5, 3};
    for (int i = 0; i < 40; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!host.coop.arriving() && !member.coop.arriving());
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());
    CHECK(has(host_log, "everyone is in level 5: going on"));
    CHECK(has(member_log, "everyone is in level 5: going on"));
    CHECK(member.coop.figures() == 1 && host.coop.figures() == 1);
    CHECK(member.world.spawned() == 1);
    CHECK(member.game.calls.empty());

    // The next travel, to 7: this time the host lands first; its control is held until the member
    // is in.
    member.game.now.phase = mp::kPhasePlaying;
    CHECK(host.coop.ship_travel(7, 5));
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK(member.game.calls.back() == "travel 7");
    member.game.now = {mp::kPhaseLoading, 7, 4};
    host.game.now = {mp::kPhasePlaying, 7, 4};
    for (int i = 0; i < 200; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(host.coop.arriving() && host.coop.hold_input() && host.coop.waiting_for() == 1);
    CHECK(has(host_log, "waiting for 1 players to arrive in level 7"));
    CHECK(host.coop.banner(host.clock) == "Waiting for the other players to arrive");
    member.game.now = {mp::kPhasePlaying, 7, 5};
    for (int i = 0; i < 40; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!host.coop.hold_input() && !member.coop.hold_input() && host.coop.figures() == 1);
    CHECK(host.coop.banner(host.clock).empty());

    // A member that never arrives (its game stuck in a load) does not hold the host for ever.
    CHECK(host.coop.ship_travel(9, 7));
    route({&host, &member});
    tick({&host, &member}, 16);
    member.game.now = {mp::kPhaseLoading, 9, 6};
    host.game.now = {mp::kPhasePlaying, 9, 5};
    tick({&host, &member}, 1000);
    CHECK(host.coop.hold_input());
    for (std::uint64_t t = 0; t <= mp::kArriveMs; t += 1000) {
        tick({&host, &member}, 1000);
    }
    CHECK(!host.coop.arriving() && !host.coop.hold_input());
}


// Three players: one member in the new level waits, given nothing of the host's world and shown
// nobody, until the slower one is in too.
void the_party_arrives_together() {
    Player host(1), fast(2), slow(3);
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    for (Player* p : {&host, &fast, &slow}) {
        p->session.handle(info(all));
    }
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &fast, &slow});
    tick({&host, &fast, &slow}, 16);
    for (Player* p : {&host, &fast, &slow}) {
        p->game.now = {mp::kPhasePlaying, 11, 2};
    }
    for (int i = 0; i < 20; ++i) {
        tick({&host, &fast, &slow}, 50);
    }
    CHECK(fast.coop.figures() == 2);

    CHECK(host.coop.ship_travel(5, 11));
    CHECK(host.coop.waiting_for() == 2);
    route({&host, &fast, &slow});
    tick({&host, &fast, &slow}, 16);
    CHECK(fast.game.calls.back() == "travel 5" && slow.game.calls.back() == "travel 5");
    for (Player* p : {&host, &fast, &slow}) {
        p->game.now = {mp::kPhaseLoading, 5, 3};
    }
    tick({&host, &fast, &slow}, 50);

    // The host and the fast member in 5 (the host's bolt there), the slow one still loading.
    host.game.level = {moby(300, -1, 13, mp::kMobyUpdates, 11.0f)};
    host.game.now = fast.game.now = {mp::kPhasePlaying, 5, 4};
    for (int i = 0; i < 100; ++i) {
        tick({&host, &fast, &slow}, 50);
    }
    CHECK(host.coop.arriving() && host.coop.waiting_for() == 1);
    CHECK(host.coop.hold_input() && fast.coop.hold_input());
    CHECK(fast.world.spawned() == 0 && fast.coop.figures() == 0 && host.coop.figures() == 0);

    // The slow one in: everyone goes on, the world and the figures come.
    slow.game.now = {mp::kPhasePlaying, 5, 4};
    for (int i = 0; i < 40; ++i) {
        tick({&host, &fast, &slow}, 50);
    }
    CHECK(!host.coop.arriving() && !fast.coop.arriving() && !slow.coop.arriving());
    CHECK(!host.coop.hold_input() && !fast.coop.hold_input() && !slow.coop.hold_input());
    CHECK(fast.world.spawned() == 1 && slow.world.spawned() == 1);
    CHECK(fast.coop.figures() == 2 && host.coop.figures() == 2);
}


// The host leaves (from its pause menu): the party ends, and the host and every member go back
// to the title and its Multiplayer page; nobody stays in a level alone.
void the_host_leaving_takes_everyone_home() {
    Party party;
    party.host.game.now = {mp::kPhaseMenu, 4, 2};
    party.a.game.now = {mp::kPhaseScene, 4, 2};
    tick(party.players(), 16);
    party.host.session.leave();
    mp::Message end;
    end.kind = mp::Message::Kind::kPartyEnd;
    party.a.session.handle(end);
    party.b.session.handle(end);
    tick(party.players(), 16);

    // Each leaves its level at once: from the pause menu, a scene, playing.
    for (Player* p : party.players()) {
        CHECK(!p->session.in_party() && p->coop.to_title());
        CHECK(p->game.calls.size() == 1 && p->game.calls[0] == "travel -1");
    }

    // Not asked again while the travel is under way; the page asked for once the title is up.
    tick(party.players(), mp::kLeaveMs);
    for (Player* p : party.players()) {
        CHECK(count_of(p->game, "travel -1") == 1 && count_of(p->game, "home") == 0);
        reach_title(*p);
        CHECK(!p->coop.to_title() && p->coop.wants_home() && count_of(p->game, "home") == 1);
    }

    // The main menu not up yet: asked again until the page shows, then no more.
    tick({&party.host}, mp::kHomeRetryMs);
    CHECK(count_of(party.host.game, "home") == 2);
    party.host.coop.home_shown();
    tick({&party.host}, mp::kHomeRetryMs);
    CHECK(count_of(party.host.game, "home") == 2 && !party.host.coop.wants_home());

    // Never shown: given up after a while.
    tick({&party.a}, mp::kHomeMs);
    CHECK(!party.a.coop.wants_home());
    const std::size_t asked = count_of(party.a.game, "home");
    tick({&party.a}, mp::kHomeRetryMs);
    CHECK(count_of(party.a.game, "home") == asked);

    // The player who starts a game from the title meanwhile keeps it.
    party.b.game.now = {mp::kPhaseLoading, -1, 4};
    tick({&party.b}, mp::kHomeRetryMs);
    CHECK(!party.b.coop.wants_home());
}


// A member leaves (from a scene): only it goes back to the title and its Multiplayer page; the
// host and the other member play on, together.
void a_member_leaving_goes_home_alone() {
    Party party;
    party.a.game.now = {mp::kPhaseScene, 4, 2};
    tick(party.players(), 16);
    party.a.session.leave();
    party.left(2);
    tick(party.players(), 16);
    CHECK(party.a.game.calls.size() == 1 && party.a.game.calls[0] == "travel -1");
    reach_title(party.a);
    CHECK(count_of(party.a.game, "home") == 1);

    for (int i = 0; i < 20; ++i) {
        tick({&party.host, &party.b}, 50);
    }

    CHECK(party.host.session.in_party() && party.host.session.members().size() == 2);
    CHECK(party.b.session.in_party() && party.b.coop.host_level() == 4);
    CHECK(party.host.coop.playing() && party.b.coop.playing());
    CHECK(party.host.coop.figures() == 1 && party.b.coop.figures() == 1);

    for (Player* p : {&party.host, &party.b}) {
        CHECK(count_of(p->game, "travel -1") == 0 && count_of(p->game, "home") == 0);
        CHECK(!p->coop.to_title() && !p->coop.wants_home());
    }
}


// The connection lost under a member (the host gone, or the server): it goes home as one that
// left; the server tells the others it left, and they play on.
void a_member_disconnected_goes_home_alone() {
    Party party;
    party.b.session.disconnected();
    party.left(3);
    tick(party.players(), 16);
    CHECK(party.b.game.calls.size() == 1 && party.b.game.calls[0] == "travel -1");
    reach_title(party.b);
    CHECK(count_of(party.b.game, "home") == 1);
    tick({&party.host, &party.a}, 1000);
    CHECK(party.host.session.members().size() == 2 && party.a.coop.host_level() == 4);

    for (Player* p : {&party.host, &party.a}) {
        CHECK(count_of(p->game, "travel -1") == 0 && count_of(p->game, "home") == 0);
    }

    // The host's own link lost: it goes home too, alone in no level.
    party.host.session.disconnected();
    tick({&party.host}, 16);
    CHECK(party.host.game.calls.back() == "travel -1");
}


// A player in the title's pages (the lobby: it never played, or left the host's game) when the
// party ends stays there: no travel, and the lobby itself goes back to Home. One whose start was
// just asked follows its game into the level and comes back.
void leaving_from_the_title() {
    Party party;
    party.a.game.now = {mp::kPhaseFrontEnd, -1, 3};
    tick(party.players(), mp::kStartingMs);
    party.a.session.disconnected();
    tick({&party.a}, 16);
    tick({&party.a}, mp::kStartingMs);
    CHECK(party.a.game.calls.empty() && !party.a.coop.to_title() && !party.a.coop.wants_home());

    // The host's Start, then its party ended before the title left: back once in the level.
    Player host(1);
    host.session.handle(info({{1, "P1"}}));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    tick({&host}, 16);
    host.session.leave();
    tick({&host}, 16);
    CHECK(host.coop.to_title() && host.game.calls.back() == "start 0 0");
    host.game.now = {mp::kPhasePlaying, 0, 2};
    tick({&host}, 16);
    CHECK(host.game.calls.back() == "travel -1");

    // Why the party went, for Home: nothing when the player left it.
    CHECK(mp::leave_reason(true, true, false, "").empty());
    CHECK(mp::leave_reason(false, false, false, "The host left: the party is over")
          == "The host left: the party is over");
    CHECK(mp::leave_reason(false, true, true, "") == "The host went away");
    CHECK(mp::leave_reason(false, true, false, "") == "The server went away");
    CHECK(mp::leave_reason(false, false, false, "") == "The party is over");

    for (const char* reason : {"The host left: the party is over", "The host went away",
                               "The server went away"}) {
        CHECK(std::string(reason).size() <= mp::kFooterMax);
    }
}


// The host's pause menu up for 4.4 s (the logged report: phase 3 -> 5, the member playing on):
// the member's figure stays, the same one; it goes when the member leaves the party, and says so.
void a_long_pause_keeps_the_figure() {
    Player host(1), member(2);
    host.session.handle(info({{1, "P1"}, {2, "P2"}}));
    member.session.handle(info({{1, "P1"}, {2, "P2"}}));
    std::vector<std::string> host_log;
    host.coop.on_log([&](const std::string& l) { host_log.push_back(l); });
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = {mp::kPhasePlaying, 0, 2};
    member.game.now = {mp::kPhasePlaying, 0, 2};
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(host.game.figures.size() == 1 && host.coop.figures() == 1);
    const int figure = host.game.figures.begin()->first;

    host.game.now.phase = mp::kPhaseMenu;
    for (int i = 0; i < 264; ++i) {
        tick({&host, &member}, 17);
        CHECK(host.game.figures.size() == 1 && host.game.figures.begin()->first == figure);
    }
    const auto dropped = [&] {
        return std::count_if(host_log.begin(), host_log.end(), [](const std::string& l) {
            return l.find("no longer shown") != std::string::npos;
        });
    };
    CHECK(dropped() == 0);

    // The member closes its game under the menu: out of the party, its figure goes.
    mp::Message left;
    left.kind = mp::Message::Kind::kPartyLeft;
    left.id = 2;
    host.session.handle(left);
    tick({&host}, 17);
    CHECK(host.game.figures.empty() && host.coop.figures() == 0);
    CHECK(dropped() == 1);
    CHECK(std::find(host_log.begin(), host_log.end(), "player 2 no longer shown: out of the party")
          != host_log.end());
}
}  // namespace

void run_party_tests() {
    a_long_pause_keeps_the_figure();
    held_items_and_shots_reach_the_figure();
    figures_hide_in_a_scene();
    a_ship_travel_takes_the_party_along();
    the_party_arrives_together();
    the_host_leaving_takes_everyone_home();
    a_member_leaving_goes_home_alone();
    a_member_disconnected_goes_home_alone();
    leaving_from_the_title();
}

}  // namespace mpt
