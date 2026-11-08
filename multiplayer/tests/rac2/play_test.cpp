// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Going Commando's co-op without pages (src/rac2/party.hpp), with fake games (test_support.hpp's
// FakeGame, as the play calls of rac2 answer them: no world, no card, no placing) and sessions on
// recorders, routed as the server does:
//   - the party's description names the game, and each game keeps to its own parties: Ratchet &
//     Clank's untagged and digest-tagged ones are its own, Going Commando's only those tagged rac2;
//   - the `party` setting's values and a code as typed;
//   - hosting by the settings: the server opened once, CREATE with the rac2 description, its code
//     on the banner while alone; joining: JOIN with the code as typed, a code that is none refused
//     without a connection; direct hosting and joining through the connection's calls;
//   - the host's game is the party's once it plays a level: the member in the title starts the
//     host's level from the host's save, the figures are posed from each other's poses (place, turn
//     and the animation fields), the member follows the host's travel, the figures go when the
//     levels differ; the host back in the title then in a level again brings the member again (out
//     of its level first);
//   - a party of Ratchet & Clank's is left at once, with why; the host leaving keeps its own game,
//     a member whose party ended goes to the title; a member whose game differs from the host's
//     (its mods) is not brought in, and told.
#include "../test_support.hpp"
#include "rac2/party.hpp"

namespace mpt {
namespace {

// A Going Commando player: a session on a recorder, a fake game, the co-op, the party's mods and
// the party of the settings, with what it asked of the connection.
struct Rac2Player {
    std::int32_t id;
    Recorder link;
    std::uint64_t clock = 1000;
    mp::Session session;
    FakeGame game;
    mp::Coop coop;
    mp::PartyMods mods;
    mp::Router router;
    std::vector<std::string> net;
    bool host_ok = true;
    mp::PageParty party;
    std::vector<std::string> log;

    explicit Rac2Player(std::int32_t id_)
        : id(id_),
          session(
              link, "rac2-multiplayer/test", "P" + std::to_string(id_), [this] { return clock; }
          ),
          coop(session, game, [this] { return clock; }),
          mods(session, game, {}, [this] { return clock; }),
          party(session, coop, mods, network()) {
        router.route(mp::Owner::kMods, [this](std::int32_t sender, const Bytes& bytes) {
            mods.handle(sender, bytes);
        });
        router.fallback([this](std::int32_t sender, const Bytes& bytes) {
            coop.handle(sender, bytes);
        });
        session.on_data([this](std::int32_t sender, const Bytes& bytes) {
            router.dispatch(sender, bytes);
        });
        party.on_log([this](const std::string& l) { log.push_back(l); });
        coop.on_log([this](const std::string& l) { log.push_back(l); });
        mp::Message hello;
        hello.kind = mp::Message::Kind::kHello;
        hello.id = id;
        session.connected();
        session.handle(hello);
    }

    mp::PartyNet network() {
        mp::PartyNet n;
        n.server = [this] {
            net.push_back("server");
        };
        n.host_direct = [this](const std::string& description, std::string& error) {
            net.push_back("host " + description);
            error = host_ok ? "" : "port 7770 is taken";
            return host_ok;
        };
        n.join_direct = [this](const std::string& address) {
            net.push_back("join " + address);
            return true;
        };
        return n;
    }

    // The party requests this player sent the server (CREATE, JOIN, LEAVE, FIND), still in its link.
    std::vector<mp::Request> requests() {
        session.flush();
        std::vector<mp::Request> out;

        for (const Bytes& frame : link.frames) {
            mp::FrameReader r;
            r.feed(frame.data(), frame.size());
            Bytes body;

            if (r.next(body)) {
                const mp::Request q = mp::decode_request(body);

                if (q.kind == mp::Request::Kind::kCreate || q.kind == mp::Request::Kind::kJoin
                    || q.kind == mp::Request::Kind::kLeave || q.kind == mp::Request::Kind::kFind) {
                    out.push_back(q);
                }
            }
        }

        return out;
    }

