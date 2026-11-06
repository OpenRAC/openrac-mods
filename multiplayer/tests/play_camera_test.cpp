// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Camera scripts shown to the players near (camscripts.hpp): who sees which, what takes the camera
// back, a joiner and a party of two discs, and what a lost word does.
#include "test_support.hpp"

namespace mpt {

namespace {

constexpr int kDoor = 737;
constexpr int kLift = 877;
constexpr int kGoldBolt = 1134;
constexpr int kCannon = 440;

// A host and a member in a level, the member beside the host, a hero's health on each.
struct Pair : Party {
    Player& member = a;

    Pair() {
        for (Player* p : players()) {
            p->game.now = {mp::kPhasePlaying, 4, 2};
            p->game.hero_pose.health = 8;
        }

        tick(players(), 16);
    }

    // Time without anyone touching a pad.
    void quiet() {
        for (int i = 0; i < 40; ++i) {
            tick(players(), 16);
        }
    }

    // A script starts in the host's game, by its moby at `where`.
    void start(Player& p, int id, float x = 0.0f, bool stand_in = false) {
        p.game.script_on = true;
        p.game.script_view.where[0] = x;
        p.game.script_view.eye[2] = 50.0f;
        p.game.script_view.stand_in = stand_in;
        p.cam.script(id, true);
        tick(players(), 16);
    }

