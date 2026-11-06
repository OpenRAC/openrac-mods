// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: scenes played together and their skips.
#include "test_support.hpp"

namespace mpt {
namespace {


// Scenes end together: the host's skip ends the member's scene (the game's own skip), the member's
// Start does nothing while the host's scene plays, and whoever is out first waits for the others.
void scenes_end_together() {
    Player host(1), member(2);
    std::vector<std::string> host_log, member_log;
    host.coop.on_log([&](const std::string& l) { host_log.push_back(l); });
    member.coop.on_log([&](const std::string& l) { member_log.push_back(l); });
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = member.game.now = {mp::kPhasePlaying, 3, 2};
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    member.game.calls.clear();

    // Both in the level's scene (a landing): the member's Start is held back, nobody is held.
    host.game.now.phase = member.game.now.phase = mp::kPhaseScene;
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.coop.host_scene());
    CHECK(!member.coop.hold_input() && !host.coop.hold_input());
    CHECK(member.game.calls.empty());

    // The host skips: out of its scene; the member's is skipped a moment later (again until it is
    // out), and the host waits for it.
    host.game.now.phase = mp::kPhasePlaying;
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.calls.empty());  // not at once
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!member.coop.host_scene());
    CHECK(!member.game.calls.empty() && member.game.calls.back() == "skip");
    CHECK(host.coop.hold_input());
    member.game.now.phase = mp::kPhasePlaying;
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());
    // What happened is in the logs: the scene's start, the skip asked, its end after it.
    const auto has = [](const std::vector<std::string>& log, const std::string& line) {
        return std::find(log.begin(), log.end(), line) != log.end();
    };
    CHECK(std::any_of(member_log.begin(), member_log.end(), [](const std::string& l) {
        return l.rfind("scene started (member", 0) == 0;
    }));
    CHECK(has(host_log, "scene started (host)"));
    CHECK(has(member_log, "skip requested: the host is out of the scene"));
    CHECK(has(member_log, "scene ended after the party's skip") && has(host_log, "scene ended"));
    CHECK(has(host_log, "waiting for the members to leave their scene"));
    const std::size_t skips = member.game.calls.size();
    tick({&host, &member}, 1000);
    CHECK(member.game.calls.size() == skips);  // no skip out of a scene

    // The host in a scene the member is out of: the member waits (its control held).
    host.game.now.phase = mp::kPhaseScene;
    for (int i = 0; i < 3; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.coop.hold_input() && !host.coop.hold_input());
    host.game.now.phase = mp::kPhasePlaying;
    for (int i = 0; i < 3; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!member.coop.hold_input());

    // A member that never leaves its scene does not hold the host for ever.
    member.game.now.phase = mp::kPhaseScene;
    for (int i = 0; i < 3; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(host.coop.hold_input());
    for (std::uint64_t t = 0; t <= mp::kBarrierMs; t += 50) {
        tick({&host, &member}, 50);
    }
    CHECK(!host.coop.hold_input());
}


