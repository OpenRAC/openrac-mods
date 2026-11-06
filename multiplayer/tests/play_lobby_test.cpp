// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Tests of the party's management: a host taking a member out, a ban of a co-op save against a
// launcher's key, and the data packs a party compares.
#include "test_support.hpp"

#include <cstring>

#include "menu.hpp"

namespace mpt {
namespace {

const std::string kKeyA = "0123456789abcdef0123456789abcdef";
const std::string kKeyB = "fedcba9876543210fedcba9876543210";

/* The server's offer, as the connection hands it on: `features` what it takes. */
void offer(Player& p, std::uint8_t features) {
    mp::Message m;
    m.kind = mp::Message::Kind::kUdp;
    m.features = features;
    p.session.handle(m);
}

void kicked(Player& p) {
    mp::Message m;
    m.kind = mp::Message::Kind::kPartyKicked;
    p.session.handle(m);
}

/* What `from` sent since the last look, as requests. */
std::vector<mp::Request> sent(Player& from) {
    from.session.flush();
    std::vector<mp::Request> out;

    for (const Bytes& frame : from.link.frames) {
        mp::FrameReader r;
        r.feed(frame.data(), frame.size());
        Bytes body;

        if (r.next(body)) {
            out.push_back(mp::decode_request(body));
        }
    }

    from.link.frames.clear();
    return out;
}

void deliver(const mp::Request& q, std::int32_t from, Player& to) {
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = from;
    m.bytes = q.bytes;
    to.session.handle(m);
}

/* A party of three playing a save, each in its co-op slot. */
struct Saving {
    Player host{1}, a{2}, b{3};
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};

    Saving() {
        for (Player* p : players()) {
            p->game.card_on = true;
            p->session.handle(info(all));
        }

        host.coop.choose(Bytes(59912, 0x21), "A save");
        CHECK(start_ready(host));
        route(players());
        tick(players(), 16);

        for (Player* p : players()) {
            p->game.now = {mp::kPhasePlaying, 4, 2};
            p->game.live_save = Bytes(59912, static_cast<std::uint8_t>(0x10 + p->id));
        }

        tick(players(), 16);
    }

    std::vector<Player*> players() { return {&host, &a, &b}; }
};

void a_kick_goes_through_the_server() {
    Player host(1), member(2), other(3);
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};

    for (Player* p : {&host, &member, &other}) {
        p->session.handle(info(all));
        offer(*p, mp::kFeatureKick);
        sent(*p);
    }

    CHECK(!member.session.kick(3));  // only the host
    CHECK(!host.session.kick(1));    // not itself
    CHECK(!host.session.kick(9));    // nobody by that id
    CHECK(sent(member).empty() && sent(host).empty());

    CHECK(host.session.kick(2, mp::KickReason::kBanned));
    CHECK(!host.session.kick(2));  // asked once
    CHECK(host.session.kicking(2) && !host.session.kicking(3));
    const std::vector<mp::Request> out = sent(host);
    CHECK(out.size() == 2);
    CHECK(out[0].kind == mp::Request::Kind::kData && out[0].target == 2);
    CHECK(out[0].bytes == (Bytes{mp::kind::kKick, 1}));
    CHECK(out[1].kind == mp::Request::Kind::kKick && out[1].target == 2);

    // The word first; the member goes when the server says, so it sends no LEAVE of its own.
    deliver(out[0], 1, member);
    CHECK(member.session.in_party() && sent(member).empty());
    kicked(member);
    CHECK(!member.session.in_party());
    CHECK(member.session.take_notice() == mp::kBannedNotice);

    // The server's LEFT: the host may be asked again for a later comer of that id.
    mp::Message left;
    left.kind = mp::Message::Kind::kPartyLeft;
    left.id = 2;
    host.session.handle(left);
    CHECK(host.session.members().size() == 2 && !host.session.kicking(2));

    // Only the host's word counts, and only a member gets it.
    deliver(out[0], 3, other);
    CHECK(other.session.in_party());
    kicked(other);
    CHECK(other.session.take_notice() == mp::kKickedNotice);
}

void a_server_without_kick_gets_no_request() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};

    for (Player* p : {&host, &member}) {
        p->session.handle(info(both));
        offer(*p, 0);
        sent(*p);
    }

    // An older server closes the connection on a request it does not know: it must not get one.
    CHECK(host.session.kick(2));
    const std::vector<mp::Request> out = sent(host);
    CHECK(out.size() == 1 && out[0].kind == mp::Request::Kind::kData);

    deliver(out[0], 1, member);
    CHECK(!member.session.in_party());
    CHECK(member.session.take_notice() == mp::kKickedNotice);
    const std::vector<mp::Request> leaving = sent(member);
    CHECK(leaving.size() == 1 && leaving[0].kind == mp::Request::Kind::kLeave);
}

