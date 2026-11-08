/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's game in Going Commando (coop.hpp's Game): the play calls OpenRAC answers for
 * rac2 (port/game/rac2/host/play.c), and nothing else. The phase, the level and its loads; a start
 * from the title (a new game or a save's bytes) and a travel; the live save; the hero's pose; the
 * figures (placed and turned; they do not animate yet, the pose's animation fields go along for when
 * they do); the overlay pause and a level's name; and the mods a party agrees on. What the co-op asks
 * beyond that (the world's mobys, progress, the card's party saves, placing, scenes, shots) keeps
 * Game's answer of "not here". No address of the game's: everything is the SDK's.
 */
#ifndef MULTIPLAYER_RAC2_GAME_H
#define MULTIPLAYER_RAC2_GAME_H

#include <string>
#include <vector>

#include "game_adapter.hpp"

namespace mpmod {

class Rac2Game : public mp::Game {
public:
    explicit Rac2Game(openrac::Mod& mod) : mod_(mod), sdk_(mod) {}

    mp::GameStatus status() override { return sdk_.status(); }

    /* The port checks the save it starts (play.c's save_check): a save that is not one is refused
     * there, and play_start says so. */
    bool save_usable(const mp::Bytes& save) override { return !save.empty(); }

    bool start(int level, const mp::Bytes& save) override { return sdk_.start(level, save); }

    bool travel(int level) override { return sdk_.travel(level); }

    mp::Bytes capture() override { return sdk_.capture(); }

    bool hero(mp::Pose& out) override { return sdk_.hero(out); }

    int figure_create() override { return sdk_.figure_create(); }

    bool figure_pose(int id, const mp::Pose& pose) override { return sdk_.figure_pose(id, pose); }

    void figure_remove(int id) override { sdk_.figure_remove(id); }

    std::string level_name(int level) override { return sdk_.level_name(level); }

    void overlay_pause(bool on) override { mod_.play_overlay_pause(on); }

    /* The party's mods (party_mods.hpp): as in Ratchet & Clank. */
    mp::ModList mod_list() { return sdk_.mod_list(); }

    std::vector<mp::PartyRules> party_rules() override { return sdk_.party_rules(); }

    bool deliver_rules(const std::string& id, const mp::Bytes& data) override {
        return sdk_.deliver_rules(id, data);
    }

private:
    openrac::Mod& mod_;
    /* Ratchet & Clank's game: only its calls that are the SDK's alone are used here (none of its
     * addresses, its save check or its ship). */
    ModGame sdk_;
};

}  // namespace mpmod

#endif