    bool said(const std::string& line) const {
        return std::any_of(log.begin(), log.end(), [&](const std::string& l) {
            return l.find(line) != std::string::npos;
        });
    }
};

mp::Message info2(const std::vector<mp::Member>& members, const std::string& description) {
    mp::Message m;
    m.kind = mp::Message::Kind::kPartyInfo;
    m.code = "ABCDEF";
    m.host = 1;
    m.members = members;
    m.description = description;
    return m;
}

// The server's way with DATA: a member's to the host, the host's to everyone or one.
void route2(const std::vector<Rac2Player*>& players) {
    bool moved = true;

    while (moved) {
        moved = false;

        for (Rac2Player* from : players) {
            from->session.flush();
            std::vector<Bytes> frames;
            frames.swap(from->link.frames);
            // The party's requests stay for requests() to read; DATA goes on.
            std::vector<Bytes> kept;

            for (const Bytes& frame : frames) {
                mp::FrameReader r;
                r.feed(frame.data(), frame.size());
                Bytes body;

                if (!r.next(body)) {
                    continue;
                }

                const mp::Request q = mp::decode_request(body);

                if (q.kind != mp::Request::Kind::kData) {
                    kept.push_back(frame);
                    continue;
                }

                for (Rac2Player* to : players) {
                    const bool reaches =
                        to != from
                        && (from->session.is_host() ? q.target == 0 || q.target == to->id
                                                    : to->session.is_host());

                    if (reaches && to->session.in_party()) {
                        mp::Message m;
                        m.kind = mp::Message::Kind::kData;
                        m.id = from->id;
                        m.bytes = q.bytes;
                        to->session.handle(m);
                        moved = true;
                    }
                }
            }

            from->link.frames.insert(from->link.frames.begin(), kept.begin(), kept.end());
        }
    }
}

void tick2(const std::vector<Rac2Player*>& players, std::uint64_t ms) {
    for (Rac2Player* p : players) {
        p->clock += ms;
        p->mods.tick(p->clock);
        p->party.tick(p->clock, p->game.status());
        p->coop.tick(p->clock);
        p->session.flush();
    }

    route2(players);
}

void run2(const std::vector<Rac2Player*>& players, std::uint64_t ms) {
    for (std::uint64_t t = 0; t < ms; t += 50) {
        tick2(players, 50);
    }
}

std::size_t calls_of(const FakeGame& game, const std::string& call) {
    return static_cast<std::size_t>(std::count(game.calls.begin(), game.calls.end(), call));
}

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

void test_settings() {
    CHECK(mp::party_action("host") == mp::PartyAction::kHost);
    CHECK(mp::party_action("join") == mp::PartyAction::kJoin);
    CHECK(mp::party_action("host_direct") == mp::PartyAction::kHostDirect);
    CHECK(mp::party_action("join_direct") == mp::PartyAction::kJoinDirect);
    CHECK(
        mp::party_action("off") == mp::PartyAction::kOff
        && mp::party_action("") == mp::PartyAction::kOff
        && mp::party_action("Host") == mp::PartyAction::kOff
    );
    CHECK(mp::party_code(" abc def ") == "ABCDEF");
}

void test_host_by_settings() {
    Rac2Player host{1};

    // Off: nothing at all.
    Rac2Player solo{5};
    solo.party.begin(solo.clock);
    run2({&solo}, 500);
    CHECK(solo.net.empty() && solo.requests().empty() && solo.party.banner(solo.clock).empty());

    // Nothing before the mods are listed (begin), then the server once and a CREATE named for rac2.
    host.party.choose(mp::PartyAction::kHost, "", "");
    run2({&host}, 200);
    CHECK(host.net.empty() && host.requests().empty());
    host.party.begin(host.clock);
    host.party.begin(host.clock);
    run2({&host}, 1000);
    CHECK(host.net == std::vector<std::string>{"server"});
    const std::vector<mp::Request> asked = host.requests();
    CHECK(asked.size() == 1 && asked[0].kind == mp::Request::Kind::kCreate);
    CHECK(!asked.empty() && asked[0].text == host.party.description());
    CHECK(mp::description_game(host.party.description()) == "rac2");
    host.link.frames.clear();

    // In its party: the code on the banner while alone; no CREATE again.
    host.session.handle(info2({{1, "P1"}}, host.party.description()));
    run2({&host}, 9000);
    CHECK(host.party.banner(host.clock).find("ABCDEF") != std::string::npos);
    CHECK(host.requests().empty());

    // A member comes: said, and the code line goes.
    mp::Message came;
    came.kind = mp::Message::Kind::kPartyJoined;
    came.id = 2;
    came.text = "P2";
    host.session.handle(came);
    run2({&host}, 100);
    CHECK(host.said("P2 joined"));
    run2({&host}, mp::kNoticeMs);
    CHECK(host.party.banner(host.clock).empty());

    // Direct: the connection hosts with the description; a taken port says why.
    Rac2Player direct{3};
    direct.party.choose(mp::PartyAction::kHostDirect, "", "");
    direct.party.begin(direct.clock);
    CHECK(direct.net.size() == 1 && direct.net[0] == "host " + direct.party.description());
    Rac2Player taken{4};
    taken.host_ok = false;
    taken.party.choose(mp::PartyAction::kHostDirect, "", "");
    taken.party.begin(taken.clock);
    CHECK(taken.party.banner(taken.clock).find("port 7770 is taken") != std::string::npos);
}

void test_join_by_settings() {
    Rac2Player member{2};
    member.party.choose(mp::PartyAction::kJoin, "abc def", "");
    member.party.begin(member.clock);
    run2({&member}, 500);
    CHECK(member.net == std::vector<std::string>{"server"});
    const std::vector<mp::Request> asked = member.requests();
    CHECK(
        asked.size() == 1 && asked[0].kind == mp::Request::Kind::kJoin && asked[0].text == "ABCDEF"
    );

    // A party that would not have it (not found): asked again a few times, then given up.
    mp::Message refused;
    refused.kind = mp::Message::Kind::kError;
    refused.reason = mp::ErrorReason::kPartyNotFound;
    std::size_t joins = 0;

    for (std::uint64_t t = 0; t < mp::kAskAgainMs * (mp::kAsks + 1); t += 50) {
        member.link.frames.clear();
        run2({&member}, 50);

        for (const mp::Request& q : member.requests()) {
            joins += q.kind == mp::Request::Kind::kJoin ? 1 : 0;
        }

        if (member.session.waiting()) {
            member.session.handle(refused);
        }
    }

    CHECK(joins == static_cast<std::size_t>(mp::kAsks - 1));
    CHECK(member.said("could not join party ABCDEF"));

    // Not a code: refused here, no connection.
    Rac2Player wrong{3};
    wrong.party.choose(mp::PartyAction::kJoin, "AB", "");
    wrong.party.begin(wrong.clock);
    CHECK(wrong.net.empty() && wrong.party.action() == mp::PartyAction::kOff);
    CHECK(wrong.party.banner(wrong.clock).find("is not a party's") != std::string::npos);

    // Direct: the address as typed; none, refused.
    Rac2Player direct{4};
    direct.party.choose(mp::PartyAction::kJoinDirect, "", "192.168.1.20:7770");
    direct.party.begin(direct.clock);
    CHECK(direct.net == std::vector<std::string>{"join 192.168.1.20:7770"});
    Rac2Player none{5};
    none.party.choose(mp::PartyAction::kJoinDirect, "", "");
    none.party.begin(none.clock);
    CHECK(none.net.empty() && none.party.action() == mp::PartyAction::kOff);
}

// A host and a member of a Going Commando party, both in the title.
struct Rac2Party {
    Rac2Player host{1}, member{2};