void the_words_fit_their_lines() {
    for (const char* line : {mp::kRemoveLabel, mp::kBanLabel, mp::kRemoveNote, mp::kBanNote, mp::kCopyCodeLabel}) {
        CHECK(std::strlen(line) <= mp::kPauseLine);
    }

    for (const char* line : {mp::kKickedNotice, mp::kBannedNotice, mp::kBannedSent, mp::kRemoveHint}) {
        CHECK(std::strlen(line) <= mp::kFooterMax);
    }
}

void a_direct_party_always_kicks() {
    Player host(1);
    mp::Message m = info({{1, "P1"}, {2, "P2"}});
    m.code = mp::kDirectCode;
    host.session.handle(m);
    sent(host);
    CHECK(host.session.kick(2));
    const std::vector<mp::Request> out = sent(host);
    CHECK(out.size() == 2 && out[1].kind == mp::Request::Kind::kKick);
}

void the_lobby_keeps_its_kinds() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    sent(host);

    for (std::uint8_t kind = 64; kind <= 70; ++kind) {
        // Not the game's to send, and not handed to the game.
        host.session.send_data(2, {kind, 0});
        CHECK(!host.session.send_state(2, {kind, 0}));
    }

    CHECK(sent(host).empty());
    int reached = 0;
    member.session.on_data([&reached](std::int32_t, const Bytes&) { ++reached; });

    for (std::uint8_t kind = 68; kind <= 70; ++kind) {
        mp::Message m;
        m.kind = mp::Message::Kind::kData;
        m.id = 1;
        m.bytes = {kind, 1};
        member.session.handle(m);
    }

    CHECK(reached == 0 && member.session.in_party());
}

// The README's old line: one player who does not match held the host's Start until it left.
void removing_a_differing_member_lets_the_host_start() {
    Player host(1), member(2), other(3);
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    mp::ModList differs = sample_list();
    setting_of(differs, "bolts").value = "3";
    gate(host, sample_list());
    gate(member, differs);
    gate(other, sample_list());

    for (Player* p : {&host, &member, &other}) {
        p->session.handle(info(all));
        offer(*p, mp::kFeatureKick);
    }

    host.coop.choose({}, "New game");
    tick_mods({&host, &member, &other}, 16);
    tick_mods({&host, &member, &other}, 16);
    host.session.handle(ready_of(3));
    CHECK(host.mods.fit(2) == mp::Fit::kDiffers && !host.session.all_ready() && !host.coop.start());

    CHECK(host.session.kick(2));
    mp::Message left;
    left.kind = mp::Message::Kind::kPartyLeft;
    left.id = 2;
    host.session.handle(left);
    CHECK(host.session.all_ready() && host.coop.start());
}

void a_member_removed_in_the_game_goes_home_alone() {
    Party party;

    for (Player* p : party.players()) {
        offer(*p, mp::kFeatureKick);
    }

    party.a.game.now = {mp::kPhasePlaying, 4, 2};
    CHECK(party.host.session.kick(2));
    route(party.players());
    kicked(party.a);
    party.left(2);
    tick(party.players(), 16);
    CHECK(party.a.session.take_notice() == mp::kKickedNotice);
    CHECK(party.a.game.calls.size() == 1 && party.a.game.calls[0] == "travel -1");
    reach_title(party.a);
    CHECK(count_of(party.a.game, "home") == 1);
    CHECK(party.host.session.members().size() == 2 && party.b.session.in_party());
    CHECK(party.host.coop.playing() && party.b.coop.playing());
}

