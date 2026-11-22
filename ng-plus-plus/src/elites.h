// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Elite enemies and the challenge modes' modifiers (README.md, "Elites and modifiers"). From the
// third challenge mode a share of the enemy copies (extra_enemies.h) are elites, and in the
// arenas of Maktar and Joba every fourth enemy their waves spawn: drawn in obsidian (glossy black
// with a violet glow; OpenRAC 1.24, energy violet on 1.25 without it), with kEliteHealth times the
// health, kEliteDrops times the bolts and kEliteExperience times the weapon experience. From the
// second challenge mode each run has a modifier (perk_rules.h's modifier_of), named on screen as
// a level loads.
//
// Only the game's thread uses it; in a party's host only (a guest's copies are the host's).

#pragma once

#include <cstdint>
#include <unordered_map>

#include "bolt_api.h"
#include "game_view.h"
#include "openrac/mod.hpp"
#include "perk_rules.h"

namespace ng_plus_plus {

// The elites and the run's modifier.
class Elites {
public:
    // Makes elites in `view`'s game when `elites` (the "elite_enemies" setting), with the run's
    // modifiers when `modifiers` (the "challenge_modifiers" setting).
    Elites(openrac::Mod& mod, const GameView& view, bool elites, bool modifiers)
        : m_mod(mod),
          m_view(view),
          m_on(elites),
          m_modifiers(modifiers) {}

    // Finds moby_material.
    void start();

    // Returns the run's modifier now (none with the setting off).
    Modifier modifier() const;

    // A copy made of the enemy in `slot` (its `index`th), now at `copy` (its address): an elite at
    // the run's share.
    void copy_made(int slot, int index, std::uint32_t copy);

    // A spawned enemy seen for the first time in an arena level: every kArenaEliteEvery-th one is
    // an elite.
    void arena_spawn(std::uint32_t moby);

    // Returns whether the moby at `moby` is an elite now.
    bool is_elite(std::uint32_t moby) const;

    // An elite's health, drops and experience times an ordinary enemy's, 1 for any other moby.
    float health(std::uint32_t moby) const { return is_elite(moby) ? kEliteHealth : 1.0f; }

    float drops(std::uint32_t moby) const;

    float experience(std::uint32_t moby) const { return is_elite(moby) ? kEliteExperience : 1.0f; }

    // Draws `moby` as an elite again (after the Ice Gun's glow left it).
    void redraw(std::uint32_t moby) const;

    // A moby's death: it is an elite no more.
    void died(std::uint32_t moby);

    // Once a game tick: a new level forgets the elites, and names the run's modifier once.
    void tick();

private:
    // Takes level `level` in: a new one forgets the elites and names the run's modifier.
    void enter(int level);

    // Marks `moby` an elite and draws it so.
    void mark(std::uint32_t moby);

    openrac::Mod& m_mod;
    const GameView& m_view;
    bool m_on;
    bool m_modifiers;
    MobyMaterialFn m_material = nullptr;
    bool m_obsidian = false;
    int m_level = -1;
    int m_arena_seen = 0;
    int m_named_cycle = -1;
    std::unordered_map<std::uint32_t, int> m_elites;  // Address to class.
};

}  // namespace ng_plus_plus