    void end(Player& p, int id) {
        p.game.script_on = false;
        p.cam.script(id, false);
        tick(players(), 16);
    }
};

void wire_round_trip() {
    mp::CamScript script;
    script.play = 7;
    script.level = 18;
    script.sequence = 0xBEEF;
    script.id = 1270;
    script.started = true;
    script.where[1] = 2.0f;
    script.eye[2] = 3.0f;
    script.rotation[0] = -1.5f;
    mp::CamScript out;
    Bytes wire = mp::encode_camscript(script);
    CHECK(wire.size() == 1 + 4 + 4 + 2 + 2 + 1 + 36);
    CHECK(mp::decode_camscript(wire, out));
    CHECK(out.play == 7 && out.level == 18 && out.sequence == 0xBEEF && out.id == 1270 && out.started);
    CHECK(out.where[1] == 2.0f && out.eye[2] == 3.0f && out.rotation[0] == -1.5f);

    script.id = -1;
    script.started = false;
    wire = mp::encode_camscript(script);
    CHECK(wire.size() == 1 + 4 + 4 + 2 + 2 + 1);
    CHECK(mp::decode_camscript(wire, out));
    CHECK(out.id == -1 && !out.started);

    mp::CamState state;
    state.sequence = 9;
    state.stamp = 123456789012LL;
    state.eye[0] = 1.0f;
    state.rotation[2] = 0.5f;
    mp::CamState back;
    wire = mp::encode_camstate(state);
    CHECK(wire.size() == 35);
    CHECK(mp::decode_camstate(wire, back));
    CHECK(back.sequence == 9 && back.stamp == state.stamp && back.eye[0] == 1.0f && back.rotation[2] == 0.5f);

    // Cut short anywhere: nothing taken.
    for (std::size_t n = 0; n < wire.size(); ++n) {
        CHECK(!mp::decode_camstate(Bytes(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(n)), back));
    }
}

void who_sees_what_goes_by_class() {
    CHECK(mp::camera_share(kDoor) == mp::CamShare::kCamNear);
    CHECK(mp::camera_share(1028) == mp::CamShare::kCamNear);
    CHECK(mp::camera_share(kLift) == mp::CamShare::kCamRiders);
    CHECK(mp::camera_share(1270) == mp::CamShare::kCamRiders);
    // The gold bolt's stays the collector's; seats, rides, machines and what opens a fight, and any
    // class not named, are nobody else's.
    for (const int own : {kGoldBolt, kCannon, 1201, 1267, 1242, 69, 1379, 713, 1109, 1448, 280, 1251, 615, 1005,
                          459, 688, 1051, 1422, 11, 0, -1}) {
        CHECK(mp::camera_share(own) == mp::CamShare::kCamOwn);
    }
}

void the_pad_counts_past_the_dead_zone() {
    std::uint8_t pad[6] = {0xFF, 0xFF, 0x7F, 0x7F, 0x7F, 0x7F};
    CHECK(!mp::pad_active(pad, 6));
    pad[4] = 0x7F + 0x2F;
    CHECK(!mp::pad_active(pad, 6));
    pad[4] = 0x7F + 0x30;
    CHECK(mp::pad_active(pad, 6));
    pad[4] = 0x7F;
    pad[3] = 0x7F - 0x30;
    CHECK(mp::pad_active(pad, 6));
    pad[3] = 0x7F;
    pad[1] = 0xFE;
    CHECK(mp::pad_active(pad, 6));
    CHECK(!mp::pad_active(pad, 5));
}

void a_door_is_shown_to_a_member_near_and_handed_back_eased() {
    Pair party;
    party.quiet();
    party.start(party.host, kDoor);
    CHECK(party.member.cam.showing());
    CHECK(party.member.game.camera_calls == std::vector<int>{kDoor});
    // The host's own runs in its game; nothing of it is asked there.
    CHECK(party.host.game.camera_calls.empty());

    for (int i = 0; i < 20; ++i) {
        party.host.game.script_view.eye[0] = static_cast<float>(i);
        tick(party.players(), 16);
    }

    CHECK(party.member.game.camera_shows > 0);
    CHECK(party.member.game.shown_eye[2] == 50.0f);
    CHECK(party.member.game.shown_eye[0] > 1.0f);
    // Behind the host's by the margin (a unit each 16 ms): at least 2 sends' worth, less a tick.
    CHECK(party.member.game.shown_eye[0] <= party.host.game.script_view.eye[0] - 4.0f);

    party.end(party.host, kDoor);
    CHECK(!party.member.cam.showing());
    CHECK(party.member.game.camera_calls.back() == mp::kCameraEnd);
}

void a_gold_bolt_or_a_seat_goes_nowhere() {
    Pair party;
    party.quiet();

    for (const int id : {kGoldBolt, kCannon}) {
        const auto sent = party.host.cam.sent();
        party.start(party.host, id);
        party.quiet();
        party.end(party.host, id);
        CHECK(party.host.cam.sent() == sent);
    }

    CHECK(party.member.game.camera_calls.empty());
}

void far_away_or_off_the_lift_keeps_the_players_camera() {
    Pair party;
    party.quiet();
    party.member.game.hero_pose.position[0] = mp::kCamNear + 1.0f;
    party.start(party.host, kDoor);
    CHECK(party.member.game.camera_calls.empty());
    party.end(party.host, kDoor);

    // On the lift: yes. Beside it: no.
    party.member.game.hero_pose.position[0] = mp::kCamRiderReach - 1.0f;
    party.start(party.host, kLift);
    CHECK(party.member.cam.showing());
    party.end(party.host, kLift);
    party.member.game.hero_pose.position[0] = mp::kCamRiderReach + 1.0f;
    const std::size_t asked = party.member.game.camera_calls.size();
    party.start(party.host, kLift);
    CHECK(party.member.game.camera_calls.size() == asked);
}

void a_player_in_the_middle_of_something_keeps_the_camera() {
    Pair party;
    party.quiet();

    // Hands on the pad.
    party.member.cam.input(party.member.clock);
    party.start(party.host, kDoor);
    CHECK(party.member.game.camera_calls.empty());
    party.end(party.host, kDoor);
    party.quiet();

    // Just hurt, and still within the time after.
    party.member.game.hero_pose.health = 7;
    tick(party.players(), 16);
    party.start(party.host, kDoor);
    CHECK(party.member.game.camera_calls.empty());
    party.end(party.host, kDoor);
    party.quiet();

    // Dead, in a scene, in a menu: nothing either.
    party.member.game.hero_pose.health = 0;
    tick(party.players(), 16);
    party.start(party.host, kDoor);
    party.end(party.host, kDoor);
    party.member.game.hero_pose.health = 7;
    party.member.game.now.phase = mp::kPhaseScene;
    party.start(party.host, kDoor);
    party.end(party.host, kDoor);
    party.member.game.now.phase = mp::kPhaseMenu;
    party.start(party.host, kDoor);
    party.end(party.host, kDoor);
    CHECK(party.member.game.camera_calls.empty());
}

void anything_pressed_or_a_hit_takes_the_camera_back_at_once() {
    Pair party;
    party.quiet();
    party.start(party.host, kDoor);
    CHECK(party.member.cam.showing());
    tick(party.players(), 16);
    party.member.cam.input(party.member.clock);
    tick(party.players(), 16);
    CHECK(!party.member.cam.showing());
    CHECK(party.member.game.camera_calls.back() == mp::kCameraCut);
    // It stays the player's: more views of the script start nothing.
    const std::size_t asked = party.member.game.camera_calls.size();
    party.quiet();
    CHECK(party.member.game.camera_calls.size() == asked);
    party.end(party.host, kDoor);
    CHECK(party.member.game.camera_calls.size() == asked);

    party.quiet();
    party.start(party.host, kDoor);
    CHECK(party.member.cam.showing());
    party.member.game.hero_pose.health = 7;
    tick(party.players(), 16);
    tick(party.players(), 16);
    CHECK(!party.member.cam.showing());
    CHECK(party.member.game.camera_calls.back() == mp::kCameraCut);
}

void a_scene_or_a_menu_takes_the_camera_back() {
    Pair party;
    party.quiet();
    party.start(party.host, kDoor);
    party.member.game.now.phase = mp::kPhaseMenu;
    tick(party.players(), 16);
    CHECK(!party.member.cam.showing());
    CHECK(party.member.game.camera_calls.back() == mp::kCameraCut);
    party.end(party.host, kDoor);

    party.member.game.now.phase = mp::kPhasePlaying;
    party.quiet();
    party.start(party.host, kDoor);
    CHECK(party.member.cam.showing());
    party.member.game.now.scene = 3;
    tick(party.players(), 16);
    CHECK(!party.member.cam.showing());
}

void the_game_taking_the_camera_ends_it_without_a_call() {
    Pair party;
    party.quiet();
    party.start(party.host, kDoor);
    party.member.game.camera_taken = true;
    tick(party.players(), 16);
    tick(party.players(), 16);
    CHECK(!party.member.cam.showing());
    CHECK(party.member.game.camera_calls == std::vector<int>{kDoor});
}

void silence_from_the_host_gives_the_camera_back() {
    Pair party;
    party.quiet();
    party.start(party.host, kDoor);
    CHECK(party.member.cam.showing());

    // The host's views stop reaching this member: its game alone goes on.
    for (int i = 0; i < 70 && party.member.cam.showing(); ++i) {
        party.member.clock += 16;
        party.member.coop.tick(party.member.clock);
        party.member.cam.tick(party.member.clock);
    }

    CHECK(!party.member.cam.showing());
    CHECK(party.member.game.camera_calls.back() == mp::kCameraCut);
}

void a_script_under_way_is_not_joined() {
    Pair party;
    party.quiet();
    party.start(party.host, kDoor);
    CHECK(party.member.cam.showing());

    // Heard only by its views: a player who came after its start shows nothing.
    Pair late;
    late.quiet();
    mp::CamState state;
    state.sequence = 1;
    state.stamp = 5000;
    late.member.cam.handle(1, mp::encode_camstate(state));
    late.quiet();
    CHECK(late.member.game.camera_calls.empty());
    CHECK(late.member.game.camera_shows == 0);
}

void a_joining_player_plays_nothing() {
    Player host{1}, member{2}, late{3};
    std::vector<Player*> all = {&host, &member, &late};
    const auto run = [&](std::uint64_t ms) {
        for (std::uint64_t t = 0; t < ms; t += 50) {
            tick(all, 50);
        }
    };
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.game.live_save = Bytes(100, 7);
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = member.game.now = {mp::kPhasePlaying, 3, 2};
    host.game.hero_pose.health = member.game.hero_pose.health = late.game.hero_pose.health = 8;
    run(1000);
    mp::Message came;
    came.kind = mp::Message::Kind::kPartyJoined;
    came.id = 3;
    came.text = "P3";
    host.session.handle(came);
    member.session.handle(came);
    late.session.handle(info({{1, "P1"}, {2, "P2"}, {3, "P3"}}));
    late.game.now = {mp::kPhaseFrontEnd, -1, 0};
    run(150);
    CHECK(late.coop.joining());
    late.game.now = {mp::kPhasePlaying, 3, 3};
    run(200);
    CHECK(late.coop.joining());
    host.game.script_on = true;
    host.cam.script(kDoor, true);
    tick(all, 16);
    CHECK(member.cam.showing());
    CHECK(late.game.camera_calls.empty());
}

void a_members_script_is_the_hosts_to_watch_too() {
    Pair party;
    party.quiet();
    // The member's stand-in set it off: the port kept the host's camera out, the view is kept.
    party.start(party.host, kDoor, 0.0f, true);
    CHECK(party.host.game.camera_calls == std::vector<int>{kDoor});
    CHECK(party.member.game.camera_calls == std::vector<int>{kDoor});
    CHECK(party.host.cam.showing() && party.member.cam.showing());

    for (int i = 0; i < 20; ++i) {
        tick(party.players(), 16);
    }

    CHECK(party.host.game.camera_shows > 0);
    CHECK(party.host.game.shown_eye[2] == 50.0f);

    // The host running from it keeps the rest of the party's.
    party.host.cam.input(party.host.clock);
    tick(party.players(), 16);
    CHECK(!party.host.cam.showing());
    CHECK(party.host.game.camera_calls.back() == mp::kCameraCut);
    CHECK(party.member.cam.showing());

    party.end(party.host, kDoor);
    CHECK(!party.member.cam.showing());
    CHECK(party.member.game.camera_calls.back() == mp::kCameraEnd);
}

void a_members_script_leaves_a_busy_host_alone() {
    Pair party;
    party.quiet();
    party.host.cam.input(party.host.clock);
    party.start(party.host, kDoor, 0.0f, true);
    CHECK(party.host.game.camera_calls.empty());
    CHECK(party.member.cam.showing());
}

void a_cut_in_the_script_is_not_a_sweep() {
    Pair party;
    party.quiet();
    party.start(party.host, kDoor);
    bool between = false;

    for (int i = 0; i < 40; ++i) {
        party.host.game.script_view.eye[0] = i < 20 ? 0.0f : 500.0f;
        tick(party.players(), 16);
        const float x = party.member.game.shown_eye[0];
        between = between || (x > 0.0f && x < 500.0f);
    }

    CHECK(!between);
    CHECK(party.member.game.shown_eye[0] == 500.0f);
}

void an_angle_goes_the_short_way_round() {
    // Across the half turn, either way.
    for (const float from : {3.1f, -3.1f}) {
        Pair party;
        party.quiet();
        party.host.game.script_view.rotation[2] = from;
        party.start(party.host, kDoor);
        bool the_long_way = false;

        for (int i = 0; i < 40; ++i) {
            party.host.game.script_view.rotation[2] = i < 20 ? from : -from;
            tick(party.players(), 16);
            const float z = party.member.game.shown_rotation[2];
            the_long_way = the_long_way || (z > -3.0f && z < 3.0f);
        }

        CHECK(!the_long_way);
    }
}

void a_lost_view_goes_on_from_the_ones_that_came() {
    Pair party;
    party.quiet();
    party.start(party.host, kDoor);

    // A view every so often only (as over a bad link): what is shown keeps moving, the end arrives.
    for (int i = 0; i < 60; ++i) {
        party.host.game.script_view.eye[0] = static_cast<float>(i) * 0.2f;
        const bool lost = i % 3 != 0;

        if (lost) {
            party.host.game.script_on = false;
        }

        tick(party.players(), 16);
        party.host.game.script_on = true;
    }

    CHECK(party.member.cam.showing());
    CHECK(party.member.game.shown_eye[0] > 4.0f);
    party.end(party.host, kDoor);
    CHECK(!party.member.cam.showing());
}

void another_play_is_ignored() {
    Pair party;
    party.quiet();
    mp::CamScript word;
    word.play = party.member.coop.play() + 1;
    word.level = 4;
    word.sequence = 1;
    word.id = kDoor;
    word.started = true;
    party.member.cam.handle(1, mp::encode_camscript(word));
    CHECK(party.member.game.camera_calls.empty());
    // And a member's word of one: only the host tells.
    word.play = party.member.coop.play();
    party.member.cam.handle(2, mp::encode_camscript(word));
    CHECK(party.member.game.camera_calls.empty());
}

void a_party_gone_gives_the_camera_back() {
    Pair party;
    party.quiet();
    party.start(party.host, kDoor);
    CHECK(party.member.cam.showing());
    party.member.cam.reset();
    CHECK(!party.member.cam.showing());
    CHECK(party.member.game.camera_calls.back() == mp::kCameraCut);
}

}  // namespace

void run_camera_tests() {
    wire_round_trip();
    who_sees_what_goes_by_class();
    the_pad_counts_past_the_dead_zone();
    a_door_is_shown_to_a_member_near_and_handed_back_eased();
    a_gold_bolt_or_a_seat_goes_nowhere();
    far_away_or_off_the_lift_keeps_the_players_camera();
    a_player_in_the_middle_of_something_keeps_the_camera();
    anything_pressed_or_a_hit_takes_the_camera_back_at_once();
    a_scene_or_a_menu_takes_the_camera_back();
    the_game_taking_the_camera_ends_it_without_a_call();
    silence_from_the_host_gives_the_camera_back();
    a_script_under_way_is_not_joined();
    a_joining_player_plays_nothing();
    a_members_script_is_the_hosts_to_watch_too();
    a_members_script_leaves_a_busy_host_alone();
    a_cut_in_the_script_is_not_a_sweep();
    an_angle_goes_the_short_way_round();
    a_lost_view_goes_on_from_the_ones_that_came();
    another_play_is_ignored();
    a_party_gone_gives_the_camera_back();
}

}  // namespace mpt
