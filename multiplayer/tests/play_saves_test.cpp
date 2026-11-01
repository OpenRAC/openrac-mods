// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: the party's saves (cosave.hpp). A fake card
// stands for the port's filter (port/tests/mc_filter_test.c tests the filter itself): a save the
// game makes lands in the party's slot under the filter and in the player's own saves without it.
#include "test_support.hpp"

namespace mpt {
namespace {

// A save: bytes a test tells apart.
Bytes save_of(std::uint8_t value, std::size_t size = 59912) {
    return Bytes(size, value);
}

struct Saving {
    Player host{1}, a{2}, b{3};
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};

    explicit Saving(const Bytes& chosen = {}, bool enter = true) {
        for (Player* p : players()) {
            p->game.card_on = true;
            p->session.handle(info(all));
        }

        host.coop.choose(chosen, "A save");
        CHECK(start_ready(host));
        route(players());
        tick(players(), 16);

        if (enter) {
            in_level();
        }
    }

    void in_level() {
        for (Player* p : players()) {
            p->game.now = {mp::kPhasePlaying, 4, 2};
            p->game.live_save = save_of(static_cast<std::uint8_t>(0x10 + p->id));
        }

        tick(players(), 16);
    }

    std::vector<Player*> players() { return {&host, &a, &b}; }
};

void everyone_plays_a_copy_in_a_slot_of_its_own() {
    const Bytes chosen = save_of(0x21);
    Saving party(chosen);

    for (Player* p : party.players()) {
        CHECK(p->cosave.on() && p->game.filter == mp::kCardRedirect);
        CHECK(p->cosave.slot() == 0 && p->game.hint == 0);
        // The copy was there before the game started, the host's included.
        CHECK(p->game.slots[0] == chosen);
        CHECK(p->game.own_saves.empty());
        CHECK(p->cosave.key() == party.host.cosave.key() && p->cosave.key() != 0);
    }

    // The filter stood before the game was asked to start: nothing the game does can go first.
    CHECK(party.host.game.calls.front().rfind("start", 0) == 0);
}

void a_players_own_saves_are_never_written() {
    Saving party(save_of(0x21));

    // Every way the game saves, on every player, the host too.
    for (int i = 0; i < 5; ++i) {
        for (Player* p : party.players()) {
            p->game.game_saves(save_of(static_cast<std::uint8_t>(0x30 + i)));
        }

        tick(party.players(), 16);
    }

    for (Player* p : party.players()) {
        CHECK(p->game.own_saves.empty());
        CHECK(!p->game.slots[0].empty());
    }
}

void the_hosts_save_reaches_every_slot() {
    Saving party(save_of(0x21));
    const Bytes saved = save_of(0x41);
    party.host.game.game_saves(saved);
    tick(party.players(), 16);
    tick(party.players(), 16);

    CHECK(party.a.game.slots[0] == saved && party.b.game.slots[0] == saved);
    CHECK(party.host.cosave.sent_to() == 2 && party.host.cosave.acked() == 2);
    // Said once over each game, in plain words.
    CHECK(party.a.cosave.banner(party.a.clock) == "Party saved in Co-op 1");
    CHECK(party.a.cosave.banner(party.a.clock + mp::kCoSavedMs + 1).empty());

    // A member's own game saving stays that member's; the host's next save is everyone's again.
    party.a.game.game_saves(save_of(0x42));
    tick(party.players(), 16);
    CHECK(party.a.game.slots[0] == save_of(0x42) && party.b.game.slots[0] == saved);
    CHECK(party.host.game.slots[0] == saved);
    party.host.game.game_saves(save_of(0x43));
    tick(party.players(), 16);
    tick(party.players(), 16);
    CHECK(party.a.game.slots[0] == save_of(0x43));
}

void a_new_game_gets_its_copy_in_the_first_level() {
    Saving party({}, false);

    for (Player* p : party.players()) {
        CHECK(p->cosave.on() && p->game.slots.empty());
    }

    party.in_level();

    for (Player* p : party.players()) {
        CHECK(p->game.slots[0] == p->game.live_save);
    }
}

void a_late_joiner_gets_it_silently() {
    Saving party(save_of(0x21));
    Player late{4};
    late.game.card_on = true;
    const std::vector<mp::Member> four = {{1, "P1"}, {2, "P2"}, {3, "P3"}, {4, "P4"}};

    for (Player* p : {&party.host, &party.a, &party.b, &late}) {
        p->session.handle(info(four));
    }

    std::vector<Player*> all = {&party.host, &party.a, &party.b, &late};
    party.host.game.live_save = save_of(0x51);
    tick(all, mp::kWantMs);
    tick(all, mp::kWantMs);
    tick(all, 16);

    CHECK(late.cosave.on() && late.game.slots[0] == save_of(0x51));
    CHECK(late.cosave.key() == party.host.cosave.key());
    CHECK(late.cosave.banner(late.clock).empty());
}

// A piece of a save as the host sends it.
Bytes piece(std::uint8_t tag, int total, int index, const Bytes& bytes) {
    return mp::Writer().u8(mp::kind::kCoSave).u8(tag).i32(total).i32(index).raw(bytes).bytes();
}

void a_save_cut_short_changes_nothing() {
    Saving party(save_of(0x21));

    // The host goes after two pieces of four: the pieces are all that came.
    const Bytes chunk(mp::kChunkBytes, 0x61);
    party.a.cosave.handle(1, piece(200, 4, 0, chunk));
    party.a.cosave.handle(1, piece(200, 4, 1, chunk));
    CHECK(party.a.game.slots[0] == save_of(0x21));

    // Pieces that fit together are all it takes, whatever their order, a repeat counted once.
    party.a.cosave.handle(1, piece(201, 3, 2, Bytes(100, 0x63)));
    party.a.cosave.handle(1, piece(201, 3, 0, Bytes(mp::kChunkBytes, 0x63)));
    party.a.cosave.handle(1, piece(201, 3, 0, Bytes(mp::kChunkBytes, 0x64)));
    CHECK(party.a.game.slots[0] == save_of(0x21));
    party.a.cosave.handle(1, piece(201, 3, 1, Bytes(mp::kChunkBytes, 0x63)));
    CHECK(party.a.game.slots[0] == Bytes(2 * mp::kChunkBytes + 100, 0x63));

    // The host's next save replaces it whole.
    party.host.game.game_saves(save_of(0x62));
    tick(party.players(), 16);
    tick(party.players(), 16);
    CHECK(party.a.game.slots[0] == save_of(0x62));
}

void only_the_host_is_heard_and_only_a_save() {
    Saving party(save_of(0x21));
    const Bytes whole(mp::kChunkBytes, 0x71);

    // Another member's word, and pieces of a save no game has.
    party.a.cosave.handle(3, piece(9, 1, 0, whole));
    CHECK(party.a.game.slots[0] == save_of(0x21));
    party.a.cosave.handle(1, piece(9, 100, 0, whole));
    party.a.cosave.handle(1, piece(9, 1, 3, whole));
    party.a.cosave.handle(1, piece(9, 1, -1, whole));
    party.a.cosave.handle(1, piece(9, 2, 0, Bytes(10, 1)));
    party.a.cosave.handle(1, piece(9, 1, 0, Bytes(mp::kChunkBytes + 1, 1)));
    party.a.cosave.handle(1, piece(9, 1, 0, Bytes{}));
    CHECK(party.a.game.slots[0] == save_of(0x21));

    // A save the port would not take stays out, and the host hears it did not land.
    party.a.game.write_refused = true;
    party.host.game.game_saves(save_of(0x72));
    tick(party.players(), 16);
    tick(party.players(), 16);
    CHECK(party.a.game.slots[0] == save_of(0x21));
    CHECK(party.host.cosave.acked() == 1);
}

void every_save_for_a_slot_passes_the_check() {
    // A host's save that does not pass is kept from a slot as from the game, the member told why.
    {
        Player host{1}, member{2};
        host.game.card_on = member.game.card_on = true;
        member.game.saves_usable = false;
        const std::vector<mp::Member> two = {{1, "P1"}, {2, "P2"}};
        host.session.handle(info(two));
        member.session.handle(info(two));
        host.coop.choose(save_of(0x21), "A save");
        CHECK(start_ready(host));
        route({&host, &member});
        tick({&host, &member}, 16);
        CHECK(member.game.slots.empty() && member.game.started_save.empty());
        CHECK(member.coop.take_notice() == mp::kBadSaveMember);
    }

    // The same through COSAVE, in a game already on: the slot stays, the host hears it did not land.
    {
        Saving party(save_of(0x21));
        party.a.game.saves_usable = false;
        party.host.game.game_saves(save_of(0x75));
        tick(party.players(), 16);
        tick(party.players(), 16);
        CHECK(party.a.game.slots[0] == save_of(0x21));
        CHECK(party.b.game.slots[0] == save_of(0x75));
        CHECK(party.host.cosave.acked() == 1);
    }

    // Continue as host from a slot that does not pass: refused before anyone is told of a game.
    {
        Player host{1}, member{2};
        host.game.card_on = member.game.card_on = true;
        host.game.slots[2] = save_of(0x76);
        host.game.saves_usable = false;
        const std::vector<mp::Member> two = {{1, "P1"}, {2, "P2"}};
        host.session.handle(info(two));
        member.session.handle(info(two));
        host.cosave.choose_slot(2);
        host.coop.choose(host.game.card_read(2), "A save");
        CHECK(!start_ready(host));
        route({&host, &member});
        CHECK(host.coop.take_notice() == mp::kBadSaveHost);
        CHECK(!member.coop.playing() && host.game.calls.empty());
    }
}

void leaving_keeps_the_filter_until_the_title() {
    Saving party(save_of(0x21));
    Player& a = party.a;
    a.game.live_save = save_of(0x81);

    // The connection goes in a level: the last save, and the filter stays.
    a.session.disconnected();
    a.coop.reset();
    a.clock += 16;
    a.cosave.tick(a.clock, a.coop.playing());
    CHECK(a.game.slots[0] == save_of(0x81));
    CHECK(a.game.filter == mp::kCardRedirect && a.cosave.on());

    // The game still has the party's progress: a save of its own stays off the player's saves.
    a.game.game_saves(save_of(0x82));
    a.cosave.tick(a.clock + 16, a.coop.playing());
    CHECK(a.game.own_saves.empty() && a.game.slots[0] == save_of(0x82));

    // On its way (a load): still on. At the title the filter goes when the port can let it.
    a.game.now = {mp::kPhaseLoading, -1, 3};
    a.cosave.tick(a.clock + 32, a.coop.playing());
    CHECK(a.cosave.on());
    a.game.now = {mp::kPhaseFrontEnd, -1, 3};
    a.game.pass_refused = true;
    a.cosave.tick(a.clock + 48, a.coop.playing());
    CHECK(a.cosave.on() && a.game.filter == mp::kCardRedirect);
    a.game.pass_refused = false;
    a.cosave.tick(a.clock + 64, a.coop.playing());
    CHECK(!a.cosave.on() && a.game.filter == mp::kCardPass && a.cosave.slot() == -1);

    // The player's own game saves as ever after.
    a.game.game_saves(save_of(0x83));
    CHECK(a.game.own_saves == save_of(0x83));
}

void the_host_leaving_saves_for_everyone() {
    Saving party(save_of(0x21));
    party.host.game.live_save = save_of(0x91);
    party.host.cosave.leaving();
    route(party.players());
    CHECK(party.a.game.slots[0] == save_of(0x91) && party.b.game.slots[0] == save_of(0x91));
    CHECK(party.host.game.slots[0] == save_of(0x91));
}

void the_same_party_comes_back_to_its_slot() {
    std::uint32_t key = 0;
    std::map<int, Bytes> a_slots;
    std::map<int, std::string> a_notes;
    {
        Saving party(save_of(0x21));
        key = party.host.cosave.key();
        a_slots = party.a.game.slots;
        a_notes = party.a.game.notes;
    }

    // Another day, the member hosts from its slot and the others come with it.
    Player next{1}, friend_{2};
    next.game.card_on = friend_.game.card_on = true;
    next.clock += 5000;
    next.game.wall += 5000;
    next.game.slots = a_slots;
    next.game.notes = a_notes;
    // The friend has that party in slot 3, another party in slot 0.
    mp::CoNote other;
    other.key = 77;
    other.saved = 5;
    friend_.game.notes[0] = mp::note_text(other);
    mp::CoNote same;
    same.key = key;
    same.saved = 9;
    friend_.game.notes[3] = mp::note_text(same);
    const std::vector<mp::Member> two = {{1, "P2"}, {2, "P1"}};
    next.session.handle(info(two));
    friend_.session.handle(info(two));
    next.cosave.choose_slot(0);
    next.coop.choose(a_slots[0], "A save");
    CHECK(start_ready(next));
    route({&next, &friend_});
    tick({&next, &friend_}, 16);
    CHECK(next.cosave.key() == key && next.cosave.slot() == 0);
    CHECK(friend_.cosave.key() == key && friend_.cosave.slot() == 3);
    CHECK(friend_.game.slots[3] == a_slots[0]);
    CHECK(friend_.game.slots.count(0) == 0);
}

// Starting again is a game of its own: the last one's slot stays as that game left it, everywhere.
void starting_again_keeps_the_last_games_slot() {
    const Bytes chosen = save_of(0x21), saved = save_of(0x41);
    Saving party(chosen);
    const std::uint32_t first = party.host.cosave.key();
    party.host.game.game_saves(saved);
    tick(party.players(), 16);
    tick(party.players(), 16);
    CHECK(party.a.game.slots[0] == saved);

    party.host.game.now = {mp::kPhaseFrontEnd, -1, 3};
    tick(party.players(), 16);
    CHECK(party.host.coop.start());
    tick(party.players(), 16);
    party.a.game.now = party.b.game.now = {mp::kPhaseFrontEnd, -1, 3};
    tick(party.players(), 16);
    tick(party.players(), 16);

    CHECK(party.host.cosave.key() != first);

    for (Player* p : party.players()) {
        CHECK(p->cosave.on() && p->cosave.slot() == 1 && p->cosave.key() == party.host.cosave.key());
        CHECK(p->game.slots[0] == saved && p->game.slots[1] == chosen);
        CHECK(p->game.own_saves.empty());
    }
}

void a_member_asks_the_host_to_save() {
    Saving party(save_of(0x21));
    party.host.game.live_save = save_of(0xA1);
    CHECK(party.a.cosave.save_party());
    // Pressed again at once: nothing more.
    CHECK(!party.a.cosave.save_party());
    route(party.players());
    tick(party.players(), 16);
    CHECK(party.host.game.slots[0] == save_of(0xA1));
    CHECK(party.a.game.slots[0] == save_of(0xA1) && party.b.game.slots[0] == save_of(0xA1));
    CHECK(party.host.game.own_saves.empty());
}

void a_port_without_the_filter_plays_as_before() {
    Player host{1}, member{2};
    const std::vector<mp::Member> two = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(two));
    member.session.handle(info(two));
    host.coop.choose(save_of(0x21), "A save");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK(!host.cosave.on() && !member.cosave.on() && member.game.started_save == save_of(0x21));
    CHECK(host.cosave.where().empty());
}