    std::vector<Rac2Player*> all() { return {&host, &member}; }

    Rac2Party() {
        host.party.choose(mp::PartyAction::kHost, "", "");
        member.party.choose(mp::PartyAction::kJoin, "ABCDEF", "");
        host.party.begin(host.clock);
        member.party.begin(member.clock);
        const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
        host.session.handle(info2(both, host.party.description()));
        member.session.handle(info2(both, host.party.description()));
        host.game.live_save = Bytes(100, 7);
        run2(all(), 200);
    }
};

void test_follow_the_host() {
    Rac2Party p;

    // In the title the host's game is not the party's yet: nothing is started.
    CHECK(!p.host.coop.playing() && p.member.game.calls.empty());

    // The host plays level 3: its game is the party's, and the member starts it from the host's save.
    p.host.game.now = {mp::kPhasePlaying, 3, 2};
    run2(p.all(), 200);
    CHECK(p.host.coop.playing() && p.host.coop.adopted());
    CHECK(calls_of(p.member.game, "start 3 100") == 1);
    CHECK(p.member.game.started_save == Bytes(100, 7));
    CHECK(calls_of(p.host.game, "start 3 100") == 0);

    // Both in level 3: each poses a figure of the other from its poses, the animation fields too.
    p.member.game.now = {mp::kPhasePlaying, 3, 2};
    p.host.game.hero_pose.position[0] = 10.0f;
    p.host.game.hero_pose.position[1] = 20.0f;
    p.host.game.hero_pose.rotation[2] = 1.25f;
    p.host.game.hero_pose.seq = 7;
    p.host.game.hero_pose.frame = 3;
    p.member.game.hero_pose.position[0] = -4.0f;
    run2(p.all(), 1500);
    CHECK(p.member.coop.figures() == 1 && p.host.coop.figures() == 1);
    const int host_figure = p.member.coop.figure_of(1);
    const int member_figure = p.host.coop.figure_of(2);
    CHECK(
        p.member.game.figures.count(host_figure) == 1
        && p.host.game.figures.count(member_figure) == 1
    );

    if (p.member.game.figures.count(host_figure) == 1
        && p.host.game.figures.count(member_figure) == 1) {
        const mp::Pose& host_shown = p.member.game.figures[host_figure];
        CHECK(
            std::fabs(host_shown.position[0] - 10.0f) < 0.01f
            && std::fabs(host_shown.position[1] - 20.0f) < 0.01f
        );
        CHECK(
            std::fabs(host_shown.rotation[2] - 1.25f) < 0.01f && host_shown.seq == 7
            && host_shown.frame == 3
        );
        CHECK(std::fabs(p.host.game.figures[member_figure].position[0] + 4.0f) < 0.01f);
    }

    // The host goes on to level 5 (its own travel): the member follows, the figures go meanwhile.
    p.host.game.now = {mp::kPhasePlaying, 5, 3};
    run2(p.all(), 500);
    CHECK(calls_of(p.member.game, "travel 5") == 1);
    CHECK(p.member.coop.figures() == 0 && p.host.coop.figures() == 0);
    p.member.game.now = {mp::kPhasePlaying, 5, 3};
    run2(p.all(), 1500);
    CHECK(p.member.coop.figures() == 1 && p.host.coop.figures() == 1);
    CHECK(calls_of(p.member.game, "travel 5") == 1);

    // The host back in the title, then in a level again: a new game of the party's, which the
    // member is brought into again (out of its level first, then from the title).
    p.host.game.now = {mp::kPhaseFrontEnd, -1, 4};
    run2(p.all(), 6000);
    CHECK(p.member.coop.figures() == 0);
    p.host.game.live_save = Bytes(120, 9);
    p.host.game.now = {mp::kPhasePlaying, 4, 5};
    run2(p.all(), 500);
    CHECK(p.host.said("playing again level 4") || p.host.coop.adopted());
    CHECK(calls_of(p.member.game, "travel -1") >= 1);
    p.member.game.now = {mp::kPhaseFrontEnd, -1, 4};
    run2(p.all(), 500);
    CHECK(calls_of(p.member.game, "start 4 120") == 1);
}

void test_other_game_and_leaving() {
    // A Ratchet & Clank party (its tag, or none): left at once, with why.
    for (const std::string description :
         {std::string("Kerwan, 3 gold bolts|3fa9"), std::string("New game")}) {
        Rac2Player member{2};
        member.party.choose(mp::PartyAction::kJoin, "ABCDEF", "");
        member.party.begin(member.clock);
        member.session.handle(info2({{1, "P1"}, {2, "P2"}}, description));
        member.link.frames.clear();
        run2({&member}, 100);
        bool left = false;

        for (const mp::Request& q : member.requests()) {
            left = left || q.kind == mp::Request::Kind::kLeave;
        }

        CHECK(left && !member.session.in_party());
        CHECK(member.party.banner(member.clock).find(mp::kOtherGameNotice) != std::string::npos);
        // Never asked again: that was the party chosen.
        member.link.frames.clear();
        run2({&member}, mp::kAskAgainMs * 2);
        CHECK(member.requests().empty());
    }

    // The host leaving keeps its own game; the member's, the host's copy, goes to the title.
    Rac2Party p;
    p.host.game.now = {mp::kPhasePlaying, 3, 2};
    run2(p.all(), 200);
    p.member.game.now = {mp::kPhasePlaying, 3, 2};
    run2(p.all(), 500);
    mp::Message end;
    end.kind = mp::Message::Kind::kPartyEnd;
    p.host.session.leave();
    p.member.session.handle(end);
    run2(p.all(), 500);
    CHECK(calls_of(p.host.game, "travel -1") == 0);
    CHECK(calls_of(p.member.game, "travel -1") >= 1);
    CHECK(p.host.said("out of the party") && p.member.said(mp::kHostLeftNotice));
}

void test_mods_differ() {
    Rac2Party p;
    mp::ModList host_mods = sample_list();
    mp::ModList member_mods = sample_list();
    member_mods.mods.clear();

    for (const auto& entry :
         {std::make_pair(&p.host, host_mods), std::make_pair(&p.member, member_mods)}) {
        Rac2Player* player = entry.first;
        const mp::ModList& list = entry.second;
        player->coop.set_build("mp " + std::to_string(mp::mods_digest(list)));
        player->mods.set_own(list);
        player->coop.set_gate([player](std::int32_t id) { return player->mods.may_play(id); });
    }

    p.host.game.now = {mp::kPhasePlaying, 3, 2};
    run2(p.all(), 2000);
    CHECK(p.host.coop.playing());
    CHECK(calls_of(p.member.game, "start 3 100") == 0);
    CHECK(p.member.said("your game differs from the host's"));
}

}  // namespace

void run_rac2_tests() {
    test_tags();
    test_settings();
    test_host_by_settings();
    test_join_by_settings();
    test_follow_the_host();
    test_other_game_and_leaving();
    test_mods_differ();
}

}  // namespace mpt