// The level's scenes play in every game: the host's scene 3 starts in the member's game too (its
// own start, once its player is in control: not under a menu), late; the member's scene plays out
// after the host's has ended as long as it was late, the host waiting for it; a request the game
// did not act on is dropped when the host's scene ends; a member in another level starts nothing;
// the host's scene actors are not shared (each game has its own).
void the_hosts_scenes_play_everywhere() {
    Player host(1), member(2);
    std::vector<std::string> host_log, member_log;
    host.coop.on_log([&](const std::string& l) { host_log.push_back(l); });
    member.coop.on_log([&](const std::string& l) { member_log.push_back(l); });
    const auto has = [](const std::vector<std::string>& log, const std::string& start) {
        return std::any_of(log.begin(), log.end(), [&](const std::string& l) { return l.rfind(start, 0) == 0; });
    };
    const auto asked = [&](const std::string& call) {
        return std::count(member.game.calls.begin(), member.game.calls.end(), call);
    };
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = member.game.now = {mp::kPhasePlaying, 0, 2};
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    member.game.calls.clear();

    // Veldin's opening in the host's game (its actors: a creature it spawned for the scene), the
    // member in its pause menu: nothing asked yet, the member's control held for the host's scene.
    host.game.now = {mp::kPhaseScene, 0, 2, 3};
    host.game.level.push_back(moby(310, -1, 1250, mp::kMobyUpdates | mp::kMobyCreature | mp::kMobyScene, 5.0f));
    member.game.now.phase = mp::kPhaseMenu;
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(has(host_log, "scene 3 of level 0 started here: the party plays it too"));
    CHECK(has(member_log, "the host plays scene 3 of level 0"));
    CHECK(asked("scene 3") == 0);
    CHECK(member.world.spawned() == 0);
    CHECK(std::count(member.game.world_calls.begin(), member.game.world_calls.end(), "spawn 1250") == 0);

    // The member's menu closed: the same scene asked of its game, once (again only kSceneAskMs on).
    member.game.now.phase = mp::kPhasePlaying;
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(asked("scene 3") == 1);
    CHECK(has(member_log, "starting the host's scene 3 here"));
    CHECK(member.coop.hold_input());
    for (std::uint64_t t = 0; t < mp::kSceneAskMs; t += 50) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene 3") == 2);

    // Its game plays it (4 s after the host's started): no more asking, its own actors not counted.
    member.game.now = {mp::kPhaseScene, 0, 2, 3};
    member.game.level.push_back(moby(311, -1, 1250, mp::kMobyUpdates | mp::kMobyCreature | mp::kMobyScene, 5.0f));
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(has(member_log, "playing the host's scene 3 here, "));
    CHECK(asked("scene 3") == 2 && asked("scene -1") == 0);
    CHECK(!member.coop.hold_input() && !host.coop.hold_input());
    CHECK(host.world.spawned() == 0 && member.world.spawned() == 0);

    // The host's ends (its actors gone): the member's plays on, past kHostOutMs, the host waiting.
    host.game.now = {mp::kPhasePlaying, 0, 2};
    host.game.level.pop_back();
    for (int i = 0; i < 50; ++i) {  // 2.5 s
        tick({&host, &member}, 50);
    }
    CHECK(std::count(member.game.calls.begin(), member.game.calls.end(), "skip") == 0);
    CHECK(host.coop.hold_input());
    CHECK(has(host_log, "waiting for the members to leave their scene"));
    // Its end: everyone goes on, nothing skipped.
    member.game.now = {mp::kPhasePlaying, 0, 2};
    member.game.level.pop_back();
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());
    CHECK(std::count(member.game.calls.begin(), member.game.calls.end(), "skip") == 0);
    // Played once: not asked again while the host plays on.
    tick({&host, &member}, 4000);
    CHECK(asked("scene 3") == 2);

    // Scene 5: asked, the member's game never starts it (as after a load), the host's ends: still
    // asked a while (a member late plays it all the same), then the request is dropped.
    host.game.now = {mp::kPhaseScene, 0, 2, 5};
    for (int i = 0; i < 6; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene 5") == 1);
    host.game.now = {mp::kPhasePlaying, 0, 2};
    for (int i = 0; i < 6; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene -1") == 0);
    CHECK(member.coop.hold_input() && host.coop.hold_input());
    for (std::uint64_t t = 0; t < mp::kSceneAskMs; t += 50) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene -1") == 1);
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(!member.coop.hold_input() && !host.coop.hold_input());
    CHECK(has(member_log, "the host's scene is over: not starting it here"));

    // Scene 3 again (a new start): played again. The member in another level: nothing asked.
    member.game.calls.clear();
    host.game.now = {mp::kPhaseScene, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene 3") == 1);
    member.game.now = {mp::kPhaseScene, 0, 2, 3};
    tick({&host, &member}, 50);
    member.game.now = host.game.now = {mp::kPhasePlaying, 0, 2};
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    member.game.calls.clear();
    member.game.now = {mp::kPhasePlaying, 4, 3};
    host.game.now = {mp::kPhaseScene, 0, 2, 7};
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene 7") == 0);

    // A member that started late is let play only so long: the host's skip still skips it at once.
    member.game.now = {mp::kPhasePlaying, 0, 2};
    host.game.now = {mp::kPhasePlaying, 0, 2};
    for (int i = 0; i < 30; ++i) {
        tick({&host, &member}, 50);
    }
    host.game.now = {mp::kPhaseScene, 0, 2, 9};
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    member.game.now = {mp::kPhaseScene, 0, 2, 9};
    tick({&host, &member}, 50);
    member.game.calls.clear();
    host.coop.own_skip(mp::kPhaseScene, host.clock);
    host.game.now = {mp::kPhasePlaying, 0, 2};
    route({&host, &member});
    tick({&host, &member}, 50);
    CHECK(std::count(member.game.calls.begin(), member.game.calls.end(), "skip") >= 1);
}


// Only the host skips: a member's Start in a scene or a movie is taken out of its pad, said over
// its game, and skips nothing anywhere (the scene plays on); the host's Start in a scene skips it
// in every game, in a movie presses Start for the members' movie player; a game where nothing plays
// does nothing. A member's SKIP (an older build's) the host ignores.
void only_the_host_skips() {
    Player host(1), a(2), b(3);
    std::vector<std::string> host_log, a_log;
    host.coop.on_log([&](const std::string& l) { host_log.push_back(l); });
    a.coop.on_log([&](const std::string& l) { a_log.push_back(l); });
    const auto has = [](const std::vector<std::string>& log, const std::string& start) {
        return SceneParty::logged(log, start);
    };
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    for (Player* p : {&host, &a, &b}) {
        p->session.handle(info(all));
    }
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &a, &b});
    tick({&host, &a, &b}, 16);
    for (Player* p : {&host, &a, &b}) {
        p->game.now = {mp::kPhaseScene, 0, 2};
        p->game.calls.clear();
    }
    tick({&host, &a, &b}, 16);

    const auto skips = [](const FakeGame& g) { return std::count(g.calls.begin(), g.calls.end(), "skip"); };

    // Member a presses Start in the scene: its Start is taken out, the banner says why, nothing is
    // sent, and the scene plays on in every game.
    CHECK(a.coop.start_blocked(mp::kPhaseScene) && a.coop.start_blocked(mp::kPhaseLoading));
    CHECK(!a.coop.start_blocked(mp::kPhasePlaying) && !a.coop.start_blocked(mp::kPhaseMenu));
    CHECK(!host.coop.start_blocked(mp::kPhaseScene) && !host.coop.start_blocked(mp::kPhaseLoading));
    a.coop.own_skip(mp::kPhaseScene, a.clock);
    CHECK(a.coop.banner(a.clock) == "Only the host can skip");
    CHECK(has(a_log, "skip pressed here: only the host can skip"));
    for (int i = 0; i < 10; ++i) {
        tick({&host, &a, &b}, 16);
    }
    CHECK(skips(host.game) == 0 && skips(a.game) == 0 && skips(b.game) == 0);
    CHECK(host.game.now.phase == mp::kPhaseScene);
    // The banner goes after a while.
    CHECK(a.coop.banner(a.clock + mp::kSkipRefusedMs) == "");

    // A member's SKIP (an older build's) reaching the host: ignored, not passed on.
    host.link.frames.clear();
    host.coop.handle(2, skip_bytes(2, 0, mp::kSkipOther, 0));
    CHECK(has(host_log, "skip requested by player 2: only the host skips, ignored"));
    CHECK(host.link.frames.empty());
    route({&host, &a, &b});
    CHECK(skips(host.game) == 0 && skips(b.game) == 0);

    // The host presses Start in the scene: every member's is skipped, the host's own is its game's.
    host.coop.own_skip(mp::kPhaseScene, host.clock);
    route({&host, &a, &b});
    CHECK(skips(a.game) == 1 && skips(b.game) == 1);
    CHECK(skips(host.game) == 0);
    // Pressed again at once: no second skip (a while between).
    host.coop.own_skip(mp::kPhaseScene, host.clock + 10);
    route({&host, &a, &b});
    CHECK(skips(a.game) == 1 && skips(b.game) == 1);

    // In a movie (a load): the host's Start presses Start for the members' movie player, pressed and
    // let go in turns; not in the host's own game, and not in a member playing.
    for (Player* p : {&host, &a, &b}) {
        p->game.now = {mp::kPhaseLoading, -1, 2};
    }
    b.game.now.phase = mp::kPhasePlaying;
    host.coop.own_skip(mp::kPhaseLoading, host.clock + 1000);
    route({&host, &a, &b});
    int presses = 0, releases = 0;
    for (int i = 0; i < 6; ++i) {
        (a.coop.movie_press(a.clock + 1100) ? presses : releases) += 1;
    }
    CHECK(presses == 3 && releases == 3);
    CHECK(!host.coop.movie_press(host.clock + 1100) && !b.coop.movie_press(b.clock + 1100));
    // Long after, nothing more is pressed.
    CHECK(!a.coop.movie_press(a.clock + 5000));

    // Start in the level (no scene, no movie) skips nothing anywhere.
    host.game.now.phase = mp::kPhasePlaying;
    a.game.now.phase = mp::kPhasePlaying;
    const std::size_t b_calls = b.game.calls.size();
    host.coop.own_skip(mp::kPhasePlaying, host.clock + 3000);
    route({&host, &a, &b});
    CHECK(b.game.calls.size() == b_calls);
}


