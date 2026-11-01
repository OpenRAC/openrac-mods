/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's game, for the co-op (coop.hpp's Game, on interface 1.8's play calls and the
 * ones after): what each of the co-op's questions asks the SDK. Its definitions are in
 * game_adapter.cpp.
 */
#ifndef MULTIPLAYER_GAME_ADAPTER_H
#define MULTIPLAYER_GAME_ADAPTER_H

#include <map>
#include <string>
#include <vector>

#include "coop.hpp"
#include "menu.hpp"
#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"
#include "party_mods.hpp"

namespace mpmod {

/* Where each disc keeps the progress a party shares (see game_adapter.cpp). */
struct ProgressDisc;

class ModGame : public mp::Game {
public:
    /* The title's Multiplayer page (Home), once made. */
    openrac::UiPage* home = nullptr;

    explicit ModGame(openrac::Mod& mod) : mod_(mod) {}

    mp::GameStatus status() override;

    bool start(int level, const mp::Bytes& save) override;
    bool save_usable(const mp::Bytes& save) override;
    std::uint32_t sim_digest(int level) override;
    bool has_card_filter() override;
    bool card_filter(int mode) override;
    void card_slot(int slot) override;
    bool card_write(int slot, const mp::Bytes& save) override;
    mp::Bytes card_read(int slot) override;
    std::uint32_t card_saves() override;
    std::string card_note(int slot) override;
    void card_note_write(int slot, const std::string& text) override;
    std::int64_t wall_seconds() override;
#ifdef MULTIPLAYER_BOT
    bool game_save();
#endif

    bool travel(int level) override;

    mp::Bytes capture() override;

    bool hero(mp::Pose& out) override;

    int figure_create() override;

    bool figure_pose(int id, const mp::Pose& pose) override;

    std::vector<mp::Shot> hero_shots() override;

    bool figure_shot(int id, const mp::Shot& shot) override;

    void figure_remove(int id) override;

    std::vector<mp::MobyState> mobys() override;

    int moby_slot(int key) override;

    bool moby_drive(int slot, bool on) override;

    bool moby_pose(int slot, const mp::Pose& pose) override;

    bool moby_kill(int slot) override;

    bool moby_hide(int slot, bool hidden) override;

    bool moby_deletion(int slot, int& by, int& by_class, bool& by_mod) override;

    bool camera(float* xyz) override;

    bool moby_run(int slot, mp::MobyRun& out) override;

    int moby_spawn(int oclass, const mp::Pose& pose) override;

    int figure_slot(int id) override;

    bool stand_in(const float* position, int figure) override;

    bool hit(const mp::Hit& h) override;

    void eyes(const std::vector<float>& xyz) override;

    int bolts(int add) override;

    bool progress(mp::ProgressBits& out) override;

    // GiveItem(id, 0): as the game gives it, its banner and ammo, not put in hand. While quiet_, no
    // banner, and a planet with no message.
    bool give_item(int id) override;

    bool give_gold_bolt(int index) override;

    bool unlock_planet(int planet) override;

    // What the game has of a shared section, and bytes merged in (shared.hpp).
    bool shared_read(int tag, mp::Bytes& out) override;

    bool shared_give(int tag, const mp::Bytes& data) override;

    /* The mods of this run but this one, with their settings: once they have all loaded. */
    mp::ModList mod_list();

    std::vector<mp::PartyRules> party_rules() override;

    bool deliver_rules(const std::string& id, const mp::Bytes& data) override;

    void overlay_pause(bool on) override;

    void skip() override;

    bool play_scene(int scene) override;

    static openrac_moby moby_of(const mp::Pose& p);

    /* The two poses are the same fields: one after its size, the other alone. */
    static mp::Pose from_sdk(const openrac_pose& p);

    /* The pieces: the same fields, the SDK's in openrac_held. */
    static void held_from_sdk(const openrac_held& h, mp::Pose& out);

    static openrac_held held_to_sdk(const mp::Pose& p);

    static openrac_pose to_sdk(const mp::Pose& p);

    bool open_home() override;

    // --- WP-JOIN ---
    bool can_place() override;

    bool place(const float* position, float yaw, bool beside) override;

    std::string level_name(int level) override;

    void quiet_gifts(bool on) override { quiet_ = on; }
    // --- end WP-JOIN ---

    // --- WP-DEATH ---
    bool can_revive() override;

    void hold_death(bool on) override;

    bool revive() override;

    bool respawn() override;

    bool mission_row(int level, std::uint8_t* out) override;
    // --- end WP-DEATH ---

private:
    /* This disc's progress places, null for a disc it does not know. */
    const ProgressDisc* progress_disc() const;

    static void read_bytes(const openrac::Memory& memory, openrac::gaddr at, std::uint8_t* out, std::size_t n);

    openrac::Mod& mod_;
    std::map<std::string, int> indices_;
    bool quiet_ = false;  // gifts without the game's banners (WP-JOIN)
};

}  // namespace mpmod

#endif