void slots_are_chosen_and_notes_read_back() {
    mp::CoNote note;
    note.key = 0xDEADBEEF;
    note.saved = 1793500000;
    note.host = "Ke\nro";
    mp::CoNote back;
    CHECK(mp::parse_note(mp::note_text(note), back));
    CHECK(back.key == note.key && back.saved == note.saved && back.host == "Kero");
    CHECK(!mp::parse_note("", back) && !mp::parse_note("format=2\nkey=1\n", back));
    CHECK(!mp::parse_note("format=1\nkey=nothing\n", back));

    std::vector<mp::CoNote> notes(5);
    CHECK(mp::pick_slot(notes, 5) == 0);
    notes[0].key = 1;
    notes[0].saved = 50;
    notes[2].key = 5;
    notes[2].saved = 10;
    CHECK(mp::pick_slot(notes, 5) == 2 && mp::pick_slot(notes, 6) == 1);

    for (std::size_t i = 0; i < notes.size(); ++i) {
        notes[i].key = static_cast<std::uint32_t>(i + 1);
        notes[i].saved = 100 - static_cast<std::int64_t>(i);
    }

    // Full: the one saved longest ago.
    CHECK(mp::pick_slot(notes, 99) == 4);
    CHECK(mp::coop_row(1, "Pokitaru, 3 gold bolts", {1, 2, "Kero"}, true)
          == "Co-op 2: Pokitaru, 3 gold bolts, Kero's party");
    CHECK(mp::coop_row(0, "", {}, false) == "Co-op 1: empty");
}

}  // namespace

void run_saves_tests() {
    everyone_plays_a_copy_in_a_slot_of_its_own();
    a_players_own_saves_are_never_written();
    the_hosts_save_reaches_every_slot();
    a_new_game_gets_its_copy_in_the_first_level();
    a_late_joiner_gets_it_silently();
    a_save_cut_short_changes_nothing();
    only_the_host_is_heard_and_only_a_save();
    every_save_for_a_slot_passes_the_check();
    leaving_keeps_the_filter_until_the_title();
    the_host_leaving_saves_for_everyone();
    the_same_party_comes_back_to_its_slot();
    starting_again_keeps_the_last_games_slot();
    a_member_asks_the_host_to_save();
    a_port_without_the_filter_plays_as_before();
    slots_are_chosen_and_notes_read_back();
}

}  // namespace mpt