// The report's sequence: the host's Start skips the movies, then the level's opening (both games'
// own) plays, then the host's scene 3, skipped by the host; its scene 4 starts a moment later and
// the member's a moment after that. No skip of before (the movie's, scene 3's) ends scene 4: it
// plays in full on both, the host waiting for the member at its end.
void a_skip_ends_its_own_scene_only() {
    SceneParty p;
    Player& host = p.host;
    Player& member = p.member;

    // A movie (a load), the host's Start: the member's movie player gets Start; then the level.
    host.game.now = member.game.now = {mp::kPhaseLoading, 0, 2};
    tick({&host, &member}, 50);
    host.coop.own_skip(mp::kPhaseLoading, host.clock);
    route({&host, &member});
    CHECK(member.coop.movie_press(member.clock + 10));
    // The level's opening, both games' own, at once: the movie's skip does not skip it.
    host.game.now = member.game.now = {mp::kPhaseScene, 0, 3};
    p.run(300);
    CHECK(SceneParty::count(member.game, "skip") == 0);
    CHECK(!member.coop.movie_press(member.clock + 10));
    host.game.now = member.game.now = {mp::kPhasePlaying, 0, 3};
    p.run(200);

    // The host's scene 3, played in the member's game too; the host's Start skips it: the
    // member's is skipped.
    host.game.now = {mp::kPhaseScene, 0, 3, 3};
    p.run(100);
    member.game.now = {mp::kPhaseScene, 0, 3, 3};
    p.run(500);
    CHECK(SceneParty::logged(p.member_log, "playing the host's scene 3 here"));
    host.coop.own_skip(mp::kPhaseScene, host.clock);
    route({&host, &member});
    CHECK(SceneParty::count(member.game, "skip") == 1);
    host.game.now = {mp::kPhasePlaying, 0, 3};
    tick({&host, &member}, 50);
    member.game.now = {mp::kPhasePlaying, 0, 3};
    tick({&host, &member}, 16);

    // Scene 4 at once in the host's, 250 ms later in the member's (its own start, asked): played
    // in full on both, 3 s, nothing skipped.
    host.game.now = {mp::kPhaseScene, 0, 3, 4};
    p.run(250);
    CHECK(SceneParty::count(member.game, "scene 4") == 1);
    member.game.now = {mp::kPhaseScene, 0, 3, 4};
    const std::size_t from = p.member_log.size();
    p.run(3000);
    CHECK(SceneParty::logged(p.member_log, "playing the host's scene 4 here", from));
    CHECK(SceneParty::count(member.game, "skip") == 1 && SceneParty::count(host.game, "skip") == 0);
    CHECK(member.game.now.phase == mp::kPhaseScene);

    // The host's ends, the member's 250 ms later, as played: nobody skips, the host waits for it.
    host.game.now = {mp::kPhasePlaying, 0, 3};
    p.run(250);
    CHECK(host.coop.hold_input() && !member.coop.hold_input());
    member.game.now = {mp::kPhasePlaying, 0, 3};
    p.run(200);
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());
    CHECK(SceneParty::count(member.game, "skip") == 1 && SceneParty::count(host.game, "skip") == 0);
    CHECK(!SceneParty::logged(p.member_log, "scene ended after the party's skip", from));
}