// A late member the host asks out before it got the game gets none, though its mods match: the host
// plays on and its capture would go to someone leaving. The same comer, not asked out, gets it.
void no_game_for_someone_being_removed() {
    const auto comes = [](bool removed) {
        Player host(1), late(2);
        const std::vector<mp::Member> alone = {{1, "P1"}};
        const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
        host.game.live_save = Bytes(100, 1);
        gate(host, sample_list());
        gate(late, sample_list());
        host.session.handle(info(alone));
        offer(host, mp::kFeatureKick);
        host.coop.choose({}, "New game");
        CHECK(host.coop.start());
        host.game.now = {mp::kPhasePlaying, 3, 2};
        tick_mods({&host}, 16);
        host.session.handle(info(both));
        late.session.handle(info(both));
        offer(late, mp::kFeatureKick);
        // Its MODS reaches the host, and the host's kick goes in before the co-op's next tick.
        late.mods.tick(late.clock);
        route({&host, &late});
        CHECK(host.mods.fit(2) == mp::Fit::kMatch && host.mods.may_play(2));

        if (removed) {
            CHECK(host.session.kick(2));
            CHECK(!host.mods.may_play(2) && host.mods.fit(2) == mp::Fit::kMatch);
        }

        const std::size_t before = late.game.calls.size();
        host.coop.tick(host.clock + 16);
        route({&host, &late});
        late.coop.tick(late.clock + 16);
        return late.game.calls.size() != before;
    };

    CHECK(comes(false));
    CHECK(!comes(true));
}

void a_members_key_reaches_the_host_alone() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    mp::ModList mine = sample_list(), theirs = sample_list();
    mine.key = kKeyA;
    theirs.key = kKeyB;
    gate(host, mine);
    gate(member, theirs);
    host.session.handle(info(both));
    member.session.handle(info(both));
    tick_mods({&host, &member}, 16);
    tick_mods({&host, &member}, 16);
    CHECK(host.mods.key(2) == kKeyB);
    // The key is not a difference, and the host's own stays with it.
    CHECK(host.mods.fit(2) == mp::Fit::kMatch);
    CHECK(member.mods.host_list().key.empty() && member.mods.key(1).empty());

    // A key that is no key does not get as far as a save's note.
    mp::ModList bad = sample_list();
    bad.key = "a=b\nban=x";
    const Bytes bytes = mp::encode_mod_list(bad);
    mp::Reader r(bytes.data(), bytes.size());
    mp::ModList back;
    CHECK(mp::decode_mod_list(r, back) && back.key.empty());
}

void a_ban_stays_with_the_save() {
    Saving party;
    Player& host = party.host;
    const auto key_of = [](std::int32_t id) { return id == 2 ? kKeyA : id == 3 ? kKeyB : std::string(); };
    CHECK(host.cosave.banned_members(key_of).empty());
    CHECK(!host.cosave.ban("not a key"));
    CHECK(!party.a.cosave.ban(kKeyB));  // only the host's
    CHECK(party.a.cosave.bans().empty());

    CHECK(host.cosave.ban(kKeyA));
    CHECK(host.cosave.ban(kKeyA) && host.cosave.bans().size() == 1);
    const std::vector<mp::Member> out = host.cosave.banned_members(key_of);
    CHECK(out.size() == 1 && out[0].id == 2);
    route(party.players());

    // Beside the save at once, in every slot: a member can host this save later.
    for (Player* p : party.players()) {
        mp::CoNote note;
        CHECK(mp::parse_note(p->game.notes[p->cosave.slot()], note));
        CHECK(note.bans == std::vector<std::string>{kKeyA});
        CHECK(p->cosave.banned(kKeyA) && !p->cosave.banned(kKeyB) && !p->cosave.banned(""));
    }

    // What the launcher reads of the note is still there.
    CHECK(host.game.notes[host.cosave.slot()].find("\nhost=P1\n") != std::string::npos);

    // Continue as host from that slot: the save's bans are back, under any name. A new game has none.
    mp::CoSave later(host.session, host.game, [] { return std::uint64_t{5}; });
    later.choose_slot(host.cosave.slot());
    CHECK(later.banned(kKeyA));
    later.choose_new();
    CHECK(!later.banned(kKeyA));

    host.cosave.lift_bans();
    route(party.players());
    CHECK(host.cosave.bans().empty() && party.a.cosave.bans().empty());
    mp::CoNote note;
    CHECK(mp::parse_note(host.game.notes[host.cosave.slot()], note) && note.bans.empty());
}

void bans_read_back_within_their_limits() {
    mp::CoNote note;
    note.key = 7;
    note.bans = {kKeyA, "not a key", kKeyB};
    mp::CoNote back;
    CHECK(mp::parse_note(mp::note_text(note), back));
    CHECK((back.bans == std::vector<std::string>{kKeyA, kKeyB}));

    std::string text = "format=1\nkey=7\n";

    for (std::size_t i = 0; i < mp::kMaxBans + 5; ++i) {
        text += "ban=a" + std::to_string(i) + "\n";
    }

    CHECK(mp::parse_note(text, back) && back.bans.size() == mp::kMaxBans);

    // Past the limit the oldest gives way.
    Saving party;
    party.host.cosave.lift_bans();

    for (std::size_t i = 0; i < mp::kMaxBans + 1; ++i) {
        CHECK(party.host.cosave.ban("a" + std::to_string(i)));
    }

    CHECK(party.host.cosave.bans().size() == mp::kMaxBans);
    CHECK(!party.host.cosave.banned("a0") && party.host.cosave.banned("a1"));
}

