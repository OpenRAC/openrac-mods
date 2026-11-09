/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's game in Going Commando (coop.hpp's Game): Ratchet & Clank's (game_adapter.hpp),
 * whose calls are the SDK's, with what OpenRAC does not answer for rac2 yet left at Game's "not
 * here". OpenRAC answers rac2's play calls (port/game/rac2/host/play.c): the phase, the level and
 * its loads; a start from the title (a new game or a save's bytes) and a travel; the live save; the
 * hero's pose; the figures; the card's saves; the camera (the names over the figures); the overlay
 * pause, a level's name and the mods a party agrees on. Its world (the mobys a party shares, hits,
 * kills, pickups), shared progress, scenes, shots, placing and the hero's rescue are not shared yet:
 * README.md lists them. No address of the game's: everything is the SDK's.
 */
#ifndef MULTIPLAYER_RAC2_GAME_H
#define MULTIPLAYER_RAC2_GAME_H

#include <cstdint>
#include <string>
#include <vector>

#include "game_adapter.hpp"

namespace mpmod {

class Rac2Game : public ModGame {
public:
    explicit Rac2Game(openrac::Mod& mod) : ModGame(mod) {}

    /* The port checks the save it starts (play.c's save_check): a save that is not one is refused
     * there, and play_start says so. */
    bool save_usable(const mp::Bytes& save) override { return !save.empty(); }

    // Not answered for Going Commando yet: Game's "not here". The card's party folder (the save
    // filter and notes) is Ratchet & Clank's only, so a party's game saves as the player's own does.
    bool has_card_filter() override { return mp::Game::has_card_filter(); }
    bool card_filter(int mode) override { return mp::Game::card_filter(mode); }
    void card_slot(int slot) override { mp::Game::card_slot(slot); }
    bool card_write(int slot, const mp::Bytes& save) override { return mp::Game::card_write(slot, save); }
    mp::Bytes card_read(int slot) override { return mp::Game::card_read(slot); }
    std::uint32_t card_saves() override { return mp::Game::card_saves(); }
    std::string card_note(int slot) override { return mp::Game::card_note(slot); }
    void card_note_write(int slot, const std::string& text) override { mp::Game::card_note_write(slot, text); }
    std::uint32_t sim_digest(int level) override { return mp::Game::sim_digest(level); }
    std::vector<mp::Shot> hero_shots() override { return mp::Game::hero_shots(); }
    bool figure_shot(int id, const mp::Shot& shot) override { return mp::Game::figure_shot(id, shot); }
    std::vector<mp::MobyState> mobys() override { return mp::Game::mobys(); }
    int moby_slot(int key) override { return mp::Game::moby_slot(key); }
    bool moby_drive(int slot, bool on) override { return mp::Game::moby_drive(slot, on); }
    bool moby_pose(int slot, const mp::Pose& pose) override { return mp::Game::moby_pose(slot, pose); }
    bool moby_kill(int slot) override { return mp::Game::moby_kill(slot); }
    bool moby_hide(int slot, bool hidden) override { return mp::Game::moby_hide(slot, hidden); }
    bool moby_deletion(int slot, int& by, int& by_class, bool& by_mod) override { return mp::Game::moby_deletion(slot, by, by_class, by_mod); }
    bool level_line(const float* from, const float* to, bool& blocked) override { return mp::Game::level_line(from, to, blocked); }
    bool moby_run(int slot, mp::MobyRun& out) override { return mp::Game::moby_run(slot, out); }
    int moby_spawn(int oclass, const mp::Pose& pose) override { return mp::Game::moby_spawn(oclass, pose); }
    int figure_slot(int id) override { return mp::Game::figure_slot(id); }
    bool stand_in(const float* position, int figure) override { return mp::Game::stand_in(position, figure); }
    bool can_hold() override { return mp::Game::can_hold(); }
    bool stand_in_as(const mp::StandIn& as, int figure) override { return mp::Game::stand_in_as(as, figure); }
    bool stand_in_read(mp::StandIn& out) override { return mp::Game::stand_in_read(out); }
    bool grab(int animation, std::uint32_t flags) override { return mp::Game::grab(animation, flags); }
    bool hold_at(const float* position, float yaw) override { return mp::Game::hold_at(position, yaw); }
    bool let_go() override { return mp::Game::let_go(); }
    bool hit(const mp::Hit& h) override { return mp::Game::hit(h); }
    void eyes(const std::vector<float>& xyz) override { mp::Game::eyes(xyz); }
    int bolts(int add) override { return mp::Game::bolts(add); }
    bool progress(mp::ProgressBits& out) override { return mp::Game::progress(out); }
    bool give_item(int id) override { return mp::Game::give_item(id); }
    bool give_gold_bolt(int index) override { return mp::Game::give_gold_bolt(index); }
    bool unlock_planet(int planet) override { return mp::Game::unlock_planet(planet); }
    bool shared_read(int tag, mp::Bytes& out) override { return mp::Game::shared_read(tag, out); }
    bool shared_give(int tag, const mp::Bytes& data) override { return mp::Game::shared_give(tag, data); }
    void skip() override { mp::Game::skip(); }
    bool play_scene(int scene) override { return mp::Game::play_scene(scene); }
    bool camera_view(mp::CameraView& out) override { return mp::Game::camera_view(out); }
    bool camera_play(int id) override { return mp::Game::camera_play(id); }
    bool camera_show(const float* eye, const float* rotation) override { return mp::Game::camera_show(eye, rotation); }
    bool can_place() override { return mp::Game::can_place(); }
    bool place(const float* position, float yaw, bool beside) override { return mp::Game::place(position, yaw, beside); }
    bool can_revive() override { return mp::Game::can_revive(); }
    void hold_death(bool on) override { mp::Game::hold_death(on); }
    bool revive() override { return mp::Game::revive(); }
    bool respawn() override { return mp::Game::respawn(); }
    bool ship_takeoff(int level) override { return mp::Game::ship_takeoff(level); }
    bool mission_row(int level, std::uint8_t* out) override { return mp::Game::mission_row(level, out); }
    bool mission_clear(int level, const std::uint8_t* flags) override { return mp::Game::mission_clear(level, flags); }
    bool mission_set(int level, const std::uint8_t* flags) override { return mp::Game::mission_set(level, flags); }
};

}  // namespace mpmod

#endif