// A member late for the host's scene (in its pause menu while the host's played and ended) still
// plays it from its start once it can; its barrier waits until it has, and the host's waits for it.
void a_late_member_plays_the_scene_after_the_host() {
    SceneParty p;
    Player& host = p.host;
    Player& member = p.member;

    member.game.now.phase = mp::kPhaseMenu;
    host.game.now = {mp::kPhaseScene, 0, 2, 4};
    p.run(1000);
    host.game.now = {mp::kPhasePlaying, 0, 2};
    p.run(200);
    CHECK(SceneParty::count(member.game, "scene 4") == 0);

    // Out of its menu: the scene started here, both held meanwhile.
    member.game.now.phase = mp::kPhasePlaying;
    p.run(100);
    CHECK(SceneParty::count(member.game, "scene 4") == 1);
    CHECK(SceneParty::logged(p.member_log, "starting the host's scene 4 here (the host's has ended)"));
    CHECK(member.coop.hold_input() && host.coop.hold_input());
    member.game.now = {mp::kPhaseScene, 0, 2, 4};
    p.run(1000);
    CHECK(host.coop.hold_input());
    CHECK(SceneParty::count(member.game, "skip") == 0);
    member.game.now = {mp::kPhasePlaying, 0, 2};
    p.run(200);
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());
    CHECK(SceneParty::count(member.game, "skip") == 0 && SceneParty::count(member.game, "scene -1") == 0);
    // Played: not asked again.
    p.run(5000);
    CHECK(SceneParty::count(member.game, "scene 4") == 1);
}