mp::PackInfo pack(const std::string& id, std::uint32_t digest, const std::string& version = "") {
    return {id, "Pack " + id, version, digest};
}

void data_packs_are_compared() {
    using Kind = mp::Difference::Kind;
    const mp::ModList none = sample_list();
    mp::ModList host = sample_list(), own = sample_list();
    host.packs = own.packs = {pack("a", 1), pack("b", 2)};
    CHECK(mp::compare_mods(own, host).empty());
    CHECK(mp::mods_digest(own) == mp::mods_digest(host) && mp::mods_digest(host) != mp::mods_digest(none));

    // The same packs on either disc: the world's family is what counts, and the digests agree.
    mp::ModList other = host;
    other.disc = "ntsc";
    host.disc = "pal";
    other.family = host.family = "rac1-60-1";
    CHECK(mp::compare_mods(other, host).empty() && mp::mods_digest(other) == mp::mods_digest(host));

    own.packs = {pack("a", 1)};
    std::vector<mp::Difference> d = mp::compare_mods(own, host);
    CHECK(d.size() == 1 && d[0].kind == Kind::kHostOnly && d[0].mod == "Pack b");
    CHECK(mp::refusal_lines(d)[0] == "Host has Pack b, you don't");

    own.packs = {pack("a", 1), pack("b", 2), pack("c", 3)};
    d = mp::compare_mods(own, host);
    CHECK(d.size() == 1 && d[0].kind == Kind::kYouOnly && d[0].mod == "Pack c");

    own.packs = {pack("a", 1), pack("b", 9)};
    d = mp::compare_mods(own, host);
    CHECK(d.size() == 1 && d[0].kind == Kind::kPack && mp::refusal_short(d) == "Pack b pack");
    CHECK(mp::refusal_lines(d)[0] == "Your Pack b pack differs from the host's");
    CHECK(mp::mods_digest(own) != mp::mods_digest(host));

    own.packs = {pack("a", 1, "1.1"), pack("b", 2)};
    CHECK(mp::compare_mods(own, host).size() == 1);

    // A later pack wins where two overlap: another order is another game.
    own.packs = {pack("b", 2), pack("a", 1)};
    d = mp::compare_mods(own, host);
    CHECK(d.size() == 1 && d[0].kind == Kind::kPackOrder && !mp::refusal_lines(d).empty());
    CHECK(mp::mods_digest(own) != mp::mods_digest(host));

    // On the wire, in order.
    const Bytes bytes = mp::encode_mod_list(host);
    mp::Reader r(bytes.data(), bytes.size());
    mp::ModList back;
    CHECK(mp::decode_mod_list(r, back) && back.packs.size() == 2 && back.packs[1].id == "b");
    CHECK(back.packs[1].name == "Pack b" && back.packs[1].digest == 2);

    // What the player reads of what the party plays by.
    bool named = false;

    for (const std::string& line : mp::rules_lines(host, {}, host, mp::kPauseLine)) {
        named = named || line == "Pack a";
    }

    CHECK(named);
}

void a_member_with_another_pack_is_refused() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    mp::ModList theirs = sample_list();
    theirs.packs = {pack("walls", 7)};
    gate(host, sample_list());
    gate(member, theirs);
    host.session.handle(info(both));
    member.session.handle(info(both));
    tick_mods({&host, &member}, 16);
    tick_mods({&host, &member}, 16);
    CHECK(host.mods.fit(2) == mp::Fit::kDiffers && !host.mods.may_play(2));
    CHECK(host.mods.why(2) == "extra Pack walls");
    CHECK(!member.mods.differences().empty());
}

}  // namespace

void run_lobby_tests() {
    a_kick_goes_through_the_server();
    a_server_without_kick_gets_no_request();
    a_direct_party_always_kicks();
    the_words_fit_their_lines();
    the_lobby_keeps_its_kinds();
    removing_a_differing_member_lets_the_host_start();
    a_member_removed_in_the_game_goes_home_alone();
    no_game_for_someone_being_removed();
    a_members_key_reaches_the_host_alone();
    a_ban_stays_with_the_save();
    bans_read_back_within_their_limits();
    data_packs_are_compared();
    a_member_with_another_pack_is_refused();
}

}  // namespace mpt