// The host's skip of its second scene still skips the member's (playing it, or late for it: then
// not started at all, nobody waiting for it).
void the_hosts_skip_of_the_next_scene_skips_it() {
    SceneParty p;
    Player& host = p.host;
    Player& member = p.member;

    for (int scene : {3, 4}) {
        host.game.now = {mp::kPhaseScene, 0, 2, scene};
        p.run(100);
        member.game.now = {mp::kPhaseScene, 0, 2, scene};
        p.run(500);
        if (scene == 3) {
            host.game.now = member.game.now = {mp::kPhasePlaying, 0, 2};
            p.run(100);
        }
    }
    CHECK(SceneParty::count(member.game, "skip") == 0);
    host.coop.own_skip(mp::kPhaseScene, host.clock);
    route({&host, &member});
    CHECK(SceneParty::count(member.game, "skip") == 1);
    CHECK(SceneParty::logged(p.member_log, "skip requested by player 1: skipping the scene here"));
    host.game.now = {mp::kPhasePlaying, 0, 2};
    p.run(600);
    CHECK(SceneParty::count(member.game, "skip") >= 2);  // asked again until out
    member.game.now = {mp::kPhasePlaying, 0, 2};
    p.run(200);
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());

    // Scene 5, the member in its menu: the host skips it. Out of its menu, the member does not
    // play it, and nobody waits.
    member.game.calls.clear();
    member.game.now.phase = mp::kPhaseMenu;
    host.game.now = {mp::kPhaseScene, 0, 2, 5};
    p.run(300);
    host.coop.own_skip(mp::kPhaseScene, host.clock);
    route({&host, &member});
    host.game.now = {mp::kPhasePlaying, 0, 2};
    p.run(200);
    member.game.now.phase = mp::kPhasePlaying;
    p.run(300);
    CHECK(SceneParty::count(member.game, "scene 5") == 0);
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());

    // Scene 6 asked of the member's game, skipped before it started there: dropped, and skipped
    // at once should the game start it all the same.
    member.game.calls.clear();
    host.game.now = {mp::kPhaseScene, 0, 2, 6};
    p.run(100);
    CHECK(SceneParty::count(member.game, "scene 6") == 1);
    host.coop.own_skip(mp::kPhaseScene, host.clock);
    route({&host, &member});
    p.run(100);
    CHECK(SceneParty::count(member.game, "scene -1") == 1);
    member.game.now = {mp::kPhaseScene, 0, 2, 6};
    p.run(50);
    CHECK(SceneParty::count(member.game, "skip") == 1);
    CHECK(SceneParty::logged(p.member_log, "the party skipped the host's scene 6: skipping it here too"));
}


// A skip or an "out" of another scene is no word of this one: a skip of the host's earlier scene,
// of a scene of a game's own, of another play, or with no ids (an older build) skips nothing of
// the host's scene playing; a member's "out" of an earlier scene does not end the host's wait.
void a_stale_skip_or_out_is_ignored() {
    SceneParty p;
    Player& host = p.host;
    Player& member = p.member;

    // The host's scenes 3 (sequence 1) and 4 (sequence 2); the member late for 4 (in its menu).
    host.game.now = {mp::kPhaseScene, 0, 2, 3};
    p.run(100);
    member.game.now = {mp::kPhaseScene, 0, 2, 3};
    p.run(300);
    host.game.now = member.game.now = {mp::kPhasePlaying, 0, 2};
    p.run(100);
    member.game.now.phase = mp::kPhaseMenu;
    host.game.now = {mp::kPhaseScene, 0, 2, 4};
    p.run(500);
    host.game.now = {mp::kPhasePlaying, 0, 2};
    p.run(300);
    const std::int32_t play = [&] {
        // The play's id: what the host's skips say (one pressed where nothing plays).
        host.coop.own_skip(mp::kPhaseLoading, host.clock);
        host.session.flush();
        mp::Bytes sent;
        for (const Bytes& f : host.link.frames) {
            sent = f;
        }
        host.link.frames.clear();
        mp::FrameReader fr;
        fr.feed(sent.data(), sent.size());
        Bytes body;
        CHECK(fr.next(body));
        const mp::Request q = mp::decode_request(body);
        mp::Reader r(q.bytes.data() + 1, q.bytes.size() - 1);
        r.i32();
        r.u8();
        r.u8();
        return r.i32();
    }();
    CHECK(play != 0);

    // The host waits for the member still to play scene 4 (its pose says no scene: its word of
    // scene 4 does); a late "out" of scene 3, and broken words, end nothing.
    CHECK(host.coop.hold_input());
    const auto state = [&](std::uint16_t sequence, std::uint8_t s) {
        return mp::Writer()
            .u8(static_cast<std::uint8_t>(mp::GameKind::kSceneState))
            .i32(play)
            .u8(static_cast<std::uint8_t>(sequence >> 8))
            .u8(static_cast<std::uint8_t>(sequence))
            .u8(s)
            .bytes();
    };
    host.coop.handle(2, state(1, mp::kSceneOut));
    host.coop.handle(2, mp::Writer().u8(static_cast<std::uint8_t>(mp::GameKind::kSceneState)).i32(play).bytes());
    host.coop.handle(2, state(2, 9));
    tick({&host, &member}, 16);
    CHECK(host.coop.hold_input());

    // The member out of its menu plays scene 4; skips of other scenes skip nothing of it.
    member.game.now.phase = mp::kPhasePlaying;
    p.run(100);
    member.game.now = {mp::kPhaseScene, 0, 2, 4};
    p.run(100);
    const std::size_t skips = static_cast<std::size_t>(SceneParty::count(member.game, "skip"));
    member.coop.handle(1, skip_bytes(1, play, mp::kSkipShared, 1));      // scene 3's
    member.coop.handle(1, skip_bytes(1, play, mp::kSkipOther, 0));       // a game's own scene
    member.coop.handle(1, skip_bytes(1, play + 1, mp::kSkipShared, 2));  // another play
    member.coop.handle(1, skip_bytes(1, play, mp::kSkipShared, 0));      // no scene
    member.coop.handle(1, skip_bytes(1, play, 7, 2));                    // what is that
    member.coop.handle(1, mp::Writer().u8(static_cast<std::uint8_t>(mp::GameKind::kSkip)).i32(1).u8(0).u8(1).bytes());
    member.coop.handle(1, mp::Writer().u8(static_cast<std::uint8_t>(mp::GameKind::kSkip)).i32(1).bytes());
    p.run(500);
    CHECK(static_cast<std::size_t>(SceneParty::count(member.game, "skip")) == skips);
    CHECK(SceneParty::logged(p.member_log, "skip requested by player 1 of an earlier scene: ignored"));
    CHECK(SceneParty::logged(p.member_log, "skip requested by player 1 for no scene of this game's: ignored"));
    CHECK(host.coop.hold_input());
    // Its own word of scene 4's end does.
    member.game.now = {mp::kPhasePlaying, 0, 2};
    p.run(200);
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());
    CHECK(static_cast<std::size_t>(SceneParty::count(member.game, "skip")) == skips);
}
}  // namespace

void run_scenes_tests() {
    scenes_end_together();
    the_hosts_scenes_play_everywhere();
    only_the_host_skips();
    a_skip_ends_its_own_scene_only();
    a_late_member_plays_the_scene_after_the_host();
    the_hosts_skip_of_the_next_scene_skips_it();
    a_stale_skip_or_out_is_ignored();
}

}  // namespace mpt
